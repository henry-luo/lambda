#include "js_source_module.hpp"
#include "js_transpiler.hpp"
#include "js_runtime.h"
#include "js_runtime_state.hpp"
#include "js_function.hpp"
#include "js_props.h"
#include "js_well_known_names.h"
#include "../io/resource_policy.h"
#include "../runtime/heap_api.h"
#include "../runtime/runtime-state.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../../lib/mem.h"
#include "../../lib/utf.h"
#include "../../lib/str.h"
#include <string.h>
#include <stdlib.h>

bool js_module_set_source_ops(const JsSourceModuleOps* ops, void* owner) {
    if (!js_active_runtime_state || !ops || !owner) return false;
    JsModuleRuntimeState* state = &js_runtime_state.modules;
    if (state->source_ops && (state->source_ops != ops || state->source_owner != owner)) return false;
    state->source_ops = ops;
    state->source_owner = owner;
    return true;
}

static bool module_name_equal(const String* left, const String* right) {
    return left == right || (left && right && str_eq(left->chars, left->len, right->chars, right->len));
}
static Item module_error(JsSourceModuleRecord* record, JsSourceModuleError kind, const char* message) {
    return record->ops->error(record->owner, kind, message);
}
static Item module_fail(JsSourceModuleRecord* record, Item error) {
    // the canonical descriptor already traces both the error and any thrown payload.
    record->descriptor->evaluation_error = js_retain_scalar(error);
    record->status = JS_SOURCE_MODULE_FAILED;
    record->descriptor->loading = false;
    return record->descriptor->evaluation_error;
}
static void source_record_destroy(void* opaque) {
    JsSourceModuleRecord* record = (JsSourceModuleRecord*)opaque;
    if (record->import_meta_registered) heap_unregister_gc_root_range(&record->import_meta.item);
    ArrayList* lists[] = {record->requests, record->imports, record->exports, record->export_cells};
    for (ArrayList* list : lists) if (list) {
        for (int index = 0; index < list->length; index++) mem_free(list->data[index]);
        arraylist_free(list);
    }
    // frontend units outlive this native table and are released after heap finalizers.
    mem_free(record);
}
static Item empty_namespace() { return ItemNull; }
static const ModuleNamespaceOps source_namespace_ops = {
    empty_namespace,
    [](Item object, const char* name) { return vmap_get_by_str(object.vmap, name); },
    js_function_get_arity, js_function_get_ptr
};

