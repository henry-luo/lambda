#pragma once

#include "../lambda-data.hpp"

struct Runtime;

#if !defined(LAMBDA_NO_TASKS) && !defined(LAMBDA_NO_FILE_IO)
int lambda_create_output_parent_dirs(const char* path);
bool shell_output_is(Item value);
extern "C" int64_t fn_shell_is_stream(Item value);
extern "C" Item fn_shell_record_stage(Item source, Item callback, Item root, Item caught, int64_t filter);
Item shell_output_file(Item value, Item path, bool append, bool atomic);
void shell_task_finish();
Item shell_script_result(Runtime* runtime, Item value, bool run_main);
#else
inline bool shell_output_is(Item) { return false; }
extern "C" inline int64_t fn_shell_is_stream(Item) { return 0; }
extern "C" inline Item fn_shell_record_stage(Item, Item, Item, Item, int64_t) { return ItemError; }
inline Item shell_output_file(Item, Item, bool, bool) { return ItemError; }
inline void shell_task_finish() {}
inline Item shell_script_result(Runtime*, Item value, bool) { return value; }
#endif

extern "C" Item pn_shell_start(Item input, Item program, Item arguments, Item errors, Item timeout);
extern "C" Item pn_shell_stdin();
