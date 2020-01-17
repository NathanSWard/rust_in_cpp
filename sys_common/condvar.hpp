// condvar.hpp

#pragma once

#include <chrono>

#include "../sys/condvar.hpp"
#include "mutex.hpp"

namespace rust {
namespace sys {

class Condvar {
    impl::Condvar cv_;
public:
    Condvar() = default;

    void notify_one() { cv_.notify_one(); }
    void notify_all() { cv_.notify_all(); }

    void wait(Mutex& m) { cv_.wait(mutex::raw(m)); }

    template<class Rep, class Period>
    bool wait_timeout(Mutex& m, std::chrono::duration<Rep, Period> const dur) {
        if (dur <= dur.zero())
            return false;
        using sys_tpf = std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<long double, std::nano>>;
        using sys_tpi = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;
        constexpr sys_tpf max{sys_tpi::max()};
        std::chrono::system_clock::time_point const s_now{std::chrono::system_clock::now()};
        if (max - dur > s_now)
            return cv_.wait_timeout(mutex::raw(m), s_now + std::chrono::ceil<std::chrono::nanoseconds>(dur)));
        else
            return cv_.wait_timeout(mutex::raw(m), sys_tpi::max()));
    }
};

} // namespace sys
} // namespace rust

