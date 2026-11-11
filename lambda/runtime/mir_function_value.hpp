#pragma once

#include "mir_emitter_shared.hpp"
#include "recovery_frame.h"

// EvalContext extends Context through single non-virtual inheritance.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static constexpr size_t MIR_EVAL_CONTEXT_VARGS_OFFSET = offsetof(EvalContext, current_vargs);
#pragma clang diagnostic pop

typedef MIR_reg_t (*MirArgumentItem)(void*, int);

static inline const char* em_binding_boundary_site(MirEmitter* em,
        const char* kind, String* name) {
    StrBuf* label = strbuf_new_cap(64);
    strbuf_append_format(label, "%s '%.*s'", kind,
        name ? (int)name->len : 0, name ? name->chars : "");
    const char* result = mir_em_persist_cstr(em, label->str).str;
    strbuf_free(label);
    return result;
}

static inline MIR_reg_t em_error_stamp_site(MirEmitter* em, MIR_reg_t error,
        const char* reference, const AstSysFuncNode* site) {
    // constructor locations are definition-site metadata, shared by both MIR
    // front ends rather than supplied by an eventual error observer (S7.4.4).
    const char* file = mir_em_persist_cstr(em, reference ? reference : "").str;
    return em_call_4(em, "lambda_error_stamp_site", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, error), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)file),
        MIR_T_I64, MIR_new_int_op(em->ctx, site->site_line),
        MIR_T_I64, MIR_new_int_op(em->ctx, site->site_column), true);
}

// S7.4.4: a successful admission stays lazy; only rejection resolves its
// definition-site source point. The pooled label outlives the JIT image.
static inline MIR_reg_t em_type_boundary_check(MirEmitter* em, Pool* pool,
        const char* source, const char* reference, const AstNode* site_node,
        MIR_reg_t value, Type* expected, const char* site, MIR_reg_t env = 0,
        bool terminal = false) {
    LambdaBoundary* boundary = (LambdaBoundary*)pool_calloc(pool, sizeof(LambdaBoundary));
    const char* text = site ? site : "typed boundary";
    boundary->text = pool_dup_n(pool, text, strlen(text));
    if (site_node) {
        boundary->source = source;
        boundary->file = reference;
        boundary->source_offset = site_node->source_span.start_byte;
    }
    MIR_type_t types[] = {MIR_T_I64, MIR_T_P, MIR_T_P, MIR_T_P};
    MIR_op_t args[] = {MIR_new_reg_op(em->ctx, value), MIR_new_int_op(em->ctx, (intptr_t)expected),
        env ? MIR_new_reg_op(em->ctx, env) : MIR_new_int_op(em->ctx, 0),
        MIR_new_int_op(em->ctx, (intptr_t)boundary)};
    return terminal ? em_call_terminal_with_args(em, "lambda_type_check_lazy", MIR_T_I64, 4, types, args, false)
        : em_call_with_args(em, "lambda_type_check_lazy", MIR_T_I64, 4, types, args, false);
}

// state defaults run only on first admission of this model/template/name.
static inline MIR_reg_t em_template_state_read(MirEmitter* em, MIR_reg_t model,
        const char* reference, AstStateEntry* state, bool initialize) {
    MIR_op_t model_op = MIR_new_reg_op(em->ctx, model);
    MIR_op_t reference_op = MIR_new_int_op(em->ctx, (intptr_t)reference);
    MIR_op_t name_op = MIR_new_int_op(em->ctx, (intptr_t)state->name->chars);
    if (!initialize)
        return em_call_3(em, "tmpl_state_get", MIR_T_I64,
            MIR_T_I64, model_op, MIR_T_P, reference_op, MIR_T_P, name_op, true);
    MIR_reg_t fallback = em_new_reg(em, "state_default", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, fallback), MIR_new_int_op(em->ctx, ITEM_NULL)));
    if (state->value) {
        MIR_reg_t initialized = em_call_3(em, "tmpl_state_has", MIR_T_I64,
            MIR_T_I64, model_op, MIR_T_P, reference_op, MIR_T_P, name_op, true);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8,
            MIR_new_reg_op(em->ctx, initialized), MIR_new_reg_op(em->ctx, initialized)));
        MIR_label_t ready = em_new_label(em);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
            MIR_new_label_op(em->ctx, ready), MIR_new_reg_op(em->ctx, initialized)));
        MirValue value = em_require_rep(em, em_lower_value(em, state->value, MIR_VALUE_ANY), VALUE_REP_ITEM);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
            MIR_new_reg_op(em->ctx, fallback), MIR_new_reg_op(em->ctx, value.reg)));
        em_emit_label(em, ready);
    }
    if (em->root_call_value) em->root_call_value(em->call_owner, fallback, JIT_VALUE_BOXED_ITEM);
    return em_call_4(em, "tmpl_state_get_or_init", MIR_T_I64,
        MIR_T_I64, model_op, MIR_T_P, reference_op, MIR_T_P, name_op,
        MIR_T_I64, MIR_new_reg_op(em->ctx, fallback), true);
}

