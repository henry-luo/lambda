#include "js_interp.hpp"

#include "js_interp_env.h"
#include "js_builtin_catalog.hpp"
#include "js_property_attrs.h"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/heap_api.h"
#include "../runtime/runtime-state.h"
#include "../runtime/side_stack.h"
#include "../../lib/log.h"
#include "../../lib/mempool.h"

extern __thread EvalContext* context;
extern Item js_make_number(double value);
extern "C" Item bigint_from_string(const char* value, int length);

enum JsInterpCompletionKind : uint8_t {
    JS_INTERP_NORMAL,
    JS_INTERP_RETURN,
    JS_INTERP_THROW,
    JS_INTERP_BREAK,
    JS_INTERP_CONTINUE,
};

struct JsInterpCompletion {
    JsInterpCompletionKind kind;
    Item value;
    const char* label;
    int label_len;
};

struct JsInterpFrame {
    JsScript* script;
    JsInterpEnv* env;
    // `this` survives arbitrary nested evaluation; keep its canonical Item in
    // the side-root window rather than a native-stack copy.
    uint64_t* this_home;
    bool strict;
    const char* active_label;
    int active_label_len;
};

struct JsInterpReference {
    NameEntry* entry;
    // Assignment/update callers provide these exact root slots. A property
    // reference must keep both operands live while evaluating its RHS.
    uint64_t* object_home;
    uint64_t* key_home;
    bool property;
    bool with_binding;
};

struct JsInterpEnvRoot {
    JsInterpEnv* env;
    bool registered;

    explicit JsInterpEnvRoot(JsInterpEnv* value)
        : env(value), registered(value && heap_try_register_gc_object_root(value)) {}
    ~JsInterpEnvRoot() {
        if (registered) heap_unregister_gc_object_root(env);
    }
    void replace_with(JsInterpEnvRoot* replacement) {
        if (!replacement) return;
        if (registered) heap_unregister_gc_object_root(env);
        env = replacement->env;
        registered = replacement->registered;
        replacement->registered = false;
    }
    JsInterpEnvRoot(const JsInterpEnvRoot&) = delete;
    JsInterpEnvRoot& operator=(const JsInterpEnvRoot&) = delete;
};

static JsInterpCompletion js_interp_normal(Item value) {
    return {JS_INTERP_NORMAL, value, NULL, 0};
}

static JsInterpCompletion js_interp_throw(Item value) {
    return {JS_INTERP_THROW, value, NULL, 0};
}

static bool js_interp_completion_targets_active_label(
        const JsInterpCompletion* completion, const JsInterpFrame* frame) {
    if (!completion || completion->label_len == 0) return true;
    return frame && frame->active_label &&
        completion->label_len == frame->active_label_len &&
        memcmp(completion->label, frame->active_label,
            (size_t)completion->label_len) == 0;
}

static Item js_interp_frame_this(const JsInterpFrame* frame) {
    return frame && frame->this_home ? (Item){.item = *frame->this_home}
        : make_js_undefined();
}

static Item js_interp_reference_object(const JsInterpReference* reference) {
    return reference && reference->object_home
        ? (Item){.item = *reference->object_home} : ItemNull;
}

static Item js_interp_reference_key(const JsInterpReference* reference) {
    return reference && reference->key_home
        ? (Item){.item = *reference->key_home} : ItemNull;
}

static bool js_interp_name_equals(const String* name, const char* chars) {
    if (!name || !chars) return false;
    size_t length = strlen(chars);
    return name->len == length && memcmp(name->chars, chars, length) == 0;
}

static Item js_interp_name_key(const String* name) {
    return name ? (Item){.item = s2it((String*)name)} : ItemNull;
}

static int js_interp_scope_slot_count(NameScope* scope) {
    if (!scope) return 0;
    int count = 0;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        entry->slot = count++;
        entry->storage_assigned = true;
    }
    return count;
}

static JsInterpEnv* js_interp_env_create(NameScope* scope, JsInterpEnv* outer) {
    if (!context || !context->heap || !context->heap->gc || !scope) return NULL;
    int count = js_interp_scope_slot_count(scope);
    size_t size = sizeof(JsInterpEnv);
    if (count > 1) size += (size_t)(count - 1) * sizeof(uint64_t);
    JsInterpEnv* env = (JsInterpEnv*)gc_heap_calloc(context->heap->gc, size,
        GC_TYPE_JS_INTERP_ENV);
    if (!env) return NULL;
    env->outer = outer;
    env->scope = scope;
    env->slot_count = (uint32_t)count;
    return env;
}

static JsInterpEnv* js_interp_env_clone(JsInterpEnv* source) {
    if (!source) return NULL;
    JsInterpEnv* copy = js_interp_env_create(source->scope, source->outer);
    if (!copy || copy->slot_count != source->slot_count) return NULL;
    if (copy->slot_count) {
        memcpy(copy->slots, source->slots, (size_t)copy->slot_count * sizeof(uint64_t));
    }
    return copy;
}

static JsInterpEnv* js_interp_find_env(JsInterpEnv* env, NameScope* scope) {
    for (JsInterpEnv* scan = env; scan; scan = scan->outer) {
        if (scan->scope == scope) return scan;
    }
    return NULL;
}

static Item js_interp_tdz_error(String* name) {
    return js_throw_reference_error(js_make_string_len(name ? name->chars : "",
        name ? (int)name->len : 0));
}

static Item js_interp_read_binding(JsInterpFrame* frame, NameEntry* entry,
        String* unresolved_name) {
    if (!frame) return ItemError;
    if (unresolved_name && js_interp_name_equals(unresolved_name, "this")) {
        return js_interp_frame_this(frame);
    }
    // Object Environment Records sit in front of lexical bindings. Probe
    // before reading the static NameEntry so an outer TDZ does not mask a
    // visible `with` property.
    if (unresolved_name && js_with_depth_active()) {
        Item key = js_interp_name_key(unresolved_name);
        Item visible = js_probe_with_binding(key);
        if (item_is_error(visible)) return visible;
        if (js_is_truthy(visible)) {
            return js_get_with_binding_or_fallback(key, make_js_undefined());
        }
    }
    if (unresolved_name) {
        // A direct eval may introduce a function-scoped var which was absent
        // from this script's static NameScope. The shared eval journal is the
        // authoritative extension of that function environment.
        Item key = js_interp_name_key(unresolved_name);
        if (js_eval_local_has_var_binding(key)) {
            return js_eval_local_get_binding_or_fallback(key, ItemError);
        }
    }
    if (!entry) {
        if (js_interp_name_equals(unresolved_name, "undefined")) return make_js_undefined();
        Item key = js_interp_name_key(unresolved_name);
        if (!js_global_binding_exists(key)) return js_interp_tdz_error(unresolved_name);
        return js_get_global_property(key);
    }
    Item value = ItemNull;
    if (entry->scope == frame->script->global_scope) {
        value = js_get_module_var(entry->slot);
    } else {
        JsInterpEnv* env = js_interp_find_env(frame->env, entry->scope);
        if (!env || entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count) {
            return ItemError;
        }
        value.item = env->slots[entry->slot];
    }
    return value.item == ITEM_JS_TDZ ? js_interp_tdz_error(entry->name) : value;
}

static Item js_interp_write_binding(JsInterpFrame* frame, NameEntry* entry,
        String* unresolved_name, Item value, bool initialize) {
    if (!frame) return ItemError;
    String* name = entry ? entry->name : unresolved_name;
    Item key = js_interp_name_key(name);
    // `var`/parameter bindings may have been supplied by a previous direct
    // eval. Keep subsequent interpreted writes in the shared function journal
    // instead of accidentally materializing a realm-global property.
    if (!initialize && (!entry || !entry->is_const) &&
            js_eval_local_has_var_binding(key)) {
        js_eval_local_export_var(key, value);
        return value;
    }
    if (!entry) {
        return js_set_global_property(key, value, frame->strict ? 1 : 0);
    }
    Item current = ItemNull;
    if (entry->scope == frame->script->global_scope) {
        current = js_get_module_var(entry->slot);
        if (!initialize && current.item == ITEM_JS_TDZ) {
            return js_interp_tdz_error(entry->name);
        }
        if (!initialize && entry->is_const) {
            return js_throw_const_assign(entry->name ? name_ref_id(entry->name) : NAME_ID_NONE,
                entry->name ? (int)entry->name->len : 0);
        }
        js_set_module_var(entry->slot, value);
        if (!entry->is_lexical) {
            js_define_global_var_property(key, value);
        } else if (initialize) {
            js_global_lexical_declare(key, value, entry->is_const ? 1 : 0);
        } else {
            Item set_result = js_global_lexical_set_if_exists(key, value);
            if (item_is_error(set_result)) return set_result;
        }
        return value;
    }
    JsInterpEnv* env = js_interp_find_env(frame->env, entry->scope);
    if (!env || entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count) {
        return ItemError;
    }
    current.item = env->slots[entry->slot];
    if (!initialize && current.item == ITEM_JS_TDZ) {
        return js_interp_tdz_error(entry->name);
    }
    if (!initialize && entry->is_const) {
        return js_throw_const_assign(entry->name ? name_ref_id(entry->name) : NAME_ID_NONE,
            entry->name ? (int)entry->name->len : 0);
    }
    env->slots[entry->slot] = value.item;
    return value;
}

static int js_interp_function_param_count(const JsFunctionNode* function) {
    int count = 0;
    for (const AstNode* param = function ? function->params : NULL; param;
            param = param->next) {
        count++;
    }
    return count;
}

static Item js_interp_make_function(JsInterpFrame* frame, JsFunctionNode* function) {
    if (!frame || !function || function->is_async || function->is_generator) {
        return js_throw_type_error("unsupported interpreted function form");
    }
    uint32_t flags = 0;
    if (function->is_arrow) flags |= JS_FUNC_FLAG_ARROW;
    if (frame->strict || function->has_use_strict_directive) flags |= JS_FUNC_FLAG_STRICT;
    return js_new_interpreted_function(function, frame->script, frame->env,
        js_interp_function_param_count(function), flags);
}

static Item js_interp_make_method(JsInterpFrame* frame, JsFunctionNode* function) {
    if (!frame || !function || function->is_async || function->is_generator) {
        return js_throw_type_error("unsupported interpreted method form");
    }
    // Class methods are strict and never expose [[Construct]]. The common
    // function factory still supplies their ordinary JS call capability.
    return js_new_interpreted_function(function, frame->script, frame->env,
        js_interp_function_param_count(function), JS_FUNC_FLAG_METHOD | JS_FUNC_FLAG_STRICT);
}

static Item js_interp_make_object_method(JsInterpFrame* frame,
        JsFunctionNode* function) {
    if (!frame || !function || function->is_async || function->is_generator) {
        return js_throw_type_error("unsupported interpreted object method form");
    }
    uint32_t flags = JS_FUNC_FLAG_METHOD;
    if (frame->strict || function->has_use_strict_directive) flags |= JS_FUNC_FLAG_STRICT;
    return js_new_interpreted_function(function, frame->script, frame->env,
        js_interp_function_param_count(function), flags);
}

static Item js_interp_make_field_initializer(JsInterpFrame* frame,
        JsFieldDefinitionNode* field) {
    if (!frame || !frame->script || !frame->script->pool || !field || !field->value) {
        return ItemError;
    }
    JsFunctionNode* initializer = (JsFunctionNode*)pool_calloc(frame->script->pool,
        sizeof(JsFunctionNode));
    NameScope* scope = (NameScope*)pool_calloc(frame->script->pool, sizeof(NameScope));
    if (!initializer || !scope) return ItemError;
    // Keep an empty function environment between the initializer's `this`
    // and its captured defining environment. Evaluating the expression at
    // class definition would bind `this` to the wrong receiver.
    scope->kind = SCOPE_KIND_FUNCTION;
    scope->strict = true;
    initializer->node_type = JS_AST_NODE_FUNCTION_EXPRESSION;
    initializer->source_span = field->source_span;
    initializer->body = field->value;
    initializer->vars = scope;
    initializer->has_use_strict_directive = true;
    return js_new_interpreted_function(initializer, frame->script, frame->env,
        0, JS_FUNC_FLAG_METHOD | JS_FUNC_FLAG_STRICT);
}

static JsInterpCompletion js_interp_eval(JsInterpFrame* frame, JsAstNode* node);
static JsInterpCompletion js_interp_exec(JsInterpFrame* frame, JsAstNode* node);

static NameEntry* js_interp_find_binding(JsInterpFrame* frame, String* name) {
    if (!frame || !name) return NULL;
    for (JsInterpEnv* env = frame->env; env; env = env->outer) {
        for (NameEntry* entry = env->scope ? env->scope->first : NULL;
                entry; entry = entry->next) {
            if (entry->name && entry->name->len == name->len &&
                    memcmp(entry->name->chars, name->chars, name->len) == 0) {
                return entry;
            }
        }
    }
    for (NameEntry* entry = frame->script && frame->script->global_scope
            ? frame->script->global_scope->first : NULL;
            entry; entry = entry->next) {
        if (entry->name && entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return entry;
        }
    }
    return NULL;
}

