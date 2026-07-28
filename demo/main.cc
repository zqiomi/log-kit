/**
 * @file main.cc
 * @brief log_kit 使用示例（C 接口）
 *
 * @details 演示：
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
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <atomic>

#include "log_kit.h"

static std::atomic<bool> g_running(true);

static void OnSignal(int sig)
{
    (void)sig;
    g_running = false;
}

int main(int argc, char* argv[])
{
    const char* socket_path = nullptr;
    if (argc >= 2)
    {
        socket_path = argv[1];
    }

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);

    int demo_id = log_kit_register("demo");
    int net_id = log_kit_register("network");
    int codec_id = log_kit_register("codec");

    log_kit_set_level(demo_id, LOG_KIT_DEBUG);
    log_kit_set_level(net_id, LOG_KIT_INFO);
    log_kit_set_level(codec_id, LOG_KIT_WARN);

    if (log_kit_start_socket(socket_path) == 0)
    {
        printf("=== log_kit demo ===\n");
        printf("模块: demo(%d)[%s], network(%d)[%s], codec(%d)[%s]\n",
               demo_id, log_kit_get_module_name(demo_id),
               net_id, log_kit_get_module_name(net_id),
               codec_id, log_kit_get_module_name(codec_id));
        printf("Socket 控制服务器已启动\n");
        printf("使用 log_tool 连接进行远程控制\n");
        printf("按 Ctrl+C 退出\n\n");
    }
    else
    {
        fprintf(stderr, "Socket 服务器启动失败，仅运行日志演示\n");
    }

    int counter = 0;
    while (g_running)
    {
        _LOG_DEBUG(demo_id, "demo 心跳, count=%d", counter);
        _LOG_INFO(net_id, "network 数据包已处理, seq=%u", (unsigned)(counter * 10));
        _LOG_WARN(codec_id, "codec 缓冲区使用率: %d%%", 50 + (counter % 50));

        if (counter % 10 == 0 && counter > 0)
        {
            _LOG_ERROR(demo_id, "模拟错误 count=%d", counter);
        }

        counter++;
        usleep(500000);
    }

    log_kit_stop_socket();

    printf("\n=== demo 已退出 ===\n");
    return 0;
}
