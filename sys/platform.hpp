// platform.hpp

#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define RUST_WINDOWS
#elif defined(__APPLE__) && TARGET_OS_MAC
    #define RUST_MAC
    #define RUST_POSIX
#elif defined(__linux__)
    #define RUST_LINUX
    #define RUST_POSIX
#else
    #error "Unknown compiler"
#endif