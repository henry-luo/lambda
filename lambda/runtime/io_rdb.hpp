#pragma once
#include "../lambda-data.hpp"

// S12.4.1v3–S12.4.3: a task owns the branded session for its open scope.
bool io_rdb_is_target(Item value);
bool io_rdb_scope_open_here(void);
Item io_rdb_open(Item target);
Item io_rdb_close(Bool unwinding);
