// _detail.hpp

#pragma once

#include <type_traits>

namespace rust {

template<class T> class option;
template<class T, class E> class result;

namespace detail {

// struct overloaded used for match function
template<class... Fns> struct overloaded : Fns... { using Fns::operator()...; };
template<class... Fns> overloaded(Fns...) -> overloaded<Fns...>;

// Trait for checking if a type is a rust::option
template <class T> struct is_option_impl : std::false_type {};
template <class T> struct is_option_impl<option<T>> : std::true_type {};
template <class T> using is_option = is_option_impl<std::decay_t<T>>;
template <class T> static constexpr bool is_option_v = is_option<T>::value;

// Trait for checking if a type is a rust::result
template <class T> struct is_result_impl : std::false_type {};
template <class T, class E> struct is_result_impl<result<T, E>> : std::true_type {};
template<class T> using is_result = is_result_impl<std::decay_t<T>>;
template<class T> static constexpr bool is_result_v = is_result<T>::value;

// C++20's remove_cvref
template<class T>
struct remove_cvref {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template<class T>
using remove_cvref_t = typename remove_cvref<T>::type;

// C++20's is_nothrow_convertible
template<class From, class To>
struct is_nothrow_convertible {
    static constexpr bool value = std::is_convertible_v<From, To> && noexcept(noexcept(To(std::declval<From>())));
};

template<class From, class To>
static constexpr bool is_nothrow_convertible_v = is_nothrow_convertible<From, To>::value;

// Check if invoking F for some Us returns void
template <class F, class = void, class... U> struct returns_void_impl;
template <class F, class... U>
struct returns_void_impl<F, std::void_t<std::invoke_result_t<F, U...>>, U...>
    : std::is_void<std::invoke_result_t<F, U...>> {};
template <class F, class... U>
using returns_void = returns_void_impl<F, void, U...>;

template<class F, class... U>
static constexpr bool returns_void_v = returns_void<F, U...>::value;

template <class T, class... U>
using enable_if_ret_void = std::enable_if_t<returns_void_v<T&&, U...>>;

template <class T, class... U>
using disable_if_ret_void = std::enable_if_t<!returns_void_v<T&&, U...>>;

} // namespace detail
} // namespace rust