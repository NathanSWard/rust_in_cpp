// mutex.hpp

#pragma once

#include "../sys_common/mutex.hpp"
#include "_sync_base.hpp"
#ifdef RUST_DEBUG
    #include "../panic.hpp"
#endif

namespace rust {
namespace sync {

template<class T>
class Mutex;

template<class T>
class MutexGuard;

namespace mutex {
template<class T>
constexpr sys::Mutex& guard_lock(MutexGuard<T>& guard) noexcept {
    return guard.mtx_->mutex_;
} 

template<class T>
constexpr Flag const& guard_poison(MutexGuard<T> const& guard) noexcept {
    return guard.mtx_->poison_;
}
} // namespace mutex

template<class T>
class MutexGuard {
    Mutex<T>* mtx_;
    Guard const poison_;

    friend constexpr sys::Mutex& mutex::guard_lock(MutexGuard&) noexcept;
    friend constexpr Flag& mutex::guard_poison(MutexGuard&) noexcept;
public:
    constexpr explicit MutexGuard(Mutex<T>& mtx) noexcept
        : mtx_{std::addressof(mtx)}
        , poison_{[&mtx] {
            auto res = mtx_.poison_.borrow();
            return res.is_ok() ? res.unwrap_unsafe() 
                               : res.unwrap_err_unsafe().get_ref();
        }()}
    {}

    constexpr MutexGuard(MutexGuard&& other) noexcept
        : mtx_(std::exchange(other.mtx_, nullptr))
        , poison(other.poison)
    {}

    constexpr MutexGuard& operator=(MutexGuard&& other) noexcept {
        mtx_ = std::exchange(other.mtx_, nullptr);
        poison_ = other.poison_;
    }

    MutexGuard(MutexGuard const&) = delete;
    MutexGuard& operator=(MutexGuard const&) = delete;

    ~MutexGuard() {
        if (mtx_) { 
            mtx_->poison_.done(poison_);
            mtx_->mutex_.raw_unlock(); 
        }
    }

    [[nodiscard]] constexpr T& operator*() noexcept { return mtx_->value_; }
    [[nodiscard]] constexpr T const& operator*() const noexcept { return mtx_->value_; }
};

template<class T> 
class Mutex {
public:
    // constructors
    template<class... Args, rust::detail::enable_variadic_ctr<Mutex, Args...> = 0>
    constexpr explicit Mutex(Args&&... args)
        : value_(std::forward<Args>(args)...)
    {}

    constexpr Mutex(Mutex&& other) noexcept 
        : value_([&other] {
            other.mutex_.raw_lock();
            return std::move(other.value_);
        }())
        , poison_{other.poison_.get()}
    {
        other.mutex_.raw_unlock();
    }
    
    Mutex& operator=(Mutex&&) = delete;
    Mutex(Mutex const&) = delete;
    Mutex& operator=(Mutex const&) = delete;

    // get_mut
    [[nodiscard]] constexpr LockResult<T&> get_mut() noexcept {
#ifdef RUST_DEBUG
        if (!mutex_.try_lock())
            panic("Mutex::get_mut called while mutex was locked");
        mutex_.raw_unlock();
#endif // RUST_DEBUG
        using ok_t = T&;
        using err_t = PoisonError<T&>;
        return is_poisoned() ? result::Err<ok_t, err_t>(value_) 
                             : result::Ok<ok_t, err_t>(value_);
    }

    // into_inner
    [[nodiscard]] constexpr LockResult<T> into_inner() && noexcept {
#ifdef RUST_DEBUG
        if (!mutex_.try_lock())
            panic("Mutex::into_inner called while mutex was locked");
        mutex_.raw_unlock();
#endif // RUST_DEBUG
        using ok_t = T;
        using err_t = PoisonError<T>;
        return is_poisoned() ? result::Err<ok_t, err_t>(std::move(value_)) 
                             : result::Ok<ok_t, err_t>(std::move(value_)); 
    }
    
    // is_poisoned
    [[nodiscard]] constexpr bool is_poisoned() const noexcept {
        return poison_.get();
    }

    // lock
    [[nodiscard]] constexpr LockResult<MutexGuard<T>> lock() noexcept {
        mutex_.raw_lock();
        using ok_t = MutexGuard<T>;
        using err_t = PoisonError<MutexGuard<T>>;
        return is_poisoned() ? result::Err<ok_t, err_t>(*this) 
                             : result::Ok<ok_t, err_t>(*this);
    }

    // try_lock
    [[nodiscard]] constexpr TryLockResult<MutexGuard<T>> try_lock() noexcept {
        using ok_t = MutexGuard<T>;
        using err_t = TryLockError<MutexGuard<T>>;
        if (!mutex_.try_lock())
            return result::Err<ok_t, err_t>(WouldBlock);
        if (is_poisoned())
            return result::Err<ok_t, err_t>(poison_tag, *this);
        return result::Ok<ok_t, err_t>(*this);
    }

private:
    T value_;
    sys::Mutex mutex_{}; // make Box<sys::Mutex>?
    Flag poison_{};

    friend class MutexGuard<T>;
};

template<class T>
Mutex(T) -> Mutex<T>;

}
}