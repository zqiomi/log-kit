/**
 * @file log_guard.h
 * @brief log_kit RAII 守卫工具
 *
 * @details 提供 pthread_mutex 和文件描述符的 RAII 封装。
 */

#ifndef LOG_KIT_GUARD_H_
#define LOG_KIT_GUARD_H_

#include <pthread.h>
#include <unistd.h>

namespace log_kit
{

/**
 * @class LockGuard
 * @brief pthread_mutex RAII 守卫，构造时加锁，析构时解锁
 */
class LockGuard
{
public:
    explicit LockGuard(pthread_mutex_t &mutex) : mutex_(mutex)
    {
        pthread_mutex_lock(&mutex_);
    }

    ~LockGuard()
    {
        pthread_mutex_unlock(&mutex_);
    }

    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    pthread_mutex_t &mutex_;
};

/**
 * @class FdGuard
 * @brief 文件描述符 RAII 守卫，析构时自动关闭 fd
 */
class FdGuard
{
public:
    FdGuard() : fd_(-1) {}
    explicit FdGuard(int fd) : fd_(fd) {}

    ~FdGuard()
    {
        if (fd_ >= 0) ::close(fd_);
    }

    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;

    FdGuard(FdGuard &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    FdGuard &operator=(FdGuard &&other) noexcept
    {
        if (this != &other)
        {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int Release()
    {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    int fd() const
    {
        return fd_;
    }
    bool IsValid() const
    {
        return fd_ >= 0;
    }
    explicit operator bool() const
    {
        return IsValid();
    }

private:
    int fd_;
};

}  // namespace log_kit

#endif  // LOG_KIT_GUARD_H_
