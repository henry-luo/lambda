 #include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>  // for va_list
#include <math.h>
#include "../lib/log.h"
#include "../lib/hashmap.h"    // for O(1) import resolution
#include "mir.h"
#include "mir-gen.h"
#ifdef LAMBDA_C2MIR
#include "c2mir.h"
#endif
#include "lambda.h"
#include "lambda-error.h"
#include "sys_func_registry.h"

// Shared runtime context pointer - all JIT modules import this
// This ensures imported modules share the same runtime context as the main module
Context* _lambda_rt = NULL;

typedef struct jit_item {
    const char *code;
    size_t code_size;
    size_t curr;
} jit_item_t;

int getc_func(void *data) {
    jit_item_t *item = data;
    // printf("getc_func: %d\n", item->curr);
    return item->curr >= item->code_size ? EOF : item->code[item->curr++];
}


// ============================================================================
// O(1) Import Resolution via Hashmap
// ============================================================================
// Builds a hashmap from sys_func_defs[] + jit_runtime_imports[] at init time
// for O(1) symbol resolution instead of O(n) linear scan on every import.

static struct hashmap* func_map = NULL;

static uint64_t func_obj_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JitImport* e = (const JitImport*)item;
    return hashmap_xxhash3(e->name, strlen(e->name), seed0, seed1);
}

static int func_obj_compare(const void* a, const void* b, void* udata) {
    const JitImport* fa = (const JitImport*)a;
    const JitImport* fb = (const JitImport*)b;
    return strcmp(fa->name, fb->name);
}

static void init_func_map(void) {
    if (func_map) return;  // already initialized

    // Count total entries: sys_func_defs (with func_ptr) + jit_runtime_imports
    size_t total = 0;
    for (int i = 0; i < sys_func_def_count; i++) {
        if (sys_func_defs[i].func_ptr) total++;
        if (sys_func_defs[i].native_func_ptr) total++;
    }
    total += (size_t)jit_runtime_import_count;

    func_map = hashmap_new(sizeof(JitImport), total * 2,
        0, 0, func_obj_hash, func_obj_compare, NULL, NULL);

    // Insert sys_func_defs entries (c_func_name → func_ptr)
    for (int i = 0; i < sys_func_def_count; i++) {
        if (sys_func_defs[i].func_ptr) {
            JitImport entry = {sys_func_defs[i].c_func_name, sys_func_defs[i].func_ptr};
            hashmap_set(func_map, &entry);
        }
        if (sys_func_defs[i].native_func_ptr) {
            JitImport entry = {sys_func_defs[i].native_c_name, sys_func_defs[i].native_func_ptr};
            hashmap_set(func_map, &entry);
        }
    }

    // Insert jit_runtime_imports entries
    for (int i = 0; i < jit_runtime_import_count; i++) {
        hashmap_set(func_map, &jit_runtime_imports[i]);
    }

    log_info("func_map initialized: %zu runtime functions", total);
}

// Dynamic import table for cross-module function/variable resolution (O(1) hashmap)
// Thread-local: each compilation thread gets its own map for parallel module compilation.
static __thread struct hashmap* dynamic_import_map = NULL;

void ensure_jit_imports_initialized(void) {
    init_func_map();
}

void register_dynamic_import(const char *name, void *addr) {
    if (!dynamic_import_map) {
        dynamic_import_map = hashmap_new(sizeof(JitImport), 64, 0, 0,
            func_obj_hash, func_obj_compare, NULL, NULL);
    }
    log_debug("register dynamic import: %s -> %p", name, addr);
    JitImport entry = {(char*)name, (fn_ptr)addr};
    hashmap_set(dynamic_import_map, &entry);
}

void clear_dynamic_imports(void) {
    if (dynamic_import_map) {
        hashmap_clear(dynamic_import_map, false);
    }
}

void *import_resolver(const char *name) {
    log_debug("resolving name: %s", name);
    // Check dynamic imports first (cross-module functions/variables) — O(1) hashmap
    if (dynamic_import_map) {
        JitImport key = {.name = (char*)name, .func = NULL};
        const JitImport* found = (const JitImport*)hashmap_get(dynamic_import_map, &key);
        if (found) {
            log_debug("found dynamic import: %s at %p", name, found->func);
            return (void*)found->func;
        }
    }
    // Check static runtime function hashmap (O(1) lookup)
    JitImport key = {.name = (char*)name, .func = NULL};
    const JitImport* found = (const JitImport*)hashmap_get(func_map, &key);
    if (found) {
        log_debug("found function: %s at %p", name, found->func);
        return (void*)found->func;
    }
    log_error("failed to resolve native fn/pn: %s", name);
    return NULL;
}

