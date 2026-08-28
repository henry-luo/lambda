// module_registry.cpp — Unified cross-language module registry implementation
#include "../lambda-data.hpp"  // must be first — wraps lambda.h in extern "C" block
#include "module_registry.h"
#include "ast.hpp"
#include "transpiler.hpp"
#include "../core/shape_pool.hpp"
#include "concurrency_js.h"
#include "lambda-root-frame.hpp"
#include "../../lib/hashmap.h"
#include "../../lib/mem_factory.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"
#include "../../lib/file.h"
#include "../../lib/path_str.h"

#include <string.h>
#include <stdlib.h>

extern __thread EvalContext* context;
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" uint64_t js_get_heap_epoch(void);

extern "C" Item js_get_key_default(Item object, Item key);
extern "C" Item js_new_object();
extern "C" Item js_set_key_default(Item object, Item key, Item value);
extern "C" int js_function_get_arity(Item fn_item);
extern "C" void* js_function_get_ptr(Item fn_item);

// C++ linkage — defined in build_ast.cpp
bool is_sys_func_name(const char* name, int name_len);

// =============================================================================
// Registry hashmap
// =============================================================================

struct ModuleRegistry {
    Runtime* runtime;
    struct hashmap* map;
    ModuleDescriptor* first;
    ModuleDescriptor* last;
};

// hashmap entry for module descriptors
typedef struct {
    const char* path;       // key (not owned — points to ModuleDescriptor.path)
    ModuleDescriptor* desc; // value (owned)
} RegistryEntry;
HASHMAP_DEFINE_STRKEY(registry, RegistryEntry, path)

static Runtime* module_registry_active_runtime(void) {
    return context ? context->runtime : NULL;
}

static ModuleRegistry* module_registry_for_runtime(Runtime* runtime) {
    Runtime* owner = runtime ? runtime : module_registry_active_runtime();
    return owner ? owner->module_registry : NULL;
}

static ModuleRegistry* module_registry_ensure(Runtime* runtime) {
    Runtime* owner = runtime ? runtime : module_registry_active_runtime();
    if (!owner) return NULL;
    if (owner->module_registry) return owner->module_registry;

    ModuleRegistry* registry = (ModuleRegistry*)mem_calloc(
        1, sizeof(ModuleRegistry), MEM_CAT_SYSTEM);
    if (!registry) return NULL;
    registry->runtime = owner;
    registry->map = registry_new(32);
    if (!registry->map) {
        mem_free(registry);
        return NULL;
    }
    owner->module_registry = registry;
    return registry;
}

static void module_descriptor_ensure_roots(ModuleDescriptor* desc) {
    if (!desc || !context || !context->heap || !context->heap->gc) return;
    uint64_t epoch = js_get_heap_epoch();
    if (desc->roots_epoch == epoch) return;
    heap_register_gc_root(&desc->namespace_obj.item);
    heap_register_gc_root(&desc->specifier_item.item);
    heap_register_gc_root(&desc->awaited_target.item);
    heap_register_gc_root(&desc->evaluation_error.item);
    desc->roots_epoch = epoch;
}

// Canonical module keys collapse aliases before a descriptor is inserted or
// queried. This keeps one definition per resolved file and prevents a
// relative spelling from retaining a namespace in a different registry slot.
static char* module_registry_key_dup(const char* path) {
    if (!path || !*path) return NULL;

    char lexical[4096];
    path_str_normalize_lexical_posix(path, lexical, (int)sizeof(lexical), false);
    if (!lexical[0]) path_str_copy(lexical, (int)sizeof(lexical), path);

    char* resolved = file_realpath(lexical);
    const char* source = resolved ? resolved : lexical;
    char normalized[4096];
    path_str_normalize_lexical_posix(source, normalized, (int)sizeof(normalized), false);
    if (!normalized[0]) path_str_copy(normalized, (int)sizeof(normalized), source);

    char* key = mem_strdup(normalized, MEM_CAT_SYSTEM);
    if (resolved) mem_free(resolved);
    return key;
}

static Item js_namespace_get(Item namespace_obj, const char* name) {
    RootFrame roots(2);
    Rooted<Item> namespace_root(roots, namespace_obj);
    Item key = {.item = s2it(heap_create_name(name))};
    Rooted<Item> key_root(roots, key);
    return js_get_key_default(namespace_root.get(), key_root.get());
}

