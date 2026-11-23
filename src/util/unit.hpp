#pragma once

#include <sys/resource.h>

namespace unit {
    enum class size:rlim_t {
        B = 1,
        KB = 1024,
        MB  = 1048576,
    };
    constexpr rlim_t bytes(rlim_t n, size u) {
        return n * static_cast<rlim_t>(u);
    }
}
