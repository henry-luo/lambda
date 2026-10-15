#pragma once
#include "mvp_lmd_runtime.h"

// enabled only for programs that admit Promise or async syntax.
Item mvp_lmd_promise_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
Item mvp_lmd_promise_property(Item owner, Item key, Item value, int64_t operation,
    Item receiver);
Item mvp_lmd_async_call(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);

Item mvp_lmd_suspended_functions_initialize(MvpLmdProgram* program, MvpLmdLibraryState* state);
