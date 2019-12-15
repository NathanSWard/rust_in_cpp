// panic.hpp

#pragma once

#include <exception>
#include <iostream>
#include <string>
#include <utility>

#if defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define RUST_EXCEPTIONS_ENABLED
#endif

namespace rust {

template<bool HasCustomMessage>
class panic_exception;

template<>
class panic_exception<true> : std::exception {
public:
    template<class Msg>
    explicit panic_exception(Msg&& msg)
        : msg_{std::forward<Msg>(msg)}
    {}

    char const* what() const noexcept final {
        return msg_.c_str();
    }
private:
    std::string const msg_;
};

constexpr char const* default_panic_message = "explicit panic";

template<>
class panic_exception<false> : std::exception {
public:
    char const* what() const noexcept final {
        return default_panic_message;
    }
};

[[noreturn]] void panic() {
#ifdef RUST_EXCEPTIONS_ENABLED
    throw panic_exception<false>{};
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
    std::cerr << "rust::panic: " << default_panic_message << '\n';
    std::terminate();
#endif // RUST_EXCEPTIONS_ENABLED
}

template<class Ex, std::enable_if_t<std::is_base_of_v<std::exception, Ex>>* = nullptr>
[[noreturn]] void panic(Ex&& ex) {
#ifdef RUST_EXCEPTIONS_ENABLED
    throw std::forward<Ex>(ex);
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^ 
    std::cerr << "rust::panic: " << ex.what() << '\n';
    std::terminate();
#endif // RUST_EXCEPTIONS_ENABLED
}

template<class Msg, std::enable_if_t<!std::is_base_of_v<std::exception, Msg>>* = nullptr>
[[noreturn]] void panic(Msg&& msg) {
#ifdef RUST_EXCEPTIONS_ENABLED
    throw panic_exception<true>{std::forward<Msg>(msg)};
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
    std::cerr << "rust::panic: " << msg << '\n';
    std::terminate();
#endif // RUST_EXCEPTIONS_ENABLED
}

} // namespace rust