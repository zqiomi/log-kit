/**
 * @file log_kit.cc
 * @brief log_kit 核心实现
 *
 * @details 包含日志模块管理、缓冲区格式化输出、输出重定向以及
 *          Unix Domain Socket 控制服务器的实现。
 */

#include "log_kit.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace log_kit
{

// ===== 内部常量 =====

/** @brief 最大模块数 */
static const int kMaxModules = 64;

/** @brief 默认输出文件描述符 */
static const int kDefaultFd = STDERR_FILENO;

/** @brief 日志缓冲区大小 */
static const int kLogBufSize = 2048;

/** @brief Socket 接收缓冲区大小 */
static const int kSockBufSize = 512;

/** @brief Socket 响应缓冲区大小 */
static const int kSockRespSize = 1024;

#ifdef LOG_KIT_DEFAULT_SOCKET_PATH
/** @brief 编译时默认 Socket 路径 */
static const char* kDefaultSocketPath = LOG_KIT_DEFAULT_SOCKET_PATH;
#else
static const char* kDefaultSocketPath = "/tmp/log_kit.sock";
#endif

// ===== 模块信息 =====

/**
 * @struct ModuleInfo
 * @brief 模块内部状态
 */
struct ModuleInfo
{
    const char* name;     ///< 模块名称
    LogLevel level;       ///< 当前日志级别
    int fd;               ///< 当前输出 fd
    int saved_fd;         ///< 保存的原始 stderr fd（用于恢复）
    bool active;          ///< 是否已注册激活
};

// ===== 全局状态 =====

/** @brief 模块数组（索引 0 对应模块 ID 1） */
static ModuleInfo g_modules[kMaxModules];

/** @brief 已注册模块计数 */
static int g_module_count = 0;

/** @brief 是否已初始化 */
static bool g_initialized = false;

/** @brief 级别设置互斥锁（保护 socket 线程与主线程的并发写入） */
static pthread_mutex_t g_level_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===== Socket 服务器状态 =====

/**
 * @struct SocketState
 * @brief Socket 服务器内部状态
 */
struct SocketState
{
    int listen_fd;          ///< 监听 socket fd
    bool running;           ///< 运行标志
    pthread_t thread;       ///< 后台线程
    bool thread_started;    ///< 线程是否已启动
    char socket_path[256];  ///< Socket 文件路径
};

static SocketState g_socket = {-1, false, 0, false, {0}};

// ===== 内部函数 =====

/**
 * @brief 确保全局状态已初始化
 */
static void EnsureInitialized()
{
    if (g_initialized) return;

    for (int i = 0; i < kMaxModules; ++i)
    {
        g_modules[i].name = nullptr;
        g_modules[i].level = LogLevel::kInfo;
        g_modules[i].fd = kDefaultFd;
        g_modules[i].saved_fd = -1;
        g_modules[i].active = false;
    }

    g_module_count = 0;
    g_initialized = true;
}

/**
 * @brief 将模块 ID 转换为内部数组索引
 *
 * @param module_id 模块 ID（从 1 开始）
 * @return 数组索引（从 0 开始），无效 ID 返回 -1
 */
static int ModuleIdToIndex(int module_id)
{
    int idx = module_id - 1;
    if (idx < 0 || idx >= g_module_count || !g_modules[idx].active)
    {
        return -1;
    }
    return idx;
}

/**
 * @brief 解析日志级别字符串
 *
 * @param str 级别字符串（数字或名称）
 * @param out 输出解析后的 LogLevel
 * @return true 解析成功，false 解析失败
 */
static bool ParseLevel(const char* str, LogLevel* out)
{
    int val = atoi(str);
    if (val >= 0 && val <= 5)
    {
        *out = static_cast<LogLevel>(val);
        return true;
    }

    if (strcasecmp(str, "trace") == 0) { *out = LogLevel::kTrace; return true; }
    if (strcasecmp(str, "debug") == 0) { *out = LogLevel::kDebug; return true; }
    if (strcasecmp(str, "info") == 0)  { *out = LogLevel::kInfo;  return true; }
    if (strcasecmp(str, "warn") == 0)  { *out = LogLevel::kWarn;  return true; }
    if (strcasecmp(str, "error") == 0) { *out = LogLevel::kError; return true; }
    if (strcasecmp(str, "fatal") == 0) { *out = LogLevel::kFatal; return true; }

    return false;
}

/**
 * @brief 处理 Socket 客户端命令
 *
 * @param cmd 命令字符串
 * @param response 响应缓冲区
 * @param response_size 缓冲区大小
 * @return 响应数据长度
 */
static int HandleSocketCommand(const char* cmd, char* response, int response_size)
{
    int len = 0;

    if (strncmp(cmd, "level ", 6) == 0)
    {
        int module_id = 0;
        char level_str[32] = {0};
        if (sscanf(cmd + 6, "%d %31s", &module_id, level_str) == 2)
        {
            LogLevel level;
            if (ParseLevel(level_str, &level))
            {
                pthread_mutex_lock(&g_level_mutex);
                if (module_id == 0)
                {
                    LogSetLevel(0, level);
                    pthread_mutex_unlock(&g_level_mutex);
                    len = snprintf(response, (size_t)response_size,
                                   "OK: 所有模块级别已设置为 %s\n", level_str);
                }
                else if (ModuleIdToIndex(module_id) >= 0)
                {
                    LogSetLevel(module_id, level);
                    pthread_mutex_unlock(&g_level_mutex);
                    len = snprintf(response, (size_t)response_size,
                                   "OK: 模块 %d 级别已设置为 %s\n", module_id, level_str);
                }
                else
                {
                    pthread_mutex_unlock(&g_level_mutex);
                    len = snprintf(response, (size_t)response_size,
                                   "ERROR: 无效的模块 ID %d\n", module_id);
                }
            }
            else
            {
                len = snprintf(response, (size_t)response_size,
                               "ERROR: 无效的级别 '%s'\n", level_str);
            }
        }
        else
        {
            len = snprintf(response, (size_t)response_size,
                           "ERROR: 用法: level <module_id> <level>\n");
        }
    }
    else if (strncmp(cmd, "list", 4) == 0)
    {
        len = snprintf(response, (size_t)response_size, "模块列表:\n");
        for (int i = 0; i < g_module_count; ++i)
        {
            if (!g_modules[i].active) continue;
            LogLevel lvl = g_modules[i].level;
            len += snprintf(response + len, (size_t)(response_size - len),
                            "  [%d] %-16s 级别=%s\n",
                            i + 1, g_modules[i].name ? g_modules[i].name : "?",
                            LogLevelStr(lvl));
        }
    }
    else if (strncmp(cmd, "quit", 4) == 0)
    {
        len = snprintf(response, (size_t)response_size, "OK: 正在关闭\n");
        g_socket.running = false;
    }
    else
    {
        len = snprintf(response, (size_t)response_size,
                       "ERROR: 未知命令 '%s'\n", cmd);
    }

    return len;
}

/**
 * @brief Socket 服务器后台线程
 *
 * @param arg 未使用
 * @return nullptr
 */
static void* SocketThreadFunc(void* /*arg*/)
{
    while (g_socket.running)
    {
        // 使用 poll 超时等待连接，以便检查 running 标志
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_socket.listen_fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(g_socket.listen_fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_socket.listen_fd,
                               (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        // 读取命令
        char buf[kSockBufSize];
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = '\0';

            // 去掉末尾换行
            size_t cmd_len = strlen(buf);
            while (cmd_len > 0 && (buf[cmd_len - 1] == '\n' || buf[cmd_len - 1] == '\r'))
            {
                buf[--cmd_len] = '\0';
            }

            if (cmd_len > 0)
            {
                char response[kSockRespSize];
                int rlen = HandleSocketCommand(buf, response, sizeof(response));
                if (rlen > 0)
                {
                    // 写入响应
                    ssize_t written = 0;
                    while (written < rlen)
                    {
                        ssize_t w = write(client_fd, response + written,
                                          (size_t)(rlen - written));
                        if (w <= 0) break;
                        written += w;
                    }
                }
            }
        }

        close(client_fd);
    }

    return nullptr;
}

// ===== 公共 API =====

int LogRegister(const char* name)
{
    EnsureInitialized();

    if (g_module_count >= kMaxModules)
    {
        fprintf(stderr, "[log_kit] ERROR: 模块数已达上限 (%d)\n", kMaxModules);
        return -1;
    }

    int idx = g_module_count++;
    g_modules[idx].name = name;
    g_modules[idx].level = LogLevel::kInfo;
    g_modules[idx].fd = kDefaultFd;
    g_modules[idx].saved_fd = -1;
    g_modules[idx].active = true;

    // 模块 ID 从 1 开始
    return idx + 1;
}

void LogSetLevel(int module_id, LogLevel level)
{
    if (module_id == 0)
    {
        // 批量操作所有模块
        for (int i = 0; i < g_module_count; ++i)
        {
            if (g_modules[i].active)
            {
                g_modules[i].level = level;
            }
        }
        return;
    }

    int idx = ModuleIdToIndex(module_id);
    if (idx < 0) return;
    g_modules[idx].level = level;
}

LogLevel LogGetLevel(int module_id)
{
    int idx = ModuleIdToIndex(module_id);
    if (idx < 0) return LogLevel::kInfo;
    return g_modules[idx].level;
}

int LogSetOutputFile(int module_id, const char* path)
{
    int idx = ModuleIdToIndex(module_id);
    if (idx < 0) return -1;

    if (!path)
    {
        LogResetOutput(module_id);
        return 0;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        return -1;
    }

    // 保存当前 fd 用于恢复
    g_modules[idx].saved_fd = g_modules[idx].fd;
    g_modules[idx].fd = fd;

    return 0;
}

void LogResetOutput(int module_id)
{
    int idx = ModuleIdToIndex(module_id);
    if (idx < 0) return;

    // 如果之前重定向过，关闭文件 fd 并恢复
    if (g_modules[idx].fd != kDefaultFd && g_modules[idx].saved_fd >= 0)
    {
        close(g_modules[idx].fd);
        g_modules[idx].fd = g_modules[idx].saved_fd;
        g_modules[idx].saved_fd = -1;
    }
    else
    {
        g_modules[idx].fd = kDefaultFd;
    }
}

const char* LogLevelStr(LogLevel level)
{
    switch (level)
    {
        case LogLevel::kTrace: return "TRACE";
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo:  return "INFO ";
        case LogLevel::kWarn:  return "WARN ";
        case LogLevel::kError: return "ERROR";
        case LogLevel::kFatal: return "FATAL";
        default:               return "?????";
    }
}

int LogGetModuleCount()
{
    return g_module_count;
}

const char* LogGetModuleName(int module_id)
{
    int idx = ModuleIdToIndex(module_id);
    if (idx < 0) return "unknown";
    return g_modules[idx].name ? g_modules[idx].name : "unknown";
}

void LogWrite(int module_id, LogLevel level, const char* file, const char* func, int line, const char* fmt, ...)
{
    int idx = ModuleIdToIndex(module_id);
    if (idx < 0) return;

    // 加锁获取级别和fd，确保线程安全
    pthread_mutex_lock(&g_level_mutex);
    LogLevel current_level = g_modules[idx].level;
    int fd = g_modules[idx].fd;
    pthread_mutex_unlock(&g_level_mutex);

    if (current_level > level) return;

    // 获取当前时间（线程安全）
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    // 提取文件名（只取最后一段）
    const char* filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    // 使用 char 缓冲区格式化
    char buf[kLogBufSize];
    int pos = 0;

    // 写入日志头
    pos += snprintf(buf + pos, (size_t)(kLogBufSize - pos),
                    "[%s] [%-5s] [%s:%s:%d] ",
                    time_buf, LogLevelStr(level), filename, func, line);

    // 写入日志内容
    va_list args;
    va_start(args, fmt);
    pos += vsnprintf(buf + pos, (size_t)(kLogBufSize - pos), fmt, args);
    va_end(args);

    // 追加换行符
    if (pos < kLogBufSize - 1)
    {
        buf[pos++] = '\n';
    }
    else
    {
        buf[kLogBufSize - 2] = '\n';
    }

    // 写入文件描述符
    ssize_t written = 0;
    while (written < pos)
    {
        ssize_t w = write(fd, buf + written, (size_t)(pos - written));
        if (w <= 0) break;
        written += w;
    }
}

// ===== Socket 服务器 API =====

int LogStartSocket(const char* socket_path)
{
    if (g_socket.running)
    {
        fprintf(stderr, "[log_kit] ERROR: Socket 服务器已在运行\n");
        return -1;
    }

    // 确定 socket 路径
    const char* path = socket_path ? socket_path : kDefaultSocketPath;
    strncpy(g_socket.socket_path, path, sizeof(g_socket.socket_path) - 1);
    g_socket.socket_path[sizeof(g_socket.socket_path) - 1] = '\0';

    // 删除旧的 socket 文件
    unlink(g_socket.socket_path);

    // 创建 socket
    g_socket.listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_socket.listen_fd < 0)
    {
        perror("[log_kit] socket");
        return -1;
    }

    // 绑定
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket.socket_path, sizeof(addr.sun_path) - 1);

    if (bind(g_socket.listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("[log_kit] bind");
        close(g_socket.listen_fd);
        g_socket.listen_fd = -1;
        return -1;
    }

    // 监听
    if (listen(g_socket.listen_fd, 5) < 0)
    {
        perror("[log_kit] listen");
        close(g_socket.listen_fd);
        g_socket.listen_fd = -1;
        unlink(g_socket.socket_path);
        return -1;
    }

    // 启动后台线程
    g_socket.running = true;
    if (pthread_create(&g_socket.thread, nullptr, SocketThreadFunc, nullptr) != 0)
    {
        perror("[log_kit] pthread_create");
        close(g_socket.listen_fd);
        g_socket.listen_fd = -1;
        g_socket.running = false;
        unlink(g_socket.socket_path);
        return -1;
    }

    g_socket.thread_started = true;
    return 0;
}

void LogStopSocket()
{
    if (!g_socket.running && !g_socket.thread_started) return;

    g_socket.running = false;

    if (g_socket.thread_started)
    {
        pthread_join(g_socket.thread, nullptr);
        g_socket.thread_started = false;
    }

    if (g_socket.listen_fd >= 0)
    {
        close(g_socket.listen_fd);
        g_socket.listen_fd = -1;
    }

    if (g_socket.socket_path[0] != '\0')
    {
        unlink(g_socket.socket_path);
        g_socket.socket_path[0] = '\0';
    }
}

}  // namespace log_kit
