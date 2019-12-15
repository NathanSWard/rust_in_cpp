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

class custom_panic_exception : public std::exception {
public:
    template<class Msg>
    explicit custom_panic_exception(Msg&& msg)
        : msg_{std::forward<Msg>(msg)}
    {}

    char const* what() const noexcept final { return msg_.c_str(); }
private:
    std::string const msg_;
};

class default_panic_exception : public std::exception {
public:
    char const* what() const noexcept final { return "explicit panic"; }
};

[[noreturn]] void panic() {
#ifdef RUST_EXCEPTIONS_ENABLED
    throw default_panic_exception{};
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
    std::cerr << "rust::panic: explicit panic" << '\n';
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
    throw custom_panic_exception{std::forward<Msg>(msg)};
#else // ^^^RUST_EXCEPTIONS_ENABLED^^^
    std::cerr << "rust::panic: " << msg << '\n';
    std::terminate();
#endif // RUST_EXCEPTIONS_ENABLED
}

} // namespace rust