static JsInterpCompletion js_interp_class_key(JsInterpFrame* frame,
        JsAstNode* key, bool computed) {
    if (!key) return js_interp_throw(js_throw_type_error("class member has no name"));
    if (!computed && key->node_type == AST_NODE_IDENT) {
        return js_interp_normal(js_interp_name_key(((JsIdentifierNode*)key)->name));
    }
    JsInterpCompletion value = js_interp_eval(frame, key);
    if (value.kind != JS_INTERP_NORMAL) return value;
    Item property_key = js_to_property_key(value.value);
    return item_is_error(property_key) ? js_interp_throw(property_key)
        : js_interp_normal(property_key);
}

static JsInterpCompletion js_interp_eval_class(JsInterpFrame* frame,
        JsClassNode* cls, bool declaration) {
    if (!frame || !cls) return js_interp_throw(ItemError);
    RootFrame roots(7);
    Rooted<Item> class_root(roots, js_new_class_function());
    Rooted<Item> prototype_root(roots, ItemNull);
    Rooted<Item> super_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> target_root(roots, ItemNull);
    Rooted<Item> method_root(roots, ItemNull);
    if (item_is_error(class_root.get())) return js_interp_throw(class_root.get());

    prototype_root.set(js_get_key_cstr(class_root.get(), "prototype"));
    if (item_is_error(prototype_root.get())) return js_interp_throw(prototype_root.get());
    if (get_type_id(prototype_root.get()) != LMD_TYPE_MAP) {
        prototype_root.set(js_new_object());
        if (item_is_error(prototype_root.get())) return js_interp_throw(prototype_root.get());
        key_root.set(js_make_string("prototype"));
        Item published = js_set_key_default(class_root.get(), key_root.get(), prototype_root.get());
        if (item_is_error(published)) return js_interp_throw(published);
    }
    js_set_class_instance_prototype(class_root.get(), prototype_root.get());
    js_set_default_constructor_property(prototype_root.get(), class_root.get());
    if (cls->name) {
        value_root.set(js_make_string_len(cls->name->chars, (int)cls->name->len));
        js_set_class_name(class_root.get(), value_root.get());
    }

    if (cls->superclass) {
        JsInterpCompletion heritage = js_interp_eval(frame, (JsAstNode*)cls->superclass);
        if (heritage.kind != JS_INTERP_NORMAL) return heritage;
        super_root.set(heritage.value);
        if (get_type_id(super_root.get()) != LMD_TYPE_NULL) {
            Item constructable = js_is_constructor(super_root.get());
            if (item_is_error(constructable)) return js_interp_throw(constructable);
            if (!js_is_truthy(constructable)) {
                return js_interp_throw(js_throw_type_error(
                    "Class extends value is not a constructor or null"));
            }
            value_root.set(js_get_key_cstr(super_root.get(), "prototype"));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            TypeId parent_type = get_type_id(value_root.get());
            if (parent_type != LMD_TYPE_MAP && parent_type != LMD_TYPE_FUNC &&
                    !js_is_js_array(value_root.get()) && parent_type != LMD_TYPE_ELEMENT) {
                return js_interp_throw(js_throw_type_error(
                    "Class extends value has invalid prototype property"));
            }
            js_set_prototype(prototype_root.get(), value_root.get());
        } else {
            js_set_prototype(prototype_root.get(), ItemNull);
        }
        js_set_prototype(class_root.get(), super_root.get());
        js_set_class_superclass(class_root.get(), super_root.get());
    }

    // A declaration becomes visible after heritage setup, before static
    // element evaluation. That lets static fields/blocks close over the class
    // binding while preserving the pre-heritage TDZ.
    if (declaration && cls->name) {
        NameEntry* entry = js_interp_find_binding(frame, cls->name);
        Item stored = js_interp_write_binding(frame, entry, cls->name,
            class_root.get(), true);
        if (item_is_error(stored)) return js_interp_throw(stored);
    }

    int instance_field_count = 0;
    for (JsAstNode* member = cls->body ? (JsAstNode*)((JsBlockNode*)cls->body)->statements
            : NULL; member; member = (JsAstNode*)member->next) {
        if (member->node_type == JS_AST_NODE_FIELD_DEFINITION &&
                !((JsFieldDefinitionNode*)member)->is_static) instance_field_count++;
    }
    if (instance_field_count > 0) {
        js_init_class_instance_field_metadata(class_root.get(), instance_field_count);
    }
    int instance_field_index = 0;
    JsInterpFrame static_frame = *frame;
    static_frame.this_home = class_root.home();
    for (JsAstNode* member = cls->body ? (JsAstNode*)((JsBlockNode*)cls->body)->statements
            : NULL; member; member = (JsAstNode*)member->next) {
        if (member->node_type == JS_AST_NODE_METHOD_DEFINITION) {
            JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)member;
            JsInterpCompletion key = js_interp_class_key(frame, (JsAstNode*)method->key,
                method->computed);
            if (key.kind != JS_INTERP_NORMAL) return key;
            key_root.set(key.value);
            method_root.set(js_interp_make_method(frame, (JsFunctionNode*)method));
            if (item_is_error(method_root.get())) return js_interp_throw(method_root.get());
            js_set_function_home_class(method_root.get(), class_root.get());
            if (method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR) {
                if (method->static_method || cls->superclass) {
                    return js_interp_throw(js_throw_type_error(
                        "unsupported interpreted derived class constructor"));
                }
                js_set_class_constructor(class_root.get(), method_root.get());
                js_set_formal_length(class_root.get(), js_interp_function_param_count(
                    (JsFunctionNode*)method));
                continue;
            }
            target_root.set(method->static_method ? class_root.get() : prototype_root.get());
            Item installed = (method->kind == JsMethodDefinitionNode::JS_METHOD_GET ||
                    method->kind == JsMethodDefinitionNode::JS_METHOD_SET)
                ? js_define_accessor_partial(target_root.get(), key_root.get(), method_root.get(),
                    method->kind == JsMethodDefinitionNode::JS_METHOD_SET ? 1 : 0,
                    JSPD_NON_ENUMERABLE)
                : js_create_data_property(target_root.get(), key_root.get(), method_root.get());
            if (item_is_error(installed)) return js_interp_throw(installed);
            if (method->kind != JsMethodDefinitionNode::JS_METHOD_GET &&
                    method->kind != JsMethodDefinitionNode::JS_METHOD_SET) {
                js_mark_non_enumerable(target_root.get(), key_root.get());
            }
            continue;
        }
        if (member->node_type == JS_AST_NODE_FIELD_DEFINITION) {
            JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)member;
            if (field->is_private) return js_interp_throw(js_throw_type_error(
                "unsupported interpreted private field"));
            JsInterpCompletion key = js_interp_class_key(frame, (JsAstNode*)field->key,
                field->computed);
            if (key.kind != JS_INTERP_NORMAL) return key;
            key_root.set(key.value);
            if (!field->is_static) {
                js_set_class_instance_field_metadata_key(class_root.get(),
                    instance_field_index, key_root.get());
                if (field->value) {
                    method_root.set(js_interp_make_field_initializer(frame, field));
                    if (item_is_error(method_root.get())) return js_interp_throw(method_root.get());
                    js_set_class_instance_field_metadata_initializer(class_root.get(),
                        instance_field_index, method_root.get());
                } else {
                    js_set_class_instance_field_metadata_value(class_root.get(),
                        instance_field_index, make_js_undefined());
                }
                instance_field_index++;
                continue;
            }
            if (field->value) {
                JsInterpCompletion value = js_interp_eval(&static_frame,
                    (JsAstNode*)field->value);
                if (value.kind != JS_INTERP_NORMAL) return value;
                value_root.set(value.value);
            } else {
                value_root.set(make_js_undefined());
            }
            Item installed = js_create_data_property(class_root.get(), key_root.get(),
                value_root.get());
            if (item_is_error(installed)) return js_interp_throw(installed);
            continue;
        }
        if (member->node_type == JS_AST_NODE_STATIC_BLOCK) {
            JsStaticBlockNode* block = (JsStaticBlockNode*)member;
            JsInterpCompletion completion = block->body
                ? js_interp_exec(&static_frame, block->body)
                : js_interp_normal(make_js_undefined());
            if (completion.kind != JS_INTERP_NORMAL) return completion;
            continue;
        }
        return js_interp_throw(js_throw_type_error("unsupported interpreted class member"));
    }
    js_mark_all_non_enumerable(prototype_root.get());
    return js_interp_normal(class_root.get());
}

static Item js_interp_property_key(JsInterpFrame* frame, JsMemberNode* member) {
    if (!member) return ItemError;
    if (!member->computed && member->property &&
            member->property->node_type == AST_NODE_IDENT) {
        return js_interp_name_key(((JsIdentifierNode*)member->property)->name);
    }
    RootFrame roots(1);
    Rooted<Item> key_root(roots, ItemNull);
    JsInterpCompletion key = js_interp_eval(frame, (JsAstNode*)member->property);
    if (key.kind != JS_INTERP_NORMAL) return key.value;
    key_root.set(key.value);
    return js_to_property_key(key_root.get());
}

static JsInterpCompletion js_interp_eval_reference(JsInterpFrame* frame,
        JsAstNode* node, JsInterpReference* out_reference,
        uint64_t* object_home, uint64_t* key_home) {
    if (!out_reference) return js_interp_throw(ItemError);
    memset(out_reference, 0, sizeof(*out_reference));
    out_reference->object_home = object_home;
    out_reference->key_home = key_home;
    if (node && node->node_type == AST_NODE_IDENT) {
        JsIdentifierNode* identifier = (JsIdentifierNode*)node;
        out_reference->entry = identifier->entry;
        Item key = js_interp_name_key(identifier->name);
        if (key_home) *key_home = key.item;
        if (js_with_depth_active()) {
            Item captured = js_capture_with_binding(key);
            if (item_is_error(captured)) return js_interp_throw(captured);
            out_reference->with_binding = js_is_truthy(captured);
        }
        return js_interp_normal(ItemNull);
    }
    if (node && (node->node_type == AST_NODE_MEMBER_EXPR ||
            node->node_type == AST_NODE_INDEX_EXPR)) {
        JsMemberNode* member = (JsMemberNode*)node;
        RootFrame roots(2);
        Rooted<Item> object_root(roots, ItemNull);
        Rooted<Item> key_root(roots, ItemNull);
        JsInterpCompletion object = js_interp_eval(frame, (JsAstNode*)member->object);
        if (object.kind != JS_INTERP_NORMAL) return object;
        object_root.set(object.value);
        Item key = js_interp_property_key(frame, member);
        if (item_is_error(key)) return js_interp_throw(key);
        key_root.set(key);
        if (object_home) *object_home = object_root.get().item;
        if (key_home) *key_home = key_root.get().item;
        out_reference->property = true;
        return js_interp_normal(ItemNull);
    }
    return js_interp_throw(js_throw_type_error("invalid assignment target"));
}

static Item js_interp_reference_read(JsInterpFrame* frame,
        const JsInterpReference* reference) {
    if (!reference) return ItemError;
    if (reference->with_binding) {
        return js_get_with_binding_or_fallback(js_interp_reference_key(reference),
            make_js_undefined());
    }
    if (!reference->property) {
        return js_interp_read_binding(frame, reference->entry,
            reference->entry ? reference->entry->name
                : it2s(js_interp_reference_key(reference)));
    }
    return js_get_key_default(js_interp_reference_object(reference),
        js_interp_reference_key(reference));
}

static Item js_interp_reference_write(JsInterpFrame* frame,
        const JsInterpReference* reference, Item value, bool initialize) {
    if (!reference) return ItemError;
    if (reference->with_binding) {
        Item written = js_set_last_with_binding_if_valid(
            js_interp_reference_key(reference), value, frame->strict ? 1 : 0);
        if (item_is_error(written)) return written;
        if (js_is_truthy(written)) return value;
    }
    if (!reference->property) {
        return js_interp_write_binding(frame, reference->entry,
            reference->entry ? NULL : it2s(js_interp_reference_key(reference)), value, initialize);
    }
    return js_set_key_policy(js_interp_reference_object(reference),
        js_interp_reference_key(reference), value,
        frame->strict ? 1 : 0);
}

static Item js_interp_binary(Operator op, Item left, Item right) {
    switch (op) {
    case OPERATOR_ADD: return js_add(left, right);
    case OPERATOR_SUB: return js_subtract(left, right);
    case OPERATOR_MUL: return js_multiply(left, right);
    case OPERATOR_DIV: return js_divide(left, right);
    case OPERATOR_MOD: return js_modulo(left, right);
    case OPERATOR_POW:
    case OPERATOR_JS_EXP: return js_power(left, right);
    case OPERATOR_EQ: return js_equal(left, right);
    case OPERATOR_NE: return js_logical_not(js_equal(left, right));
    case OPERATOR_JS_STRICT_EQ: return js_strict_equal(left, right);
    case OPERATOR_JS_STRICT_NE: return js_logical_not(js_strict_equal(left, right));
    case OPERATOR_LT: return js_less_than(left, right);
    case OPERATOR_LE: return js_compare(OPERATOR_LE, left, right);
    case OPERATOR_GT: return js_greater_than(left, right);
    case OPERATOR_GE: return js_compare(OPERATOR_GE, left, right);
    case OPERATOR_JS_BIT_AND: return js_bitwise_and(left, right);
    case OPERATOR_JS_BIT_OR: return js_bitwise_or(left, right);
    case OPERATOR_JS_BIT_XOR: return js_bitwise_xor(left, right);
    case OPERATOR_JS_LSHIFT: return js_left_shift(left, right);
    case OPERATOR_JS_RSHIFT: return js_right_shift(left, right);
    case OPERATOR_JS_URSHIFT: return js_unsigned_right_shift(left, right);
    case OPERATOR_IN: return js_in(left, right);
    case OPERATOR_JS_INSTANCEOF: return js_instanceof(left, right);
    default: return js_throw_type_error("unsupported interpreted binary operator");
    }
}

