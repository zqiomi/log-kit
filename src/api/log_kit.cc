/**
 * @file log_kit.cc
 * @brief log_kit 对外 C 接口薄封装
 *
 * @details 本文件仅包含对外暴露的 C API，具体实现转发到 core/ 下的内部实现。
 */

#include <cstdarg>

#include "core/log_kit_impl.h"
#include "core/log_server.h"

// ===== C 接口实现（薄封装，直接转发）=====

extern "C" int log_kit_register(const char *name)
{
    return log_kit::RegisterModule(name);
}

extern "C" int log_kit_find_module_by_name(const char *name)
{
    return log_kit::FindModuleByName(name);
}

extern "C" void log_kit_set_level(int module_id, log_kit_level_t level)
{
    log_kit::SetLevel(module_id, level);
}

extern "C" log_kit_level_t log_kit_get_level(int module_id)
{
    return log_kit::GetLevel(module_id);
}

extern "C" int log_kit_get_module_count(void)
{
    return log_kit::GetModuleCount();
}

extern "C" const char *log_kit_get_module_name(int module_id)
{
    return log_kit::GetModuleName(module_id);
}

extern "C" int log_kit_set_output_file(int module_id, const char *path)
{
    return log_kit::SetOutputFile(module_id, path);
}

extern "C" void log_kit_reset_output(int module_id)
{
    log_kit::ResetOutput(module_id);
}

extern "C" const char *log_kit_level_str(log_kit_level_t level)
{
    return log_kit::LevelStr(level);
}

extern "C" void log_kit_write(int module_id, log_kit_level_t level, const char *file, const char *func, int line,
                              const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_kit::WriteLog(module_id, level, file, func, line, fmt, ap);
    va_end(ap);
}

extern "C" int log_kit_start_socket(const char *socket_path)
{
    return log_kit::SocketServer::GetSocket().Start(socket_path) ? 0 : -1;
}

extern "C" void log_kit_stop_socket(void)
{
    return log_kit::SocketServer::GetSocket().Stop();
}
