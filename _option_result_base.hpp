// _option_result_base.hpp

#pragma once

#include "_include.hpp"
#include "_detail.hpp"
#include "panic.hpp"

#include <cassert>
#include <exception>
#include <functional>
#include <iostream>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace rust {

namespace option {

template<class T>
class Option;

namespace { struct do_not_use{}; }

struct None_t { constexpr explicit None_t(do_not_use, do_not_use) noexcept {} };
static constexpr None_t None{do_not_use{}, do_not_use{}};

} // namespace option

namespace result {

template<class T, class E>
class Result;

struct err_tag_t { err_tag_t() = default; };
static constexpr err_tag_t err_tag{};

struct ok_tag_t { ok_tag_t() = default; };
static constexpr ok_tag_t ok_tag{};

// ------------------------------------------------------------------------------------------
// Result 

namespace detail {

// https://stackoverflow.com/questions/26744589/what-is-a-proper-way-to-implement-is-swappable-to-test-for-the-swappable-concept
namespace swap_adl_tests {
    // if swap ADL finds this then it would call std::swap otherwise (same
    // signature)
    struct tag {};

    template <class T> tag swap(T&, T&);
    template <class T, std::size_t N> tag swap(T(&a)[N], T(&b)[N]);

    // helper functions to test if an unqualified swap is possible, and if it
    // becomes std::swap
    template <class, class> std::false_type can_swap(...) noexcept(false);
    template <class T, class U,
        class = decltype(swap(std::declval<T&>(), std::declval<U&>()))>
        std::true_type can_swap(int) noexcept(noexcept(swap(std::declval<T&>(),
        std::declval<U&>())));

    template <class, class> std::false_type uses_std(...);
    template <class T, class U>
    std::is_same<decltype(swap(std::declval<T&>(), std::declval<U&>())), tag>
        uses_std(int);

    template <class T>
    struct is_std_swap_noexcept
        : std::integral_constant<bool,
        std::is_nothrow_move_constructible<T>::value&&
        std::is_nothrow_move_assignable<T>::value> {};

    template <class T, std::size_t N>
    struct is_std_swap_noexcept<T[N]> : is_std_swap_noexcept<T> {};

    template <class T, class U>
    struct is_adl_swap_noexcept
        : std::integral_constant<bool, noexcept(can_swap<T, U>(0))> {};
} // namespace swap_adl_tests

template <class T, class U = T>
struct is_swappable 
    : std::integral_constant<bool, 
    decltype(detail::swap_adl_tests::can_swap<T, U>(0))::value &&
    (!decltype(detail::swap_adl_tests::uses_std<T, U>(0))::value ||
    (std::is_move_assignable<T>::value &&
    std::is_move_constructible<T>::value))> {};

template <class T, std::size_t N>
struct is_swappable<T[N], T[N]>
    : std::integral_constant<bool,
    decltype(detail::swap_adl_tests::can_swap<T[N], T[N]>(0))::value &&
    (!decltype(detail::swap_adl_tests::uses_std<T[N], T[N]>(0))::value ||
    is_swappable<T, T>::value)> {};

template <class T, class U = T>
struct is_nothrow_swappable
    : std::integral_constant<bool,
    is_swappable<T, U>::value &&
    ((decltype(detail::swap_adl_tests::uses_std<T, U>(0))::value && 
    detail::swap_adl_tests::is_std_swap_noexcept<T>::value) ||
    (!decltype(detail::swap_adl_tests::uses_std<T, U>(0))::value &&
    detail::swap_adl_tests::is_adl_swap_noexcept<T,U>::value))> {};

template <class T, class E, class U>
using Result_enable_forward_value = std::enable_if_t<
    std::is_constructible_v<T, U&&> &&
    !std::is_same_v<std::decay_t<U>, ok_tag_t> &&
    !std::is_same_v<std::decay_t<U>, err_tag_t> &&
    !std::is_same_v<std::decay_t<U>, Result<T, E>>>;

template <class T, class E, class U, class G, class UR, class GR>
using Result_enable_from_other = std::enable_if_t<
    std::is_constructible_v<T, UR> &&
    std::is_constructible_v<E, GR> &&
    !std::is_constructible_v<T, Result<U, G>&> &&
    !std::is_constructible_v<T, Result<U, G>&&> &&
    !std::is_constructible_v<T, Result<U, G> const&> &&
    !std::is_constructible_v<T, Result<U, G> const&&> &&
    !std::is_convertible_v<Result<U, G>&, T> &&
    !std::is_convertible_v<Result<U, G>&&, T> &&
    !std::is_convertible_v<Result<U, G> const&, T> &&
    !std::is_convertible_v<Result<U, G> const&&, T>>;

template <class T, class U>
using is_void_or = std::conditional_t<std::is_void_v<T>, std::true_type, U>;

template <class T>
using is_copy_constructible_or_void = is_void_or<T, std::is_copy_constructible<T>>;

template <class T>
using is_move_constructible_or_void = is_void_or<T, std::is_move_constructible<T>>;

template <class T>
using is_copy_assignable_or_void = is_void_or<T, std::is_copy_assignable<T>>;

template <class T>
using is_move_assignable_or_void = is_void_or<T, std::is_move_assignable<T>>;

} // namespace detail

namespace detail {

struct no_init_t {};
static constexpr no_init_t no_init{};

// Implements the storage of the values, and ensures that the destructor is
// trivial if it can be.
//
// This specialization is for where neither `T` or `E` is trivially
// destructible, so the destructors must be called on destruction of the
// `Result`
template <class T, class E, bool = std::is_trivially_destructible_v<T>, bool = std::is_trivially_destructible_v<E>>
struct Result_storage_base {

    constexpr Result_storage_base() 
        : value_(T{})
        , is_ok_(true) 
    {}

    constexpr Result_storage_base(no_init_t) 
        : no_init_()
        , is_ok_(false) 
    {}

    template <class... Args, std::enable_if_t<std::is_constructible_v<T, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, Args&&... args)
        : value_(std::forward<Args>(args)...)
        , is_ok_(true) 
    {}

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, std::initializer_list<U> il, Args&&... args)
        : value_(il, std::forward<Args>(args)...)
        , is_ok_(true) 
    {}
    
    template <class... Args, std::enable_if_t<std::is_constructible_v<E, Args&&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, Args&&... args)
        : err_(std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, std::initializer_list<U> il, Args&&... args)
        : err_(il, std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    ~Result_storage_base() {
        if (is_ok_)
            value_.~T();
        else
            err_.~E();
    }

    union {
        T value_;
        E err_;
        char no_init_;
    };
    bool is_ok_;
};

// This specialization is for when both `T` and `E` are trivially-destructible,
// so the destructor of the `Result` can be trivial.
template<class T, class E> 
struct Result_storage_base<T, E, true, true> {
    constexpr Result_storage_base() 
        : value_(T{})
        , is_ok_(true) 
    {}
    
    constexpr Result_storage_base(no_init_t) 
        : no_init_()
        , is_ok_(false) 
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<T, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, Args&&... args)
        : value_(std::forward<Args>(args)...)
        , is_ok_(true) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, std::initializer_list<U> il, Args&&... args)
        : value_(il, std::forward<Args>(args)...)
        , is_ok_(true) 
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<E, Args&&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, Args&&... args)
        : err_(std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, std::initializer_list<U> il, Args&&... args)
        : err_(il, std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    ~Result_storage_base() = default;

    union {
        T value_;
        E err_;
        char no_init_;
    };
    bool is_ok_;
};

// T is trivial, E is not.
template<class T, class E> 
struct Result_storage_base<T, E, true, false> {
    constexpr Result_storage_base() 
        : value_(T{})
        , is_ok_(true) 
    {}
    
    constexpr Result_storage_base(no_init_t)
        : no_init_()
        , is_ok_(false) 
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<T, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, Args&&... args)
        : value_(std::forward<Args>(args)...)
        , is_ok_(true) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, std::initializer_list<U> il, Args&&... args)
        : value_(il, std::forward<Args>(args)...)
        , is_ok_(true) 
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<E, Args&&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, Args&&... args)
        : err_(std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U> &, Args &&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, std::initializer_list<U> il, Args &&... args)
        : err_(il, std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    ~Result_storage_base() {
        if (!is_ok_)
            err_.~E();
    }

    union {
        T value_;
        E err_;
        char no_init_;
    };
    bool is_ok_;
};

// E is trivial, T is not.
template<class T, class E> 
struct Result_storage_base<T, E, false, true> {
    constexpr Result_storage_base() 
        : value_(T{})
        , is_ok_(true) 
    {}
  
    constexpr Result_storage_base(no_init_t) 
        : no_init_()
        , is_ok_(false) 
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<T, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, Args&&... args)
        : value_(std::forward<Args>(args)...)
        , is_ok_(true) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr Result_storage_base(ok_tag_t, std::initializer_list<U> il, Args&&... args)
        : value_(il, std::forward<Args>(args)...)
        , is_ok_(true) 
    {}
  
    template<class... Args, std::enable_if_t<std::is_constructible_v<E, Args&&...>>* = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, Args&&... args)
        : err_(std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args&&...>> * = nullptr>
    constexpr explicit Result_storage_base(err_tag_t, std::initializer_list<U> il, Args&&... args)
        : err_(il, std::forward<Args>(args)...)
        , is_ok_(false) 
    {}

    ~Result_storage_base() {
        if (is_ok_)
            value_.~T();
    } 
    
    union {
        T value_;
        E err_;
        char no_init_;
    };
    bool is_ok_;
};

// This base class provides some handy member functions which can be used in
// further derived classes
template <class T, class E>
struct Result_operations_base : Result_storage_base<T, E> {
    using Result_storage_base<T, E>::Result_storage_base;

    template <class... Args> 
    void construct(Args&&... args) noexcept {
        new (std::addressof(this->value_)) T(std::forward<Args>(args)...);
        this->is_ok_ = true;
    }

    template <class Rhs> 
    void construct_with(Rhs&& rhs) noexcept {
        new (std::addressof(this->value_)) T(std::forward<Rhs>(rhs).get());
        this->is_ok_ = true;
    }

    template <class... Args> 
    void construct_err(Args&&... args) noexcept {
        new (std::addressof(this->err_)) E(std::forward<Args>(args)...);
        this->is_ok_ = false;
    }

#ifdef RUST_EXCEPTIONS_ENABLED

    // These assign overloads ensure that the most efficient assignment
    // implementation is used while maintaining the strong exception guarantee.
    // The problematic case is where rhs has a value, but *this does not.
    //
    // This overload handles the case where we can just copy-construct `T`
    // directly into place without throwing.
    template <class U = T, std::enable_if_t<std::is_nothrow_copy_constructible_v<U>>* = nullptr>
    void assign(Result_operations_base const& rhs) noexcept {
        if (!this->is_ok_ && rhs.is_ok_) {
            geterr().~E();
            construct(rhs.get());
        } 
        else
            assign_common(rhs);
    }

    // This overload handles the case where we can attempt to create a copy of
    // `T`, then no-throw move it into place if the copy was successful.
    template <class U = T, std::enable_if_t<!std::is_nothrow_copy_constructible_v<U> && std::is_nothrow_move_constructible_v<U>>* = nullptr>
    void assign(const Result_operations_base &rhs) noexcept {
        if (!this->is_ok_ && rhs.is_ok_) {
            T tmp = rhs.get();
            geterr().~E();
            construct(std::move(tmp));
        } 
        else
            assign_common(rhs);
    }

    // This overload is the worst-case, where we have to move-construct the
    // err value into temporary storage, then try to copy the T into place.
    // If the construction succeeds, then everything is fine, but if it throws,
    // then we move the old err value back into place before rethrowing the
    // exception.
    template <class U = T, std::enable_if_t<!std::is_nothrow_copy_constructible_v<U> && !std::is_nothrow_move_constructible_v<U>>* = nullptr>
    void assign(const Result_operations_base &rhs) {
        if (!this->is_ok_ && rhs.is_ok_) {
            auto tmp = std::move(geterr());
            geterr().~E();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                construct(rhs.get());
            }
            catch (...) {
                geterr() = std::move(tmp);
                throw;
            }
#else // RUST_EXCEPTIONS_ENABLED
            construct(rhs.get());
#endif // RUST_EXCEPTIONS_ENABLED
        }
        else
            assign_common(rhs);
    }

    // These overloads do the same as above, but for rvalues
    template <class U = T, std::enable_if_t<std::is_nothrow_move_constructible_v<U>>* = nullptr>
    void assign(Result_operations_base&& rhs) noexcept {
        if (!this->is_ok_ && rhs.is_ok_) {
            geterr().~E();
            construct(std::move(rhs).get());
        } 
        else
            assign_common(std::move(rhs));
    }

    template <class U = T, std::enable_if_t<!std::is_nothrow_move_constructible_v<U>>* = nullptr>
    void assign(Result_operations_base &&rhs) {
        if (!this->is_ok_ && rhs.is_ok_) {
            auto tmp = std::move(geterr());
            geterr().~E();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                construct(std::move(rhs).get());
            }
            catch (...) {
                geterr() = std::move(tmp);
                throw;
            }
#else // RUST_EXCEPTIONS_ENABLED
            construct(std::move(rhs).get());
#endif // RUST_EXCEPTIONS_ENABLED
        } 
        else
            assign_common(std::move(rhs));
    }

#else // ^^^RUST_EXCEPTIONS_ENABLED^^^ 

    void assign(const Result_operations_base &rhs) noexcept {
        if (!this->is_ok_ && rhs.is_ok_) {
            geterr().~E();
            construct(rhs.get());
        } 
        else
            assign_common(rhs);
    }

    void assign(Result_operations_base &&rhs) noexcept {
        if (!this->is_ok_ && rhs.is_ok_) {
            geterr().~E();
            construct(std::move(rhs).get());
        } 
        else
            assign_common(rhs);
    }

#endif // RUST_EXCEPTIONS_ENABLED

    // The common part of move/copy assigning
    template<class Rhs> 
    void assign_common(Rhs &&rhs) {
        if (this->is_ok_) {
            if (rhs.is_ok_)
                get() = std::forward<Rhs>(rhs).get();
            else {
		        destroy_val();
                construct_err(std::forward<Rhs>(rhs).geterr());
            }
        } 
        else {
            if (!rhs.is_ok_)
                geterr() = std::forward<Rhs>(rhs).geterr();
        }
    }

    constexpr bool is_ok() const noexcept { return this->is_ok_; }

    constexpr T& get() & { return this->value_; }
    constexpr T const& get() const& { return this->value_; }
    constexpr T&& get() && { return std::move(this->value_); }
    constexpr T const&& get() const&& { return std::move(this->value_); }

    constexpr E& geterr() & { return this->err_; }
    constexpr E const& geterr() const& { return this->err_; }
    constexpr E&& geterr() && { return std::move(this->err_); }
    constexpr E const&& geterr() const&& { return std::move(this->err_); }

    constexpr void destroy_val() {
	    get().~T();
    }
};

// This base class provides some handy member functions which can be used in
// further derived classes

// This class manages conditionally having a trivial copy constructor
// This specialization is for when T and E are trivially copy constructible
template<class T, class E, bool = is_void_or<T, std::is_trivially_copy_constructible<T>>::value && std::is_trivially_copy_constructible_v<E>>
struct Result_copy_base : Result_operations_base<T, E> {
    using Result_operations_base<T, E>::Result_operations_base;
};

// This specialization is for when T or E are not trivially copy constructible
template<class T, class E>
struct Result_copy_base<T, E, false> : Result_operations_base<T, E> {
    using Result_operations_base<T, E>::Result_operations_base;

    Result_copy_base() = default;

    Result_copy_base(const Result_copy_base &rhs)
        : Result_operations_base<T, E>(no_init) 
    {
        if (rhs.is_ok())
            this->construct_with(rhs);
        else
            this->construct_err(rhs.geterr());
    }

    Result_copy_base(Result_copy_base &&rhs) = default;
    Result_copy_base &operator=(const Result_copy_base &rhs) = default;
    Result_copy_base &operator=(Result_copy_base &&rhs) = default;
};

// This class manages conditionally having a trivial move constructor
template<class T, class E, bool = is_void_or<T, std::is_trivially_move_constructible<T>>::value && std::is_trivially_move_constructible_v<E>>
struct Result_move_base : Result_copy_base<T, E> {
    using Result_copy_base<T, E>::Result_copy_base;
};

template <class T, class E>
struct Result_move_base<T, E, false> : Result_copy_base<T, E> {
    using Result_copy_base<T, E>::Result_copy_base;

    Result_move_base() = default;
    Result_move_base(const Result_move_base &rhs) = default;

    Result_move_base(Result_move_base &&rhs) 
    noexcept(std::is_nothrow_move_constructible<T>::value)
        : Result_copy_base<T, E>(no_init) 
    {
        if (rhs.is_ok())
            this->construct_with(std::move(rhs));
        else
            this->construct_err(std::move(rhs.geterr()));
    }

    Result_move_base &operator=(const Result_move_base &rhs) = default;
    Result_move_base &operator=(Result_move_base &&rhs) = default;
};

// This class manages conditionally having a trivial copy assignment operator
template <class T, class E, bool = 
    is_void_or<T, std::conjunction<std::is_trivially_copy_assignable<T>, std::is_trivially_copy_constructible<T>, std::is_trivially_destructible<T>>>::value
    && std::is_trivially_copy_assignable_v<E> && std::is_trivially_copy_constructible_v<E> && std::is_trivially_destructible_v<E>>
struct Result_copy_assign_base : Result_move_base<T, E> {
    using Result_move_base<T, E>::Result_move_base;
};

template <class T, class E>
struct Result_copy_assign_base<T, E, false> : Result_move_base<T, E> {
    using Result_move_base<T, E>::Result_move_base;

    Result_copy_assign_base() = default;
    Result_copy_assign_base(Result_copy_assign_base const& rhs) = default;
    Result_copy_assign_base(Result_copy_assign_base&& rhs) = default;

    Result_copy_assign_base &operator=(Result_copy_assign_base const& rhs) {
        this->assign(rhs);
        return *this;
    }

    Result_copy_assign_base& operator=(Result_copy_assign_base&& rhs) = default;
};

// This class manages conditionally having a trivial move assignment operator
template<class T, class E, bool = 
    is_void_or<T, std::conjunction<std::is_trivially_destructible<T>, std::is_trivially_move_constructible<T>, std::is_trivially_move_assignable<T>>>::value 
    && std::is_trivially_destructible_v<E> && std::is_trivially_move_constructible_v<E> && std::is_trivially_move_assignable_v<E>>
