// module_registry.cpp — Unified cross-language module registry implementation
#include "lambda-data.hpp"  // must be first — wraps lambda.h in extern "C" block
#include "module_registry.h"
#include "ast.hpp"
#include "transpiler.hpp"
#include "shape_pool.hpp"
#include "../lib/hashmap.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"

#include <string.h>
#include <stdlib.h>

extern "C" Item js_property_get(Item object, Item key);
extern "C" Item js_new_object();
extern "C" Item js_property_set(Item object, Item key, Item value);
extern "C" int js_function_get_arity(Item fn_item);

// C++ linkage — defined in build_ast.cpp
bool is_sys_func_name(const char* name, int name_len);

// =============================================================================
// Registry hashmap
// =============================================================================

static struct hashmap* registry_map = NULL;

// hashmap entry for module descriptors
typedef struct {
    const char* path;       // key (not owned — points to ModuleDescriptor.path)
    ModuleDescriptor* desc; // value (owned)
} RegistryEntry;

static uint64_t registry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const RegistryEntry* entry = (const RegistryEntry*)item;
    return hashmap_sip(entry->path, strlen(entry->path), seed0, seed1);
}

static int registry_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    const RegistryEntry* ea = (const RegistryEntry*)a;
    const RegistryEntry* eb = (const RegistryEntry*)b;
    return strcmp(ea->path, eb->path);
}

void module_registry_init(void) {
    if (registry_map) return;
    registry_map = hashmap_new(sizeof(RegistryEntry), 32, 0, 0,
        registry_hash, registry_compare, NULL, NULL);
}

void module_registry_cleanup(void) {
    if (!registry_map) return;
    // free all descriptors
    size_t iter = 0;
    void* item;
    while (hashmap_iter(registry_map, &iter, &item)) {
        RegistryEntry* entry = (RegistryEntry*)item;
        if (entry->desc) {
            mem_free((void*)entry->desc->path);
            mem_free(entry->desc);
        }
    }
    hashmap_free(registry_map);
    registry_map = NULL;
}

void module_register(const char* path, const char* lang, Item namespace_obj, void* mir_ctx) {
    if (!path) return;
    if (!registry_map) module_registry_init();

    // check if already registered — update if so
    RegistryEntry lookup = { .path = path, .desc = NULL };
    const RegistryEntry* existing = (const RegistryEntry*)hashmap_get(registry_map, &lookup);
    if (existing && existing->desc) {
        existing->desc->namespace_obj = namespace_obj;
        existing->desc->mir_ctx = mir_ctx;
        existing->desc->initialized = true;
        existing->desc->loading = false;
        log_debug("module_registry: updated module '%s' (lang=%s)", path, lang);
        return;
    }

    ModuleDescriptor* desc = (ModuleDescriptor*)mem_calloc(1, sizeof(ModuleDescriptor), MEM_CAT_SYSTEM);
    desc->path = mem_strdup(path, MEM_CAT_SYSTEM);
    desc->source_lang = lang;  // static string, not owned
    desc->namespace_obj = namespace_obj;
    desc->mir_ctx = mir_ctx;
    desc->initialized = true;
    desc->loading = false;

    RegistryEntry entry = { .path = desc->path, .desc = desc };
    hashmap_set(registry_map, &entry);
    log_info("module_registry: registered '%s' (lang=%s)", path, lang);
}

ModuleDescriptor* module_get(const char* path) {
    if (!path || !registry_map) return NULL;
    RegistryEntry lookup = { .path = path, .desc = NULL };
    const RegistryEntry* found = (const RegistryEntry*)hashmap_get(registry_map, &lookup);
    return found ? found->desc : NULL;
}

bool module_is_loaded(const char* path) {
    ModuleDescriptor* desc = module_get(path);
    return desc && desc->initialized;
}

ModuleDescriptor* module_register_loading(const char* path, const char* lang) {
    if (!path) return NULL;
    if (!registry_map) module_registry_init();

    // check if already registered
    RegistryEntry lookup = { .path = path, .desc = NULL };
    const RegistryEntry* existing = (const RegistryEntry*)hashmap_get(registry_map, &lookup);
    if (existing && existing->desc) {
        existing->desc->loading = true;
        return existing->desc;
    }

    ModuleDescriptor* desc = (ModuleDescriptor*)mem_calloc(1, sizeof(ModuleDescriptor), MEM_CAT_SYSTEM);
    desc->path = mem_strdup(path, MEM_CAT_SYSTEM);
    desc->source_lang = lang;
    desc->namespace_obj = js_new_object();
    desc->mir_ctx = NULL;
    desc->initialized = false;
    desc->loading = true;

    RegistryEntry entry = { .path = desc->path, .desc = desc };
    hashmap_set(registry_map, &entry);
    log_info("module_registry: marked '%s' as loading (lang=%s)", path, lang);
    return desc;
}

