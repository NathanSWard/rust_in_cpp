// thread.hpp

#pragma once

#include <pthread.h>

namespace rust {
namespace sys {
namespace impl {

[[noreturn]] inline void thread_exit() {
    pthread_exit(nullptr);
}

} // namespace rust
} // namespace sys
} // namespace impl