struct Result_move_assign_base : Result_copy_assign_base<T, E> {
    using Result_copy_assign_base<T, E>::Result_copy_assign_base;
};

template<class T, class E>
struct Result_move_assign_base<T, E, false> : Result_copy_assign_base<T, E> {
    using Result_copy_assign_base<T, E>::Result_copy_assign_base;

    Result_move_assign_base() = default;
    Result_move_assign_base(Result_move_assign_base const& rhs) = default;

    Result_move_assign_base(Result_move_assign_base&& rhs) = default;

    Result_move_assign_base& operator=(Result_move_assign_base const& rhs) = default;

    Result_move_assign_base& operator=(Result_move_assign_base&& rhs) 
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
        this->assign(std::move(rhs));
        return *this;
    }
};

// Result_delete_ctor_base will conditionally delete copy and move
// constructors depending on whether T is copy/move constructible
template<class T, class E,
          bool EnableCopy = (is_copy_constructible_or_void<T>::value &&
                             std::is_copy_constructible_v<E>),
          bool EnableMove = (is_move_constructible_or_void<T>::value &&
                             std::is_move_constructible_v<E>)>
struct Result_delete_ctor_base {
    Result_delete_ctor_base() = default;
    Result_delete_ctor_base(Result_delete_ctor_base const&) = default;
    Result_delete_ctor_base(Result_delete_ctor_base&&) noexcept = default;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base const&) = default;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base&&) noexcept = default;
};

template<class T, class E>
struct Result_delete_ctor_base<T, E, true, false> {
    Result_delete_ctor_base() = default;
    Result_delete_ctor_base(Result_delete_ctor_base const&) = default;
    Result_delete_ctor_base(Result_delete_ctor_base &&) noexcept = delete;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base const&) = default;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base&&) noexcept = default;
};

template<class T, class E>
struct Result_delete_ctor_base<T, E, false, true> {
    Result_delete_ctor_base() = default;
    Result_delete_ctor_base(Result_delete_ctor_base const&) = delete;
    Result_delete_ctor_base(Result_delete_ctor_base&&) noexcept = default;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base const&) = default;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base&&) noexcept = default;
};

template<class T, class E>
struct Result_delete_ctor_base<T, E, false, false> {
    Result_delete_ctor_base() = default;
    Result_delete_ctor_base(Result_delete_ctor_base const&) = delete;
    Result_delete_ctor_base(Result_delete_ctor_base&&) noexcept = delete;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base const&) = default;
    Result_delete_ctor_base& operator=(Result_delete_ctor_base&&) noexcept = default;
};

// Result_delete_assign_base will conditionally delete copy and move
// constructors depending on whether T and E are copy/move constructible +
// assignable
template<class T, class E,
          bool EnableCopy = (is_copy_constructible_or_void<T>::value &&
                             std::is_copy_constructible_v<E> &&
                             is_copy_assignable_or_void<T>::value &&
                             std::is_copy_assignable_v<E>),
          bool EnableMove = (is_move_constructible_or_void<T>::value &&
                             std::is_move_constructible_v<E> &&
                             is_move_assignable_or_void<T>::value &&
                             std::is_move_assignable_v<E>)>
struct Result_delete_assign_base {
    Result_delete_assign_base() = default;
    Result_delete_assign_base(Result_delete_assign_base const&) = default;
    Result_delete_assign_base(Result_delete_assign_base&&) noexcept = default;
    Result_delete_assign_base& operator=(Result_delete_assign_base const&) = default;
    Result_delete_assign_base& operator=(Result_delete_assign_base&&) noexcept = default;
};

template<class T, class E>
struct Result_delete_assign_base<T, E, true, false> {
    Result_delete_assign_base() = default;
    Result_delete_assign_base(Result_delete_assign_base const&) = default;
    Result_delete_assign_base(Result_delete_assign_base&&) noexcept = default;
    Result_delete_assign_base& operator=(Result_delete_assign_base const&) = default;
    Result_delete_assign_base& operator=(Result_delete_assign_base&&) noexcept = delete;
};

template<class T, class E>
struct Result_delete_assign_base<T, E, false, true> {
    Result_delete_assign_base() = default;
    Result_delete_assign_base(Result_delete_assign_base const&) = default;
    Result_delete_assign_base(Result_delete_assign_base&&) noexcept = default;
    Result_delete_assign_base& operator=(Result_delete_assign_base const&) = delete;
    Result_delete_assign_base& operator=(Result_delete_assign_base&&) noexcept = default;
};

template<class T, class E>
struct Result_delete_assign_base<T, E, false, false> {
    Result_delete_assign_base() = default;
    Result_delete_assign_base(Result_delete_assign_base const&) = default;
    Result_delete_assign_base(Result_delete_assign_base&&) noexcept = default;
    Result_delete_assign_base& operator=(Result_delete_assign_base const&) = delete;
    Result_delete_assign_base& operator=(Result_delete_assign_base&&) noexcept = delete;
};

// This is needed to be able to construct the Result_default_ctor_base which
// follows, while still conditionally deleting the default constructor.
struct default_constructor_tag {
    explicit constexpr default_constructor_tag() = default;
};

// Result_default_ctor_base will ensure that Result has a deleted default
// consturctor if T is not default constructible.
// This specialization is for when T is default constructible
template <class T, class E, bool Enable = std::is_default_constructible_v<T> || std::is_void_v<T>>
struct Result_default_ctor_base {
    constexpr Result_default_ctor_base() noexcept = default;
    constexpr Result_default_ctor_base(Result_default_ctor_base const&) noexcept = default;
    constexpr Result_default_ctor_base(Result_default_ctor_base&&) noexcept = default;
    Result_default_ctor_base& operator=(Result_default_ctor_base const&) noexcept = default;
    Result_default_ctor_base& operator=(Result_default_ctor_base&&) noexcept = default;
    constexpr explicit Result_default_ctor_base(default_constructor_tag) {}
};

// This specialization is for when T is not default constructible
template <class T, class E> struct Result_default_ctor_base<T, E, false> {
    constexpr Result_default_ctor_base() noexcept = delete;
    constexpr Result_default_ctor_base(Result_default_ctor_base const&) noexcept = default;
    constexpr Result_default_ctor_base(Result_default_ctor_base&&) noexcept = default;
    Result_default_ctor_base& operator=(Result_default_ctor_base const&) noexcept = default;
    Result_default_ctor_base& operator=(Result_default_ctor_base&&) noexcept = default;
    constexpr explicit Result_default_ctor_base(default_constructor_tag) {}
};

} // namespace detail