// conversion returns an Item so rejected inputs retain their error value.
static inline MIR_reg_t em_numeric_type_call(MirEmitter* em, Type* target, MIR_reg_t item) {
    return target->type_id == LMD_TYPE_NUM_SIZED
        ? em_call_2(em, "coerce_num_sized", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, item),
            MIR_T_I64, MIR_new_int_op(em->ctx, type_num_sized_kind(target)), true)
        : em_call_1(em, "coerce_uint64", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, item), true);
}

// JS exports retain a Function object and its module environment, not a Core
// code pointer. The shared bridge writes cold scalars into this frame's home.
static inline MIR_reg_t em_js_export_call(MirEmitter* em, MIR_item_t target,
        int count, const MIR_op_t* arguments) {
    MIR_reg_t function = em_new_reg(em, "js_export", MIR_T_P);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, function), MIR_new_ref_op(em->ctx, target)));
    if (em->root_call_value)
        em->root_call_value(em->call_owner, function, JIT_VALUE_RAW_GC_POINTER);
    int slot = em_scalar_home_new(em);
    MIR_reg_t home = em_materialize_frame_ref(em, em_scalar_home_ref(em, slot));
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS + 2];
    MIR_op_t operands[LAMBDA_MAX_FUNCTION_ARGS + 2];
    types[0] = MIR_T_P; operands[0] = MIR_new_reg_op(em->ctx, function);
    for (int i = 0; i < count; i++) {
        types[i + 1] = MIR_T_I64; operands[i + 1] = arguments[i];
    }
    types[count + 1] = MIR_T_P; operands[count + 1] = MIR_new_reg_op(em->ctx, home);
    const char* bridge = mir_em_format_name(em, "js_call_export_%d_into", count);
    MIR_reg_t result = em_call_with_args(em, bridge, MIR_T_I64,
        count + 2, types, operands, true);
    em_scalar_home_bind(em, slot, result);
    return result;
}

// D4.1.1/D4.1.4: admission holds inner contracts; expressions read type Items.
static inline MIR_reg_t em_binder_type_value(MirEmitter* em, MIR_reg_t environment,
        uint16_t slot, Type* bound) {
    MIR_reg_t selected = em_load_at(em, environment, slot * sizeof(Type*),
        MIR_T_I64, "binder_type");
    // S11.4.8v2: an empty nested site selects its declared bound.
    MIR_label_t ready = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, ready),
        MIR_new_reg_op(em->ctx, selected), MIR_new_int_op(em->ctx, 0)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, selected),
        MIR_new_int_op(em->ctx, (intptr_t)bound)));
    em_emit_label(em, ready);
    return em_call_1(em, "lambda_type_value_from_contract", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, selected), true);
}

// D3.3.1: call/rest slots retain nulls and keep a list as one array image.
// The precise candidate survives evaluation of every later argument.
static inline MIR_reg_t em_argument_array(MirEmitter* em, int first, int count,
        MirArgumentItem read, void* owner) {
    MIR_reg_t arguments = em_call_0(em, "array_plain", MIR_T_P, true);
    if (em->root_call_value)
        em->root_call_value(em->call_owner, arguments, JIT_VALUE_RAW_GC_POINTER);
    for (int i = first; i < count; i++) {
        MIR_reg_t item = read(owner, i);
        if (!item) continue;
        if (em->root_call_value)
            em->root_call_value(em->call_owner, item, JIT_VALUE_BOXED_ITEM);
        em_call_void_2(em, "array_push_verbatim", MIR_T_P, MIR_new_reg_op(em->ctx, arguments),
            MIR_T_I64, MIR_new_reg_op(em->ctx, item), true);
    }
    return arguments;
}

