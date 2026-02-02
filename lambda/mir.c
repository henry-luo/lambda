#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>  // for va_list
#include "../lib/log.h"
#include "../lib/stringbuf.h"  // for StringBuf functions
#include "mir.h"
#include "mir-gen.h"
#include "c2mir.h"
#include "lambda.h"
#include "lambda-error.h"
#include "js/js_runtime.h"

// Stack overflow protection functions
extern void lambda_stack_overflow_error(const char* func_name);

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

typedef struct {
    char *name;
    fn_ptr func;
} func_obj_t;

func_obj_t func_list[] = {
    // C library functions
    {"memset", (fn_ptr) memset},
    // Stack overflow protection
    {"lambda_stack_overflow_error", (fn_ptr) lambda_stack_overflow_error},
    // {"printf", (fn_ptr) printf}, // printf does not work
    {"array", (fn_ptr) array},
    {"array_int", (fn_ptr) array_int},
    {"array_int64", (fn_ptr) array_int64},
    {"array_float", (fn_ptr) array_float},
    {"array_fill", (fn_ptr) array_fill},
    {"array_int_fill", (fn_ptr) array_int_fill},
    {"array_int64_fill", (fn_ptr) array_int64_fill},
    {"array_float_fill", (fn_ptr) array_float_fill},
    {"array_get", (fn_ptr) array_get},
    {"array_int_get", (fn_ptr) array_int_get},
    {"array_int64_get", (fn_ptr) array_int64_get},
    {"array_float_get", (fn_ptr) array_float_get},
    {"list", (fn_ptr) list},
    {"list_fill", (fn_ptr) list_fill},
    {"list_push", (fn_ptr) list_push},
    {"list_push_spread", (fn_ptr) list_push_spread},
    {"list_get", (fn_ptr) list_get},
    {"list_end", (fn_ptr) list_end},
    {"array_spreadable", (fn_ptr) array_spreadable},
    {"array_push", (fn_ptr) array_push},
    {"array_push_spread", (fn_ptr) array_push_spread},
    {"array_end", (fn_ptr) array_end},
    {"frame_end", (fn_ptr) frame_end},
    {"map", (fn_ptr) map},
    {"map_fill", (fn_ptr) map_fill},
    {"map_get", (fn_ptr) map_get},
    {"elmt", (fn_ptr) elmt},
    {"elmt_fill", (fn_ptr) elmt_fill},
    {"elmt_get", (fn_ptr) elmt_get},
    {"is_truthy", (fn_ptr) is_truthy},
    {"v2it", (fn_ptr) v2it},
    {"push_d", (fn_ptr) push_d},
    {"push_l", (fn_ptr) push_l},
    {"push_k", (fn_ptr) push_k},
    {"push_c", (fn_ptr) push_c},
    {"item_keys", (fn_ptr) item_keys},
    {"item_attr", (fn_ptr) item_attr},
    {"item_type_id", (fn_ptr) item_type_id},
    {"item_at", (fn_ptr) item_at},

    {"fn_int", (fn_ptr) fn_int},
    {"fn_int64", (fn_ptr) fn_int64},
    {"fn_add", (fn_ptr) fn_add},
    {"fn_sub", (fn_ptr) fn_sub},
    {"fn_mul", (fn_ptr) fn_mul},
    {"fn_div", (fn_ptr) fn_div},
    {"fn_idiv", (fn_ptr) fn_idiv},
    {"fn_mod", (fn_ptr) fn_mod},
    {"fn_pow", (fn_ptr) fn_pow},
    {"fn_abs", (fn_ptr) fn_abs},
    // pipe functions
    {"fn_pipe_map", (fn_ptr) fn_pipe_map},
    {"fn_pipe_where", (fn_ptr) fn_pipe_where},
    {"fn_pipe_call", (fn_ptr) fn_pipe_call},
    {"fn_round", (fn_ptr) fn_round},
    {"fn_floor", (fn_ptr) fn_floor},
    {"fn_ceil", (fn_ptr) fn_ceil},
    {"fn_min1", (fn_ptr) fn_min1},
    {"fn_min2", (fn_ptr) fn_min2},
    {"fn_max1", (fn_ptr) fn_max1},
    {"fn_max2", (fn_ptr) fn_max2},
    {"fn_sum", (fn_ptr) fn_sum},
    {"fn_avg", (fn_ptr) fn_avg},
    {"fn_pos", (fn_ptr) fn_pos},
    {"fn_neg", (fn_ptr) fn_neg},
    // vector functions
    {"fn_prod", (fn_ptr) fn_prod},
    {"fn_cumsum", (fn_ptr) fn_cumsum},
    {"fn_cumprod", (fn_ptr) fn_cumprod},
    {"fn_argmin", (fn_ptr) fn_argmin},
    {"fn_argmax", (fn_ptr) fn_argmax},
    {"fn_fill", (fn_ptr) fn_fill},
    {"fn_dot", (fn_ptr) fn_dot},
    {"fn_norm", (fn_ptr) fn_norm},
    // statistical functions
    {"fn_mean", (fn_ptr) fn_mean},
    {"fn_median", (fn_ptr) fn_median},
    {"fn_variance", (fn_ptr) fn_variance},
    {"fn_deviation", (fn_ptr) fn_deviation},
    // element-wise math functions
    {"fn_sqrt", (fn_ptr) fn_sqrt},
    {"fn_log", (fn_ptr) fn_log},
    {"fn_log10", (fn_ptr) fn_log10},
    {"fn_exp", (fn_ptr) fn_exp},
    {"fn_sin", (fn_ptr) fn_sin},
    {"fn_cos", (fn_ptr) fn_cos},
    {"fn_tan", (fn_ptr) fn_tan},
    {"fn_sign", (fn_ptr) fn_sign},
    // vector manipulation functions
    {"fn_reverse", (fn_ptr) fn_reverse},
    {"fn_sort1", (fn_ptr) fn_sort1},
    {"fn_sort2", (fn_ptr) fn_sort2},
    {"fn_unique", (fn_ptr) fn_unique},
    {"fn_concat", (fn_ptr) fn_concat},
    {"fn_take", (fn_ptr) fn_take},
    {"fn_drop", (fn_ptr) fn_drop},
    {"fn_slice", (fn_ptr) fn_slice},
    {"fn_zip", (fn_ptr) fn_zip},
    {"fn_range3", (fn_ptr) fn_range3},
    {"fn_quantile", (fn_ptr) fn_quantile},
    {"fn_strcat", (fn_ptr) fn_strcat},
    {"fn_normalize", (fn_ptr) fn_normalize},
    {"fn_normalize1", (fn_ptr) fn_normalize1},
    {"fn_normalize2", (fn_ptr) fn_normalize},  // 2-arg version
    {"fn_substring", (fn_ptr) fn_substring},
    {"fn_contains", (fn_ptr) fn_contains},
    // string functions
    {"fn_starts_with", (fn_ptr) fn_starts_with},
    {"fn_ends_with", (fn_ptr) fn_ends_with},
    {"fn_index_of", (fn_ptr) fn_index_of},
    {"fn_last_index_of", (fn_ptr) fn_last_index_of},
    {"fn_trim", (fn_ptr) fn_trim},
    {"fn_trim_start", (fn_ptr) fn_trim_start},
    {"fn_trim_end", (fn_ptr) fn_trim_end},
    {"fn_split", (fn_ptr) fn_split},
    {"fn_str_join", (fn_ptr) fn_str_join},
    {"fn_replace", (fn_ptr) fn_replace},
    {"fn_eq", (fn_ptr) fn_eq},
    {"fn_ne", (fn_ptr) fn_ne},
    {"fn_lt", (fn_ptr) fn_lt},
    {"fn_gt", (fn_ptr) fn_gt},
    {"fn_le", (fn_ptr) fn_le},
    {"fn_ge", (fn_ptr) fn_ge},
    {"fn_not", (fn_ptr) fn_not},
    {"fn_and", (fn_ptr) fn_and},
    {"fn_or", (fn_ptr) fn_or},
    {"op_and", (fn_ptr) op_and},
    {"op_or", (fn_ptr) op_or},
    {"it2l", (fn_ptr) it2l},
    {"it2d", (fn_ptr) it2d},
    {"it2i", (fn_ptr) it2i},
    {"it2s", (fn_ptr) it2s},
    {"to_fn", (fn_ptr) to_fn},
    {"to_fn_n", (fn_ptr) to_fn_n},
    {"to_fn_named", (fn_ptr) to_fn_named},
    {"to_closure", (fn_ptr) to_closure},
    {"to_closure_named", (fn_ptr) to_closure_named},
    {"heap_calloc", (fn_ptr) heap_calloc},
    {"heap_create_name", (fn_ptr) heap_create_name},
    {"fn_call", (fn_ptr) fn_call},
    {"fn_call0", (fn_ptr) fn_call0},
    {"fn_call1", (fn_ptr) fn_call1},
    {"fn_call2", (fn_ptr) fn_call2},
    {"fn_call3", (fn_ptr) fn_call3},
    {"fn_is", (fn_ptr) fn_is},
    {"fn_in", (fn_ptr) fn_in},
    {"fn_to", (fn_ptr) fn_to},
    {"base_type", (fn_ptr) base_type},
    {"const_type", (fn_ptr) const_type},
    {"const_pattern", (fn_ptr) const_pattern},
    {"fn_string", (fn_ptr) fn_string},
    {"fn_type", (fn_ptr) fn_type},
    {"fn_input1", (fn_ptr) fn_input1},
    {"fn_input2", (fn_ptr) fn_input2},
    {"fn_format1", (fn_ptr) fn_format1},
    {"fn_format2", (fn_ptr) fn_format2},
    {"fn_error", (fn_ptr) fn_error},
    {"fn_datetime", (fn_ptr) fn_datetime},
    {"fn_index", (fn_ptr) fn_index},
    {"fn_member", (fn_ptr) fn_member},
    {"fn_len", (fn_ptr) fn_len},
    {"fn_join", (fn_ptr) fn_join},
    // variadic parameter access
    {"set_vargs", (fn_ptr) set_vargs},
    {"fn_varg0", (fn_ptr) fn_varg0},
    {"fn_varg1", (fn_ptr) fn_varg1},
    // procedures
    {"pn_print", (fn_ptr) pn_print},
    {"pn_cmd", (fn_ptr) pn_cmd},
    {"pn_fetch", (fn_ptr) pn_fetch},
    {"pn_output2", (fn_ptr) pn_output2},
    {"pn_output3", (fn_ptr) pn_output3},
    // shared runtime context pointer
    {"_lambda_rt", (fn_ptr) &_lambda_rt},
    
    // JavaScript runtime functions
    {"js_to_number", (fn_ptr) js_to_number},
    {"js_to_string", (fn_ptr) js_to_string},
    {"js_to_boolean", (fn_ptr) js_to_boolean},
    {"js_is_truthy", (fn_ptr) js_is_truthy},
    {"js_add", (fn_ptr) js_add},
    {"js_subtract", (fn_ptr) js_subtract},
    {"js_multiply", (fn_ptr) js_multiply},
    {"js_divide", (fn_ptr) js_divide},
    {"js_modulo", (fn_ptr) js_modulo},
    {"js_power", (fn_ptr) js_power},
    {"js_equal", (fn_ptr) js_equal},
    {"js_not_equal", (fn_ptr) js_not_equal},
    {"js_strict_equal", (fn_ptr) js_strict_equal},
    {"js_strict_not_equal", (fn_ptr) js_strict_not_equal},
    {"js_less_than", (fn_ptr) js_less_than},
    {"js_less_equal", (fn_ptr) js_less_equal},
    {"js_greater_than", (fn_ptr) js_greater_than},
    {"js_greater_equal", (fn_ptr) js_greater_equal},
    {"js_logical_and", (fn_ptr) js_logical_and},
    {"js_logical_or", (fn_ptr) js_logical_or},
    {"js_logical_not", (fn_ptr) js_logical_not},
    {"js_bitwise_and", (fn_ptr) js_bitwise_and},
    {"js_bitwise_or", (fn_ptr) js_bitwise_or},
    {"js_bitwise_xor", (fn_ptr) js_bitwise_xor},
    {"js_bitwise_not", (fn_ptr) js_bitwise_not},
    {"js_left_shift", (fn_ptr) js_left_shift},
    {"js_right_shift", (fn_ptr) js_right_shift},
    {"js_unsigned_right_shift", (fn_ptr) js_unsigned_right_shift},
    {"js_unary_plus", (fn_ptr) js_unary_plus},
    {"js_unary_minus", (fn_ptr) js_unary_minus},
    {"js_typeof", (fn_ptr) js_typeof},
    {"js_new_object", (fn_ptr) js_new_object},
    {"js_property_get", (fn_ptr) js_property_get},
    {"js_property_set", (fn_ptr) js_property_set},
    {"js_property_access", (fn_ptr) js_property_access},
    {"js_array_new", (fn_ptr) js_array_new},
    {"js_array_get", (fn_ptr) js_array_get},
    {"js_array_set", (fn_ptr) js_array_set},
    {"js_array_length", (fn_ptr) js_array_length},
    {"js_array_push", (fn_ptr) js_array_push},
    {"js_new_function", (fn_ptr) js_new_function},
    {"js_call_function", (fn_ptr) js_call_function},
    {"js_console_log", (fn_ptr) js_console_log},
    // StringBuf functions for template literals
    {"stringbuf_new", (fn_ptr) stringbuf_new},
    {"stringbuf_append_str", (fn_ptr) stringbuf_append_str},
    {"stringbuf_append_str_n", (fn_ptr) stringbuf_append_str_n},
    {"stringbuf_to_string", (fn_ptr) stringbuf_to_string},
};