template <class T, class E>
class Result : private detail::Result_move_assign_base<T, E>,
               private detail::Result_delete_ctor_base<T, E>,
               private detail::Result_delete_assign_base<T, E>,
               private detail::Result_default_ctor_base<T, E> {

    static_assert(!std::is_void_v<T>, "T must not be void");
    static_assert(!std::is_void_v<E>, "E must not be void");
    static_assert(!std::is_same_v<T, std::remove_cv_t<ok_tag_t>>, "T must not be ok_tag_t");
    static_assert(!std::is_same_v<T, std::remove_cv_t<err_tag_t>>, "T must not be err_tag_t");

    // val_ptr
    T* val_ptr() { return std::addressof(this->value_); }
    T const* val_ptr() const { return std::addressof(this->value_); }    
    
    // err_ptr
    E* err_ptr() { return std::addressof(this->err_); }
    E const* err_ptr() const { return std::addressof(this->err_); }    

    // get_val
    constexpr T& get_val() { return this->value_; }
    constexpr T const& get_val() const { return this->value_; }
    
    // get_err
    constexpr E& get_err() { return this->err_; }
    constexpr E const& get_err() const { return this->err_; }

    using impl_base = detail::Result_move_assign_base<T, E>;
    using ctor_base = detail::Result_default_ctor_base<T, E>;

public:
    using ok_type = T;
    using err_type = E;

    // constructors 
    constexpr Result() = default;
    constexpr Result(Result const& rhs) = default;
    constexpr Result(Result&& rhs) = default;
    Result& operator=(Result const& rhs) = default;
    Result& operator=(Result&& rhs) = default;

    template <class... Args, std::enable_if_t<std::is_constructible_v<T, Args&&...>>* = nullptr>
    constexpr Result(ok_tag_t, Args&&... args)
        : impl_base(ok_tag, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr Result(ok_tag_t, std::initializer_list<U> il, Args &&... args)
        : impl_base(ok_tag, il, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template<class... Args, std::enable_if_t<std::is_constructible_v<E, Args&&...>>* = nullptr>
    constexpr explicit Result(err_tag_t, Args&&... args)
        : impl_base(err_tag, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr explicit Result(err_tag_t, std::initializer_list<U> il, Args&&... args)
        : impl_base(err_tag, il, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template<class U, class G, std::enable_if_t<!(std::is_convertible_v<U const&, T> && std::is_convertible_v<G const&, E>)>* = nullptr,
                               detail::Result_enable_from_other<T, E, U, G, U const&, G const&>* = nullptr>
    explicit constexpr Result(Result<U, G> const& rhs)
        : ctor_base(detail::default_constructor_tag{}) 
    {
        if (rhs.is_ok())
            this->construct(rhs.unwrap_unsafe());
        else
            this->construct_err(rhs.unwrap_err_unsafe());        
    }

    template <class U, class G, std::enable_if_t<(std::is_convertible_v<U const&, T> && std::is_convertible_v<G const&, E>)>* = nullptr,
                                detail::Result_enable_from_other<T, E, U, G, U const&, G const&>* = nullptr>
    constexpr Result(Result<U, G> const& rhs)
        : ctor_base(detail::default_constructor_tag{}) 
    {
        if (rhs.is_ok())
            this->construct(rhs.unwrap_unsafe());
        else
            this->construct_err(rhs.unwrap_err_unsafe());             
    }

    template<class U, class G, std::enable_if_t<!(std::is_convertible_v<U&&, T> && std::is_convertible_v<G&&, E>)>* = nullptr,
                               detail::Result_enable_from_other<T, E, U, G, U&&, G&&>* = nullptr>
    explicit constexpr Result(Result<U, G>&& rhs)
        : ctor_base(detail::default_constructor_tag{}) 
    {
        if (rhs.is_ok())
            this->construct(std::move(rhs.unwrap_unsafe()));
        else
            this->construct_err(std::move(rhs.unwrap_err_unsafe()));                  
    }

    template<class U, class G, std::enable_if_t<(std::is_convertible_v<U&&, T> && std::is_convertible_v<G&&, E>)>* = nullptr,
                               detail::Result_enable_from_other<T, E, U, G, U&&, G&&>* = nullptr>
    constexpr Result(Result<U, G>&& rhs)
        : ctor_base(detail::default_constructor_tag{}) 
    {
        if (rhs.is_ok())
            this->construct(std::move(rhs.unwrap_unsafe()));
        else
            this->construct_err(std::move(rhs.unwrap_err_unsafe()));                       
    }

    template <class U = T, std::enable_if_t<!std::is_convertible_v<U&&, T>>* = nullptr,
                           detail::Result_enable_forward_value<T, E, U>* = nullptr>
    explicit constexpr Result(U&& u)
        : Result(ok_tag, std::forward<U>(u)) 
    {}

    template <class U = T, std::enable_if_t<std::is_convertible_v<U&&, T>>* = nullptr,
                           detail::Result_enable_forward_value<T, E, U>* = nullptr>
    constexpr Result(U&& u)
        : Result(ok_tag, std::forward<U>(u)) 
    {}

    // operator=
    template <class U = T, class G = T, 
        std::enable_if_t<std::is_nothrow_constructible_v<T, U&&>>* = nullptr,
        std::enable_if_t<!std::is_void_v<G>>* = nullptr,
        std::enable_if_t<(!std::is_same_v<Result<T, E>, std::decay_t<U>> &&
                          !std::conjunction_v<std::is_scalar<T>, std::is_same<T, std::decay_t<U>>> &&
                          std::is_constructible_v<T, U> &&
                          std::is_assignable_v<G &, U> &&
                          std::is_nothrow_move_constructible_v<E>)>* = nullptr>
    Result& operator=(U &&u) {
        if (is_ok())
            get_val() = std::forward<U>(u);
        else {
            get_err().~E();
            ::new (val_ptr()) T(std::forward<U>(u));
            this->is_ok_ = true;
        }
        return *this;
    }

    template<class U = T, class G = T,
        std::enable_if_t<!std::is_nothrow_constructible_v<T, U&&>>* = nullptr,
        std::enable_if_t<!std::is_void_v<U>>* = nullptr,
        std::enable_if_t<(!std::is_same<Result<T, E>, std::decay_t<U>>::value &&
                          !std::conjunction_v<std::is_scalar<T>, std::is_same<T, std::decay_t<U>>> &&
                          std::is_constructible_v<T, U> &&
                          std::is_assignable_v<G&, U> &&
                          std::is_nothrow_move_constructible_v<E>)>* = nullptr>
    Result& operator=(U &&v) {
        if (is_ok())
            get_val() = std::forward<U>(v);
        else {
            auto tmp = std::move(get_err());
            get_err().~E();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                ::new (val_ptr()) T(std::forward<U>(v));
                this->is_ok_ = true;
            } catch (...) {
                get_err() = std::move(tmp);
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (val_ptr()) T(std::forward<U>(v));
            this->is_ok_ = true;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return *this;
    }

    // emplace
    template<class... Args, std::enable_if_t<std::is_nothrow_constructible_v<T, Args&&...>>* = nullptr>
    T& emplace(Args&&... args) {
        if (is_ok())
            get_val() = T(std::forward<Args>(args)...);
        else {
            get_err().~E();
            ::new (val_ptr()) T(std::forward<Args>(args)...);
            this->is_ok_ = true;
        }
        return get_val();
    }

    template<class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<T, Args&&...>>* = nullptr>
    T& emplace(Args &&... args) {
        if (is_ok())
            get_val() = T(std::forward<Args>(args)...);
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto tmp = std::move(get_err());
#endif // ^^^RUST_EXCEPTIONS_ENABLED^^^
            get_err().~E();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                ::new (val_ptr()) T(std::forward<Args>(args)...);
                this->is_ok_ = true;
            } 
            catch (...) {
                get_err() = std::move(tmp);
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (val_ptr()) T(std::forward<Args>(args)...);
            this->is_ok_ = true;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_val();
    }

    template<class U, class... Args, std::enable_if_t<std::is_nothrow_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    T& emplace(std::initializer_list<U> il, Args&&... args) {
        if (is_ok()) {
            T t(il, std::forward<Args>(args)...);
            get_val() = std::move(t);
        } 
        else {
            get_err().~E();
            ::new (val_ptr()) T(il, std::forward<Args>(args)...);
            this->is_ok_ = true;
        }
        return get_val();
    }

    template<class U, class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    T& emplace(std::initializer_list<U> il, Args &&... args) {
        if (is_ok()) {
            T t(il, std::forward<Args>(args)...);
            get_val() = std::move(t);
        } 
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto tmp = std::move(get_err());
#endif // ^^^RUST_EXCEPTIONS_ENABLED^^^
            get_err().~E();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                ::new (val_ptr()) T(il, std::forward<Args>(args)...);
                this->is_ok_ = true;
            } catch (...) {
                get_err() = std::move(tmp);
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (val_ptr()) T(il, std::forward<Args>(args)...);
            this->is_ok_ = true;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_val();
    }

    // emplace_err
    template<class... Args, std::enable_if_t<std::is_nothrow_constructible_v<E, Args&&...>>* = nullptr>
    E& emplace_err(Args&&... args) {
        if (is_err())
            get_err() = E(std::forward<Args>(args)...);
        else {
            get_val().~T();
            ::new (err_ptr()) E(std::forward<Args>(args)...);
            this->is_ok_ = false;
        }
        return get_err();
    }

    template<class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<E, Args&&...>>* = nullptr>
    E& emplace_err(Args &&... args) {
        if (is_err())
            get_err() = E(std::forward<Args>(args)...);
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto tmp = std::move(get_val());
#endif // ^^^RUST_EXCEPTIONS_ENABLED^^^
            get_val().~T();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                ::new (err_ptr()) E(std::forward<Args>(args)...);
                this->is_ok_ = false;
            } 
            catch (...) {
                get_val() = std::move(tmp);
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (err_ptr()) E(std::forward<Args>(args)...);
            this->is_ok_ = false;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_err();
    }

    template<class U, class... Args, std::enable_if_t<std::is_nothrow_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    E& emplace_err(std::initializer_list<U> il, Args&&... args) {
        if (is_err()) {
            E e(il, std::forward<Args>(args)...);
            get_err() = std::move(e);
        } 
        else {
            get_val().~T();
            ::new (err_ptr()) E(il, std::forward<Args>(args)...);
            this->is_ok_ = false;
        }
        return get_err();
    }

    template<class U, class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    E& emplace(std::initializer_list<U> il, Args &&... args) {
        if (is_err()) {
            E e(il, std::forward<Args>(args)...);
            get_err() = std::move(e);
        } 
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto tmp = std::move(get_val());
#endif // ^^^RUST_EXCEPTIONS_ENABLED^^^
            get_val().~T();
#ifdef RUST_EXCEPTIONS_ENABLED
            try {
                ::new (err_ptr()) E(il, std::forward<Args>(args)...);
                this->is_ok_ = false;
            } catch (...) {
                get_val() = std::move(tmp);
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (err_ptr()) E(il, std::forward<Args>(args)...);
            this->is_ok_ = false;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_err();
    }

    // And
    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E> const& res) const& {
        return is_ok() ? res : Result<U, E>{err_tag, get_err()};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E> const& res) && {
        return is_ok() ? res : Result<U, E>{err_tag, std::move(get_err())};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E>&& res) const& {
        return is_ok() ? std::move(res) : Result<U, E>{err_tag, get_err()};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E>&& res) && {
        return is_ok() ? std::move(res) : Result<U, E>{err_tag, std::move(get_err())};
    }

    // and_then
    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "& overload of rust::Result<T, E>::and_then(Fn) requires Fn to be invocable by a T&");
        using Res = std::invoke_result_t<Fn, T&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "& overload of rust::Result<T, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const& overload of rust::Result<T, E>::and_then(Fn) requires Fn to be invocable by a T const&");
        using Res = std::invoke_result_t<Fn, T const&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const& overload of rust::Result<T, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, T&&>, 
            "&& overload of rust::Result<T, E>::and_then(Fn) requires Fn to be invocable by a T&&");
        using Res = std::invoke_result_t<Fn, T&&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "&& overload of rust::Result<T, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), std::move(get_val()))
                       : Res{err_tag, std::move(get_err())};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, T const&&>, 
            "const&& overload of rust::Result<T, E>::and_then(Fn) requires Fn to be invocable by a T const&&");
        using Res = std::invoke_result_t<Fn, T const&&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const&& overload of rust::Result<T, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), std::move(get_val()))
                       : Res{err_tag, std::move(get_err())};
    }

    // map
    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "& overload of rust::Result<T, E>::map(Fn) requires Fn to be invocable by T&");
        using Res = Result<std::invoke_result_t<Fn, T&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const& overload of rust::Result<T, E>::map(Fn) requires Fn to be invocable by T const&");
        using Res = Result<std::invoke_result_t<Fn, T const&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, T&&>, 
            "&& overload of rust::Result<T, E>::map(Fn) requires Fn to be invocable by T&&");
        using Res = Result<std::invoke_result_t<Fn, T&&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), std::move(get_val()))}
                       : Res{err_tag, std::move(get_err())};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, T const&&>, 
            "const&& overload of rust::Result<T, E>::map(Fn) requires Fn to be invocable by T const&&");
        using Res = Result<std::invoke_result_t<Fn, T const&&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), std::move(get_val()))}
                       : Res{err_tag, std::move(get_err())};
    }

    // map_err
    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, E&>, 
            "& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E&");
        using Res = Result<T, std::invoke_result_t<Fn, E&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, E const&>, 
            "const& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E const&");
        using Res = Result<T, std::invoke_result_t<Fn, E const&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, E&&>, 
            "&& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E&&");
        using Res = Result<T, std::invoke_result_t<Fn, E&&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))}
                        : Res{ok_tag, std::move(get_val())};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, E const&&>, 
            "const&& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E const&&");
        using Res = Result<T, std::invoke_result_t<Fn, E const&&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))}
                        : Res{ok_tag, std::move(get_val())};
    }

    // map_or_else
    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) & {
        static_assert(std::is_invocable_v<FnErr, E&>, 
            "& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&");
        static_assert(std::is_invocable_v<FnOk, T&>, 
            "& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&>, std::invoke_result_t<FnOk, T&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const& {
        static_assert(std::is_invocable_v<FnErr, E const&>, 
            "const& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&");
        static_assert(std::is_invocable_v<FnOk, T const&>, 
            "const& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&>, std::invoke_result_t<FnOk, T const&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) && {
        static_assert(std::is_invocable_v<FnErr, E&&>, 
            "&& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&&");
        static_assert(std::is_invocable_v<FnOk, T&&>, 
            "&& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&&>, std::invoke_result_t<FnOk, T&&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), std::move(get_val())))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), std::move(get_err())));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const&& {
        static_assert(std::is_invocable_v<FnErr, E const&&>, 
            "const&& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&&");
        static_assert(std::is_invocable_v<FnOk, T const&&>, 
            "const&& overload of rust::Result<T, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&&>, std::invoke_result_t<FnOk, T const&&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), std::move(get_val())))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), std::move(get_err())));   
    }

private:
    using t_is_nothrow_move_constructible = std::true_type;
    using move_constructing_t_can_throw = std::false_type;
    using e_is_nothrow_move_constructible = std::true_type;
    using move_constructing_e_can_throw = std::false_type;

    void swap_where_both_have_value(Result& rhs) {
        std::swap(get_val(), rhs.get_val());
    }

    void swap_where_only_one_has_value(Result& rhs) {
        swap_where_only_one_is_ok(
            rhs, std::is_nothrow_move_constructible<T>{},
            std::is_nothrow_move_constructible<E>{});
    }

    void swap_where_only_one_is_ok(
    Result& rhs, t_is_nothrow_move_constructible, e_is_nothrow_move_constructible) noexcept {
        auto temp = std::move(get_val());
        get_val().~T();
        ::new (err_ptr()) err_type(std::move(rhs.get_err()));
        rhs.get_err().~err_type();
        ::new (rhs.val_ptr()) T(std::move(temp));
        std::swap(this->is_ok_, rhs.is_ok_);
    }

    void swap_where_only_one_is_ok(
    Result& rhs, t_is_nothrow_move_constructible, move_constructing_e_can_throw) {
        auto temp = std::move(get_val());
        get_val().~T();
#ifdef RUST_EXCEPTIONS_ENABLED
        try {
            ::new (err_ptr()) err_type(std::move(rhs.get_err()));
            rhs.get_err().~err_type();
            ::new (rhs.val_ptr()) T(std::move(temp));
            std::swap(this->is_ok_, rhs.is_ok_);
        } 
        catch (...) {
            get_val() = std::move(temp);
            throw;
        }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
        ::new (err_ptr()) err_type(std::move(rhs.get_err()));
        rhs.get_err().~err_type();
        ::new (rhs.val_ptr()) T(std::move(temp));
        std::swap(this->is_ok_, rhs.is_ok_);
#endif // RUST_EXCEPTIONS_ENABLED
    }

    void swap_where_only_one_is_ok(
    Result& rhs, move_constructing_t_can_throw, t_is_nothrow_move_constructible) {
        auto temp = std::move(rhs.get_err());
        rhs.get_err().~err_type();
#ifdef RUST_EXCEPTIONS_ENABLED
        try {
            ::new (rhs.val_ptr()) T(get_val());
            get_val().~T();
            ::new (err_ptr()) err_type(std::move(temp));
            std::swap(this->is_ok_, rhs.is_ok_);
        } 
        catch (...) {
            rhs.get_err() = std::move(temp);
            throw;
        }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
        ::new (rhs.val_ptr()) T(get_val());
        get_val().~T();
        ::new (err_ptr()) err_type(std::move(temp));
        std::swap(this->is_ok_, rhs.is_ok_);
#endif // RUST_EXCEPTIONS_ENABLED
    }

public:
    // swap
    template<class T1 = T, class E1 = E>
    std::enable_if_t<detail::is_swappable<T1>::value &&
                     detail::is_swappable<E1>::value &&
                     (std::is_nothrow_move_constructible_v<T1> ||
                      std::is_nothrow_move_constructible_v<E1>)>
    swap(Result& rhs) 
    noexcept(std::is_nothrow_move_constructible_v<T> && detail::is_nothrow_swappable<T>::value &&
             std::is_nothrow_move_constructible_v<E> && detail::is_nothrow_swappable<E>::value) {
        if (is_ok() && rhs.is_ok())
            swap_where_both_have_value(rhs);
        else if (!is_ok() && rhs.is_ok())
            rhs.swap(*this);
        else if (is_ok())
            swap_where_only_one_has_value(rhs);
        else
            std::swap(get_err(), rhs.get_err());
    }

    // is_ok
    [[nodiscard]] constexpr bool is_ok() const noexcept { return this->is_ok_; }

    // is_err
    [[nodiscard]] constexpr bool is_err() const noexcept { return !this->is_ok_; }
    
    // operator bool
    constexpr explicit operator bool() const noexcept { return this->is_ok_; }

    // contains
    template<class U>
    [[nodiscard]] constexpr bool contains(U const& u) const 
    noexcept(noexcept(get_val() == u)) {
        return is_ok() ? (get_val() == u) : false;
    } 

    // contains_err
    template<class G>
    [[nodiscard]] constexpr bool contains_err(G const& g) const 
    noexcept(noexcept(get_err() == g)) {
        return is_err() ? (get_err() == g) : false;
    }

    // ok
    [[nodiscard]] constexpr option::Option<T> ok() const& 
    noexcept(std::is_nothrow_constructible_v<option::Option<T>, T const&>) {
        return is_ok() ? option::Option<T>{get_val()} : option::None;
    }

    [[nodiscard]] constexpr option::Option<T> ok() && 
    noexcept(std::is_nothrow_constructible_v<option::Option<T>, T&&>) {
        return is_ok() ? option::Option<T>{std::move(get_val())} : option::None;
    }

    // err
    [[nodiscard]] constexpr option::Option<E> err() const& 
    noexcept(std::is_nothrow_constructible_v<option::Option<E>, E const&>) {
        return is_err() ? option::Option<E>{get_err()} : option::None;
    }

    [[nodiscard]] constexpr option::Option<E> err() && 
    noexcept(std::is_nothrow_constructible_v<option::Option<E>, E&&>) {
        return is_err() ? option::Option<E>{std::move(get_err())} : option::None;
    }
    
    // expect
    template<class Msg>
    [[nodiscard]] constexpr T& expect(Msg&& msg) & noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const& expect(Msg&& msg) const& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T&& expect(Msg&& msg) && noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const&& expect(Msg&& msg) const&& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic(std::forward<Msg>(msg));
    }

    // expect_err
    template<class Msg>
    [[nodiscard]] constexpr E& expect_err(Msg&& msg) & noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E const& expect_err(Msg&& msg) const& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E&& expect_err(Msg&& msg) && noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(get_err());
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E const&& expect_err(Msg&& msg) const&& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(get_err());
        panic(std::forward<Msg>(msg));
    }

    // unwrap_unsafe
    [[nodiscard]] constexpr T const& unwrap_unsafe() const& {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    [[nodiscard]] constexpr T& unwrap_unsafe() & {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    [[nodiscard]] constexpr T const&& unwrap_unsafe() const&& {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return std::move(get_val());
    }

    [[nodiscard]] constexpr T&& unwrap_unsafe() && {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return std::move(get_val());
    }

    // unwrap_err_unsafe
    [[nodiscard]] constexpr E& unwrap_err_unsafe() & { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return get_err(); 
    }

    [[nodiscard]] constexpr E const& unwrap_err_unsafe() const& {
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG 
        return get_err(); 
    }

    [[nodiscard]] constexpr E&& unwrap_err_unsafe() && { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return std::move(get_err()); 
    }
    
    [[nodiscard]] constexpr E const&& unwrap_err_unsafe() const&& { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return std::move(get_err()); 
    }

    // unwrap
    [[nodiscard]] constexpr T const& unwrap() const& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T, E>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T& unwrap() & noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T, E>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T const&& unwrap() const&& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic("result::Result<T, E>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T&& unwrap() && noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic("result::Result<T, E>::unwrap() panicked");
    }

    // unwrap_err
    [[nodiscard]] constexpr E const& unwrap_err() const& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T, E>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E& unwrap_err() & noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T, E>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E const&& unwrap_err() const&& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(unwrap_err_unsafe());
        panic("result::Result<T, E>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E&& unwrap_err() && noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(unwrap_err_unsafe());
        panic("result::Result<T, E>::unwrap_err() panicked");
    }

    // unwrap_or
    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u) const&
    noexcept(std::is_nothrow_copy_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E>::unwrap_or requires T to be copy constructible");
        static_assert(std::is_convertible_v<U&&, T>,
                      "Result<T, E>::unwrap_or(U) requires U to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::forward<U>(u));
    }

    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u) &&
    noexcept(std::is_nothrow_move_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_move_constructible_v<T>,
                      "The && overload of Result<T, E>::unwrap_or requires T to be move constructible");
        static_assert(std::is_convertible_v<U&&, T>,
                      "Result<T, E>::unwrap_or(U) requires U to be convertible to T");
        return is_ok() ? std::move(get_val()) : static_cast<T>(std::forward<U>(u));
    }

    // unwrap_or_default
    [[nodiscard]] constexpr T unwrap_or_default() const&
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E>::unwrap_or_default requires T to be copy constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Result<T, E>::unwrap_or_default requires T to be default constructible");
        return is_ok() ? get_val() : T{};
    }

    [[nodiscard]] constexpr T unwrap_or_default() &&
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_move_constructible_v<T>,
                      "The && overload of Result<T, E>::unwrap_or_default requires T to be move constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Result<T, E>::unwrap_or_default requires T to be default constructible");
        return is_ok() ? std::move(get_val()) : T{};
    }

    // unwrap_or_else
    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) &
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The & overload of Result<T, E>::unwrap_or_else requires T to be copy constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) const&
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E>::unwrap_or_else requires T to be copy constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) &&
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_move_constructible_v<T>,
                      "The && overload of Result<T, E>::unwrap_or_else requires T to be move constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? std::move(get_val()) : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) const&&
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const&& overload of Result<T, E>::unwrap_or_else requires T to be move constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? std::move(get_val()) : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }
    
    // Or
    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G> const& res) const& {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : res;
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G> const& res) && {
        return is_ok() ? Result<T, G>{ok_tag, std::move(get_val())} : res;
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G>&& res) const& {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : std::move(res);
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G>&& res) && {
        return is_ok() ? Result<T, G>{ok_tag, std::move(get_val())} : std::move(res);
    }

    // or_else
    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) & {
        using Res = Result<T, std::invoke_result_t<Fn, E&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E const&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const& {
        using Res = Result<T, std::invoke_result_t<Fn, E const&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E&&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) && {
        using Res = Result<T, std::invoke_result_t<Fn, E&&>>;
        return is_ok() ? Res{ok_tag, std::move(get_val())} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E const&&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const&& {
        using Res = Result<T, std::invoke_result_t<Fn, E const&&>>;
        return is_ok() ? Res{ok_tag, std::move(get_val())} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))};
    }

    // transpose
    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
    transpose() const&;

    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
    transpose() &&;

    // match
    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) & 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&> || std::is_nothrow_invocable_v<Fns, E&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const& 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&> || std::is_nothrow_invocable_v<Fns, E const&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) && 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&&> || std::is_nothrow_invocable_v<Fns, E&&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_val()));
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_err()));
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const&& 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&&> || std::is_nothrow_invocable_v<Fns, E const&&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_val()));
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_err()));
    }
};

