/**
 * @file log_kit.h
 * @brief 轻量级 C++ 日志库
 *
 * @details 特性：
 * - 模块注册机制，每个模块独立控制日志级别
 * - 模块 ID 从 1 开始，ID 0 表示批量操作所有模块
 * - 输出重定向（dup2 接管 stderr）
 * - 缓冲区格式化，线程安全（依赖 stdio 内置锁 + localtime_r）
 * - 内置 Unix Domain Socket 服务器，支持运行时远程配置
 *
 * @note 使用方式：
 * 1. 在模块初始化时调用 LogRegister() 注册模块，保存返回的模块 ID（从 1 开始）
 * 2. 使用 _LOG_INFO / _LOG_WARN 等宏打印日志
 * 3. 外部可通过 LogSetLevel() 动态调整日志级别
 * 4. 调用 LogStartSocket() 启动 Socket 服务器，通过 log_tool 远程控制
 *
 * @code
* // 模块初始化
* static const int kModuleId = log_kit::LogRegister("my-module");
*
* // 打印日志
* _LOG_INFO(kModuleId, "packet sent, seq=%u", seq);
* _LOG_ERROR(kModuleId, "failed to bind port %d", port);
* @endcode
 */

#ifndef LOG_KIT_H_
#define LOG_KIT_H_

namespace log_kit
{

/**
 * @enum LogLevel
 * @brief 日志级别（数值越小越详细）
 */
enum class LogLevel
{
    kTrace = 0,  ///< 详细跟踪
    kDebug,      ///< 调试信息
    kInfo,       ///< 一般信息
    kWarn,       ///< 警告
    kError,      ///< 错误
    kFatal,      ///< 致命错误
};

/**
 * @brief 注册日志模块
 *
 * @details 返回模块 ID，从 1 开始自增。
 *          模块 ID 0 保留为批量操作所有模块。
 *
 * @param name 模块名称（用于标识，不影响性能）
 * @return 模块 ID，>= 1 成功，-1 失败（模块数已达上限）
 */
int LogRegister(const char* name);

/**
 * @brief 设置模块日志级别
 *
 * @details 当 module_id 为 0 时，批量设置所有已注册模块的日志级别。
 *
 * @param module_id 模块 ID（由 LogRegister 返回，0 表示所有模块）
 * @param level 日志级别
 */
void LogSetLevel(int module_id, LogLevel level);

/**
 * @brief 获取模块当前日志级别
 *
 * @param module_id 模块 ID（由 LogRegister 返回）
 * @return 当前日志级别，无效 ID 返回 kInfo
 */
LogLevel LogGetLevel(int module_id);

/**
 * @brief 重定向模块输出到文件（通过 dup2 接管 stderr）
 *
 * @param module_id 模块 ID（由 LogRegister 返回）
 * @param path 文件路径，nullptr 恢复输出到 stderr
 * @return 0 成功，-1 失败
 */
int LogSetOutputFile(int module_id, const char* path);

/**
 * @brief 恢复模块输出到 stderr
 *
 * @param module_id 模块 ID（由 LogRegister 返回）
 */
void LogResetOutput(int module_id);

/**
 * @brief 核心日志写入函数（通常通过 LOG_* 宏调用）
 *
 * @details 使用 char 缓冲区格式化后写入，避免 dprintf 的兼容性问题。
 *
 * @param module_id 模块 ID
 * @param level 日志级别
 * @param file 源文件名（__FILE__）
 * @param func 函数名（__func__）
 * @param line 行号（__LINE__）
 * @param fmt 格式化字符串（printf 风格）
 */
void LogWrite(int module_id, LogLevel level, const char* file, const char* func, int line, const char* fmt, ...);

/**
 * @brief 获取日志级别名称字符串
 *
 * @param level 日志级别
 * @return 级别名称（如 "INFO "、"ERROR"），固定 5 字符宽度
 */
const char* LogLevelStr(LogLevel level);

/**
 * @brief 获取已注册模块数量
 *
 * @return 当前已注册的模块数
 */
int LogGetModuleCount();

/**
 * @brief 获取模块名称
 *
 * @param module_id 模块 ID（由 LogRegister 返回）
 * @return 模块名称，无效 ID 返回 "unknown"
 */
const char* LogGetModuleName(int module_id);

// ===== Socket 服务器 API =====

/**
 * @brief 启动 Unix Domain Socket 控制服务器
 *
 * @details 在后台线程中启动 Socket 服务器，接受 log_tool 客户端连接。
 *          支持通过 Socket 远程执行命令（设置级别、查询状态等）。
 *          需要编译时启用 LOG_KIT_ENABLE_SOCKET 选项。
 *
 * @param socket_path Socket 文件路径，nullptr 使用编译时默认路径
 * @return 0 成功，-1 失败
 */
int LogStartSocket(const char* socket_path = nullptr);

/**
 * @brief 停止 Socket 控制服务器
 *
 * @details 关闭 Socket 服务器并等待后台线程退出。
 */
void LogStopSocket();

}  // namespace log_kit

// ===== 日志宏 =====
// 使用完全限定名，确保在任意命名空间中可用
// 宏带 _ 前缀，避免与业务代码中的 LOG_* 宏冲突

/** @brief TRACE 级别日志宏 */
#define _LOG_TRACE(module_id, fmt, ...) \
    ::log_kit::LogWrite(module_id, ::log_kit::LogLevel::kTrace, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

/** @brief DEBUG 级别日志宏 */
#define _LOG_DEBUG(module_id, fmt, ...) \
    ::log_kit::LogWrite(module_id, ::log_kit::LogLevel::kDebug, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

/** @brief INFO 级别日志宏 */
#define _LOG_INFO(module_id, fmt, ...) \
    ::log_kit::LogWrite(module_id, ::log_kit::LogLevel::kInfo, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

/** @brief WARN 级别日志宏 */
#define _LOG_WARN(module_id, fmt, ...) \
    ::log_kit::LogWrite(module_id, ::log_kit::LogLevel::kWarn, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

/** @brief ERROR 级别日志宏 */
#define _LOG_ERROR(module_id, fmt, ...) \
    ::log_kit::LogWrite(module_id, ::log_kit::LogLevel::kError, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

/** @brief FATAL 级别日志宏 */
#define _LOG_FATAL(module_id, fmt, ...) \
    ::log_kit::LogWrite(module_id, ::log_kit::LogLevel::kFatal, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

#endif  // LOG_KIT_H_
