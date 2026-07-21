#pragma once

#include "../lambda-data.hpp"

// Rebase the scalar tail after a collection buffer has been replaced. This is
// storage-only: the caller owns allocation policy and any GC root protocol.
void list_relocate_owned_tail(List* list, Item* old_items, int64_t old_capacity,
                              Item* new_items, int64_t new_capacity);