struct MirMethodField {
    MIR_reg_t item;
    MIR_reg_t present;
};

static inline MirMethodField em_method_field_load(MirEmitter* em,
        MIR_reg_t self, const ShapeEntry* field) {
    const char* name = mir_em_persist_name(em, *field->name).str;
    MirMethodField value = {};
    value.item = em_call_2(em, "item_attr", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, self),
        MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), true);
    if (em->root_call_value) em->root_call_value(em->call_owner, value.item, JIT_VALUE_BOXED_ITEM);
    if (field->is_optional) {
        MIR_reg_t text = em_call_2(em, "heap_strcpy", MIR_T_P,
            MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name),
            MIR_T_I64, MIR_new_int_op(em->ctx, field->name->length), true);
        MirValue key = em_require_rep(em,
            em_value_for_rep(text, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING), VALUE_REP_ITEM);
        value.present = em_call_2(em, "fn_at", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, key.reg),
            MIR_T_I64, MIR_new_reg_op(em->ctx, self), true);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8,
            MIR_new_reg_op(em->ctx, value.present), MIR_new_reg_op(em->ctx, value.present)));
    }
    return value;
}

// S7.4.6v3/S16.9.5: an untouched absent field stays absent; an assigned null
// is present. Both frontends publish the same final snapshot on early return.
static inline void em_method_field_store(MirEmitter* em, MIR_reg_t self,
        const ShapeEntry* field, MIR_reg_t item, MIR_reg_t present, MirResultCheck check) {
    MIR_label_t done = em_new_label(em);
    if (present) {
        MIR_label_t store = em_new_label(em);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, store),
            MIR_new_reg_op(em->ctx, present)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, done),
            MIR_new_reg_op(em->ctx, item), MIR_new_int_op(em->ctx, ITEM_NULL)));
        em_emit_label(em, store);
    }
    const char* name = mir_em_persist_name(em, *field->name).str;
    MIR_reg_t symbol = em_call_2(em, "heap_create_symbol", MIR_T_P,
        MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name),
        MIR_T_I64, MIR_new_int_op(em->ctx, field->name->length), true);
    MirValue key = em_require_rep(em,
        em_value_for_rep(symbol, LMD_TYPE_SYMBOL, VALUE_REP_RAW_GC_POINTER, &TYPE_SYMBOL), VALUE_REP_ITEM);
    MIR_reg_t result = em_call_3(em, "fn_map_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, self), MIR_T_I64, MIR_new_reg_op(em->ctx, key.reg),
        MIR_T_I64, MIR_new_reg_op(em->ctx, item), true);
    check(em->call_owner, result);
    em_emit_label(em, done);
}

static inline void em_object_method_register(MirEmitter* em, int type_index,
        AstFuncNode* method, MIR_item_t target) {
    const char* name = mir_em_persist_cstr(em, method->name->chars).str;
    MIR_reg_t address = em_new_reg(em, "method_address", MIR_T_P);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, address), MIR_new_ref_op(em->ctx, target)));
    em_call_void_5(em, "object_type_set_method", MIR_T_I64, MIR_new_int_op(em->ctx, type_index),
        MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), MIR_T_P, MIR_new_reg_op(em->ctx, address),
        MIR_T_I64, MIR_new_int_op(em->ctx, ast_linked_node_count(method->param)),
        MIR_T_I64, MIR_new_int_op(em->ctx, lambda_type_func_is_proc(method->type)), true);
}

typedef MIR_reg_t (*MirConstraintPredicate)(void*, TypeConstrained*, MIR_reg_t);
typedef MIR_reg_t (*MirConstraintTruth)(void*, MIR_reg_t);

