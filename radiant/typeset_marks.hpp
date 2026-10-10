#pragma once
#include "typeset.hpp"
#include "../lib/mempool.h"

enum TypesetMarkKind : uint8_t { TYPESET_MARK_STRING, TYPESET_MARK_RUNNING, TYPESET_MARK_NATIVE };
enum TypesetMarkSelection : uint8_t {
    TYPESET_MARK_FIRST, TYPESET_MARK_START, TYPESET_MARK_LAST, TYPESET_MARK_FIRST_EXCEPT,
};
struct TypesetMark {
    TypesetMarkKind kind;
    const char* name;
    TypesetSource source;
    const char* text;
    lam::Up<const TypesetRecord> value;
    uint32_t page_number;
    bool at_page_start;
};
struct TypesetMarkCheckpoint { uint64_t provider, generation; size_t count; };
struct TypesetMarkStore {
    uint64_t provider, generation;
    Pool* pool;
    TypesetMark* entries;
    size_t count, capacity;
    TypesetSourceScope sources;
};

// Payloads remain producer-owned immutable values throughout trial and replay.
TypesetStatus typeset_mark_append(TypesetMarkStore* store, const TypesetMark* mark);
TypesetMarkCheckpoint typeset_marks_checkpoint(const TypesetMarkStore* store);
TypesetStatus typeset_marks_restore(TypesetMarkStore* store, TypesetMarkCheckpoint checkpoint);
const TypesetMark* typeset_mark_select(const TypesetMarkStore* store, TypesetMarkKind kind,
    const char* name, uint32_t page_number, TypesetMarkSelection selection);