static const ModuleNamespaceOps js_namespace_ops = {
    js_new_object,
    js_namespace_get,
    js_function_get_arity,
    js_function_get_ptr,
};

void module_registry_init_for_runtime(Runtime* runtime) {
    (void)module_registry_ensure(runtime);
}

void module_registry_cleanup_for_runtime(Runtime* runtime) {
    Runtime* owner = runtime ? runtime : module_registry_active_runtime();
    ModuleRegistry* registry = module_registry_for_runtime(owner);
    if (!registry) return;

    // free all descriptors
    size_t iter = 0;
    void* item;
    while (hashmap_iter(registry->map, &iter, &item)) {
        RegistryEntry* entry = (RegistryEntry*)item;
        if (entry->desc) {
            // Descriptors are native allocations, so unregister their exact
            // namespace roots before the owning heap is retired.
            if (entry->desc->roots_epoch == js_get_heap_epoch()) {
                heap_unregister_gc_root(&entry->desc->namespace_obj.item);
                heap_unregister_gc_root(&entry->desc->specifier_item.item);
                heap_unregister_gc_root(&entry->desc->awaited_target.item);
                heap_unregister_gc_root(&entry->desc->evaluation_error.item);
            }
            mem_free(entry->desc->async_parents);
            mem_free((void*)entry->desc->path);
            mem_free(entry->desc);
        }
    }
    hashmap_free(registry->map);
    owner->module_registry = NULL;
    mem_free(registry);
}

ModuleDescriptor* module_registry_first_for_runtime(Runtime* runtime) {
    ModuleRegistry* registry = module_registry_for_runtime(runtime);
    return registry ? registry->first : NULL;
}

ModuleDescriptor* module_registry_next(ModuleDescriptor* module) {
    return module ? module->next : NULL;
}

void module_registry_init(void) {
    Runtime* runtime = module_registry_active_runtime();
    if (!runtime) {
        log_error("module_registry: init requested without an active Runtime");
        return;
    }
    module_registry_init_for_runtime(runtime);
}

void module_registry_cleanup(void) {
    Runtime* runtime = module_registry_active_runtime();
    if (!runtime) {
        log_error("module_registry: cleanup requested without an active Runtime");
        return;
    }
    module_registry_cleanup_for_runtime(runtime);
}

void module_register_with_namespace_ops_for_runtime(
        Runtime* runtime, const char* path, const char* lang,
        Item namespace_obj, void* mir_ctx,
        const ModuleNamespaceOps* namespace_ops) {
    if (!path || !lang) return;
    ModuleRegistry* registry = module_registry_ensure(runtime);
    if (!registry) {
        log_error("module_registry: cannot register '%s' without a Runtime", path);
        return;
    }
    char* key_path = module_registry_key_dup(path);
    if (!key_path) return;

    // check if already registered — update if so
    RegistryEntry lookup = { .path = key_path, .desc = NULL };
    const RegistryEntry* existing = (const RegistryEntry*)hashmap_get(registry->map, &lookup);
    if (existing && existing->desc) {
        existing->desc->namespace_obj = namespace_obj;
        existing->desc->mir_ctx = mir_ctx;
        existing->desc->source_lang = lang;
        existing->desc->namespace_ops = namespace_ops ? namespace_ops : &js_namespace_ops;
        existing->desc->profile = lang_profile_for_name(lang);
        existing->desc->initialized = true;
        existing->desc->loading = false;
        module_descriptor_ensure_roots(existing->desc);
        log_debug("module_registry: updated module '%s' (lang=%s)", key_path, lang);
        mem_free(key_path);
        return;
    }

    ModuleDescriptor* desc = (ModuleDescriptor*)mem_calloc(1, sizeof(ModuleDescriptor), MEM_CAT_SYSTEM);
    desc->path = key_path;
    desc->next = NULL;
    desc->source_lang = lang;  // static string, not owned
    desc->profile = lang_profile_for_name(lang);
    desc->namespace_obj = namespace_obj;
    // Module descriptors live outside the GC heap. Keep their namespace slot
    // rooted so exact collection cannot reclaim exports between module loads.
    desc->namespace_ops = namespace_ops ? namespace_ops : &js_namespace_ops;
    desc->mir_ctx = mir_ctx;
    desc->initialized = true;
    desc->loading = false;
    desc->async_eval_order = -1;
    desc->saved_module_state_id = UINT32_MAX;
    module_descriptor_ensure_roots(desc);

    RegistryEntry entry = { .path = desc->path, .desc = desc };
    hashmap_set(registry->map, &entry);
    if (registry->last) registry->last->next = desc;
    else registry->first = desc;
    registry->last = desc;
    log_info("module_registry: registered '%s' (lang=%s)", desc->path, lang);
}

