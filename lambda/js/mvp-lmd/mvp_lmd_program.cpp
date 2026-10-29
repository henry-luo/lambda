// mvp_lmd_program.cpp — MIR-free MVP program and realm runtime: unit and
// realm ownership, realm-library ambient state, internal names, callable
// preparation and dynamic-source globals. It is shared by every build;
// only mvp_lmd_mir.cpp generates code (vibe/Lambda_Design_Mobile.md MB22).
#include "mvp_lmd_internal.h"

MvpLmdProgram* mvp_lmd_new_unit(MvpLmdExecution* execution, MvpLmdProgram* realm) {
    if (!execution->units) execution->units = arraylist_new(8);
    if (!execution->units) return NULL;
    auto* program = (MvpLmdProgram*)mem_calloc(1, sizeof(MvpLmdProgram), MEM_CAT_EVAL);
    if (!program) return NULL;
    if (!arraylist_append(execution->units, program)) { mem_free(program); return NULL; }
    program->execution_owner = execution;
    program->realm_program = realm;
    if (realm) {
        program->library = realm->library;
        program->host_enabled = realm->host_enabled;
        program->test262_host = realm->test262_host;
    }
    return program;
}
void mvp_lmd_runtime_reset() {
    if (!context || !js_active_runtime_state) return;
    Runtime* runtime = context->runtime;
    for (int i = 0; runtime && runtime->scripts && i < runtime->scripts->length; i++) {
        js_script_reset_heap(js_script_from_script((Script*)runtime->scripts->data[i]));
    }
    // queued work is already retired by js_batch_reset. Drop heap-derived
    // owners before replacement; Script extensions retain compiler metadata.
    js_runtime_state.formal_mir_owner = NULL;
    js_runtime_state.auto_mir_owner = NULL;
    js_runtime_state.promises.host_ops = NULL;
    js_runtime_state.promises.host_context = NULL;
    js_runtime_state.modules.source_ops = NULL;
    js_runtime_state.modules.source_owner = NULL;
    context_capsule_extensions_drop_owner(context, MVP_LMD_MODULE_OWNER);
    context_capsule_extensions_drop_owner(context, MVP_LMD_DYNAMIC_OWNER);
    context_capsule_extensions_drop_owner(context, MVP_LMD_REGEXP_OWNER);
    context_capsule_extensions_drop_owner(context, MVP_LMD_CAPSULE_OWNER);
}
MvpLmdProgram* mvp_lmd_program_realm(MvpLmdProgram* program) {
    return program && program->realm_program ? program->realm_program : program;
}
MvpLmdProgram* mvp_lmd_program_for_global(MvpLmdProgram* program, Item global) {
    MvpLmdExecution* execution = program ? program->execution_owner : NULL;
    if (!execution || get_type_id(global) != LMD_TYPE_MAP) return NULL;
    auto matches = [&](MvpLmdProgram* candidate) {
        return !candidate->realm_program && candidate->global_receiver && candidate->slots &&
            candidate->slots[candidate->global_slot].item == global.item;
    };
    if (matches(&execution->program)) return &execution->program;
    for (int i = 0; execution->units && i < execution->units->length; i++) {
        auto* candidate = (MvpLmdProgram*)execution->units->data[i];
        if (matches(candidate)) return candidate;
    }
    // prepared native globals may predate the formal realm. They retain the
    // caller's bridge policy when no MVP owner exists; no new realm is created.
    return NULL;
}
uint32_t mvp_lmd_program_realm_key(MvpLmdProgram* program) {
    return program ? mvp_lmd_program_realm(program)->realm_key : 0;
}
extern "C" const JsTemplateObjectOps* mvp_lmd_template_object_ops() {
    static const JsTemplateObjectOps ops = {
        [](int count) { return mvp_lmd_array_new(count); },
        [](Item array, int index, Item value) { return mvp_lmd_array_store(array, index, value); },
        [](Item object, Item raw) { return mvp_lmd_named_set(object, "raw", raw,
            JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE); },
        [](Item object) { return mvp_lmd_object_integrity(object, 1); }
    };
    return &ops;
}
extern "C" Item mvp_lmd_template_object(MvpLmdProgram* program, JsTemplateLiteralNode* literal, int64_t slot) {
    if (program->slots[slot].item) return program->slots[slot];
    int count = ast_linked_node_count(literal->quasis);
    RootSpan values(count * 2);
    if (!values.valid()) return ItemError;
    Item* cooked = values.items(); Item* raw = cooked + count;
    int index = 0;
    for (AstNode* node = literal->quasis; node; node = node->next, index++) {
        JsTemplateElementNode* element = (JsTemplateElementNode*)node;
        cooked[index].item = element->cooked ? s2it(element->cooked) : ITEM_JS_UNDEFINED;
        raw[index].item = s2it(element->raw);
    }
    Item object = js_build_template_object_with_ops(cooked, raw, count, mvp_lmd_template_object_ops());
    // publish only a complete frozen object; the execution owner traces this site cache.
    if (!item_is_error(object)) program->slots[slot] = object;
    return object;
}
Item mvp_lmd_object_create(MvpLmdProgram* p, Item prototype) {
    TypeId type = get_type_id(prototype);
    if (!mvp_lmd_is_object(prototype) && type != LMD_TYPE_NULL)
        return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> parent(roots, prototype), object(roots, mvp_lmd_object_new(p->roots[0], 0));
    if (item_is_error(object.get())) return object.get();
    Item result = mvp_lmd_set_prototype(object.get(), parent.get());
    return item_is_error(result) ? result : object.get();
}
__thread MvpLmdLibraryState** mvp_lmd_library_slot;
__thread Context* mvp_lmd_library_context;
__thread MvpLmdLibraryState* mvp_lmd_foreign_library;
const char* mvp_lmd_program_reference(MvpLmdProgram* program) {
    return program && program->frontend ? program->frontend->reference : NULL;
}
MvpLmdLibraryState* mvp_lmd_program_library(MvpLmdProgram* program) {
    if (program) return program->library_runtime ? program->library : NULL;
    // derived TLS points into the owning context capsule; teardown clears it.
    // an activation may restore an empty cache saved before another turn's first promotion.
    if (context != mvp_lmd_library_context || !mvp_lmd_library_slot) {
        mvp_lmd_library_context = context;
        mvp_lmd_library_slot = (MvpLmdLibraryState**)context_capsule_extension(context, MVP_LMD_CAPSULE_OWNER, 0);
        mvp_lmd_foreign_library = mvp_lmd_library_slot && *mvp_lmd_library_slot &&
            (*mvp_lmd_library_slot)->default_realm ? *mvp_lmd_library_slot : NULL;
    }
    return mvp_lmd_library_slot ? *mvp_lmd_library_slot : NULL;
}
NameRef mvp_lmd_internal_name(MvpLmdInternalName name) {
    static const char* names[] = {"MVP [[Prototype]]", "MVP [[Extensible]]",
        "MVP [[PrimitiveValue]]", "MVP [[DateValue]]", "MVP [[ErrorData]]",
        "MVP [[ParameterMap]]", "MVP [[RegExpSource]]", "MVP [[RegExpFlags]]",
        "RegExp iterator matcher", "RegExp iterator string", "RegExp iterator global",
        "RegExp iterator unicode", "RegExp iterator done", "MVP [[IteratorNext]]",
        "MVP [[CustomElementName]]", "URL record", "PerformanceObserver record",
        "PerformanceObserverEntryList record"};
    static_assert(sizeof(names) / sizeof(names[0]) == LMD_INTERNAL_NAME_COUNT);
    static const ContextCapsuleOps ops = {"js-mvp-internal-names", CONTEXT_CAPSULE_LIFETIME_CONTEXT,
        sizeof(NameRef) * LMD_INTERNAL_NAME_COUNT, NULL, NULL, NULL};
    auto* keys = (NameRef*)context_capsule_extension(context, MVP_LMD_CAPSULE_OWNER, 1);
    if (!keys) {
        keys = (NameRef*)mem_calloc(LMD_INTERNAL_NAME_COUNT, sizeof(NameRef), MEM_CAT_EVAL);
        if (!keys) return NULL;
        if (!context_capsule_extension_install(context, MVP_LMD_CAPSULE_OWNER, 1, keys, &ops)) {
            mem_free(keys); return NULL;
        }
    }
    if (!keys[name]) keys[name] = name_pool_create_unique_private(context->name_pool,
        {names[name], strlen(names[name])});
    return keys[name];
}
MvpLmdRealmScope::MvpLmdRealmScope(MvpLmdProgram* program, bool native_realm) {
    previous = mvp_lmd_program_library();
    foreign = mvp_lmd_foreign_library;
    current = native_realm ? NULL : program ? mvp_lmd_program_library(program) : previous;
    if (current != previous && mvp_lmd_library_slot) {
        *mvp_lmd_library_slot = current;
        mvp_lmd_foreign_library = current && current->default_realm ? current : NULL;
        if (js_active_runtime_state) {
            previous_promise_context = js_runtime_state.promises.host_context;
            js_runtime_state.promises.host_context = native_realm ? NULL : mvp_lmd_program_realm(program);
        }
    }
}
MvpLmdRealmScope::~MvpLmdRealmScope() {
    if (current != previous) {
        if (mvp_lmd_library_slot) *mvp_lmd_library_slot = previous;
        mvp_lmd_foreign_library = foreign;
        if (js_active_runtime_state) js_runtime_state.promises.host_context = previous_promise_context;
    }
}
Item MvpLmdRealmScope::complete(Item result, bool force_error_payload) {
    if (!current || (current == previous && !force_error_payload)) return result;
    if (current->abort_kind && previous && !previous->abort_kind) {
        previous->abort_kind = current->abort_kind; previous->abort_site = current->abort_site;
    }
    if (item_is_error(result)) {
        RootFrame roots(1);
        if (!roots.valid()) return ItemError;
        Rooted<Item> error(roots, result);
        // materialize an implicit JS error before restoring the caller's intrinsics.
        Item caught = mvp_lmd_caught_value(current, error.get());
        return item_is_error(caught) ? caught : error.get();
    }
    return result;
}
struct MvpLmdRealmAmbient {
    Context* context;
    MvpLmdLibraryState** slot;
    MvpLmdLibraryState* current;
    MvpLmdLibraryState* foreign;
};
static void mvp_lmd_realm_save(void* opaque) {
    auto* state = (MvpLmdRealmAmbient*)opaque;
    state->current = mvp_lmd_program_library();
    state->context = mvp_lmd_library_context;
    state->slot = mvp_lmd_library_slot;
    state->foreign = mvp_lmd_foreign_library;
}
static void mvp_lmd_realm_load(const void* opaque) {
    const auto* state = (const MvpLmdRealmAmbient*)opaque;
    mvp_lmd_library_context = state->context;
    mvp_lmd_library_slot = state->slot;
    if (state->slot) *state->slot = state->current;
    mvp_lmd_foreign_library = state->foreign;
}
static const ActivationAmbientHook mvp_lmd_realm_ambient = {
    sizeof(MvpLmdRealmAmbient), mvp_lmd_realm_save, mvp_lmd_realm_save, mvp_lmd_realm_load, NULL};
