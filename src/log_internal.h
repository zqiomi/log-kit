/**
 * @file log_internal.h
 * @brief log_kit 内部核心结构与类声明
 *
 * @details 包含模块信息、全局上下文、Socket 服务器类和辅助函数声明。
 */

#ifndef LOG_KIT_INTERNAL_H_
#define LOG_KIT_INTERNAL_H_

#include <pthread.h>

#include "log_config.h"
#include "log_kit.h"

namespace log_kit
{

// ===== 响应格式（仅服务端使用）=====

static const char *kRespOkAll = "OK: 所有模块级别已设置为 %s\n";
static const char *kRespOkOne = "OK: 模块 %d 级别已设置为 %s\n";
static const char *kRespOkQuit = "OK: 正在关闭\n";
static const char *kRespErrUnknown = "ERROR: 未知命令 '%s'\n";
static const char *kRespErrInvalidModule = "ERROR: 无效的模块 ID %d\n";
static const char *kRespErrInvalidLevel = "ERROR: 无效的级别 '%s'\n";
static const char *kRespErrUsage = "ERROR: 用法: level <module_id> <level>\n";
static const char *kRespListHeader = "模块列表:\n";
static const char *kRespListFormat = "  [%d] %-16s 级别=%s\n";

// ===== 模块信息 =====

struct ModuleInfo
{
    const char *name = nullptr;
    LogLevel level = LogLevel::kInfo;
    int fd = kDefaultFd;
    int saved_fd = -1;
    bool active = false;
};

// ===== 全局上下文 =====

/**
 * @struct LogContext
 * @brief 日志库全局上下文（RAII 管理 mutex 生命周期）
 */
struct LogContext
{
    ModuleInfo modules[kMaxModules];
    int module_count = 0;
    bool initialized = false;
    pthread_mutex_t mutex;

    LogContext()
    {
        pthread_mutex_init(&mutex, nullptr);
    }

    ~LogContext()
    {
        pthread_mutex_destroy(&mutex);
    }

    LogContext(const LogContext &) = delete;
    LogContext &operator=(const LogContext &) = delete;

    void EnsureInit()
    {
        if (initialized) return;
        for (auto &m : modules) m = ModuleInfo{};
        module_count = 0;
        initialized = true;
    }

    int ModuleIdToIndex(int module_id) const
    {
        int idx = module_id - 1;
        if (idx < 0 || idx >= module_count || !modules[idx].active) return -1;
        return idx;
    }
};

// ===== Socket 服务器 =====

/**
 * @class SocketServer
 * @brief Unix Domain Socket 服务器（RAII 管理生命周期）
 */
class SocketServer
{
public:
    SocketServer() = default;
    ~SocketServer()
    {
        Stop();
    }

    SocketServer(const SocketServer &) = delete;
    SocketServer &operator=(const SocketServer &) = delete;

    bool Start(const char *socket_path);
    void Stop();
    bool IsRunning() const
    {
        return running_;
    }

private:
    static void *ThreadFunc(void *arg);
    void RunLoop();
    void HandleClient(int client_fd);
    int HandleCommand(const char *cmd, char *response, int response_size);
    void CloseListenFd();

    int listen_fd_ = -1;
    bool running_ = false;
    pthread_t thread_ = 0;
    bool thread_started_ = false;
    char socket_path_[256] = {0};
};

// ===== 内部辅助函数 =====

bool ParseLevel(const char *str, LogLevel *out);

}  // namespace log_kit

#endif  // LOG_KIT_INTERNAL_H_
