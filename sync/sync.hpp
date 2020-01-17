// sync.hpp

#pragma once

#include "../_detail.hpp"
#include "../sys_common/mutex.hpp"
#include "../sys_common/rwlock.hpp"
#include "../option.hpp"
#include "../result.hpp"
#include "../thread/thread.hpp"

#include <atomic>
#include <type_traits>

namespace rust { 
namespace sync {

class BarrierWaitResult;
class Barrier;

class WaitTimeoutResult;
class Condvar;

class Once;

} // namespace sync
} // namespace rust