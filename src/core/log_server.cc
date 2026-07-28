/**
 * @file log_server.cc
 * @brief log_kit Socket 控制服务器实现
 *
 * @details 提供运行时远程日志级别控制能力，
 *          支持 log_tool 客户端通过 Socket 发送命令。
 */

#include "log_server.h"

#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../utils/log_cmd.h"
#include "log_context.h"

namespace log_kit
{

// ===== 内部辅助函数 =====

static bool ParseLevelStr(const char *str, log_kit_level_t *out)
{
    int val = atoi(str);
    if (val >= 0 && val <= 5)
    {
        *out = static_cast<log_kit_level_t>(val);
        return true;
    }

    if (strcasecmp(str, "trace") == 0)
    {
        *out = LOG_KIT_TRACE;
        return true;
    }
    if (strcasecmp(str, "debug") == 0)
    {
        *out = LOG_KIT_DEBUG;
        return true;
    }
    if (strcasecmp(str, "info") == 0)
    {
        *out = LOG_KIT_INFO;
        return true;
    }
    if (strcasecmp(str, "warn") == 0)
    {
        *out = LOG_KIT_WARN;
        return true;
    }
    if (strcasecmp(str, "error") == 0)
    {
        *out = LOG_KIT_ERROR;
        return true;
    }
    if (strcasecmp(str, "fatal") == 0)
    {
        *out = LOG_KIT_FATAL;
        return true;
    }

    return false;
}

// ===== SocketServer 实现 =====

bool SocketServer::Start(const char *socket_path)
{
    if (running_.load(std::memory_order_acquire))
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

    running_.store(true, std::memory_order_release);
    if (pthread_create(&thread_, nullptr, ThreadFunc, this) != 0)
    {
        perror("[log_kit] pthread_create");
        CloseListenFd();
        unlink(socket_path_);
        running_.store(false, std::memory_order_release);
        return false;
    }

    thread_started_.store(true, std::memory_order_release);
    return true;
}

void SocketServer::Stop()
{
    if (!running_.load(std::memory_order_acquire) && !thread_started_.load(std::memory_order_acquire))
    {
        return;
    }

    running_.store(false, std::memory_order_release);

    if (thread_started_.load(std::memory_order_acquire))
    {
        pthread_join(thread_, nullptr);
        thread_started_.store(false, std::memory_order_release);
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
    auto &ctx = LogContext::getInstance();

    while (running_.load(std::memory_order_acquire))
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd_, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(listen_fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0)
        {
            continue;
        }

        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            continue;
        }

        HandleClient(client_fd);
        ::close(client_fd);
    }
}

void SocketServer::HandleClient(int client_fd)
{
    char buf[kSockBufSize];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        return;
    }

    buf[n] = '\0';

    size_t cmd_len = strlen(buf);
    while (cmd_len > 0 && (buf[cmd_len - 1] == '\n' || buf[cmd_len - 1] == '\r'))
    {
        buf[--cmd_len] = '\0';
    }

    if (cmd_len == 0)
    {
        return;
    }

    char response[kSockRespSize];
    int rlen = HandleCommand(buf, response, sizeof(response));
    if (rlen > 0)
    {
        ssize_t written = 0;
        while (written < rlen)
        {
            ssize_t w = write(client_fd, response + written, (size_t)(rlen - written));
            if (w <= 0)
            {
                break;
            }
            written += w;
        }
    }
}

int SocketServer::HandleCommand(const char *cmd, char *response, int response_size)
{
    int len = 0;
    CmdType type = ParseCmdType(cmd);
    auto &ctx = LogContext::getInstance();

    switch (type)
    {
        case CmdType::kLevel:
        {
            int module_id = 0;
            char level_str[32] = {0};
            if (sscanf(cmd + 6, "%d %31s", &module_id, level_str) == 2)
            {
                log_kit_level_t level;
                if (ParseLevelStr(level_str, &level))
                {
                    if (module_id == 0)
                    {
                        int count = ctx.ModuleCount();
                        for (int i = 0; i < count; ++i)
                        {
                            auto &m = ctx.ModuleAt(i);
                            if (m.active.load(std::memory_order_acquire))
                            {
                                m.level.store(level, std::memory_order_relaxed);
                            }
                        }
                        len = snprintf(response, (size_t)response_size, kRespOkAll, level_str);
                    }
                    else
                    {
                        int idx = ctx.ModuleIdToIndex(module_id);
                        if (idx >= 0)
                        {
                            ctx.ModuleAt(idx).level.store(level, std::memory_order_relaxed);
                            len = snprintf(response, (size_t)response_size, kRespOkOne, module_id, level_str);
                        }
                        else
                        {
                            len = snprintf(response, (size_t)response_size, kRespErrInvalidModule, module_id);
                        }
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
            int ret = snprintf(response, (size_t)response_size, "%s", kRespListHeader);
            if (ret < 0 || ret >= response_size)
            {
                break;
            }
            len = ret;
            int count = ctx.ModuleCount();
            for (int i = 0; i < count; ++i)
            {
                auto &m = ctx.ModuleAt(i);
                if (!m.active.load(std::memory_order_acquire))
                {
                    continue;
                }
                std::lock_guard<std::mutex> lock(m.mtx);
                ret = snprintf(response + len, (size_t)(response_size - len), kRespListFormat, i + 1,
                               m.name ? m.name : "?", log_kit_level_str(m.level.load(std::memory_order_relaxed)));
                if (ret < 0 || len + ret >= response_size)
                {
                    break;
                }
                len += ret;
            }
            break;
        }

        case CmdType::kQuit:
        {
            len = snprintf(response, (size_t)response_size, "%s", kRespOkQuit);
            running_.store(false, std::memory_order_release);
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

}  // namespace log_kit