MIR_context_t jit_init(unsigned int optimize_level) {
    init_func_map();  // build O(1) import resolution hashmap
    MIR_context_t ctx = MIR_init();
#ifdef LAMBDA_C2MIR
    c2mir_init(ctx);
#endif
    MIR_gen_init(ctx); // init the JIT generator
    // Level 0: Only register allocator and machine code generator (no inlining)
    // Level 1: Adds code selection (more compact/faster code)
    // Level 2: Adds CSE/GVN and constant propagation (default)
    // Level 3: Adds register renaming and loop invariant code motion
    // Note: MIR inlines CALL instructions for functions under 50 instructions at levels > 0
    log_info("MIR JIT optimization level: %u", optimize_level);
    MIR_gen_set_optimize_level(ctx, optimize_level);
    return ctx;
}

#ifdef LAMBDA_C2MIR
// compile C code to MIR
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name) {
    struct c2mir_options ops = {0}; // Default options

    // Check if we want to capture C2MIR debug messages via environment variable
    const char* debug_env = getenv("LAMBDA_C2MIR_DEBUG");
    bool enable_debug = (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0));

    #ifdef ENABLE_C2MIR_DEBUG
        enable_debug = true;  // Force enable if compile-time flag is set
    #endif

    if (enable_debug) {
        // Create a temporary file to capture C2MIR messages
        FILE* temp_log = tmpfile();
        if (temp_log) {
            ops.message_file = temp_log;
            ops.verbose_p = 1;  // Enable verbose output
            ops.debug_p = 0;    // Keep debug off to avoid too much noise
            log_debug("C2MIR debug logging enabled");
        } else {
            ops.message_file = NULL;
            ops.verbose_p = 0;
            ops.debug_p = 0;
            log_warn("Failed to create temporary file for C2MIR logging");
        }
    } else {
        ops.message_file = NULL;
        ops.verbose_p = 0;
        ops.debug_p = 0;
    }

    log_notice("Compiling C code in '%s' to MIR", file_name);
    jit_item_t jit_ptr = {.curr = 0, .code = code, .code_size = code_size};
    if (!c2mir_compile(ctx, &ops, getc_func, &jit_ptr, file_name, NULL)) {
        log_error("compiled '%s' with error!!", file_name);
    }

    // Read and log the captured C2MIR messages
    if (enable_debug && ops.message_file) {
        // Rewind to beginning of temp file
        rewind(ops.message_file);

        // Read and log each line
        char line_buffer[1024];
        while (fgets(line_buffer, sizeof(line_buffer), ops.message_file)) {
            // Remove trailing newline
            size_t len = strlen(line_buffer);
            if (len > 0 && line_buffer[len - 1] == '\n') {
                line_buffer[len - 1] = '\0';
            }

            // Log the C2MIR message (skip empty lines)
            if (strlen(line_buffer) > 0) {
                log_debug("C2MIR: %s", line_buffer);
            }
        }

        // Close the temporary file
        fclose(ops.message_file);
    }
}
#endif // LAMBDA_C2MIR

void print_module_item(MIR_item_t mitem) {
    switch (mitem->item_type) {
    case MIR_func_item:
        // Function mitem->addr is the address to call the function
        log_debug("module item func: %d %s, addr %p, call addr %p",
            mitem->item_type, mitem->u.func->name, mitem->addr, mitem->u.func->call_addr);
        break;
    case MIR_proto_item:
        log_debug("module item proto: %d %s", mitem->item_type, mitem->u.proto->name);
        break;
    case MIR_import_item:
        log_debug("module item import: %d %s", mitem->item_type, mitem->u.import_id);
        break;
    case MIR_export_item:
        log_debug("module item export: %d %s", mitem->item_type, mitem->u.export_id);
        break;
    case MIR_forward_item:
        log_debug("module item forward: %d %s", mitem->item_type, mitem->u.forward_id);
        break;
    case MIR_data_item:
        log_debug("module item data: %d %s", mitem->item_type, mitem->u.data->name);
        break;
    case MIR_ref_data_item:
        log_debug("module item ref_data: %d %s", mitem->item_type, mitem->u.ref_data->name);
        break;
    case MIR_lref_data_item:
        log_debug("module item lref_data: %d %s", mitem->item_type, mitem->u.lref_data->name);
        break;
    case MIR_expr_data_item:
        log_debug("module item expr_data: %d %s", mitem->item_type, mitem->u.expr_data->name);
        break;
    case MIR_bss_item:
        log_debug("module item bss: %d %s", mitem->item_type, mitem->u.bss->name);
        break;
    }
}

