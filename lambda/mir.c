#include <stdio.h>
#include <string.h>
#include <zlog.h> 
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
    {"printf", (fn_ptr) printf},
    {"pow", (fn_ptr) pow},
    {"array", (fn_ptr) array},
    {"array_fill", (fn_ptr) array_fill},
    {"array_get", (fn_ptr) array_get},
    {"array_long_new", (fn_ptr) array_long_new},
    {"list", (fn_ptr) list},
    {"list_fill", (fn_ptr) list_fill},
    {"list_push", (fn_ptr) list_push},
    {"list_get", (fn_ptr) list_get},
    {"map", (fn_ptr) map},
    {"map_fill", (fn_ptr) map_fill},
    {"map_get", (fn_ptr) map_get},
    {"elmt", (fn_ptr) elmt},
    {"elmt_fill", (fn_ptr) elmt_fill},
    {"item_true", (fn_ptr) item_true},
    {"v2it", (fn_ptr) v2it},
    {"push_d", (fn_ptr) push_d},
    {"push_l", (fn_ptr) push_l},
    {"str_cat", (fn_ptr) str_cat},
    {"add", (fn_ptr) add},
    {"it2l", (fn_ptr) it2l},
    {"it2d", (fn_ptr) it2d},
    {"to_fn", (fn_ptr) to_fn},
    {"is", (fn_ptr) is},
    {"in", (fn_ptr) in},

    {"type_null", (fn_ptr) type_null},
    {"type_bool", (fn_ptr) type_bool},
    {"type_int", (fn_ptr) type_int},
    {"type_float", (fn_ptr) type_float},
    {"type_number", (fn_ptr) type_number},
    {"type_string", (fn_ptr) type_string},
    {"type_binary", (fn_ptr) type_binary},
    {"type_symbol", (fn_ptr) type_symbol},
    {"type_dtime", (fn_ptr) type_dtime},    
    {"type_list", (fn_ptr) type_list},
    {"type_map", (fn_ptr) type_map},
    {"type_elmt", (fn_ptr) type_elmt},
    {"type_array", (fn_ptr) type_array},
    {"type_func", (fn_ptr) type_func},
    {"type_type", (fn_ptr) type_type},
    {"type_any", (fn_ptr) type_any},
    {"type_error", (fn_ptr) type_error},

    {"string", (fn_ptr) string},
    {"type", (fn_ptr) type},
};

void *import_resolver(const char *name) {
    printf("resolving name: %s\n", name);
    size_t len = sizeof(func_list) / sizeof(func_obj_t);
    for (int i = 0; i < len; i++) 
        if (strcmp(func_list[i].name, name) == 0)
            return func_list[i].func;
    printf("failed to resolve: %s\n", name);
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
void jit_compile_to_mir(MIR_context_t ctx, const char *code, size_t code_size, char *file_name) {
    struct c2mir_options ops = {0}; // Default options
    ops.message_file = stdout;  ops.verbose_p = 1;  ops.debug_p = 0;
    printf("compiling C code in '%s' to MIR\n", file_name);
    jit_item_t jit_ptr = {.curr = 0, .code = code, .code_size = code_size};
    if (!c2mir_compile(ctx, &ops, getc_func, &jit_ptr, file_name, NULL)) {
        printf("compiled '%s' with error!!\n", file_name);
    }
}

// compile MIR code to native code
void* jit_gen_func(MIR_context_t ctx, char *func_name) {
    printf("loading modules\n");
    MIR_item_t mir_func = NULL;
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        MIR_item_t func = DLIST_HEAD (MIR_item_t, module->items);
        dzlog_debug("Loaded module: %p, items: %p\n", module, (void*)func);
        for (; func != NULL; func = DLIST_NEXT (MIR_item_t, func)) {
            if (func->item_type != MIR_func_item) continue;
            dzlog_debug("got func: %s\n", func->u.func->name);
            if (strcmp(func->u.func->name, func_name) == 0) mir_func = func;
        }
        MIR_load_module(ctx, module);
    }
    if (!mir_func) {
        printf("Failed to find function '%s'\n", func_name);
        return NULL;
    }

    printf("generating native code...\n");
    // link MIR code with external functions
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);
    // generate native code
    return MIR_gen(ctx, mir_func);
}

void jit_cleanup(MIR_context_t ctx) {
    // Cleanup
    MIR_gen_finish(ctx);
    c2mir_finish(ctx);
    MIR_finish(ctx);
}