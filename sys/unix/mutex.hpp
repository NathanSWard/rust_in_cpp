// mutex.hpp

#pragma once

#include "../../debug/debug.hpp"

#include <errno.h>
#include <pthread.h>

namespace rust {
namespace sys {
namespace impl {

struct Mutex {
    pthread_mutex_t mutex_;
    
    explicit Mutex() {
        pthread_mutexattr_t attr;
        auto err = pthread_mutexattr_init(&attr);
        debug_assert_eq(err, 0);
#ifdef RUST_DEBUG
        err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
#else // RUST_DEBUG
        err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
#endif // RUST_DEBUG
        debug_assert_eq(err, 0);
        err = pthread_mutex_init(&mutex_, &attr);
        debug_assert_eq(err, 0);
        err = pthread_mutexattr_destroy(&attr);
        debug_assert_eq(err, 0);
    }

    ~Mutex() {
        auto const err = pthread_mutex_destroy(&mutex_);
        debug_assert(err == 0 || err == EINVAL);
    }

    void lock() {
        auto const err = pthread_mutex_lock(&mutex_);
        debug_assert_eq(err, 0);
    }

    void unlock() {
        auto const err = pthread_mutex_unlock(&mutex_);
        debug_assert_eq(err, 0);
    }

    bool try_lock() {
        return (pthread_mutex_trylock(&mutex_) == 0);
    }
};

class RecursiveMutex {
    pthread_mutex_t mutex_;
public:
    explicit RecursiveMutex() {
        pthread_mutexattr_t attr;
        auto err = pthread_mutexattr_init(&attr);
        debug_assert_eq(err, 0);
        err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        debug_assert_eq(err, 0);
        err = pthread_mutex_init(&mutex_, &attr);
        debug_assert_eq(err, 0);
        err = pthread_mutexattr_destroy(&attr);
        debug_assert_eq(err, 0);
    }

    ~RecursiveMutex() {
        auto const err = pthread_mutex_destroy(&mutex_);
        debug_assert_eq(err, 0);
    }

    void lock() {
        auto const err = pthread_mutex_lock(&mutex_);
        debug_assert_eq(err, 0);
    }

    void unlock() {
        auto const err = pthread_mutex_unlock(&mutex_);
        debug_assert_eq(err, 0);
    }

    bool try_lock() {
        return (pthread_mutex_trylock(&mutex_) == 0);
    }
};

} // namespace impl
} // namespace sys
} // namespace rust