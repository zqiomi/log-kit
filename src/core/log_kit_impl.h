/**
 * @file log_kit_impl.h
 * @brief log_kit 内部实现函数声明
 *
 * @details 声明 log_kit 核心内部函数：
 *          - 模块管理（注册、查找、级别控制、输出重定向）
 *          - 日志格式化与写入
 *          - Socket 服务生命周期
 *
 *          这些函数由 src/api/log_kit.cc 的薄封装层调用。
 */

#ifndef LOG_KIT_IMPL_H_
#define LOG_KIT_IMPL_H_

#include <cstdarg>

#include "log_kit.h"

namespace log_kit
{

// ===== 模块管理 =====

int RegisterModule(const char *name);
int FindModuleByName(const char *name);
void SetLevel(int module_id, log_kit_level_t level);
log_kit_level_t GetLevel(int module_id);
int GetModuleCount(void);
const char *GetModuleName(int module_id);
int SetOutputFile(int module_id, const char *path);
void ResetOutput(int module_id);
const char *LevelStr(log_kit_level_t level);

// ===== 日志写入 =====

void WriteLog(int module_id, log_kit_level_t level, const char *file, const char *func, int line, const char *fmt,
              va_list ap);

// ===== Socket 服务 =====

int StartSocket(const char *socket_path);
void StopSocket(void);

}  // namespace log_kit

#endif  // LOG_KIT_IMPL_H_