// compile MIR code to native code
void* jit_gen_func(MIR_context_t ctx, char *func_name) {
    log_debug("finding and to load module: %s", func_name);
    MIR_item_t mir_func = NULL;
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        log_info("Loaded module: %p, items: %p", module, (void*)mitem);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            print_module_item(mitem);
            if (mitem->item_type != MIR_func_item) { continue; }
            if (strcmp(mitem->u.func->name, func_name) == 0) mir_func = mitem;
        }
        MIR_load_module(ctx, module);
    }
    if (!mir_func) {
        log_error("Failed to find function '%s'", func_name);
        return NULL;
    }

    log_notice("Generating native code...");
    // link MIR code with external functions
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);
    // generate native code
    void* func_ptr =  MIR_gen(ctx, mir_func);
    log_debug("generated fn ptr: %p", func_ptr);
    return func_ptr;
}

MIR_item_t find_import(MIR_context_t ctx, const char *mod_name) {
    log_debug("finding import module:: %s, %p", mod_name, ctx);
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            if (mitem->item_type == MIR_bss_item && strcmp(mitem->u.bss->name, mod_name) == 0) {
                return mitem;
            }
        }
    }
    return NULL;
}

void* find_func(MIR_context_t ctx, const char *fn_name) {
    log_debug("finding function: %s, %p", fn_name, ctx);
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        log_debug("checking module: %s", module->name);
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            print_module_item(mitem);
            if (mitem->item_type == MIR_func_item) {
                log_debug("checking fn item: %s", mitem->u.func->name);
                if (strcmp(mitem->u.func->name, fn_name) == 0)
                    return mitem->addr;
            }
        }
    }
    return NULL;
}

void* find_data(MIR_context_t ctx, const char *data_name) {
    log_debug("finding data: %s, %p", data_name, ctx);
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        log_debug("checking module: %s", module->name);
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            print_module_item(mitem);
            if (mitem->item_type == MIR_data_item) {
                log_debug("checking data item: %s", mitem->u.data->name);
                if (strcmp(mitem->u.data->name, data_name) == 0)
                    return mitem->addr;
            }
        }
    }
    return NULL;
}

void jit_cleanup(MIR_context_t ctx) {
    // Cleanup
    MIR_gen_finish(ctx);
#ifdef LAMBDA_C2MIR
    c2mir_finish(ctx);
#endif
    MIR_finish(ctx);
}

// ============================================================================
// BSS Global Root Registration for GC
// ============================================================================
// Walk all MIR modules to find BSS items with _gvar_ prefix (module-level
// let bindings). Register their resolved addresses as GC root slots so the
// garbage collector scans them during mark phase.

extern void heap_register_gc_root(uint64_t* slot);

void register_bss_gc_roots(void* mir_ctx) {
    if (!mir_ctx) return;
    MIR_context_t ctx = (MIR_context_t)mir_ctx;
    int count = 0;

    for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
         module != NULL;
         module = DLIST_NEXT(MIR_module_t, module)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items);
             item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_bss_item && item->u.bss->name &&
                strncmp(item->u.bss->name, "_gvar_", 6) == 0 && item->addr) {
                heap_register_gc_root((uint64_t*)item->addr);
                count++;
            }
        }
    }
    log_debug("register_bss_gc_roots: registered %d BSS global roots", count);
}

// ============================================================================
// Debug Info Table for Native Stack Walking
// ============================================================================
// This code is in mir.c because it uses MIR APIs (MIR_get_module_list)
// which are only linked into the main lambda executable.
//
// The lookup_debug_info() and free_debug_info_table() functions are in
// lambda-error.cpp since they don't use MIR APIs.

#include "lambda-error.h"
// note: hashmap.h already included at top of file

// Simple dynamic array for debug info (matches struct in lambda-error.cpp)
typedef struct {
    FuncDebugInfo** items;
    size_t length;
    size_t capacity;
} DebugInfoList;

// Comparator for sorting FuncDebugInfo by address
static int compare_debug_info(const void* a, const void* b) {
    FuncDebugInfo* fa = *(FuncDebugInfo**)a;
    FuncDebugInfo* fb = *(FuncDebugInfo**)b;
    if (fa->native_addr_start < fb->native_addr_start) return -1;
    if (fa->native_addr_start > fb->native_addr_start) return 1;
    return 0;
}