// S11.2.1/S11.4.11: admit the base, then call each declaring-scope predicate
// innermost first. A predicate's Error answer is false, not a propagated fault.
static inline MIR_reg_t em_constrained_type_test(MirEmitter* em,
        TypeConstrained* constrained, MIR_reg_t value, MIR_reg_t type_value,
        MirConstraintPredicate predicate, MirConstraintTruth truth) {
    MIR_reg_t result = em_new_reg(em, "ct_result", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result),
        MIR_new_int_op(em->ctx, 0)));
    TypeConstrained* layers[LAMBDA_CONSTRAINT_CHAIN_MAX];
    int count = ast_constrained_type_layers(constrained, layers);
    if (count < 0) return result;
    MIR_label_t done = em_new_label(em);
    MIR_reg_t admitted = em_call_2(em, "fn_is", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, value),
        MIR_T_I64, MIR_new_reg_op(em->ctx, type_value), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8,
        MIR_new_reg_op(em->ctx, admitted), MIR_new_reg_op(em->ctx, admitted)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, done),
        MIR_new_reg_op(em->ctx, admitted), MIR_new_int_op(em->ctx, BOOL_TRUE)));
    for (int i = 0; i < count; i++) {
        MIR_reg_t answer = predicate(em->call_owner, layers[i], value);
        MIR_reg_t accepts = truth(em->call_owner, answer);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, done),
            MIR_new_reg_op(em->ctx, accepts)));
    }
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result),
        MIR_new_int_op(em->ctx, 1)));
    em_emit_label(em, done);
    return result;
}

// imported boxed entries need the trampoline to bind their declaring context.
static inline MIR_reg_t em_imported_constraint_call(MirEmitter* em, MIR_item_t target,
        MIR_reg_t value) {
    MIR_reg_t address = em_new_reg(em, "that_fp", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, address), MIR_new_ref_op(em->ctx, target)));
    MIR_var_t vars[3] = {{MIR_T_P, "fp", 0}, {MIR_T_I64, "p", 0}, {MIR_T_P, "home", 0}};
    MirImportEntry* trampoline = em_ensure_import(em, "fn_call_boxed_1_into", MIR_T_I64, 3, vars, 1, true);
    MIR_reg_t answer = em_new_reg(em, "that_answer", MIR_T_I64);
    MIR_op_t ops[6] = {MIR_new_ref_op(em->ctx, trampoline->proto),
        MIR_new_ref_op(em->ctx, trampoline->import), MIR_new_reg_op(em->ctx, answer),
        MIR_new_reg_op(em->ctx, address), MIR_new_reg_op(em->ctx, value), MIR_new_int_op(em->ctx, 0)};
    em_emit_borrowed_call(em, "fn_call_boxed_1_into", MIR_new_insn_arr(em->ctx, MIR_CALL, 6, ops));
    return answer;
}

static inline void em_mark_boxed_function(MirEmitter* em, MIR_reg_t object,
        bool procedure, uint32_t shape) {
    em_call_void_1(em, "lambda_function_mark_mir_context_abi", MIR_T_P, MIR_new_reg_op(em->ctx, object), true);
    em_call_void_1(em, procedure ? "lambda_function_mark_lambda_boxed_procedure" :
        "lambda_function_mark_lambda_boxed_function", MIR_T_P, MIR_new_reg_op(em->ctx, object), true);
    // keep hot function-value creation on the existing inline descriptor store.
    MIR_reg_t flags = em_load_at(em, object, offsetof(Function, flags), MIR_T_U32, "fn_flags");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, flags),
        MIR_new_reg_op(em->ctx, flags), MIR_new_uint_op(em->ctx, shape << LAMBDA_MIR_PUBLIC_RETURN_SHAPE_SHIFT)));
    em_store_at(em, object, offsetof(Function, flags), MIR_T_U32, flags);
}

