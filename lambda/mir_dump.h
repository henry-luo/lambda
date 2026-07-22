// mir_dump.h
// Canonical MIR artifact contract shared by every frontend (MT2).
//
// One place decides whether optional MIR instrumentation runs, where the
// canonical artifact goes, and how it is written safely, so the --no-log
// master gate and the LAMBDA_MIR_DUMP_PATH contract cannot drift per
// frontend. See vibe/Lambda_Design_MIR_Emission_Test.md MT2.
//
// Contract:
//  - --no-log suppresses every optional MIR dump. No file is opened or
//    truncated, in any build. Env switches never override it.
//  - LAMBDA_MIR_DUMP_PATH names the canonical *finalized* artifact: the MIR
//    after MIR_finish_func/MIR_finish_module, before link/generation. It is
//    honored in release builds too, and a write failure is an error rather
//    than a warning that would leave a stale artifact in place.
//  - Frontend default paths (temp/mir_dump.txt and friends) stay
//    developer-only conveniences and are never consumed by tests.

#ifndef LAMBDA_MIR_DUMP_H
#define LAMBDA_MIR_DUMP_H

#include <stdbool.h>
#include <mir.h>

#ifdef __cplusplus
extern "C" {
#endif

// true when optional MIR dump instrumentation may run at all; false under
// --no-log. Callers check this before doing any dump-related work.
bool mir_dump_instrumentation_enabled(void);

// the explicit canonical artifact path, or NULL when LAMBDA_MIR_DUMP_PATH is
// unset/empty or instrumentation is disabled.
const char* mir_dump_explicit_path(void);

// write the whole MIR context as text to path. Skips the dump when the module
// still holds NULL labels, which would crash MIR_output. When required, an
// open/write failure is reported through log_error instead of log_warn.
// Returns true only when a complete dump was written.
bool mir_dump_write_context(MIR_context_t ctx, const char* path, bool required);

// emit the canonical finalized artifact for a frontend. Call right after
// MIR_finish_func/MIR_finish_module. Writes LAMBDA_MIR_DUMP_PATH when set;
// otherwise writes default_path only when default_enabled describes the
// frontend's developer-only fallback (debug build, JS_MIR_DUMP=1, ...).
void mir_dump_finalized(MIR_context_t ctx, const char* default_path, bool default_enabled);

#ifdef __cplusplus
}
#endif

#endif  // LAMBDA_MIR_DUMP_H