// Build debug info table from MIR-compiled functions
// This collects all function addresses, sorts them, and computes boundaries
// using address ordering (next func start = current func end)
// If func_name_map is provided, it maps MIR internal names to Lambda user-friendly names
void* build_debug_info_table(void* mir_ctx, void* func_name_map) {
    if (!mir_ctx) {
        log_debug("build_debug_info_table: mir_ctx is NULL");
        return NULL;
    }

    MIR_context_t ctx = (MIR_context_t)mir_ctx;
    struct hashmap* name_map = (struct hashmap*)func_name_map;

    // Create list to hold debug info entries
    DebugInfoList* debug_list = (DebugInfoList*)malloc(sizeof(DebugInfoList));
    if (!debug_list) {
        log_error("build_debug_info_table: failed to allocate debug_list");
        return NULL;
    }
    debug_list->capacity = 64;
    debug_list->length = 0;
    debug_list->items = (FuncDebugInfo**)malloc(sizeof(FuncDebugInfo*) * debug_list->capacity);
    if (!debug_list->items) {
        log_error("build_debug_info_table: failed to allocate debug_list items");
        free(debug_list);
        return NULL;
    }

    // Iterate all modules and collect function addresses
    for (MIR_module_t module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(ctx));
         module != NULL;
         module = DLIST_NEXT(MIR_module_t, module)) {
        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items);
             item != NULL;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item && item->addr != NULL) {
                FuncDebugInfo* info = (FuncDebugInfo*)calloc(1, sizeof(FuncDebugInfo));
                if (!info) continue;

                // Use machine_code if available, otherwise addr
                void* code_addr = item->u.func->machine_code ? item->u.func->machine_code : item->addr;
                info->native_addr_start = code_addr;
                info->native_addr_end = NULL;  // computed later

                // Look up Lambda name from map, fall back to MIR name
                const char* mir_name = item->u.func->name;
                const char* lambda_name = mir_name;  // default to MIR name
                if (name_map) {
                    // map stores char*[2] = {mir_name, lambda_name}
                    const char** found = (const char**)hashmap_get(name_map, &mir_name);
                    if (found) {
                        lambda_name = found[1];  // second element is Lambda name
                        log_debug("build_debug_info_table: mapped MIR name '%s' -> Lambda name '%s'", mir_name, lambda_name);
                    }
                }
                info->lambda_func_name = lambda_name;
                info->source_file = NULL;  // could be set from AST if available
                info->source_line = 0;

                log_debug("build_debug_info_table: func '%s' addr=%p machine_code=%p call_addr=%p",
                          lambda_name, item->addr, item->u.func->machine_code, item->u.func->call_addr);

                // grow list if needed
                if (debug_list->length >= debug_list->capacity) {
                    size_t new_cap = debug_list->capacity * 2;
                    FuncDebugInfo** new_items = (FuncDebugInfo**)realloc(debug_list->items, sizeof(FuncDebugInfo*) * new_cap);
                    if (!new_items) {
                        free(info);
                        continue;
                    }
                    debug_list->items = new_items;
                    debug_list->capacity = new_cap;
                }
                debug_list->items[debug_list->length++] = info;

                log_debug("build_debug_info_table: added func '%s' at %p",
                          info->lambda_func_name, info->native_addr_start);
            }
        }
    }

    if (debug_list->length == 0) {
        log_debug("build_debug_info_table: no functions found");
        free(debug_list->items);
        free(debug_list);
        return NULL;
    }

    // Sort by address
    qsort(debug_list->items, debug_list->length, sizeof(FuncDebugInfo*), compare_debug_info);

    // Compute end addresses using next function's start
    for (size_t i = 0; i < debug_list->length; i++) {
        FuncDebugInfo* info = debug_list->items[i];
        if (i + 1 < debug_list->length) {
            FuncDebugInfo* next = debug_list->items[i + 1];
            info->native_addr_end = next->native_addr_start;
        } else {
            // Last function: use conservative size (64KB should cover any function)
            info->native_addr_end = (char*)info->native_addr_start + 65536;
        }
        log_debug("build_debug_info_table: func '%s' range [%p, %p)",
                  info->lambda_func_name, info->native_addr_start, info->native_addr_end);
    }

    log_info("build_debug_info_table: built table with %zu functions", debug_list->length);
    return debug_list;
}