static Item js_interp_assignment_value(Operator op, Item old_value, Item value) {
    switch (op) {
    case OPERATOR_ASSIGN: return value;
    case OPERATOR_JS_ADD_ASSIGN: return js_add(old_value, value);
    case OPERATOR_JS_SUB_ASSIGN: return js_subtract(old_value, value);
    case OPERATOR_JS_MUL_ASSIGN: return js_multiply(old_value, value);
    case OPERATOR_JS_DIV_ASSIGN: return js_divide(old_value, value);
    case OPERATOR_JS_MOD_ASSIGN: return js_modulo(old_value, value);
    case OPERATOR_JS_EXP_ASSIGN: return js_power(old_value, value);
    case OPERATOR_JS_BIT_AND_ASSIGN: return js_bitwise_and(old_value, value);
    case OPERATOR_JS_BIT_OR_ASSIGN: return js_bitwise_or(old_value, value);
    case OPERATOR_JS_BIT_XOR_ASSIGN: return js_bitwise_xor(old_value, value);
    case OPERATOR_JS_LSHIFT_ASSIGN: return js_left_shift(old_value, value);
    case OPERATOR_JS_RSHIFT_ASSIGN: return js_right_shift(old_value, value);
    case OPERATOR_JS_URSHIFT_ASSIGN: return js_unsigned_right_shift(old_value, value);
    default: return js_throw_type_error("unsupported interpreted assignment operator");
    }
}

static bool js_interp_is_nullish(Item value) {
    return get_type_id(value) == LMD_TYPE_NULL ||
        get_type_id(value) == LMD_TYPE_UNDEFINED;
}

static bool js_interp_is_logical_assignment(Operator op) {
    return op == OPERATOR_JS_AND_ASSIGN || op == OPERATOR_JS_OR_ASSIGN ||
        op == OPERATOR_JS_NULLISH_ASSIGN;
}

static bool js_interp_logical_assignment_keeps_old(Operator op, Item old_value) {
    if (op == OPERATOR_JS_AND_ASSIGN) return !js_is_truthy(old_value);
    if (op == OPERATOR_JS_OR_ASSIGN) return js_is_truthy(old_value);
    return op == OPERATOR_JS_NULLISH_ASSIGN && !js_interp_is_nullish(old_value);
}

static bool js_interp_scope_has_name(NameScope* scope, String* name) {
    if (!scope || !name) return false;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (entry->name && entry->name->len == name->len &&
                memcmp(entry->name->chars, name->chars, name->len) == 0) {
            return true;
        }
    }
    return false;
}

static Item js_interp_binding_raw_value(JsInterpFrame* frame, NameEntry* entry) {
    if (!frame || !entry) return ItemError;
    if (entry->scope == frame->script->global_scope) {
        return js_get_module_var(entry->slot);
    }
    JsInterpEnv* env = js_interp_find_env(frame->env, entry->scope);
    if (!env || entry->slot < 0 || (uint32_t)entry->slot >= env->slot_count) {
        return ItemError;
    }
    return (Item){.item = env->slots[entry->slot]};
}

static Item js_interp_sync_global_bindings(JsInterpFrame* frame) {
    if (!frame || !frame->script || !frame->script->global_scope) return ItemError;
    for (NameEntry* entry = frame->script->global_scope->first; entry;
            entry = entry->next) {
        if (!entry->name) continue;
        Item key = js_interp_name_key(entry->name);
        Item fallback = js_get_module_var(entry->slot);
        Item value = entry->is_lexical
            ? js_global_lexical_get_or_fallback(key, fallback)
            : js_get_global_property(key);
        if (item_is_error(value)) return value;
        js_set_module_var(entry->slot, value);
    }
    return js_status_ok();
}

static void js_interp_eval_bind_scope(JsInterpFrame* frame, NameScope* scope,
        bool global_lexical) {
    if (!frame || !scope) return;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (!entry->name) continue;
        Item key = js_interp_name_key(entry->name);
        Item value = js_interp_binding_raw_value(frame, entry);
        if (global_lexical) {
            js_eval_global_lexical_bind(key, value);
        } else {
            js_eval_env_bind(key, value);
        }
    }
}

static void js_interp_eval_bind_envs(JsInterpFrame* frame, JsInterpEnv* env,
        bool global_lexical) {
    if (!env) return;
    js_interp_eval_bind_envs(frame, env->outer, global_lexical);
    js_interp_eval_bind_scope(frame, env->scope, global_lexical);
}

static bool js_interp_env_name_shadowed_before(JsInterpEnv* inner,
        JsInterpEnv* target, String* name) {
    for (JsInterpEnv* env = inner; env && env != target; env = env->outer) {
        if (js_interp_scope_has_name(env->scope, name)) return true;
    }
    return false;
}

static Item js_interp_eval_writeback_scope(JsInterpFrame* frame,
        NameScope* scope, JsInterpEnv* scope_env) {
    if (!frame || !scope) return ItemError;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (!entry->name || entry->is_const ||
                (scope_env && js_interp_env_name_shadowed_before(frame->env,
                    scope_env, entry->name))) {
            continue;
        }
        Item key = js_interp_name_key(entry->name);
        RootFrame roots(1);
        Rooted<Item> value_root(roots, js_get_global_property(key));
        if (item_is_error(value_root.get())) return value_root.get();
        Item written = js_interp_write_binding(frame, entry, entry->name,
            value_root.get(), false);
        if (item_is_error(written)) return written;
    }
    return js_status_ok();
}

static Item js_interp_eval_writeback_envs(JsInterpFrame* frame) {
    if (!frame) return ItemError;
    for (JsInterpEnv* env = frame->env; env; env = env->outer) {
        Item status = js_interp_eval_writeback_scope(frame, env->scope, env);
        if (item_is_error(status)) return status;
    }
    return js_status_ok();
}

static bool js_interp_frame_has_function_scope(const JsInterpFrame* frame) {
    for (JsInterpEnv* env = frame ? frame->env : NULL; env; env = env->outer) {
        if (env->scope && env->scope->kind == SCOPE_KIND_FUNCTION) return true;
    }
    return false;
}

static void js_interp_eval_note_lexicals(JsInterpEnv* env) {
    if (!env) return;
    js_interp_eval_note_lexicals(env->outer);
    for (NameEntry* entry = env->scope ? env->scope->first : NULL;
            entry; entry = entry->next) {
        if (!entry->name || !entry->is_lexical) continue;
        Item key = js_interp_name_key(entry->name);
        js_eval_local_note_lexical_binding(key);
        if (entry->is_const) js_eval_local_note_immutable_binding(key);
    }
}

struct JsInterpEvalBridge {
    JsInterpFrame* frame;
    bool global_lexical;
    bool active;

    explicit JsInterpEvalBridge(JsInterpFrame* value)
        : frame(value), global_lexical(!js_interp_frame_has_function_scope(value)),
          active(false) {
        if (!frame) return;
        if (global_lexical) {
            js_eval_global_lexical_push_frame();
            js_interp_eval_bind_scope(frame, frame->script->global_scope, true);
            js_interp_eval_bind_envs(frame, frame->env, true);
        } else {
            js_eval_env_push_frame();
            js_interp_eval_bind_envs(frame, frame->env, false);
            // Preserve vars created by a prior direct eval in this activation.
            js_eval_env_bridge_journal_vars();
        }
        active = true;
    }

    Item writeback() {
        if (!active) return ItemError;
        // A global direct eval shares the caller module slab. Its source can
        // update global lexical slots directly, while the temporary property
        // bridge still carries block-scoped bindings; writing the global scope
        // back from that property would overwrite the authoritative slab.
        return js_interp_eval_writeback_envs(frame);
    }

    void close() {
        if (!active) return;
        if (global_lexical) js_eval_global_lexical_pop_frame();
        else js_eval_env_pop_frame();
        active = false;
    }

    ~JsInterpEvalBridge() { close(); }

    JsInterpEvalBridge(const JsInterpEvalBridge&) = delete;
    JsInterpEvalBridge& operator=(const JsInterpEvalBridge&) = delete;
};

static Item js_interp_direct_eval(JsInterpFrame* frame, Item code) {
    if (frame && !frame->env) {
        // Script-level direct eval already has the realm's lexical and var
        // environments. Reuse that global path rather than projecting its
        // module slab through temporary object properties.
        RootFrame roots(1);
        Rooted<Item> result_root(roots, js_builtin_eval(code,
            1 | (frame->strict ? 4 : 0)));
        Item synced = js_interp_sync_global_bindings(frame);
        if (item_is_error(result_root.get())) return result_root.get();
        return item_is_error(synced) ? synced : result_root.get();
    }
    JsInterpEvalBridge bridge(frame);
    if (!bridge.active) return ItemError;
    RootFrame roots(1);
    Rooted<Item> result_root(roots, js_builtin_eval(code,
        3 | (frame && frame->strict ? 4 : 0)));
    Item writeback = bridge.writeback();
    bridge.close();
    if (item_is_error(result_root.get())) return result_root.get();
    return item_is_error(writeback) ? writeback : result_root.get();
}

struct JsInterpEvalLocalFrame {
    bool pushed;

    explicit JsInterpEvalLocalFrame(JsInterpEnv* env)
        : pushed(js_eval_local_push_frame() != 0) {
        if (pushed) js_interp_eval_note_lexicals(env);
    }
    ~JsInterpEvalLocalFrame() {
        if (pushed) js_eval_local_pop_frame();
    }
    JsInterpEvalLocalFrame(const JsInterpEvalLocalFrame&) = delete;
    JsInterpEvalLocalFrame& operator=(const JsInterpEvalLocalFrame&) = delete;
};

static bool js_interp_identifier_is(JsAstNode* node, const char* name);

static JsInterpCompletion js_interp_eval_call(JsInterpFrame* frame, JsCallNode* call,
        bool construct) {
    if (!call) return js_interp_throw(ItemError);
    RootFrame roots(6);
    Rooted<Item> callee_root(roots, ItemNull);
    Rooted<Item> this_root(roots, make_js_undefined());
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> arguments_root(roots, js_array_new(0));
    Rooted<Item> value_root(roots, ItemNull);
    Rooted<Item> spread_item_root(roots, ItemNull);
    if (item_is_error(arguments_root.get())) return js_interp_throw(arguments_root.get());
    if (call->function && (call->function->node_type == AST_NODE_MEMBER_EXPR ||
            call->function->node_type == AST_NODE_INDEX_EXPR)) {
        JsMemberNode* member = (JsMemberNode*)call->function;
        JsInterpCompletion receiver = js_interp_eval(frame, (JsAstNode*)member->object);
        if (receiver.kind != JS_INTERP_NORMAL) return receiver;
        this_root.set(receiver.value);
        if (member->optional && js_interp_is_nullish(this_root.get())) {
            return js_interp_normal(make_js_undefined());
        }
        key_root.set(js_interp_property_key(frame, member));
        if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
        callee_root.set(js_get_key_default(this_root.get(), key_root.get()));
    } else {
        JsInterpCompletion callee = js_interp_eval(frame, (JsAstNode*)call->function);
        if (callee.kind != JS_INTERP_NORMAL) return callee;
        callee_root.set(callee.value);
        if (call->function && call->function->node_type == AST_NODE_IDENT &&
                js_with_depth_active()) {
            this_root.set(js_get_last_with_binding_base_or_undefined(
                js_interp_name_key(((JsIdentifierNode*)call->function)->name)));
        }
    }
    if (call->optional && js_interp_is_nullish(callee_root.get())) {
        return js_interp_normal(make_js_undefined());
    }
    if (item_is_error(callee_root.get())) return js_interp_throw(callee_root.get());
    for (JsAstNode* arg = (JsAstNode*)call->arguments; arg; arg = (JsAstNode*)arg->next) {
        if (arg->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
            JsSpreadElementNode* spread = (JsSpreadElementNode*)arg;
            JsInterpCompletion source = js_interp_eval(frame,
                (JsAstNode*)spread->argument);
            if (source.kind != JS_INTERP_NORMAL) return source;
            value_root.set(source.value);
            value_root.set(js_iterable_to_array(value_root.get()));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            int64_t length = js_array_length(value_root.get());
            for (int64_t index = 0; index < length; index++) {
                spread_item_root.set(js_elements_get_int(value_root.get(), index));
                if (item_is_error(spread_item_root.get())) {
                    return js_interp_throw(spread_item_root.get());
                }
                Item pushed = js_array_push(arguments_root.get(), spread_item_root.get());
                if (item_is_error(pushed)) return js_interp_throw(pushed);
            }
            continue;
        }
        JsInterpCompletion value = js_interp_eval(frame, arg);
        if (value.kind != JS_INTERP_NORMAL) return value;
        value_root.set(value.value);
        Item pushed = js_array_push(arguments_root.get(), value_root.get());
        if (item_is_error(pushed)) return js_interp_throw(pushed);
    }
    if (!construct && call->function && call->function->node_type == AST_NODE_IDENT &&
            js_interp_identifier_is((JsAstNode*)call->function, "eval")) {
        Item intrinsic = js_get_global_builtin_fn_by_id(
            (Item){.item = i2it(JS_BUILTIN_GLOBAL_FN_EVAL)});
        if (js_strict_equal(callee_root.get(), intrinsic).item == b2it(true)) {
            value_root.set(js_elements_get_int(arguments_root.get(), 0));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            Item result = js_interp_direct_eval(frame, value_root.get());
            return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
        }
    }
    Item result = construct
        ? js_construct_array_like(callee_root.get(), arguments_root.get(), callee_root.get())
        : js_apply_function(callee_root.get(), this_root.get(), arguments_root.get());
    return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
}

