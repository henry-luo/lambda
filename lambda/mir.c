#include <stdio.h>
#include <string.h>
#include "mir.h"
#include "mir-gen.h"
#include "c2mir.h"  // Required for using c2mir as a library

typedef struct jit_item {
    char *code;
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

func_obj_t func_list[4] = {
    {"printf", (void (*)(void))printf},
    {"puts", (void (*)(void))puts},
    {NULL, NULL}
};

void *import_resolver(const char *name) {
    size_t len = sizeof(func_list) / sizeof(func_obj_t);
    for (int i = 0; i < len; i++) 
        if (!strcmp(func_list[i].name, name))
            return func_list[i].func;
    return NULL;
}

int main(void) {
    // Step 1: Initialize MIR context
    MIR_context_t ctx = MIR_init();

    // Step 2: Initialize C to MIR
    c2mir_init(ctx);

    // Step 3: Define a small C program in memory        
    jit_item_t *jit_ptr = (jit_item_t *)malloc(sizeof(jit_item_t));
    jit_ptr->curr = 0;
    jit_ptr->code = 
        "int add(int a,int b) {                  \
            int c = a + b;                       \
            return a + b;                        \
        }";
    jit_ptr->code_size = strlen(jit_ptr->code);
    
    // Step 4: Compile C code to MIR
    struct c2mir_options ops = {0}; // Default options
    ops.verbose_p = 1; // Set to 1 for verbose output
    ops.module_num = 1; // module index
    printf("Compiling C code to MIR...\n");
    if (!c2mir_compile(ctx, &ops, getc_func, jit_ptr, "add.c", NULL)) {
        perror("Compile failure");
        exit(EXIT_FAILURE);
    }
    
    printf("Compilation to MIR successful!\n");

    printf("Generating function pointer...\n");
    MIR_item_t mir_func = NULL;
    for (MIR_module_t module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx)); module != NULL;
        module = DLIST_NEXT (MIR_module_t, module)) {
        printf("Loaded module: %p, items: %p\n", module, module->items);
        for (MIR_item_t func = DLIST_HEAD (MIR_item_t, module->items); func != NULL;
            func = DLIST_NEXT (MIR_item_t, func)) {
            printf("got func: %p\n", func);
            if (func->item_type != MIR_func_item) continue;
            printf("got func: %s\n", func->u.func->name);
            if (strcmp (func->u.func->name, "add") == 0) mir_func = func;
        }
        MIR_load_module (ctx, module);
    }

    // Load the generated MIR module
    MIR_gen_init(ctx);
    // Initialize the JIT generator
    // MIR_gen_set_optimize_level(ctx, 1); // Optional: Set optimization level (0-3)  
    MIR_link(ctx, MIR_set_gen_interface, import_resolver);  
    typedef int (*add_ptr_t)(int, int);
    printf("Generating function code...\n");
    add_ptr_t add = mir_func ? MIR_gen(ctx, mir_func) : NULL;
    if (!add) {
        fprintf(stderr, "Failed to generate function pointer\n");
        exit(EXIT_FAILURE);
    }
    // Get the generated function pointer and execute it
    printf("Executing function...\n");
    int result = add(3, 4);
    printf("Add returned: %d\n", result);

    // Cleanup
    c2mir_finish(ctx);
    MIR_gen_finish(ctx);
    MIR_finish(ctx);
    return 0;
}

// zig cc -o mir.exe mir.c /usr/local/lib/libmir.a -I/usr/local/include