template <class T, class E>
class Result<T&, E> : private detail::Result_move_assign_base<T*, E>,
                      private detail::Result_delete_ctor_base<T*, E>,
                      private detail::Result_delete_assign_base<T*, E>,
                      private detail::Result_default_ctor_base<T*, E> {

    static_assert(!std::is_void_v<T>, "T must not be void");
    static_assert(!std::is_void_v<E>, "E must not be void");
    static_assert(!std::is_same_v<T, std::remove_cv_t<ok_tag_t>>, "T must not be ok_tag_t");
    static_assert(!std::is_same_v<T, std::remove_cv_t<err_tag_t>>, "T must not be err_tag_t");

    // val_ptr
    constexpr T*& val_ptr() { return this->value_; }
    constexpr T const* const& val_ptr() const { return this->value_; }    
    
    // err_ptr
    constexpr E* err_ptr() { return std::addressof(this->err_); }
    constexpr E const* err_ptr() const { return std::addressof(this->err_); }    

    // get_val
    constexpr T& get_val() { return *(this->value_); }
    constexpr T const& get_val() const { return *(this->value_); }
    
    // get_err
    constexpr E& get_err() { return this->err_; }
    constexpr E const& get_err() const { return this->err_; }

    using impl_base = detail::Result_move_assign_base<T*, E>;
    using ctor_base = detail::Result_default_ctor_base<T*, E>;

public:
    using ok_type = T&;
    using err_type = E;

    // constructors 
    constexpr Result() = default;
    constexpr Result(Result const& rhs) = default;
    constexpr Result(Result&& rhs) = default;
    Result& operator=(Result const& rhs) = default;
    Result& operator=(Result&& rhs) = default;

    template<class U>
    constexpr Result(ok_tag_t, U&& u)
        : impl_base(ok_tag, std::addressof(u))
        , ctor_base(detail::default_constructor_tag{}) 
    {
        static_assert(std::is_lvalue_reference_v<U>, "Result<T&, E>::Result(ok_tag_t, U) requires U to be an lvalue");
    }

    template<class... Args, std::enable_if_t<std::is_constructible_v<E, Args&&...>>* = nullptr>
    constexpr explicit Result(err_tag_t, Args&&... args)
        : impl_base(err_tag, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr explicit Result(err_tag_t, std::initializer_list<U> il, Args&&... args)
        : impl_base(err_tag, il, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    // operator=
    template<class U>
    Result& operator=(U&& u) noexcept {
        static_assert(std::is_lvalue_reference_v<U&&>, "Result<T&, E>::operator=(U) require U to be an lvalue");
        if (is_ok())
            this->value_ = std::addressof(u);
        else {
            get_err().~E();
            this->value_ = std::addressof(u);
            this->is_ok_ = true;
        }
        return *this;
    }

    // emplace
    template<class U>
    T& emplace(U&& u) noexcept {
        static_assert(std::is_lvalue_reference_v<U&&>, "Result<T&, E>::emplace(U) require U to be an lvalue");
        if (is_ok())
            this->value_ = std::addressof(u);
        else {
            get_err().~E();
            this->value_ = std::addressof(u);
            this->is_ok_ = true;
        }
        return get_val();
    }

    // emplace_err
    template<class... Args, std::enable_if_t<std::is_nothrow_constructible_v<E, Args&&...>>* = nullptr>
    E& emplace_err(Args&&... args) {
        if (is_err())
            get_err() = E(std::forward<Args>(args)...);
        else {
            ::new (err_ptr()) E(std::forward<Args>(args)...);
            this->is_ok_ = false;
        }
        return get_err();
    }

    template<class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<E, Args&&...>>* = nullptr>
    E& emplace_err(Args &&... args) {
        if (is_err())
            get_err() = E(std::forward<Args>(args)...);
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto const tmp = val_ptr();
            try {
                ::new (err_ptr()) E(std::forward<Args>(args)...);
                this->is_ok_ = false;
            } 
            catch (...) {
                val_ptr() = tmp;
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (err_ptr()) E(std::forward<Args>(args)...);
            this->is_ok_ = false;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_err();
    }

    template<class U, class... Args, std::enable_if_t<std::is_nothrow_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    E& emplace_err(std::initializer_list<U> il, Args&&... args) {
        if (is_err()) {
            E e(il, std::forward<Args>(args)...);
            get_err() = std::move(e);
        } 
        else {
            ::new (err_ptr()) E(il, std::forward<Args>(args)...);
            this->is_ok_ = false;
        }
        return get_err();
    }

    template<class U, class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<E, std::initializer_list<U>&, Args&&...>>* = nullptr>
    E& emplace(std::initializer_list<U> il, Args &&... args) {
        if (is_err()) {
            E e(il, std::forward<Args>(args)...);
            get_err() = std::move(e);
        } 
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto const tmp = val_ptr();
            try {
                ::new (err_ptr()) E(il, std::forward<Args>(args)...);
                this->is_ok_ = false;
            } catch (...) {
                val_ptr() = tmp;
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (err_ptr()) E(il, std::forward<Args>(args)...);
            this->is_ok_ = false;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_err();
    }

    // And
    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E> const& res) const& {
        return is_ok() ? res : Result<U, E>{err_tag, get_err()};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E> const& res) && {
        return is_ok() ? res : Result<U, E>{err_tag, std::move(get_err())};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E>&& res) const& {
        return is_ok() ? std::move(res) : Result<U, E>{err_tag, get_err()};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E>&& res) && {
        return is_ok() ? std::move(res) : Result<U, E>{err_tag, std::move(get_err())};
    }

    // and_then
    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "& overload of rust::Result<T&, E>::and_then(Fn) requires Fn to be invocable by a T&");
        using Res = std::invoke_result_t<Fn, T&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "& overload of rust::Result<T&, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const& overload of rust::Result<T&, E>::and_then(Fn) requires Fn to be invocable by a T const&");
        using Res = std::invoke_result_t<Fn, T const&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const overload of rust::Result<T&, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "&& overload of rust::Result<T&, E>::and_then(Fn) requires Fn to be invocable by a T&");
        using Res = std::invoke_result_t<Fn, T&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "non-const overload of rust::Result<T&, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, std::move(get_err())};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const&& overload of rust::Result<T&, E>::and_then(Fn) requires Fn to be invocable by a T const&");
        using Res = std::invoke_result_t<Fn, T const&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const overload of rust::Result<T&, E>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, std::move(get_err())};
    }

    // map
    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "& overload of rust::Result<T&, E>::map(Fn) requires Fn to be invocable by T&");
        using Res = Result<std::invoke_result_t<Fn, T&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const& overload of rust::Result<T&, E>::map(Fn) requires Fn to be invocable by T const&");
        using Res = Result<std::invoke_result_t<Fn, T const&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "const overload of rust::Result<T&, E>::map(Fn) requires Fn to be invocable by T&");
        using Res = Result<std::invoke_result_t<Fn, T&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, std::move(get_err())};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const overload of rust::Result<T&, E>::map(Fn) requires Fn to be invocable by T const&");
        using Res = Result<std::invoke_result_t<Fn, T const&>, E>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, std::move(get_err())};
    }

    // map_err
    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, E&>, 
            "& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E&");
        using Res = Result<T&, std::invoke_result_t<Fn, E&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, E const&>, 
            "const& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E const&");
        using Res = Result<T&, std::invoke_result_t<Fn, E const&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, E&&>, 
            "&& overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E&&");
        using Res = Result<T&, std::invoke_result_t<Fn, E&&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, E const&&>, 
            "const&& overload of rust::Result<T&, E>::map_err(Fn) requires Fn to be invocable by E const&&");
        using Res = Result<T&, std::invoke_result_t<Fn, E const&&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))}
                        : Res{ok_tag, get_val()};
    }

    // map_or_else
    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) & {
        static_assert(std::is_invocable_v<FnErr, E&>, 
            "& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&");
        static_assert(std::is_invocable_v<FnOk, T&>, 
            "& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&>, std::invoke_result_t<FnOk, T&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const& {
        static_assert(std::is_invocable_v<FnErr, E const&>, 
            "const& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&");
        static_assert(std::is_invocable_v<FnOk, T const&>, 
            "const& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&>, std::invoke_result_t<FnOk, T const&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) && {
        static_assert(std::is_invocable_v<FnErr, E&&>, 
            "&& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&&");
        static_assert(std::is_invocable_v<FnOk, T&>, 
            "&& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&&>, std::invoke_result_t<FnOk, T&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), std::move(get_err())));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const&& {
        static_assert(std::is_invocable_v<FnErr, E const&&>, 
            "const&& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&&");
        static_assert(std::is_invocable_v<FnOk, T const&>, 
            "const&& overload of rust::Result<T&, E>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&&>, std::invoke_result_t<FnOk, T const&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), std::move(get_err())));   
    }

private:
    using e_is_nothrow_move_constructible = std::true_type;
    using move_constructing_e_can_throw = std::false_type;

    void swap_where_both_have_value(Result& rhs) {
        std::swap(this->value_, rhs.value_);
    }

    void swap_where_only_one_has_value(Result& rhs) {
        swap_where_only_one_is_ok(rhs, std::is_nothrow_move_constructible<E>{});
    }

    void swap_where_only_one_is_ok(
    Result& rhs, e_is_nothrow_move_constructible) noexcept {
        auto const temp = val_ptr();
        ::new (err_ptr()) err_type(std::move(rhs.get_err()));
        rhs.get_err().~err_type();
        rhs.val_ptr() = temp;
        std::swap(this->is_ok_, rhs.is_ok_);
    }

    void swap_where_only_one_is_ok(
    Result& rhs, move_constructing_e_can_throw) {
        auto const temp = val_ptr();
#ifdef RUST_EXCEPTIONS_ENABLED
        try {
            ::new (err_ptr()) err_type(std::move(rhs.get_err()));
            rhs.get_err().~err_type();
            rhs.val_ptr() = temp;
            std::swap(this->is_ok_, rhs.is_ok_);
        } 
        catch (...) {
            get_val() = temp;
            throw;
        }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
        ::new (err_ptr()) err_type(std::move(rhs.get_err()));
        rhs.get_err().~err_type();
        rhs.val_ptr() = temp;
        std::swap(this->is_ok_, rhs.is_ok_);
#endif // RUST_EXCEPTIONS_ENABLED
    }

public:
    // swap
    template<class E1 = E>
    std::enable_if_t<detail::is_swappable<E1>::value, void>
    swap(Result& rhs) 
    noexcept(std::is_nothrow_move_constructible_v<E> && detail::is_nothrow_swappable<E>::value) {
        if (is_ok() && rhs.is_ok())
            swap_where_both_have_value(rhs);
        else if (!is_ok() && rhs.is_ok())
            rhs.swap(*this);
        else if (is_ok())
            swap_where_only_one_has_value(rhs);
        else
            std::swap(get_err(), rhs.get_err());
    }

    // is_ok
    [[nodiscard]] constexpr bool is_ok() const noexcept { return this->is_ok_; }

    // is_err
    [[nodiscard]] constexpr bool is_err() const noexcept { return !this->is_ok_; }
    
    // operator bool
    constexpr explicit operator bool() const noexcept { return this->is_ok_; }

    // contains
    template<class U>
    [[nodiscard]] constexpr bool contains(U const& u) const 
    noexcept(noexcept(get_val() == u)) {
        return is_ok() ? (get_val() == u) : false;
    } 

    // contains_err
    template<class G>
    [[nodiscard]] constexpr bool contains_err(G const& g) const 
    noexcept(noexcept(get_err() == g)) {
        return is_err() ? (get_err() == g) : false;
    }

    // ok
    [[nodiscard]] constexpr option::Option<T&> ok() const 
    noexcept(std::is_nothrow_constructible_v<option::Option<T&>, T const&>) {
        return is_ok() ? option::Option<T&>{get_val()} : option::None;
    }

    // err
    [[nodiscard]] constexpr option::Option<E> err() const& 
    noexcept(std::is_nothrow_constructible_v<option::Option<E>, E const&>) {
        return is_err() ? option::Option<E>{get_err()} : option::None;
    }

    [[nodiscard]] constexpr option::Option<E> err() && 
    noexcept(std::is_nothrow_constructible_v<option::Option<E>, E&&>) {
        return is_err() ? option::Option<E>{std::move(get_err())} : option::None;
    }
    
    // expect
    template<class Msg>
    [[nodiscard]] constexpr T& expect(Msg&& msg) noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const& expect(Msg&& msg) const noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    // expect_err
    template<class Msg>
    [[nodiscard]] constexpr E& expect_err(Msg&& msg) & noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E const& expect_err(Msg&& msg) const& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E&& expect_err(Msg&& msg) && noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(get_err());
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E const&& expect_err(Msg&& msg) const&& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(get_err());
        panic(std::forward<Msg>(msg));
    }

    // unwrap_unsafe
    [[nodiscard]] constexpr T& unwrap_unsafe() {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    [[nodiscard]] constexpr T const& unwrap_unsafe() const {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    // unwrap_err_unsafe
    [[nodiscard]] constexpr E& unwrap_err_unsafe() & { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return get_err(); 
    }

    [[nodiscard]] constexpr E const& unwrap_err_unsafe() const& {
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG 
        return get_err(); 
    }

    [[nodiscard]] constexpr E&& unwrap_err_unsafe() && { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return std::move(get_err()); 
    }
    
    [[nodiscard]] constexpr E const&& unwrap_err_unsafe() const&& { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return std::move(get_err()); 
    }

    // unwrap
    [[nodiscard]] constexpr T& unwrap() & noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T&, E>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T const& unwrap() const& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T&, E>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T& unwrap() && noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T&, E>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T const& unwrap() const&& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T&, E>::unwrap() panicked");
    }

    // unwrap_err
    [[nodiscard]] constexpr E const& unwrap_err() const& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T&, E>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E& unwrap_err() & noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T&, E>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E const&& unwrap_err() const&& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(unwrap_err_unsafe());
        panic("result::Result<T&, E>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E&& unwrap_err() && noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return std::move(unwrap_err_unsafe());
        panic("result::Result<T&, E>::unwrap_err() panicked");
    }

    // unwrap_or
    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u) const
    noexcept(std::is_nothrow_copy_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "Result<T&, E>::unwrap_or requires T to be copy constructible");
        static_assert(std::is_convertible_v<U&&, T>,
                      "Result<T&, E>::unwrap_or(U) requires U to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::forward<U>(u));
    }

    // unwrap_or_default
    [[nodiscard]] constexpr T unwrap_or_default() const
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "Result<T&, E>::unwrap_or_default requires T to be copy constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Result<T&, E>::unwrap_or_default requires T to be default constructible");
        return is_ok() ? get_val() : T{};
    }

    // unwrap_or_else
    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) const
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "Result<T&, E>::unwrap_or_else requires T to be copy constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T&, E>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }
    
    // Or
    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G> const& res) const {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : res;
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G>&& res) const {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : std::move(res);
    }

    // or_else
    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) & {
        using Res = Result<T&, std::invoke_result_t<Fn, E&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E const&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const& {
        using Res = Result<T&, std::invoke_result_t<Fn, E const&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E&&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) && {
        using Res = Result<T&, std::invoke_result_t<Fn, E&&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E const&&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const&& {
        using Res = Result<T&, std::invoke_result_t<Fn, E const&&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), std::move(get_err()))};
    }

    // transpose
    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
    transpose() const&;

    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
    transpose() &&;

    // match
    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) & 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&> || std::is_nothrow_invocable_v<Fns, E&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const& 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&> || std::is_nothrow_invocable_v<Fns, E const&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) && 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&> || std::is_nothrow_invocable_v<Fns, E&&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_err()));
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const&& 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&> || std::is_nothrow_invocable_v<Fns, E const&&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_err()));
    }
};


