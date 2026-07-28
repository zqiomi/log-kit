/**
 * @file log_server.h
 * @brief log_kit Unix Domain Socket 控制服务器
 *
 * @details 提供运行时远程日志级别控制能力，
 *          支持 log_tool 客户端通过 Socket 发送命令。
 *
 *          running_ 使用 std::atomic<bool>，在主线程写、server 线程读之间安全同步。
 */

#ifndef LOG_SERVER_H_
#define LOG_SERVER_H_

#include <pthread.h>

#include <atomic>

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

class SocketServer
{
public:
    SocketServer() = default;
    ~SocketServer()
    {
        Stop();
    }

    static SocketServer &GetSocket()
    {
        static SocketServer socket;
        return socket;
    }

    SocketServer(const SocketServer &) = delete;
    SocketServer &operator=(const SocketServer &) = delete;

    bool Start(const char *socket_path);
    void Stop();
    bool IsRunning() const
    {
        return running_.load(std::memory_order_acquire);
    }

private:
    static void *ThreadFunc(void *arg);
    void RunLoop();
    void HandleClient(int client_fd);
    int HandleCommand(const char *cmd, char *response, int response_size);
    void CloseListenFd();

    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    pthread_t thread_ = 0;
    std::atomic<bool> thread_started_{false};
    char socket_path_[256] = {0};
};

}  // namespace log_kit

#endif  // LOG_SERVER_H_
