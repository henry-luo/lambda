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
    void (*func)(void);
} func_obj_t;

func_obj_t func_list[] = {
    {"printf", (void (*)(void))printf},
    {"array_new", (void (*)(void))array_new},
    {"array_int_new", (void (*)(void))array_int_new},
    {"list", (void (*)(void))list},
    {"list_new", (void (*)(void))list_new},
    {"list_int", (void (*)(void))list_int},
    {"list_int_push", (void (*)(void))list_int_push},
    {"map_new", (void (*)(void))map_new},
    {"map_get", (void (*)(void))map_get},
    {"item_true", (void (*)(void))item_true},
    {"v2x", (void (*)(void))v2x},
    {"push_d", (void (*)(void))push_d},
    {NULL, NULL}
};

void *import_resolver(const char *name) {
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

void jit_compile(MIR_context_t ctx, const char *code, size_t code_size, char *file_name) {
    struct c2mir_options ops = {0}; // Default options
    ops.message_file = stdout;  ops.verbose_p = 1;  ops.debug_p = 0;
    printf("compiling C code in '%s' to MIR\n", file_name);
    jit_item_t jit_ptr = {.curr = 0, .code = code, .code_size = code_size};
    if (!c2mir_compile(ctx, &ops, getc_func, &jit_ptr, file_name, NULL)) {
        printf("compiled '%s' with error!!\n", file_name);
    }
}

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

    printf("generating native code...\n");
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);
    if (!mir_func) {
        printf("Failed to find function '%s'\n", func_name);
        return NULL;
    }
    printf("generating function code...\n");
    return MIR_gen(ctx, mir_func);
}

void jit_cleanup(MIR_context_t ctx) {
    // Cleanup
    MIR_gen_finish(ctx);
    c2mir_finish(ctx);
    MIR_finish(ctx);
}