template <class T, class E>
class Result<T, E&> : private detail::Result_move_assign_base<T, E*>,
                      private detail::Result_delete_ctor_base<T, E*>,
                      private detail::Result_delete_assign_base<T, E*>,
                      private detail::Result_default_ctor_base<T, E*> {

    static_assert(!std::is_void_v<T>, "T must not be void");
    static_assert(!std::is_void_v<E>, "E must not be void");
    static_assert(!std::is_same_v<T, std::remove_cv_t<ok_tag_t>>, "T must not be ok_tag_t");
    static_assert(!std::is_same_v<T, std::remove_cv_t<err_tag_t>>, "T must not be err_tag_t");

    // val_ptr
    constexpr T* val_ptr() { return std::addressof(this->value_); }
    constexpr T const* val_ptr() const { return std::addressof(this->value_); }    
    
    // err_ptr
    constexpr E*& err_ptr() { return this->err_; }
    constexpr E const* const& err_ptr() const { return this->err_; }    

    // get_val
    constexpr T& get_val() { return this->value_; }
    constexpr T const& get_val() const { return this->value_; }
    
    // get_err
    constexpr E& get_err() { return *(this->err_); }
    constexpr E const& get_err() const { return *(this->err_); }

    using impl_base = detail::Result_move_assign_base<T, E*>;
    using ctor_base = detail::Result_default_ctor_base<T, E*>;

public:
    using ok_type = T;
    using err_type = E&;

    // constructors 
    constexpr Result() = default;
    constexpr Result(Result const& rhs) = default;
    constexpr Result(Result&& rhs) = default;
    Result& operator=(Result const& rhs) = default;
    Result& operator=(Result&& rhs) = default;

    template <class... Args, std::enable_if_t<std::is_constructible_v<T, Args&&...>>* = nullptr>
    constexpr Result(ok_tag_t, Args&&... args)
        : impl_base(ok_tag, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template<class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr Result(ok_tag_t, std::initializer_list<U> il, Args &&... args)
        : impl_base(ok_tag, il, std::forward<Args>(args)...)
        , ctor_base(detail::default_constructor_tag{}) 
    {}

    template<class U>
    constexpr explicit Result(err_tag_t, U&& u)
        : impl_base(err_tag, std::addressof(u))
        , ctor_base(detail::default_constructor_tag{}) 
    {
        static_assert(std::is_lvalue_reference_v<U>, "Result<T, E&>::Result(err_tag_t, U) requires U to be an lvalue");
    }

    template <class U = T, std::enable_if_t<!std::is_convertible_v<U&&, T>>* = nullptr,
                           detail::Result_enable_forward_value<T, E, U>* = nullptr>
    explicit constexpr Result(U&& u)
        : Result(ok_tag, std::forward<U>(u)) 
    {}

    template <class U = T, std::enable_if_t<std::is_convertible_v<U&&, T>>* = nullptr,
                           detail::Result_enable_forward_value<T, E, U>* = nullptr>
    constexpr Result(U&& u)
        : Result(ok_tag, std::forward<U>(u)) 
    {}

    // operator=
    template <class U = T, class G = T, 
        std::enable_if_t<std::is_nothrow_constructible_v<T, U&&>>* = nullptr,
        std::enable_if_t<(!std::is_same_v<Result<T, E&>, std::decay_t<U>> &&
                          !std::conjunction_v<std::is_scalar<T>, std::is_same<T, std::decay_t<U>>> &&
                          std::is_constructible_v<T, U> &&
                          std::is_assignable_v<G&, U>)>* = nullptr>
    Result& operator=(U&& u) {
        if (is_ok())
            get_val() = std::forward<U>(u);
        else {
            ::new (val_ptr()) T(std::forward<U>(u));
            this->is_ok_ = true;
        }
        return *this;
    }

    template<class U = T, class G = T,
        std::enable_if_t<!std::is_nothrow_constructible_v<T, U&&>>* = nullptr,
        std::enable_if_t<(!std::is_same<Result<T, E&>, std::decay_t<U>>::value &&
                          !std::conjunction_v<std::is_scalar<T>, std::is_same<T, std::decay_t<U>>> &&
                          std::is_constructible_v<T, U> &&
                          std::is_assignable_v<G&, U>)>* = nullptr>
    Result& operator=(U&& v) {
        if (is_ok())
            get_val() = std::forward<U>(v);
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto const tmp = err_ptr();
            try {
                ::new (val_ptr()) T(std::forward<U>(v));
                this->is_ok_ = true;
            } catch (...) {
                err_ptr() = tmp;
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (val_ptr()) T(std::forward<U>(v));
            this->is_ok_ = true;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return *this;
    }

    // emplace
    template<class... Args, std::enable_if_t<std::is_nothrow_constructible_v<T, Args&&...>>* = nullptr>
    T& emplace(Args&&... args) {
        if (is_ok())
            get_val() = T(std::forward<Args>(args)...);
        else {
            ::new (val_ptr()) T(std::forward<Args>(args)...);
            this->is_ok_ = true;
        }
        return get_val();
    }

    template<class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<T, Args&&...>>* = nullptr>
    T& emplace(Args &&... args) {
        if (is_ok())
            get_val() = T(std::forward<Args>(args)...);
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto const tmp = err_ptr();
            try {
                ::new (val_ptr()) T(std::forward<Args>(args)...);
                this->is_ok_ = true;
            } 
            catch (...) {
                err_ptr() = tmp;
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (val_ptr()) T(std::forward<Args>(args)...);
            this->is_ok_ = true;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_val();
    }

    template<class U, class... Args, std::enable_if_t<std::is_nothrow_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    T& emplace(std::initializer_list<U> il, Args&&... args) {
        if (is_ok()) {
            T t(il, std::forward<Args>(args)...);
            get_val() = std::move(t);
        } 
        else {
            ::new (val_ptr()) T(il, std::forward<Args>(args)...);
            this->is_ok_ = true;
        }
        return get_val();
    }

    template<class U, class... Args, std::enable_if_t<!std::is_nothrow_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    T& emplace(std::initializer_list<U> il, Args &&... args) {
        if (is_ok()) {
            T t(il, std::forward<Args>(args)...);
            get_val() = std::move(t);
        } 
        else {
#ifdef RUST_EXCEPTIONS_ENABLED
            auto const tmp = err_ptr();
            try {
                ::new (val_ptr()) T(il, std::forward<Args>(args)...);
                this->is_ok_ = true;
            } catch (...) {
                err_ptr() = tmp;
                throw;
            }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
            ::new (val_ptr()) T(il, std::forward<Args>(args)...);
            this->is_ok_ = true;
#endif // RUST_EXCEPTIONS_ENABLED
        }
        return get_val();
    }

    // emplace_err
    template<class U>
    E& emplace_err(U&& u) noexcept {
        static_assert(std::is_lvalue_reference_v<U&&>, "Result<T&, E>::emplace_err(U) require U to be an lvalue");
        if (is_err())
            this->err_ = std::addressof(u);
        else {
            get_val().~T();
            this->err_ = std::addressof(u);
            this->is_ok_ = false;
        }
        return get_err();
    }

    // And
    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E> const& res) const {
        return is_ok() ? res : Result<U, E>{err_tag, get_err()};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E>&& res) const {
        return is_ok() ? std::move(res) : Result<U, E>{err_tag, get_err()};
    }

    // and_then
    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "& overload of rust::Result<T, E&>::and_then(Fn) requires Fn to be invocable by a T&");
        using Res = std::invoke_result_t<Fn, T&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "& overload of rust::Result<T, E&>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const& overload of rust::Result<T, E&>::and_then(Fn) requires Fn to be invocable by a T const&");
        using Res = std::invoke_result_t<Fn, T const&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const& overload of rust::Result<T, E&>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, T&&>, 
            "&& overload of rust::Result<T, E&>::and_then(Fn) requires Fn to be invocable by a T&&");
        using Res = std::invoke_result_t<Fn, T&&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "&& overload of rust::Result<T, E&>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), std::move(get_val()))
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, T const&&>, 
            "const&& overload of rust::Result<T, E&>::and_then(Fn) requires Fn to be invocable by a T const&&");
        using Res = std::invoke_result_t<Fn, T const&&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const&& overload of rust::Result<T, E&>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), std::move(get_val()))
                       : Res{err_tag, get_err()};
    }

    // map
    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "& overload of rust::Result<T, E&>::map(Fn) requires Fn to be invocable by T&");
        using Res = Result<std::invoke_result_t<Fn, T&>, E&>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const& overload of rust::Result<T, E&>::map(Fn) requires Fn to be invocable by T const&");
        using Res = Result<std::invoke_result_t<Fn, T const&>, E&>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, T&&>, 
            "&& overload of rust::Result<T, E&>::map(Fn) requires Fn to be invocable by T&&");
        using Res = Result<std::invoke_result_t<Fn, T&&>, E&>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), std::move(get_val()))}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, T const&&>, 
            "const&& overload of rust::Result<T, E&>::map(Fn) requires Fn to be invocable by T const&&");
        using Res = Result<std::invoke_result_t<Fn, T const&&>, E&>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), std::move(get_val()))}
                       : Res{err_tag, get_err()};
    }

    // map_err
    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, E&>, 
            "& overload of rust::Result<T, E&>::map_err(Fn) requires Fn to be invocable by E&");
        using Res = Result<T, std::invoke_result_t<Fn, E&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, E const&>, 
            "const& overload of rust::Result<T, E&>::map_err(Fn) requires Fn to be invocable by E const&");
        using Res = Result<T, std::invoke_result_t<Fn, E const&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn, E&>, 
            "&& overload of rust::Result<T, E&>::map_err(Fn) requires Fn to be invocable by E&");
        using Res = Result<T, std::invoke_result_t<Fn, E&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, std::move(get_val())};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn, E const&>, 
            "const&& overload of rust::Result<T, E&>::map_err(Fn) requires Fn to be invocable by E const&");
        using Res = Result<T, std::invoke_result_t<Fn, E const&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, std::move(get_val())};
    }

    // map_or_else
    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) & {
        static_assert(std::is_invocable_v<FnErr, E&>, 
            "& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&");
        static_assert(std::is_invocable_v<FnOk, T&>, 
            "& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&>, std::invoke_result_t<FnOk, T&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const& {
        static_assert(std::is_invocable_v<FnErr, E const&>, 
            "const& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&");
        static_assert(std::is_invocable_v<FnOk, T const&>, 
            "const& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&>, std::invoke_result_t<FnOk, T const&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) && {
        static_assert(std::is_invocable_v<FnErr, E&>, 
            "&& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&");
        static_assert(std::is_invocable_v<FnOk, T&&>, 
            "&& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&>, std::invoke_result_t<FnOk, T&&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), std::move(get_val())))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const&& {
        static_assert(std::is_invocable_v<FnErr, E const&>, 
            "const&& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&");
        static_assert(std::is_invocable_v<FnOk, T const&&>, 
            "const&& overload of rust::Result<T, E&>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&>, std::invoke_result_t<FnOk, T const&&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), std::move(get_val())))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

private:
    using t_is_nothrow_move_constructible = std::true_type;
    using move_constructing_t_can_throw = std::false_type;

    void swap_where_both_have_value(Result& rhs) {
        std::swap(get_val(), rhs.get_val());
    }

    void swap_where_only_one_has_value(Result& rhs) {
        swap_where_only_one_is_ok(
            rhs, std::is_nothrow_move_constructible<T>{});
    }

    void swap_where_only_one_is_ok(
    Result& rhs, t_is_nothrow_move_constructible) noexcept {
        auto temp = std::move(get_val());
        get_val().~T();
        err_ptr() = rhs.err_ptr();
        ::new (rhs.val_ptr()) T(std::move(temp));
        std::swap(this->is_ok_, rhs.is_ok_);
    }

    void swap_where_only_one_is_ok(
    Result& rhs, move_constructing_t_can_throw) {
        auto temp = rhs.err_ptr();
#ifdef RUST_EXCEPTIONS_ENABLED
        try {
            ::new (rhs.val_ptr()) T(get_val());
            get_val().~T();
            err_ptr() = temp;
            std::swap(this->is_ok_, rhs.is_ok_);
        } 
        catch (...) {
            rhs.get_err() = temp;
            throw;
        }
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
        ::new (rhs.val_ptr()) T(get_val());
        get_val().~T();
        err_ptr() = temp;
        std::swap(this->is_ok_, rhs.is_ok_);
#endif // RUST_EXCEPTIONS_ENABLED
    }

public:
    // swap
    template<class T1 = T>
    std::enable_if_t<detail::is_swappable<T1>::value, void>
    swap(Result& rhs) 
    noexcept(std::is_nothrow_move_constructible_v<T> && detail::is_nothrow_swappable<T>::value) {
        if (is_ok() && rhs.is_ok())
            swap_where_both_have_value(rhs);
        else if (!is_ok() && rhs.is_ok())
            rhs.swap(*this);
        else if (is_ok())
            swap_where_only_one_has_value(rhs);
        else
            std::swap(err_ptr(), rhs.err_ptr());
    }

    // is_ok
    [[nodiscard]] constexpr bool is_ok() const noexcept { return this->is_ok_; }

    // is_err
    [[nodiscard]] constexpr bool is_err() const noexcept { return !this->is_ok_; }
    
    // operator bool
    constexpr explicit operator bool() const noexcept { return this->is_ok_; }

    // contains
    template<class U>
    [[nodiscard]] constexpr bool contains(U const& u) const 
    noexcept(noexcept(get_val() == u)) {
        return is_ok() ? (get_val() == u) : false;
    } 

    // contains_err
    template<class G>
    [[nodiscard]] constexpr bool contains_err(G const& g) const 
    noexcept(noexcept(get_err() == g)) {
        return is_err() ? (get_err() == g) : false;
    }

    // ok
    [[nodiscard]] constexpr option::Option<T> ok() const& 
    noexcept(std::is_nothrow_constructible_v<option::Option<T>, T const&>) {
        return is_ok() ? option::Option<T>{get_val()} : option::None;
    }

    [[nodiscard]] constexpr option::Option<T> ok() && 
    noexcept(std::is_nothrow_constructible_v<option::Option<T>, T&&>) {
        return is_ok() ? option::Option<T>{std::move(get_val())} : option::None;
    }

    // err
    [[nodiscard]] constexpr option::Option<E&> err() const 
    noexcept(std::is_nothrow_constructible_v<option::Option<E&>, E const&>) {
        return is_err() ? option::Option<E&>{get_err()} : option::None;
    }
    
    // expect
    template<class Msg>
    [[nodiscard]] constexpr T& expect(Msg&& msg) & noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const& expect(Msg&& msg) const& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T&& expect(Msg&& msg) && noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const&& expect(Msg&& msg) const&& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic(std::forward<Msg>(msg));
    }

    // expect_err
    template<class Msg>
    [[nodiscard]] constexpr E& expect_err(Msg&& msg) noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E const& expect_err(Msg&& msg) const noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    // unwrap_unsafe
    [[nodiscard]] constexpr T const& unwrap_unsafe() const& {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    [[nodiscard]] constexpr T& unwrap_unsafe() & {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    [[nodiscard]] constexpr T const&& unwrap_unsafe() const&& {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return std::move(get_val());
    }

    [[nodiscard]] constexpr T&& unwrap_unsafe() && {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return std::move(get_val());
    }

    // unwrap_err_unsafe
    [[nodiscard]] constexpr E& unwrap_err_unsafe() { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return get_err(); 
    }

    [[nodiscard]] constexpr E const& unwrap_err_unsafe() const {
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG 
        return get_err(); 
    }

    // unwrap
    [[nodiscard]] constexpr T& unwrap() & noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T, E&>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T const& unwrap() const& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T, E&>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T&& unwrap() && noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic("result::Result<T, E&>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T const&& unwrap() const&& noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return std::move(get_val());
        panic("result::Result<T, E&>::unwrap() panicked");
    }

    // unwrap_err
    [[nodiscard]] constexpr E& unwrap_err() & noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T, E&>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E const& unwrap_err() const& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T, E&>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E& unwrap_err() && noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T, E&>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E const& unwrap_err() const&& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return unwrap_err_unsafe();
        panic("result::Result<T, E&>::unwrap_err() panicked");
    }

    // unwrap_or
    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u) const&
    noexcept(std::is_nothrow_copy_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E&>::unwrap_or requires T to be copy constructible");
        static_assert(std::is_convertible_v<U&&, T>,
                      "Result<T, E&>::unwrap_or(U) requires U to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::forward<U>(u));
    }

    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u) &&
    noexcept(std::is_nothrow_move_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_move_constructible_v<T>,
                      "The && overload of Result<T, E&>::unwrap_or requires T to be move constructible");
        static_assert(std::is_convertible_v<U&&, T>,
                      "Result<T, E&>::unwrap_or(U) requires U to be convertible to T");
        return is_ok() ? std::move(get_val()) : static_cast<T>(std::forward<U>(u));
    }

    // unwrap_or_default
    [[nodiscard]] constexpr T unwrap_or_default() const&
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E&>::unwrap_or_default requires T to be copy constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Result<T, E&>::unwrap_or_default requires T to be default constructible");
        return is_ok() ? get_val() : T{};
    }

    [[nodiscard]] constexpr T unwrap_or_default() &&
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_move_constructible_v<T>,
                      "The && overload of Result<T, E&>::unwrap_or_default requires T to be move constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Result<T, E&>::unwrap_or_default requires T to be default constructible");
        return is_ok() ? std::move(get_val()) : T{};
    }

    // unwrap_or_else
    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) &
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The & overload of Result<T, E&>::unwrap_or_else requires T to be copy constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E&>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) const&
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E&>::unwrap_or_else requires T to be copy constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E&>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) &&
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_move_constructible_v<T>,
                      "The && overload of Result<T, E&>::unwrap_or_else requires T to be move constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E&>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? std::move(get_val()) : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn) const&&
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const&& overload of Result<T, E&>::unwrap_or_else requires T to be move constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E&>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? std::move(get_val()) : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }
    
    // Or
    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G> const& res) const& {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : res;
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G> const& res) && {
        return is_ok() ? Result<T, G>{ok_tag, std::move(get_val())} : res;
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G>&& res) const& {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : std::move(res);
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G>&& res) && {
        return is_ok() ? Result<T, G>{ok_tag, std::move(get_val())} : std::move(res);
    }

    // or_else
    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) & {
        using Res = Result<T, std::invoke_result_t<Fn, E&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E const&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const& {
        using Res = Result<T, std::invoke_result_t<Fn, E const&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) && {
        using Res = Result<T, std::invoke_result_t<Fn, E&>>;
        return is_ok() ? Res{ok_tag, std::move(get_val())} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn, std::enable_if_t<rust::detail::is_result_v<std::invoke_result_t<Fn, E const&>>>* = nullptr>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const&& {
        using Res = Result<T, std::invoke_result_t<Fn, E const&>>;
        return is_ok() ? Res{ok_tag, std::move(get_val())} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    // transpose
    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E&>>> 
    transpose() const&;

    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E&>>> 
    transpose() &&;

    // match
    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) & 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&> || std::is_nothrow_invocable_v<Fns, E&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const& 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&> || std::is_nothrow_invocable_v<Fns, E const&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) && 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&&> || std::is_nothrow_invocable_v<Fns, E&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_val()));
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const&& 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&&> || std::is_nothrow_invocable_v<Fns, E const&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(get_val()));
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }
};

template<class T, class E>
class Result<T&, E&> {

    union {
        T* value_;
        E* error_;
    };
    bool is_ok_;

    static_assert(!std::is_same_v<T, std::remove_cv_t<ok_tag_t>>, "T must not be ok_tag_t");
    static_assert(!std::is_same_v<T, std::remove_cv_t<err_tag_t>>, "T must not be err_tag_t");

    constexpr T& get_val() { return *value_; }
    constexpr T const& get_val() const { return *value_; }

    constexpr E& get_err() { return *error_; }
    constexpr E const& get_err() const { return *error_; }

public:
    using ok_type = T&;
    using err_type = E&;

    // constructors 
    constexpr Result() = default;
    constexpr Result(Result const& rhs) = default;
    constexpr Result(Result&& rhs) = default;
    Result& operator=(Result const& rhs) = default;
    Result& operator=(Result&& rhs) = default;

    template<class U>
    constexpr explicit Result(ok_tag_t, U&& u)
        : value_{std::addressof(u)}
        , is_ok_{true}
    {
        static_assert(std::is_lvalue_reference_v<U>, "Result<T&, E&>::Result(ok_tag_t, U) requires U to be an lvalue");
    }

    template<class U>
    constexpr explicit Result(err_tag_t, U&& u)
        : error_{std::addressof(u)}
        , is_ok_{false}
    {
        static_assert(std::is_lvalue_reference_v<U>, "Result<T&, E&>::Result(err_tag_t, U) requires U to be an lvalue");
    }

    // operator=
    template<class U>
    Result& operator=(U&& u) {
        static_assert(std::is_lvalue_reference_v<U>, "Result<T&, E&>::operatr=(U) requires U to be an lvalue");
        value_ = std::addressof(u);
        is_ok_ = true;
        return *this;
    }

    // emplace
    template<class U>
    T& emplace(U&& u) noexcept {
        static_assert(std::is_lvalue_reference_v<U&&>, "Result<T&, E&>::emplace(U) require U to be an lvalue");
        value_ = std::addressof(u);
        is_ok_ = true;
        return get_val();
    }

    // emplace_err
    template<class U>
    E& emplace_err(U&& u) noexcept {
        static_assert(std::is_lvalue_reference_v<U&&>, "Result<T&, E&>::emplace_err(U) require U to be an lvalue");
        error_ = std::addressof(u);
        is_ok_ = false;
        return get_err();
    }