static JsInterpCompletion js_interp_eval_template(JsInterpFrame* frame,
        JsTemplateLiteralNode* literal) {
    if (!literal) return js_interp_throw(ItemError);
    RootFrame roots(3);
    Rooted<Item> result_root(roots, js_make_string(""));
    Rooted<Item> chunk_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
    JsAstNode* expression = literal->expressions;
    for (JsAstNode* quasi = literal->quasis; quasi;
            quasi = (JsAstNode*)quasi->next) {
        if (quasi->node_type != JS_AST_NODE_TEMPLATE_ELEMENT) {
            return js_interp_throw(js_throw_type_error("invalid template element"));
        }
        JsTemplateElementNode* element = (JsTemplateElementNode*)quasi;
        if (!element->cooked) {
            return js_interp_throw(js_throw_syntax_error(
                js_make_string("invalid escape in template literal")));
        }
        chunk_root.set(js_make_string_len(element->cooked->chars,
            (int)element->cooked->len));
        result_root.set(js_add(result_root.get(), chunk_root.get()));
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        if (!element->tail) {
            if (!expression) {
                return js_interp_throw(js_throw_syntax_error(
                    js_make_string("missing template substitution")));
            }
            JsInterpCompletion substituted = js_interp_eval(frame, expression);
            if (substituted.kind != JS_INTERP_NORMAL) return substituted;
            value_root.set(substituted.value);
            result_root.set(js_add(result_root.get(), value_root.get()));
            if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
            expression = (JsAstNode*)expression->next;
        }
    }
    return expression ? js_interp_throw(js_throw_syntax_error(
        js_make_string("extra template substitution"))) : js_interp_normal(result_root.get());
}

static JsInterpCompletion js_interp_eval_tagged_template(JsInterpFrame* frame,
        JsTaggedTemplateNode* tagged) {
    if (!frame || !tagged || !tagged->quasi) return js_interp_throw(ItemError);
    int count = 0;
    for (JsAstNode* quasi = tagged->quasi->quasis; quasi;
            quasi = (JsAstNode*)quasi->next) count++;
    RootFrame roots(5);
    Rooted<Item> tag_root(roots, ItemNull);
    Rooted<Item> this_root(roots, make_js_undefined());
    Rooted<Item> key_root(roots, ItemNull);
    Rooted<Item> template_root(roots, ItemNull);
    Rooted<Item> arguments_root(roots, js_array_new(0));
    RootSpan cooked_roots((size_t)count);
    RootSpan raw_roots((size_t)count);
    Item* cooked = (Item*)(void*)cooked_roots.words();
    Item* raw = (Item*)(void*)raw_roots.words();
    if (item_is_error(arguments_root.get())) return js_interp_throw(arguments_root.get());

    if (tagged->tag->node_type == AST_NODE_MEMBER_EXPR ||
            tagged->tag->node_type == AST_NODE_INDEX_EXPR) {
        JsMemberNode* member = (JsMemberNode*)tagged->tag;
        JsInterpCompletion receiver = js_interp_eval(frame, (JsAstNode*)member->object);
        if (receiver.kind != JS_INTERP_NORMAL) return receiver;
        this_root.set(receiver.value);
        key_root.set(js_interp_property_key(frame, member));
        if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
        tag_root.set(js_get_key_default(this_root.get(), key_root.get()));
    } else {
        JsInterpCompletion tag = js_interp_eval(frame, tagged->tag);
        if (tag.kind != JS_INTERP_NORMAL) return tag;
        tag_root.set(tag.value);
        if (tagged->tag->node_type == AST_NODE_IDENT && js_with_depth_active()) {
            this_root.set(js_get_last_with_binding_base_or_undefined(
                js_interp_name_key(((JsIdentifierNode*)tagged->tag)->name)));
        }
    }
    if (item_is_error(tag_root.get())) return js_interp_throw(tag_root.get());

    int index = 0;
    for (JsAstNode* quasi = tagged->quasi->quasis; quasi;
            quasi = (JsAstNode*)quasi->next, index++) {
        if (quasi->node_type != JS_AST_NODE_TEMPLATE_ELEMENT) {
            return js_interp_throw(js_throw_syntax_error(
                js_make_string("invalid tagged template element")));
        }
        JsTemplateElementNode* element = (JsTemplateElementNode*)quasi;
        raw[index] = js_make_string_len(element->raw ? element->raw->chars : "",
            element->raw ? (int)element->raw->len : 0);
        cooked[index] = element->cooked
            ? js_make_string_len(element->cooked->chars, (int)element->cooked->len)
            : make_js_undefined();
        if (item_is_error(raw[index]) || item_is_error(cooked[index])) {
            return js_interp_throw(item_is_error(raw[index]) ? raw[index] : cooked[index]);
        }
    }
    int64_t site_id = ((int64_t)frame->script->index << 32) |
        (int64_t)tagged->source_span.start_byte;
    template_root.set(js_build_template_object_cached(cooked, raw, count, site_id));
    if (item_is_error(template_root.get())) return js_interp_throw(template_root.get());
    Item pushed = js_array_push(arguments_root.get(), template_root.get());
    if (item_is_error(pushed)) return js_interp_throw(pushed);
    for (JsAstNode* expression = tagged->quasi->expressions; expression;
            expression = (JsAstNode*)expression->next) {
        JsInterpCompletion value = js_interp_eval(frame, expression);
        if (value.kind != JS_INTERP_NORMAL) return value;
        pushed = js_array_push(arguments_root.get(), value.value);
        if (item_is_error(pushed)) return js_interp_throw(pushed);
    }
    Item result = js_apply_function(tag_root.get(), this_root.get(), arguments_root.get());
    return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
}

static bool js_interp_is_undefined(Item value) {
    return value.item == ITEM_JS_UNDEFINED || get_type_id(value) == LMD_TYPE_UNDEFINED;
}

static JsInterpCompletion js_interp_bind_pattern(JsInterpFrame* frame,
        JsAstNode* pattern, Item input, bool initialize) {
    if (!pattern) return js_interp_throw(js_throw_type_error("missing binding pattern"));
    switch (pattern->node_type) {
    case AST_NODE_IDENT: {
        JsIdentifierNode* identifier = (JsIdentifierNode*)pattern;
        Item stored = js_interp_write_binding(frame, identifier->entry,
            identifier->name, input, initialize);
        return item_is_error(stored) ? js_interp_throw(stored) : js_interp_normal(stored);
    }
    case JS_AST_NODE_ASSIGNMENT_PATTERN: {
        JsAssignmentPatternNode* assignment = (JsAssignmentPatternNode*)pattern;
        RootFrame roots(1);
        Rooted<Item> value_root(roots, input);
        if (js_interp_is_undefined(value_root.get())) {
            JsInterpCompletion fallback = js_interp_eval(frame,
                (JsAstNode*)assignment->right);
            if (fallback.kind != JS_INTERP_NORMAL) return fallback;
            value_root.set(fallback.value);
        }
        return js_interp_bind_pattern(frame, (JsAstNode*)assignment->left,
            value_root.get(), initialize);
    }
    case JS_AST_NODE_ARRAY_PATTERN: {
        JsArrayPatternNode* array = (JsArrayPatternNode*)pattern;
        RootFrame roots(3);
        Rooted<Item> source_root(roots, input);
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> rest_root(roots, ItemNull);
        source_root.set(js_iterable_to_array(source_root.get()));
        if (item_is_error(source_root.get())) return js_interp_throw(source_root.get());
        int64_t length = js_array_length(source_root.get());
        int64_t index = 0;
        for (JsAstNode* element = (JsAstNode*)array->elements; element;
                element = (JsAstNode*)element->next) {
            if (element->node_type == AST_NODE_NULL) {
                index++;
                continue;
            }
            if (element->node_type == JS_AST_NODE_REST_ELEMENT ||
                    element->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* rest = (JsSpreadElementNode*)element;
                rest_root.set(js_array_new(0));
                if (item_is_error(rest_root.get())) return js_interp_throw(rest_root.get());
                for (; index < length; index++) {
                    value_root.set(js_elements_get_int(source_root.get(), index));
                    if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
                    Item pushed = js_array_push(rest_root.get(), value_root.get());
                    if (item_is_error(pushed)) return js_interp_throw(pushed);
                }
                return js_interp_bind_pattern(frame, (JsAstNode*)rest->argument,
                    rest_root.get(), initialize);
            }
            value_root.set(index < length
                ? js_elements_get_int(source_root.get(), index)
                : make_js_undefined());
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            JsInterpCompletion bound = js_interp_bind_pattern(frame, element,
                value_root.get(), initialize);
            if (bound.kind != JS_INTERP_NORMAL) return bound;
            index++;
        }
        return js_interp_normal(make_js_undefined());
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        JsObjectPatternNode* object = (JsObjectPatternNode*)pattern;
        int property_count = 0;
        JsSpreadElementNode* rest = NULL;
        for (JsAstNode* property = (JsAstNode*)object->properties; property;
                property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY) {
                rest = (JsSpreadElementNode*)property;
            } else if (property->node_type == AST_NODE_PROPERTY) {
                property_count++;
            } else {
                return js_interp_throw(js_throw_type_error("invalid object binding pattern"));
            }
        }
        RootFrame roots(3);
        Rooted<Item> source_root(roots, input);
        Rooted<Item> key_root(roots, ItemNull);
        Rooted<Item> value_root(roots, ItemNull);
        RootSpan excluded_roots(property_count > 0 ? (size_t)property_count : 0);
        Item* excluded = property_count > 0 ? (Item*)(void*)excluded_roots.words() : NULL;
        int excluded_count = 0;
        for (JsAstNode* property = (JsAstNode*)object->properties; property;
                property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY) continue;
            JsPropertyNode* pair = (JsPropertyNode*)property;
            if (!pair->computed && pair->key && pair->key->node_type == AST_NODE_IDENT) {
                key_root.set(js_interp_name_key(((JsIdentifierNode*)pair->key)->name));
            } else {
                JsInterpCompletion key = js_interp_eval(frame, (JsAstNode*)pair->key);
                if (key.kind != JS_INTERP_NORMAL) return key;
                key_root.set(js_to_property_key(key.value));
            }
            if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
            excluded[excluded_count++] = key_root.get();
            value_root.set(js_get_key_default(source_root.get(), key_root.get()));
            if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            JsInterpCompletion bound = js_interp_bind_pattern(frame,
                (JsAstNode*)pair->value, value_root.get(), initialize);
            if (bound.kind != JS_INTERP_NORMAL) return bound;
        }
        if (!rest) return js_interp_normal(make_js_undefined());
        value_root.set(js_object_rest(source_root.get(), excluded, excluded_count));
        if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
        return js_interp_bind_pattern(frame, (JsAstNode*)rest->argument,
            value_root.get(), initialize);
    }
    default:
        return js_interp_throw(js_throw_type_error("unsupported binding pattern"));
    }
}

