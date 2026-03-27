#pragma once
// py_stdlib.h — Python standard library module stubs (Phase C)
// Each py_stdlib_*_init() returns a Map Item representing the module namespace.

#ifdef __cplusplus
extern "C" {
#endif

#include "../lambda.h"

// Module initializers — each returns a dict (Map Item) with module contents.
// Called at import time by the transpiler's builtin module lookup table.
Item py_stdlib_math_init(void);
Item py_stdlib_os_init(void);
Item py_stdlib_os_path_init(void);
Item py_stdlib_sys_init(void);
Item py_stdlib_re_init(void);
Item py_stdlib_json_init(void);
Item py_stdlib_time_init(void);
Item py_stdlib_random_init(void);
Item py_stdlib_functools_init(void);
Item py_stdlib_collections_init(void);
Item py_stdlib_io_init(void);
Item py_stdlib_copy_init(void);

// Builtin module lookup: returns init function or NULL if not a builtin.
// Used by the transpiler for both `import X` and `from X import Y`.
typedef Item (*PyBuiltinModuleInitFn)(void);
PyBuiltinModuleInitFn py_stdlib_find_builtin(const char* name, int len);

#ifdef __cplusplus
}
#endif