bool module_is_loading(const char* path) {
    ModuleDescriptor* desc = module_get(path);
    return desc && desc->loading && !desc->initialized;
}

// =============================================================================
// Lambda namespace builder
// =============================================================================

Item module_build_lambda_namespace(void* script_ptr) {
    Script* script = (Script*)script_ptr;
    if (!script || !script->ast_root) return ItemNull;

    AstScript* ast = (AstScript*)script->ast_root;
    Item ns = js_new_object();

    AstNode* node = ast->child;
    while (node) {
        if (node->node_type == AST_NODE_CONTENT) {
            node = ((AstListNode*)node)->item;
            continue;
        }

        if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) {
            AstFuncNode* fn_node = (AstFuncNode*)node;
            TypeFunc* fn_type = (TypeFunc*)fn_node->type;
            if (fn_type && fn_type->is_public) {
                // prefer boxed wrapper for cross-language compatibility
                StrBuf* name_buf = strbuf_new();
                void* func_ptr = NULL;

                if (node->node_type != AST_NODE_PROC && needs_fn_call_wrapper(fn_node)) {
                    write_fn_name_ex(name_buf, fn_node, NULL, "_b");
                    func_ptr = find_func((MIR_context_t)script->jit_context, name_buf->str);
                }
                if (!func_ptr) {
                    // fall back to direct variant
                    strbuf_reset(name_buf);
                    write_fn_name(name_buf, fn_node, NULL);
                    func_ptr = find_func((MIR_context_t)script->jit_context, name_buf->str);
                }
                strbuf_free(name_buf);

                if (func_ptr) {
                    int arity = fn_type->param_count;
                    const char* export_name = fn_node->name ? fn_node->name->chars : NULL;
                    if (export_name && module_name_collides_with_sys(export_name, (int)strlen(export_name))) {
                        log_error("module_registry: pub fn '%s' shadows system function", export_name);
                    }
                    Function* fn = to_fn_named((fn_ptr)func_ptr, arity, export_name);
                    Item key = {.item = s2it(heap_create_name(export_name))};
                    Item val = {.function = fn};
                    js_property_set(ns, key, val);
                    log_debug("module_registry: lambda ns export fn '%s' arity=%d", export_name, arity);
                }
            }
        }
        else if (node->node_type == AST_NODE_PUB_STAM) {
            // pub var — the value is initialized when module main runs,
            // so we need to read it from the module struct at registration time.
            // For now, we skip pub vars in the namespace — they require
            // running the module first and reading from the BSS struct.
            // This will be addressed when we add live binding support.
        }

        node = node->next;
    }

    return ns;
}

// =============================================================================
// Create a synthetic Script from a JS namespace for Lambda→JS import
// =============================================================================

