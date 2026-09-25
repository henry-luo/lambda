#pragma once

// Hand-built validator patterns shared by the validator GTests.

#include "../lambda/lambda-data.hpp"
#include "../lib/mempool.h"

// A declared content pattern of `count` `any` slots, which admits exactly
// `count` children of any kind (S11.1.6v3). Hand-built element types use it
// where they once set a content count. The slot's `any` is allocated here
// rather than taken from TYPE_ANY, so the test binaries need no runtime global.
static inline TypeList* test_any_content_pattern(Pool* pool, int count) {
    Type* any_type = (Type*)pool_calloc(pool, sizeof(Type));
    any_type->type_id = LMD_TYPE_ANY;  // kind 0 is TYPE_KIND_SIMPLE
    TypeType* any_slot = (TypeType*)pool_calloc(pool, sizeof(TypeType));
    any_slot->type_id = LMD_TYPE_TYPE;
    any_slot->type = any_type;

    TypeList* pattern = (TypeList*)pool_calloc(pool, sizeof(TypeList));
    pattern->type_id = LMD_TYPE_ARRAY;
    pattern->length = count;
    pattern->item_patterns = (Item*)pool_calloc(pool, sizeof(Item) * (size_t)count);
    pattern->item_is_type_pattern = (uint8_t*)pool_calloc(pool, (size_t)count);
    for (int i = 0; i < count; i++) {
        pattern->item_patterns[i].type = (Type*)any_slot;
        pattern->item_is_type_pattern[i] = 1;
    }
    return pattern;
}