static inline MIR_reg_t em_function_value(MirEmitter* em, MIR_reg_t address,
        int arity, const char* name, Type* type, bool procedure, bool boxed, uint32_t shape,
        bool system = false, MIR_reg_t environment = 0, int capture_count = 0) {
    MIR_reg_t object;
    if (environment) {
        object = name && *name
            ? em_call_4(em, "to_closure_named", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(em->ctx, address), MIR_T_I64, MIR_new_int_op(em->ctx, arity),
                MIR_T_P, MIR_new_reg_op(em->ctx, environment), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), true)
            : em_call_3(em, "to_closure", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(em->ctx, address), MIR_T_I64, MIR_new_int_op(em->ctx, arity),
                MIR_T_P, MIR_new_reg_op(em->ctx, environment), true);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
            MIR_new_mem_op(em->ctx, MIR_T_U8, offsetof(Function, closure_field_count), object, 0, 1),
            MIR_new_int_op(em->ctx, capture_count)));
    } else {
        object = name && *name
            ? em_call_3(em, system ? "to_sys_fn_named" : "to_fn_named", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, address),
                MIR_T_I64, MIR_new_int_op(em->ctx, arity), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name), true)
            : em_call_2(em, "to_fn_n", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, address),
                MIR_T_I64, MIR_new_int_op(em->ctx, arity), true);
    }
    if (boxed) em_mark_boxed_function(em, object, procedure, shape);
    if (type) em_call_void_2(em, "lambda_function_set_type", MIR_T_P, MIR_new_reg_op(em->ctx, object),
        MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)type), true);
    return object;
}

typedef MIR_reg_t (*MirCaptureReader)(void* owner, FnCapture* capture);

static inline MIR_reg_t em_capture_home_new(MirEmitter* em, MIR_reg_t item) {
    MIR_reg_t home = em_call_1(em, "lambda_capture_home_new", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(em->ctx, item), true);
    if (em->root_call_value) em->root_call_value(em->call_owner, home, JIT_VALUE_RAW_GC_POINTER);
    return home;
}

// reload after a callback without a call that could overwrite its companion.
static inline MIR_reg_t em_capture_home_peek(MirEmitter* em, MIR_reg_t home) {
    MIR_reg_t items = em_load_at(em, home, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_P, "shared_items");
    return em_load_at(em, items, 0, MIR_T_I64, "shared_value");
}

// S16.8.7: both decomposition forms project the same once-evaluated source.
static inline MIR_reg_t em_decompose_item(MirEmitter* em, AstDecomposeNode* node,
        MIR_reg_t source, int index) {
    const char* name = node->is_named
        ? mir_em_persist_cstr(em, node->names[index]->chars).str : NULL;
    return em_call_2(em, node->is_named ? "item_attr" : "item_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, source), node->is_named ? MIR_T_P : MIR_T_I64,
        MIR_new_int_op(em->ctx, node->is_named ? (intptr_t)name : index), true);
}

// each flat environment owns its Item payloads; frontends supply lexical reads
// and capture permissions, while this loop owns layout and precise roots.
static inline MIR_reg_t em_capture_environment(MirEmitter* em, FnCapture* captures,
        MirCaptureReader read_capture) {
    int count = ast_capture_count(captures);
    if (!count) return 0;
    // the collector must see initialized slots before a Function owns the env.
    MIR_reg_t environment = em_call_1(em, "heap_calloc_closure_env", MIR_T_P,
        MIR_T_I64, MIR_new_int_op(em->ctx, count * sizeof(Item)), true);
    if (em->root_call_value) em->root_call_value(em->call_owner, environment, JIT_VALUE_RAW_GC_POINTER);
    int index = 0;
    for (FnCapture* capture = captures; capture; capture = capture->next, index++) {
        MIR_reg_t item = read_capture(em->call_owner, capture);
        if (em->root_call_value) em->root_call_value(em->call_owner, item, JIT_VALUE_BOXED_ITEM);
        em_call_void_4(em, "owned_item_slot_store",
            MIR_T_P, MIR_new_reg_op(em->ctx, environment), MIR_T_I64, MIR_new_int_op(em->ctx, count),
            MIR_T_I64, MIR_new_int_op(em->ctx, index), MIR_T_I64, MIR_new_reg_op(em->ctx, item), true);
    }
    return environment;
}

static inline MIR_reg_t em_capture_slot_read(MirEmitter* em, MIR_reg_t environment,
        int count, int index) {
    return em_call_4(em, "owned_item_slot_read", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, environment), MIR_T_I64, MIR_new_int_op(em->ctx, count),
        MIR_T_I64, MIR_new_int_op(em->ctx, index), MIR_T_I64, MIR_new_int_op(em->ctx, 0), true);
}