static JsSourceModuleRecord* module_record(Runtime* runtime, const JsSourceModuleOps* ops,
        void* owner, const char* path) {
    ModuleDescriptor* descriptor = module_get_for_runtime(runtime, path);
    bool fresh = descriptor == NULL;
    if (!descriptor) descriptor = module_register_loading_with_namespace_ops_for_runtime(
        runtime, path, "js", &source_namespace_ops);
    if (!descriptor) return NULL;
    if (descriptor->source_record) {
        auto* record = (JsSourceModuleRecord*)descriptor->source_record;
        return record->ops == ops && record->owner == owner ? record : NULL;
    }
    // a foreign backend's live module cannot be reinterpreted as this frontend's record.
    if (!fresh) return NULL;
    auto* record = (JsSourceModuleRecord*)mem_calloc(1, sizeof(JsSourceModuleRecord), MEM_CAT_JS_RUNTIME);
    if (!record) return NULL;
    record->runtime = runtime; record->descriptor = descriptor; record->ops = ops; record->owner = owner;
    record->requests = arraylist_new(4); record->imports = arraylist_new(4);
    record->exports = arraylist_new(4); record->export_cells = arraylist_new(4);
    if (!record->requests || !record->imports || !record->exports || !record->export_cells) {
        source_record_destroy(record); return NULL;
    }
    descriptor->source_record = record;
    descriptor->source_record_destroy = source_record_destroy;
    // canonical descriptors already root their specifier before any queued observer captures it.
    descriptor->specifier_item = js_make_string(descriptor->path);
    if (item_is_error(descriptor->specifier_item)) return NULL;
    return record;
}
static JsSourceModuleRecord* module_request(JsSourceModuleRecord* record, String* source) {
    for (int index = 0; index < record->requests->length; index++) {
        auto* request = (JsSourceModuleRequest*)record->requests->data[index];
        if (module_name_equal(request->source, source)) return request->module;
    }
    return NULL;
}
static bool append_export(JsSourceModuleRecord* record, String* name, String* source,
        String* imported, NameEntry* local, bool namespace_binding) {
    // source parsers already reject duplicate explicit export names; retain that invariant here.
    for (int index = 0; name && index < record->exports->length; index++) {
        auto* item = (JsSourceModuleExport*)record->exports->data[index];
        if (module_name_equal(item->name, name)) return item->local == local && item->source == source;
    }
    auto* item = (JsSourceModuleExport*)mem_calloc(1, sizeof(JsSourceModuleExport), MEM_CAT_JS_RUNTIME);
    if (!item) return false;
    item->name = name; item->source = source; item->imported = imported;
    item->local = local; item->namespace_binding = namespace_binding;
    if (arraylist_append(record->exports, item)) return true;
    mem_free(item); return false;
}
struct ModulePatternExports { JsSourceModuleRecord* record; bool valid; };
static void append_pattern_exports(AstNode* node, void* opaque) {
    auto* state = (ModulePatternExports*)opaque;
    if (!node || !state->valid) return;
    if (node->node_type == AST_NODE_IDENT) {
        auto* identifier = (AstIdentNode*)node;
        state->valid = append_export(state->record, identifier->name, NULL, NULL, identifier->entry, false);
    } else js_ast_visit_binding_pattern_children(node, append_pattern_exports, state);
}
static Item collect_module_declarations(JsSourceModuleRecord* record, ArrayList* graph) {
    JsScript* script = record->script;
    if (!script || !script->ast_root || script->ast_root->node_type != AST_SCRIPT)
        return module_error(record, JS_SOURCE_MODULE_CAPABILITY, "Module frontend did not retain a source program");
    AstNodeId root = ast_index_find(&script->ast_index, script->ast_root);
    record->descriptor->has_tla = js_ast_index_can_suspend(&script->ast_index, root,
        root, script->ast_index.count, JS_SUSPENSION_AWAIT);
    for (JsInterpModuleBinding* binding = script->interp_imports; binding; binding = binding->next) {
        auto* imported = (JsSourceModuleImport*)mem_calloc(1, sizeof(JsSourceModuleImport), MEM_CAT_JS_RUNTIME);
        if (!imported) return ItemError;
        imported->local = binding->entry; imported->source = binding->source;
        imported->imported = binding->export_name; imported->namespace_binding = binding->namespace_binding;
        if (!arraylist_append(record->imports, imported)) { mem_free(imported); return ItemError; }
    }
    for (JsInterpModuleBinding* binding = script->interp_exports; binding; binding = binding->next)
        if (!append_export(record, binding->star_export ? NULL : binding->export_name,
                binding->source, binding->local_name, binding->entry, binding->namespace_binding)) return ItemError;
    for (AstNode* node = ((AstScript*)script->ast_root)->body; node; node = node->next) {
        String* source = node->node_type == AST_NODE_IMPORT ? ((AstImportNode*)node)->source :
            node->node_type == AST_NODE_EXPORT ? ((AstExportDeclNode*)node)->source : NULL;
        if (source && !module_request(record, source)) {
            char resolved[4096];
            js_resolve_script_module_path(record->runtime, record->descriptor->path,
                source->chars, (int)source->len, resolved, sizeof(resolved));
            // a warm registry entry cannot grant a request the host now denies.
            if (!input_resource_policy_admits(record->runtime->resource_policy, resolved))
                return module_error(record, JS_SOURCE_MODULE_REFERENCE,
                    "Module blocked by document resource policy");
            JsSourceModuleRecord* child = module_record(record->runtime, record->ops, record->owner, resolved);
            if (!child) return module_error(record, JS_SOURCE_MODULE_CAPABILITY, "Module belongs to an incompatible frontend");
            auto* request = (JsSourceModuleRequest*)mem_calloc(1, sizeof(JsSourceModuleRequest), MEM_CAT_JS_RUNTIME);
            if (!request) return ItemError;
            request->source = source; request->module = child;
            if (!arraylist_append(record->requests, request)) { mem_free(request); return ItemError; }
            bool present = false;
            for (int index = 0; index < graph->length; index++) present |= graph->data[index] == child;
            if (!present && !arraylist_append(graph, child)) return ItemError;
        }
        if (node->node_type != AST_NODE_EXPORT) continue;
        auto* exported = (AstExportDeclNode*)node;
        if (exported->is_star && !append_export(record, NULL, source, NULL, NULL, false)) return ItemError;
        AstNode* declaration = exported->declaration;
        if (exported->is_default) {
            String* name = name_pool_create_len(script->name_pool, "default", 7);
            record->default_entry.name = name;
            record->default_entry.is_lexical = record->default_entry.is_const = true;
            record->default_entry.node = declaration;
            record->default_declaration = declaration;
            NameEntry* entry = declaration && declaration->node_type == AST_NODE_FUNC
                ? ((AstFuncNode*)declaration)->entry : declaration && declaration->node_type == AST_NODE_CLASS
                && !module_name_equal(((AstClassNode*)declaration)->name, name)
                ? ((AstClassNode*)declaration)->outer_entry : NULL;
            if (!name || !append_export(record, name, NULL, NULL,
                    entry ? entry : &record->default_entry, false)) return ItemError;
        } else if (declaration && declaration->node_type == AST_NODE_VAR_STAM) {
            ModulePatternExports state = {record, true};
            for (AstNode* variable = ((AstVarDeclNode*)declaration)->declarations; variable; variable = variable->next)
                append_pattern_exports(((AstDeclaratorNode*)variable)->id, &state);
            if (!state.valid) return ItemError;
        }
    }
    return ItemNull;
}
struct ModuleResolveVisit { JsSourceModuleRecord* record; String* name; ModuleResolveVisit* parent; };
static JsSourceBindingStatus resolve_binding(JsSourceModuleRecord* record, String* name,
        JsResolvedModuleBinding* output, ModuleResolveVisit* visited) {
    if (lambda_stack_is_exhausted(context ? context->stack_limit : 0)) {
        module_fail(record, module_error(record, JS_SOURCE_MODULE_RANGE, "Module resolution stack exhausted"));
        return JS_SOURCE_BINDING_ERROR;
    }
    for (ModuleResolveVisit* item = visited; item; item = item->parent)
        if (item->record == record && module_name_equal(item->name, name)) return JS_SOURCE_BINDING_MISSING;
    ModuleResolveVisit visit = {record, name, visited};
    for (int index = 0; index < record->exports->length; index++) {
        auto* exported = (JsSourceModuleExport*)record->exports->data[index];
        if (!exported->name || !module_name_equal(exported->name, name)) continue;
        if (exported->source) {
            JsSourceModuleRecord* child = module_request(record, exported->source);
            if (!child) return JS_SOURCE_BINDING_ERROR;
            if (exported->namespace_binding) {
                *output = {child, NULL, NULL, 0, true}; return JS_SOURCE_BINDING_FOUND;
            }
            JsSourceBindingStatus status = resolve_binding(child, exported->imported, output, &visit);
            if (status == JS_SOURCE_BINDING_ERROR && child->status == JS_SOURCE_MODULE_FAILED)
                module_fail(record, child->descriptor->evaluation_error);
            return status;
        }
        for (int imported_index = 0; imported_index < record->imports->length; imported_index++) {
            auto* imported = (JsSourceModuleImport*)record->imports->data[imported_index];
            if (imported->local != exported->local) continue;
            JsSourceModuleRecord* child = module_request(record, imported->source);
            if (!child) return JS_SOURCE_BINDING_ERROR;
            if (imported->namespace_binding) {
                *output = {child, NULL, NULL, 0, true}; return JS_SOURCE_BINDING_FOUND;
            }
            JsSourceBindingStatus status = resolve_binding(child, imported->imported, output, &visit);
            if (status == JS_SOURCE_BINDING_ERROR && child->status == JS_SOURCE_MODULE_FAILED)
                module_fail(record, child->descriptor->evaluation_error);
            return status;
        }
        return record->ops->local_binding(record->owner, record, exported->local, output)
            ? JS_SOURCE_BINDING_FOUND : JS_SOURCE_BINDING_ERROR;
    }
    if (name->len == 7 && !memcmp(name->chars, "default", 7)) return JS_SOURCE_BINDING_MISSING;
    bool found = false;
    for (int index = 0; index < record->exports->length; index++) {
        auto* exported = (JsSourceModuleExport*)record->exports->data[index];
        if (exported->name) continue;
        JsSourceModuleRecord* child = module_request(record, exported->source);
        if (!child) return JS_SOURCE_BINDING_ERROR;
        JsResolvedModuleBinding candidate = {};
        JsSourceBindingStatus status = resolve_binding(child, name, &candidate, &visit);
        if (status == JS_SOURCE_BINDING_ERROR && child->status == JS_SOURCE_MODULE_FAILED)
            module_fail(record, child->descriptor->evaluation_error);
        if (status == JS_SOURCE_BINDING_ERROR || status == JS_SOURCE_BINDING_AMBIGUOUS) return status;
        if (status != JS_SOURCE_BINDING_FOUND) continue;
        if (found && (output->module != candidate.module || output->entry != candidate.entry ||
                output->namespace_binding != candidate.namespace_binding)) return JS_SOURCE_BINDING_AMBIGUOUS;
        *output = candidate; found = true;
    }
    return found ? JS_SOURCE_BINDING_FOUND : JS_SOURCE_BINDING_MISSING;
}
JsSourceBindingStatus js_module_resolve_binding(JsSourceModuleRecord* record, String* name,
        JsResolvedModuleBinding* output) {
    return record && name && output ? resolve_binding(record, name, output, NULL) : JS_SOURCE_BINDING_ERROR;
}
static bool collect_export_names(JsSourceModuleRecord* record, ArrayList* names, ArrayList* visited, bool star) {
    if (lambda_stack_is_exhausted(context ? context->stack_limit : 0)) {
        module_fail(record, module_error(record, JS_SOURCE_MODULE_RANGE, "Module export stack exhausted"));
        return false;
    }
    for (int index = 0; index < visited->length; index++) if (visited->data[index] == record) return true;
    if (!arraylist_append(visited, record)) return false;
    for (int index = 0; index < record->exports->length; index++) {
        auto* exported = (JsSourceModuleExport*)record->exports->data[index];
        if (!exported->name) {
            JsSourceModuleRecord* child = module_request(record, exported->source);
            if (!child) return false;
            if (!collect_export_names(child, names, visited, true)) {
                if (child->status == JS_SOURCE_MODULE_FAILED) module_fail(record, child->descriptor->evaluation_error);
                return false;
            }
            continue;
        }
        if (star && exported->name->len == 7 && !memcmp(exported->name->chars, "default", 7)) continue;
        bool present = false;
        for (int found = 0; found < names->length; found++)
            present |= module_name_equal((String*)names->data[found], exported->name);
        if (!present && !arraylist_append(names, exported->name)) return false;
    }
    return true;
}
static int compare_export_cells(const void* left, const void* right) {
    auto* a = *(JsSourceModuleExportCell* const*)left;
    auto* b = *(JsSourceModuleExportCell* const*)right;
    return utf16_compare(a->name->chars, a->name->len, b->name->chars, b->name->len);
}
static Item link_bindings(JsSourceModuleRecord* record) {
    // a sibling can be retried after an earlier graph failed before backend linking.
    for (int index = 0; index < record->export_cells->length; index++)
        mem_free(record->export_cells->data[index]);
    record->export_cells->length = 0;
    for (int index = 0; index < record->imports->length; index++) {
        auto* imported = (JsSourceModuleImport*)record->imports->data[index];
        JsSourceModuleRecord* child = module_request(record, imported->source);
        if (!child) return ItemError;
        if (imported->namespace_binding) imported->resolved = {child, NULL, NULL, 0, true};
        else if (js_module_resolve_binding(child, imported->imported, &imported->resolved) != JS_SOURCE_BINDING_FOUND)
            return child->status == JS_SOURCE_MODULE_FAILED ? child->descriptor->evaluation_error :
                module_error(record, JS_SOURCE_MODULE_SYNTAX, "Module import has no unambiguous export");
    }
    // explicit re-exports are link obligations; ambiguous star-only names are omitted from namespaces.
    for (int index = 0; index < record->exports->length; index++) {
        auto* exported = (JsSourceModuleExport*)record->exports->data[index];
        JsResolvedModuleBinding binding = {};
        if (exported->name && js_module_resolve_binding(record, exported->name, &binding) != JS_SOURCE_BINDING_FOUND)
            return record->status == JS_SOURCE_MODULE_FAILED ? record->descriptor->evaluation_error :
                module_error(record, JS_SOURCE_MODULE_SYNTAX, "Module export does not resolve to a binding");
    }
    ArrayList* names = arraylist_new(8); ArrayList* visited = arraylist_new(8);
    bool valid = names && visited && collect_export_names(record, names, visited, false);
    for (int index = 0; valid && index < names->length; index++) {
        auto* cell = (JsSourceModuleExportCell*)mem_calloc(1, sizeof(JsSourceModuleExportCell), MEM_CAT_JS_RUNTIME);
        if (!cell) { valid = false; break; }
        cell->name = (String*)names->data[index];
        JsSourceBindingStatus status = js_module_resolve_binding(record, cell->name, &cell->binding);
        if (status == JS_SOURCE_BINDING_ERROR) valid = false;
        if (status != JS_SOURCE_BINDING_FOUND || !arraylist_append(record->export_cells, cell)) {
            if (status == JS_SOURCE_BINDING_FOUND) valid = false;
            mem_free(cell);
        }
    }
    if (names) arraylist_free(names);
    if (visited) arraylist_free(visited);
    if (!valid) return record->status == JS_SOURCE_MODULE_FAILED ? record->descriptor->evaluation_error : ItemError;
    qsort(record->export_cells->data, record->export_cells->length, sizeof(void*), compare_export_cells);
    return ItemNull;
}
Item js_module_load_link(Runtime* runtime, const JsSourceModuleOps* ops, void* owner,
        const char* path, const char* source, size_t length, JsSourceModuleRecord** output) {
    if (output) *output = NULL;
    if (!runtime || !ops || !path) return ItemError;
    if (!input_resource_policy_admits(runtime->resource_policy, path))
        return ops->error(owner, JS_SOURCE_MODULE_REFERENCE,
            "Module blocked by document resource policy");
    JsSourceModuleRecord* root = module_record(runtime, ops, owner, path);
    if (!root) return ops->error(owner, JS_SOURCE_MODULE_CAPABILITY, "Module belongs to an incompatible frontend");
    if (output) *output = root;
    if (root->status == JS_SOURCE_MODULE_FAILED) return root->descriptor->evaluation_error;
    if (root->status >= JS_SOURCE_MODULE_LINKED) return ItemNull;
    ArrayList* graph = arraylist_new(8);
    if (!graph || !arraylist_append(graph, root)) { if (graph) arraylist_free(graph); return ItemError; }
    RootFrame roots(1);
    if (!roots.valid()) { arraylist_free(graph); return ItemError; }
    Rooted<Item> status(roots, ItemNull);
    // publish identities before following requests; cycles never recurse through the loader.
    for (int index = 0; index < graph->length && !item_is_error(status.get()); index++) {
        auto* record = (JsSourceModuleRecord*)graph->data[index];
        if (record->status == JS_SOURCE_MODULE_FAILED) { status.set(record->descriptor->evaluation_error); break; }
        if (record->status != JS_SOURCE_MODULE_NEW) {
            for (int request_index = 0; request_index < record->requests->length; request_index++) {
                auto* child = ((JsSourceModuleRequest*)record->requests->data[request_index])->module;
                bool present = false;
                for (int found = 0; found < graph->length; found++) present |= graph->data[found] == child;
                if (!present && !arraylist_append(graph, child)) { status.set(ItemError); break; }
            }
            continue;
        }
        size_t loaded_length = 0;
        char* loaded = record == root && source ? NULL : js_load_script_source_from_cache(
            record->descriptor->path, "js-source-module", "module", true, &loaded_length, runtime->resource_policy);
        if (!loaded && !(record == root && source))
            status.set(module_error(record, JS_SOURCE_MODULE_REFERENCE, "Cannot find module"));
        else status.set(ops->prepare(owner, record, loaded ? loaded : source, loaded ? loaded_length : length));
        mem_free(loaded);
        if (!item_is_error(status.get())) status.set(collect_module_declarations(record, graph));
        if (!item_is_error(status.get())) status.set(ops->plan(owner, record));
        if (item_is_error(status.get())) module_fail(record, status.get());
        else record->status = JS_SOURCE_MODULE_PARSED;
    }
    for (int index = 0; index < graph->length && !item_is_error(status.get()); index++) {
        auto* record = (JsSourceModuleRecord*)graph->data[index];
        if (record->status == JS_SOURCE_MODULE_PARSED) status.set(link_bindings(record));
    }
    // every requested export is validated before any backend instantiates or enters a body.
    for (int index = 0; index < graph->length && !item_is_error(status.get()); index++) {
        auto* record = (JsSourceModuleRecord*)graph->data[index];
        if (record->status != JS_SOURCE_MODULE_PARSED) continue;
        status.set(ops->link(owner, record));
        if (!item_is_error(status.get())) record->status = JS_SOURCE_MODULE_LINKED;
    }
    if (item_is_error(status.get())) module_fail(root, status.get());
    arraylist_free(graph); return status.get();
}
static Item instantiate_graph(JsSourceModuleRecord* record) {
    if (lambda_stack_is_exhausted(context ? context->stack_limit : 0))
        return module_fail(record, module_error(record, JS_SOURCE_MODULE_RANGE, "Module instantiation stack exhausted"));
    if (record->status == JS_SOURCE_MODULE_FAILED) return record->descriptor->evaluation_error;
    if (record->status >= JS_SOURCE_MODULE_INSTANTIATED || record->linking) return ItemNull;
    if (record->status != JS_SOURCE_MODULE_LINKED) return ItemError;
    record->linking = true;
    RootFrame roots(1);
    if (!roots.valid()) { record->linking = false; return ItemError; }
    Rooted<Item> status(roots, ItemNull);
    for (int index = 0; index < record->requests->length && !item_is_error(status.get()); index++)
        status.set(instantiate_graph(((JsSourceModuleRequest*)record->requests->data[index])->module));
    if (!item_is_error(status.get())) status.set(record->ops->instantiate(record->owner, record));
    record->linking = false;
    if (item_is_error(status.get())) return module_fail(record, status.get());
    record->status = JS_SOURCE_MODULE_INSTANTIATED; return ItemNull;
}
void js_module_source_complete(JsSourceModuleRecord* record, Item completion) {
    if (!record || record->descriptor->body_executed) return;
    RootFrame roots(2);
    if (!roots.valid()) return;
    Rooted<Item> result(roots, completion), reason(roots, ItemNull);
    ModuleDescriptor* descriptor = record->descriptor;
    bool rejected = item_is_error(result.get());
    if (rejected) {
        module_fail(record, result.get());
        reason.set(record->ops->error_value(record->owner, result.get()));
    } else {
        record->status = JS_SOURCE_MODULE_EVALUATED;
        descriptor->initialized = true;
        descriptor->loading = false;
    }
    descriptor->body_executed = true;
    descriptor->post_await_pending = false;
    descriptor->deferred_async_frame = ItemNull;
    record->async_evaluation = false;
    if (rejected) js_promise_reject_existing(descriptor->awaited_target, reason.get());
    else js_promise_fulfill_existing(descriptor->awaited_target, Item{.item = ITEM_JS_UNDEFINED});
    // all edges are updated before the existing ready drain executes another guest body.
    for (int index = 0; index < descriptor->async_parent_count; index++) {
        ModuleDescriptor* parent = descriptor->async_parents[index];
        if (!parent || parent->body_executed) continue;
        if (parent->pending_async_deps > 0) parent->pending_async_deps--;
        if (rejected && parent->source_record)
            js_module_source_complete((JsSourceModuleRecord*)parent->source_record, result.get());
    }
    if (!rejected) js_tla_flush_for_runtime(record->runtime);
}

