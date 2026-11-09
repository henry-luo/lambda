#pragma once
#include "../lambda-data.hpp"

// The open-block dispatch uses branded values, never a forgeable ordinary map.
bool io_sqlite_is_target(Item value);
bool io_sqlite_scope_open_here(void);
Item io_sqlite_open(Item target);
Item io_sqlite_close(Bool unwinding);
