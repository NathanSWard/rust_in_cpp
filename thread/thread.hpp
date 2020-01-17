// thread.hpp

#pragma once

#include "../panic.hpp"
#include "../sys_common/thread.hpp"

namespace rust {
namespace thread {
    
namespace impl {
std::size_t update_panic_count(std::size_t const amt) noexcept {
    thread_local std::size_t panic_count = 0;
    panic_count += amt;
    return panic_count;
}
} // namespace impl

[[nodiscard]] inline bool panicking() noexcept {
    return impl::update_panic_count(0) != 0;
}

} // namespace thread
} // namespace rust