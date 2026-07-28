/**
 * @file log_context.h
 * @brief log_kit 模块上下文与全局状态
 *
 * @details 包含模块信息结构和全局上下文类，
 *          负责模块注册、查找、级别控制、输出重定向等核心管理。
 *
 *          并发模型：
 *          - 热路径（log_kit_write）：无锁，通过 ModuleInfo 中 atomic 字段读取 level/fd/active
 *          - 冷路径（register/set_output_file 等）：通过 mutex 保护
 */

#ifndef LOG_CONTEXT_H_
#define LOG_CONTEXT_H_

#include <pthread.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "log_kit.h"
#include "utils/log_config.h"

namespace log_kit
{

// ===== 模块信息 =====
//
// 并发设计说明：
// - level / active 使用 atomic，供热路径（log_kit_write）无锁读取
// - name 通过模块级 mutex（mtx）保护，仅在注册/查询时短暂持有
// - fd 由 fd_lock（读写锁）保护：
//     * 热路径 write：rdlock，多线程并发写不阻塞
//     * 冷路径 SetOutputFile：wrlock，切换 fd 时独占，close 在锁内执行
//   这样彻底消除原 atomic fd 设计中"读到旧 fd 值 → write 到已被 close/复用的 fd"
//   的串台竞态。stderr（kDefaultFd）作为哨兵值，永远不由本模块 close。
//   支持任意可 write 的 fd：stderr、普通文件、socket（含 /dev/fd/N 重定向）。

struct ModuleInfo
{
    ModuleInfo() : name(nullptr), level(LOG_KIT_INFO), fd(kDefaultFd), active(false)
    {
        pthread_rwlock_init(&fd_lock, nullptr);
    }

    ~ModuleInfo()
    {
        if (name)
        {
            free(const_cast<char *>(name));
            name = nullptr;
        }
        // 在写锁内关闭独占 fd，确保无热路径仍持有旧 fd
        pthread_rwlock_wrlock(&fd_lock);
        if (fd != kDefaultFd && fd >= 0)
        {
            ::close(fd);
        }
        fd = -1;
        pthread_rwlock_unlock(&fd_lock);
        pthread_rwlock_destroy(&fd_lock);
    }

    ModuleInfo(const ModuleInfo &) = delete;
    ModuleInfo &operator=(const ModuleInfo &) = delete;

    const char *name;
    std::atomic<log_kit_level_t> level;
    int fd;                       // 由 fd_lock 保护
    std::atomic<bool> active;
    std::mutex mtx;               // 保护 name
    pthread_rwlock_t fd_lock;     // 保护 fd 切换，避免热路径 write 串台
};

// ===== 全局上下文 =====

class LogContext
{
public:
    static LogContext &getInstance()
    {
        static LogContext ctx;
        return ctx;
    }

    int ModuleIdToIndex(int module_id) const
    {
        int idx = module_id - 1;
        if (idx < 0 || idx >= kMaxModules)
        {
            return -1;
        }
        if (!modules_[idx].active.load(std::memory_order_acquire))
        {
            return -1;
        }
        return idx;
    }

    int FindByName(const char *name) const
    {
        if (!name)
        {
            return -1;
        }
        int count = module_count_.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            if (modules_[i].active.load(std::memory_order_acquire) && modules_[i].name &&
                strcmp(modules_[i].name, name) == 0)
            {
                return i;
            }
        }
        return -1;
    }

    ModuleInfo &ModuleAt(int idx)
    {
        return modules_[idx];
    }

    const ModuleInfo &ModuleAt(int idx) const
    {
        return modules_[idx];
    }

    int ModuleCount() const
    {
        return module_count_.load(std::memory_order_acquire);
    }

    void SetModuleCount(int count)
    {
        module_count_.store(count, std::memory_order_release);
    }

    std::mutex &Lock()
    {
        return mtx_;
    }

private:
    LogContext()
    {
        module_count_.store(0, std::memory_order_relaxed);
    }

    ~LogContext() = default;

    LogContext(const LogContext &) = delete;
    LogContext &operator=(const LogContext &) = delete;

    ModuleInfo modules_[kMaxModules];
    std::atomic<int> module_count_{0};
    std::mutex mtx_;
};

}  // namespace log_kit

#endif  // LOG_CONTEXT_H_
