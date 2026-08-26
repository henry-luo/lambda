#pragma once

// Internal, GC-owned lexical environment used by the AST interpreter. This
// stays C-compatible because the collector traces the raw record directly.

#include <stdint.h>

struct NameScope;

typedef struct JsInterpEnv {
    struct JsInterpEnv* outer;
    struct NameScope* scope;
    // The function activation owns its materialized exotic arguments object.
    // Arrows leave this empty and resolve the nearest outer function record.
    uint64_t arguments_object;
    struct AstNode* function_node;
    uint32_t slot_count;
    uint8_t arguments_are_mapped;
    uint8_t reserved[3];
    uint64_t slots[1];
} JsInterpEnv;
