#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>  // for va_list
#include "../lib/log.h"
#include "mir.h"
#include "mir-gen.h"
#include "c2mir.h"
#include "lambda.h"

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
    {"list_get", (fn_ptr) list_get},
    {"list_end", (fn_ptr) list_end},
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
    {"fn_substring", (fn_ptr) fn_substring},
    {"fn_contains", (fn_ptr) fn_contains},
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
    {"to_fn", (fn_ptr) to_fn},
    {"fn_is", (fn_ptr) fn_is},
    {"fn_in", (fn_ptr) fn_in},
    {"fn_to", (fn_ptr) fn_to},
    {"base_type", (fn_ptr) base_type},
    {"const_type", (fn_ptr) const_type},
    {"fn_string", (fn_ptr) fn_string},
    {"fn_type", (fn_ptr) fn_type},
    {"fn_input1", (fn_ptr) fn_input1},
    {"fn_input2", (fn_ptr) fn_input2},
    {"fn_format1", (fn_ptr) fn_format1},
    {"fn_format2", (fn_ptr) fn_format2},
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

MIR_context_t jit_init() {
    MIR_context_t ctx = MIR_init();
    c2mir_init(ctx);
    MIR_gen_init(ctx); // init the JIT generator
    MIR_gen_set_optimize_level(ctx, 1); // set optimization level (0-3)
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