// task entries use boxed functions and a boxed-value argument array (S13.1.1v3).
// S13.1.3v2: select stages all handles and its named timeout in source order;
// both MIR frontends use the same rooted variadic-to-array bridge.
static inline MIR_reg_t em_select_tasks(MirEmitter* em, AstCallNode* call) {
    MIR_reg_t handles = em_call_0(em, "array", MIR_T_P, true);
    if (em->root_call_value) em->root_call_value(em->call_owner, handles, JIT_VALUE_RAW_GC_POINTER);
    MIR_reg_t timeout = em_new_reg(em, "select_timeout", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, timeout),
        MIR_new_int_op(em->ctx, (uint64_t)LMD_TYPE_INT << 56)));
    if (em->root_call_value) em->root_call_value(em->call_owner, timeout, JIT_VALUE_BOXED_ITEM);
    for (AstNode* argument = call->argument; argument; argument = argument->next) {
        AstNamedNode* named = argument->node_type == AST_NODE_NAMED_ARG ? (AstNamedNode*)argument : NULL;
        bool is_timeout = named && named->name && named->name->len == 7 &&
            memcmp(named->name->chars, "timeout", 7) == 0;
        MirValue item = em_require_rep(em,
            em_lower_value(em, is_timeout ? named->as : argument, MIR_VALUE_ANY), VALUE_REP_ITEM);
        if (is_timeout) {
            em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, timeout),
                MIR_new_reg_op(em->ctx, item.reg)));
        } else em_call_void_2(em, "array_push_verbatim",
            MIR_T_P, MIR_new_reg_op(em->ctx, handles), MIR_T_I64, MIR_new_reg_op(em->ctx, item.reg), true);
    }
    MIR_reg_t items = em_call_1(em, "array_end", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, handles), true);
    return em_call_2(em, "pn_select", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, items), MIR_T_I64, MIR_new_reg_op(em->ctx, timeout), true);
}

static inline MIR_reg_t em_start_task(MirEmitter* em, AstStartNode* start,
        MirResultCheck check_arguments) {
    AstCallNode* call = start->call;
    MirValue function = em_require_rep(em,
        em_lower_value(em, call->function, MIR_VALUE_ANY), VALUE_REP_ITEM);
    if (em->root_call_value) em->root_call_value(em->call_owner, function.reg, JIT_VALUE_BOXED_ITEM);
    MIR_reg_t arguments;
    if (call->argument) {
        MirValue source = em_require_rep(em,
            em_lower_value(em, call->argument, MIR_VALUE_ANY), VALUE_REP_ITEM);
        arguments = em_call_2(em, "ensure_typed_array", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, source.reg),
            MIR_T_I64, MIR_new_int_op(em->ctx, LMD_TYPE_ANY), true);
        check_arguments(em->call_owner, arguments);
    } else arguments = em_call_0(em, "list", MIR_T_P, true);
    if (em->root_call_value) em->root_call_value(em->call_owner, arguments, JIT_VALUE_RAW_GC_POINTER);
    if (start->document_owner)
        return em_call_2(em, "lambda_task_start_document", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, function.reg),
            MIR_T_P, MIR_new_reg_op(em->ctx, arguments), true);
    return em_call_3(em, "lambda_task_start_function_scoped", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, function.reg),
        MIR_T_P, MIR_new_reg_op(em->ctx, arguments),
        MIR_T_I64, MIR_new_int_op(em->ctx, start->escapes ? 1 : 0), true);
}

// both frontends retain their lexical exit destinations; the runtime joins or
// cancels the crossed task scopes while the activation's roots remain live.
static inline void em_task_scope_finish(MirEmitter* em, MIR_reg_t scope,
        MIR_op_t error_exit, bool unwind = false) {
    (void)em_call_2(em, unwind ? "lambda_task_scope_unwind" : "lambda_task_scope_leave", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, scope), MIR_T_I64, error_exit, true);
}

typedef MIR_reg_t (*MirFaultOperandReader)(void* owner, AstNode* node, MIR_reg_t frame);

