/**
 * @file log_kit.cc
 * @brief log_kit 核心实现
 *
 * @details 包含日志模块管理、缓冲区格式化输出、输出重定向以及
 *          Unix Domain Socket 控制服务器的实现。
 */

#include <fcntl.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "log_cmd.h"
#include "log_guard.h"
#include "log_internal.h"

namespace log_kit
{

// ===== 全局实例 =====

static LogContext g_ctx;
static SocketServer g_socket;

// ===== 内部辅助函数 =====

bool ParseLevel(const char *str, LogLevel *out)
{
    int val = atoi(str);
    if (val >= 0 && val <= 5)
    {
        *out = static_cast<LogLevel>(val);
        return true;
    }

    if (strcasecmp(str, "trace") == 0)
    {
        *out = LogLevel::kTrace;
        return true;
    }
    if (strcasecmp(str, "debug") == 0)
    {
        *out = LogLevel::kDebug;
        return true;
    }
    if (strcasecmp(str, "info") == 0)
    {
        *out = LogLevel::kInfo;
        return true;
    }
    if (strcasecmp(str, "warn") == 0)
    {
        *out = LogLevel::kWarn;
        return true;
    }
    if (strcasecmp(str, "error") == 0)
    {
        *out = LogLevel::kError;
        return true;
    }
    if (strcasecmp(str, "fatal") == 0)
    {
        *out = LogLevel::kFatal;
        return true;
    }

    return false;
}

// ===== SocketServer 实现 =====

bool SocketServer::Start(const char *socket_path)
{
    if (running_)
    {
        fprintf(stderr, "[log_kit] ERROR: Socket 服务器已在运行\n");
        return false;
    }

    const char *path = socket_path ? socket_path : kDefaultSocketPath;
    strncpy(socket_path_, path, sizeof(socket_path_) - 1);
    socket_path_[sizeof(socket_path_) - 1] = '\0';

    unlink(socket_path_);

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        perror("[log_kit] socket");
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("[log_kit] bind");
        CloseListenFd();
        return false;
    }

    if (listen(listen_fd_, 5) < 0)
    {
        perror("[log_kit] listen");
        CloseListenFd();
        unlink(socket_path_);
        return false;
    }

    running_ = true;
    if (pthread_create(&thread_, nullptr, ThreadFunc, this) != 0)
    {
        perror("[log_kit] pthread_create");
        CloseListenFd();
        unlink(socket_path_);
        running_ = false;
        return false;
    }

    thread_started_ = true;
    return true;
}

void SocketServer::Stop()
{
    if (!running_ && !thread_started_) return;

    running_ = false;

    if (thread_started_)
    {
        pthread_join(thread_, nullptr);
        thread_started_ = false;
    }

    CloseListenFd();

    if (socket_path_[0] != '\0')
    {
        unlink(socket_path_);
        socket_path_[0] = '\0';
    }
}

void *SocketServer::ThreadFunc(void *arg)
{
    auto *self = static_cast<SocketServer *>(arg);
    self->RunLoop();
    return nullptr;
}

void SocketServer::RunLoop()
{
    while (running_)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd_, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(listen_fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) continue;

        HandleClient(client_fd);
        ::close(client_fd);
    }
}

void SocketServer::HandleClient(int client_fd)
{
    char buf[kSockBufSize];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;

    buf[n] = '\0';

    size_t cmd_len = strlen(buf);
    while (cmd_len > 0 && (buf[cmd_len - 1] == '\n' || buf[cmd_len - 1] == '\r')) buf[--cmd_len] = '\0';

    if (cmd_len == 0) return;

    char response[kSockRespSize];
    int rlen = HandleCommand(buf, response, sizeof(response));
    if (rlen > 0)
    {
        ssize_t written = 0;
        while (written < rlen)
        {
            ssize_t w = write(client_fd, response + written, (size_t)(rlen - written));
            if (w <= 0) break;
            written += w;
        }
    }
}

int SocketServer::HandleCommand(const char *cmd, char *response, int response_size)
{
    int len = 0;
    CmdType type = ParseCmdType(cmd);

    switch (type)
    {
        case CmdType::kLevel:
        {
            int module_id = 0;
            char level_str[32] = {0};
            if (sscanf(cmd + 6, "%d %31s", &module_id, level_str) == 2)
            {
                LogLevel level;
                if (ParseLevel(level_str, &level))
                {
                    LockGuard lock(g_ctx.mutex);
                    if (module_id == 0)
                    {
                        for (auto &m : g_ctx.modules)
                            if (m.active) m.level = level;
                        len = snprintf(response, (size_t)response_size, kRespOkAll, level_str);
                    }
                    else if (g_ctx.ModuleIdToIndex(module_id) >= 0)
                    {
                        g_ctx.modules[g_ctx.ModuleIdToIndex(module_id)].level = level;
                        len = snprintf(response, (size_t)response_size, kRespOkOne, module_id, level_str);
                    }
                    else
                    {
                        len = snprintf(response, (size_t)response_size, kRespErrInvalidModule, module_id);
                    }
                }
                else
                {
                    len = snprintf(response, (size_t)response_size, kRespErrInvalidLevel, level_str);
                }
            }
            else
            {
                len = snprintf(response, (size_t)response_size, "%s", kRespErrUsage);
            }
            break;
        }

        case CmdType::kList:
        {
            len = snprintf(response, (size_t)response_size, "%s", kRespListHeader);
            for (int i = 0; i < g_ctx.module_count; ++i)
            {
                if (!g_ctx.modules[i].active) continue;
                len +=
                    snprintf(response + len, (size_t)(response_size - len), kRespListFormat, i + 1,
                             g_ctx.modules[i].name ? g_ctx.modules[i].name : "?", LogLevelStr(g_ctx.modules[i].level));
            }
            break;
        }

        case CmdType::kQuit:
        {
            len = snprintf(response, (size_t)response_size, "%s", kRespOkQuit);
            running_ = false;
            break;
        }

        case CmdType::kUnknown:
        default:
        {
            len = snprintf(response, (size_t)response_size, kRespErrUnknown, cmd);
            break;
        }
    }

    return len;
}

