// emit_ast_dump.h — Emit canonical Lambda AST dumps
#pragma once

#ifdef __cplusplus
#include "ast.hpp"

// Dump output primitives. The Lambda and JavaScript dumps share one AST and
// therefore one rendering of spans, escaped text and indentation; only the
// node-kind spellings and the per-kind fields differ between them.
const char* emit_dump_node_src(const char* source, SourceSpan span, int* out_len);
void emit_dump_escaped_string(const char* str, int len);
void emit_dump_indent(int indent);
void emit_dump_string_field(const char* label, String* str);
void emit_dump_source_field(const char* source, SourceSpan span);
#endif

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