[[maybe_unused]] static const bool mvp_lmd_realm_ambient_registered =
    activation_register_ambient_hook(&mvp_lmd_realm_ambient);
Item mvp_lmd_function_source(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item receiver, Item) {
    if (!mvp_lmd_is_callable(receiver)) return mvp_lmd_fail(LMD_MVP_TYPE, 0);
    Item dynamic = mvp_lmd_dynamic_function_source(receiver);
    if (dynamic.item != ITEM_JS_UNDEFINED) return dynamic;
    if (!mvp_lmd_is_proxy(receiver)) {
        MvpLmdCallable* function = (MvpLmdCallable*)receiver.function;
        AstNode* definition = function->constructor && function->home && function->home->ast ?
            (AstNode*)function->home->ast : (AstNode*)function->def;
        if (definition) {
            JsTranspiler* source = function->program->frontend;
            SourceSpan span = definition->source_span;
            if (span.end_byte > source->source_length || span.end_byte < span.start_byte)
                return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
            const char* text = source->source + span.start_byte;
            uint32_t length = span.end_byte - span.start_byte;
            // shared method spans omit the class-only static prefix and its trivia.
            // class constructors retain their complete ClassExpression/Declaration source.
            if (definition->node_type != AST_NODE_CLASS && definition->node_type != AST_NODE_CLASS_EXPR)
                js_function_source_span(source->source, source->source_length,
                    (const AstFuncNode*)definition, &text, &length);
            String* result = heap_strcpy(text, length);
            return result ? Item{.item = s2it(result)} : mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        }
        if (function->native_source && function->ptr != (fn_ptr)mvp_lmd_function_bound)
            return js_named_native_function_source_item(((MvpLmdNativeCallable*)function)->source_name);
    }
    // bound and proxy callables never expose their target's retained source.
    return js_named_native_function_source_item(NULL);
}
extern "C" Item mvp_lmd_function_set_name(Item function, Item key, int64_t prefix) {
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    Rooted<Item> owner(roots, function), name(roots, key);
    name.set(js_function_name_display(name.get().get_string(), prefix));
    if (item_is_error(name.get())) return name.get();
    // SetFunctionName defines a configurable property; assignment would ignore
    // the already-created, nonwritable empty name.
    JsPropertyDescriptor descriptor = {};
    descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE;
    js_pd_set_configurable(&descriptor, true);
    descriptor.value = name.get();
    Item prepared = mvp_lmd_function_prepare(owner.get());
    if (item_is_error(prepared)) return prepared;
    MvpLmdProgram* program = ((MvpLmdCallable*)owner.get().function)->program;
    String* property = name_pool_create_len(program->frontend->name_pool, "name", 4);
    if (!property) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    // minimal realms use the owner's canonical pool without initializing native JS intrinsics.
    Item defined = mvp_lmd_define_own_property(owner.get(),
        Item{.item = s2it(property)}, &descriptor);
    return item_is_error(defined) || defined.item == ITEM_FALSE ?
        item_is_error(defined) ? defined : mvp_lmd_fail(LMD_MVP_TYPE, 0) : owner.get();
}
extern "C" Item mvp_lmd_function_prepare(Item owner) {
    Function* function = owner.function;
    // native own properties are materialized by the shared descriptor kernel.
    if (function_has_abi(function, FN_ENTRY_ABI_JS_FUNCTION)) return owner;
    if (function->entry_abi != FN_ENTRY_ABI_MVP_LMD || !function->requires_runtime_context)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    auto* callable = (MvpLmdCallable*)function;
    if (callable->properties || callable->constructor) return owner;
    // Native helpers and AST wrappers have no generated-source metadata.
    if (callable->native_source || !function->def)
        return mvp_lmd_function_materialize(owner, NULL, 0, NULL);
#ifdef LAMBDA_NO_MIR
    // Only generated definitions require compiler naming and constructor-layout facts.
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
#else
    return mvp_lmd_function_materialize_compiled(owner);
#endif
}
bool mvp_lmd_intrinsic_enabled(MvpLmdProgram* p, int i) {
    if ((i == I_STRING || i == I_NUMBER || i == I_BOOLEAN || i == I_BIGINT) && !p->primitives) return false;
    if ((i == I_ERROR || (i >= I_EVAL_ERROR && i <= I_AGGREGATE_ERROR)) && !p->errors) return false;
    if (typed_intrinsic(i) >= 0 && !p->typed_storage) return false;
    if (i >= I_ARRAYBUFFER && i <= I_FLOAT16_ARRAY && !p->buffers) return false;
    if (i == I_SHARED_ARRAYBUFFER && !p->shared_buffer) return false;
    if (i == I_ATOMICS && !p->atomics) return false;
    if (i == I_PROMISE && !p->promises) return false;
    if ((i == I_WEAK_MAP || i == I_WEAK_SET) && !p->weak_collections) return false;
    if ((i == I_WEAK_REF || i == I_FINALIZATION_REGISTRY) && !p->weak_lifetimes) return false;
    if (i == I_EVAL && !p->dynamic_source) return false;
    if (i >= I_SET_TIMEOUT && i <= I_QUEUE_MICROTASK && !p->timers) return false;
    if (i == I_TEST262 && !p->test262_host) return false;
    return !(i >= I_PERFORMANCE && i <= I_REQUIRE && !p->host_active);
}
Item mvp_lmd_intrinsic_value(MvpLmdProgram* p, int i) {
    int library = i == I_JSON ? LMD_LIBRARY_JSON : i == I_DATE ? LMD_LIBRARY_DATE :
        i == I_REGEXP ? LMD_LIBRARY_REGEXP : i == I_SYMBOL ? LMD_LIBRARY_SYMBOL :
        i == I_SET ? LMD_LIBRARY_SET : i == I_REFLECT ? LMD_LIBRARY_REFLECT :
        i == I_PROXY ? LMD_LIBRARY_PROXY : i == I_FUNCTION ? LMD_LIBRARY_FUNCTION :
        i == I_IS_NAN ? LMD_LIBRARY_GLOBAL_NAN :
        i == I_ESCAPE || i == I_UNESCAPE ? LMD_LIBRARY_ESCAPE + i - I_ESCAPE :
        i >= I_DECODE_URI && i <= I_ENCODE_COMPONENT ? LMD_LIBRARY_DECODE_URI + i - I_DECODE_URI : -1;
    if (i == I_PARSE_INT) return p->library->values[LMD_LIBRARY_PARSE_INT];
    return i == I_UNDEFINED ? Item{.item = ITEM_JS_UNDEFINED} : i == I_NAN ? push_d(NAN) :
        i == I_INFINITY ? push_d(INFINITY) : i == I_GLOBAL_THIS ? p->slots[p->global_slot] :
        library >= 0 ? p->library->values[library] : p->slots[p->intrinsics[i - 1].slot];
}
Item mvp_lmd_realm_intrinsic(const char* name, size_t length) {
    MvpLmdLibraryState* library = mvp_lmd_program_library();
    if (!library || !library->intrinsics_ready || library->native_callable_values) return ItemNull;
    MvpLmdProgram* program = library->object.program;
    // immutable intrinsic slots remain authoritative after guest global replacement.
    if (length == 10 && !memcmp(name, "TypedArray", 10))
        return library->typed_values ? library->typed_arrays[
            sizeof(typed_intrinsics) / sizeof(typed_intrinsics[0])].values[0] : ItemNull;
    const struct { const char* name; int slot; } globals[] = {
        {"parseFloat", LMD_LIBRARY_PARSE_FLOAT}, {"isFinite", LMD_LIBRARY_GLOBAL_FINITE},
        {"gc", LMD_LIBRARY_GC}, {"structuredClone", LMD_LIBRARY_STRUCTURED_CLONE}
    };
    for (const auto& global : globals)
        if (strlen(global.name) == length && !memcmp(name, global.name, length))
            return library->values[global.slot];
    if (length == 5 && !memcmp(name, "print", 5))
        return program->host.values ? program->host.values[LMD_HOST_PRINT] : ItemNull;
    for (int i = 1; i < I_COUNT; i++) {
        const char* spelling = intrinsic_names[i - 1];
        if (strlen(spelling) == length && !memcmp(spelling, name, length))
            return mvp_lmd_intrinsic_enabled(program, i) ? mvp_lmd_intrinsic_value(program, i) : ItemNull;
    }
    return ItemNull;
}
Item mvp_lmd_dynamic_global_this(MvpLmdProgram* p, bool required) {
    if (!p && js_active_runtime_state && js_runtime_state.formal_mir_owner) {
        auto* library = mvp_lmd_program_library();
        p = library ? library->object.program : &js_runtime_state.formal_mir_owner->program;
    }
    return p && p->global_receiver
        ? p->slots[p->global_slot] : required ? mvp_lmd_fail(LMD_MVP_CAPABILITY, 0) : ItemNull;
}
bool mvp_lmd_dynamic_retain_script(MvpLmdProgram* p, JsScript* script) {
    if (!p->dynamic_scripts) p->dynamic_scripts = arraylist_new(4);
    return p->dynamic_scripts && arraylist_append(p->dynamic_scripts, script);
}
Item mvp_lmd_dynamic_global(MvpLmdProgram* p, Item key, Item value, int64_t operation, bool strict) {
    bool host_entry = !p;
    if (!p && js_active_runtime_state && js_runtime_state.formal_mir_owner) {
        auto* library = mvp_lmd_program_library();
        p = library ? library->object.program : &js_runtime_state.formal_mir_owner->program;
    }
    p = mvp_lmd_program_realm(p);
    if (!p || !p->dynamic_source || get_type_id(key) != LMD_TYPE_STRING)
        return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    bool script = operation & JS_INTERP_HOST_SCRIPT;
    operation &= ~JS_INTERP_HOST_SCRIPT;
    RootFrame roots(2);
    if (!roots.valid()) return ItemError;
    uint64_t home = 0;
    Rooted<Item> name(roots, key), input(roots, lambda_item_adopt_scalar_home(value, &home));
    if (host_entry && operation == JS_INTERP_HOST_SET) {
        // shared host services publish native callbacks through the existing
        // AST/native callable membrane, preserving its identity cache.
        input.set(mvp_lmd_dynamic_import_value(p, input.get()));
        if (item_is_error(input.get())) return input.get();
    }
    key = name.get(); value = input.get();
    bool found = false;
    Item lexical = mvp_lmd_dynamic_lexical(p, key, value, operation, &found);
    if (found || item_is_error(lexical)) return lexical;
    if (operation == JS_INTERP_HOST_HAS_LEXICAL) return Item{.item = ITEM_FALSE};
    Item global = p->slots[p->global_slot];
    // the checked read shares an own-descriptor lookup while retaining ordered
    // HasProperty/Get for inherited bindings and proxies.
    if (operation == JS_INTERP_HOST_GET)
        return mvp_lmd_class_property(global, name.get(), ItemNull, LMD_PROP_GET_BINDING);
    if (operation == JS_INTERP_HOST_CHECK_DECLARE_LEXICAL) {
        JsPropertyDescriptor descriptor = {};
        Item present = mvp_lmd_get_own_property(global, name.get(), &descriptor);
        if (item_is_error(present)) return present;
        return present.item == ITEM_TRUE && !js_pd_is_configurable(&descriptor)
            ? mvp_lmd_fail(LMD_MVP_SYNTAX, 0) : Item{.item = ITEM_TRUE};
    }
    if (operation == JS_INTERP_HOST_CHECK_DECLARE_FUNCTION || operation == JS_INTERP_HOST_DECLARE_FUNCTION) {
        JsPropertyDescriptor current = {};
        Item present = mvp_lmd_get_own_property(global, name.get(), &current);
        if (item_is_error(present)) return present;
        bool configurable = present.item != ITEM_TRUE || js_pd_is_configurable(&current);
        if (present.item != ITEM_TRUE) {
            Item extensible = mvp_lmd_object_extensible(global, 0);
            if (item_is_error(extensible)) return extensible;
            if (extensible.item != ITEM_TRUE) return mvp_lmd_fail(script ? LMD_MVP_SYNTAX : LMD_MVP_TYPE, 0);
        } else if (!configurable && (!js_pd_is_data(&current) ||
                !(current.flags & JS_PD_WRITABLE) || !(current.flags & JS_PD_ENUMERABLE))) {
            return mvp_lmd_fail(script ? LMD_MVP_SYNTAX : LMD_MVP_TYPE, 0);
        }
        if (operation == JS_INTERP_HOST_CHECK_DECLARE_FUNCTION) return Item{.item = ITEM_TRUE};
        // configurable globals become ordinary eval function bindings; otherwise
        // preserve their attributes and update only the validated data value.
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE;
        descriptor.value = input.get();
        if (configurable) {
            descriptor.flags |= JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE | JS_PD_HAS_CONFIGURABLE |
                JS_PD_WRITABLE | JS_PD_ENUMERABLE;
            js_pd_set_configurable(&descriptor, !script);
        }
        Item defined = mvp_lmd_define_own_property(global, name.get(), &descriptor);
        return !item_is_error(defined) && defined.item != ITEM_TRUE
            ? mvp_lmd_fail(LMD_MVP_TYPE, 0) : defined;
    }
    bool declaration = operation == JS_INTERP_HOST_DECLARE || operation == JS_INTERP_HOST_CHECK_DECLARE;
    Item present = mvp_lmd_property_with_receiver(global, name.get(), ItemNull,
        declaration ? LMD_PROP_OWN : LMD_PROP_HAS, global);
    if (item_is_error(present)) return present;
    if (operation == JS_INTERP_HOST_HAS) return present;
    if (declaration) {
        if (present.item == ITEM_TRUE) return Item{.item = ITEM_TRUE};
        Item extensible = mvp_lmd_object_extensible(global, 0);
        if (item_is_error(extensible)) return extensible;
        if (extensible.item != ITEM_TRUE) return operation == JS_INTERP_HOST_CHECK_DECLARE && input.get().item == ITEM_TRUE
            ? Item{.item = ITEM_FALSE} : mvp_lmd_fail(LMD_MVP_TYPE, 0);
        if (operation == JS_INTERP_HOST_CHECK_DECLARE) return Item{.item = ITEM_TRUE};
        // Script var bindings are non-configurable; eval vars retain their separate contract.
        JsPropertyDescriptor descriptor = {};
        descriptor.flags = JS_PD_HAS_VALUE | JS_PD_HAS_WRITABLE | JS_PD_HAS_ENUMERABLE |
            JS_PD_HAS_CONFIGURABLE | JS_PD_WRITABLE | JS_PD_ENUMERABLE;
        js_pd_set_configurable(&descriptor, !script);
        descriptor.value = Item{.item = ITEM_JS_UNDEFINED};
        return mvp_lmd_define_own_property(global, name.get(), &descriptor);
    }
    if (present.item != ITEM_TRUE && (operation == JS_INTERP_HOST_GET ||
            (operation == JS_INTERP_HOST_SET && strict))) return mvp_lmd_fail(LMD_MVP_REFERENCE, 0);
    int mode = operation == JS_INTERP_HOST_GET ? LMD_PROP_GET :
        operation == JS_INTERP_HOST_SET ? LMD_PROP_SET : LMD_PROP_DELETE;
    return mvp_lmd_property_with_receiver(global, name.get(), input.get(),
        mode | (strict ? LMD_PROP_STRICT : 0), global);
}

