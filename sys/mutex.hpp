// mutex.hpp

#pragma once

#include "platform.hpp"

#ifdef RUST_WINDOWS

#include "windows/mutex.hpp"

#elif defined(RUST_LINUX) || defined(RUST_MAC)

#include "unix/mutex.hpp"

#endif