void* create_js_import_script(const char* resolved_path, Item namespace_obj, void* runtime_ptr) {
    Runtime* runtime = (Runtime*)runtime_ptr;
    TypeId ns_type = get_type_id(namespace_obj);
    if (ns_type != LMD_TYPE_MAP) {
        log_error("module_registry: JS namespace is not a map (type=%d)", ns_type);
        return NULL;
    }

    // Create a Script with its own pool
    Pool* pool = pool_create();
    Script* script = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    script->pool = pool;
    script->reference = mem_strdup(resolved_path, MEM_CAT_SYSTEM);
    script->is_main = false;
    script->is_loading = false;
    script->const_list = arraylist_new(4);
    script->type_list = arraylist_new(4);

    // Register in the runtime's scripts list
    arraylist_append(runtime->scripts, script);
    script->index = runtime->scripts->length - 1;

    // Create synthetic AstScript root
    AstScript* ast = (AstScript*)pool_calloc(pool, sizeof(AstScript));
    ast->node_type = AST_SCRIPT;
    ast->type = &TYPE_NULL;
    ast->child = NULL;
    script->ast_root = (AstNode*)ast;

    // Walk namespace map shape to discover exports
    Map* map = namespace_obj.map;
    TypeMap* type_map = (TypeMap*)map->type;
    if (!type_map || !type_map->shape) {
        log_debug("module_registry: JS namespace has no shape entries");
        return script;
    }

    AstNode* tail = NULL;
    int synthetic_offset = 1000000;  // use high offsets to avoid collisions

    ShapeEntry* shape = type_map->shape;
    while (shape) {
        if (!shape->name || !shape->name->str) {
            shape = shape->next;
            continue;
        }

        // Read the export value from the namespace
        Item key = {.item = s2it(heap_create_name(shape->name->str, (int)shape->name->length))};
        Item value = js_property_get(namespace_obj, key);
        TypeId val_type = get_type_id(value);

        if (val_type == LMD_TYPE_FUNC) {
            // Function export — create synthetic AstFuncNode
            int arity = js_function_get_arity(value);

            if (module_name_collides_with_sys(shape->name->str, (int)shape->name->length)) {
                log_error("module_registry: JS export '%.*s' shadows system function",
                    (int)shape->name->length, shape->name->str);
            }

            AstFuncNode* fn_node = (AstFuncNode*)pool_calloc(pool, sizeof(AstFuncNode));
            fn_node->node_type = AST_NODE_FUNC;
            fn_node->next = NULL;
            fn_node->captures = NULL;
            fn_node->param = NULL;
            fn_node->body = NULL;
            fn_node->vars = NULL;

            // Set synthetic TSNode with unique byte offset
            memset(&fn_node->node, 0, sizeof(TSNode));
            fn_node->node.context[0] = synthetic_offset++;  // ts_node_start_byte() reads context[0]

            // Create name string in pool
            fn_node->name = (String*)pool_calloc(pool, sizeof(String) + shape->name->length + 1);
            fn_node->name->len = (uint32_t)shape->name->length;
            fn_node->name->is_ascii = 1;
            memcpy(fn_node->name->chars, shape->name->str, shape->name->length);
            fn_node->name->chars[shape->name->length] = '\0';

            // Create TypeFunc — all params as Item (boxed), public
            TypeFunc* fn_type = (TypeFunc*)pool_calloc(pool, sizeof(TypeFunc));
            fn_type->type_id = LMD_TYPE_FUNC;
            fn_type->param_count = arity;
            fn_type->required_param_count = arity;
            fn_type->is_public = true;
            fn_type->is_anonymous = false;
            fn_type->is_proc = false;
            fn_type->param = NULL;  // no typed params — all Item
            fn_type->returned = &TYPE_ANY;
            fn_node->type = (Type*)fn_type;

            // Link into AST child list
            if (tail) {
                tail->next = (AstNode*)fn_node;
            } else {
                ast->child = (AstNode*)fn_node;
            }
            tail = (AstNode*)fn_node;

            log_debug("module_registry: JS export fn '%.*s' arity=%d offset=%d",
                (int)shape->name->length, shape->name->str, arity, fn_node->node.context[0]);
        } else if (val_type != LMD_TYPE_NULL) {
            // Variable export — create synthetic pub var node
            AstLetNode* pub_node = (AstLetNode*)pool_calloc(pool, sizeof(AstLetNode));
            pub_node->node_type = AST_NODE_PUB_STAM;
            pub_node->next = NULL;
            pub_node->type = &TYPE_NULL;

            AstNamedNode* named = (AstNamedNode*)pool_calloc(pool, sizeof(AstNamedNode));
            named->node_type = AST_NODE_ASSIGN;
            named->next = NULL;
            named->as = NULL;
            named->error_name = NULL;
            named->type = &TYPE_ANY;

            // Create name in pool
            named->name = (String*)pool_calloc(pool, sizeof(String) + shape->name->length + 1);
            named->name->len = (uint32_t)shape->name->length;
            named->name->is_ascii = 1;
            memcpy(named->name->chars, shape->name->str, shape->name->length);
            named->name->chars[shape->name->length] = '\0';

            pub_node->declare = (AstNode*)named;

            if (tail) {
                tail->next = (AstNode*)pub_node;
            } else {
                ast->child = (AstNode*)pub_node;
            }
            tail = (AstNode*)pub_node;

            log_debug("module_registry: JS export var '%.*s' type=%d",
                (int)shape->name->length, shape->name->str, val_type);
        }

        shape = shape->next;
    }

    return script;
}

// =============================================================================
// Naming convention helpers (Phase 4)
// =============================================================================

const char* module_to_mir_name(const char* raw_name, char* buf, int buf_size) {
    if (!raw_name || buf_size < 2) return "";
    buf[0] = '_';
    int i = 0;
    while (raw_name[i] && i + 1 < buf_size - 1) {
        buf[i + 1] = raw_name[i];
        i++;
    }
    buf[i + 1] = '\0';
    return buf;
}

const char* module_from_mir_name(const char* unified_name) {
    if (!unified_name) return "";
    if (unified_name[0] == '_') return unified_name + 1;
    return unified_name;
}

bool module_name_collides_with_sys(const char* name, int name_len) {
    if (!name || name_len <= 0) return false;
    return is_sys_func_name(name, name_len);
}
