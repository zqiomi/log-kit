/**
 * @file main.cc
 * @brief log_kit 运行时日志级别配置工具（通过 Unix Domain Socket 通信）
 *
 * @details 使用方法：
 *   @code
 *   ./log_tool [socket_path] <命令> [参数...]
 *   @endcode
 *
 * 支持的命令：
 *   - list                       查看所有模块当前状态
 *   - level <模块ID> <级别>      设置模块日志级别（模块ID为0表示所有模块）
 *   - quit                       关闭目标程序的 Socket 服务器
 *
 * @note 示例：
 *   @code
 *   ./log_tool                           # 查看所有模块状态
 *   ./log_tool level 1 trace             # 模块1设为TRACE
 *   ./log_tool level 0 warn              # 所有模块设为WARN
 *   ./log_tool /tmp/my.sock list         # 指定 Socket 路径
 *   @endcode
 *
 * 默认 Socket 路径：/tmp/log_kit.sock
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/** @brief 默认 Socket 路径 */
static const char* kDefaultSocketPath = "/tmp/log_kit.sock";

/**
 * @brief 打印帮助信息
 *
 * @param prog 程序名称
 */
static void PrintUsage(const char* prog)
{
    printf("用法: %s [socket路径] <命令> [参数...]\n", prog);
    printf("\n");
    printf("log_kit 运行时日志级别配置工具\n");
    printf("\n");
    printf("命令:\n");
    printf("  list                     查看所有模块当前状态\n");
    printf("  level <模块ID> <级别>    设置模块日志级别\n");
    printf("                           模块ID为0表示设置所有模块\n");
    printf("                           级别可以是数字(0-5)或名称\n");
    printf("  quit                     关闭目标程序的Socket服务器\n");
    printf("\n");
    printf("日志级别:\n");
    printf("  0 = trace    详细跟踪\n");
    printf("  1 = debug    调试信息\n");
    printf("  2 = info     一般信息（默认）\n");
    printf("  3 = warn     警告\n");
    printf("  4 = error    错误\n");
    printf("  5 = fatal    致命错误\n");
    printf("\n");
    printf("示例:\n");
    printf("  %s                          # 查看所有模块状态\n", prog);
    printf("  %s level 1 trace            # 模块1设为TRACE\n", prog);
    printf("  %s level 0 warn             # 所有模块设为WARN\n", prog);
    printf("  %s level 2 4                # 模块2设为ERROR\n", prog);
    printf("  %s /tmp/my.sock list        # 指定Socket路径查看\n", prog);
}

/**
 * @brief 通过 Unix Domain Socket 发送命令并接收响应
 *
 * @param socket_path Socket 文件路径
 * @param command 要发送的命令字符串
 * @return 0 成功，1 失败
 */
static int SendCommand(const char* socket_path, const char* command)
{
    // 创建 socket
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("创建 socket 失败");
        return 1;
    }

    // 连接
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("连接失败");
        fprintf(stderr, "\n请确认 log_kit_demo 是否正在运行，Socket 路径: %s\n", socket_path);
        close(fd);
        return 1;
    }

    // 发送命令（带换行符）
    char buf[256];
    int buf_len = snprintf(buf, sizeof(buf), "%s\n", command);

    ssize_t w = write(fd, buf, (size_t)buf_len);
    if (w <= 0)
    {
        perror("发送命令失败");
        close(fd);
        return 1;
    }

    // 读取响应
    char resp[1024];
    ssize_t n = read(fd, resp, sizeof(resp) - 1);
    if (n > 0)
    {
        resp[n] = '\0';
        printf("%s", resp);
    }

    close(fd);
    return 0;
}

/**
 * @brief 程序入口
 *
 * @param argc 参数个数
 * @param argv 参数列表
 * @return 0 成功，1 失败
 */
int main(int argc, char* argv[])
{
    const char* socket_path = kDefaultSocketPath;
    char command[256] = {0};
    int cmd_start = 1;

    // 无参数时默认 list
    if (argc < 2)
    {
        strncpy(command, "list", sizeof(command) - 1);
    }
    else if (argc >= 3)
    {
        // 检查第一个参数是否为路径
        if (argv[1][0] == '/')
        {
            socket_path = argv[1];
            cmd_start = 2;
        }

        int pos = 0;
        for (int i = cmd_start; i < argc; ++i)
        {
            if (i > cmd_start) command[pos++] = ' ';
            int n = snprintf(command + pos, sizeof(command) - (size_t)pos, "%s", argv[i]);
            if (n > 0) pos += n;
        }
    }
    else
    {
        // 单个参数
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
            PrintUsage(argv[0]);
            return 0;
        }

        if (argv[1][0] == '/')
        {
            socket_path = argv[1];
            strncpy(command, "list", sizeof(command) - 1);
        }
        else
        {
            strncpy(command, argv[1], sizeof(command) - 1);
        }
    }

    if (command[0] == '\0')
    {
        PrintUsage(argv[0]);
        return 1;
    }

    return SendCommand(socket_path, command);
}
