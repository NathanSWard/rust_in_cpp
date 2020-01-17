// panic.hpp

#pragma once

#include "_include.hpp"
#include "thread/thread.hpp"
#include "sys_common/thread.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <utility>

namespace rust {

template<class Msg>
[[noreturn]] void panic(Msg&& msg) {
#ifdef RUST_PANIC_SHOULD_ABORT
    std::cerr << "rust::panic: " << msg << '\n';
    std::abort();
#else // RUST_PANIC_SHOULD_ABORT
    if (thread::impl::update_panic_count(1) > 1) {
        std::cerr << "thread panicked while panicking. aborting.\n";
        std::abort();
    }
    std::cerr << "rust::panic: " << msg << '\n';
    sys::thread_exit();
#endif // RUST_PANIC_SHOULD_ABORT
}

[[noreturn]] void panic() {
    panic("explicit panic");
}

void assert(bool const b) {
    if (!b)
        panic("rust::assert() failed");
}

template<class T, class U>
void assert_eq(T const& t, U const& u) {
    if (t != u)
        panic("rust::assert_eq() failed");
}

} // namespace rust