/**
 * @file log_config.h
 * @brief log_kit 配置常量定义
 *
 * @details 包含模块上限、缓冲区大小、默认 fd、Socket 路径等常量。
 */

#ifndef LOG_KIT_CONFIG_H_
#define LOG_KIT_CONFIG_H_

#include <unistd.h>

namespace log_kit
{

static const int kMaxModules = 64;
static const int kDefaultFd = STDERR_FILENO;
static const int kLogBufSize = 2048;
static const int kSockBufSize = 512;
static const int kSockRespSize = 1024;
static const int kCmdMaxLen = 256;
static const int kRespMaxLen = 1024;

#ifdef LOG_KIT_DEFAULT_SOCKET_PATH
static const char *kDefaultSocketPath = LOG_KIT_DEFAULT_SOCKET_PATH;
#else
static const char *kDefaultSocketPath = "/tmp/log_kit.sock";
#endif

}  // namespace log_kit

#endif  // LOG_KIT_CONFIG_H_