static Item source_body_settled(Item callee, Item, Item* args, int count, uint64_t*) {
    auto* function = (JsFunction*)callee.function;
    auto* record = (JsSourceModuleRecord*)(uintptr_t)js_fn_native(function)->target.bits;
    if (!record || !function->env) return ItemError;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    // registry identity, not a same-spelled path in the current realm, owns completion.
    Rooted<Item> input(roots, js_retain_scalar(count ? args[0] : make_js_undefined()));
    Rooted<Item> completion(roots, function->env[0].item == ITEM_TRUE ? js_throw_value(input.get()) : ItemNull);
    js_module_source_complete(record, completion.get());
    return Item{.item = ITEM_JS_UNDEFINED};
}

static Item source_start_body(JsSourceModuleRecord* record) {
    ModuleDescriptor* descriptor = record->descriptor;
    if (descriptor->body_executed || descriptor->pending_async_deps > 0) return ItemNull;
    if (descriptor->has_tla) {
        if (descriptor->deferred_async_frame.item == ITEM_NULL) return ItemNull;
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> frame(roots, descriptor->deferred_async_frame);
        descriptor->deferred_async_frame = ItemNull;
        return js_async_start(frame.get());
    }
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> result(roots, record->ops->evaluate(record->owner, record, false));
    js_module_source_complete(record, result.get());
    return result.get();
}