void module_register_with_namespace_ops(const char* path, const char* lang,
                                        Item namespace_obj, void* mir_ctx,
                                        const ModuleNamespaceOps* namespace_ops) {
    module_register_with_namespace_ops_for_runtime(
        module_registry_active_runtime(), path, lang, namespace_obj, mir_ctx, namespace_ops);
}

void module_register(const char* path, const char* lang, Item namespace_obj, void* mir_ctx) {
    module_register_for_runtime(
        module_registry_active_runtime(), path, lang, namespace_obj, mir_ctx);
}

void module_register_for_runtime(Runtime* runtime, const char* path, const char* lang,
                                 Item namespace_obj, void* mir_ctx) {
    module_register_with_namespace_ops_for_runtime(
        runtime, path, lang, namespace_obj, mir_ctx, &js_namespace_ops);
}

ModuleDescriptor* module_get(const char* path) {
    return module_get_for_runtime(module_registry_active_runtime(), path);
}

ModuleDescriptor* module_get_for_runtime(Runtime* runtime, const char* path) {
    ModuleRegistry* registry = module_registry_for_runtime(runtime);
    if (!path || !registry) return NULL;
    char* key_path = module_registry_key_dup(path);
    if (!key_path) return NULL;
    RegistryEntry lookup = { .path = key_path, .desc = NULL };
    const RegistryEntry* found = (const RegistryEntry*)hashmap_get(registry->map, &lookup);
    mem_free(key_path);
    return found ? found->desc : NULL;
}

bool module_is_loaded(const char* path) {
    ModuleDescriptor* desc = module_get(path);
    return desc && desc->initialized;
}

ModuleDescriptor* module_register_loading_with_namespace_ops_for_runtime(
        Runtime* runtime, const char* path, const char* lang,
        const ModuleNamespaceOps* namespace_ops) {
    if (!path || !lang) return NULL;
    ModuleRegistry* registry = module_registry_ensure(runtime);
    if (!registry) {
        log_error("module_registry: cannot mark '%s' loading without a Runtime", path);
        return NULL;
    }
    char* key_path = module_registry_key_dup(path);
    if (!key_path) return NULL;

    // check if already registered
    RegistryEntry lookup = { .path = key_path, .desc = NULL };
    const RegistryEntry* existing = (const RegistryEntry*)hashmap_get(registry->map, &lookup);
    if (existing && existing->desc) {
        existing->desc->loading = true;
        existing->desc->source_lang = lang;
        existing->desc->profile = lang_profile_for_name(lang);
        existing->desc->namespace_ops = namespace_ops ? namespace_ops : &js_namespace_ops;
        module_descriptor_ensure_roots(existing->desc);
        mem_free(key_path);
        return existing->desc;
    }

    ModuleDescriptor* desc = (ModuleDescriptor*)mem_calloc(1, sizeof(ModuleDescriptor), MEM_CAT_SYSTEM);
    desc->path = key_path;
    desc->next = NULL;
    desc->source_lang = lang;
    desc->profile = lang_profile_for_name(lang);
    desc->namespace_obj = namespace_ops && namespace_ops->create
        ? namespace_ops->create() : js_new_object();
    // A loading namespace can be observed by cyclic imports before it is
    // initialized, so its native descriptor must own an exact GC root now.
    desc->namespace_ops = namespace_ops ? namespace_ops : &js_namespace_ops;
    desc->mir_ctx = NULL;
    desc->initialized = false;
    desc->loading = true;
    desc->async_eval_order = -1;
    desc->saved_module_state_id = UINT32_MAX;
    module_descriptor_ensure_roots(desc);

    RegistryEntry entry = { .path = desc->path, .desc = desc };
    hashmap_set(registry->map, &entry);
    if (registry->last) registry->last->next = desc;
    else registry->first = desc;
    registry->last = desc;
    log_info("module_registry: marked '%s' as loading (lang=%s)", desc->path, lang);
    return desc;
}

