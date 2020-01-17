// rwlock.hpp

#pragma once

#include <utility>

#include "../result.hpp"
#include "../sys_common/rwlock.hpp"
#include "_sync_base.hpp"
#ifdef RUST_DEBUG
    #include "../panic.hpp"
#endif

namespace rust {
namespace sync {

template<class T> class RwLockReadGuard;
template<class T> class RwLockWriteGuard;

template<class T> 
class RwLock {
    T value_;
    sys::RwLock rwlock_{};
    Flag poison_{};
    friend class RwLockReadGuard<T>;
    friend class RwLockWriteGuard<T>;
public:
    template<class... Args, rust::detail::enable_variadic_ctr<RwLock, Args...> = 0>
    constexpr explicit RwLock(Args&&... args)
        : value_(std::forward<Args>(args)...)
    {}

    constexpr RwLock(RwLock&& other) noexcept 
        : value_([&other] {
            other.rwlock_.write();
            return std::move(other.value_);
        }())
        , poison_{other.poison_.get()}
    {
        other.rwlock_.write_unlock();
    }
    
    RwLock& operator=(RwLock&&) = delete;
    RwLock(RwLock const&) = delete;
    RwLock& operator=(RwLock const&) = delete;

    [[nodiscard]] constexpr LockResult<RwLockReadGuard<T>> read() {
        using ok_t = RwLockReadGuard<T>;
        using err_t = PoisonError<RwLockReadGuard<T>>;
        rwlock_.read();
        return is_poisoned() ? result::Err<ok_t, err_t>(*this)
                             : result::Ok<ok_t, err_t>(*this);
    }

    [[nodiscard]] constexpr TryLockResult<RwLockReadGuard<T>> try_read() {
        using ok_t = RwLockReadGuard<T>;
        using err_t = TryLockError<RwLockReadGuard<T>>;
        if (!rwlock_.try_read())
            return result::Err<ok_t, err_t>(WouldBlock);  
        if (is_poisoned())
            return result::Err<ok_t, err_t>(poison_tag, *this);
        return result::Ok<ok_t, err_t>(*this);
    }

    [[nodiscard]] constexpr LockResult<RwLockReadGuard<T>> write() {
        using ok_t = RwLockReadGuard<T>;
        using err_t = PoisonError<RwLockReadGuard<T>>;
        rwlock_.write();
        return is_poisoned() ? result::Err<ok_t, err_t>(*this)
                             : result::Ok<ok_t, err_t>(*this);
    }

    [[nodiscard]] constexpr TryLockResult<RwLockReadGuard<T>> try_write() {
        using ok_t = RwLockReadGuard<T>;
        using err_t = TryLockError<RwLockReadGuard<T>>;
        if (!rwlock_.try_write())
            return result::Err<ok_t, err_t>(WouldBlock);  
        if (is_poisoned())
            return result::Err<ok_t, err_t>(poison_tag, *this);
        return result::Ok<ok_t, err_t>(*this);
    }

    [[nodiscard]] constexpr bool is_poisoned() const noexcept {
        return poison_.get();
    }

    [[nodiscard]] constexpr LockResult<T> into_inner() && noexcept {
#ifdef RUST_DEBUG
        if (!rwlock_.try_write())
            panic("RwLock::into_inner called while RwLock was locked");
        rwlock_.write_unlock();
#endif // RUST_DEBUG
        using ok_t = T;
        using err_t = PoisonError<T>;
        return is_poisoned() ? result::Err<ok_t, err_t>(std::move(value_)) 
                             : result::Ok<ok_t, err_t>(std::move(value_)); 
    }

    [[nodiscard]] constexpr LockResult<T&> get_mut() {
#ifdef RUST_DEBUG
        if (!rwlock_.try_write())
            panic("RwLock::get_mut called while RwLock was locked");
        mutex_.write_unlock();
#endif // RUST_DEBUG
        using ok_t = T&;
        using err_t = PoisonError<T&>;
        return is_poisoned() ? result::Err<ok_t, err_t>(value_) 
                             : result::Ok<ok_t, err_t>(value_);
    }
};

template<class T>
RwLock(T) -> RwLock<T>;

template<class T>
class RwLockReadGuard {
    RwLock<T>& rwlock_;
public:
    constexpr explicit RwLockReadGuard(RwLock<T>& rwlock) noexcept : rwlock_{rwlock} {}

    RwLockReadGuard(RwLockReadGuard const&) = delete;
    RwLockReadGuard& operator=(RwLockReadGuard const&) = delete;

    ~RwLockReadGuard() { rwlock_.rwlock_.read_unlock(); }

    [[nodiscard]] constexpr T const& operator*() const { return rwlock_.value_; }
};

template<class T>
class RwLockWriteGuard {
    RwLock<T>& rwlock_;
    Guard poison_;
public:
    constexpr explicit RwLockWriteGuard(RwLock<T>& rwlock) noexcept 
        : rwlock_{rwlock} 
        , poison_{[&rwlock] {
            auto res = rwlock_.poison_.borrow();
            return res.is_ok() ? res.unwrap_unsafe() 
                               : res.unwrap_err_unsafe().get_ref();
        }()}
    {}

    RwLockWriteGuard(RwLockWriteGuard const&) = delete;
    RwLockWriteGuard& operator=(RwLockWriteGuard const&) = delete;

    ~RwLockreadGuard() {
        rwlock_.poison_.done(poison_);
        rwlock_.rwlock_.write_unlock();
    }

    [[nodiscard]] constexpr T& operator*() { return rwlock_.value_; }
    [[nodiscard]] constexpr T const& operator*() const { return rwlock_.value_; }
};

} // namespace sync
} // namespace rust