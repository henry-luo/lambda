#pragma once

// Internal, GC-owned lexical environment used by the AST interpreter. This
// stays C-compatible because the collector traces the raw record directly.

#include <stdint.h>

struct NameScope;

typedef struct JsInterpEnv {
    struct JsInterpEnv* outer;
    struct NameScope* scope;
    uint32_t slot_count;
    uint32_t reserved;
    uint64_t slots[1];
} JsInterpEnv;
