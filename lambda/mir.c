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
    {"string", (fn_ptr) string},
    {"base_type", (fn_ptr) base_type},
    {"const_type", (fn_ptr) const_type},
    {"type", (fn_ptr) type},
};

void *import_resolver(const char *name) {
    // printf("resolving name: %s\n", name);
    size_t len = sizeof(func_list) / sizeof(func_obj_t);
    for (int i = 0; i < len; i++) 
        if (strcmp(func_list[i].name, name) == 0)
            return func_list[i].func;
    printf("failed to resolve native fn: %s\n", name);
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
    ops.message_file = stdout;  ops.verbose_p = 1;  ops.debug_p = 0;
    printf("compiling C code in '%s' to MIR\n", file_name);
    jit_item_t jit_ptr = {.curr = 0, .code = code, .code_size = code_size};
    if (!c2mir_compile(ctx, &ops, getc_func, &jit_ptr, file_name, NULL)) {
        printf("compiled '%s' with error!!\n", file_name);
    }
}

void print_module_item(MIR_item_t mitem) {
    switch (mitem->item_type) {
    case MIR_func_item:
        // Function mitem->addr is the address to call the function
        printf("module item func: %d %s, addr %p, call addr %p\n", 
            mitem->item_type, mitem->u.func->name, mitem->addr, mitem->u.func->call_addr);
        break;
    case MIR_proto_item:
        printf("module item proto: %d %s\n", mitem->item_type, mitem->u.proto->name);
        break;
    case MIR_import_item:
        printf("module item import: %d %s\n", mitem->item_type, mitem->u.import_id);
        break;
    case MIR_export_item:
        printf("module item export: %d %s\n", mitem->item_type, mitem->u.export_id);
        break;
    case MIR_forward_item:
        printf("module item forward: %d %s\n", mitem->item_type, mitem->u.forward_id);
        break;
    case MIR_data_item:
        printf("module item data: %d %s\n", mitem->item_type, mitem->u.data->name);
        break;
    case MIR_ref_data_item:
        printf("module item ref_data: %d %s\n", mitem->item_type, mitem->u.ref_data->name);
        break;
    case MIR_lref_data_item:
        printf("module item lref_data: %d %s\n", mitem->item_type, mitem->u.lref_data->name);
        break;
    case MIR_expr_data_item:
        printf("module item expr_data: %d %s\n", mitem->item_type, mitem->u.expr_data->name);
        break;
    case MIR_bss_item:
        printf("module item bss: %d %s\n", mitem->item_type, mitem->u.bss->name);
        break;
    }
}

// compile MIR code to native code
void* jit_gen_func(MIR_context_t ctx, char *func_name) {
    printf("finding and to load module: %s\n", func_name);
    MIR_item_t mir_func = NULL;
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        dzlog_debug("Loaded module: %p, items: %p\n", module, (void*)mitem);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            print_module_item(mitem);
            if (mitem->item_type != MIR_func_item) { continue; }
            if (strcmp(mitem->u.func->name, func_name) == 0) mir_func = mitem;
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
    void* func_ptr =  MIR_gen(ctx, mir_func);
    printf("generated fn ptr: %p\n", func_ptr);
    return func_ptr;
}

MIR_item_t find_import(MIR_context_t ctx, const char *mod_name) {
    printf("finding import module: %s, %p\n", mod_name, ctx);
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
    printf("finding function: %s, %p\n", fn_name, ctx);
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        printf("checking module: %s\n", module->name);
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            print_module_item(mitem);
            if (mitem->item_type == MIR_func_item) {
                printf("checking fn item: %s\n", mitem->u.func->name);
                if (strcmp(mitem->u.func->name, fn_name) == 0)
                    return mitem->addr;
            }
        }
    }
    return NULL;
}

void* find_data(MIR_context_t ctx, const char *data_name) {
    printf("finding data: %s, %p\n", data_name, ctx);
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list(ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        printf("checking module: %s\n", module->name);
        MIR_item_t mitem = DLIST_HEAD (MIR_item_t, module->items);
        for (; mitem != NULL; mitem = DLIST_NEXT (MIR_item_t, mitem)) {
            print_module_item(mitem);
            if (mitem->item_type == MIR_data_item) {
                printf("checking data item: %s\n", mitem->u.data->name);
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