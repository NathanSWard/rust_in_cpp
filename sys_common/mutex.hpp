// mutex.hpp

#pragma once

#include "../sys/mutex.hpp"

namespace rust {
namespace sys {

class Condvar;
class Mutex;

class MutexGuard {
    Mutex& mutex_;
public:
    constexpr explicit MutexGuard(Mutex& m) : mutex_{m} {}
    ~MutexGuard();
};

namespace mutex { constexpr impl::Mutex& raw(Mutex& m) noexcept { return m.mutex_; } }

class Mutex {
    impl::Mutex mutex_{};
    friend constexpr impl::Mutex& mutex::raw(Mutex&) noexcept;
public:
    [[nodiscard]] auto lock() { raw_lock(); return MutexGuard{*this}; }
    void raw_lock() { mutex_.lock(); }
    void raw_unlock() { mutex_.unlock(); }
    [[nodiscard]] bool try_lock() { return mutex_.try_lock(); }
};

MutexGuard::~MutexGuard() { mutex_.raw_unlock(); }

} // namespace sys
} // namespace rust