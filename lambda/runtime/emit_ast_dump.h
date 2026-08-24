// emit_ast_dump.h — Emit canonical Lambda AST dumps
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Parse a Lambda .ls file and emit a canonical AST-kind dump to stdout.
// Returns 0 on success, 1 on error.
int emit_ast_dump_file(const char* script_path);

// Parse a JavaScript/TypeScript source file and emit a canonical AST-kind dump
// to stdout. Returns 0 on success, 1 on error.
int emit_js_ast_dump_file(const char* script_path);

#ifdef __cplusplus
}
#endif