void *import_resolver(const char *name) {
    log_debug("resolving name: %s", name);
    // Use explicit count since sizeof calculation may fail
    const size_t len = sizeof(func_list) / sizeof(func_obj_t);
    for (int i = 0; i < len; i++) {
        // log_debug("checking fn: %s", func_list[i].name);
        if (strcmp(func_list[i].name, name) == 0) {
            log_debug("found function: %s at %p", name, func_list[i].func);
            return func_list[i].func;
        }
    }
    log_error("failed to resolve native fn/pn: %s", name);
    return NULL;
}

MIR_context_t jit_init(unsigned int optimize_level) {
    MIR_context_t ctx = MIR_init();
    c2mir_init(ctx);
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

// compile C code to MIR
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, const char *file_name) {
    struct c2mir_options ops = {0}; // Default options
    
    // Check if we want to capture C2MIR debug messages via environment variable
    const char* debug_env = getenv("LAMBDA_C2MIR_DEBUG");
    bool enable_debug = (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0));
    
    #ifdef ENABLE_C2MIR_DEBUG
        enable_debug = true;  // Force enable if compile-time flag is set
    #endif
    enable_debug = true;  // hardcode enable for now
    
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
    c2mir_finish(ctx);
    MIR_finish(ctx);
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
#include "../lib/hashmap.h"

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