static JsInterpCompletion js_interp_eval(JsInterpFrame* frame, JsAstNode* node) {
    if (!node) return js_interp_normal(make_js_undefined());
    switch (node->node_type) {
    case AST_NODE_LITERAL: {
        JsLiteralNode* literal = (JsLiteralNode*)node;
        switch (literal->literal_type) {
        case JS_LITERAL_NUMBER:
            return js_interp_normal(literal->is_bigint
                ? bigint_from_string(literal->bigint_str->chars, literal->bigint_str->len)
                : js_make_number(literal->value.number_value));
        case JS_LITERAL_STRING:
            return js_interp_normal(js_make_string_len(literal->value.string_value->chars,
                literal->value.string_value->len));
        case JS_LITERAL_BOOLEAN:
            return js_interp_normal((Item){.item = b2it(literal->value.boolean_value)});
        case JS_LITERAL_NULL: return js_interp_normal(ItemNull);
        case JS_LITERAL_UNDEFINED: return js_interp_normal(make_js_undefined());
        default: return js_interp_throw(ItemError);
        }
    }
    case AST_NODE_IDENT: {
        JsIdentifierNode* identifier = (JsIdentifierNode*)node;
        Item value = js_interp_read_binding(frame, identifier->entry, identifier->name);
        return item_is_error(value) ? js_interp_throw(value) : js_interp_normal(value);
    }
    case JS_AST_NODE_CLASS_EXPRESSION:
        return js_interp_eval_class(frame, (JsClassNode*)node, false);
    case AST_NODE_PRIMARY:
        return js_interp_eval(frame, (JsAstNode*)((AstPrimaryNode*)node)->expr);
    case AST_NODE_UNARY: {
        JsUnaryNode* unary = (JsUnaryNode*)node;
        if (unary->op == OPERATOR_JS_TYPEOF && unary->operand &&
                unary->operand->node_type == AST_NODE_IDENT) {
            JsIdentifierNode* identifier = (JsIdentifierNode*)unary->operand;
            // ECMAScript's `typeof` is the one identifier consumer that does
            // not throw for an unresolvable reference. A lexical TDZ still
            // has an entry and therefore follows the regular error path.
            if (!identifier->entry && !js_global_binding_exists(
                    js_interp_name_key(identifier->name)) &&
                    !js_eval_local_has_var_binding(
                        js_interp_name_key(identifier->name))) {
                return js_interp_normal(js_make_string("undefined"));
            }
        }
        if (unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) {
            RootFrame roots(5);
            Rooted<Item> reference_object_root(roots, ItemNull);
            Rooted<Item> reference_key_root(roots, ItemNull);
            JsInterpReference reference;
            JsInterpCompletion ref = js_interp_eval_reference(frame,
                (JsAstNode*)unary->operand, &reference,
                reference_object_root.home(), reference_key_root.home());
            if (ref.kind != JS_INTERP_NORMAL) return ref;
            Rooted<Item> old_root(roots, js_interp_reference_read(frame, &reference));
            Rooted<Item> one_root(roots, js_make_number(1.0));
            Rooted<Item> next_root(roots, unary->op == OPERATOR_JS_INCREMENT
                ? js_add(old_root.get(), one_root.get())
                : js_subtract(old_root.get(), one_root.get()));
            if (item_is_error(old_root.get()) || item_is_error(next_root.get())) {
                return js_interp_throw(item_is_error(old_root.get()) ? old_root.get() : next_root.get());
            }
            Item set = js_interp_reference_write(frame, &reference, next_root.get(), false);
            if (item_is_error(set)) return js_interp_throw(set);
            return js_interp_normal(unary->prefix ? next_root.get() : old_root.get());
        }
        if (unary->op == OPERATOR_JS_DELETE) {
            if (unary->operand && (unary->operand->node_type == AST_NODE_MEMBER_EXPR ||
                    unary->operand->node_type == AST_NODE_INDEX_EXPR)) {
                RootFrame roots(2);
                Rooted<Item> object_root(roots, ItemNull);
                Rooted<Item> key_root(roots, ItemNull);
                JsInterpReference reference;
                JsInterpCompletion resolved = js_interp_eval_reference(frame,
                    (JsAstNode*)unary->operand, &reference, object_root.home(), key_root.home());
                if (resolved.kind != JS_INTERP_NORMAL) return resolved;
                Item deleted = js_delete_property(js_interp_reference_object(&reference),
                    js_interp_reference_key(&reference));
                Item result = js_delete_reference_result(js_interp_reference_key(&reference),
                    deleted, frame->strict ? 1 : 0);
                return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
            }
            if (unary->operand && unary->operand->node_type == AST_NODE_IDENT) {
                JsIdentifierNode* identifier = (JsIdentifierNode*)unary->operand;
                Item result = js_delete_identifier_with_binding(
                    js_interp_name_key(identifier->name), identifier->entry ? 1 : 0);
                return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
            }
            JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)unary->operand);
            return value.kind == JS_INTERP_NORMAL ? js_interp_normal((Item){.item = b2it(true)})
                : value;
        }
        RootFrame roots(1);
        Rooted<Item> operand_root(roots, ItemNull);
        JsInterpCompletion operand = js_interp_eval(frame, (JsAstNode*)unary->operand);
        if (operand.kind != JS_INTERP_NORMAL) return operand;
        operand_root.set(operand.value);
        Item result = ItemError;
        switch (unary->op) {
        case OPERATOR_NOT: result = js_logical_not(operand_root.get()); break;
        case OPERATOR_POS: result = js_unary_plus(operand_root.get()); break;
        case OPERATOR_NEG: result = js_unary_minus(operand_root.get()); break;
        case OPERATOR_JS_BIT_NOT: result = js_bitwise_not(operand_root.get()); break;
        case OPERATOR_JS_TYPEOF: result = js_typeof(operand_root.get()); break;
        case OPERATOR_JS_VOID: result = make_js_undefined(); break;
        default: result = js_throw_type_error("unsupported interpreted unary operator"); break;
        }
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case AST_NODE_BINARY: {
        JsBinaryNode* binary = (JsBinaryNode*)node;
        JsInterpCompletion left = js_interp_eval(frame, (JsAstNode*)binary->left);
        if (left.kind != JS_INTERP_NORMAL) return left;
        if (binary->op == OPERATOR_AND && !js_is_truthy(left.value)) return left;
        if (binary->op == OPERATOR_OR && js_is_truthy(left.value)) return left;
        if (binary->op == OPERATOR_JS_NULLISH_COALESCE &&
                get_type_id(left.value) != LMD_TYPE_NULL &&
                get_type_id(left.value) != LMD_TYPE_UNDEFINED) return left;
        RootFrame roots(2);
        Rooted<Item> left_root(roots, left.value);
        JsInterpCompletion right = js_interp_eval(frame, (JsAstNode*)binary->right);
        if (right.kind != JS_INTERP_NORMAL) return right;
        Rooted<Item> right_root(roots, right.value);
        Item result = (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR ||
                binary->op == OPERATOR_JS_NULLISH_COALESCE) ? right_root.get()
            : js_interp_binary(binary->op, left_root.get(), right_root.get());
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case AST_NODE_ASSIGN: {
        JsAssignmentNode* assignment = (JsAssignmentNode*)node;
        if (assignment->left && (assignment->left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
                assignment->left->node_type == JS_AST_NODE_OBJECT_PATTERN ||
                assignment->left->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN)) {
            RootFrame roots(1);
            Rooted<Item> value_root(roots, ItemNull);
            JsInterpCompletion right = js_interp_eval(frame,
                (JsAstNode*)assignment->right);
            if (right.kind != JS_INTERP_NORMAL) return right;
            value_root.set(right.value);
            JsInterpCompletion bound = js_interp_bind_pattern(frame,
                (JsAstNode*)assignment->left, value_root.get(), false);
            // Destructuring assignment evaluates to its RHS, rather than the
            // last target written while recursively binding the pattern.
            return bound.kind == JS_INTERP_NORMAL ? js_interp_normal(value_root.get()) : bound;
        }
        RootFrame roots(5);
        Rooted<Item> reference_object_root(roots, ItemNull);
        Rooted<Item> reference_key_root(roots, ItemNull);
        JsInterpReference reference;
        JsInterpCompletion ref = js_interp_eval_reference(frame, (JsAstNode*)assignment->left,
            &reference, reference_object_root.home(), reference_key_root.home());
        if (ref.kind != JS_INTERP_NORMAL) return ref;
        Rooted<Item> old_root(roots, ItemNull);
        if (assignment->op != OPERATOR_ASSIGN) {
            old_root.set(js_interp_reference_read(frame, &reference));
            if (item_is_error(old_root.get())) return js_interp_throw(old_root.get());
            if (js_interp_is_logical_assignment(assignment->op) &&
                    js_interp_logical_assignment_keeps_old(assignment->op, old_root.get())) {
                return js_interp_normal(old_root.get());
            }
        }
        Rooted<Item> right_root(roots, ItemNull);
        JsInterpCompletion right = js_interp_eval(frame, (JsAstNode*)assignment->right);
        if (right.kind != JS_INTERP_NORMAL) return right;
        right_root.set(right.value);
        Rooted<Item> result_root(roots, js_interp_is_logical_assignment(assignment->op)
            ? right_root.get() : js_interp_assignment_value(assignment->op,
                old_root.get(), right_root.get()));
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        Item set = js_interp_reference_write(frame, &reference, result_root.get(), false);
        return item_is_error(set) ? js_interp_throw(set) : js_interp_normal(result_root.get());
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR: {
        JsMemberNode* member = (JsMemberNode*)node;
        RootFrame roots(2);
        Rooted<Item> object_root(roots, ItemNull);
        Rooted<Item> key_root(roots, ItemNull);
        JsInterpCompletion object = js_interp_eval(frame, (JsAstNode*)member->object);
        if (object.kind != JS_INTERP_NORMAL) return object;
        object_root.set(object.value);
        if (member->optional && js_interp_is_nullish(object_root.get())) {
            return js_interp_normal(make_js_undefined());
        }
        key_root.set(js_interp_property_key(frame, member));
        if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
        Item result = js_get_key_default(object_root.get(), key_root.get());
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case AST_NODE_CALL_EXPR:
        return js_interp_eval_call(frame, (JsCallNode*)node, false);
    case AST_NODE_NEW_EXPR:
        return js_interp_eval_call(frame, (JsCallNode*)node, true);
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return js_interp_eval_template(frame, (JsTemplateLiteralNode*)node);
    case JS_AST_NODE_TAGGED_TEMPLATE:
        return js_interp_eval_tagged_template(frame, (JsTaggedTemplateNode*)node);
    case JS_AST_NODE_REGEX: {
        JsRegexNode* regex = (JsRegexNode*)node;
        RootFrame roots(2);
        Rooted<Item> pattern_root(roots, js_make_string_len(
            regex->pattern ? regex->pattern : "", regex->pattern ? regex->pattern_len : 0));
        Rooted<Item> flags_root(roots, js_make_string_len(
            regex->flags ? regex->flags : "", regex->flags ? regex->flags_len : 0));
        Item result = js_create_regex_literal_items(pattern_root.get(), flags_root.get());
        return item_is_error(result) ? js_interp_throw(result) : js_interp_normal(result);
    }
    case AST_NODE_ARRAY: {
        JsArrayNode* array = (JsArrayNode*)node;
        RootFrame roots(3);
        Rooted<Item> result_root(roots, js_array_new(0));
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> spread_item_root(roots, ItemNull);
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        for (JsAstNode* element = (JsAstNode*)array->elements; element;
                element = (JsAstNode*)element->next) {
            if (element->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsSpreadElementNode* spread = (JsSpreadElementNode*)element;
                JsInterpCompletion source = js_interp_eval(frame,
                    (JsAstNode*)spread->argument);
                if (source.kind != JS_INTERP_NORMAL) return source;
                value_root.set(source.value);
                value_root.set(js_iterable_to_array(value_root.get()));
                if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
                int64_t length = js_array_length(value_root.get());
                for (int64_t index = 0; index < length; index++) {
                    spread_item_root.set(js_elements_get_int(value_root.get(), index));
                    if (item_is_error(spread_item_root.get())) {
                        return js_interp_throw(spread_item_root.get());
                    }
                    Item pushed = js_array_push(result_root.get(), spread_item_root.get());
                    if (item_is_error(pushed)) return js_interp_throw(pushed);
                }
                continue;
            }
            JsInterpCompletion value = js_interp_eval(frame, element);
            if (value.kind != JS_INTERP_NORMAL) return value;
            value_root.set(value.value);
            Item pushed = js_array_push(result_root.get(), value_root.get());
            if (item_is_error(pushed)) return js_interp_throw(pushed);
        }
        return js_interp_normal(result_root.get());
    }
    case AST_NODE_MAP: {
        JsObjectNode* object = (JsObjectNode*)node;
        RootFrame roots(4);
        Rooted<Item> result_root(roots, js_new_object());
        Rooted<Item> key_root(roots, ItemNull);
        Rooted<Item> value_root(roots, ItemNull);
        Rooted<Item> raw_key_root(roots, ItemNull);
        if (item_is_error(result_root.get())) return js_interp_throw(result_root.get());
        for (JsAstNode* property = (JsAstNode*)object->properties; property;
                property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                JsInterpCompletion source = js_interp_eval(frame,
                    (JsAstNode*)((JsSpreadElementNode*)property)->argument);
                if (source.kind != JS_INTERP_NORMAL) return source;
                value_root.set(source.value);
                Item spread = js_object_spread_into(result_root.get(), value_root.get());
                if (item_is_error(spread)) return js_interp_throw(spread);
                continue;
            }
            if (property->node_type != AST_NODE_PROPERTY) {
                return js_interp_throw(js_throw_type_error("unsupported object property"));
            }
            JsPropertyNode* pair = (JsPropertyNode*)property;
            if (!pair->computed && pair->key && pair->key->node_type == AST_NODE_IDENT) {
                key_root.set(js_interp_name_key(((JsIdentifierNode*)pair->key)->name));
            } else {
                JsInterpCompletion key = js_interp_eval(frame, (JsAstNode*)pair->key);
                if (key.kind != JS_INTERP_NORMAL) return key;
                raw_key_root.set(key.value);
                key_root.set(js_to_property_key(raw_key_root.get()));
            }
            if (item_is_error(key_root.get())) return js_interp_throw(key_root.get());
            if (pair->method || pair->is_getter || pair->is_setter) {
                value_root.set(js_interp_make_object_method(frame,
                    (JsFunctionNode*)pair->value));
                if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
            } else {
                JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)pair->value);
                if (value.kind != JS_INTERP_NORMAL) return value;
                value_root.set(value.value);
            }
            Item set = (pair->is_getter || pair->is_setter)
                ? js_define_accessor_partial(result_root.get(), key_root.get(), value_root.get(),
                    pair->is_setter ? 1 : 0, 0)
                : js_create_data_property(result_root.get(), key_root.get(), value_root.get());
            if (item_is_error(set)) return js_interp_throw(set);
        }
        return js_interp_normal(result_root.get());
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_ARROW_FUNC: {
        Item function = js_interp_make_function(frame, (JsFunctionNode*)node);
        return item_is_error(function) ? js_interp_throw(function) : js_interp_normal(function);
    }
    case AST_NODE_CONDITIONAL_EXPR: {
        JsConditionalNode* conditional = (JsConditionalNode*)node;
        JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)conditional->test);
        if (test.kind != JS_INTERP_NORMAL) return test;
        return js_interp_eval(frame, (JsAstNode*)(js_is_truthy(test.value)
            ? conditional->consequent : conditional->alternate));
    }
    case AST_NODE_SEQ: {
        JsInterpCompletion result = js_interp_normal(make_js_undefined());
        for (JsAstNode* expression = (JsAstNode*)((JsSequenceNode*)node)->elements;
                expression; expression = (JsAstNode*)expression->next) {
            result = js_interp_eval(frame, expression);
            if (result.kind != JS_INTERP_NORMAL) return result;
        }
        return result;
    }
    default:
        return js_interp_throw(js_throw_type_error("unsupported interpreted expression"));
    }
}

