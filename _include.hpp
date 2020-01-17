// _include.hpp

#pragma once

#if __has_cpp_attribute(likely)
    #define RUST_ATTR_LIKELY [[likely]]
#else
    #define RUST_ATTR_LIKELY 
#endif

#if __has_cpp_attribute(unlikely)
    #define RUST_ATTR_UNLIKELY [[unlikely]]
#else
    #define RUST_ATTR_UNLIKELY
#endif