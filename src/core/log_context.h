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

#include <atomic>
#include <cstring>
#include <mutex>

#include "log_kit.h"
#include "utils/log_config.h"

namespace log_kit
{

// ===== 模块信息 =====
//
// 并发设计说明：
// - level / fd / active 使用 atomic，供热路径（log_kit_write）无锁读取
// - name / saved_fd 通过模块级 mutex 保护
// - saved_fd 仅在持有 mutex 时访问，故使用普通 int

struct ModuleInfo
{
    const char *name = nullptr;
    std::atomic<log_kit_level_t> level{LOG_KIT_INFO};
    std::atomic<int> fd{kDefaultFd};
    int saved_fd{-1};
    std::atomic<bool> active{false};
    std::mutex mtx;  // 保护 name / saved_fd 等冷路径字段
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
        for (auto &m : modules_)
        {
            m.name = nullptr;
            m.level.store(LOG_KIT_INFO, std::memory_order_relaxed);
            m.fd.store(kDefaultFd, std::memory_order_relaxed);
            m.saved_fd = -1;
            m.active.store(false, std::memory_order_relaxed);
        }
        module_count_.store(0, std::memory_order_relaxed);
    }

    ~LogContext()
    {
        int count = module_count_.load(std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            auto &m = modules_[i];
            if (!m.active.load(std::memory_order_acquire))
            {
                continue;
            }
            int fd = m.fd.load(std::memory_order_relaxed);
            if (fd != kDefaultFd && fd >= 0)
            {
                ::close(fd);
            }
        }
    }

    LogContext(const LogContext &) = delete;
    LogContext &operator=(const LogContext &) = delete;

    ModuleInfo modules_[kMaxModules];
    std::atomic<int> module_count_{0};
    std::mutex mtx_;
};

}  // namespace log_kit

#endif  // LOG_CONTEXT_H_
