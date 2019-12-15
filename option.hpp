// option.hpp

#pragma once

#include "_detail.hpp"
#include "_include.hpp"
#include "_option_result_base.hpp"
#include "panic.hpp"
#include "result.hpp"

#include <functional>
#include <type_traits>
#include <utility>

namespace rust {

// option<T>::transpose
template<class T>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<detail::is_result_v<U>, result<option<typename U::ok_type>, typename U::err_type>> 
option<T>::transpose() const& {
    using Ok = typename U::ok_type;
    using Err = typename U::err_type;
    return bool(*this) ? (bool(**this) ? ok<Ok, Err>((**this).unwrap_unsafe()) 
                                       : err<Ok, Err>((**this).unwrap_err_unsafe())) 
                        : ok<Ok, Err>(none);
}

template<class T>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<detail::is_result_v<U>, result<option<typename U::ok_type>, typename U::err_type>> 
option<T>::transpose() && {
    using Ok = typename U::ok_type;
    using Err = typename U::err_type;
    return bool(*this) ? (bool(**this) ? ok<Ok, Err>(std::move((**this).unwrap_unsafe())) 
                                       : err<Ok, Err>(std::move((**this).unwrap_err_unsafe()))) 
                        : ok<Ok, Err>(none);
}

/// Compares two option objects
template <class T, class U> 
[[nodiscard]] inline constexpr bool operator==(option<T> const& lhs, option<U> const& rhs) {
    return bool(lhs) == bool(rhs) && (!bool(lhs) || *lhs == *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator!=(option<T> const& lhs, option<U> const& rhs) {
    return bool(lhs) != bool(rhs) || (bool(lhs) && *lhs != *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<(option<T> const& lhs, option<U> const& rhs) {
    return bool(rhs) && (!bool(lhs) || *lhs < *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>(option<T> const& lhs, option<U> const& rhs) {
    return bool(lhs) && (!bool(rhs) || *lhs > * rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<=(option<T> const& lhs, option<U> const& rhs) {
    return !bool(lhs) || (bool(rhs) && *lhs <= *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>=(option<T> const& lhs, option<U> const& rhs) {
    return !bool(rhs) || (bool(lhs) && *lhs >= *rhs);
}

/// Compares an option to a `none`
template <class T>
[[nodiscard]] inline constexpr bool operator==(option<T> const& lhs, none_t) noexcept {
    return !bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator==(none_t, option<T> const& rhs) noexcept {
    return !bool(rhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator!=(option<T> const& lhs, none_t) noexcept {
    return bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator!=(none_t, option<T> const& rhs) noexcept {
    return bool(rhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator<(option<T> const&, none_t) noexcept {
    return false;
}
template <class T>
[[nodiscard]] inline constexpr bool operator<(none_t, option<T> const& rhs) noexcept {
    return bool(rhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator<=(option<T> const& lhs, none_t) noexcept {
    return !bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator<=(none_t, option<T> const&) noexcept {
    return true;
}
template <class T>
[[nodiscard]] inline constexpr bool operator>(option<T> const& lhs, none_t) noexcept {
    return bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator>(none_t, option<T> const&) noexcept {
    return false;
}
template <class T>
[[nodiscard]] inline constexpr bool operator>=(option<T> const&, none_t) noexcept {
    return true;
}
template <class T>
[[nodiscard]] inline constexpr bool operator>=(none_t, option<T> const& rhs) noexcept {
    return !bool(rhs);
}

/// Compares the option with a value.
template <class T, class U>
[[nodiscard]] inline constexpr bool operator==(option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs == rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator==(U const& lhs, option<T> const& rhs) {
    return bool(rhs) ? lhs == *rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator!=(option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs != rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator!=(U const& lhs, option<T> const& rhs) {
    return bool(rhs) ? lhs != *rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<(option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs < rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<(U const& lhs, option<T> const& rhs) {
    return bool(rhs) ? lhs < *rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<=(option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs <= rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<=(U const& lhs, option<T> const& rhs) {
    return bool(rhs) ? lhs <= *rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>(option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs > rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>(U const& lhs, option<T> const& rhs) {
    return bool(rhs) ? lhs > * rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>=(option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs >= rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>=(U const& lhs, option<T> const& rhs) {
    return bool(rhs) ? lhs >= *rhs : true;
}

template <class T,
    std::enable_if_t<std::is_move_constructible_v<T>> * = nullptr,
    std::enable_if_t<std::is_swappable_v<T>> * = nullptr>
void swap(option<T>& lhs, option<T>& rhs) 
noexcept(noexcept(lhs.swap(rhs))) {
    return lhs.swap(rhs);
}

namespace detail {
struct _secret_tag {};
} // namespace detail

// some(T) -> option<T>
template <class T = detail::_secret_tag, class U,
    class Ret =
    std::conditional_t<std::is_same_v<T, detail::_secret_tag>,
    std::decay_t<U>, T>>
[[nodiscard]] inline constexpr option<Ret> some(U&& v) {
    return option<Ret>{std::forward<U>(v)};
}

template <class T, class... Args>
[[nodiscard]] inline constexpr option<T> some(Args&&... args) {
    return option<T>{some_tag, std::forward<Args>(args)...};
}
template <class T, class U, class... Args>
[[nodiscard]] inline constexpr option<T> some(std::initializer_list<U> il, Args&&... args) {
    return option<T>{some_tag, il, std::forward<Args>(args)...};
}

// user-defined deduction guide
template <class T>
option(T) -> option<T>;

} // namespace rust

namespace {
// from libc++ 
// SFINAE protection for std::hash<> specialization
template <class Key, class Hash>
using _check_hash_requirements = std::bool_constant<
    std::is_copy_constructible_v<Hash> &&
    std::is_move_constructible_v<Hash> &&
    std::is_invocable_r_v<std::size_t, Hash, Key const&>>;

template <class Key, class Hash = std::hash<Key> >
using _has_enabled_hash = std::bool_constant<
    _check_hash_requirements<Key, Hash>::value &&
    std::is_default_constructible_v<Hash>>;

template <class T, class>
using _enable_hash_helper_impl = T;

template <class T, class Key>
using _enable_hash_helper = _enable_hash_helper_impl<T,
    std::enable_if_t<_has_enabled_hash<Key>::value>>;
} // namespace

namespace std {
template<class T>
struct hash<_enable_hash_helper<rust::option<T>, remove_const_t<T>>> {
    [[nodiscard]] constexpr size_t operator()(rust::option<T> const& opt) const {
        return bool(opt) ? hash<remove_const_t<T>>(*opt) : 0;
    }
};
} // namespace std