static Item evaluate_graph(JsSourceModuleRecord* record, ArrayList* stack, uint32_t* next_index) {
    if (lambda_stack_is_exhausted(context ? context->stack_limit : 0))
        return module_fail(record, module_error(record, JS_SOURCE_MODULE_RANGE, "Module evaluation stack exhausted"));
    if (record->status == JS_SOURCE_MODULE_FAILED) return record->descriptor->evaluation_error;
    if (record->status >= JS_SOURCE_MODULE_EVALUATING) return ItemNull;
    if (!arraylist_append(stack, record)) return ItemError;
    record->status = JS_SOURCE_MODULE_EVALUATING;
    record->evaluation_index = record->evaluation_ancestor = (*next_index)++;
    record->evaluation_on_stack = true;
    RootFrame roots(3);
    if (!roots.valid()) return ItemError;
    Rooted<Item> status(roots, ItemNull), fulfilled(roots, ItemNull), rejected(roots, ItemNull);
    ModuleDescriptor* descriptor = record->descriptor;
    descriptor->awaited_target = js_promise_create_pending();
    if (item_is_error(descriptor->awaited_target)) return descriptor->awaited_target;
    // internal completion records propagate through module edges; only import() results are public capabilities.
    js_promise_mark_handled(descriptor->awaited_target);
    for (int index = 0; index < record->requests->length && !item_is_error(status.get()); index++) {
        auto* child = ((JsSourceModuleRequest*)record->requests->data[index])->module;
        status.set(evaluate_graph(child, stack, next_index));
        if (item_is_error(status.get())) break;
        if (child->evaluation_on_stack) {
            if (child->evaluation_ancestor < record->evaluation_ancestor)
                record->evaluation_ancestor = child->evaluation_ancestor;
        } else if (child->descriptor->async_cycle_root) {
            child = (JsSourceModuleRecord*)child->descriptor->async_cycle_root->source_record;
            if (child->status == JS_SOURCE_MODULE_FAILED) { status.set(child->descriptor->evaluation_error); break; }
        }
        if (child->async_evaluation)
            js_module_register_async_parent(child->descriptor->specifier_item, descriptor->specifier_item);
    }
    if (item_is_error(status.get())) return status.get();
    if (descriptor->has_tla || descriptor->pending_async_deps > 0) {
        record->async_evaluation = true;
        record->async_evaluation_order = ++js_runtime_state.modules.source_evaluation_order;
        if (descriptor->has_tla) {
            descriptor->deferred_async_frame = record->ops->evaluate(record->owner, record, true);
            if (item_is_error(descriptor->deferred_async_frame)) return descriptor->deferred_async_frame;
            for (int mode = 0; mode < 2; mode++) {
                Item* environment = js_alloc_env1(Item{.item = b2it(mode != 0)});
                if (!environment) return ItemError;
                Item callback = js_new_native_payload_function(source_body_settled,
                    (uint64_t)(uintptr_t)record, 1, environment, 1);
                (mode ? rejected : fulfilled).set(callback);
                if (item_is_error(callback)) return callback;
            }
            if (!js_promise_observe(js_async_get_promise(descriptor->deferred_async_frame),
                    fulfilled.get(), rejected.get(), ItemNull)) return ItemError;
            if (!descriptor->pending_async_deps) status.set(source_start_body(record));
        }
    } else {
        status.set(record->ops->evaluate(record->owner, record, false));
        if (!item_is_error(status.get())) descriptor->body_executed = true;
    }
    if (item_is_error(status.get())) return status.get();
    if (record->evaluation_ancestor == record->evaluation_index) {
        // the DFS root, rather than registry insertion order, owns the component completion.
        JsSourceModuleRecord* member;
        do {
            member = (JsSourceModuleRecord*)stack->data[--stack->length];
            member->evaluation_on_stack = false;
            member->descriptor->async_cycle_root = descriptor;
            member->status = member->async_evaluation ? JS_SOURCE_MODULE_ASYNC : JS_SOURCE_MODULE_EVALUATED;
            if (!member->async_evaluation) {
                member->descriptor->initialized = true;
                member->descriptor->loading = false;
                js_promise_fulfill_existing(member->descriptor->awaited_target, Item{.item = ITEM_JS_UNDEFINED});
            }
        } while (member != record);
    }
    return ItemNull;
}
Item js_module_evaluate(JsSourceModuleRecord* record) {
    if (!record) return ItemError;
    if (record->status == JS_SOURCE_MODULE_ASYNC) return source_start_body(record);
    Item ready = instantiate_graph(record);
    if (item_is_error(ready)) return ready;
    ArrayList* stack = arraylist_new(8);
    if (!stack) return ItemError;
    RootFrame roots(1);
    if (!roots.valid()) { arraylist_free(stack); return ItemError; }
    uint32_t index = 0;
    Rooted<Item> status(roots, evaluate_graph(record, stack, &index));
    if (item_is_error(status.get())) for (int cursor = 0; cursor < stack->length; cursor++) {
        auto* pending = (JsSourceModuleRecord*)stack->data[cursor];
        pending->evaluation_on_stack = false;
        // a completed synchronous body can still belong to the failing evaluation component.
        pending->descriptor->body_executed = false;
        js_module_source_complete(pending, status.get());
    }
    arraylist_free(stack); return status.get();
}
Item js_module_binding_get(const JsResolvedModuleBinding* binding) {
    if (!binding || !binding->module) return ItemError;
    if (binding->namespace_binding) return js_module_namespace_create(binding->module);
    Item value = lambda_module_var_at(binding->slab, binding->slot);
    return value.item == ITEM_JS_TDZ ? module_error(binding->module, JS_SOURCE_MODULE_REFERENCE,
        "Module binding is not initialized") : js_retain_scalar(value);
}