ModuleDescriptor* module_register_loading_with_namespace_ops(
        const char* path, const char* lang, const ModuleNamespaceOps* namespace_ops) {
    return module_register_loading_with_namespace_ops_for_runtime(
        module_registry_active_runtime(), path, lang, namespace_ops);
}

ModuleDescriptor* module_register_loading(const char* path, const char* lang) {
    return module_register_loading_with_namespace_ops(path, lang, &js_namespace_ops);
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
    RootFrame roots(1);
    Rooted<Item> namespace_root(roots, ns);

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
                bool uses_public_wrapper = false;

                // MIR type inference can give a pn a native ABI even when its
                // source parameters are untyped. Prefer the generated boxed
                // entry whenever present so the JS membrane always passes Items.
                write_fn_name_ex(name_buf, fn_node, NULL, "_b");
                func_ptr = find_func((MIR_context_t)script->jit_context, name_buf->str);
                uses_public_wrapper = func_ptr != NULL;
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
                    Item key = {.item = s2it(heap_create_name(export_name))};
                    RootFrame export_roots(3);
                    Rooted<Item> key_root(export_roots, key);
                    Function* fn = to_fn_named((fn_ptr)func_ptr, arity, export_name);
                    Rooted<Item> function_root(export_roots,
                        (Item){.function = fn});
                    if (uses_public_wrapper) {
                        // The JavaScript call membrane enters through Lambda's
                        // dynamic boxed ABI, which needs the defining signature
                        // for required/optional argument adaptation.
                        lambda_function_set_type(fn, fn_type);
                        // Published MIR wrappers require the defining context;
                        // their v3 companion lane is not a caller-home ABI.
                        lambda_function_mark_mir_context_abi(fn);
                        if (node->node_type == AST_NODE_PROC) {
                            lambda_function_mark_lambda_boxed_procedure(fn);
                        } else {
                            lambda_function_mark_lambda_boxed_function(fn);
                        }
                        FnVariantAnalysis* public_variant = fn_node->analysis
                            ? fn_analysis_variant(fn_node->analysis,
                                FN_ENTRY_PUBLIC_WRAPPER) : NULL;
                        uint32_t public_shape = LAMBDA_MIR_PUBLIC_RETURN_UNKNOWN;
                        if (public_variant) {
                            public_shape = public_variant->result.shape ==
                                RETURN_SHAPE_ITEM
                                ? LAMBDA_MIR_PUBLIC_RETURN_ITEM
                                : public_variant->result.shape ==
                                    RETURN_SHAPE_ITEM_SCALAR
                                    ? LAMBDA_MIR_PUBLIC_RETURN_ITEM_COMPANION
                                    : LAMBDA_MIR_PUBLIC_RETURN_UNKNOWN;
                        }
                        lambda_function_mark_mir_public_return_shape(fn,
                            public_shape);
                    }
                    // Lambda procedures cross into JavaScript through one
                    // uniform Promise membrane, even when a particular call
                    // completes without parking.
                    Item val = node->node_type == AST_NODE_PROC
                        ? lambda_js_wrap_procedure(function_root.get().function, arity, export_name)
                        : function_root.get();
                    Rooted<Item> value_root(export_roots, val);
                    // The namespace and export pair remain unpublished until
                    // this store completes; root all three across map growth.
                    js_set_key_default(namespace_root.get(), key_root.get(), value_root.get());
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

    return namespace_root.get();
}

// =============================================================================
// Create a synthetic Script from a hosted namespace for Lambda imports
// =============================================================================

