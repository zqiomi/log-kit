/**
 * @file main.cc
 * @brief log_kit 使用示例
 *
 * @details 功能：
 * 1. 注册多个日志模块，展示独立级别控制
 * 2. 启动内置 Socket 服务器，接受 log_tool 远程控制
 * 3. 主线程循环打印日志，演示运行时级别调整效果
 *
 * @note 使用方法：
 *   @code
 *   # 终端1：运行 demo
 *   ./log_kit_demo [socket_path]
 *
 *   # 终端2：使用 log_tool 实时调整日志级别
 *   ./log_tool [socket_path] level 1 trace   # 模块1设为TRACE
 *   ./log_tool [socket_path] level 0 warn    # 所有模块设为WARN
 *   ./log_tool [socket_path] list             # 查看所有模块状态
 *   @endcode
 *
 * 默认 Socket 路径：/tmp/log_kit.sock
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <atomic>

#include "log_kit.h"

// ===== 全局状态 =====

/** @brief 运行标志 */
static std::atomic<bool> g_running(true);

/** @brief 模块 ID 数组（从 1 开始） */
static int g_module_ids[3] = {-1, -1, -1};

/**
 * @brief 信号处理函数
 * @param sig 信号编号
 */
static void OnSignal(int sig)
{
    (void)sig;
    g_running = false;
}

// ===== 主函数 =====

/**
 * @brief 程序入口
 *
 * @param argc 参数个数
 * @param argv 参数列表，可选第一个参数为 socket 路径
 * @return 0 正常退出
 */
int main(int argc, char* argv[])
{
    const char* socket_path = nullptr;
    if (argc >= 2)
    {
        socket_path = argv[1];
    }

    // 信号处理
    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);

    // 注册模块（ID 从 1 开始）
    g_module_ids[0] = log_kit::LogRegister("demo");
    g_module_ids[1] = log_kit::LogRegister("network");
    g_module_ids[2] = log_kit::LogRegister("codec");

    // 设置不同级别
    log_kit::LogSetLevel(g_module_ids[0], log_kit::LogLevel::kDebug);
    log_kit::LogSetLevel(g_module_ids[1], log_kit::LogLevel::kInfo);
    log_kit::LogSetLevel(g_module_ids[2], log_kit::LogLevel::kWarn);

    // 启动 Socket 服务器
    if (log_kit::LogStartSocket(socket_path) == 0)
    {
        printf("=== log_kit demo ===\n");
        printf("模块: demo(%d), network(%d), codec(%d)\n",
               g_module_ids[0], g_module_ids[1], g_module_ids[2]);
        printf("Socket 控制服务器已启动\n");
        printf("使用 log_tool 连接进行远程控制\n");
        printf("按 Ctrl+C 退出\n\n");
    }
    else
    {
        fprintf(stderr, "Socket 服务器启动失败，仅运行日志演示\n");
    }

    // 主循环
    int counter = 0;
    while (g_running)
    {
        LOG_DEBUG(g_module_ids[0], "demo 心跳, count=%d", counter);
        LOG_INFO(g_module_ids[1], "network 数据包已处理, seq=%u", (unsigned)(counter * 10));
        LOG_WARN(g_module_ids[2], "codec 缓冲区使用率: %d%%", 50 + (counter % 50));

        if (counter % 10 == 0 && counter > 0)
        {
            LOG_ERROR(g_module_ids[0], "模拟错误 count=%d", counter);
        }

        counter++;
        usleep(500000);  // 500ms
    }

    // 停止 Socket 服务器
    log_kit::LogStopSocket();

    printf("\n=== demo 已退出 ===\n");
    return 0;
}
