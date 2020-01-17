// rc.hpp

#pragma once

#include "../_detail.hpp"
#include "../_include.hpp"
#include "../debug/debug.hpp"
#include "../option.hpp"
#include "../result.hpp"

#include <memory>
#include <new>
#include <utility>

namespace rust {
namespace rc {

namespace {

template<class T>
struct Rc_storage_base {
public:
    template<class... Args>
    constexpr explicit Rc_storage_base(Args&&... args) {
        new(std::addressof(value_)) T(std::forward<Args>(args)...);
    }

    constexpr std::size_t weak_count() const noexcept {
        return weak_count_;
    }

    constexpr std::size_t strong_count() const noexcept {
        return strong_count_;
    }

    constexpr void dec_weak_count() noexcept {
        if (--weak_count_ == 0 && strong_count_ == 0)
            delete this;
    }

    constexpr void inc_weak_count() noexcept {
        ++weak_count_;
    }

    constexpr void dec_strong_count() {
        if (--strong_count_ == 0) {
            reinterpret_cast<T&>(value_).~T();
            if (--weak_count_ == 0) // "strong weak pointer"
                delete this;
        }
    }

    constexpr void inc_strong_count() noexcept {
        ++strong_count_;
    }

    constexpr T& get_val() noexcept { return reinterpret_cast<T&>(value_); }
    constexpr T const& get_val() const noexcept { return reinterpret_cast<T const&>(value_); }

    constexpr T* get_ptr() noexcept { return std::addressof(value_); }
    constexpr T const* get_ptr() const noexcept { return std::addressof(value_); }

private:
    std::aligned_storage_t<sizeof(T), alignof(T)> value_;
    std::size_t weak_count_{1};
    std::size_t strong_count_{1};
};

} // namespace

namespace Rc { template<class T> class Rc; }

namespace Weak {

template<class T>
class Weak {
    constexpr explicit Weak(Rc::Rc<T> const& rc) noexcept
        : base_{rc.base_}
    {
        base_->inc_weak_count();
    }

    constexpr Weak() noexcept : base_{nullptr} {} 

    friend Weak New();
    friend class Rc::Rc<T>;
public:
    constexpr explicit Weak(Weak const& w) noexcept
        : base_{w.base_}
    {
        base_->inc_weak_count();
    }

    constexpr explicit Weak(Weak&& w) noexcept
        : base_{std::exchange(w.base_, nullptr)}
    {}

    // operator=
    constexpr Weak& operator=(Weak const& w) noexcept {
        base_ = w.base_;
        base_->inc_weak_count();
    }

    constexpr Weak& operator=(Weak&& w) noexcept {
        base_ = std::exchange(w.base_, nullptr);
    }

    // destructor
    ~Weak() {
        if (base_)
            base_->dec_weak_count();
    }

    // upgrade
    constexpr option::Option<Rc::Rc<T>> upgrade() const noexcept {
        if (base_)
            if (base_->strong_count() > 0)
                return option::Option<Rc::Rc<T>>{*this};
        return option::None;
    }

    // ptr_eq
    constexpr bool ptr_eq(Weak const& other) const noexcept {
        return base_ == other.base_;
    }

private:
    Rc_storage_base<T>* base_;
};

template<class T>
Weak<T> New() { return {}; }

} // namespace Weak

namespace Rc {

template<class T>
class Rc {
    constexpr explicit Rc(Weak::Weak<T> const& w) noexcept 
        : base_{w.base_}
    {
        base_->inc_strong_count();
    }

    constexpr explicit Rc(T const* const ptr) noexcept
        : base_{reinterpret_cast<Rc_storage_base<T>*>(ptr)}
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<T, Args...>, int> = 0>
    constexpr explicit Rc(Args&&... args)
        : base_{new Rc_storage_base<T>(std::forward<Args>(args)...)}
    {}

