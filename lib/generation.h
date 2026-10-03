// Generation counters stamp cached data with the state it was computed from.
// One form for every cache check: a counter starts at 1 and skips 0 when it
// wraps, so a zero stamp always means "never stamped", and a stamp is current
// only when it is nonzero and equal to the counter.
#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline uint64_t generation_next(uint64_t current) {
    uint64_t next = current + 1;
    return next ? next : 1;
}

static inline uint32_t generation_next32(uint32_t current) {
    uint32_t next = current + 1;
    return next ? next : 1;
}

static inline bool generation_stamped(uint64_t stamp) {
    return stamp != 0;
}

static inline bool generation_current(uint64_t stamp, uint64_t current) {
    return stamp != 0 && stamp == current;
}
