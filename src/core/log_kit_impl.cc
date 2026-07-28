/**
 * @file log_kit_impl.cc
 * @brief log_kit 核心实现：模块管理、日志写入、Socket 控制
 *
 * @details 本文件包含 log_kit 所有内部实现函数，
 *          由 src/api/log_kit.cc 的 C 接口薄封装层调用。
 */

#include "log_kit_impl.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "core/log_context.h"

namespace log_kit
{

// ===== 辅助函数 =====

static LogContext &ctx()
{
    return LogContext::getInstance();
}

// ===== 级别字符串 =====

const char *LevelStr(log_kit_level_t level)
{
    switch (level)
    {
        case LOG_KIT_TRACE:
        {
            return "TRACE";
        }
        case LOG_KIT_DEBUG:
        {
            return "DEBUG";
        }
        case LOG_KIT_INFO:
        {
            return "INFO ";
        }
        case LOG_KIT_WARN:
        {
            return "WARN ";
        }
        case LOG_KIT_ERROR:
        {
            return "ERROR";
        }
        case LOG_KIT_FATAL:
        {
            return "FATAL";
        }
        default:
        {
            return "?????";
        }
    }
}

// ===== 模块注册与查找 =====

int RegisterModule(const char *name)
{
    if (!name || !*name)
    {
        fprintf(stderr, "[log_kit] ERROR: 模块名称不能为空\n");
        return 0;
    }

    // 先无锁检查是否已存在（快路径）
    int existing = ctx().FindByName(name);
    if (existing >= 0)
    {
        return existing + 1;
    }

    // 加锁处理注册（冷路径串行化）
    std::lock_guard<std::mutex> lock(ctx().Lock());

    // 双重检查：加锁后再查一次，防止并发重复注册
    existing = ctx().FindByName(name);
    if (existing >= 0)
    {
        return existing + 1;
    }

    int count = ctx().ModuleCount();
    if (count >= kMaxModules)
    {
        fprintf(stderr, "[log_kit] ERROR: 模块数已达上限 (%d)\n", kMaxModules);
        return 0;
    }

    int idx = count;
    auto &m = ctx().ModuleAt(idx);

    std::lock_guard<std::mutex> mlock(m.mtx);
    m.name = strdup(name);
    m.active.store(true, std::memory_order_release);

    ctx().SetModuleCount(idx + 1);

    return idx + 1;
}

int FindModuleByName(const char *name)
{
    int idx = ctx().FindByName(name);
    if (idx < 0)
    {
        return 0;
    }
    return idx + 1;
}

// ===== 级别控制 =====

void SetLevel(int module_id, log_kit_level_t level)
{
    if (module_id == 0)
    {
        int count = ctx().ModuleCount();
        for (int i = 0; i < count; ++i)
        {
            auto &m = ctx().ModuleAt(i);
            if (m.active.load(std::memory_order_acquire))
            {
                m.level.store(level, std::memory_order_relaxed);
            }
        }
        return;
    }

    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx >= 0)
    {
        ctx().ModuleAt(idx).level.store(level, std::memory_order_relaxed);
    }
}

log_kit_level_t GetLevel(int module_id)
{
    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx < 0)
    {
        return LOG_KIT_INFO;
    }
    return ctx().ModuleAt(idx).level.load(std::memory_order_acquire);
}

// ===== 模块信息查询 =====

int GetModuleCount(void)
{
    return ctx().ModuleCount();
}

const char *GetModuleName(int module_id)
{
    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx < 0)
    {
        return "unknown";
    }
    auto &m = ctx().ModuleAt(idx);
    std::lock_guard<std::mutex> lock(m.mtx);
    return m.name ? m.name : "unknown";
}

// ===== 输出重定向 =====

int SetOutputFile(int module_id, const char *path)
{
    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx < 0)
    {
        return -1;
    }

    auto &m = ctx().ModuleAt(idx);
    std::lock_guard<std::mutex> lock(m.mtx);

    if (!path)
    {
        int old_fd = m.fd.load(std::memory_order_relaxed);
        int saved = m.saved_fd;
        if (old_fd != kDefaultFd && saved >= 0)
        {
            m.fd.store(saved, std::memory_order_relaxed);
            m.saved_fd = -1;
            ::close(old_fd);
        }
        else
        {
            m.fd.store(kDefaultFd, std::memory_order_relaxed);
        }
        return 0;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        return -1;
    }

    int old_fd = m.fd.load(std::memory_order_relaxed);
    m.saved_fd = old_fd;
    m.fd.store(fd, std::memory_order_relaxed);
    if (old_fd != kDefaultFd && old_fd >= 0)
    {
        ::close(old_fd);
    }

    return 0;
}

void ResetOutput(int module_id)
{
    SetOutputFile(module_id, nullptr);
}

// ===== 日志写入（热路径）=====

void WriteLog(int module_id, log_kit_level_t level, const char *file, const char *func, int line, const char *fmt,
              va_list ap)
{
    // 热路径：无锁，直接 atomic load
    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx < 0)
    {
        return;
    }

    auto &m = ctx().ModuleAt(idx);
    log_kit_level_t current_level = m.level.load(std::memory_order_relaxed);
    if (current_level > level)
    {
        return;
    }

    // 注意：fd 使用 relaxed load，可能与 SetOutputFile 并发关闭产生竞态
    // 这是有意为之的设计——日志热路径优先，接受偶发 write 失败（返回 -1 或 EBADF）
    int fd = m.fd.load(std::memory_order_relaxed);

    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    char buf[kLogBufSize];
    int pos = 0;

    pos += snprintf(buf + pos, (size_t)(kLogBufSize - pos), "[%s] [%-5s] [%s:%s:%d] ", time_buf, LevelStr(level),
                    filename, func, line);

    pos += vsnprintf(buf + pos, (size_t)(kLogBufSize - pos), fmt, ap);

    if (pos < kLogBufSize - 1)
    {
        buf[pos++] = '\n';
    }
    else
    {
        buf[kLogBufSize - 2] = '\n';
    }

    ssize_t written = 0;
    while (written < pos)
    {
        ssize_t w = write(fd, buf + written, (size_t)(pos - written));
        if (w < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        written += w;
    }
}

}  // namespace log_kit
