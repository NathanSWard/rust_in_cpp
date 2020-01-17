// condvar.hpp

#pragma once

#include <atomic>
#include <chrono>
#include <utility>

#include "../sys_common/condvar.hpp"
#include "_sync_base.hpp"
#include "mutex.hpp"
#include "../panic.hpp"
#include "../result.hpp"

namespace rust {
namespace sync {

class WaitTimeoutResult {
    bool const res_;
public:
    constexpr WaitTimeoutResult(bool b) noexcept : res_{b} {}
    constexpr bool timed_out() const& { return res_; }
};

class Condvar {
    std::atomic_uintptr_t mutex_{0};
    sys::Condvar cv_{};

    void verify(sys::Mutex const& m) {
        uintptr_t expected = 0;
        auto const addr = reinterpret_cast<uintptr_t>(std::addressof(mutex_));
        if (auto const n = mutex_.compare_exchange_strong(expected, addr, std::memory_order_seq_cst); n == 0 || n == addr)
            return;
        else
            panic("attempted to use a condition variable with two mutexes");
    }
public:
    template<class T>
    LockResult<MutexGuard<T>> wait(MutexGuard<T>&& guard) {
        using ok_t = MutexGuard<T>;
        using err_t = PoisonError<MutexGuard<T>>;
        
        sys::Mutex& lock = mutex::guard_lock(guard);
        verify(lock);
        cv_.wait(lock);
        if (mutex::guard_poison(guard).get())
            return result::Err<ok_t, err_t>(guard); 
        return result::Ok<ok_t, err_t>(guard);
    }

    template<class T, class Fn>
    LockResult<MutexGuard<T>> wait_until(MutexGuard<T>&& guard, Fn&& condition) {
        auto g(guard);
        while (!condition(*g))
            g = wait(std::move(g)).unwrap();
        return result::Ok<MutexGuard<T>, PoisonError<MutexGuard<T>>(std::move(g));
    }

    template<class T>
    LockResult<std::pair<MutexGuard<T>, bool>> wait_timeout_ms(MutexGuard<T>&& guard, std::uint32_t const ms);

    template<class T, class Rep, class Period>
    LockResult<std::pair<MutexGuard<T>, WaitTimeoutResult>> wait_timeout(MutexGuard<T>&& guard, std::chrono::duration<Rep, Period> const dur);

    template<class T, class Rep, class Period, class Fn>
    LockResult<std::pair<MutexGuard<T>, WaitTimeoutResult>> wait_timeout_until(MutexGuard<T>&& guard, std::chrono::duration<Rep, Period> const dur, Fn&& condition);

    void notify_one() { cv_.notify_one(); }
    void notify_all() { cv_.notify_all(); }
};

}
}