static JsInterpCompletion js_interp_initialize_scope(JsInterpFrame* frame,
        NameScope* scope) {
    if (!scope) return js_interp_normal(make_js_undefined());
    js_interp_scope_slot_count(scope);
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        Item initial = entry->is_lexical ? (Item){.item = ITEM_JS_TDZ}
            : make_js_undefined();
        Item stored = js_interp_write_binding(frame, entry, NULL, initial, true);
        if (item_is_error(stored)) return js_interp_throw(stored);
    }
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        AstNode* node = entry->node;
        if (!node || (node->node_type != AST_NODE_FUNC &&
                node->node_type != AST_NODE_FUNC_EXPR &&
                node->node_type != AST_NODE_ARROW_FUNC)) continue;
        RootFrame roots(1);
        Rooted<Item> function_root(roots, js_interp_make_function(frame,
            (JsFunctionNode*)node));
        if (item_is_error(function_root.get())) return js_interp_throw(function_root.get());
        Item stored = js_interp_write_binding(frame, entry, NULL, function_root.get(), true);
        if (item_is_error(stored)) return js_interp_throw(stored);
        if (entry->scope == frame->script->global_scope) {
            js_define_global_function_property(js_interp_name_key(entry->name), function_root.get());
        }
    }
    return js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_exec_list(JsInterpFrame* frame, JsAstNode* node) {
    JsInterpCompletion result = js_interp_normal(make_js_undefined());
    for (JsAstNode* current = node; current; current = (JsAstNode*)current->next) {
        result = js_interp_exec(frame, current);
        if (result.kind != JS_INTERP_NORMAL) return result;
    }
    return result;
}

