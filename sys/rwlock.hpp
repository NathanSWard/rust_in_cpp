// rwlock.hpp

#pragma once

#include "platform.hpp"

#ifdef RUST_WINDOWS

#include "windows/rwlock.hpp"

#elif defined(RUST_LINUX) || defined(RUST_MAC)

#include "unix/rwlock.hpp"

#endif