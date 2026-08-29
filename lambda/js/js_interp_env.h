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
    // Class evaluation publishes this lexical private-name owner for member
    // bodies, field initializers, static blocks, and their nested closures.
    uint64_t private_home_class;
    // Flat [source name, identity key] pairs exported to a direct-eval bridge.
    uint64_t private_bindings;
    // Flat [name, value] pairs declared by direct eval in this variable
    // environment. Closures retain this GC-owned record after the caller exits.
    uint64_t eval_bindings;
    // The nearest non-arrow function's live lexical `this` binding. Derived
    // constructors initialize this cell only after their `super()` call.
    uint64_t lexical_this;
    struct AstNode* function_node;
    uint32_t slot_count;
    uint8_t arguments_are_mapped;
    uint8_t has_lexical_this;
    uint8_t reserved[2];
    uint64_t slots[1];
} JsInterpEnv;