void mvp_lmd_program_diagnostic(MvpLmdProgram* p, AstNode* n, const char* reason) {
    if (!p->diagnostic[0]) snprintf(p->diagnostic, sizeof(p->diagnostic),
        "MVP scope error at byte %u: %s", n ? n->source_span.start_byte : 0, reason);
}

static Item math_call(Context*, MvpLmdProgram* program, Item* args, uint64_t count, Item self, Item, Item) {
    const JsBuiltinMethodSpec* operation = (const JsBuiltinMethodSpec*)((MvpLmdNativeCallable*)self.function)->state;
    if (operation->builtin_id == JS_BUILTIN_MATH_RANDOM)
        return push_d(math_splitmix64(&program->random_state));
    return js_math_intrinsic_with_number((JsBuiltinId)operation->builtin_id, args, count,
        [](Item value) { return mvp_lmd_primitive_to_number(value); });
}

void mvp_lmd_nominal_initialize(MvpLmdClass* cls, String* name, MvpLmdProgram* p) {
    cls->program = p;
    cls->nominal.type_name = {name->chars, name->len};
    cls->nominal.struct_kind = LMD_TYPE_MAP;
    cls->nominal.extension = &mvp_lmd_class_extension; cls->nominal.extension_data = cls;
    cls->prototype_nominal = cls->nominal;
    TypeMap* shapes[] = {&cls->shape, &cls->prototype_shape, &cls->static_shape};
    for (int i = 0; i < 3; i++) {
        shapes[i]->type_id = LMD_TYPE_MAP; shapes[i]->type_index = -1;
        shapes[i]->is_nominal = true;
        shapes[i]->nominal = i ? &cls->prototype_nominal : &cls->nominal;
    }
}


void mvp_lmd_require_library(MvpLmdProgram* p, AstNode* site) {
    if (p->library_runtime) return;
    if (!p->library) p->library = (MvpLmdLibraryState*)pool_calloc(p->frontend->pool, sizeof(MvpLmdLibraryState));
    if (!p->library) { mvp_lmd_program_diagnostic(p, site, "builtin metadata allocation"); return; }
    p->library_runtime = true;
    p->library_slot = p->slot_count; p->slot_count += LMD_LIBRARY_COUNT;
}
void mvp_lmd_activate_library(MvpLmdProgram* p, AstNode* site, bool observes_object_mutation) {
    if (observes_object_mutation) p->immutable_objects = false;
    p->library_active = p->receiver_abi = true;
    mvp_lmd_require_library(p, site);
}
void mvp_lmd_admit_shared_realm(MvpLmdProgram* p, AstNode* site) {
    // external source and host callbacks can observe all Script bindings.
    p->dynamic_source = p->global_receiver = p->global_reflection =
        p->receiver_abi = p->property_callbacks = p->symbols = true;
    mvp_lmd_activate_library(p, site);
    if (p->execution_owner && !p->realm_program)
        p->execution_owner->shared_realm = true;
}

