// sys_func_registry.h — declarations for the unified system function registry
// The registry is the single source of truth for all system function metadata.
// Full SysFuncInfo struct is defined in ast.hpp.
#pragma once

#include "ast.hpp"

// The single source of truth: defined in sys_func_registry.cpp
extern SysFuncInfo sys_func_defs[];
extern const int sys_func_def_count;
