#pragma once

#include <stdint.h>

// Inclusive Unicode scalar interval shared by language-facing parsers.
struct CodePointInterval {
    uint32_t first;
    uint32_t last;
};

static inline bool codepoint_interval_contains(const CodePointInterval* interval,
                                               uint32_t codepoint) {
    return codepoint >= interval->first && codepoint <= interval->last;
}
