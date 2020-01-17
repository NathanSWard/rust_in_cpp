// Result.hpp

#pragma once

#include "_include.hpp"
#include "_detail.hpp"
#include "_option_result_base.hpp"
#include "option.hpp"
#include "panic.hpp"

#include <functional>
#include <type_traits>
#include <utility>

namespace rust {
namespace result {

// Result<T, E>::transpose
template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
Result<T, E>::transpose() const& {
    using Opt = option::Option<Result<typename U::value_type, E>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, *get_val()};
        return Opt{option::None};
    }   
    return Opt{err_tag, get_err()};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>>  
Result<T, E>::transpose() && {
    using Opt = option::Option<Result<typename U::value_type, E>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, std::move(*get_val())};
        return Opt{option::None};
    }   
    return Opt{err_tag, std::move(get_err())};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
Result<T&, E>::transpose() const& {
    using Opt = option::Option<Result<typename U::value_type, E>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, *get_val()};
        return Opt{option::None};
    }   
    return Opt{err_tag, get_err()};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E>>> 
Result<T&, E>::transpose() && {
    using Opt = option::Option<Result<typename U::value_type, E>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, *get_val()};
        return Opt{option::None};
    }   
    return Opt{err_tag, std::move(get_err())};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E&>>> 
Result<T, E&>::transpose() const& {
    using Opt = option::Option<Result<typename U::value_type, E&>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, *get_val()};
        return Opt{option::None};
    }   
    return Opt{err_tag, get_err()};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E&>>> 
Result<T, E&>::transpose() && {
    using Opt = option::Option<Result<typename U::value_type, E&>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, std::move(*get_val())};
        return Opt{option::None};
    }   
    return Opt{err_tag, get_err()};
}

template<class T, class E>
template<class U>
[[nodiscard]] constexpr std::enable_if_t<rust::detail::is_option_v<U>, option::Option<Result<typename U::value_type, E&>>> 
Result<T&, E&>::transpose() const {
    using Opt = option::Option<Result<typename U::value_type, E&>>;
    if (is_ok()) {
        if (get_val())
            return Opt{ok_tag, *get_val()};
        return Opt{option::None};
    }   
    return Opt{err_tag, get_err()};
}

template <class T, class E, class U, class F>
[[nodiscard]] inline constexpr bool operator==(Result<T, E> const& lhs, const Result<U, F> &rhs) {
    return (lhs.is_ok() != rhs.is_ok())
        ? false
        : (!lhs.is_ok() ? lhs.unwrap_err_unsafe() == rhs.unwrap_err_unsafe() 
                        : lhs.unwrap_unsafe() == rhs.unwrap_unsafe());
}
template <class T, class E, class U, class F>
[[nodiscard]] inline constexpr bool operator!=(Result<T, E> const& lhs, Result<U, F> const& rhs) {
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
inline void swap(Result<T, E>& lhs, Result<T, E>& rhs) 
noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}

} // namespace result
} // namespace rust

namespace std {
// std::hash<Result<T, E>> specialization
template<class T, class E>
struct hash<rust::result::Result<T, E>> {
    [[nodiscard]] constexpr size_t operator()(rust::result::Result<T, E> const& res) const {
        return res.is_ok() ? hash<T>{res.unwrap_unsafe()}() : hash<E>{res.unwrap_err_unsafe()}();
    }
};
}