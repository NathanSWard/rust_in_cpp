// rwlock.hpp

#pragma once
#include "../../debug/debug.hpp"
#include "../../panic.hpp"
#include <atomic>
#include <errno.h>
#include <pthread.h>

namespace rust {
namespace sys {
namespace impl {

class RWLock {
    pthread_rwlock_t handle_ = PTHREAD_RWLOCK_INITIALIZER;
    std::atomic_uint num_readers_{0};
    bool write_locked_{false};
public:
    constexpr RWLock() noexcept = default;

    ~RWLock() {
        auto const err = pthread_rwlock_destroy(&handle_);
        debug_assert_eq(err, 0);
    }

    void read() {
        auto const err = pthread_rwlock_rdlock(&handle_);
        if (err == EAGAIN)
            panic("rwlock maximum reader count exceeded");
        else if (err == EDEADLK || (err == 0 && write_locked_)) {
            if (err == 0)
                raw_unlock();
            panic("rwlock read lock would result in deadlock");
        }
        assert_eq(err, 0);
        num_readers_.fetch_add(1, std::memory_order_relaxed);
    }

    bool try_read() {
        auto const err = pthread_rwlock_tryrdlock(&handle_);
        if (err == 0) {
            if (write_locked_) {
                raw_unlock();
                return false;
            }
            num_readers_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    void write() {
        auto const err = pthread_rwlock_wrlock(&handle_);
        if (err == EDEADLK || write_locked_ || num_readers_.load(std::memory_order_relaxed)) {
            if (err == 0)
                raw_unlock();
            panic("rwlock write lock would result in deadlock");
        }
        debug_assert_eq(err, 0);
        write_locked_ = true;
    }

    bool try_write() {
        auto const err = pthread_rwlock_trywrlock(&handle_);
        if (err == 0) {
            if (write_locked_ || num_readers_.load(std::memory_order_relaxed) != 0) {
                raw_unlock();
                return false;
            }
            write_locked_ = true;
            return true;
        }
        return false;
    }

    void raw_unlock() {
        auto const err = pthread_rwlock_unlock(&handle_);
        debug_assert_eq(err, 0);
    }

    void read_unlock() {
        debug_assert(!write_locked_);
        num_readers_.fetch_sub(1, std::memory_order_relaxed);
        raw_unlock();
    }

    void write_unlock() {
        debug_assert_eq(num_readers_.load(std::memory_order_relaxed), 0);
        debug_assert(write_locked_);
        write_locked_ = false;
        raw_unlock();
    }
};

} // namespace impl
} // namespace sys
} // namespace rust