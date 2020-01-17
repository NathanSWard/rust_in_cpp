// thread.hpp

#pragma once

#include "../sys/thread.hpp"

namespace rust {
namespace sys {

[[noreturn]] inline void thread_exit() {
    impl::thread_exit();
}

} // namespace sys
} // namespace rust