// Namespace carriers are the module exotic, never the physical representation
// of an ordinary guest object. The registry owns the record through job shutdown.
static Item namespace_vmap_get(void*, Item);
static void namespace_vmap_set(void*, Item, Item);
static int64_t namespace_vmap_count(void*);
static SymbolKeyList* namespace_vmap_keys(void*);
static Item namespace_vmap_key_at(void*, int64_t);
static Item namespace_vmap_value_at(void*, int64_t);
static void namespace_vmap_destroy(void*);
static void namespace_vmap_trace(void*, gc_heap*);
static VMapVtable source_namespace_vtable = {namespace_vmap_get, namespace_vmap_set,
    namespace_vmap_count, namespace_vmap_keys, namespace_vmap_key_at,
    namespace_vmap_value_at, namespace_vmap_destroy, namespace_vmap_trace};
JsSourceModuleRecord* js_module_namespace_record(Item object) {
    return get_type_id(object) == LMD_TYPE_VMAP && object.vmap &&
        object.vmap->vtable == &source_namespace_vtable
        ? (JsSourceModuleRecord*)object.vmap->data : NULL;
}
static JsSourceModuleExportCell* namespace_export(JsSourceModuleRecord* record, Item key) {
    if (!record || get_type_id(key) != LMD_TYPE_STRING ||
            property_key_requires_identity(key.get_string())) return NULL;
    String* name = key.get_string();
    int low = 0, high = record->export_cells->length;
    while (low < high) {
        int middle = low + (high - low) / 2;
        auto* cell = (JsSourceModuleExportCell*)record->export_cells->data[middle];
        int order = utf16_compare(name->chars, name->len, cell->name->chars, cell->name->len);
        if (!order) return cell;
        if (order < 0) high = middle;
        else low = middle + 1;
    }
    return NULL;
}
static bool namespace_tag(Item key) {
    return get_type_id(key) == LMD_TYPE_STRING &&
        property_key_id(key.get_string()) == JS_SYMBOL_TO_STRING_TAG;
}
Item js_module_namespace_property(Item owner, Item key, JsModuleNamespaceOperation operation) {
    JsSourceModuleRecord* record = js_module_namespace_record(owner);
    if (!record) return ItemError;
    if (operation == JS_MODULE_NAMESPACE_SET) return Item{.item = ITEM_FALSE};
    JsSourceModuleExportCell* exported = namespace_export(record, key);
    bool tag = namespace_tag(key), present = exported || tag;
    if (operation == JS_MODULE_NAMESPACE_HAS) return Item{.item = b2it(present)};
    if (operation == JS_MODULE_NAMESPACE_DELETE) return Item{.item = b2it(!present)};
    if (exported) return js_module_binding_get(&exported->binding);
    return tag ? js_name_item("Module", 6) : Item{.item = ITEM_JS_UNDEFINED};
}
Item js_module_namespace_get_own(Item owner, Item key, JsPropertyDescriptor* output) {
    JsSourceModuleRecord* record = js_module_namespace_record(owner);
    if (!record || !output) return ItemError;
    JsSourceModuleExportCell* exported = namespace_export(record, key);
    if (!exported && !namespace_tag(key)) return Item{.item = ITEM_FALSE};
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner);
    *output = {};
    output->flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    if (exported) output->flags |= JS_PD_WRITABLE | JS_PD_ENUMERABLE;
    js_pd_set_configurable(output, false);
    // GetOwnProperty must read the live binding, including its TDZ failure.
    output->value = js_module_namespace_property(object.get(), key, JS_MODULE_NAMESPACE_GET);
    return item_is_error(output->value) ? output->value : Item{.item = ITEM_TRUE};
}
Item js_module_namespace_define(Item owner, Item key, const JsPropertyDescriptor* update) {
    JsSourceModuleRecord* record = js_module_namespace_record(owner);
    if (!record || !update) return ItemError;
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), name(roots, key), desired(roots, ItemNull), current(roots, ItemNull);
    if (update->flags & JS_PD_HAS_VALUE) {
        desired.set(js_retain_scalar(update->value));
        if (item_is_error(desired.get())) return desired.get();
    }
    JsPropertyDescriptor existing = {};
    Item found = js_module_namespace_get_own(object.get(), name.get(), &existing);
    if (item_is_error(found) || found.item == ITEM_FALSE) return found;
    current.set(existing.value);
    if (js_pd_is_accessor(update) ||
            ((update->flags & JS_PD_HAS_CONFIGURABLE) && js_pd_is_configurable(update)) ||
            ((update->flags & JS_PD_HAS_ENUMERABLE) && ((update->flags ^ existing.flags) & JS_PD_ENUMERABLE)) ||
            ((update->flags & JS_PD_HAS_WRITABLE) && ((update->flags ^ existing.flags) & JS_PD_WRITABLE)))
        return Item{.item = ITEM_FALSE};
    return Item{.item = b2it(!(update->flags & JS_PD_HAS_VALUE) ||
        record->ops->same_value(current.get(), desired.get()))};
}
Item js_module_namespace_project(Item owner, int64_t projection) {
    JsSourceModuleRecord* record = js_module_namespace_record(owner);
    if (!record) return ItemError;
    RootFrame roots(4);
    if (!roots.valid()) return ItemError;
    Rooted<Item> object(roots, owner), result(roots, ItemNull), value(roots, ItemNull), pair(roots, ItemNull);
    result.set(Item{.array = array()});
    int64_t length = record->export_cells->length + (projection == 5 ? 1 : 0);
    if (!result.get().array || !array_reserve_append_slots(result.get().array, length * 2)) return ItemError;
    for (int index = 0; index < record->export_cells->length; index++) {
        auto* cell = (JsSourceModuleExportCell*)record->export_cells->data[index];
        Item key = {.item = s2it(cell->name)};
        // OwnPropertyKeys never reads bindings; enumerable projections require descriptors.
        if (projection < 3 || projection == 4) {
            value.set(js_module_binding_get(&cell->binding));
            if (item_is_error(value.get())) return value.get();
        }
        if (projection == 0 || projection >= 3) value.set(key);
        if (projection == 2) {
            pair.set(Item{.array = array()});
            if (!pair.get().array || !array_reserve_append_slots(pair.get().array, 4)) return ItemError;
            array_push_verbatim(pair.get().array, key);
            array_push_verbatim(pair.get().array, value.get());
            if (pair.get().array->length != 2) return ItemError;
            value.set(pair.get());
        }
        array_push_verbatim(result.get().array, value.get());
    }
    if (projection == 5) {
        Item key = {.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))};
        array_push_verbatim(result.get().array, key);
    }
    return result.get().array->length == length ? result.get() : ItemError;
}
static Item namespace_vmap_get(void* data, Item key) {
    auto* record = (JsSourceModuleRecord*)data;
    return js_module_namespace_property(record->descriptor->namespace_obj, key, JS_MODULE_NAMESPACE_GET);
}
static void namespace_vmap_set(void*, Item, Item) {}
static int64_t namespace_vmap_count(void* data) {
    return ((JsSourceModuleRecord*)data)->export_cells->length;
}
static SymbolKeyList* namespace_vmap_keys(void* data) {
    auto* record = (JsSourceModuleRecord*)data;
    SymbolKeyList* keys = symbol_key_list_new(record->export_cells->length);
    if (!keys) return NULL;
    // SymbolKeyList already retains every appended name in its exact root chunks.
    for (int index = 0; index < record->export_cells->length; index++) {
        auto* cell = (JsSourceModuleExportCell*)record->export_cells->data[index];
        Symbol* symbol = name_key_symbol(cell->name->chars, cell->name->len);
        if (!symbol) { symbol_key_list_free(keys); return NULL; }
        if (!symbol_key_list_append(keys, symbol)) { symbol_key_list_free(keys); return NULL; }
    }
    return keys;
}
static Item namespace_vmap_key_at(void* data, int64_t index) {
    auto* record = (JsSourceModuleRecord*)data;
    return index >= 0 && index < record->export_cells->length
        ? Item{.item = s2it(((JsSourceModuleExportCell*)record->export_cells->data[index])->name)} : ItemNull;
}
static Item namespace_vmap_value_at(void* data, int64_t index) {
    auto* record = (JsSourceModuleRecord*)data;
    return index >= 0 && index < record->export_cells->length
        ? js_module_binding_get(&((JsSourceModuleExportCell*)record->export_cells->data[index])->binding) : ItemNull;
}
// The registry releases native records before heap retirement. Neither callback
// dereferences data: all guest Items already belong to descriptor/slab roots.
static void namespace_vmap_destroy(void*) {}
static void namespace_vmap_trace(void*, gc_heap*) {}
Item js_module_namespace_create(JsSourceModuleRecord* record) {
    if (!record) return ItemError;
    if (record->descriptor->namespace_obj.item != ITEM_NULL) return record->descriptor->namespace_obj;
    VMap* object = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!object) return ItemError;
    object->type_id = LMD_TYPE_VMAP;
    object->data = record;
    object->vtable = &source_namespace_vtable;
    record->descriptor->namespace_obj = Item{.vmap = object};
    return record->descriptor->namespace_obj;
}
Item js_module_import_meta(JsSourceModuleRecord* record) {
    if (!record || !record->ops->import_meta_create) return ItemError;
    if (record->import_meta_registered) return record->import_meta;
    // Publish the optional exact root before calling frontend allocation hooks.
    record->import_meta = ItemNull;
    if (!heap_try_register_gc_root_range(&record->import_meta.item, 1)) return ItemError;
    record->import_meta_registered = true;
    record->import_meta = record->ops->import_meta_create(record->owner, record);
    if (!item_is_error(record->import_meta)) return record->import_meta;
    Item error = record->import_meta;
    heap_unregister_gc_root_range(&record->import_meta.item);
    record->import_meta_registered = false; record->import_meta = ItemNull;
    return error;
}