static JsInterpCompletion js_interp_exec_block(JsInterpFrame* frame, JsBlockNode* block) {
    JsInterpEnv* env = js_interp_env_create(block->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame child = *frame;
    child.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&child, block->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    return js_interp_exec_list(&child, (JsAstNode*)block->statements);
}

static JsInterpCompletion js_interp_exec_scoped(JsInterpFrame* frame,
        NameScope* scope, JsAstNode* node) {
    if (!scope) return js_interp_exec(frame, node);
    JsInterpEnv* env = js_interp_env_create(scope, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame child = *frame;
    child.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&child, scope);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    return js_interp_exec(&child, node);
}

static JsInterpCompletion js_interp_exec_switch(JsInterpFrame* frame,
        JsSwitchNode* switched) {
    if (!switched) return js_interp_throw(ItemError);
    JsInterpEnv* env = js_interp_env_create(switched->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame switch_frame = *frame;
    switch_frame.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&switch_frame,
        switched->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;

    RootFrame roots(1);
    Rooted<Item> discriminant_root(roots, ItemNull);
    JsInterpCompletion discriminant = js_interp_eval(&switch_frame,
        (JsAstNode*)switched->discriminant);
    if (discriminant.kind != JS_INTERP_NORMAL) return discriminant;
    discriminant_root.set(discriminant.value);

    JsSwitchCaseNode* selected = NULL;
    JsSwitchCaseNode* default_case = NULL;
    for (JsSwitchCaseNode* candidate = (JsSwitchCaseNode*)switched->cases;
            candidate; candidate = (JsSwitchCaseNode*)candidate->next) {
        if (!candidate->test) {
            default_case = candidate;
            continue;
        }
        JsInterpCompletion test = js_interp_eval(&switch_frame,
            (JsAstNode*)candidate->test);
        if (test.kind != JS_INTERP_NORMAL) return test;
        if (js_strict_equal(discriminant_root.get(), test.value).item == b2it(true)) {
            selected = candidate;
            break;
        }
    }
    if (!selected) selected = default_case;
    if (!selected) return js_interp_normal(make_js_undefined());

    for (JsSwitchCaseNode* current = selected; current;
            current = (JsSwitchCaseNode*)current->next) {
        JsInterpCompletion completion = js_interp_exec_list(&switch_frame,
            (JsAstNode*)current->consequent);
        if (completion.kind == JS_INTERP_BREAK &&
                js_interp_completion_targets_active_label(&completion, &switch_frame)) {
            return js_interp_normal(make_js_undefined());
        }
        if (completion.kind != JS_INTERP_NORMAL) return completion;
    }
    return js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_assign_iteration_head(JsInterpFrame* frame,
        JsAstNode* left, Item value, bool initialize) {
    if (!left) return js_interp_throw(ItemError);
    if (left->node_type == AST_NODE_VAR_STAM) {
        JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)left;
        JsVariableDeclaratorNode* declarator = declaration->declarations &&
                declaration->declarations->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? (JsVariableDeclaratorNode*)declaration->declarations : NULL;
        if (!declarator || declarator->next || !declarator->id) {
            return js_interp_throw(js_throw_type_error("unsupported iteration declaration"));
        }
        return js_interp_bind_pattern(frame, (JsAstNode*)declarator->id,
            value, initialize);
    }
    if (left->node_type == JS_AST_NODE_ARRAY_PATTERN ||
            left->node_type == JS_AST_NODE_OBJECT_PATTERN ||
            left->node_type == JS_AST_NODE_ASSIGNMENT_PATTERN) {
        return js_interp_bind_pattern(frame, left, value, initialize);
    }
    RootFrame roots(2);
    Rooted<Item> object_root(roots, ItemNull);
    Rooted<Item> key_root(roots, ItemNull);
    JsInterpReference reference;
    JsInterpCompletion resolved = js_interp_eval_reference(frame, left, &reference,
        object_root.home(), key_root.home());
    if (resolved.kind != JS_INTERP_NORMAL) return resolved;
    Item stored = js_interp_reference_write(frame, &reference, value, initialize);
    return item_is_error(stored) ? js_interp_throw(stored) : js_interp_normal(stored);
}

static JsInterpCompletion js_interp_close_iterator_after_completion(Item iterator,
        const JsInterpFrame* frame, JsInterpCompletion completion) {
    bool matching_continue = completion.kind == JS_INTERP_CONTINUE &&
        js_interp_completion_targets_active_label(&completion, frame);
    if (completion.kind == JS_INTERP_NORMAL || matching_continue) {
        return completion;
    }
    RootFrame roots(1);
    Rooted<Item> completion_root(roots, completion.value);
    Item closed = js_iterator_close(iterator);
    if (item_is_error(closed)) return js_interp_throw(closed);
    if (completion.kind == JS_INTERP_BREAK &&
            js_interp_completion_targets_active_label(&completion, frame)) {
        return js_interp_normal(make_js_undefined());
    }
    completion.value = completion_root.get();
    return completion;
}

static JsInterpCompletion js_interp_exec_for_of(JsInterpFrame* frame,
        JsForOfNode* loop, bool is_for_in) {
    if (!loop) return js_interp_throw(ItemError);
    if (loop->is_await) return js_interp_throw(js_throw_syntax_error(
        js_make_string("unsupported asynchronous iteration")));

    RootFrame roots(3);
    Rooted<Item> source_root(roots, ItemNull);
    Rooted<Item> iterator_root(roots, ItemNull);
    Rooted<Item> value_root(roots, ItemNull);
    if (loop->init) {
        JsInterpCompletion initial = js_interp_eval(frame, (JsAstNode*)loop->init);
        if (initial.kind != JS_INTERP_NORMAL) return initial;
    }
    JsInterpCompletion source = js_interp_eval(frame, (JsAstNode*)loop->right);
    if (source.kind != JS_INTERP_NORMAL) return source;
    source_root.set(source.value);
    if (is_for_in) {
        source_root.set(js_for_in_keys(source_root.get()));
        if (item_is_error(source_root.get())) return js_interp_throw(source_root.get());
    }
    iterator_root.set(js_get_iterator(source_root.get()));
    if (item_is_error(iterator_root.get())) return js_interp_throw(iterator_root.get());

    JsInterpEnv* env = js_interp_env_create(loop->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame header_frame = *frame;
    header_frame.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&header_frame, loop->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    bool has_per_iteration_lexical = false;
    for (NameEntry* entry = loop->vars ? loop->vars->first : NULL;
            entry; entry = entry->next) {
        if (entry->is_lexical) {
            has_per_iteration_lexical = true;
            break;
        }
    }

    for (;;) {
        value_root.set(js_iterator_step(iterator_root.get()));
        if (item_is_error(value_root.get())) return js_interp_throw(value_root.get());
        if (value_root.get().item == JS_ITER_DONE_SENTINEL) {
            return js_interp_normal(make_js_undefined());
        }
        JsInterpEnv* iteration_env = has_per_iteration_lexical
            ? js_interp_env_clone(env_root.env) : env_root.env;
        JsInterpEnvRoot iteration_root(has_per_iteration_lexical ? iteration_env : NULL);
        if (has_per_iteration_lexical && (!iteration_env || !iteration_root.registered)) {
            return js_interp_throw(ItemError);
        }
        JsInterpFrame iteration_frame = *frame;
        iteration_frame.env = iteration_env;
        JsInterpCompletion assigned = js_interp_assign_iteration_head(&iteration_frame,
            (JsAstNode*)loop->left, value_root.get(), loop->declares_binding);
        if (assigned.kind != JS_INTERP_NORMAL) {
            return js_interp_close_iterator_after_completion(iterator_root.get(),
                &iteration_frame, assigned);
        }
        JsInterpFrame body_frame = iteration_frame;
        body_frame.active_label = NULL;
        body_frame.active_label_len = 0;
        JsInterpCompletion completion = js_interp_exec(&body_frame,
            (JsAstNode*)loop->body);
        if (has_per_iteration_lexical) env_root.replace_with(&iteration_root);
        if (completion.kind == JS_INTERP_NORMAL ||
                (completion.kind == JS_INTERP_CONTINUE &&
                 js_interp_completion_targets_active_label(&completion, &iteration_frame))) {
            continue;
        }
        return js_interp_close_iterator_after_completion(iterator_root.get(),
            &iteration_frame, completion);
    }
}

static JsInterpCompletion js_interp_exec_declaration(JsInterpFrame* frame,
        JsVariableDeclarationNode* declaration) {
    for (JsAstNode* item = (JsAstNode*)declaration->declarations; item;
            item = (JsAstNode*)item->next) {
        if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) {
            return js_interp_throw(js_throw_type_error("unsupported declaration pattern"));
        }
        JsVariableDeclaratorNode* declarator = (JsVariableDeclaratorNode*)item;
        if (!declarator->id) {
            return js_interp_throw(js_throw_type_error("unsupported declaration pattern"));
        }
        RootFrame roots(1);
        Rooted<Item> value_root(roots, make_js_undefined());
        if (declarator->init) {
            JsInterpCompletion initialized = js_interp_eval(frame,
                (JsAstNode*)declarator->init);
            if (initialized.kind != JS_INTERP_NORMAL) return initialized;
            value_root.set(initialized.value);
        }
        JsInterpCompletion bound = js_interp_bind_pattern(frame,
            (JsAstNode*)declarator->id, value_root.get(), true);
        if (bound.kind != JS_INTERP_NORMAL) return bound;
    }
    return js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_exec_catch(JsInterpFrame* frame,
        JsCatchNode* handler, Item thrown) {
    if (!handler) return js_interp_throw(thrown);
    RootFrame roots(2);
    Rooted<Item> thrown_root(roots, thrown);
    JsInterpEnv* env = js_interp_env_create(handler->vars, frame ? frame->env : NULL);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return js_interp_throw(ItemError);
    JsInterpFrame catch_frame = *frame;
    catch_frame.env = env;
    JsInterpCompletion initialized = js_interp_initialize_scope(&catch_frame, handler->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized;
    if (handler->param) {
        Rooted<Item> payload_root(roots, js_error_lane_payload(thrown_root.get()));
        JsInterpCompletion bound = js_interp_bind_pattern(&catch_frame,
            (JsAstNode*)handler->param, payload_root.get(), true);
        if (bound.kind != JS_INTERP_NORMAL) return bound;
    }
    return handler->body ? js_interp_exec(&catch_frame, (JsAstNode*)handler->body)
        : js_interp_normal(make_js_undefined());
}

static JsInterpCompletion js_interp_exec(JsInterpFrame* frame, JsAstNode* node) {
    if (!node) return js_interp_normal(make_js_undefined());
    switch (node->node_type) {
    case AST_SCRIPT:
        return js_interp_exec_list(frame, (JsAstNode*)((JsProgramNode*)node)->body);
    case AST_NODE_BLOCK:
        return js_interp_exec_block(frame, (JsBlockNode*)node);
    case AST_NODE_EXPR_STMT:
        return js_interp_eval(frame, (JsAstNode*)((JsExpressionStatementNode*)node)->expression);
    case AST_NODE_VAR_STAM:
        return js_interp_exec_declaration(frame, (JsVariableDeclarationNode*)node);
    case AST_NODE_FUNC:
        // Declaration instantiation created the function before any statement
        // executed. Re-creating it here would change its observable identity.
        return js_interp_normal(make_js_undefined());
    case JS_AST_NODE_CLASS_DECLARATION:
        return js_interp_eval_class(frame, (JsClassNode*)node, true);
    case JS_AST_NODE_WITH_STATEMENT: {
        JsWithStatementNode* with = (JsWithStatementNode*)node;
        RootFrame roots(1);
        Rooted<Item> object_root(roots, ItemNull);
        JsInterpCompletion object = js_interp_eval(frame, with->object);
        if (object.kind != JS_INTERP_NORMAL) return object;
        object_root.set(object.value);
        Item pushed = js_with_push(object_root.get());
        if (item_is_error(pushed)) return js_interp_throw(pushed);
        JsInterpCompletion completion = with->body
            ? js_interp_exec(frame, with->body) : js_interp_normal(make_js_undefined());
        js_with_pop();
        return completion;
    }
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* labeled = (JsLabeledStatementNode*)node;
        JsInterpFrame labeled_frame = *frame;
        labeled_frame.active_label = labeled->label;
        labeled_frame.active_label_len = labeled->label_len;
        JsInterpCompletion completion = js_interp_exec(&labeled_frame, labeled->body);
        if (completion.kind == JS_INTERP_BREAK &&
                js_interp_completion_targets_active_label(&completion, &labeled_frame)) {
            return js_interp_normal(make_js_undefined());
        }
        return completion;
    }
    case AST_NODE_IF_EXPR: {
        JsIfNode* conditional = (JsIfNode*)node;
        JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)conditional->test);
        if (test.kind != JS_INTERP_NORMAL) return test;
        bool take_consequent = js_is_truthy(test.value);
        return js_interp_exec_scoped(frame, take_consequent
            ? conditional->consequent_vars : conditional->alternate_vars,
            (JsAstNode*)(take_consequent ? conditional->consequent
                : conditional->alternate));
    }
    case AST_NODE_WHILE_STAM: {
        JsWhileNode* loop = (JsWhileNode*)node;
        for (;;) {
            JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)loop->test);
            if (test.kind != JS_INTERP_NORMAL) return test;
            if (!js_is_truthy(test.value)) return js_interp_normal(make_js_undefined());
            JsInterpFrame body_frame = *frame;
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
            JsInterpCompletion body = js_interp_exec(&body_frame, (JsAstNode*)loop->body);
            if (body.kind == JS_INTERP_BREAK) {
                if (js_interp_completion_targets_active_label(&body, frame)) {
                    return js_interp_normal(make_js_undefined());
                }
                return body;
            }
            if (body.kind == JS_INTERP_RETURN || body.kind == JS_INTERP_THROW) return body;
            if (body.kind == JS_INTERP_CONTINUE &&
                    !js_interp_completion_targets_active_label(&body, frame)) return body;
        }
    }
    case AST_NODE_DO_WHILE_STAM: {
        JsDoWhileNode* loop = (JsDoWhileNode*)node;
        for (;;) {
            JsInterpFrame body_frame = *frame;
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
            JsInterpCompletion body = js_interp_exec(&body_frame, (JsAstNode*)loop->body);
            if (body.kind == JS_INTERP_BREAK) {
                if (js_interp_completion_targets_active_label(&body, frame)) {
                    return js_interp_normal(make_js_undefined());
                }
                return body;
            }
            if (body.kind == JS_INTERP_RETURN || body.kind == JS_INTERP_THROW) return body;
            if (body.kind == JS_INTERP_CONTINUE &&
                    !js_interp_completion_targets_active_label(&body, frame)) return body;
            JsInterpCompletion test = js_interp_eval(frame, (JsAstNode*)loop->test);
            if (test.kind != JS_INTERP_NORMAL) return test;
            if (!js_is_truthy(test.value)) return js_interp_normal(make_js_undefined());
        }
    }
    case AST_NODE_FOR_STAM: {
        JsForNode* loop = (JsForNode*)node;
        JsInterpEnv* env = js_interp_env_create(loop->vars, frame ? frame->env : NULL);
        JsInterpEnvRoot env_root(env);
        if (!env || !env_root.registered) return js_interp_throw(ItemError);
        JsInterpFrame header_frame = *frame;
        header_frame.env = env;
        JsInterpCompletion initialized = js_interp_initialize_scope(&header_frame, loop->vars);
        if (initialized.kind != JS_INTERP_NORMAL) return initialized;
        if (loop->init) {
            JsInterpCompletion init = loop->init->node_type == AST_NODE_VAR_STAM
                ? js_interp_exec(&header_frame, (JsAstNode*)loop->init)
                : js_interp_eval(&header_frame, (JsAstNode*)loop->init);
            if (init.kind != JS_INTERP_NORMAL) return init;
        }
        bool has_per_iteration_lexical = false;
        for (NameEntry* entry = loop->vars ? loop->vars->first : NULL;
                entry; entry = entry->next) {
            if (entry->is_lexical) {
                has_per_iteration_lexical = true;
                break;
            }
        }
        for (;;) {
            JsInterpFrame loop_frame = *frame;
            loop_frame.env = env_root.env;
            if (loop->test) {
                JsInterpCompletion test = js_interp_eval(&loop_frame, (JsAstNode*)loop->test);
                if (test.kind != JS_INTERP_NORMAL) return test;
                if (!js_is_truthy(test.value)) return js_interp_normal(make_js_undefined());
            }
            JsInterpFrame body_frame = loop_frame;
            body_frame.active_label = NULL;
            body_frame.active_label_len = 0;
            JsInterpCompletion body = js_interp_exec(&body_frame, (JsAstNode*)loop->body);
            if (body.kind == JS_INTERP_BREAK) {
                if (js_interp_completion_targets_active_label(&body, &loop_frame)) {
                    return js_interp_normal(make_js_undefined());
                }
                return body;
            }
            if (body.kind == JS_INTERP_RETURN || body.kind == JS_INTERP_THROW) return body;
            if (body.kind == JS_INTERP_CONTINUE &&
                    !js_interp_completion_targets_active_label(&body, &loop_frame)) return body;
            // ECMAScript gives each `for (let/const ...)` iteration a fresh
            // binding environment. Clone before the update so body closures
            // retain the value they observed, while the update feeds the next
            // iteration through the new record.
            if (has_per_iteration_lexical) {
                JsInterpEnv* next_env = js_interp_env_clone(env_root.env);
                JsInterpEnvRoot next_env_root(next_env);
                if (!next_env || !next_env_root.registered) return js_interp_throw(ItemError);
                JsInterpFrame update_frame = loop_frame;
                update_frame.env = next_env;
                if (loop->update) {
                    JsInterpCompletion update = js_interp_eval(&update_frame,
                        (JsAstNode*)loop->update);
                    if (update.kind != JS_INTERP_NORMAL) return update;
                }
                env_root.replace_with(&next_env_root);
            } else if (loop->update) {
                JsInterpCompletion update = js_interp_eval(&loop_frame,
                    (JsAstNode*)loop->update);
                if (update.kind != JS_INTERP_NORMAL) return update;
            }
        }
    }
    case JS_AST_NODE_SWITCH_STATEMENT:
        return js_interp_exec_switch(frame, (JsSwitchNode*)node);
    case JS_AST_NODE_FOR_OF_STATEMENT:
        return js_interp_exec_for_of(frame, (JsForOfNode*)node, false);
    case JS_AST_NODE_FOR_IN_STATEMENT:
        return js_interp_exec_for_of(frame, (JsForOfNode*)node, true);
    case AST_NODE_TRY_STAM: {
        JsTryNode* tried = (JsTryNode*)node;
        JsInterpCompletion completion = js_interp_exec(frame, (JsAstNode*)tried->block);
        RootFrame roots(1);
        Rooted<Item> completion_root(roots, completion.value);
        if (completion.kind == JS_INTERP_THROW && tried->handler) {
            completion = js_interp_exec_catch(frame, (JsCatchNode*)tried->handler,
                completion_root.get());
            completion_root.set(completion.value);
        }
        if (tried->finalizer) {
            JsInterpCompletion finalizer = js_interp_exec(frame,
                (JsAstNode*)tried->finalizer);
            if (finalizer.kind != JS_INTERP_NORMAL) {
                completion = finalizer;
                completion_root.set(completion.value);
            }
        }
        completion.value = completion_root.get();
        return completion;
    }
    case AST_NODE_RETURN_STAM: {
        JsReturnNode* returned = (JsReturnNode*)node;
        JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)returned->value);
        return value.kind == JS_INTERP_NORMAL
            ? JsInterpCompletion{JS_INTERP_RETURN, value.value, NULL, 0} : value;
    }
    case AST_NODE_RAISE_STAM: {
        JsThrowNode* thrown = (JsThrowNode*)node;
        RootFrame roots(1);
        Rooted<Item> value_root(roots, ItemNull);
        JsInterpCompletion value = js_interp_eval(frame, (JsAstNode*)thrown->value);
        if (value.kind != JS_INTERP_NORMAL) return value;
        value_root.set(value.value);
        return js_interp_throw(js_throw_value(value_root.get()));
    }
    case AST_NODE_BREAK_STAM: {
        AstBreakContinueNode* control = (AstBreakContinueNode*)node;
        return {JS_INTERP_BREAK, make_js_undefined(), control->label, control->label_len};
    }
    case AST_NODE_CONTINUE_STAM: {
        AstBreakContinueNode* control = (AstBreakContinueNode*)node;
        return {JS_INTERP_CONTINUE, make_js_undefined(), control->label, control->label_len};
    }
    default:
        return js_interp_eval(frame, node);
    }
}

static bool js_interp_operator_supported(Operator op) {
    switch (op) {
    case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL: case OPERATOR_DIV:
    case OPERATOR_MOD: case OPERATOR_POW: case OPERATOR_JS_EXP:
    case OPERATOR_EQ: case OPERATOR_NE: case OPERATOR_JS_STRICT_EQ:
    case OPERATOR_JS_STRICT_NE: case OPERATOR_LT: case OPERATOR_LE:
    case OPERATOR_GT: case OPERATOR_GE: case OPERATOR_AND: case OPERATOR_OR:
    case OPERATOR_IN: case OPERATOR_JS_INSTANCEOF: case OPERATOR_JS_NULLISH_COALESCE:
    case OPERATOR_JS_BIT_AND: case OPERATOR_JS_BIT_OR: case OPERATOR_JS_BIT_XOR:
    case OPERATOR_JS_LSHIFT: case OPERATOR_JS_RSHIFT: case OPERATOR_JS_URSHIFT:
    case OPERATOR_NOT: case OPERATOR_POS: case OPERATOR_NEG: case OPERATOR_JS_BIT_NOT:
    case OPERATOR_JS_TYPEOF: case OPERATOR_JS_VOID: case OPERATOR_JS_INCREMENT:
    case OPERATOR_JS_DECREMENT: case OPERATOR_JS_DELETE:
    case OPERATOR_ASSIGN: case OPERATOR_JS_ADD_ASSIGN:
    case OPERATOR_JS_SUB_ASSIGN: case OPERATOR_JS_MUL_ASSIGN: case OPERATOR_JS_DIV_ASSIGN:
    case OPERATOR_JS_MOD_ASSIGN: case OPERATOR_JS_EXP_ASSIGN: case OPERATOR_JS_BIT_AND_ASSIGN:
    case OPERATOR_JS_BIT_OR_ASSIGN: case OPERATOR_JS_BIT_XOR_ASSIGN:
    case OPERATOR_JS_LSHIFT_ASSIGN: case OPERATOR_JS_RSHIFT_ASSIGN:
    case OPERATOR_JS_URSHIFT_ASSIGN:
    case OPERATOR_JS_AND_ASSIGN: case OPERATOR_JS_OR_ASSIGN:
    case OPERATOR_JS_NULLISH_ASSIGN:
        return true;
    default:
        return false;
    }
}

