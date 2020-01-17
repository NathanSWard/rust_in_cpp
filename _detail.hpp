// _detail.hpp

#pragma once

#include <type_traits>

namespace rust {

namespace option { template<class T> class Option; }
namespace result { template<class T, class E> class Result; }
namespace boxed { template<class T> class Box; }
template<class T> class non_null;

namespace sync {
    template<class T> class Mutex;
}

namespace detail {

template<class T, class... Args>
using enable_variadic_ctr =  std::enable_if_t<(sizeof...(Args) != 1 || (!std::is_same_v<std::decay_t<Args>, T> || ...)), int>;

// struct overloaded used for match function
template<class... Fns> struct overloaded : Fns... { using Fns::operator()...; };
template<class... Fns> overloaded(Fns...) -> overloaded<Fns...>;

// Trait for checking if a type is a rust::option::Option
template <class T> struct is_option_impl : std::false_type {};
template <class T> struct is_option_impl<option::Option<T>> : std::true_type {};
template <class T> using is_option = is_option_impl<std::decay_t<T>>;
template <class T> static constexpr bool is_option_v = is_option<T>::value;

// Trait for checking if a type is a rust::result::Result
template <class T> struct is_result_impl : std::false_type {};
template <class T, class E> struct is_result_impl<result::Result<T, E>> : std::true_type {};
template<class T> using is_result = is_result_impl<std::decay_t<T>>;
template<class T> static constexpr bool is_result_v = is_result<T>::value;

// Trait for checking if a type is a rust::non_null
template <class T> struct is_non_null_impl : std::false_type {};
template <class T> struct is_non_null_impl<non_null<T>> : std::true_type {};
template <class T> using is_non_null = is_non_null_impl<std::decay_t<T>>;
template <class T> static constexpr bool is_non_null_v = is_non_null<T>::value;

// Trait for checking if a type is a rust::boxed::Box
template <class T> struct is_box_impl : std::false_type {};
template <class T> struct is_box_impl<boxed::Box<T>> : std::true_type {};
template <class T> using is_box = is_box_impl<std::decay_t<T>>;
template <class T> static constexpr bool is_box_v = is_box<T>::value;

// Trait for checking if a type is a rust::sync::Mutex
template <class T> struct is_mutex_impl : std::false_type {};
template <class T> struct is_mutex_impl<sync::Mutex<T>> : std::true_type {};
template <class T> using is_mutex = is_mutex_impl<std::decay_t<T>>;
template <class T> static constexpr bool is_mutex_v = is_mutex<T>::value;

// Tratis for checking if a type can be dereferenced
template<class T>
decltype(static_cast<void>(*std::declval<T>()), std::true_type{})
can_be_dereferenced_impl(int);

template<class>
std::false_type can_be_dereferenced_impl(...);

template<class T>
struct can_be_dereferenced : decltype(detail::can_be_dereferenced_impl<T>(0)) {};

template<class T>
static constexpr bool can_be_dereferenced_v = can_be_dereferenced<T>::value;

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