void SocketServer::CloseListenFd()
{
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

// ===== 公共 API =====

int LogRegister(const char *name)
{
    LockGuard lock(g_ctx.mutex);
    g_ctx.EnsureInit();

    if (g_ctx.module_count >= kMaxModules)
    {
        fprintf(stderr, "[log_kit] ERROR: 模块数已达上限 (%d)\n", kMaxModules);
        return -1;
    }

    int idx = g_ctx.module_count++;
    g_ctx.modules[idx].name = name;
    g_ctx.modules[idx].level = LogLevel::kInfo;
    g_ctx.modules[idx].fd = kDefaultFd;
    g_ctx.modules[idx].saved_fd = -1;
    g_ctx.modules[idx].active = true;

    return idx + 1;
}

void LogSetLevel(int module_id, LogLevel level)
{
    LockGuard lock(g_ctx.mutex);

    if (module_id == 0)
    {
        for (auto &m : g_ctx.modules)
            if (m.active) m.level = level;
        return;
    }

    int idx = g_ctx.ModuleIdToIndex(module_id);
    if (idx >= 0) g_ctx.modules[idx].level = level;
}

LogLevel LogGetLevel(int module_id)
{
    LockGuard lock(g_ctx.mutex);
    int idx = g_ctx.ModuleIdToIndex(module_id);
    if (idx < 0) return LogLevel::kInfo;
    return g_ctx.modules[idx].level;
}

int LogSetOutputFile(int module_id, const char *path)
{
    LockGuard lock(g_ctx.mutex);

    int idx = g_ctx.ModuleIdToIndex(module_id);
    if (idx < 0) return -1;

    if (!path)
    {
        if (g_ctx.modules[idx].fd != kDefaultFd && g_ctx.modules[idx].saved_fd >= 0)
        {
            ::close(g_ctx.modules[idx].fd);
            g_ctx.modules[idx].fd = g_ctx.modules[idx].saved_fd;
            g_ctx.modules[idx].saved_fd = -1;
        }
        else
        {
            g_ctx.modules[idx].fd = kDefaultFd;
        }
        return 0;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;

    g_ctx.modules[idx].saved_fd = g_ctx.modules[idx].fd;
    g_ctx.modules[idx].fd = fd;

    return 0;
}

void LogResetOutput(int module_id)
{
    LogSetOutputFile(module_id, nullptr);
}

const char *LogLevelStr(LogLevel level)
{
    switch (level)
    {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO ";
        case LogLevel::kWarn:
            return "WARN ";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kFatal:
            return "FATAL";
        default:
            return "?????";
    }
}

int LogGetModuleCount()
{
    LockGuard lock(g_ctx.mutex);
    return g_ctx.module_count;
}

const char *LogGetModuleName(int module_id)
{
    LockGuard lock(g_ctx.mutex);
    int idx = g_ctx.ModuleIdToIndex(module_id);
    if (idx < 0) return "unknown";
    return g_ctx.modules[idx].name ? g_ctx.modules[idx].name : "unknown";
}

void LogWrite(int module_id, LogLevel level, const char *file, const char *func, int line, const char *fmt, ...)
{
    int fd = kDefaultFd;
    LogLevel current_level = LogLevel::kInfo;
    {
        LockGuard lock(g_ctx.mutex);
        int idx = g_ctx.ModuleIdToIndex(module_id);
        if (idx < 0) return;
        current_level = g_ctx.modules[idx].level;
        fd = g_ctx.modules[idx].fd;
    }

    if (current_level > level) return;

    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    char buf[kLogBufSize];
    int pos = 0;

    pos += snprintf(buf + pos, (size_t)(kLogBufSize - pos), "[%s] [%-5s] [%s:%s:%d] ", time_buf, LogLevelStr(level),
                    filename, func, line);

    va_list args;
    va_start(args, fmt);
    pos += vsnprintf(buf + pos, (size_t)(kLogBufSize - pos), fmt, args);
    va_end(args);

    if (pos < kLogBufSize - 1)
        buf[pos++] = '\n';
    else
        buf[kLogBufSize - 2] = '\n';

    ssize_t written = 0;
    while (written < pos)
    {
        ssize_t w = write(fd, buf + written, (size_t)(pos - written));
        if (w <= 0) break;
        written += w;
    }
}

// ===== Socket 服务器 API =====

int LogStartSocket(const char *socket_path)
{
    return g_socket.Start(socket_path) ? 0 : -1;
}

void LogStopSocket()
{
    g_socket.Stop();
}

}  // namespace log_kit