struct JsInterpSupportState {
    bool supported;
};

static void js_interp_check_child(JsAstNode* child, void* opaque);

static bool js_interp_identifier_is(JsAstNode* node, const char* name) {
    if (!node || node->node_type != AST_NODE_IDENT) return false;
    return js_interp_name_equals(((JsIdentifierNode*)node)->name, name);
}

static bool js_interp_pattern_supported(JsAstNode* pattern) {
    if (!pattern) return false;
    switch (pattern->node_type) {
    case AST_NODE_IDENT:
        return true;
    case JS_AST_NODE_ASSIGNMENT_PATTERN:
        return ((JsAssignmentPatternNode*)pattern)->left &&
            js_interp_pattern_supported((JsAstNode*)((JsAssignmentPatternNode*)pattern)->left);
    case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
    case JS_AST_NODE_SPREAD_ELEMENT:
        return ((JsSpreadElementNode*)pattern)->argument &&
            js_interp_pattern_supported((JsAstNode*)((JsSpreadElementNode*)pattern)->argument);
    case JS_AST_NODE_ARRAY_PATTERN: {
        for (JsAstNode* element = (JsAstNode*)((JsArrayPatternNode*)pattern)->elements;
                element; element = (JsAstNode*)element->next) {
            if (element->node_type != AST_NODE_NULL &&
                    !js_interp_pattern_supported(element)) return false;
        }
        return true;
    }
    case JS_AST_NODE_OBJECT_PATTERN: {
        for (JsAstNode* property = (JsAstNode*)((JsObjectPatternNode*)pattern)->properties;
                property; property = (JsAstNode*)property->next) {
            if (property->node_type == JS_AST_NODE_REST_PROPERTY ||
                    property->node_type == JS_AST_NODE_SPREAD_ELEMENT) {
                if (!js_interp_pattern_supported((JsAstNode*)((JsSpreadElementNode*)property)->argument)) {
                    return false;
                }
            } else if (property->node_type != AST_NODE_PROPERTY ||
                    !js_interp_pattern_supported((JsAstNode*)((JsPropertyNode*)property)->value)) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

static bool js_interp_function_params_supported(JsFunctionNode* function) {
    for (JsAstNode* param = function ? (JsAstNode*)function->params : NULL;
            param; param = (JsAstNode*)param->next) {
        if (!js_interp_pattern_supported(param)) return false;
    }
    return true;
}

static bool js_interp_iteration_head_supported(JsAstNode* left) {
    if (!left) return false;
    if (js_interp_pattern_supported(left) || left->node_type == AST_NODE_IDENT ||
            left->node_type == AST_NODE_MEMBER_EXPR ||
            left->node_type == AST_NODE_INDEX_EXPR) return true;
    if (left->node_type != AST_NODE_VAR_STAM) return false;
    JsVariableDeclarationNode* declaration = (JsVariableDeclarationNode*)left;
    JsVariableDeclaratorNode* declarator = declaration->declarations &&
            declaration->declarations->node_type == AST_NODE_VARIABLE_DECLARATOR
        ? (JsVariableDeclaratorNode*)declaration->declarations : NULL;
    return declarator && !declarator->next && declarator->id &&
        js_interp_pattern_supported((JsAstNode*)declarator->id);
}

static void js_interp_check_node(JsAstNode* node, JsInterpSupportState* state) {
    if (!node || !state || !state->supported) return;
    switch (node->node_type) {
    case AST_SCRIPT: case AST_NODE_BLOCK: case AST_NODE_EXPR_STMT:
    case AST_NODE_VAR_STAM:
    case AST_NODE_LITERAL: case AST_NODE_PRIMARY:
    case AST_NODE_NEW_EXPR:
    case AST_NODE_ARRAY: case AST_NODE_MAP:
    case AST_NODE_CONDITIONAL_EXPR: case AST_NODE_SEQ:
    case AST_NODE_IF_EXPR: case AST_NODE_WHILE_STAM: case AST_NODE_DO_WHILE_STAM:
    case AST_NODE_FOR_STAM: case AST_NODE_RETURN_STAM: case AST_NODE_RAISE_STAM:
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_TRY_STAM:
    case JS_AST_NODE_SWITCH_STATEMENT: case JS_AST_NODE_SWITCH_CASE:
    case JS_AST_NODE_TEMPLATE_LITERAL: case JS_AST_NODE_TEMPLATE_ELEMENT:
    case JS_AST_NODE_SPREAD_ELEMENT:
    case JS_AST_NODE_REGEX:
    case JS_AST_NODE_ARRAY_PATTERN: case JS_AST_NODE_OBJECT_PATTERN:
    case JS_AST_NODE_ASSIGNMENT_PATTERN: case JS_AST_NODE_REST_ELEMENT:
    case JS_AST_NODE_REST_PROPERTY:
        break;
    case JS_AST_NODE_CLASS_DECLARATION:
    case JS_AST_NODE_CLASS_EXPRESSION:
        break;
    case JS_AST_NODE_METHOD_DEFINITION: {
        JsMethodDefinitionNode* method = (JsMethodDefinitionNode*)node;
        if ((method->kind == JsMethodDefinitionNode::JS_METHOD_CONSTRUCTOR &&
                 method->static_method)) state->supported = false;
        break;
    }
    case JS_AST_NODE_FIELD_DEFINITION: {
        JsFieldDefinitionNode* field = (JsFieldDefinitionNode*)node;
        if (field->is_private) state->supported = false;
        break;
    }
    case JS_AST_NODE_STATIC_BLOCK:
    case JS_AST_NODE_LABELED_STATEMENT:
    case JS_AST_NODE_WITH_STATEMENT:
    case JS_AST_NODE_TAGGED_TEMPLATE:
        break;
    case JS_AST_NODE_FOR_OF_STATEMENT:
    case JS_AST_NODE_FOR_IN_STATEMENT: {
        JsForOfNode* loop = (JsForOfNode*)node;
        if (loop->is_await || !js_interp_iteration_head_supported(
                (JsAstNode*)loop->left)) state->supported = false;
        break;
    }
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: {
        JsFunctionNode* function = (JsFunctionNode*)node;
        if (function->is_async || function->is_generator ||
                !js_interp_function_params_supported(function)) state->supported = false;
        break;
    }
    case AST_NODE_IDENT:
        // P2 does not yet materialize function `arguments`/`new.target` or
        // class `super` environments. Reject before instantiation instead of
        // exposing a misleading global binding.
        if (js_interp_identifier_is(node, "arguments") ||
                js_interp_identifier_is(node, "new.target") ||
                js_interp_identifier_is(node, "super") ||
                js_interp_identifier_is(node, "import.meta")) state->supported = false;
        break;
    case AST_NODE_VARIABLE_DECLARATOR:
        if (!((JsVariableDeclaratorNode*)node)->id ||
                !js_interp_pattern_supported((JsAstNode*)((JsVariableDeclaratorNode*)node)->id)) {
            state->supported = false;
        }
        break;
    case AST_NODE_CALL_EXPR: {
        JsCallNode* call = (JsCallNode*)node;
        if (js_interp_identifier_is((JsAstNode*)call->function, "import")) {
            state->supported = false;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        break;
    case AST_NODE_PROPERTY: {
        break;
    }
    case AST_NODE_CATCH_CLAUSE: {
        JsCatchNode* handler = (JsCatchNode*)node;
        if (handler->param && !js_interp_pattern_supported((JsAstNode*)handler->param)) {
            state->supported = false;
        }
        break;
    }
    case AST_NODE_UNARY:
        if (!js_interp_operator_supported(((JsUnaryNode*)node)->op)) state->supported = false;
        break;
    case AST_NODE_BINARY:
        if (!js_interp_operator_supported(((JsBinaryNode*)node)->op)) state->supported = false;
        break;
    case AST_NODE_ASSIGN:
        if (!js_interp_operator_supported(((JsAssignmentNode*)node)->op)) state->supported = false;
        break;
    default:
        state->supported = false;
        break;
    }
    if (state->supported) js_ast_visit_children(node, js_interp_check_child, state);
}

static void js_interp_check_child(JsAstNode* child, void* opaque) {
    js_interp_check_node(child, (JsInterpSupportState*)opaque);
}

bool js_interp_script_is_supported(JsScript* script) {
    if (!script || !script->ast_root) return false;
    JsInterpSupportState state = {true};
    js_interp_check_node((JsAstNode*)script->ast_root, &state);
    return state.supported;
}

Item js_interp_call_function(JsFunction* function, Item* args, int arg_count,
        uint64_t* result_home) {
    (void)result_home;
    if (!function || function->body_kind != JS_FUNCTION_BODY_AST ||
            !function->ast_function || !function->ast_script) return ItemError;
    RootFrame roots(1);
    Rooted<Item> this_root(roots, (function->flags & JS_FUNC_FLAG_ARROW)
        ? function->ast_lexical_this : js_get_this());
    JsInterpEnv* env = js_interp_env_create(function->ast_function->vars,
        function->interp_env);
    JsInterpEnvRoot env_root(env);
    if (!env || !env_root.registered) return ItemError;
    // Direct eval's function-scoped `var` declarations live in the shared
    // EvalContext journal for this activation, including names absent from the
    // static AST scope.
    JsInterpEvalLocalFrame eval_local(env);
    if (!eval_local.pushed) return ItemError;
    JsInterpFrame frame = {function->ast_script, env, this_root.home(),
        (function->flags & JS_FUNC_FLAG_STRICT) != 0, NULL, 0};
    JsInterpCompletion initialized = js_interp_initialize_scope(&frame,
        function->ast_function->vars);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    int index = 0;
    for (JsAstNode* param = (JsAstNode*)function->ast_function->params; param;
            param = (JsAstNode*)param->next, index++) {
        Item value = index < arg_count && args ? args[index] : make_js_undefined();
        JsInterpCompletion bound = js_interp_bind_pattern(&frame, param, value, true);
        if (bound.kind != JS_INTERP_NORMAL) return bound.value;
    }
    JsInterpCompletion result = function->ast_function->body &&
            function->ast_function->body->node_type == AST_NODE_BLOCK
        ? js_interp_exec_block(&frame, (JsBlockNode*)function->ast_function->body)
        : js_interp_eval(&frame, (JsAstNode*)function->ast_function->body);
    if (result.kind == JS_INTERP_RETURN || result.kind == JS_INTERP_NORMAL) return result.value;
    if (result.kind == JS_INTERP_THROW) return result.value;
    return js_throw_syntax_error(js_make_string("illegal control flow"));
}

Item js_interp_execute_script(Runtime* runtime, JsScript* script,
        uint64_t* result_home) {
    (void)result_home;
    if (!runtime || !script || !script->ast_root) return ItemError;
    EvalContext* eval = NULL;
    bool reusing_context = false;
    if (!js_prepare_eval_context(runtime, true, &eval, &reusing_context)) return ItemError;
    (void)eval;
    Input* input = Input::create(context->pool);
    js_runtime_set_input(input);
    // Rejection happens before declaration instantiation or a user-visible
    // runtime action, but after the realm exists so its SyntaxError uses the
    // same JavaScript error lane as an admitted script.
    if (!js_interp_script_is_supported(script)) {
        return js_throw_syntax_error(js_make_string("unsupported AST interpreter script"));
    }
    int global_slots = js_interp_scope_slot_count(script->global_scope);
    if (!lambda_module_state_prepare(script->module_state_id,
            (uint32_t)(global_slots > 0 ? global_slots : 1)) ||
            !js_set_active_module_state_id(script->module_state_id)) {
        return ItemError;
    }
    RootFrame roots(1);
    Rooted<Item> this_root(roots, js_get_global_this());
    JsInterpFrame frame = {script, NULL, this_root.home(), script->strict_mode, NULL, 0};
    JsInterpCompletion initialized = js_interp_initialize_scope(&frame, script->global_scope);
    if (initialized.kind != JS_INTERP_NORMAL) return initialized.value;
    JsInterpCompletion result = js_interp_exec(&frame, (JsAstNode*)script->ast_root);
    return result.kind == JS_INTERP_NORMAL || result.kind == JS_INTERP_RETURN
        ? result.value : result.value;
}

Item js_interp_execute_source(Runtime* runtime, const char* source,
        size_t source_length, const char* filename, uint64_t* result_home) {
    if (!runtime || !source) return ItemError;
    JsTranspiler* transpiler = js_transpiler_create(runtime);
    if (!transpiler || !js_transpiler_parse(transpiler, source, source_length)) {
        js_transpiler_destroy(transpiler);
        return ItemError;
    }
    JsAstNode* ast = build_js_ast_indexed(transpiler, ts_tree_root_node(transpiler->tree));
    if (!ast || js_check_early_errors(transpiler, ast) > 0) {
        js_transpiler_destroy(transpiler);
        return ItemError;
    }
    JsScript* script = js_script_adopt_transpiler(transpiler, runtime,
        filename ? filename : "<inline-js>");
    if (!script) return ItemError;
    return js_interp_execute_script(runtime, script, result_home);
}