    friend class Weak::Weak<T>;
    template<class T> friend constexpr Weak::Weak<T> downgrade(Rc const&);
    friend constexpr Rc from_raw(T const* const);
    friend constexpr T const* into_raw(Rc&&);
    friend constexpr option::Option<T&> get_mut(Rc<T>&);
    friend constexpr T& make_mut(Rc&);
    template<class... Args> friend constexpr Rc New(Args&&...);
    // pin
    friend constexpr bool ptr_eq(Rc const&, Rc const&);
    friend constexpr std::size_t strong_count(Rc const&);
    friend constexpr std::size_t weak_count(Rc const&);
    friend constexpr result::Result<T, Rc> try_unwrap(Rc&&);

public:
    constexpr Rc(Rc const& other) noexcept 
        : base_{other.base_}
    {
        base_->inc_strong_count();
    }

    constexpr Rc(Rc&& other) noexcept
        : base_{std::exchange(other.base_, nullptr)}
    {}

    // operator=
    constexpr Rc& operator=(Rc const& rhs) {
        base_->dec_strong_count();
        base_ = rhs.base_;
        base_->inc_strong_count();
        return *this;
    }

    constexpr Rc& operator=(Rc&& rhs) noexcept {
        base_->dec_strong_count();
        base_ = std::exchange(rhs.base_, nullptr);
        return *this;
    }

    // destructor
    ~Rc() {
        if (base_)
            base_->dec_strong_count();
    }

    // operator*
    constexpr T& operator*() noexcept {
        return base_->get_val();
    }

    constexpr T const& operator*() const noexcept {
        return base_->get_val();
    }

    // operator->
    constexpr T* operator->() noexcept {
        return base_->get_ptr();
    }

    constexpr T const* operator->() const noexcept {
        return base_->get_get();
    }

private:
    Rc_storage_base<T>* base_;
};

template<class T>
Rc(T) -> Rc<T>;

template<class T>
inline constexpr Rc<T> New(T&& t) {
    return Rc<T>{std::forward<T>(t)};
}

template<class T, class... Args>
inline constexpr Rc<T> New(Args&&... args) {
    return Rc<T>{std::forward<Args>(args)...};
}

template<class T>
inline constexpr Weak::Weak<T> downgrade(Rc<T> const& r) noexcept {
    return Weak::Weak<T>(r);
}

template<class T>
inline constexpr Rc<T> from_raw(T const* const ptr) noexcept {
    return Rc<T>{ptr};
}

template<class T>
inline constexpr T const* into_raw(Rc<T>&& r) noexcept {
    auto const tmp = r.base_->get_ptr();
    r.base_ = nullptr;
    return tmp;
}

template<class T>
inline constexpr option::Option<T&> get_mut(Rc<T>& r) {
    if (weak_count(r) == 0 && strong_count(r) == 1)
        return option::Option<T&>{r.base_->get_val()};
    return option::None;
}

template<class T>
inline constexpr T& make_mut(Rc<T>& r) {
    if (strong_count(r) != 1)
        r.base_ = new Rc_storage_base<T>(r.base_->get_val());
    else if (weak_count(r) != 0) {
        auto swap = New<T>(std::move(r.base_->get_val()));
        std::swap(r.base_, swap.base_);
        swap.base_->dec_strong_count();
        swap.base_->dec_weak_count();
        swap.base_ = nullptr;
    }
    return r.base_->get_val();
}

// pin

template<class T>
inline constexpr bool ptr_eq(Rc<T> const& a, Rc<T> const& b) noexcept {
    return a.base_ == b.base_;
}

template<class T>
inline constexpr std::size_t strong_count(Rc<T> const& r) noexcept {
    return r.base_->strong_count();
}

template<class T>
inline constexpr std::size_t weak_count(Rc<T> const& r) noexcept {
    return r.base_->weak_count() - 1;
}

template<class T>
inline constexpr result::Result<T, Rc<T>> try_unwrap(Rc<T>&& r) {
    if (strong_count(r) == 1) {
        r.base_->dec_strong_count();
        auto& val = r.base_->get_val();
        r.base_ = nullptr;
        return result::Ok<T, Rc<T>>(std::move(val));
    }
    return result::Err<T, Rc<T>>(std::move(r));
}

} // namespace Rc

} // namespace rc
} // namespace rust
