/**
 * @file log_kit_impl.cc
 * @brief log_kit 核心实现：模块管理、日志写入、Socket 控制
 *
 * @details 本文件包含 log_kit 所有内部实现函数，
 *          由 src/api/log_kit.cc 的 C 接口薄封装层调用。
 */

#include "log_kit_impl.h"

#include <fcntl.h>
#include <pthread.h>
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

// ===== 时间戳缓存 =====
//
// thread_local 秒级缓存：同一秒内的多条日志复用同一格式化串，
// 避免每条日志调用 localtime_r + strftime（strftime 开销最大）。
// thread_local 无锁、无数据竞争，每线程内首条日志触发一次格式化。
// 手写 snprintf 比 strftime 快 5-10x。
//
// 精度：秒级（与原 strftime "%Y-%m-%d %H:%M:%S" 一致，无毫秒）。

static const char *GetCachedTime(time_t now)
{
    static thread_local time_t t_cached_sec = 0;
    static thread_local char t_cached_time[24] = {0};  // "YYYY-MM-DD HH:MM:SS\0" 需 20，留余量

    if (now == t_cached_sec)
    {
        return t_cached_time;
    }
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    snprintf(t_cached_time, sizeof(t_cached_time), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    t_cached_sec = now;
    return t_cached_time;
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
    if (!m.name)
    {
        // strdup 失败：保持模块未激活，下次注册会重用此 idx
        fprintf(stderr, "[log_kit] ERROR: 内存不足，模块注册失败\n");
        return 0;
    }
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
//
// fd 控制语义（覆盖 stderr / 文件 / 网络 socket 三类 fd）：
// - kDefaultFd (STDERR_FILENO=2) 是哨兵：表示"指向 stderr，本模块不持有独占资源"，
//   切换时绝不 close(2)，否则会破坏进程 stderr。
// - 文件 fd：open() 返回，本模块独占持有，切换/恢复时 close。
// - 网络 socket fd：用户可通过 SetOutputFile("/dev/fd/N") 把日志重定向到既有的
//   socket fd（N 为已打开的 fd 号）。本模块按"独占持有"语义处理——一旦传入，
//   切换时会 close 它。若调用方希望保留 socket 所有权，应自行 dup 一份再传入。
//
// 并发安全：所有 fd 读写均在 fd_lock 下进行（热路径 rdlock，本函数 wrlock），
// close 在 wrlock 内执行，确保无热路径仍持有旧 fd 值，彻底消除串台竞态。

int SetOutputFile(int module_id, const char *path)
{
    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx < 0)
    {
        return -1;
    }

    auto &m = ctx().ModuleAt(idx);

    // 恢复到 stderr
    if (!path)
    {
        pthread_rwlock_wrlock(&m.fd_lock);
        int old_fd = m.fd;
        m.fd = kDefaultFd;
        if (old_fd != kDefaultFd && old_fd >= 0)
        {
            ::close(old_fd);  // 锁内 close，确保无并发 write
        }
        pthread_rwlock_unlock(&m.fd_lock);
        return 0;
    }

    // 打开新 fd（文件路径，或 /dev/fd/N 形式的既有 fd 重定向）
    int new_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (new_fd < 0)
    {
        return -1;
    }

    pthread_rwlock_wrlock(&m.fd_lock);
    int old_fd = m.fd;
    m.fd = new_fd;
    if (old_fd != kDefaultFd && old_fd >= 0)
    {
        ::close(old_fd);  // 锁内 close 旧 fd
    }
    pthread_rwlock_unlock(&m.fd_lock);
    return 0;
}

void ResetOutput(int module_id)
{
    SetOutputFile(module_id, nullptr);
}

// ===== 日志写入（热路径）=====
//
// 优化点（方案 B，保持同步语义）：
// 1. 时间戳用 thread_local 秒级缓存，避免每条日志 localtime_r + strftime
// 2. header + body 合并为单次 vsnprintf（原为 snprintf + vsnprintf 两次）
// 3. fd 用 rdlock 保护，杜绝与 SetOutputFile 的 close 竞态（修复串台 bug）
// 4. write 循环处理 EINTR（信号中断，文件/网络 fd 均可能），
//    其他错误（EBADF/EPIPE/ECONNRESET/ENOSPC）静默丢弃——日志不应阻塞业务
//
// fd 类型覆盖：
// - stderr (kDefaultFd=2)：write 永不失败，无需特殊处理
// - 普通文件：write 极少失败（除非磁盘满 ENOSPC）
// - 网络 socket：可能 EPIPE/ECONNRESET，本实现静默丢弃

void WriteLog(int module_id, log_kit_level_t level, const char *file, const char *func, int line, const char *fmt,
              va_list ap)
{
    int idx = ctx().ModuleIdToIndex(module_id);
    if (idx < 0)
    {
        return;
    }

    auto &m = ctx().ModuleAt(idx);
    if (m.level.load(std::memory_order_relaxed) > level)
    {
        return;
    }

    // 时间戳：thread_local 秒级缓存
    time_t now = time(nullptr);
    const char *tstr = GetCachedTime(now);

    const char *filename = strrchr(file, '/');
    filename = filename ? filename + 1 : file;

    // 单次格式化 header + body
    char buf[kLogBufSize];
    int pos = snprintf(buf, sizeof(buf), "[%s] [%-5s] [%s:%s:%d] ",
                       tstr, LevelStr(level), filename, func, line);
    if (pos < 0 || pos >= (int)sizeof(buf))
    {
        return;
    }

    pos += vsnprintf(buf + pos, sizeof(buf) - (size_t)pos, fmt, ap);
    if (pos < 0)
    {
        return;
    }
    if (pos >= (int)sizeof(buf))
    {
        pos = (int)sizeof(buf) - 1;  // 截断
    }
    buf[pos++] = '\n';

    // 读锁保护 fd：与 SetOutputFile 的 wrlock 互斥，确保 write 期间 fd 不会被 close
    pthread_rwlock_rdlock(&m.fd_lock);
    int fd = m.fd;
    ssize_t written = 0;
    while (written < pos)
    {
        ssize_t w = write(fd, buf + written, (size_t)(pos - written));
        if (w < 0)
        {
            if (errno == EINTR)
            {
                continue;  // 信号中断，重试（文件/网络 fd 均可能）
            }
            break;  // EBADF/EPIPE/ECONNRESET/ENOSPC：静默丢弃
        }
        written += w;
    }
    pthread_rwlock_unlock(&m.fd_lock);
}

}  // namespace log_kit