void* create_module_import_script(const char* resolved_path, Item namespace_obj, void* runtime_ptr) {
    Runtime* runtime = (Runtime*)runtime_ptr;
    ModuleDescriptor* module = module_get_for_runtime(runtime, resolved_path);
    TypeId ns_type = get_type_id(namespace_obj);
    if (ns_type != LMD_TYPE_MAP) {
        log_error("module_registry: hosted namespace is not a map (type=%d)", ns_type);
        return NULL;
    }

    // Create a Script with its own pool
    Pool* pool = mem_pool_create(NULL, MEM_ROLE_AST, "module_registry");
    Script* script = (Script*)mem_calloc(1, sizeof(Script), MEM_CAT_SYSTEM);
    script->pool = pool;
    script->reference = mem_strdup(resolved_path, MEM_CAT_SYSTEM);
    script->is_main = false;
    script->is_loading = false;
    // The module's language profile is already resolved at registration; the
    // synthetic Lambda bridge must not assume a JavaScript namespace.
    script->profile = module && module->profile ? module->profile : &js_profile;
    script->const_list = arraylist_new(4);
    script->type_list = arraylist_new(4);

    // register in the runtime's script list and path index
    runtime_register_script(runtime, script);

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
        log_debug("module_registry: hosted namespace has no shape entries");
        return script;
    }

    AstNode* tail = NULL;
    uint32_t synthetic_offset = 1000000U;  // use high offsets to avoid collisions

    ShapeEntry* shape = type_map->shape;
    while (shape) {
        if (!shape->name || !shape->name->str) {
            shape = shape->next;
            continue;
        }

        // Read the export value from the namespace
        Item value = module_namespace_get(module, shape->name->str);
        TypeId val_type = get_type_id(value);

        if (val_type == LMD_TYPE_FUNC) {
            // Function export — create synthetic AstFuncNode
            int arity = module_namespace_function_arity(module, value);

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

            // Hosted functions have no source file; use a unique empty span so
            // diagnostics and AST identity remain deterministic without a parser node.
            fn_node->source_span = (SourceSpan){synthetic_offset, synthetic_offset};
            synthetic_offset++;

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
            fn_type->inferred_return = &TYPE_ANY;
            // Hosted exports are dynamically typed interfaces, so their
            // signature explicitly admits the full value top.
            fn_type->return_contract = &TYPE_ANY;
            fn_type->has_explicit_return_contract = true;
            fn_node->type = (Type*)fn_type;

            // Link into AST child list
            if (tail) {
                tail->next = (AstNode*)fn_node;
            } else {
                ast->child = (AstNode*)fn_node;
            }
            tail = (AstNode*)fn_node;

            log_debug("module_registry: hosted export fn '%.*s' arity=%d offset=%d",
                (int)shape->name->length, shape->name->str, arity,
                fn_node->source_span.start_byte);
        } else if (val_type != LMD_TYPE_NULL) {
            // Variable export — create synthetic pub var node
            AstLetNode* pub_node = (AstLetNode*)pool_calloc(pool, sizeof(AstLetNode));
            pub_node->node_type = AST_NODE_PUB_STAM;
            pub_node->next = NULL;
            pub_node->type = &TYPE_NULL;

            AstDeclaratorNode* named = (AstDeclaratorNode*)pool_calloc(pool,
                sizeof(AstDeclaratorNode));
            named->node_type = AST_NODE_VARIABLE_DECLARATOR;
            named->next = NULL;
            named->init = NULL;
            named->type = &TYPE_ANY;

            // Create name in pool
            named->name = (String*)pool_calloc(pool, sizeof(String) + shape->name->length + 1);
            named->name->len = (uint32_t)shape->name->length;
            named->name->is_ascii = 1;
            memcpy(named->name->chars, shape->name->str, shape->name->length);
            named->name->chars[shape->name->length] = '\0';

            AstIdentNode* id = (AstIdentNode*)pool_calloc(pool, sizeof(AstIdentNode));
            id->node_type = AST_NODE_IDENT;
            id->name = named->name;
            named->id = (AstNode*)id;

            pub_node->declare = (AstNode*)named;

            if (tail) {
                tail->next = (AstNode*)pub_node;
            } else {
                ast->child = (AstNode*)pub_node;
            }
            tail = (AstNode*)pub_node;

            log_debug("module_registry: hosted export var '%.*s' type=%d",
                (int)shape->name->length, shape->name->str, val_type);
        }

        shape = shape->next;
    }

    return script;
}

Item module_namespace_get(const ModuleDescriptor* module, const char* name) {
    if (!module || !module->namespace_ops || !module->namespace_ops->get || !name) return ItemNull;
    return module->namespace_ops->get(module->namespace_obj, name);
}

int module_namespace_function_arity(const ModuleDescriptor* module, Item function_obj) {
    if (!module || !module->namespace_ops || !module->namespace_ops->function_arity) return 0;
    return module->namespace_ops->function_arity(function_obj);
}

void* module_namespace_function_ptr(const ModuleDescriptor* module, Item function_obj) {
    if (!module || !module->namespace_ops || !module->namespace_ops->function_ptr) return NULL;
    return module->namespace_ops->function_ptr(function_obj);
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
