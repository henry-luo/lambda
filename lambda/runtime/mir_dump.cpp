// mir_dump.cpp
// Implementation of the shared MIR artifact contract. See mir_dump.h.

#include "mir_dump.h"
#include "../../lib/log.h"
#include "../../lib/file_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool mir_dump_instrumentation_enabled(void) {
    // --no-log is the master gate: log_disable_all() sets the disabled flag and
    // clears every category, so optional MIR artifacts must stay unwritten even
    // when their env switches are set. Keeping the default-category check too
    // preserves the pre-existing behavior where a log.conf that silences the
    // default category also silences the developer MIR dump.
    if (log_is_disabled()) return false;
    return log_default_category && log_default_category->enabled;
}

const char* mir_dump_explicit_path(void) {
    if (!mir_dump_instrumentation_enabled()) return NULL;
    const char* path = getenv("LAMBDA_MIR_DUMP_PATH");
    if (!path || !path[0]) return NULL;
    return path;
}

// create the parent directory of path so callers can point the artifact at a
// per-test scratch directory that does not exist yet.
static void mir_dump_ensure_parent_dir(const char* path) {
    const char* last_sep = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_sep = p;
    }
    if (!last_sep || last_sep == path) return;
    size_t dir_len = (size_t)(last_sep - path);
    char* dir = (char*)malloc(dir_len + 1);
    if (!dir) return;
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    create_dir_recursive(dir);
    free(dir);
}

// MIR_output crashes on a label operand that was never bound, so the module is
// pre-scanned. A frontend bug must surface as a failed dump, not a segfault
// inside the dump itself.
static bool mir_dump_labels_are_bound(MIR_context_t ctx, const char** out_func_name) {
    for (MIR_module_t m = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx)); m != NULL;
         m = DLIST_NEXT(MIR_module_t, m)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, m->items); item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type != MIR_func_item) continue;
            MIR_func_t func = item->u.func;
            for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, func->insns); insn != NULL;
                 insn = DLIST_NEXT(MIR_insn_t, insn)) {
                for (size_t i = 0; i < insn->nops; i++) {
                    if (insn->ops[i].mode == MIR_OP_LABEL && insn->ops[i].u.label == NULL) {
                        if (out_func_name) *out_func_name = func->name;
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

bool mir_dump_write_context(MIR_context_t ctx, const char* path, bool required) {
    if (!ctx || !path || !path[0]) return false;

    const char* bad_func = NULL;
    if (!mir_dump_labels_are_bound(ctx, &bad_func)) {
        // an unbound label means the frontend produced malformed MIR; say so and
        // leave no artifact rather than writing a partial file a test could read.
        if (required) {
            log_error("mir dump: NULL label in function '%s'; refusing to write '%s'",
                bad_func ? bad_func : "<unknown>", path);
        } else {
            log_warn("mir dump: NULL label in function '%s'; skipping dump to '%s'",
                bad_func ? bad_func : "<unknown>", path);
        }
        return false;
    }

    mir_dump_ensure_parent_dir(path);
    // remove first so an open/write failure cannot leave an earlier run's dump
    // in place for a reader to mistake for this compilation's output.
    remove(path);

    FILE* out = fopen(path, "w");
    if (!out) {
        if (required) {
            log_error("mir dump: failed to open '%s' for writing", path);
        } else {
            log_warn("mir dump: failed to open '%s' for writing", path);
        }
        return false;
    }
    MIR_output(ctx, out);
    bool ok = (ferror(out) == 0);
    if (fclose(out) != 0) ok = false;
    if (!ok) {
        if (required) {
            log_error("mir dump: failed to write '%s'", path);
        } else {
            log_warn("mir dump: failed to write '%s'", path);
        }
        remove(path);
        return false;
    }
    return true;
}

void mir_dump_finalized(MIR_context_t ctx, const char* default_path, bool default_enabled) {
    if (!ctx || !mir_dump_instrumentation_enabled()) return;

    const char* explicit_path = mir_dump_explicit_path();
    if (explicit_path) {
        // the caller asked for this exact artifact, so a failure is an error and
        // the shared developer default is left untouched.
        mir_dump_write_context(ctx, explicit_path, true);
        return;
    }
    if (default_enabled && default_path && default_path[0]) {
        mir_dump_write_context(ctx, default_path, false);
    }
}
