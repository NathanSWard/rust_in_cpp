// thread.hpp

#pragma once

#include "platform.hpp"

#ifdef RUST_WINDOWS

#include "windows/thread.hpp"

#elif defined(RUST_LINUX) || defined(RUST_MAC)

#include "unix/thread.hpp"

#endif