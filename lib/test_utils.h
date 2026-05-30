// test_utils.h — shared helpers for gtest test files.
//
// Pulls the most-duplicated test scaffolding into one place:
//   - temp dir / temp file management (always under ./temp/, never /tmp)
//   - cross-platform process spawn with output capture
//   - file slurp + existence check
//   - in-place string trim / line-strip
//   - runtime fixture (Pool + Heap + EvalContext + thread-local `context`)
//
// Include this header from any gtest *.cpp. To use, the test entry in
// build_lambda_config.json must list "lib/test_utils.cpp" under
// "additional_sources".

#ifndef LIB_TEST_UTILS_H
#define LIB_TEST_UTILS_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Temp dir / file helpers
// =============================================================================
//
// All paths are created under ./temp/<base>_<pid>_<epoch> to avoid races when
// tests run in parallel. CLAUDE.md rule #2: never write to /tmp.
//
// Returned strings are heap-allocated; caller must free().

// Create a fresh subdirectory under ./temp/. Aborts (FAIL) on failure.
//   tu_mkdtemp("dir_test") -> "./temp/dir_test_12345_1717000000"
char* tu_mkdtemp(const char* base);

// Recursive remove. No-op if path doesn't exist or path is NULL.
void tu_rmtree(const char* path);

// Write `content` to a fresh file under ./temp/. Returns its path.
//   tu_write_temp("foo", "json", "{}")
//     -> "./temp/foo_12345_1717000000.json" containing "{}"
char* tu_write_temp(const char* base, const char* ext, const char* content);

// =============================================================================
// File I/O
// =============================================================================

// Slurp entire file into heap-allocated null-terminated buffer.
// Returns NULL on error. Caller must free().
char* tu_slurp(const char* path);

// True if path exists (file or dir).
bool tu_exists(const char* path);

// =============================================================================
// Process spawn
// =============================================================================

// Run a shell command, capture combined stdout+stderr.
// On success returns malloc'd null-terminated string and writes the exit code
// to *exit_code (if non-NULL). On failure returns NULL.
// Caller must free() the result.
char* tu_run(const char* cmd, int* exit_code);

// =============================================================================
// String normalization
// =============================================================================

// Trim trailing whitespace (spaces, tabs, CR, LF) from `s` in place.
void tu_trim_trailing(char* s);

// Remove every line in `s` that begins with `prefix` (in place).
// Useful for stripping non-deterministic lines like "__TIMING__:..." before
// golden-file comparison.
void tu_strip_lines(char* s, const char* prefix);

// =============================================================================
// Pool-only fixture
// =============================================================================
//
// For the common case of `log_init(NULL); pool = pool_create()` followed by an
// assertion. Most tests only need this — they don't touch Heap/EvalContext, so
// they can stay out of the heavy transpiler.hpp include chain.
//
//   class MyTest : public ::testing::Test {
//   protected:
//       Pool* pool;
//       void SetUp() override    { pool = tu_setup_pool(); }
//       void TearDown() override { tu_teardown_pool(pool); }
//   };

// Returns a freshly-created pool. log_init(NULL) is called once per process
// (subsequent calls are cheap no-ops in lib/log.c). Aborts on alloc failure.
struct Pool* tu_setup_pool(void);

// Destroy `pool` if non-NULL. Safe to call repeatedly with the same pointer
// only if the caller nulls it after the first call.
void tu_teardown_pool(struct Pool* pool);

#ifdef __cplusplus
}  // extern "C"
#endif

// =============================================================================
// Runtime fixture (C++ only)
// =============================================================================
//
// Sets up the Pool + Heap + EvalContext + thread-local `context` pointer that
// most Lambda gtest fixtures need. Storage is caller-owned so we don't have to
// include transpiler.hpp from this header — opaque forward declarations only.
//
// Typical use:
//   #include "../lambda/transpiler.hpp"
//   #include "../lib/test_utils.h"
//
//   class MyTest : public ::testing::Test {
//   protected:
//       Pool* pool;
//       Heap heap;
//       EvalContext ctx;
//       void SetUp() override    { tu_setup_runtime(&pool, &heap, &ctx); }
//       void TearDown() override { tu_teardown_runtime(pool); }
//   };

#ifdef __cplusplus
// Forward decls — concrete definitions live in lambda/transpiler.hpp /
// lambda/lambda-data.hpp. Callers using these must include those headers.
struct Pool;
struct Heap;
struct EvalContext;

// Initialise log, create pool, zero out heap/ctx, wire ctx<->heap<->pool,
// set thread-local `context` to ctx, and call path_init().
// Aborts via gtest FAIL on allocation failure.
void tu_setup_runtime(Pool** out_pool, Heap* heap, EvalContext* ctx);

// Clear thread-local `context` and destroy the pool. Safe with NULL.
void tu_teardown_runtime(Pool* pool);
#endif

#endif  // LIB_TEST_UTILS_H