// S7.11.1v2/S7.11.4: the generated activation owns the platform checkpoint;
// a returned C wrapper cannot be a valid non-local recovery target.
static inline MIR_reg_t em_local_fault_expression(MirEmitter* em, AstNode* node,
        MirFaultOperandReader read_operand) {
    MIR_reg_t result = em_new_reg(em, "local_fault_result", MIR_T_I64);
    MIR_reg_t scope_mark = em_call_0(em, "lambda_task_scope_current", MIR_T_P, true);
    MIR_reg_t frame = em_call_2(em, "lambda_recovery_frame_begin_for", MIR_T_P,
        MIR_T_P, MIR_new_reg_op(em->ctx, em->frame.runtime),
        MIR_T_I64, MIR_new_int_op(em->ctx, LAMBDA_RECOVERY_CAP_LOCAL_FAULT), true);
    MIR_label_t have_frame = em_new_label(em), landing = em_new_label(em);
    MIR_label_t setup_failed = em_new_label(em), restored_landing = em_new_label(em), done = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, have_frame),
        MIR_new_reg_op(em->ctx, frame), MIR_new_int_op(em->ctx, 0)));
    // setup failure must raise to the enclosing frame rather than evaluate
    // the protected operand without a live target.
    (void)em_call_2(em, "lambda_recovery_frame_raise_fault", MIR_T_I64,
        MIR_T_I64, MIR_new_int_op(em->ctx, LAMBDA_FAULT_OUT_OF_MEMORY),
        MIR_T_I64, MIR_new_int_op(em->ctx, ERR_OK), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result),
        MIR_new_int_op(em->ctx, ITEM_ERROR)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, have_frame);
    MIR_reg_t jump_buffer = em_new_reg(em, "local_fault_jmp", MIR_T_P);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, jump_buffer),
        MIR_new_reg_op(em->ctx, frame), MIR_new_int_op(em->ctx, LAMBDA_RECOVERY_FRAME_JUMP_BUFFER_OFFSET)));
    MIR_var_t checkpoint_args[2] = {
        {MIR_T_P, "jump_buffer", 0},
#if defined(_WIN32)
        {MIR_T_P, "frame", 0},
#else
        {MIR_T_I64, "save_mask", 0},
#endif
    };
    MirImportEntry* checkpoint_import = em_ensure_import(em,
        LAMBDA_RECOVERY_FRAME_MIR_CHECKPOINT_NAME, MIR_T_I64, 2, checkpoint_args, 1, true);
    MIR_reg_t checkpoint = em_new_reg(em, "local_fault_checkpoint", MIR_T_I64);
    em_emit_insn(em, MIR_new_call_insn(em->ctx, 5,
        MIR_new_ref_op(em->ctx, checkpoint_import->proto), MIR_new_ref_op(em->ctx, checkpoint_import->import),
        MIR_new_reg_op(em->ctx, checkpoint), MIR_new_reg_op(em->ctx, jump_buffer),
#if defined(_WIN32)
        // mingw requires a null frame pointer, not the POSIX save-mask flag.
        MIR_new_int_op(em->ctx, 0)
#else
        MIR_new_int_op(em->ctx, 1)
#endif
        ));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, landing),
        MIR_new_reg_op(em->ctx, checkpoint), MIR_new_int_op(em->ctx, 0)));
    MIR_reg_t armed = em_call_1(em, "lambda_recovery_frame_arm", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8, MIR_new_reg_op(em->ctx, armed), MIR_new_reg_op(em->ctx, armed)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, setup_failed), MIR_new_reg_op(em->ctx, armed)));
    MIR_reg_t produced = read_operand(em->call_owner, node, frame);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, produced)));
    (void)em_call_1(em, "lambda_recovery_frame_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, landing);
    MIR_reg_t restored = em_call_1(em, "lambda_recovery_frame_restore_landing", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UEXT8, MIR_new_reg_op(em->ctx, restored), MIR_new_reg_op(em->ctx, restored)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, restored_landing), MIR_new_reg_op(em->ctx, restored)));
    em_emit_label(em, setup_failed);
    (void)em_call_1(em, "lambda_recovery_frame_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, ITEM_ERROR)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, restored_landing);
    // restore the precise side-stack checkpoint before publishing the durable
    // fault; callees abandoned by the jump cannot run their own epilogues.
    MIR_reg_t fault = em_call_2(em, "lambda_recovery_frame_fault_item", MIR_T_I64,
        MIR_T_P, MIR_new_reg_op(em->ctx, em->frame.runtime), MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, fault)));
    (void)em_call_1(em, "lambda_recovery_frame_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, frame), true);
    em_task_scope_finish(em, scope_mark, MIR_new_int_op(em->ctx, 1), true);
    em_emit_label(em, done);
    return result;
}