bool mvp_lmd_plan_shape_field(MvpLmdProgram* p, TypeMap* shape, String* key, TypeId tid, uint8_t flags) {
    if (!key) return false;
    key = name_pool_create_string(p->frontend->name_pool, key);
    if (!key) return false;
    if (typemap_shape_lookup_last(shape, key->chars, key->len)) return true;
    ShapeEntry* field = (ShapeEntry*)pool_calloc(p->frontend->pool, sizeof(ShapeEntry) + sizeof(StrView));
    if (!field) return false;
    field->name = (StrView*)(field + 1); *field->name = {key->chars, key->len};
    field->key_kind = NAME_KEY_STRING; field->flags = flags;
    field->name_hash = typemap_name_hash(key->chars, key->len);
    shape_entry_set_type(field, type_info[tid].type);
    field->byte_offset = shape->byte_size; shape->byte_size += shape_entry_storage_size(field);
    if (shape->last) shape->last->chain_next = field; else shape->shape = field;
    shape->last = field; shape->length++;
    return true;
}

bool mvp_lmd_plan_callable_shapes(MvpLmdProgram* p, MvpLmdClass* cls, int count,
        uint8_t metadata_flags, uint8_t prototype_flags, TypeId prototype_type) {
    const char* names[] = {"length", "name", "prototype", "constructor"};
    const TypeId types[] = {LMD_TYPE_INT, LMD_TYPE_STRING, prototype_type, LMD_TYPE_FUNC};
    for (int i = 0; i < count; i++) {
        uint8_t flags = JSPD_NON_ENUMERABLE | (i < 2 ? metadata_flags : i == 2 ? prototype_flags : 0);
        if (!mvp_lmd_plan_shape_field(p, i == 3 ? &cls->prototype_shape : &cls->static_shape,
                name_pool_create_len(p->frontend->name_pool, names[i], strlen(names[i])), types[i], flags))
            return false;
    }
    return true;
}

