// box.hpp

#pragma once

#include "../_include.hpp"
#include "../_detail.hpp"
#ifdef RUST_DEBUG
    #include "../panic.hpp"
#endif // RUST_DEBUG

#include <type_traits>
#include <utility>

namespace rust {
namespace boxed {
namespace Box {

template<class T>
class Box {
    T* ptr_;

    template<class... Args, std::enable_if_t<std::is_constructible_v<T, Args...>, int> = 0>
    constexpr Box(Args&&... args) : ptr_{new T(std::forward<Args>(args)...)} {}
    
    constexpr Box(T* const ptr) noexcept : ptr_{ptr} {}

    template<class... Args> 
    friend Box New(Args&&... args);
    friend Box from_raw(T* const ptr);
    friend T* into_raw(Box&& b);
    friend T& leak(Box&& b);
public:
    constexpr Box(Box const&) = delete;
    
    constexpr Box(Box&& other) noexcept
        : ptr_{std::exchange(other.ptr_, nullptr)}
    {}

    // operator=
    Box& operator=(Box const&) = delete;

    Box& operator=(Box&& rhs) noexcept {
        if (ptr_)
            delete ptr_;
        ptr_ = std::exchange(rhs.ptr_, nullptr);
    }

    // destructor
    ~Box() { delete ptr_; }

    // operator*
    [[nodiscard]] constexpr T& operator*() { 
#ifdef RUST_DEBUG
        if (!ptr_)
            panic("rust::boxed::Box::Box::operator* trying to dereference a null pointer");
#endif
        return *ptr_; 
    }

    [[nodiscard]] constexpr T const& operator*() const {
#ifdef RUST_DEBUG
        if (!ptr_)
            panic("rust::boxed::Box::Box::operator* trying to dereference a null pointer");
#endif
        return *ptr_;
    }

    // operator->
    [[nodiscard]] constexpr T* operator->() {
#ifdef RUST_DEBUG
        if (!ptr_)
            panic("rust::boxed::Box::Box::operator-> trying to access a null pointer");
#endif
        return ptr_;
    }

    [[nodiscard]] constexpr T const* operator->() const {
#ifdef RUST_DEBUG
        if (!ptr_)
            panic("rust::boxed::Box::Box::operator-> trying to access a null pointer");
#endif
        return ptr_;
    }
};

template<class T>
Box(T) -> Box<T>;

template<class T>
Box<T> New(T&& t) {
    return Box<T>{std::forward<T>(t)};
}

template<class T, class... Args, std::enable_if_t<(sizeof...(Args) > 1) || (sizeof...(Args) == 0), int> = 0>
Box<T> New(Args&&... args) {
    return Box<T>{std::forward<Args>(args)...};
}

template<class T>
Box<T> from_raw(T* const ptr) noexcept {
    return Box<T>{ptr};
}

template<class T>
T* into_raw(Box<T>&& b) noexcept {
    auto const tmp = std::exchange(b.ptr_, nullptr);
    return tmp;
}

template<class T>
T& leak(Box<T>&& b) noexcept {
#ifdef RUST_DEBUG
        if (!b.ptr_)
            panic("rust::boxed::Box::leak trying to access a null pointer");
#endif
        T& tmp = *b.ptr_;
        b.ptr_ = nullptr;
        return tmp;
}

// into_pin
// pin

} // namespace Box
} // namespace boxed
} // namespace rust