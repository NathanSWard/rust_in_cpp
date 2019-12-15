// result.hpp

#pragma once

#include "_detail.hpp"
#include "_include.hpp"
#include "_option_result_base.hpp"
#include "option.hpp"
#include "panic.hpp"

#include <functional>
#include <type_traits>
#include <utility>

namespace rust {

// result<T, E>::transpose
template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<detail::is_option_v<U>, option<result<typename U::value_type, E>>> 
result<T, E>::transpose() const& {
    using Opt = option<result<typename U::value_type, E>>;
    return is_ok() ? Opt{std::in_place, ok_tag, *get_val()} : Opt{std::in_place, err_tag, get_err()};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<detail::is_option_v<U>, option<result<typename U::value_type, E>>>  
result<T, E>::transpose() && {
    using Opt = option<result<typename U::value_type, E>>;
    return is_ok() ? Opt{std::in_place, ok_tag, std::move(*get_val())} : Opt{std::in_place, err_tag, std::move(get_err())};
}

template <class T, class E, class U, class F>
[[nodiscard]] inline constexpr bool operator==(result<T, E> const& lhs, const result<U, F> &rhs) {
    return (lhs.is_ok() != rhs.is_ok())
        ? false
        : (!lhs.is_ok() ? lhs.unwrap_err_unsafe() == rhs.unwrap_err_unsafe() 
                        : lhs.unwrap_unsafe() == rhs.unwrap_unsafe());
}
template <class T, class E, class U, class F>
[[nodiscard]] inline constexpr bool operator!=(result<T, E> const& lhs, result<U, F> const& rhs) {
    return (lhs.is_ok() != rhs.is_ok())
        ? true
        : (!lhs.is_ok() ? lhs.unwrap_err_unsafe() != rhs.unwrap_err_unsafe() 
                        : lhs.unwrap_unsafe() != rhs.unwrap_unsafe());
}

template <class T, class E,
          std::enable_if_t<(std::is_void_v<T> ||
                            std::is_move_constructible_v<T>) &&
                            detail::is_swappable<T>::value &&
                            std::is_move_constructible_v<E> &&
                            detail::is_swappable<E>::value>* = nullptr>
inline void swap(result<T, E>& lhs, result<T, E>& rhs) 
noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

} // namespace rust

namespace std {
// std::hash<result<T, E>> specialization
template<class T, class E>
struct hash<rust::result<T, E>> {
    [[nodiscard]] constexpr size_t operator()(rust::result<T, E> const& res) const {
        return res.is_ok() ? hash<T>(res.unwrap_unsafe()) : hash<E>(res.unwrap_err_unsafe());
    }
};
}