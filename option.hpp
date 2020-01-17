// Option.hpp

#pragma once

#include "_include.hpp"
#include "_detail.hpp"
#include "_option_result_base.hpp"
#include "panic.hpp"
#include "result.hpp"

#include <functional>
#include <type_traits>
#include <utility>

namespace rust {
namespace option {

// Option<T>::transpose
template<class T>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_result_v<U>, result::Result<Option<typename U::ok_type>, typename U::err_type>> 
Option<T>::transpose() && {
    using ok_t = typename U::ok_type;
    using err_t = typename U::err_type;
    return bool(*this) ? (bool(**this) ? result::Ok<ok_t, err_t>(std::move((**this).unwrap_unsafe())) 
                                       : result::Err<ok_t, err_t>(std::move((**this).unwrap_err_unsafe()))) 
                        : result::Ok<ok_t, err_t>(None);
}

template<class T>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_result_v<U>, result::Result<Option<typename U::ok_type>, typename U::err_type>> 
Option<T&>::transpose() && {
    using ok_t = typename U::ok_type;
    using err_t = typename U::err_type;
    return bool(*this) ? (bool(**this) ? result::Ok<ok_t, err_t>((**this).unwrap_unsafe()) 
                                       : result::Err<ok_t, err_t>((**this).unwrap_err_unsafe())) 
                       : result::Ok<ok_t, err_t>(None);
}

// Some
template<class T, class... Args>
[[nodiscard]] inline constexpr auto Some(Args&&... args) {
    return Option<T>{some_tag, std::forward<Args>(args)...};
}

template<class T, class U, class... Args>
[[nodiscard]] inline constexpr auto Some(std::initializer_list<U> il, Args&&... args) {
    return Option<T>{some_tag, il, std::forward<Args>(args)...};
}

/// Compares two Option objects
template <class T, class U> 
[[nodiscard]] inline constexpr bool operator==(Option<T> const& lhs, Option<U> const& rhs) {
    return bool(lhs) == bool(rhs) && (!bool(lhs) || *lhs == *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator!=(Option<T> const& lhs, Option<U> const& rhs) {
    return bool(lhs) != bool(rhs) || (bool(lhs) && *lhs != *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<(Option<T> const& lhs, Option<U> const& rhs) {
    return bool(rhs) && (!bool(lhs) || *lhs < *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>(Option<T> const& lhs, Option<U> const& rhs) {
    return bool(lhs) && (!bool(rhs) || *lhs > * rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<=(Option<T> const& lhs, Option<U> const& rhs) {
    return !bool(lhs) || (bool(rhs) && *lhs <= *rhs);
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>=(Option<T> const& lhs, Option<U> const& rhs) {
    return !bool(rhs) || (bool(lhs) && *lhs >= *rhs);
}

/// Compares an Option to a `None`
template <class T>
[[nodiscard]] inline constexpr bool operator==(Option<T> const& lhs, None_t) noexcept {
    return !bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator==(None_t, Option<T> const& rhs) noexcept {
    return !bool(rhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator!=(Option<T> const& lhs, None_t) noexcept {
    return bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator!=(None_t, Option<T> const& rhs) noexcept {
    return bool(rhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator<(Option<T> const&, None_t) noexcept {
    return false;
}
template <class T>
[[nodiscard]] inline constexpr bool operator<(None_t, Option<T> const& rhs) noexcept {
    return bool(rhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator<=(Option<T> const& lhs, None_t) noexcept {
    return !bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator<=(None_t, Option<T> const&) noexcept {
    return true;
}
template <class T>
[[nodiscard]] inline constexpr bool operator>(Option<T> const& lhs, None_t) noexcept {
    return bool(lhs);
}
template <class T>
[[nodiscard]] inline constexpr bool operator>(None_t, Option<T> const&) noexcept {
    return false;
}
template <class T>
[[nodiscard]] inline constexpr bool operator>=(Option<T> const&, None_t) noexcept {
    return true;
}
template <class T>
[[nodiscard]] inline constexpr bool operator>=(None_t, Option<T> const& rhs) noexcept {
    return !bool(rhs);
}

/// Compares the Option with a value.
template <class T, class U>
[[nodiscard]] inline constexpr bool operator==(Option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs == rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator==(U const& lhs, Option<T> const& rhs) {
    return bool(rhs) ? lhs == *rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator!=(Option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs != rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator!=(U const& lhs, Option<T> const& rhs) {
    return bool(rhs) ? lhs != *rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<(Option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs < rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<(U const& lhs, Option<T> const& rhs) {
    return bool(rhs) ? lhs < *rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<=(Option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs <= rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator<=(U const& lhs, Option<T> const& rhs) {
    return bool(rhs) ? lhs <= *rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>(Option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs > rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>(U const& lhs, Option<T> const& rhs) {
    return bool(rhs) ? lhs > * rhs : true;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>=(Option<T> const& lhs, U const& rhs) {
    return bool(lhs) ? *lhs >= rhs : false;
}
template <class T, class U>
[[nodiscard]] inline constexpr bool operator>=(U const& lhs, Option<T> const& rhs) {
    return bool(rhs) ? lhs >= *rhs : true;
}

template <class T,
    std::enable_if_t<std::is_move_constructible_v<T>> * = nullptr,
    std::enable_if_t<std::is_swappable_v<T>> * = nullptr>
void swap(Option<T>& lhs, Option<T>& rhs) 
noexcept(noexcept(lhs.swap(rhs))) {
    return lhs.swap(rhs);
}

} // namesace option
} // namespace rust

namespace {
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
struct hash<_enable_hash_helper<rust::option::Option<T>, remove_const_t<T>>> {
    [[nodiscard]] constexpr size_t operator()(rust::option::Option<T> const& opt) const {
        return bool(opt) ? hash<remove_const_t<T>>(*opt) : 0;
    }
};
} // namespace std
