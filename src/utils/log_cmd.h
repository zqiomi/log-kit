/**
 * @file log_cmd.h
 * @brief log_kit 命令协议定义
 *
 * @details 定义 Socket 控制命令的枚举、命令表、构建函数和响应格式。
 *          服务端和客户端均包含此文件以保持协议一致。
 */

#ifndef LOG_KIT_CMD_H_
#define LOG_KIT_CMD_H_

#include <cstdio>
#include <cstring>

namespace log_kit
{

// ===== 命令类型 =====

/**
 * @enum CmdType
 * @brief Socket 控制命令类型
 */
enum class CmdType
{
    kLevel,    ///< 设置日志级别
    kList,     ///< 查询模块列表
    kQuit,     ///< 关闭服务器
    kUnknown,  ///< 未知命令
};

/**
 * @struct CmdDef
 * @brief 命令定义（名称 + 类型映射）
 */
struct CmdDef
{
    const char *name;
    CmdType type;
};

// ===== 命令表 =====

static const CmdDef kCmdTable[] = {
    {"level", CmdType::kLevel},
    {"list", CmdType::kList},
    {"quit", CmdType::kQuit},
    {nullptr, CmdType::kUnknown},
};

// ===== 命令解析 =====

/**
 * @brief 解析命令字符串到 CmdType
 *
 * @param cmd 命令字符串（不含空格后的参数）
 * @return 命令类型
 */
inline CmdType ParseCmdType(const char *cmd)
{
    if (!cmd)
    {
        return CmdType::kUnknown;
    }
    for (int i = 0; kCmdTable[i].name; ++i)
    {
        if (strncmp(cmd, kCmdTable[i].name, strlen(kCmdTable[i].name)) == 0)
        {
            return kCmdTable[i].type;
        }
    }
    return CmdType::kUnknown;
}

// ===== 命令构建 =====

inline int BuildLevelCmd(char *buf, int buf_size, int module_id, const char *level_str)
{
    return snprintf(buf, (size_t)buf_size, "level %d %s", module_id, level_str);
}

inline int BuildListCmd(char *buf, int buf_size)
{
    return snprintf(buf, (size_t)buf_size, "list");
}

inline int BuildQuitCmd(char *buf, int buf_size)
{
    return snprintf(buf, (size_t)buf_size, "quit");
}

}  // namespace log_kit

#endif  // LOG_KIT_CMD_H_