    // And
    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E> const& res) const {
        return is_ok() ? res : Result<U, E>{err_tag, get_err()};
    }

    template<class U>
    [[nodiscard]] constexpr Result<U, E> And(Result<U, E>&& res) const {
        return is_ok() ? std::move(res) : Result<U, E>{err_tag, get_err()};
    }

    // and_then
    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "non-const overload of rust::Result<T&, E&>::and_then(Fn) requires Fn to be invocable by a T&");
        using Res = std::invoke_result_t<Fn, T&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "non-const overload of rust::Result<T&, E&>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const overload of rust::Result<T&, E&>::and_then(Fn) requires Fn to be invocable by a T const&");
        using Res = std::invoke_result_t<Fn, T const&>;
        static_assert(rust::detail::is_result_v<Res>, 
            "const overload of rust::Result<T&, E&>::and_then(Fn) requires Fn's return type to be a Result<U, E>");
        return is_ok() ? std::invoke(std::forward<Fn>(fn), get_val())
                       : Res{err_tag, get_err()};
    }

    // map
    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) {
        static_assert(std::is_invocable_v<Fn, T&>, 
            "non-const overload of rust::Result<T&, E&>::map(Fn) requires Fn to be invocable by T&");
        using Res = Result<std::invoke_result_t<Fn, T&>, E&>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map(Fn&& fn) const {
        static_assert(std::is_invocable_v<Fn, T const&>, 
            "const overload of rust::Result<T&, E&>::map(Fn) requires Fn to be invocable by T const&");
        using Res = Result<std::invoke_result_t<Fn, T const&>, E&>;
        return is_ok() ? Res{ok_tag, std::invoke(std::forward<Fn>(fn), get_val())}
                       : Res{err_tag, get_err()};
    }

    // map_err
    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) & {
        static_assert(std::is_invocable_v<Fn, E&>, 
            "non-const overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E&");
        using Res = Result<T&, std::invoke_result_t<Fn, E&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    template<class Fn> 
    [[nodiscard]] constexpr auto map_err(Fn&& fn) const& {
        static_assert(std::is_invocable_v<Fn, E const&>, 
            "const overload of rust::Result<T, E>::map_err(Fn) requires Fn to be invocable by E const&");
        using Res = Result<T&, std::invoke_result_t<Fn, E const&>>;
        return is_err() ? Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())}
                        : Res{ok_tag, get_val()};
    }

    // map_or_else
    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) {
        static_assert(std::is_invocable_v<FnErr, E&>, 
            "non-const overload of rust::Result<T&, E&>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E&");
        static_assert(std::is_invocable_v<FnOk, T&>, 
            "non-const overload of rust::Result<T&, E&>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E&>, std::invoke_result_t<FnOk, T&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    template<class FnErr, class FnOk>
    [[nodiscard]] constexpr auto map_or_else(FnErr&& fnerr, FnOk&& fnok) const {
        static_assert(std::is_invocable_v<FnErr, E const&>, 
            "const overload of rust::Result<T&, E&>::map_or_else(FnErr, FnOk) requires FnErr to be invocable by E const&");
        static_assert(std::is_invocable_v<FnOk, T const&>, 
            "const overload of rust::Result<T&, E&>::map_or_else(FnErr, FnOk) requires FnOk to be invocable by T const&");
        using R = std::common_type_t<std::invoke_result_t<FnErr, E const&>, std::invoke_result_t<FnOk, T const&>>;
        return is_ok() ? static_cast<R>(std::invoke(std::forward<FnOk>(fnok), get_val()))
                       : static_cast<R>(std::invoke(std::forward<FnErr>(fnerr), get_err()));   
    }

    // swap
    constexpr void swap(Result& rhs) noexcept {
        if (is_ok() && rhs.is_ok())
            std::swap(value_, rhs.value_);
        else if (!is_ok() && rhs.is_ok()) {
            std::swap(value_, rhs.value_); // UB?
            is_ok_ = std::exchange(rhs.is_ok_, false);
        }
        else if (is_ok()) {
            std::swap(value_, rhs.value_); // UB?
            is_ok_ = std::exchange(rhs.is_ok_, true);
        }
        else
            std::swap(error_, rhs.error_);
    }

    // is_ok
    [[nodiscard]] constexpr bool is_ok() const noexcept { return this->is_ok_; }

    // is_err
    [[nodiscard]] constexpr bool is_err() const noexcept { return !this->is_ok_; }
    
    // operator bool
    constexpr explicit operator bool() const noexcept { return this->is_ok_; }

    // contains
    template<class U>
    [[nodiscard]] constexpr bool contains(U const& u) const 
    noexcept(noexcept(get_val() == u)) {
        return is_ok() ? (get_val() == u) : false;
    } 

    // contains_err
    template<class G>
    [[nodiscard]] constexpr bool contains_err(G const& g) const 
    noexcept(noexcept(get_err() == g)) {
        return is_err() ? (get_err() == g) : false;
    }

    // ok
    [[nodiscard]] constexpr option::Option<T&> ok() const 
    noexcept(std::is_nothrow_constructible_v<option::Option<T&>, T const&>) {
        return is_ok() ? option::Option<T&>{get_val()} : option::None;
    }

    // err
    [[nodiscard]] constexpr option::Option<E&> err() const 
    noexcept(std::is_nothrow_constructible_v<option::Option<E>, E const&>) {
        return is_err() ? option::Option<E&>{get_err()} : option::None;
    }
    
    // expect
    template<class Msg>
    [[nodiscard]] constexpr T& expect(Msg&& msg) noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const& expect(Msg&& msg) const noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic(std::forward<Msg>(msg));
    }

    // expect_err
    template<class Msg>
    [[nodiscard]] constexpr E& expect_err(Msg&& msg) & noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr E const& expect_err(Msg&& msg) const& noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic(std::forward<Msg>(msg));
    }

    // unwrap_unsafe
    [[nodiscard]] constexpr T& unwrap_unsafe() {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    [[nodiscard]] constexpr T const& unwrap_unsafe() const {
#ifdef RUST_DEBUG
        if (is_err())
            panic("rust::result::Result::unwrap_unsafe result has a err");
#endif // RUST_DEBUG
        return get_val();
    }

    // unwrap_err_unsafe
    [[nodiscard]] constexpr E& unwrap_err_unsafe() { 
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG
        return get_err(); 
    }

    [[nodiscard]] constexpr E const& unwrap_err_unsafe() const {
#ifdef RUST_DEBUG
        if (is_ok())
            panic("rust::result::Result::unwrap_err_unsafe result has a value");
#endif // RUST_DEBUG 
        return get_err(); 
    }

    // unwrap
    [[nodiscard]] constexpr T& unwrap() noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T&, E&>::unwrap() panicked");
    }

    [[nodiscard]] constexpr T const& unwrap() const noexcept(false) {
        if (is_ok()) RUST_ATTR_LIKELY
            return get_val();
        panic("result::Result<T&, E&>::unwrap() panicked");
    }

    // unwrap_err
    [[nodiscard]] constexpr E& unwrap_err() noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic("result::Result<T&, E&>::unwrap_err() panicked");
    }

    [[nodiscard]] constexpr E const& unwrap_err() const noexcept(false) {
        if (is_err()) RUST_ATTR_LIKELY
            return get_err();
        panic("result::Result<T&, E&>::unwrap_err() panicked");
    }

    // unwrap_or
    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u) const
    noexcept(std::is_nothrow_copy_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E>::unwrap_or requires T to be copy constructible");
        static_assert(std::is_convertible_v<U&&, T>,
                      "Result<T, E>::unwrap_or(U) requires U to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::forward<U>(u));
    }

    // unwrap_or_default
    [[nodiscard]] constexpr T unwrap_or_default() const
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The const& overload of Result<T, E>::unwrap_or_default requires T to be copy constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Result<T, E>::unwrap_or_default requires T to be default constructible");
        return is_ok() ? get_val() : T{};
    }

    // unwrap_or_else
    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn)
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_copy_constructible_v<T>,
                      "The & overload of Result<T, E>::unwrap_or_else requires T to be copy constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Result<T, E>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return is_ok() ? get_val() : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }
    
    // Or
    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G> const& res) const {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : res;
    }

    template<class G>
    [[nodiscard]] constexpr Result<T, G> Or(Result<T, G>&& res) const {
        return is_ok() ? Result<T, G>{ok_tag, get_val()} : std::move(res);
    }

    // or_else
    template<class Fn>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) {
        static_assert(rust::detail::is_result_v<std::invoke_result_t<Fn, E&>>, 
            "rust::result::Result::or_else(Fn) requires Fn take an E& and return a Result");
        using Res = Result<T&, std::invoke_result_t<Fn, E&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    template<class Fn>
    [[nodiscard]] constexpr auto or_else(Fn&& fn) const {
        static_assert(rust::detail::is_result_v<std::invoke_result_t<Fn, E const&>>, 
            "rust::result::Result::or_else(Fn) requires Fn take an E const& and return a Result");
        using Res = Result<T&, std::invoke_result_t<Fn, E const&>>;
        return is_ok() ? Res{ok_tag, get_val()} 
                       : Res{err_tag, std::invoke(std::forward<Fn>(fn), get_err())};
    }

    // transpose
    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E&>>> 
    transpose() const;

    // match
    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&> || std::is_nothrow_invocable_v<Fns, E&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const 
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&> || std::is_nothrow_invocable_v<Fns, E const&>>...>) {
        if (is_ok())
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_val());
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, get_err());
    }
};

// Ok()
template<class T, class E, class... Args>
[[nodiscard]] inline constexpr auto Ok(Args&&... args) {
    return Result<T, E>{ok_tag, std::forward<Args>(args)...};
}

// Err()
template<class T, class E, class... Args>
[[nodiscard]] inline constexpr auto Err(Args&&... args) {
    return Result<T, E>{err_tag, std::forward<Args>(args)...};
}

} // namespace result
 
// ----------------------------------------------------------------------------------------- 
// Option

namespace option {

struct some_tag_t{
    some_tag_t() = default;
};

static constexpr some_tag_t some_tag{};

namespace detail {

template <class T, class U>
using enable_forward_value_t =
std::enable_if_t<std::is_constructible_v<T, U&&> &&
    !std::is_same_v<std::decay_t<U>, some_tag_t> &&
    !std::is_same_v<Option<T>, std::decay_t<U>>>;

template <class T, class U, class Other>
using enable_from_other_t = std::enable_if_t<
    std::is_constructible_v<T, Other> &&
    !std::is_constructible_v<T, Option<U>&> &&
    !std::is_constructible_v<T, Option<U>&&> &&
    !std::is_constructible_v<T, Option<U> const&> &&
    !std::is_constructible_v<T, Option<U> const&&> &&
    !std::is_convertible_v<Option<U>&, T> &&
    !std::is_convertible_v<Option<U>&&, T> &&
    !std::is_convertible_v<Option<U> const&, T> &&
    !std::is_convertible_v<Option<U> const&&, T>>;

template <class T, class U>
using enable_assign_forward_t = std::enable_if_t<
    !std::is_same<Option<T>, std::decay_t<U>>::value &&
    !std::conjunction<std::is_scalar<T>,
    std::is_same<T, std::decay_t<U>>>::value &&
    std::is_constructible<T, U>::value && std::is_assignable<T&, U>::value>;

template <class T, class U, class Other>
using enable_assign_from_other_t = std::enable_if_t<
    std::is_constructible_v<T, Other> &&
    std::is_assignable_v<T&, Other> &&
    !std::is_constructible_v<T, Option<U>&> &&
    !std::is_constructible_v<T, Option<U>&&> &&
    !std::is_constructible_v<T, Option<U> const&> &&
    !std::is_constructible_v<T, Option<U> const&&> &&
    !std::is_convertible_v<Option<U>&, T> &&
    !std::is_convertible_v<Option<U>&&, T> &&
    !std::is_convertible_v<Option<U> const&, T> &&
    !std::is_convertible_v<Option<U> const&&, T> &&
    !std::is_assignable_v<T&, Option<U>&> &&
    !std::is_assignable_v<T&, Option<U>&&> &&
    !std::is_assignable_v<T&, Option<U> const&> &&
    !std::is_assignable_v<T&, Option<U> const&&>>;

// The storage base manages the actual storage, and correctly propagates
// trivial destruction from T. This case is for when T is not trivially
// destructible.
template <class T, bool = std::is_trivially_destructible_v<T>>
struct Option_storage_base {
    constexpr Option_storage_base() noexcept
        : dummy_{}
        , is_some_{false}
    {}

    template <class... U>
    constexpr Option_storage_base(some_tag_t, U&&... u)
        : value_(std::forward<U>(u)...)
        , is_some_{true} 
    {}

    ~Option_storage_base() {
        if (is_some_) {
            value_.~T();
            is_some_ = false;
        }
    }

    struct dummy {};
    union {
        dummy dummy_;
        T value_;
    };

    bool is_some_;
};

// This case is for when T is trivially destructible.
template <class T> 
struct Option_storage_base<T, true> {
    constexpr Option_storage_base() noexcept
        : dummy_()
        , is_some_{false} 
    {}

    template <class... U>
    constexpr Option_storage_base(some_tag_t, U&&... u)
        : value_(std::forward<U>(u)...)
        , is_some_{true} 
    {}

    // No destructor, so this class is trivially destructible

    struct dummy {};
    union {
        dummy dummy_;
        T value_;
    };

    bool is_some_ = false;
};

// This base class provides some handy member functions which can be used in
// further derived classes
template <class T> 
struct Option_operations_base : Option_storage_base<T> {
    using Option_storage_base<T>::Option_storage_base;

    void hard_reset() noexcept {
        get().~T();
        this->is_some_ = false;
    }

    template <class... Args> 
    void construct(Args&&... args) noexcept {
        new (std::addressof(this->value_)) T(std::forward<Args>(args)...);
        this->is_some_ = true;
    }

    template <class Opt> 
    void assign(Opt&& rhs) {
        if (is_some()) {
            if (rhs)
                this->value_ = std::forward<Opt>(rhs).get();
            else {
                this->value_.~T();
                this->is_some_ = false;
            }
        }
        else if (rhs)
            construct(std::forward<Opt>(rhs).get());
    }

    bool is_some() const noexcept { return this->is_some_; }

    constexpr T& get() & { return this->value_; }
    constexpr T const& get() const& { return this->value_; }
    constexpr T&& get() && { return std::move(this->value_); }
    constexpr T const&& get() const&& { return std::move(this->value_); }
};

// This class manages conditionally having a trivial copy constructor
// This specialization is for when T is trivially copy constructible
template <class T, bool = std::is_trivially_copy_constructible_v<T>>
struct Option_copy_base : Option_operations_base<T> {
    using Option_operations_base<T>::Option_operations_base;
};

// This specialization is for when T is not trivially copy constructible
template <class T>
struct Option_copy_base<T, false> : Option_operations_base<T> {
    using Option_operations_base<T>::Option_operations_base;

    Option_copy_base() = default;
    Option_copy_base(Option_copy_base const& rhs) {
        if (rhs)
            this->construct(rhs.get());
        else
            this->is_some_ = false;
    }

    Option_copy_base(Option_copy_base&& rhs) = default;
    Option_copy_base& operator=(Option_copy_base const& rhs) = default;
    Option_copy_base& operator=(Option_copy_base&& rhs) = default;
};

// This class manages conditionally having a trivial move constructor
template <class T, bool = std::is_trivially_move_constructible_v<T>>
struct Option_move_base : Option_copy_base<T> {
    using Option_copy_base<T>::Option_copy_base;
};

template <class T> 
struct Option_move_base<T, false> : Option_copy_base<T> {
    using Option_copy_base<T>::Option_copy_base;

    Option_move_base() = default;
    Option_move_base(Option_move_base const& rhs) = default;

    Option_move_base(Option_move_base&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        if (rhs)
            this->construct(std::move(rhs.get()));
        else
            this->is_some_ = false;
    }
    Option_move_base& operator=(Option_move_base const& rhs) = default;
    Option_move_base& operator=(Option_move_base&& rhs) = default;
};

// This class manages conditionally having a trivial copy assignment operator
template <class T, bool = std::is_trivially_copy_assignable_v<T>&&
    std::is_trivially_copy_constructible_v<T>&&
    std::is_trivially_destructible_v<T>>
struct Option_copy_assign_base : Option_move_base<T> {
    using Option_move_base<T>::Option_move_base;
};

template <class T>
struct Option_copy_assign_base<T, false> : Option_move_base<T> {
    using Option_move_base<T>::Option_move_base;

    Option_copy_assign_base() = default;
    Option_copy_assign_base(Option_copy_assign_base const& rhs) = default;

    Option_copy_assign_base(Option_copy_assign_base&& rhs) = default;
    Option_copy_assign_base& operator=(Option_copy_assign_base const& rhs) {
        this->assign(rhs);
        return *this;
    }
    Option_copy_assign_base& operator=(Option_copy_assign_base&& rhs) = default;
};

// This class manages conditionally having a trivial move assignment operator
template <class T, bool = std::is_trivially_destructible_v<T>
        && std::is_trivially_move_constructible_v<T>
        && std::is_trivially_move_assignable_v<T>>
struct Option_move_assign_base : Option_copy_assign_base<T> {
        using Option_copy_assign_base<T>::Option_copy_assign_base;
};

template <class T>
struct Option_move_assign_base<T, false> : Option_copy_assign_base<T> {
    using Option_copy_assign_base<T>::Option_copy_assign_base;

    Option_move_assign_base() = default;
    Option_move_assign_base(Option_move_assign_base const& rhs) = default;

    Option_move_assign_base(Option_move_assign_base&& rhs) = default;

    Option_move_assign_base& operator=(Option_move_assign_base const& rhs) = default;

    Option_move_assign_base& operator=(Option_move_assign_base&& rhs) 
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
        this->assign(std::move(rhs));
        return *this;
    }
};

// Option_delete_ctor_base will conditionally delete copy and move
// constructors depending on whether T is copy/move constructible
template <class T, bool EnableCopy = std::is_copy_constructible_v<T>,
    bool EnableMove = std::is_move_constructible_v<T>>
struct Option_delete_ctor_base {
    Option_delete_ctor_base() = default;
    Option_delete_ctor_base(Option_delete_ctor_base const&) = default;
    Option_delete_ctor_base(Option_delete_ctor_base&&) noexcept = default;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base const&) = default;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base&&) noexcept = default;
};

template <class T> 
struct Option_delete_ctor_base<T, true, false> {
    Option_delete_ctor_base() = default;
    Option_delete_ctor_base(Option_delete_ctor_base const&) = default;
    Option_delete_ctor_base(Option_delete_ctor_base&&) noexcept = delete;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base const&) = default;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base&&) noexcept = default;
};

template <class T> 
struct Option_delete_ctor_base<T, false, true> {
    Option_delete_ctor_base() = default;
    Option_delete_ctor_base(Option_delete_ctor_base const&) = delete;
    Option_delete_ctor_base(Option_delete_ctor_base&&) noexcept = default;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base const&) = default;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base&&) noexcept = default;
};

template <class T> 
struct Option_delete_ctor_base<T, false, false> {
    Option_delete_ctor_base() = default;
    Option_delete_ctor_base(Option_delete_ctor_base const&) = delete;
    Option_delete_ctor_base(Option_delete_ctor_base&&) noexcept = delete;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base const&) = default;
    Option_delete_ctor_base& operator=(Option_delete_ctor_base&&) noexcept = default;
};

// Option_delete_assign_base will conditionally delete copy and move
// constructors depending on whether T is copy/move constructible + assignable
template <class T,
    bool EnableCopy = (std::is_copy_constructible_v<T>&&
                        std::is_copy_assignable_v<T>),
    bool EnableMove = (std::is_move_constructible_v<T>&&
                        std::is_move_assignable_v<T>)>
struct Option_delete_assign_base {
    Option_delete_assign_base() = default;
    Option_delete_assign_base(Option_delete_assign_base const&) = default;
    Option_delete_assign_base(Option_delete_assign_base&&) noexcept = default;
    Option_delete_assign_base& operator=(Option_delete_assign_base const&) = default;
    Option_delete_assign_base& operator=(Option_delete_assign_base&&) noexcept = default;
};

template <class T> 
struct Option_delete_assign_base<T, true, false> {
    Option_delete_assign_base() = default;
    Option_delete_assign_base(Option_delete_assign_base const&) = default;
    Option_delete_assign_base(Option_delete_assign_base&&) noexcept = default;
    Option_delete_assign_base& operator=(Option_delete_assign_base const&) = default;
    Option_delete_assign_base& operator=(Option_delete_assign_base&&) noexcept = delete;
};

template <class T> 
struct Option_delete_assign_base<T, false, true> {
    Option_delete_assign_base() = default;
    Option_delete_assign_base(Option_delete_assign_base const&) = default;
    Option_delete_assign_base(Option_delete_assign_base&&) noexcept = default;
    Option_delete_assign_base& operator=(Option_delete_assign_base const&) = delete;
    Option_delete_assign_base& operator=(Option_delete_assign_base&&) noexcept = default;
};

template <class T> 
struct Option_delete_assign_base<T, false, false> {
    Option_delete_assign_base() = default;
    Option_delete_assign_base(Option_delete_assign_base const&) = default;
    Option_delete_assign_base(Option_delete_assign_base&&) noexcept = default;
    Option_delete_assign_base& operator=(Option_delete_assign_base const&) = delete;
    Option_delete_assign_base& operator=(Option_delete_assign_base&&) noexcept = delete;
};

template<class T>
using disable_if_Option_ref = std::enable_if_t<!std::disjunction_v<std::is_lvalue_reference<T>, rust::detail::is_non_null<T>>>;

template<class T>
using enable_if_Option_ref = std::enable_if_t<std::disjunction_v<std::is_lvalue_reference<T>, rust::detail::is_non_null<T>>>;

} // namespace detail