static void plan_library_class(MvpLmdProgram* p, MvpLmdClass* cls, const char* name,
        TypeId prototype_type = LMD_TYPE_MAP) {
    mvp_lmd_nominal_initialize(cls, name_pool_create_len(p->frontend->name_pool, name, strlen(name)), p);
    if (!mvp_lmd_plan_callable_shapes(p, cls, 4, 0, 0, prototype_type))
        mvp_lmd_program_diagnostic(p, NULL, "builtin common shape allocation");
}
void mvp_lmd_plan_library_shapes(MvpLmdProgram* p, bool metadata_only,
        bool collection_iterator_observed, bool regexp_iterator_observed) {
    // key admission inspects the native field inventory without enabling runtime coercion.
    if (!p->library) p->library = (MvpLmdLibraryState*)pool_calloc(p->frontend->pool, sizeof(MvpLmdLibraryState));
    if (!p->library) { mvp_lmd_program_diagnostic(p, NULL, "builtin metadata allocation"); return; }
    if (!p->library->object.shape.type_id) {
        MvpLmdClass* classes[] = {&p->library->date, &p->library->regexp, &p->library->object, &p->library->array, &p->library->generator, &p->library->symbol, &p->library->iterator, &p->library->map, &p->library->set};
        const char* names[] = {"Date", "RegExp", "Object", "Array", "Generator", "Symbol", "Iterator", "Map", "Set"};
        for (int i = 0; i < 9; i++)
            plan_library_class(p, classes[i], names[i], i == 3 ? LMD_TYPE_ARRAY : LMD_TYPE_MAP);
        p->library->date.nominal.base = &p->library->object.nominal;
        p->library->regexp.nominal.base = &p->library->object.nominal;
        p->library->generator.nominal.base = &p->library->object.nominal;
        p->library->iterator.nominal.base = &p->library->object.nominal;
        p->library->map.nominal.base = &p->library->object.nominal;
        p->library->set.nominal.base = &p->library->object.nominal;
        p->library->json_shape = *p->roots[0];
        p->library->json_shape.nominal = &p->library->object.nominal;
        struct Field { TypeMap* shape; const char* name; TypeId type; };
        const Field fields[] = {
            {&p->library->json_shape, "parse", LMD_TYPE_FUNC}, {&p->library->json_shape, "stringify", LMD_TYPE_FUNC},
            {&p->library->date.prototype_shape, "getTime", LMD_TYPE_FUNC},
            {&p->library->date.prototype_shape, "valueOf", LMD_TYPE_FUNC}, {&p->library->date.static_shape, "now", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "toString", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "toLocaleString", LMD_TYPE_FUNC},
            {&p->library->array.prototype_shape, "toLocaleString", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "isPrototypeOf", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "propertyIsEnumerable", LMD_TYPE_FUNC},
            {&p->library->array.static_shape, "isArray", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "hasOwnProperty", LMD_TYPE_FUNC},
            {&p->library->object.prototype_shape, "valueOf", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "is", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "groupBy", LMD_TYPE_FUNC},
            {&p->library->map.static_shape, "groupBy", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "keys", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "values", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "entries", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "hasOwn", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertyNames", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertySymbols", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertyDescriptors", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "defineProperty", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "defineProperties", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "setPrototypeOf", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getOwnPropertyDescriptor", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "preventExtensions", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "seal", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "freeze", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "isExtensible", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "isSealed", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "isFrozen", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "create", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "assign", LMD_TYPE_FUNC},
            {&p->library->object.static_shape, "getPrototypeOf", LMD_TYPE_FUNC},
            {&p->library->array.static_shape, "from", LMD_TYPE_FUNC},
            {&p->library->array.static_shape, "of", LMD_TYPE_FUNC},
            {&p->library->generator.prototype_shape, "next", LMD_TYPE_FUNC},
            {&p->library->generator.prototype_shape, "return", LMD_TYPE_FUNC},
            {&p->library->generator.prototype_shape, "throw", LMD_TYPE_FUNC},
            {&p->library->symbol.static_shape, "iterator", LMD_TYPE_SYMBOL},
            {&p->library->iterator.prototype_shape, "next", LMD_TYPE_FUNC}};
        for (const Field& field : fields) if (!mvp_lmd_plan_shape_field(p, field.shape,
                name_pool_create_len(p->frontend->name_pool, field.name, strlen(field.name)), field.type, JSPD_NON_ENUMERABLE))
            mvp_lmd_program_diagnostic(p, NULL, "builtin shape allocation");
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != JS_BUILTIN_OWNER_SYMBOL_STATIC_METHOD) continue;
            if (!mvp_lmd_plan_shape_field(p, &p->library->symbol.static_shape,
                    name_pool_create_len(p->frontend->name_pool, method->name, method->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                mvp_lmd_program_diagnostic(p, NULL, "Symbol registry shape allocation");
        }
        const JsBuiltinMethodSpec* symbol_description = js_builtin_catalog_find(JS_BUILTIN_OWNER_SYMBOL_INTERNAL, "description", 11);
        if (!mvp_lmd_plan_shape_field(p, &p->library->symbol.prototype_shape,
                name_pool_create_len(p->frontend->name_pool, symbol_description->name, symbol_description->len),
                LMD_TYPE_ANY, JSPD_NON_ENUMERABLE)) mvp_lmd_program_diagnostic(p, NULL, "Symbol description shape allocation");
        if (!mvp_lmd_plan_shape_field(p, &p->library->regexp.shape,
                name_pool_create_len(p->frontend->name_pool, "lastIndex", 9), LMD_TYPE_INT,
                JSPD_NON_ENUMERABLE | JSPD_NON_CONFIGURABLE))
            mvp_lmd_program_diagnostic(p, NULL, "regexp lastIndex shape allocation");
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            if (method->owner != JS_BUILTIN_OWNER_REGEXP_PROTOTYPE_METHOD &&
                    method->owner != JS_BUILTIN_OWNER_REGEXP_ACCESSOR_INTERNAL) continue;
            if (!mvp_lmd_plan_shape_field(p, &p->library->regexp.prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, method->name, method->len),
                    method->property_kind == JS_BUILTIN_PROPERTY_ACCESSOR ? LMD_TYPE_ANY : LMD_TYPE_FUNC,
                    JSPD_NON_ENUMERABLE))
                mvp_lmd_program_diagnostic(p, NULL, "regexp protocol shape allocation");
        }
        for (int i = 0; i < mvp_lmd_builtin_method_count; i++) {
            const MvpLmdBuiltinMethod& method = mvp_lmd_builtin_methods[i];
            if (method.owner == LMD_TYPE_ARRAY && !mvp_lmd_plan_shape_field(p, &p->library->array.prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, method.name, strlen(method.name)), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                mvp_lmd_program_diagnostic(p, NULL, "array builtin shape allocation");
        }
        for (int i = 0; ; i++) {
            const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
            if (!method) break;
            bool set = method->owner == JS_BUILTIN_OWNER_SET_PROTOTYPE_METHOD;
            if (!set && method->owner != JS_BUILTIN_OWNER_MAP_PROTOTYPE_METHOD) continue;
            if (!mvp_lmd_plan_shape_field(p, set ? &p->library->set.prototype_shape : &p->library->map.prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, method->name, method->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                mvp_lmd_program_diagnostic(p, NULL, "collection shape allocation");
        }
        if (!mvp_lmd_plan_shape_field(p, &p->library->set.prototype_shape,
                name_pool_create_len(p->frontend->name_pool, "keys", 4), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
            mvp_lmd_program_diagnostic(p, NULL, "Set keys alias shape allocation");
        typemap_hash_build(&p->library->json_shape, p->frontend->pool);
        for (MvpLmdClass* cls : classes) {
            typemap_hash_build(&cls->shape, p->frontend->pool);
            typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
            typemap_hash_build(&cls->static_shape, p->frontend->pool);
        }
    }
    if (metadata_only) return;
    p->library->date_observed = p->date_observed;
    if (collection_iterator_observed && !p->library->iterator_families) {
        // Proven unobserved units keep their original iterator shape and roots.
        p->library->iterator_families = (MvpLmdClass*)pool_calloc(p->frontend->pool, LMD_ITERATOR_COUNT * sizeof(MvpLmdClass));
        if (!p->library->iterator_families) { mvp_lmd_program_diagnostic(p, NULL, "collection iterator metadata allocation"); return; }
        p->iterator_family_slot = p->slot_count; p->slot_count += LMD_ITERATOR_COUNT * 3;
        for (int family = 0; family < LMD_ITERATOR_COUNT; family++) {
            MvpLmdClass* cls = &p->library->iterator_families[family];
            const char* name = mvp_lmd_iterator_names[family];
            mvp_lmd_nominal_initialize(cls, name_pool_create_len(p->frontend->name_pool, name, strlen(name)), p);
            cls->nominal.base = &p->library->iterator.nominal;
            if (!mvp_lmd_plan_shape_field(p, &cls->prototype_shape,
                    name_pool_create_len(p->frontend->name_pool, "next", 4), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                mvp_lmd_program_diagnostic(p, NULL, "collection iterator next shape allocation");
            typemap_hash_build(&cls->shape, p->frontend->pool);
            typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
        }
    }
    if (regexp_iterator_observed && !p->library->regexp_iterator) {
        p->library->regexp_iterator = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->regexp_iterator) { mvp_lmd_program_diagnostic(p, NULL, "RegExp iterator metadata allocation"); return; }
        p->regexp_iterator_slot = p->slot_count; p->slot_count += 3;
        MvpLmdClass* cls = p->library->regexp_iterator;
        mvp_lmd_nominal_initialize(cls,
            name_pool_create_len(p->frontend->name_pool, "RegExp String Iterator", 22), p);
        cls->nominal.base = &p->library->iterator.nominal;
        const JsBuiltinMethodSpec* next = js_builtin_catalog_find(JS_BUILTIN_OWNER_REGEXP_ITERATOR_INTERNAL, "next", 4);
        if (!mvp_lmd_plan_shape_field(p, &cls->prototype_shape,
                name_pool_create_len(p->frontend->name_pool, next->name, next->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
            mvp_lmd_program_diagnostic(p, NULL, "RegExp iterator next shape allocation");
        typemap_hash_build(&cls->shape, p->frontend->pool);
        typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
    }
    if ((collection_iterator_observed || p->library->regexp_iterator) && !p->library->iterator_base) {
        // reuse the existing observation proof; closed loops keep their original metadata and roots.
        p->library->iterator_base = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->iterator_base) { mvp_lmd_program_diagnostic(p, NULL, "Iterator prototype metadata allocation"); return; }
        p->iterator_base_slot = p->slot_count; p->slot_count += 3;
        MvpLmdClass* cls = p->library->iterator_base;
        mvp_lmd_nominal_initialize(cls, name_pool_create_len(p->frontend->name_pool, "Iterator", 8), p);
        cls->nominal.base = &p->library->object.nominal;
        typemap_hash_build(&cls->shape, p->frontend->pool);
        typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
    }
    if (p->library->iterator_base) {
        TypeNominal* base = &p->library->iterator_base->nominal;
        p->library->iterator.nominal.base = p->library->generator.nominal.base = base;
        if (p->library->iterator_families) for (int i = 0; i < LMD_ITERATOR_COUNT; i++)
            p->library->iterator_families[i].nominal.base = base;
        if (p->library->regexp_iterator) p->library->regexp_iterator->nominal.base = base;
    }
    if (p->weak_collections && !p->library->weak_collections) {
        p->library->weak_class_count = p->weak_lifetimes ? 4 : 2;
        p->library->weak_collections = (MvpLmdClass*)pool_calloc(p->frontend->pool, p->library->weak_class_count * sizeof(MvpLmdClass));
        if (!p->library->weak_collections) { mvp_lmd_program_diagnostic(p, NULL, "weak collection metadata allocation"); return; }
        p->weak_slot = p->slot_count; p->slot_count += 3 * p->library->weak_class_count;
        for (int family = 0; family < p->library->weak_class_count; family++) {
            MvpLmdClass* cls = &p->library->weak_collections[family];
            plan_library_class(p, cls, mvp_lmd_weak_families[family].name);
            cls->nominal.base = &p->library->object.nominal;
            for (int i = 0; ; i++) {
                const JsBuiltinMethodSpec* method = js_builtin_method_at(i);
                if (!method) break;
                if (method->owner != mvp_lmd_weak_families[family].prototype_owner) continue;
                if (!mvp_lmd_plan_shape_field(p, &cls->prototype_shape,
                        name_pool_create_len(p->frontend->name_pool, method->name, method->len), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE))
                    mvp_lmd_program_diagnostic(p, NULL, "weak collection prototype allocation");
            }
            typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
            int intrinsic = family < 2 ? I_WEAK_MAP + family : I_WEAK_REF + family - 2;
            p->intrinsics[intrinsic - 1].kinds = K_FUNCTION;
            p->intrinsics[intrinsic - 1].slot = p->weak_slot + 3 * family;
        }
    }
    if (p->promises && !p->library->promise) {
        p->library->promise = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->promise) { mvp_lmd_program_diagnostic(p, NULL, "Promise metadata allocation"); return; }
        plan_library_class(p, p->library->promise, "Promise");
        p->library->promise->nominal.base = &p->library->object.nominal;
        p->promise_slot = p->slot_count; p->slot_count += 3;
        p->intrinsics[I_PROMISE - 1].slot = p->promise_slot;
    }
    if (p->async_generators && !p->library->async_generator) {
        p->library->async_generator = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->async_generator) { mvp_lmd_program_diagnostic(p, NULL, "async generator metadata allocation"); return; }
        plan_library_class(p, p->library->async_generator, "AsyncGenerator");
        p->library->async_generator->nominal.base = &p->library->object.nominal;
        p->async_generator_slot = p->slot_count; p->slot_count += 5;
    }
    if (p->async_functions && !p->library->async_function) {
        p->library->async_function = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!p->library->async_function) { mvp_lmd_program_diagnostic(p, NULL, "async function metadata allocation"); return; }
        plan_library_class(p, p->library->async_function, "AsyncFunction");
        p->library->async_function->nominal.base = &p->library->object.nominal;
        p->async_function_slot = p->slot_count; p->slot_count += 3;
    }
    if (p->generator_functions && p->property_callbacks && !p->generator_function_slot) {
        p->generator_function_slot = p->slot_count; p->slot_count += 2;
    }
    if (p->errors && !p->library->errors) {
        p->library->errors = (MvpLmdClass*)pool_calloc(p->frontend->pool, LMD_ERROR_COUNT * sizeof(MvpLmdClass));
        if (!p->library->errors) { mvp_lmd_program_diagnostic(p, NULL, "error class metadata allocation"); return; }
        p->error_slot = p->slot_count; p->slot_count += 3 * LMD_ERROR_COUNT;
        for (int i = 0; i < LMD_ERROR_COUNT; i++) {
            MvpLmdClass* cls = &p->library->errors[i];
            plan_library_class(p, cls, mvp_lmd_error_names[i]);
            cls->nominal.base = i ? &p->library->errors[0].nominal : &p->library->object.nominal;
            for (const char* name : {"name", "message"})
                if (!mvp_lmd_plan_shape_field(p, &cls->prototype_shape,
                        name_pool_create_len(p->frontend->name_pool, name, strlen(name)), LMD_TYPE_STRING, JSPD_NON_ENUMERABLE))
                    mvp_lmd_program_diagnostic(p, NULL, "error prototype shape allocation");
            int intrinsic = i ? I_EVAL_ERROR + i - 1 : I_ERROR;
            p->intrinsics[intrinsic - 1].kinds = K_FUNCTION;
            p->intrinsics[intrinsic - 1].slot = p->error_slot + i * 3;
        }
    }
    if (p->primitives && !p->library->primitives) {
        p->library->primitives = (MvpLmdClass*)pool_calloc(p->frontend->pool, LMD_PRIMITIVE_COUNT * sizeof(MvpLmdClass));
        if (!p->library->primitives) { mvp_lmd_program_diagnostic(p, NULL, "primitive class metadata allocation"); return; }
        p->primitive_slot = p->slot_count; p->slot_count += 3 * LMD_PRIMITIVE_COUNT;
        const int intrinsics[] = {I_STRING, I_NUMBER, I_BOOLEAN, I_BIGINT};
        for (int i = 0; i < LMD_PRIMITIVE_COUNT; i++) {
            MvpLmdClass* cls = &p->library->primitives[i];
            plan_library_class(p, cls, mvp_lmd_primitive_names[i]);
            cls->nominal.base = &p->library->object.nominal;
            p->intrinsics[intrinsics[i] - 1].kinds = K_FUNCTION;
            p->intrinsics[intrinsics[i] - 1].slot = p->primitive_slot + i * 3;
        }
    }
    if (p->buffers && !p->library->buffers) {
        p->library->buffers = (MvpLmdClass*)pool_calloc(p->frontend->pool, 2 * sizeof(MvpLmdClass));
        if (!p->library->buffers) { mvp_lmd_program_diagnostic(p, NULL, "buffer class metadata allocation"); return; }
        p->buffer_slot = p->slot_count; p->slot_count += p->test262_host ? 7 : 6;
        p->library->test262_host = p->test262_host;
        if (p->test262_host) p->intrinsics[I_TEST262 - 1].slot = p->buffer_slot + 6;
        for (int i = 0; i < 2; i++) {
            MvpLmdClass* cls = &p->library->buffers[i];
            plan_library_class(p, cls, i ? "DataView" : "ArrayBuffer");
            cls->nominal.base = &p->library->object.nominal;
            p->intrinsics[I_ARRAYBUFFER + i - 1].kinds = K_FUNCTION;
            p->intrinsics[I_ARRAYBUFFER + i - 1].slot = p->buffer_slot + i * 3;
        }
    }
    if (p->atomics && !p->atomics_slot) {
        p->atomics_slot = p->slot_count++;
        p->intrinsics[I_ATOMICS - 1].slot = p->atomics_slot;
    }
    if (p->shared_buffer && !p->library->shared_buffer) {
        MvpLmdClass* cls = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
        if (!cls) { mvp_lmd_program_diagnostic(p, NULL, "shared buffer metadata allocation"); return; }
        p->library->shared_buffer = cls;
        p->shared_buffer_slot = p->slot_count; p->slot_count += 3;
        plan_library_class(p, cls, "SharedArrayBuffer");
        cls->nominal.base = &p->library->object.nominal;
        p->intrinsics[I_SHARED_ARRAYBUFFER - 1].kinds = K_FUNCTION;
        p->intrinsics[I_SHARED_ARRAYBUFFER - 1].slot = p->shared_buffer_slot;
    }
    if (p->typed_storage && !p->library->typed_arrays) {
        p->library->typed_arrays = (MvpLmdClass*)pool_calloc(p->frontend->pool, 13 * sizeof(MvpLmdClass));
        if (!p->library->typed_arrays) { mvp_lmd_program_diagnostic(p, NULL, "typed array metadata allocation"); return; }
        p->typed_slot = p->slot_count; p->slot_count += 39;
        for (int step = 0; step < 13; step++) {
            int type = step ? step - 1 : 12;
            MvpLmdClass* cls = &p->library->typed_arrays[type];
            plan_library_class(p, cls, type == 12 ? "TypedArray" : js_typed_array_spec((JsTypedArrayType)type)->name);
            cls->nominal.base = type == 12 ? &p->library->object.nominal : &p->library->typed_arrays[12].nominal;
            if (type == 12) continue;
            p->intrinsics[typed_intrinsics[type] - 1].kinds = K_FUNCTION;
            p->intrinsics[typed_intrinsics[type] - 1].slot = p->typed_slot + 3 * type;
        }
    }
    p->roots[1]->nominal = &p->library->map.nominal;
    p->roots[0]->nominal = &p->library->object.nominal;
    if (!p->realm_program) p->library->object_shape = p->roots[0];
    p->intrinsics[I_OBJECT - 1].kinds = K_FUNCTION;
    p->intrinsics[I_OBJECT - 1].slot = p->library_slot + LMD_LIBRARY_OBJECT;
    p->intrinsics[I_ARRAY - 1].kinds = K_FUNCTION;
    p->intrinsics[I_ARRAY - 1].slot = p->library_slot + LMD_LIBRARY_ARRAY;
    p->intrinsics[I_MAP - 1].kinds = p->intrinsics[I_SET - 1].kinds = K_FUNCTION;
    p->intrinsics[I_MAP - 1].slot = p->library_slot + LMD_LIBRARY_MAP;
    p->intrinsics[I_MATH - 1].slot = p->library_slot + LMD_LIBRARY_MATH;
}

void mvp_lmd_plan_typed_storage(MvpLmdProgram* p) {
    if (!p->typed_storage) return;
    mvp_lmd_activate_library(p, p->frontend->ast_root);
    p->buffers = p->primitives = p->iterators = p->symbols = true;
    // publishing unused global constructors does not expose ordinary array mutation.
    if (p->typed_explicit) {
        p->property_callbacks = true;
        p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
    }
}

static Item mvp_lmd_global_initialize(MvpLmdProgram* p) {
    Item global = p->slots[p->global_slot];
    RootFrame roots(1);
    if (!roots.valid()) return ItemError;
    Rooted<Item> value(roots, ItemNull);
    for (int i = 1; i < I_COUNT; i++) {
        // public host services keep their shared namespace and loader policies.
        if (p->execution_owner->shared_realm && (i == I_PERFORMANCE || i == I_PROCESS || i == I_REQUIRE)) continue;
        // an unrequested process stays a lazy shared namespace, not an undefined shadow.
        if (i == I_PROCESS && p->host_active &&
                p->host.values[LMD_HOST_PROCESS].item == ITEM_JS_UNDEFINED) continue;
        if (!mvp_lmd_intrinsic_enabled(p, i)) continue;
        value.set(mvp_lmd_intrinsic_value(p, i));
        if (item_is_error(value.get())) return value.get();
        Item result = mvp_lmd_named_set(global, intrinsic_names[i - 1], value.get(), JSPD_NON_ENUMERABLE |
            (i <= I_INFINITY ? JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE : 0));
        if (item_is_error(result)) return result;
    }
    value.set(p->library->values[LMD_LIBRARY_PARSE_FLOAT]);
    if (item_is_error(value.get())) return value.get();
    Item result = mvp_lmd_named_set(global, "parseFloat", value.get(), JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    result = mvp_lmd_named_set(global, "isFinite", p->library->values[LMD_LIBRARY_GLOBAL_FINITE], JSPD_NON_ENUMERABLE);
    if (item_is_error(result)) return result;
    if (p->host_active) {
        // shell output uses the same rooted conversion/write path as console.log.
        result = mvp_lmd_named_set(global, "print", p->host.values[LMD_HOST_PRINT], JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        value.set(mvp_lmd_native_function(p, NULL,
            [](Context*, MvpLmdProgram*, Item* args, uint64_t count, Item self, Item receiver, Item) {
                return js_intrinsic_global_gc_body(self, receiver, args, (int)count, NULL);
            }, 0, "gc"));
        p->library->values[LMD_LIBRARY_GC] = value.get();
        result = item_is_error(value.get()) ? value.get() :
            mvp_lmd_named_set(global, "gc", value.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
        value.set(mvp_lmd_native_function(p, NULL, mvp_lmd_structured_clone, 1, "structuredClone"));
        p->library->values[LMD_LIBRARY_STRUCTURED_CLONE] = value.get();
        result = item_is_error(value.get()) ? value.get() :
            mvp_lmd_named_set(global, "structuredClone", value.get(), JSPD_NON_ENUMERABLE);
        if (item_is_error(result)) return result;
    }
    if (p->execution_owner->host_runtime || p->execution_owner->owned_runtime) {
        result = js_install_host_namespace_slots(global, [](Item object, Item key, Item value) -> Item {
            // core intrinsics and the closed-shell host paths already have owners.
            if (typemap_hash_lookup_key((TypeMap*)object.map->type, key.get_string())) return ItemNull;
            return mvp_lmd_named_set(object, key, value, JSPD_NON_ENUMERABLE);
        }, true);
        if (item_is_error(result)) return result;
    }
    for (int i = 0; !p->execution_owner->shared_realm && !p->script_bindings_pending &&
            p->bindings && i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->reflected_global || !b->entry->scope) continue;
        bool function = b->entry->node && b->entry->node->node_type == AST_NODE_FUNC && !b->entry->is_annex_b_companion;
        Item key = {.item = s2it(b->entry->name)};
        Item own = mvp_lmd_property_has(global, key, 0);
        if (item_is_error(own)) return own;
        if (own.item == ITEM_TRUE && !function) continue;
        result = mvp_lmd_named_set(global, b->entry->name->chars, Item{.item = ITEM_JS_UNDEFINED}, JSPD_NON_CONFIGURABLE);
        if (item_is_error(result)) return result;
    }
    return global;
}

bool mvp_lmd_plan_roots(MvpLmdProgram* p) {
    for (int i = 0; i < 2; i++) {
        const char* name = i ? "MvpLmdMap" : "MvpLmdObject";
        p->families[i].type_name = {name, strlen(name)};
        p->families[i].struct_kind = LMD_TYPE_MAP;
        p->families[i].extension = &mvp_lmd_plain_extension;
        p->families[i].extension_data = p;
        p->roots[i] = (TypeMap*)alloc_type(p->frontend->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!p->roots[i]) return false;
        p->roots[i]->type_id = LMD_TYPE_MAP;
        p->roots[i]->nominal = &p->families[i];
        p->roots[i]->is_nominal = true;
        p->roots[i]->type_index = -1;
    }
    return true;
}

void mvp_lmd_plan_runtime_slots(MvpLmdProgram* p) {
    // host functions capture one canonical global even when source never reflects on it.
    p->global_receiver |= p->host_active;
    if (p->global_receiver) p->global_slot = p->slot_count++;
    if (p->dynamic_source) {
        p->eval_slot = p->slot_count++;
        p->intrinsics[I_EVAL - 1].slot = p->eval_slot;
    }
    if (p->global_reflection) {
        p->global_math_observed = p->math_observed;
        p->global_primitive_method_overrides = p->primitive_method_overrides;
        mvp_lmd_activate_library(p, p->frontend->ast_root);
        p->primitives = p->errors = p->math_observed = p->buffers = p->shared_buffer = p->date_observed = p->weak_collections = p->weak_lifetimes = p->promises = p->atomics = true;
        p->primitive_method_overrides = true;
        if (p->host_enabled) p->timers = p->promises = true;
        if (p->host_enabled) {
            p->native_host_values = true;
            if (!p->host_active) {
                p->host_active = true; p->host_slot = p->slot_count; p->slot_count += LMD_HOST_COUNT;
            }
            // an earlier single-host read allocated the span without indexing its unused siblings.
            for (int i = I_PERFORMANCE; i <= I_REQUIRE; i++) p->intrinsics[i - 1].slot = p->host_slot + i - I_PERFORMANCE;
        }
        if (p->property_callbacks) p->array_method_overrides = p->map_method_overrides = p->primitive_method_overrides = true;
    }
    if (p->timers) {
        p->timer_slot = p->slot_count; p->slot_count += LMD_TIMER_COUNT;
        for (int i = I_SET_TIMEOUT; i <= I_QUEUE_MICROTASK; i++) {
            p->intrinsics[i - 1].slot = p->timer_slot + i - I_SET_TIMEOUT;
            p->intrinsics[i - 1].kinds = K_FUNCTION;
        }
    }
    // parameter and receiver kinds are still seeds here; decide storage from their solved domains below.
    p->typed_storage |= p->buffers || p->global_reflection || (p->typed_explicit && p->property_callbacks);
    mvp_lmd_plan_typed_storage(p);
}

void mvp_lmd_plan_dynamic_features(MvpLmdProgram* p) {
    if (p->dynamic_source) {
        // later interpreted or imported source shares these bindings; scheduling still initializes lazily.
        p->timers = p->promises = p->iterators = p->errors = true;
        p->async_generators = p->async_functions = p->generator_functions = true;
    }
}

Item mvp_lmd_instantiate_runtime(MvpLmdExecution* e, MvpLmdProgram* p,
        const MvpLmdHost* host, const MvpLmdRuntimeFacts& facts) {
    p->random_state = time_now_ns() ^ (uint64_t)e;
    if (p->slot_count) {
        p->slots = (Item*)mem_calloc((size_t)p->slot_count * 2, sizeof(Item), MEM_CAT_EVAL);
        if (!p->slots || !heap_try_register_gc_root_range((uint64_t*)p->slots, (int)p->slot_count)) return ItemError;
        p->slots_registered = true;
    }
    for (int i = 0; p->classes && i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        cls->values = p->slots + cls->slot;
        cls->initializers = p->slots + cls->initializer_slot;
    }
    if (p->realm_program) {
        // unit-owned slots retain literals/classes; all host and intrinsic values have one realm owner.
        p->slots[p->global_slot] = p->realm_program->slots[p->realm_program->global_slot];
        p->slots[p->eval_slot] = p->realm_program->slots[p->realm_program->eval_slot];
        return ItemNull;
    }
    bool existing_global = false;
    if (p->global_receiver) {
        JsGlobalEnvironment* environment = !p->realm_key && js_runtime_state_for(e->owner_context)
            ? js_runtime_state.global_environment : NULL;
        if (environment) {
            if (!js_global_environment_ensure(environment)) return ItemError;
            p->slots[p->global_slot] = js_global_environment_slot(environment, JS_GLOBAL_ENV_GLOBAL_THIS);
            existing_global = get_type_id(p->slots[p->global_slot]) == LMD_TYPE_MAP;
        }
        if (!existing_global) {
            p->slots[p->global_slot] = mvp_lmd_object_new(p->roots[0], 0);
            if (item_is_error(p->slots[p->global_slot])) return p->slots[p->global_slot];
            if (environment) {
                // publish once, before native factories can capture the realm global.
                js_global_environment_slot(environment, JS_GLOBAL_ENV_GLOBAL_THIS) = p->slots[p->global_slot];
                js_global_environment_slot(environment, JS_GLOBAL_ENV_WINDOW_EVENT) = make_js_undefined();
                environment->window_event_intercept_enabled = true;
            }
        }
    }
    if (p->library_runtime) {
        p->library->native_callable_values = existing_global;
        p->library->values = p->slots + p->library_slot;
        if (p->library->iterator_base)
            p->library->iterator_base_values = p->slots + p->iterator_base_slot;
        if (p->library->regexp_iterator)
            p->library->regexp_iterator_values = p->slots + p->regexp_iterator_slot;
        if (p->library->iterator_families)
            p->library->iterator_family_values = p->slots + p->iterator_family_slot;
        if (p->errors) p->library->error_values = p->slots + p->error_slot;
        if (p->primitives) p->library->primitive_values = p->slots + p->primitive_slot;
        if (p->buffers) p->library->buffer_values = p->slots + p->buffer_slot;
        if (p->shared_buffer) p->library->shared_buffer_values = p->slots + p->shared_buffer_slot;
        if (p->atomics) p->library->atomics_value = p->slots + p->atomics_slot;
        if (p->weak_collections) p->library->weak_values = p->slots + p->weak_slot;
        if (p->typed_storage) p->library->typed_values = p->slots + p->typed_slot;
        if (p->promises) p->library->promise_values = p->slots + p->promise_slot;
        if (p->async_generators) p->library->async_generator_values = p->slots + p->async_generator_slot;
        if (p->async_functions) p->library->async_function_values = p->slots + p->async_function_slot;
        if (p->generator_function_slot) p->library->generator_function_values = p->slots + p->generator_function_slot;
        p->library->mutable_prototypes = p->property_callbacks;
        p->library->function_prototype_observed = p->function_prototype_observed;
        p->library->regexp_protocol_unobserved = facts.regexp_protocol_unobserved;
        static const ContextCapsuleOps ops = {"js-mvp-library", CONTEXT_CAPSULE_LIFETIME_CONTEXT,
            sizeof(MvpLmdLibraryState*), NULL, NULL, [](void* opaque) {
                // the optional fast selector must not outlive its context capsule.
                if (mvp_lmd_library_slot == opaque) {
                    mvp_lmd_library_slot = NULL; mvp_lmd_foreign_library = NULL;
                    mvp_lmd_library_context = NULL;
                }
                mem_free(opaque);
            }};
        auto** state = (MvpLmdLibraryState**)context_capsule_extension(e->owner_context, MVP_LMD_CAPSULE_OWNER, 0);
        if (!state) {
            state = (MvpLmdLibraryState**)mem_alloc(sizeof(*state), MEM_CAT_EVAL);
            if (!state) return ItemError;
            if (!context_capsule_extension_install(e->owner_context, MVP_LMD_CAPSULE_OWNER, 0, state, &ops)) {
                mem_free(state); return ItemError;
            }
        }
        *state = p->library;
        mvp_lmd_library_slot = state;
        mvp_lmd_library_context = e->owner_context;
        Item result = mvp_lmd_library_initialize(p, p->library);
        if (item_is_error(result)) return result;
        Item& math = p->library->values[LMD_LIBRARY_MATH];
        math = existing_global ? js_get_math_object_value() : mvp_lmd_object_new(p->roots[0], 0);
        if (item_is_error(math)) result = math;
        for (int i = 0; !existing_global && !item_is_error(result) && i < JS_INTRINSIC_BINDING_COUNT; i++) {
            const JsBuiltinMethodSpec* operation = js_builtin_method_at(i);
            if (operation->owner != JS_BUILTIN_OWNER_MATH_METHOD) continue;
            result = mvp_lmd_named_set(math, operation->name, mvp_lmd_native_function(p,
                (void*)operation, math_call, operation->param_count, operation->display_name ? operation->display_name : operation->name),
                JSPD_NON_ENUMERABLE);
        }
        for (int i = 0; !existing_global && !item_is_error(result) && i < js_math_constant_count; i++) {
            const JsMathConstantSpec& constant = js_math_constants[i];
            result = mvp_lmd_named_set(math, constant.name, push_d(constant.value),
                JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE | JSPD_NON_CONFIGURABLE);
        }
        if (!existing_global && !item_is_error(result)) {
            result = mvp_lmd_named_set(math,
                Item{.item = s2it(well_known_name_ref(JS_SYMBOL_TO_STRING_TAG))},
                Item{.item = s2it(heap_create_name("Math", 4))}, JSPD_NON_ENUMERABLE | JSPD_NON_WRITABLE);
        }
        if (item_is_error(result)) return result;
    }
    if (p->promises) {
        Item initialized = mvp_lmd_promise_initialize(p, p->library);
        if (item_is_error(initialized)) return initialized;
    }
    if (p->async_generators || p->async_functions || p->generator_function_slot) {
        Item initialized = mvp_lmd_suspended_functions_initialize(p, p->library);
        if (item_is_error(initialized)) return initialized;
    }
    if (p->dynamic_source) {
        Item initialized = mvp_lmd_dynamic_initialize(p);
        if (item_is_error(initialized)) return initialized;
        p->slots[p->eval_slot] = initialized;
    }
    if (existing_global && p->library_runtime) p->library->default_realm = p->library;
    if (p->library_runtime) p->library->intrinsics_ready = true;
    if (p->host_active || p->timers) {
        p->host.values = p->host_active ? p->slots + p->host_slot : NULL;
        p->host.timers = p->timers ? p->slots + p->timer_slot : NULL;
        p->host.global = p->global_receiver ? p->slots + p->global_slot : NULL;
        p->host.process_requested = facts.process_requested;
        p->host.process_stdio_only = !existing_global && facts.process_stdio_only;
        Item result = mvp_lmd_host_initialize(p, &p->host, p->roots[0], host);
        if (item_is_error(result)) return result;
    }
    if (p->global_receiver) {
        if (p->global_reflection && !existing_global && !item_is_error(p->slots[p->global_slot])) {
            Item initialized = mvp_lmd_global_initialize(p);
            if (item_is_error(initialized)) return initialized;
        }
        if (item_is_error(p->slots[p->global_slot])) {
            return p->slots[p->global_slot];
        }
        // a public receiver-only unit need not materialize any builtin library.
        if (p->test262_host && p->library && p->library->buffer_values) {
            Item result = mvp_lmd_named_set(p->library->buffer_values[6], "global", p->slots[p->global_slot]);
            if (item_is_error(result)) return result;
        }
    }
    return ItemNull;
}

bool mvp_lmd_plan_open_realm(MvpLmdProgram* p) {
    // open source can observe every intrinsic; no compiler facts or empty MIR unit are needed.
    p->frontend = js_transpiler_create(NULL);
    if (!p->frontend || !mvp_lmd_plan_roots(p)) return false;
    mvp_lmd_admit_shared_realm(p, NULL);
    mvp_lmd_plan_dynamic_features(p);
    mvp_lmd_plan_runtime_slots(p);
    mvp_lmd_plan_library_shapes(p, false, true, true);
    return !p->diagnostic[0];
}

Item mvp_lmd_create_realm(Context*, MvpLmdProgram* owner, Item*, uint64_t, Item, Item, Item) {
    MvpLmdExecution* execution = owner->execution_owner;
    if (!execution) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
    if (execution->next_realm_key == UINT32_MAX) return ItemError;
    auto* program = mvp_lmd_new_unit(execution, NULL);
    if (!program) return ItemError;
    program->realm_key = ++execution->next_realm_key;
    program->module_realm = true; // complete intrinsic surface, with its own global and lexical owner
    program->host_enabled = owner->host_enabled;
    program->test262_host = owner->test262_host;
    if (!mvp_lmd_plan_open_realm(program)) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    program->library->default_realm = execution->program.library;
    MvpLmdRealmScope realm(program);
    Item result = mvp_lmd_instantiate_runtime(execution, program, execution->host, {});
    return realm.complete(item_is_error(result) ? result : program->library->buffer_values[6]);
}

Runtime* mvp_lmd_prepare_module_realm(void* owner) {
    auto* program = mvp_lmd_program_realm((MvpLmdProgram*)owner);
    auto* execution = program->execution_owner;
    if (program->module_controller) return program->module_controller->active_runtime;
    auto* realm = (MvpLmdRealm*)mem_calloc(1, sizeof(MvpLmdRealm), MEM_CAT_EVAL);
    if (!realm) return NULL;
    if (!execution->units) execution->units = arraylist_new(8);
    if (!execution->units) { mem_free(realm); return NULL; }
    realm->execution = execution;
    realm->program = program;
    realm->host = execution->host;
    realm->runtime.eval_context = execution->owner_context;
    realm->runtime.use_mir_direct = true;
    realm->runtime.optimize_level = 2;
    Runtime* parent = execution->host_runtime ? execution->host_runtime : execution->owned_runtime;
    if (parent) {
        realm->runtime.resource_policy = parent->resource_policy;
        realm->runtime.js_document_base_url = parent->js_document_base_url;
        realm->runtime.js_import_maps = parent->js_import_maps;
        realm->runtime.import_base_dir = parent->import_base_dir;
    }
    // child realms share the heap and scheduler, but own independent module registries.
    realm->active_runtime = parent && !program->realm_key ? parent : &realm->runtime;
    static const ContextCapsuleOps capsule = {"js-mvp-module-realm", CONTEXT_CAPSULE_LIFETIME_REALM,
        sizeof(MvpLmdRealm), NULL, NULL, [](void* opaque) {
            auto* realm = (MvpLmdRealm*)opaque;
            if (realm->active_runtime == &realm->runtime)
                module_registry_cleanup_for_runtime(&realm->runtime);
            // native agent AST bodies can outlive their queues until shared-heap
            // finalization; the existing program script list owns that tail.
            if (realm->runtime.scripts) {
                if (!realm->program->dynamic_scripts)
                    realm->program->dynamic_scripts = arraylist_new(8);
                for (int i = 0; i < realm->runtime.scripts->length; i++)
                    arraylist_append(realm->program->dynamic_scripts, realm->runtime.scripts->data[i]);
                realm->runtime.scripts->length = 0;
                runtime_free_all_scripts(&realm->runtime);
            }
            realm->program->module_controller = NULL;
            if (realm->execution->realm == realm) realm->execution->realm = NULL;
            mem_free(realm);
        }};
    if (!context_capsule_extension_install(execution->owner_context, MVP_LMD_MODULE_OWNER,
            program->realm_key, realm, &capsule)) { mem_free(realm); return NULL; }
    program->module_controller = realm;
    if (!program->realm_key) {
        execution->realm = realm;
        if (!parent) execution->owner_context->runtime = &realm->runtime;
    }
    return realm->active_runtime;
}

#ifdef LAMBDA_NO_MIR
// MVP source-module lowering remains in mvp_lmd_mir.cpp. Runtime-only
// realms and dynamic classes no longer depend on it (D8.1.3v42).
static Item mvp_lmd_generator_excluded(const char* entry) {
    log_error("mvp-lmd: %s needs MIR code generation, which this build excludes", entry);
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0, "MIR code generation is excluded from this build");
}
Item mvp_lmd_dynamic_import(MvpLmdProgram*, Item, const char*) {
    return mvp_lmd_generator_excluded("dynamic import");
}
#endif

MvpLmdRetainedScript* mvp_lmd_retain_execution(Runtime* runtime) {
    auto* retained = (MvpLmdRetainedScript*)mem_calloc(1, sizeof(MvpLmdRetainedScript), MEM_CAT_EVAL);
    if (!retained) return NULL;
    if (!runtime->scripts) runtime->scripts = arraylist_new(8);
    if (!runtime->scripts) { mem_free(retained); return NULL; }
    retained->destroy_extension = [](Script* script) {
        mvp_lmd_destroy(((MvpLmdRetainedScript*)script)->execution);
    };
    runtime_register_script(runtime, retained);
    return retained;
}

void mvp_lmd_release_unit(MvpLmdProgram* p) {
    mem_free(p->slots); mem_free(p->intrinsic_uses);
#ifndef LAMBDA_NO_MIR
    if (p->code) mvp_lmd_code_release(p->code);
    else if (p->mir) { if (p->linked) MIR_gen_finish(p->mir); MIR_finish(p->mir); }
#endif
    if (p->relocations) arraylist_free(p->relocations);
    if (p->functions) {
        for (int i = 0; i < p->functions->length; i++) {
            LmdFunction* f = (LmdFunction*)p->functions->data[i];
            if (f->captures) arraylist_free(f->captures);
            mem_free(f);
        }
        arraylist_free(p->functions);
    }
    if (p->bindings) {
        for (int i = 0; i < p->bindings->length; i++) mem_free(p->bindings->data[i]);
        arraylist_free(p->bindings);
    }
    if (p->import_cache) hashmap_free(p->import_cache);
    if (p->private_bindings) hashmap_free(p->private_bindings);
    if (p->objects) arraylist_free(p->objects);
    if (p->classes) arraylist_free(p->classes);
    if (p->dynamic_scripts) {
        for (int i = 0; i < p->dynamic_scripts->length; i++)
            runtime_free_script(NULL, (Script*)p->dynamic_scripts->data[i], false);
        arraylist_free(p->dynamic_scripts);
    }
    if (p->frontend) js_transpiler_destroy(p->frontend);
}
void mvp_lmd_destroy(MvpLmdExecution* e) {
    if (!e) return;
    MvpLmdProgram* p = &e->program;
    if (e->owned_runtime) {
        // D5.3/D5.4.1: one owner retires jobs and heap before compiler metadata.
        runtime_cleanup(e->owned_runtime);
        mem_free(e->owned_runtime);
    }
    if (e->units) {
        for (int index = 0; index < e->units->length; index++) {
            auto* unit = (MvpLmdProgram*)e->units->data[index];
            mvp_lmd_release_unit(unit); mem_free(unit);
        }
        arraylist_free(e->units);
    }
    if (e->realm) mem_free(e->realm);
    mvp_lmd_release_unit(p);
    mem_free(e);
}

Item mvp_lmd_ensure_runtime_realm(Runtime* runtime) {
    if (!runtime) return ItemError;
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!owner || (context && context != owner)) return ItemError;
    bool reusing_context = false;
    if (!js_prepare_eval_context(runtime, true, &owner, &reusing_context, true) ||
            !js_runtime_state_ensure_input(owner)) return ItemError;
    if (js_runtime_state.formal_mir_owner) return ItemNull;
    MvpLmdRetainedScript* retained = mvp_lmd_retain_execution(runtime);
    if (!retained) return ItemError;
    auto* execution = (MvpLmdExecution*)mem_calloc(1, sizeof(MvpLmdExecution), MEM_CAT_EVAL);
    if (!execution) return ItemError;
    // Script ownership keeps nominal records alive until after heap finalization (D5.3).
    retained->execution = execution;
    execution->owner_context = owner;
    execution->host_runtime = runtime;
    execution->retained_host.output = stdout;
    execution->retained_host.native_timers = true;
    execution->retained_host.argv = js_process_bootstrap_arguments(&execution->retained_host.argc);
    execution->host = &execution->retained_host;
    auto* program = &execution->program;
    execution->last_program = program;
    program->execution_owner = execution;
    program->host_enabled = program->test262_host = program->module_realm = true;
    program->failure_phase = MVP_LMD_INTERNAL_FAILURE;
    if (!mvp_lmd_plan_open_realm(program)) return ItemError;
    Item result = mvp_lmd_instantiate_runtime(execution, program, execution->host, {});
    if (item_is_error(result)) return result;
    js_runtime_state.formal_mir_owner = execution;
    return ItemNull;
}
Item mvp_lmd_create_interpreter_realm() {
    // Native AST callers adopt their existing global; the child gets independent MVP intrinsics.
    Item ready = mvp_lmd_ensure_runtime_realm(context ? context->runtime : NULL);
    if (item_is_error(ready)) return ready;
    return mvp_lmd_create_realm(context, &js_runtime_state.formal_mir_owner->program,
        NULL, 0, ItemNull, ItemNull, ItemNull);
}
