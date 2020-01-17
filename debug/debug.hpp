// debug.hpp

#pragma once

#ifdef RUST_DEBUG

#include "../panic.hpp"

namespace rust {

template<class T, class U>
inline void debug_assert_eq(T const& t, U const& u) {
    if (t != u)
        panic("rust::debug_assert_eq() failed");
}

template<class T, class U, class Msg>
inline void debug_assert_eq(T const& t, U const& u, Msg&& msg) {
    if (t != u)
        panic(std::forward<Msg>(msg));
}

inline void debug_assert(bool const b) {
    if (!b)
        panic("rust::debug_assert() failed");
}

template<class Msg>
inline void debug_assert(bool const b, Msg&& msg) {
    if (!b)
        panic(std::forward<Msg>(msg));
}

} // namespace rust

#else // RUST_DEBUG

#define debug_assert_eq(...) (static_cast<void>(0))
#define debug_assert(...) (static_cast<void>(0))

#endif // RUST_DEBUG