/**
 * @file log_kit.h
 * @brief 轻量级 C/C++ 日志库（纯 C 接口）
 *
 * @details 特性：
 * - 纯 C 接口，C/C++ 项目均可直接使用
 * - 基于 int 模块 ID 的简单 API
 * - 模块独立级别控制，每个模块可设置不同的日志级别
 * - 模块信息可随时查询：通过 ID 获取名称/级别，通过名称查找 ID
 * - 输出重定向（每个模块可独立重定向到文件）
 * - 线程安全，内置 Unix Domain Socket 远程控制
 *
 * @note 使用步骤：
 * 1. 调用 log_kit_register() 注册模块，获得 int 类型的模块 ID（>=1）
 * 2. 通过 log_kit_set_level() 控制级别
 * 3. 通过 _LOG_* 宏打印日志
 * 4. 调用 log_kit_start_socket() 启动远程控制服务器
 *
 * @code
 * // 注册模块
 * int net_id = log_kit_register("network");
 * int codec_id = log_kit_register("codec");
 *
 * // 设置级别
 * log_kit_set_level(net_id, LOG_KIT_DEBUG);
 *
 * // 打印日志
 * _LOG_INFO(net_id, "packet sent, seq=%u", seq);
 * _LOG_WARN(codec_id, "buffer usage: %d%%", usage);
 *
 * // 通过名称查找已注册模块
 * int id = log_kit_find_module_by_name("network");
 * @endcode
 */

#ifndef LOG_KIT_H_
#define LOG_KIT_H_

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 日志级别（数值越小越详细）
 */
typedef enum
{
    LOG_KIT_TRACE = 0, /**< 详细跟踪 */
    LOG_KIT_DEBUG = 1, /**< 调试信息 */
    LOG_KIT_INFO = 2,  /**< 一般信息（默认） */
    LOG_KIT_WARN = 3,  /**< 警告 */
    LOG_KIT_ERROR = 4, /**< 错误 */
    LOG_KIT_FATAL = 5  /**< 致命错误 */
} log_kit_level_t;

// ===== 注册与查找 =====

/**
 * @brief 注册日志模块
 *
 * 注册一个具有唯一名称的日志模块，返回模块 ID（从 1 开始）。
 * 如果重复注册同名模块，返回已注册模块的 ID（自动去重）。
 *
 * @param name 模块名称（如 "network"、"codec"），不能为空
 * @return 模块 ID（>=1），注册失败返回 0
 */
int log_kit_register(const char *name);

/**
 * @brief 按名称查找已注册的模块
 *
 * @param name 模块名称
 * @return 模块 ID（>=1），未找到返回 0
 */
int log_kit_find_module_by_name(const char *name);

// ===== 级别控制 =====

/**
 * @brief 设置模块日志级别
 *
 * @param module_id 模块 ID（>=1），传 0 表示设置所有模块
 * @param level 目标日志级别
 */
void log_kit_set_level(int module_id, log_kit_level_t level);

/**
 * @brief 获取模块当前日志级别
 *
 * @param module_id 模块 ID
 * @return 当前日志级别，无效 ID 返回 LOG_KIT_INFO
 */
log_kit_level_t log_kit_get_level(int module_id);

// ===== 模块信息查询 =====

/**
 * @brief 获取已注册模块数量
 * @return 模块数量
 */
int log_kit_get_module_count(void);

/**
 * @brief 获取模块名称
 *
 * @param module_id 模块 ID
 * @return 模块名称字符串，无效 ID 返回 "unknown"
 */
const char *log_kit_get_module_name(int module_id);

// ===== 输出控制 =====

/**
 * @brief 重定向模块输出到文件
 *
 * @param module_id 模块 ID
 * @param path 日志文件路径，传 NULL 恢复到默认 stderr
 * @return 0 成功，-1 失败
 */
int log_kit_set_output_file(int module_id, const char *path);

/**
 * @brief 恢复模块输出到默认 stderr
 * @param module_id 模块 ID
 */
void log_kit_reset_output(int module_id);

// ===== 日志写入 =====

/**
 * @brief 日志写入函数（内部使用，通过 _LOG_* 宏调用）
 */
void log_kit_write(int module_id, log_kit_level_t level, const char *file, const char *func, int line, const char *fmt,
                   ...);

/**
 * @brief 获取级别字符串
 * @param level 日志级别
 * @return 级别字符串（如 "INFO "）
 */
const char *log_kit_level_str(log_kit_level_t level);

// ===== Socket 服务器 =====

/**
 * @brief 启动 Unix Domain Socket 控制服务器
 *
 * 启动后可通过 log_tool 命令行工具远程控制日志级别。
 *
 * @param socket_path Socket 文件路径，传 NULL 使用编译时配置的默认路径
 * @return 0 成功，-1 失败
 */
int log_kit_start_socket(const char *socket_path);

/**
 * @brief 停止 Unix Domain Socket 控制服务器
 */
void log_kit_stop_socket(void);

#ifdef __cplusplus
}  // extern "C"
#endif

// ===== 日志宏 =====
// 宏带 _ 前缀，避免与业务代码中的 LOG_* 宏冲突
// 接受 int 类型的模块 ID，C/C++ 通用（__func__ 在 C99/C++11 均为标准）

#define _LOG_TRACE(module_id, fmt, ...) \
    log_kit_write(module_id, LOG_KIT_TRACE, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define _LOG_DEBUG(module_id, fmt, ...) \
    log_kit_write(module_id, LOG_KIT_DEBUG, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define _LOG_INFO(module_id, fmt, ...) \
    log_kit_write(module_id, LOG_KIT_INFO, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define _LOG_WARN(module_id, fmt, ...) \
    log_kit_write(module_id, LOG_KIT_WARN, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define _LOG_ERROR(module_id, fmt, ...) \
    log_kit_write(module_id, LOG_KIT_ERROR, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define _LOG_FATAL(module_id, fmt, ...) \
    log_kit_write(module_id, LOG_KIT_FATAL, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

#endif  // LOG_KIT_H_