template<class T>
class Option : private detail::Option_move_assign_base<T>,
               private detail::Option_delete_ctor_base<T>,
               private detail::Option_delete_assign_base<T> {
    using base = detail::Option_move_assign_base<T>;
public:
    using value_type = T;
private:

    static_assert(!std::is_same_v<rust::detail::remove_cvref_t<value_type>, some_tag_t>,
                  "instantiation of Option with some_tag_t is ill-formed");
    static_assert(!std::is_same_v<rust::detail::remove_cvref_t<value_type>, None_t>,
                  "instantiation of Option with None_t is ill-formed");
    static_assert(!std::is_reference_v<value_type>,
                  "instantiation of Option with a reference type is ill-formed");
    static_assert(std::is_destructible_v<value_type>,
                  "instantiation of Option with a non-destructible type is ill-formed");
    static_assert(!std::is_array_v<value_type>,
                  "instantiation of Option with an array type is ill-formed");

    // operator->
    constexpr T const* operator->() const { return std::addressof(this->value_); }
    constexpr T* operator->() { return std::addressof(this->value_); }

    // operator*
    constexpr T& operator*() & { return this->value_; }
    constexpr T const& operator*() const& { return this->value_; }
    constexpr T&& operator*() && { return std::move(this->value_); }
    constexpr T const&& operator*() const&& { return std::move(this->value_); }

public:
    // constructors
    constexpr Option() noexcept = default;

    constexpr Option(None_t) noexcept {}

    constexpr Option(Option const& rhs) = default;
    constexpr Option(Option&& rhs) = default;

    template <class... Args, std::enable_if_t<std::is_constructible_v<T, Args...>>* = nullptr>
    constexpr explicit Option(some_tag_t, Args&&... args)
        : base(some_tag, std::forward<Args>(args)...) 
    {}

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>>* = nullptr>
    constexpr explicit Option(some_tag_t, std::initializer_list<U> il, Args&&... args) {
        this->construct(il, std::forward<Args>(args)...);
    }

    template <
        class U = T,
        std::enable_if_t<std::is_convertible_v<U&&, T>>* = nullptr,
        detail::enable_forward_value_t<T, U> * = nullptr>
    constexpr Option(U&& u) 
        : base(some_tag, std::forward<U>(u)) 
    {}

    template <
        class U = T,
        std::enable_if_t<!std::is_convertible_v<U&&, T>>* = nullptr,
        detail::enable_forward_value_t<T, U> * = nullptr>
    constexpr explicit Option(U&& u) 
        : base(some_tag, std::forward<U>(u)) 
    {}

    template <
        class U, detail::enable_from_other_t<T, U, U const&>* = nullptr,
        std::enable_if_t<std::is_convertible_v<U const&, T>>* = nullptr>
    Option(Option<U> const& rhs) {
        if (rhs)
            this->construct(*rhs);
    }

    template <class U, detail::enable_from_other_t<T, U, U const&>* = nullptr,
        std::enable_if_t<!std::is_convertible_v<U const&, T>>* = nullptr>
    explicit Option(Option<U> const& rhs) {
        if (rhs)
            this->construct(*rhs);
    }

    template <
        class U, detail::enable_from_other_t<T, U, U&&>* = nullptr,
        std::enable_if_t<std::is_convertible_v<U&&, T>>* = nullptr>
    Option(Option<U>&& rhs) {
        if (rhs)
            this->construct(std::move(*rhs));
    }

    template <
        class U, detail::enable_from_other_t<T, U, U&&>* = nullptr,
        std::enable_if_t<!std::is_convertible_v<U&&, T>>* = nullptr>
    explicit Option(Option<U>&& rhs) {
        if (rhs)
            this->construct(std::move(*rhs));
    }

    // assignment operators
    Option& operator=(None_t) noexcept {
        if (*this) {
            this->value_.~T();
            this->is_some_ = false;
        }
        return *this;
    }

    constexpr Option& operator=(Option const& rhs) = default;
    constexpr Option& operator=(Option&& rhs) = default;

    template <class U = T, detail::enable_assign_forward_t<T, U>* = nullptr>
    Option& operator=(U&& u) {
        if (*this)
            this->value_ = std::forward<U>(u);
        else
            this->construct(std::forward<U>(u));
        return *this;
    }

    template <class U, detail::enable_assign_from_other_t<T, U, U const&>* = nullptr>
    Option& operator=(Option<U> const& rhs) 
    noexcept(std::is_nothrow_copy_assignable_v<T> && std::is_nothrow_assignable_v<T, U const&>) {
        if (*this) {
            if (rhs)
                this->value_ = *rhs;
            else
                this->hard_reset();
        }
        if (rhs)
            this->construct(*rhs);
        return *this;
    }

    template <class U, detail::enable_assign_from_other_t<T, U, U> * = nullptr>
    Option& operator=(Option<U>&& rhs) 
    noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_assignable_v<T, U&&>) {
        if (*this) {
            if (rhs)
                this->value_ = std::move(*rhs);
            else
                this->hard_reset();
        }
        if (rhs)
            this->construct(std::move(*rhs));
        return *this;
    }

    // emplace
    template<class... Args>
    constexpr T& emplace(Args&&... args) {
        if (*this)
            this->value_.~T();
        this->construct(std::forward<Args>(args)...);
        return **this;
    }

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>, int> = 0>
    constexpr T& emplace(std::initializer_list<U> il, Args&&... args) {
        if (*this)
            this->value_.~T();
        this->construct(il, std::forward<Args>(args)...);
        return **this;
    }

    // replace
    template <class... Args> 
    [[nodiscard]] constexpr Option replace(Args&&... args) {
        auto tmp = std::move(*this);
        if (*this)
            this->value_.~T();
        this->construct(std::forward<Args>(args)...);
        return tmp;
    }

    template <class U, class... Args, std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U>&, Args&&...>, int> = 0>
    [[nodiscard]] constexpr Option replace(std::initializer_list<U> il, Args&&... args) {
        auto tmp = std::move(*this);
        if (*this)
            this->value_.~T();
        this->construct(il, std::forward<Args>(args)...);
        return tmp;
    }

    // is_none
    [[nodiscard]] constexpr bool is_none() const noexcept {
        return !*this;
    }

    // is_some
    [[nodiscard]] constexpr bool is_some() const noexcept {
        return bool(*this);
    }

    // contains
    template<class U>
    [[nodiscard]] constexpr bool contains(U const& u) const noexcept {
        return bool(*this) ? (**this == u) : false;
    }

    // operator bool
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return this->is_some_;
    }

    // expect
    template<class Msg>
    [[nodiscard]] constexpr T&& expect(Msg&& msg) && noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return std::move(**this);
        panic(std::forward<Msg>(msg));
    }

    // expect_none
    template<class Msg>
    constexpr void expect_none(Msg&& msg) const {
        if (!*this) RUST_ATTR_LIKELY
            return;
        panic(std::forward<Msg>(msg));
    }
    
    // unwrap_unsafe
    [[nodiscard]] constexpr T&& unwrap_unsafe() && { 
#ifdef RUST_DEBUG
        if (!*this)
            panic("rust::option::Option::unwrap_unsafe option has no value");
#endif // RUST_DEBUG
        return std::move(**this); 
    }

    // unwrap
    [[nodiscard]] constexpr T&& unwrap() && noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return std::move(**this);
        panic("rust::Option::unwrap panicked");
    }

    // unwrap_none
    constexpr void unwrap_none() const&& noexcept(false) {
        if (!*this) RUST_ATTR_LIKELY
            return;
        panic("rust::Option::unwrap_none panicked");
    }

    // unwrap_or
    template<class U>
    [[nodiscard]] constexpr T unwrap_or(U&& u)
    && noexcept(std::is_nothrow_move_constructible_v<T> && rust::detail::is_nothrow_convertible_v<U, T>) {
        static_assert(std::is_move_constructible_v<T>,
                      "Option<T>::unwrap_or requires T to be move constructible");
        static_assert(std::is_convertible_v<U, T>,
                      "Option<T>::unwrap_or(U) requires U to be convertible to T");
        return bool(*this) ? std::move(**this) : static_cast<T>(std::forward<U>(u));
    }

    // unwrap_or_default
    [[nodiscard]] constexpr T unwrap_or_default()
    && noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_default_constructible_v<T>) {
        static_assert(std::is_move_constructible_v<T>,
                      "Option<T>::unwrap_or_default requires T to be move constructible");
        static_assert(std::is_default_constructible_v<T>,
                      "Option<T>::unwrap_or_default requires T to be default constructible");
        return bool(*this) ? std::move(**this) : T{};
    }

    // unwrap_or_else
    template<class Fn>
    [[nodiscard]] constexpr T unwrap_or_else(Fn&& fn)
    && noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_invocable_r_v<T, Fn>) {
        static_assert(std::is_move_constructible_v<T>,
                      "Option<T>::unwrap_or_else requires T to be move constructible");
        static_assert(std::is_invocable_r_v<T, Fn>,
                      "Option<T>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T");
        return bool(*this) ? std::move(**this) : static_cast<T>(std::invoke(std::forward<Fn>(fn)));
    }

    // filter
    template<class Fn>
    [[nodiscard]] constexpr Option<T> filter(Fn&& fn)
    && noexcept(noexcept(std::is_nothrow_invocable_v<Fn, T const&>)) {
        static_assert(std::is_invocable_r_v<bool, Fn, T const&>,
                      "Option<T>::filter(Fn) requies Fn to be invocable with T const& and return a bool");
        return bool(*this) ? (std::invoke(std::forward<Fn>(fn), **this) ? std::move(*this) : None) : None;
    }

    // flatten
    template<class U = T, std::enable_if_t<rust::detail::is_option_v<U>>* = nullptr>
    [[nodiscard]] constexpr Option<typename U::value_type> flatten() && noexcept(std::is_move_constructible_v<U>) {
        return bool(*this) ? std::move(**this) : None;
    }

    // transpose
    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_result_v<U>, result::Result<Option<typename U::ok_type>, typename U::err_type>> 
    transpose() &&;

    // get_or_insert
    template<class... Args>
    [[nodiscard]] constexpr T& get_or_insert(Args&&... args) & {
        static_assert(std::is_constructible_v<T, Args...>,
                      "Option<T>::get_or_insert(Args...) requires T to be constructible by Args...");
        if (!*this)
            static_cast<void>(emplace(std::forward<Args>(args)...));
        return **this;
    }

    template<class... Args>
    [[nodiscard]] constexpr T const& get_or_insert(Args&&... args) const& {
        static_assert(std::is_constructible_v<T, Args...>,
                      "Option<T>::get_or_insert(Args...) requires T to be constructible by Args...");
        if (!*this)
            static_cast<void>(emplace(std::forward<Args>(args)...));
        return **this;
    }

    template<class... Args>
    [[nodiscard]] constexpr T&& get_or_insert(Args&&... args) && {
        static_assert(std::is_constructible_v<T, Args...>,
                      "Option<T>::get_or_insert(Args...) requires T to be constructible by Args...");
        if (!*this)
            static_cast<void>(emplace(std::forward<Args>(args)...));
        return std::move(**this);
    }

    template<class... Args>
    [[nodiscard]] constexpr T const&& get_or_insert(Args&&... args) const&& {
        static_assert(std::is_constructible_v<T, Args...>,
                      "Option<T>::get_or_insert(Args...) requires T to be constructible by Args...");
        if (!*this)
            static_cast<void>(emplace(std::forward<Args>(args)...));
        return std::move(**this);
    }

    // get_or_insert_with
    template<class Fn>
    [[nodiscard]] constexpr T& get_or_insert_with(Fn&& fn)
    & noexcept(noexcept(std::invoke(std::forward<Fn>(fn)))) {
        static_assert(std::is_invocable_v<Fn>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn to be invocable with no arguments");
        static_assert(std::is_convertible_v<std::invoke_result_t<Fn>, T>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn's return type to be convertible to T");
        if (!*this)
            static_cast<void>(emplace(std::invoke(std::forward<Fn>(fn))));
        return **this;
    }

    template<class Fn>
    [[nodiscard]] constexpr T const& get_or_insert_with(Fn&& fn)
    const& noexcept(noexcept(std::invoke(std::forward<Fn>(fn)))) {
        static_assert(std::is_invocable_v<Fn>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn to be invocable with no arguments");
        static_assert(std::is_convertible_v<std::invoke_result_t<Fn>, T>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn's return type to be convertible to T");
        if (!*this)
            static_cast<void>(emplace(std::invoke(std::forward<Fn>(fn))));
        return **this;
    }

    template<class Fn>
    [[nodiscard]] constexpr T&& get_or_insert_with(Fn&& fn)
    && noexcept(noexcept(std::invoke(std::forward<Fn>(fn)))) {
        static_assert(std::is_invocable_v<Fn>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn to be invocable with no arguments");
        static_assert(std::is_convertible_v<std::invoke_result_t<Fn>, T>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn's return type to be convertible to T");
        if (!*this)
            static_cast<void>(emplace(std::invoke(std::forward<Fn>(fn))));
        return std::move(**this);
    }

    template<class Fn>
    [[nodiscard]] constexpr T const&& get_or_insert_with(Fn&& fn)
    const&& noexcept(noexcept(std::invoke(std::forward<Fn>(fn)))) {
        static_assert(std::is_invocable_v<Fn>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn to be invocable with no arguments");
        static_assert(std::is_convertible_v<std::invoke_result_t<Fn>, T>,
                      "Option<T>::get_or_insert_with(Fn) requires Fn's return type to be convertible to T");
        if (!*this)
            static_cast<void>(emplace(std::invoke(std::forward<Fn>(fn))));
        return std::move(**this);
    }

    // take
    [[nodiscard]] Option<T> take() {
        Option opt = std::move(*this);
        reset();
        return opt;
    }

    // swap
    void swap(Option& rhs)
    noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T>) {
        if (*this) {
            if (rhs)
                std::swap(**this, *rhs);
            else {
                new (std::addressof(rhs.value_)) T(std::move(this->value_));
                this->value_.T::~T();
            }
        }
        else if (rhs) {
            new (std::addressof(this->value_)) T(std::move(rhs.value_));
            rhs.value_.T::~T();
        }
        std::swap(this->is_some_, rhs.is_some_);
    }

    // reset
    void reset() noexcept {
        if (*this) {
            this->value_.~T();
            this->is_some_ = false;
        }
    }

    // And
    template<class U>
    [[nodiscard]] constexpr Option<U> And(Option<U>&& opt) const {
        return bool(*this) ? std::move(opt) : None;
    }

    template<class U>
    [[nodiscard]] constexpr Option<U> And(Option<U> const& opt) const {
        return bool(*this) ? opt : None;
    }

    // and_then
    template <class F> 
    [[nodiscard]] constexpr auto and_then(F&& f) && {
        using Result = std::invoke_result_t<F, T&&>;
        static_assert(rust::detail::is_option_v<Result>,
                      "Option<T>::and_then(Fn) requires Fn's return type to be an Option");

        return bool(*this) ? std::invoke(std::forward<F>(f), std::move(**this))
                           : Result{None};
    }

    // Or
    [[nodiscard]] constexpr Option<T> Or(Option<T> const& opt) && {
        return bool(*this) ? std::move(*this) : opt;
    }

    [[nodiscard]] constexpr Option<T> Or(Option<T>&& opt) && {
        return bool(*this) ? std::move(*this) : std::move(opt);
    }

    // or_else
    template <class Fn, rust::detail::disable_if_ret_void<Fn>* = nullptr>
    [[nodiscard]] constexpr Option<T> or_else(Fn&& fn) && {
        return bool(*this) ? std::move(*this) : std::invoke(std::forward<Fn>(fn));
    }

    template <class Fn, rust::detail::enable_if_ret_void<Fn>* = nullptr>
    [[nodiscard]] constexpr Option<T> or_else(Fn&& fn) && {
        if (*this)
            return std::move(*this);
        std::invoke(std::forward<Fn>(fn));
        return None;
    }

    // Xor
    [[nodiscard]] constexpr Option<T> Xor(Option<T> const& opt) && {
        if ((*this && opt) || (!*this && !opt))
            return None;
        else
            return bool(*this) ? std::move(*this) : opt;
    }

    [[nodiscard]] constexpr Option<T> Xor(Option<T>&& opt) && {
        if ((*this && opt) || (!*this && !opt))
            return None;
        else
            return bool(*this) ? std::move(*this) : std::move(opt);
    }

    // map
    template<class Fn, class U = std::invoke_result_t<Fn, T&&>>
    [[nodiscard]] constexpr Option<U> map(Fn&& fn)
    && noexcept(noexcept(std::invoke(std::forward<Fn>(fn), std::move(**this)))) {
        static_assert(std::is_invocable_r_v<U, Fn, T&&>,
                      "Option<T>::map(Fn) -> Option<U> requires Fn's return type to be convertible to an U");
        return bool(*this) ? Option<U>{static_cast<U>((std::invoke(std::forward<Fn>(fn), std::move(**this))))}
                           : Option<U>{None};
    }

    // map_or
    template<class Fn, class U, class R = std::invoke_result_t<Fn, T&&>>
    [[nodiscard]] constexpr R map_or(Fn&& fn, U&& u)
    && noexcept(noexcept(std::invoke(std::forward<Fn>(fn), std::move(**this))) && noexcept(static_cast<R>(u))) {
        static_assert(std::is_invocable_r_v<R, Fn, T&&>,
                      "Option<T>::map_or(Fn, U) requires U to be convertible to Fn's return type and Fn's argument to be T&&");
        return bool(*this) ? static_cast<R>(std::invoke(std::forward<Fn>(fn), std::move(**this)))
                           : static_cast<R>(std::forward<U>(u));
    }

    // map_or_else
    template<class Fn, class DFn, class U = std::common_type_t<std::invoke_result_t<Fn, T&&>, std::invoke_result_t<DFn>>>
    [[nodiscard]] constexpr U map_or_else(Fn&& fn, DFn&& dfn)
    && noexcept(std::is_nothrow_invocable_r_v<U, Fn, T&&> && std::is_nothrow_invocable_r_v<U, DFn>) {
        return bool(*this) ? static_cast<U>(std::invoke(std::forward<Fn>(fn), std::move(**this)))
                           : static_cast<U>(std::invoke(std::forward<DFn>(dfn)));
    }

    // ok_or
    template<class E>
    [[nodiscard]] constexpr auto ok_or(E&& e) && {
        return bool(*this) ? result::Result<T, E>{result::ok_tag, std::move(**this)}
                           : result::Result<T, E>(result::err_tag, std::forward<E>(e));
    } 

    // ok_or_else
    template<class Fn>
    [[nodiscard]] constexpr auto ok_or_else(Fn&& fn) && {
        static_assert(std::is_invocable_v<Fn>, 
            "rust::Option<T>::ok_or_else(Fn) requires Fn to be invocable with no arguments");
        using E = std::invoke_result_t<Fn>;
        return bool(*this) ? result::Result<T, E>{result::ok_tag, std::move(**this)}
                           : result::Result<T, E>{result::err_tag, std::forward<Fn>(fn)()};
    }

    // match
    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) 
    && noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&&> || std::is_nothrow_invocable_v<Fns, None_t>>...>) {
        if (*this)
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, std::move(**this));
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, None);
    }

    // as_deref
    template<class U = T, std::enable_if_t<rust::detail::can_be_dereferenced_v<U>>* = nullptr>
    [[nodiscard]] constexpr auto as_deref() const {
        using Opt = Option<std::add_lvalue_reference_t<std::add_const_t<decltype(*std::declval<U>())>>>;
        return bool(*this) ? Opt(***this) : None;
    }
    
    // as_deref_mut
    template<class U = T, std::enable_if_t<rust::detail::can_be_dereferenced_v<U>>* = nullptr>
    [[nodiscard]] constexpr auto as_deref_mut() {
        using Opt = Option<std::add_lvalue_reference_t<decltype(*std::declval<U>())>>;
        return bool(*this) ? Opt(***this) : None;
    }
    
    // as_ref
    [[nodiscard]] constexpr Option<T const&> as_ref() const {
        return bool(*this) ? Option<T const&>(**this) : None;
    }

    // as_mut
    [[nodiscard]] constexpr Option<T&> as_mut() {
        return bool(*this) ? Option<T&>(**this) : None;
    }

    // as_pin_ref
    
    // as_pin_mut
};

template<class T>
class Option<T&> {

    constexpr T& operator*() { return *value_; }
    constexpr T const&  operator*() const { return *value_; }

public:
    using value_type = T&;

    // constructors
    constexpr explicit Option() noexcept
        : value_{nullptr}
    {}

    constexpr Option(Option const&) noexcept = default;
    constexpr Option(Option&&) noexcept = default;

    constexpr Option(None_t) noexcept
        : value_{nullptr}
    {}

    template<class U, std::enable_if_t<!rust::detail::is_option_v<std::decay_t<U>>>* = nullptr>
    constexpr explicit Option(some_tag_t, U&& u) noexcept
        : value_{std::addressof(u)}
    {
        static_assert(std::is_lvalue_reference_v<U>, "rust::Option<T&>::Option(U) requires U to be an lvalue");
    }

    ~Option() = default;

    // assignment operators
    constexpr Option& operator=(None_t) noexcept {
        value_ = nullptr;
        return *this;
    }

    Option& operator=(Option const&) = default;

    template<class U = T, std::enable_if_t<!rust::detail::is_option_v<U>>* = nullptr>
    constexpr Option& operator=(U&& u) {
        static_assert(std::is_lvalue_reference_v<U>, "rust::Option<T&>::operator=(U) requires U to be an lvalue");
        value_ = static_cast<T*>(std::addressof(u));
        return *this;
    }

    // emplace
    template<class U = T>
    constexpr T& emplace(U&& u) noexcept {
        *this = std::forward<U>(u);
        return **this;
    }

    // replace
    template<class U = T>
    [[nodiscard]] constexpr Option replace(U&& u) noexcept {
        auto tmp = std::move(*this);
        *this = std::forward<U>(u);
        return tmp;
    }

    // is_none
    [[nodiscard]] constexpr bool is_none() const noexcept { return value_ == nullptr; }

    // is_some
    [[nodiscard]] constexpr bool is_some() const noexcept { return value_ != nullptr; }

    // operator bool
    [[nodiscard]] constexpr operator bool() const noexcept { return value_ != nullptr; }
    
    // contains
    template<class U>
    [[nodiscard]] constexpr bool contains(U const& u) {
        return bool(*this) ? (u == **this) : false;
    }

    // expect
    template<class Msg>
    [[nodiscard]] constexpr T& expect(Msg&& msg) && noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return **this;
        panic(std::forward<Msg>(msg));
    }

    template<class Msg>
    [[nodiscard]] constexpr T const& expect(Msg&& msg) const&& noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return **this;
        panic(std::forward<Msg>(msg));
    }

    // expect_none
    template<class Msg>
    constexpr void expect_none(Msg&& msg) const&& noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return;
        panic(std::forward<Msg>(msg));
    }

    // unwrap_unsafe
    [[nodiscard]] constexpr T& unwrap_unsafe() && noexcept { 
#ifdef RUST_DEBUG
        if (!*this)
            panic("rust::option::Option::unwrap_unsafe option has no value");
#endif // RUST_DEBUG
        return **this; 
    } 
    
    [[nodiscard]] constexpr T const& unwrap_unsafe() const&& noexcept { 
#ifdef RUST_DEBUG
        if (!*this)
            panic("rust::option::Option::unwrap_unsafe option has no value");
#endif // RUST_DEBUG
        return **this; 
    } 

    // unwrap
    [[nodiscard]] constexpr T& unwrap() && noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return **this;
        panic("option::Option<T&>::unwrap panicked");
    }

    [[nodiscard]] constexpr T const& unwrap() const&& noexcept(false) {
        if (*this) RUST_ATTR_LIKELY
            return **this;
        panic("option::Option<T&>::unwrap panicked");
    }

    // unwrap_none
    constexpr void unwrap_none() const&& noexcept(false) {
        if (!*this) RUST_ATTR_LIKELY
            return;
        panic("rust::Option::unwrap_none panicked");
    }

    // unwrap_or
    template<class U>
    [[nodiscard]] constexpr T& unwrap_or(T& t) const&& noexcept {
        return bool(*this) ? **this : t;
    }

    template<class U>
    [[nodiscard]] constexpr T const& unwrap_or(T const& t) const&& noexcept {
        return bool(*this) ? **this : t;
    }

    // unwrap_or_default -> cannot return T& otherwise dangling reference to default

    // unwrap_or_else
    template<class Fn>
    [[nodiscard]] constexpr T& unwrap_or_else(Fn&& fn) &&
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T&, Fn>) {
        static_assert(std::is_invocable_r_v<T&, Fn>,
                      "Option<T&>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T&");
        return bool(*this) ? **this : static_cast<T&>(std::invoke(std::forward<Fn>(fn)));
    }

    template<class Fn>
    [[nodiscard]] constexpr T const& unwrap_or_else(Fn&& fn) const&&
    noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_invocable_r_v<T const&, Fn>) {
        static_assert(std::is_invocable_r_v<T const&, Fn>,
                      "Option<T&>::unwrap_or_else(Fn) requires Fn's return type to be convertible to T const&");
        return bool(*this) ? **this : static_cast<T const&>(std::invoke(std::forward<Fn>(fn)));
    }

    // filter
    template<class Fn>
    [[nodiscard]] constexpr Option<T&> filter(Fn&& fn) && 
    noexcept(noexcept(std::is_nothrow_invocable_v<Fn, T const&>)) {
        static_assert(std::is_invocable_r_v<bool, Fn, T const&>,
                      "Option<T&>::filter(Fn) requies Fn to return a bool");
        return bool(*this) ? (std::invoke(std::forward<Fn>(fn), **this) ? std::move(*this) : None) : None;
    }

    // flatten
    template<class U = T, std::enable_if_t<rust::detail::is_option_v<U>>* = nullptr>
    [[nodiscard]] constexpr Option<typename U::value_type> flatten() && noexcept(std::is_copy_constructible_v<U>) {
        return bool(*this) ? std::move(**this) : None; 
    }

    // transpose
    template<class U = T>
    [[nodiscard]] constexpr std::enable_if_t<rust::detail::is_result_v<U>, result::Result<Option<typename U::ok_type>, typename U::err_type>> 
    transpose() &&;

    // get_or_insert
    template<class U>
    [[nodiscard]] constexpr T& get_or_insert(U&& u) && {
        if (!*this)
            static_cast<void>(emplace(std::forward<U>(u)));
        return **this;
    }

    template<class U>
    [[nodiscard]] constexpr T const& get_or_insert(U&& u) const&& {
        if (!*this)
            static_cast<void>(emplace(std::forward<U>(u)));
        return **this;
    }

    // get_or_insert_with
    template<class Fn>
    [[nodiscard]] constexpr T& get_or_insert_with(Fn&& fn) &&
    noexcept(noexcept(std::invoke(std::forward<Fn>(fn)))) {
        static_assert(std::is_invocable_v<Fn>,
                      "Option<T&>::get_or_insert_with(Fn) requires Fn to be invocable with no arguments");
        static_assert(std::is_invocable_r_v<T&, Fn>,
                      "Option<T&>::get_or_insert_with(Fn) requires Fn's return type to be convertible to T&");
        if (!*this)
            static_cast<void>(emplace(std::invoke(std::forward<Fn>(fn))));
        return **this;
    }

    template<class Fn>
    [[nodiscard]] constexpr T const& get_or_insert_with(Fn&& fn) const&&
    noexcept(noexcept(std::invoke(std::forward<Fn>(fn)))) {
        static_assert(std::is_invocable_v<Fn>,
                      "Option<T&>::get_or_insert_with(Fn) requires Fn to be invocable with no arguments");
        static_assert(std::is_invocable_r_v<T const&, Fn>,
                      "Option<T&>::get_or_insert_with(Fn) requires Fn's return type to be convertible to T const&");
        if (!*this)
            static_cast<void>(emplace(std::invoke(std::forward<Fn>(fn))));
        return **this;
    }

    // take
    [[nodiscard]] constexpr Option<T&> take() noexcept {
        auto opt = std::move(*this);
        reset();
        return opt;
    }

    // swap
    void swap(Option& rhs) noexcept {
        std::swap(value_, rhs.value_);
    }

    // reset
    constexpr void reset() noexcept {
        value_ = nullptr;
    }

    // And
    template<class U>
    [[nodiscard]] constexpr Option<U> And(Option<U>&& opt) {
        return bool(*this) ? std::move(opt) : None;
    }

    template<class U>
    [[nodiscard]] constexpr Option<U> And(Option<U> const& opt) {
        return bool(*this) ? opt : None;
    }

    // and_then
    template <class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) && {
        using Result = std::invoke_result_t<Fn, T&>;
        static_assert(rust::detail::is_option_v<Result>,
                      "Option<T>::and_then(Fn) requires Fn's return type to be an Option");

        return bool(*this) ? std::invoke(std::forward<Fn>(fn), **this)
                           : Result{None};
    }

    template <class Fn> 
    [[nodiscard]] constexpr auto and_then(Fn&& fn) const&& {
        using Result = std::invoke_result_t<Fn, T const&>;
        static_assert(rust::detail::is_option_v<Result>,
                      "Option<T>::and_then(Fn) requires Fn's return type to be an Option");

        return bool(*this) ? std::invoke(std::forward<Fn>(fn), **this)
                           : Result{None};
    }

    // Or
    [[nodiscard]] constexpr Option Or(Option const& opt) && {
        return bool(*this) ? std::move(*this) : opt;
    }

    [[nodiscard]] constexpr Option Or(Option&& opt) && {
        return bool(*this) ? std::move(*this) : std::move(opt);
    }

    // or_else
    template <class Fn, rust::detail::enable_if_ret_void<Fn>* = nullptr>
    [[nodiscard]] constexpr Option<T&> or_else(Fn&& fn) && {
        if (*this)
            return std::move(*this);
        std::invoke(std::forward<Fn>(fn));
        return None;
    }

    template <class Fn, rust::detail::disable_if_ret_void<Fn>* = nullptr>
    [[nodiscard]] constexpr Option<T&> or_else(Fn&& fn) && {
        return bool(*this) ? std::move(*this) : std::invoke(std::forward<Fn>(fn));
    }

    // Xor
    [[nodiscard]] constexpr Option Xor(Option const& opt) const&& {
        if ((*this && opt) || (!*this && !opt))
            return None;
        else
            return bool(*this) ? std::move(*this) : opt;
    }

    [[nodiscard]] constexpr Option Xor(Option&& opt) const&& {
        if ((*this && opt) || (!*this && !opt))
            return None;
        else
            return bool(*this) ? std::move(*this) : std::move(opt);
    }

    // map
    template<class Fn, class U = std::invoke_result_t<Fn, T&>>
    [[nodiscard]] constexpr Option<U> map(Fn&& fn) &&
    noexcept(noexcept(std::invoke(std::forward<Fn>(fn), **this))) {
        static_assert(std::is_invocable_r_v<U, Fn, T&>,
                      "Option<T&>::map<U>(Fn) requires Fn's return type to be convertible to a U");
        return bool(*this) ? Option<U>{static_cast<U>((std::invoke(std::forward<Fn>(fn), **this)))}
                           : Option<U>{None};
    }

    template<class Fn, class U = std::invoke_result_t<Fn, T const&>>
    [[nodiscard]] constexpr Option<U> map(Fn&& fn) const&&
    noexcept(noexcept(std::invoke(std::forward<Fn>(fn), **this))) {
        static_assert(std::is_invocable_r_v<U, Fn, T const&>,
                      "Option<T&>::map<U>(Fn) requires Fn's return type to be convertible to an U");
        return bool(*this) ? Option<U>{static_cast<U>((std::invoke(std::forward<Fn>(fn), **this)))}
                           : Option<U>{None};
    }

    // map_or
    template<class Fn, class U, class R = std::invoke_result_t<Fn, T&>>
    [[nodiscard]] constexpr R map_or(Fn&& fn, U&& u) &&
    noexcept(noexcept(std::invoke(std::forward<Fn>(fn), **this)) && noexcept(static_cast<R>(std::forward<U>(u)))) {
        static_assert(std::is_invocable_r_v<R, Fn, T&>,
                      "Option<T>::map_or(Fn, U) requires U to be convertible to Fn's return type and Fn's argument to be T&");
        return bool(*this) ? static_cast<R>(std::invoke(std::forward<Fn>(fn), **this))
                           : static_cast<R>(std::forward<U>(u));
    }

    template<class Fn, class U, class R = std::invoke_result_t<Fn, T const&>>
    [[nodiscard]] constexpr R map_or(Fn&& fn, U&& u) const&& 
    noexcept(noexcept(std::invoke(std::forward<Fn>(fn), **this)) && noexcept(static_cast<R>(std::forward<U>(u)))) {
        static_assert(std::is_invocable_r_v<R, Fn, T const&>,
                      "Option<T>::map_or(Fn, U) requires U to be convertible to Fn's return type and Fn's argument to be T const&");
        return bool(*this) ? static_cast<R>(std::invoke(std::forward<Fn>(fn), **this))
                           : static_cast<R>(std::forward<U>(u));
    }

    // map_or_else
    template<class Fn, class DFn, class U = std::common_type_t<std::invoke_result_t<Fn, T&>, std::invoke_result_t<DFn>>>
    [[nodiscard]] constexpr U map_or_else(Fn&& fn, DFn&& dfn) &&
    noexcept(std::is_nothrow_invocable_r_v<U, Fn, T&> && std::is_nothrow_invocable_r_v<U, DFn>) {
        return bool(*this) ? static_cast<U>(std::invoke(std::forward<Fn>(fn), **this))
                           : static_cast<U>(std::invoke(std::forward<DFn>(dfn)));
    }

    template<class Fn, class DFn, class U = std::common_type_t<std::invoke_result_t<Fn, T const&>, std::invoke_result_t<DFn>>>
    [[nodiscard]] constexpr U map_or_else(Fn&& fn, DFn&& dfn) const&& 
    noexcept(std::is_nothrow_invocable_r_v<U, Fn, T const&> && std::is_nothrow_invocable_r_v<U, DFn>) {
        return bool(*this) ? static_cast<U>(std::invoke(std::forward<Fn>(fn), **this))
                           : static_cast<U>(std::invoke(std::forward<DFn>(dfn)));
    }

    // ok_or
    template<class E>
    [[nodiscard]] constexpr auto ok_or(E&& e) const&& {
        return bool(*this) ? result::Result<T&, E>{result::ok_tag, **this}
                           : result::Result<T&, E>(result::err_tag, std::forward<E>(e));
    }

    // ok_or_else
    template<class Fn>
    [[nodiscard]] constexpr auto ok_or_else(Fn&& fn) const&& {
        static_assert(std::is_invocable_v<Fn>, 
            "rust::Option<T&>::ok_or_else(Fn) requires Fn to be invocable with no arguments");
        using E = std::invoke_result_t<Fn>;
        return bool(*this) ? result::Result<T&, E>{result::ok_tag, **this}
                           : result::Result<T&, E>{result::err_tag, std::forward<Fn>(fn)()};
    }

    // match
    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) &&
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T&> || std::is_nothrow_invocable_v<Fns, None_t>>...>) {
        if (*this)
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, **this);
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, None);
    }

    template<class... Fns>
    [[nodiscard]] constexpr auto match(Fns&&... fns) const&&
    noexcept(std::conjunction_v<std::bool_constant<std::is_nothrow_invocable_v<Fns, T const&> || std::is_nothrow_invocable_v<Fns, None_t>>...>) {
        if (*this)
            return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, **this);
        return std::invoke(rust::detail::overloaded{std::forward<Fns>(fns)...}, None);
    }

    // as_deref
    template<class U = T, std::enable_if_t<rust::detail::can_be_dereferenced_v<U>>* = nullptr>
    [[nodiscard]] constexpr auto as_deref() const& {
        using Opt = Option<std::add_lvalue_reference_t<std::add_const_t<decltype(*std::declval<U>())>>>;
        return bool(*this) ? Opt(***this) : None;
    }
    
    // as_deref_mut
    template<class U = T, std::enable_if_t<rust::detail::can_be_dereferenced_v<U>>* = nullptr>
    [[nodiscard]] constexpr auto as_deref_mut() & {
        using Opt = Option<std::add_lvalue_reference_t<decltype(*std::declval<U>())>>;
        return bool(*this) ? Opt(***this) : None;
    }
    
    // as_ref
    [[nodiscard]] constexpr Option<T const&> as_ref() const& {
        return bool(*this) ? Option<T const&>(**this) : None;
    }

    // as_mut
    [[nodiscard]] constexpr Option<T&> as_mut() & {
        return bool(*this) ? Option<T&>(**this) : None;
    }

    // as_pin_ref
    
    // as_pin_mut

private:
    T* value_;
};

// user-defined deduction guide
template<class T>
Option(T) -> Option<T>;

} // namespace option
} // namespace rust