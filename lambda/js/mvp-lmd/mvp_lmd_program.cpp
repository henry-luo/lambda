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
#ifdef LAMBDA_NO_MIR
    // MVP callables come only from generated code, which this build excludes
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
#else
    return mvp_lmd_function_materialize(owner);
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

#ifdef LAMBDA_NO_MIR
// Code generation is excluded from this build. These entries compile or plan
// through mvp_lmd_mir.cpp and are reachable only from an MVP program, which a
// T0-only build never creates; each reports the exclusion explicitly
// (vibe/Lambda_Design_Mobile.md MB22).
static Item mvp_lmd_generator_excluded(const char* entry) {
    log_error("mvp-lmd: %s needs MIR code generation, which this build excludes", entry);
    return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0, "MIR code generation is excluded from this build");
}
Item mvp_lmd_dynamic_class(MvpLmdProgram*, AstClassNode*, Item, Item) {
    return mvp_lmd_generator_excluded("dynamic class");
}
Item mvp_lmd_create_realm(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item) {
    return mvp_lmd_generator_excluded("createRealm");
}
Runtime* mvp_lmd_prepare_module_realm(void*) {
    (void)mvp_lmd_generator_excluded("module realm");
    return NULL;
}
Item mvp_lmd_dynamic_import(MvpLmdProgram*, Item, const char*) {
    return mvp_lmd_generator_excluded("dynamic import");
}
#endif
