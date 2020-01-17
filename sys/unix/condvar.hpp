// condvar.hpp

#pragma once

#include <pthread.h>
#include <chrono>

#include "mutex.hpp"
#include "../../debug/debug.hpp"

namespace rust {
namespace sys {
namespace impl {

class Condvar {
    pthread_cond_t cv_ = PTHREAD_COND_INITIALIZER;
public:
    explicit Condvar() {
        pthread_condattr_t attr;
        auto err = pthread_condattr_init(&attr);
        debug_assert_eq(err, 0);
        err = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        debug_assert_eq(err, 0);
        err = pthread_cond_init(&cv_, &attr);
        debug_assert_eq(err, 0);
        err = pthread_condattr_destroy(&attr);
        debug_assert_eq(err, 0);
    }

    ~Condvar() {
        auto const err = pthread_cond_destroy(&cv_);
        debug_assert_eq(err, 0);
    }

    void notify_one() {
        auto const err = pthread_cond_signal(&cv_);
        debug_assert_eq(err, 0);
    }

    void notify_all() {
        auto const err = pthread_cond_broadcast(&cv_);
        debug_assert_eq(err, 0);
    }

    void wait(Mutex& m) {
        auto const err = pthread_cond_wait(&cv_, &m.mutex_);
        debug_assert_eq(err, 0);
    }

    template<class Mutex>
    void wait_timeout(Mutex& m, std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> const tp) {
        using namespace std::chrono;
        nanoseconds d{tp.time_since_epoch()};
        if (d > nanoseconds(0x59682F000000E941))
            d = nanoseconds(0x59682F000000E941);
        ::timespec ts;
        seconds const s{duration_cast<seconds>(d)};
        using ts_sec = decltype(ts.tv_sec);
        if (s.count() < std::numeric_limits<ts_sec>::max()) {
            ts.tv_sec = static_cast<ts_sec>(s.count());
            ts.tv_nsec = static_cast<decltype(ts.tv_nsec)>((d - s).count());
        }
        else {
            ts.tv_sec = std::numeric_limits<ts_sec>::max();
            ts.tv_nsec = std::giga::num - 1;
        }
        auto const err = pthread_cond_timedwait(&cv_, &m.mutex_, &ts);
        debug_assert(err == ETIMEDOUT || err == 0);
        return (err == 0);
    }
};

} // namespace impl 
} // namespace sys
} // namespace rust