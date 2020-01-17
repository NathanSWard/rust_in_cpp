// condvar.hpp

#pragma once

#include "platform.hpp"

#ifdef RUST_WINDOWS

#include "windows/condvar.hpp"

#elif defined(RUST_LINUX) || defined(RUST_MAC)

#include "unix/condvar.hpp"

#endif
