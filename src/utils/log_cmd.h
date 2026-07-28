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
 * 精确匹配命令字：cmd 前缀必须等于 name，且下一位是空格或字符串结尾。
 * 避免原 strncmp 前缀匹配导致 "levelx" 被误判为 "level"。
 *
 * @param cmd 命令字符串（可带参数，如 "level 1 trace"）
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
        size_t nlen = strlen(kCmdTable[i].name);
        if (strncmp(cmd, kCmdTable[i].name, nlen) == 0 &&
            (cmd[nlen] == ' ' || cmd[nlen] == '\0'))
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
