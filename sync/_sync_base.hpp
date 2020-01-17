// _base.hpp

#pragma once

#include "../_detail.hpp"
#include "../result.hpp"
#include "../thread/thread.hpp"

#include <atomic>
#include <functional>
#include <utility>

namespace rust {
namespace sync {

template<class T>
class PoisonError {
    T value_;
public:
    template<class... Args, detail::enable_variadic_ctr<PoisonError<T>, Args...> = 0>
    constexpr explicit PoisonError(Args&&... args) : value_(std::forward<Args>(args)...) {}
    
    PoisonError(PoisonError const&) = default;
    PoisonError(PoisonError&&) = default;
    PoisonError& operator=(PoisonError const&) = default;
    PoisonError& operator=(PoisonError&&) = default;
    ~PoisonError() = default;

    [[nodiscard]] constexpr T& get_mut() noexcept { return value_; }
    [[nodiscard]] constexpr T const& get_ref() const noexcept { return value_; }
    [[nodiscard]] constexpr T&& into_inner() && { return std::move(value_); }
};

struct poison_tag_t{ constexpr poison_tag_t() noexcept = default; };
static constexpr poison_tag_t poison_tag{};

struct WouldBlock_t{ constexpr WouldBlock_t() noexcept = default; };
static constexpr WouldBlock_t WouldBlock{};

namespace {
template<class T>
struct TryLockError_storage_base {
    union {
        WouldBlock_t blocked_;
        PoisonError<T> poison_;
    };
    bool is_blocked_;
    
    TryLockError_storage_base(WouldBlock_t)
        : blocked_{}
        , is_blocked_{true}
    {}

    template<class... Args>
    TryLockError_storage_base(poison_tag_t, Args&&... args)
        : poison_(std::forward<Args>(args)...)
        , is_blocked_{false}
    {}
};

template<class T, bool IsTriviallyDestructible>
struct TryLockError_storage : public TryLockError_storage_base<T> {
    using base = TryLockError_storage_base<T>;

    template<class... Args>
    TryLockError_storage(Args&&... args) : base(std::forward<Args>(args)...) {}
};

template<class T>
struct TryLockError_storage<T, false> : public TryLockError_storage_base<T> {
    using base = TryLockError_storage_base<T>;
    
    template<class... Args>
    TryLockError_storage(Args&&... args) : base(std::forward<Args>(args)...) {}

    ~TryLockError_storage() {
        if (!this->is_blocked_)
            this->poison_.~PoisonError<T>();
    }
};

} // namespace

template<class Guard>
class TryLockError : private TryLockError_storage<Guard, std::is_trivially_destructible_v<Guard>> {
    using storage = TryLockError_storage<Guard, std::is_trivially_destructible_v<Guard>>;
public:
    template<class... Args, detail::enable_variadic_ctr<TryLockError<Guard>, Args...> = 0>
    constexpr explicit TryLockError(Args&&... args)
        : storage(std::forward<Args>(args)...)
    {}

    TryLockError(TryLockError const&) = default;
    TryLockError(TryLockError&&) = default;
    TryLockError& operator=(TryLockError const&) = default;
    TryLockError& operator=(TryLockError&&) = default; 

    ~TryLockError() = default;

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const& {
        if (this->is_blocked_)
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, WouldBlock{});
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, this->poison_);
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) && {
        if (this->is_blocked_)
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, WouldBlock{});
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(this->poison_));
    }

    [[nodiscard]] constexpr bool is_blocked() { return this->is_blocked_; }
    [[nodiscard]] constexpr bool is_poisoned() { return !this->is_blocked_; }
};

struct Guard { bool panicking; };

class Flag {
    std::atomic_bool failed_{false};
public:
    LockResult<Guard> borrow() const {
        auto const ret = Guard{thread::panicking()};
        return get() ? LockResult<Guard>(result::err_tag, ret) 
                     : LockResult<Guard>(result::ok_tag, ret);
    }

    void done(Guard const& guard) noexcept {
        if (!guard.panicking && thread::panicking())
            failed_.store(true, std::memory_order_relaxed);
    }

    bool get() const { return failed_.load(std::memory_order_relaxed); }
};

template<class T>
using LockResult = result::Result<T, PoisonError<T>>;

template<class T>
using TryLockResult = result::Result<T, TryLockError<T>>;


} // namespace sync
} // namespace rust