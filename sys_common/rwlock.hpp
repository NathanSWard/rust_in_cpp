// rwlock.hpp

#pragma once

#include "../sys/rwlock.hpp"

namespace rust {
namespace sys {

class RWLock {
    impl::RWLock rwlock_{};
public:
    void read() { rwlock_.read(); }
    [[nodiscard]] bool try_read() { return rwlock_.try_read(); }
    void write() { rwlock_.write(); }
    [[nodiscard]] bool try_write() { return rwlock_.try_write(); }
    void read_unlock() { rwlock_.read_unlock(); }
    void write_unlock() { rwlock_.write_unlock(); }
};

} // namespace sys
} // namespace rust