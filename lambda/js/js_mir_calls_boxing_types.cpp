#include "js_mir_internal.hpp"

// ============================================================================
// Import management
// ============================================================================

JsMirImportEntry* jm_ensure_import(JsMirTranspiler* mt, const char* name,
    MIR_type_t ret_type, int nargs, MIR_var_t* args, int nres) {
    JsImportCacheEntry key;
    memset(&key, 0, sizeof(key));
    snprintf(key.name, sizeof(key.name), "%s", name);

    JsImportCacheEntry* found = (JsImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    if (found) return &found->entry;

    char proto_name[140];
    snprintf(proto_name, sizeof(proto_name), "%s_p", name);

    MIR_type_t res_types[1] = { ret_type };
    MIR_item_t proto = MIR_new_proto_arr(mt->ctx, proto_name, nres, res_types, nargs, args);
    MIR_item_t imp = MIR_new_import(mt->ctx, name);

    JsImportCacheEntry new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    snprintf(new_entry.name, sizeof(new_entry.name), "%s", name);
    new_entry.entry.proto = proto;
    new_entry.entry.import = imp;
    hashmap_set(mt->import_cache, &new_entry);

    found = (JsImportCacheEntry*)hashmap_get(mt->import_cache, &key);
    return &found->entry;
}

// Item(Item, Item)
JsMirImportEntry* jm_ensure_import_ii_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[2] = {{MIR_T_I64, "a", 0}, {MIR_T_I64, "b", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 2, args, 1);
}

// Item(Item)
JsMirImportEntry* jm_ensure_import_i_i(JsMirTranspiler* mt, const char* name) {
    MIR_var_t args[1] = {{MIR_T_I64, "a", 0}};
    return jm_ensure_import(mt, name, MIR_T_I64, 1, args, 1);
}

// Item(void)
JsMirImportEntry* jm_ensure_import_v_i(JsMirTranspiler* mt, const char* name) {
    return jm_ensure_import(mt, name, MIR_T_I64, 0, NULL, 1);
}

// ============================================================================
// Emit call helpers
// ============================================================================

MIR_reg_t jm_call_0(JsMirTranspiler* mt, const char* fn_name, MIR_type_t ret_type) {
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 0, NULL, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res)));
    return res;
}

MIR_reg_t jm_call_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 1, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1));
    return res;
}

MIR_reg_t jm_call_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 2, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2));
    return res;
}

MIR_reg_t jm_call_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 3, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3));
    return res;
}

MIR_reg_t jm_call_4(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 4, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4));
    return res;
}

MIR_reg_t jm_call_5(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5) {
    MIR_var_t args[5] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}, {a5t, "e", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 5, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 8,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4, a5));
    return res;
}

// v20: 6-argument call helper (used for Date setter dispatch with up to 4 args + obj + method_id)
MIR_reg_t jm_call_6(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t ret_type, MIR_type_t a1t, MIR_op_t a1,
    MIR_type_t a2t, MIR_op_t a2, MIR_type_t a3t, MIR_op_t a3,
    MIR_type_t a4t, MIR_op_t a4, MIR_type_t a5t, MIR_op_t a5,
    MIR_type_t a6t, MIR_op_t a6) {
    MIR_var_t args[6] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}, {a5t, "e", 0}, {a6t, "f", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, ret_type, 6, args, 1);
    MIR_reg_t res = jm_new_reg(mt, fn_name, ret_type);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 9,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import),
        MIR_new_reg_op(mt->ctx, res), a1, a2, a3, a4, a5, a6));
    return res;
}

void jm_call_void_0(JsMirTranspiler* mt, const char* fn_name) {
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 0, NULL, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 2,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import)));
}

void jm_call_void_1(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1) {
    MIR_var_t args[1] = {{a1t, "a", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 1, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 3,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1));
}

void jm_call_void_2(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2) {
    MIR_var_t args[2] = {{a1t, "a", 0}, {a2t, "b", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 2, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 4,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2));
}

void jm_call_void_3(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3) {
    MIR_var_t args[3] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 3, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 5,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2, a3));
}

void jm_call_void_4(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3, MIR_type_t a4t, MIR_op_t a4) {
    MIR_var_t args[4] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 4, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 6,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2, a3, a4));
}

void jm_call_void_5(JsMirTranspiler* mt, const char* fn_name,
    MIR_type_t a1t, MIR_op_t a1, MIR_type_t a2t, MIR_op_t a2,
    MIR_type_t a3t, MIR_op_t a3, MIR_type_t a4t, MIR_op_t a4,
    MIR_type_t a5t, MIR_op_t a5) {
    MIR_var_t args[5] = {{a1t, "a", 0}, {a2t, "b", 0}, {a3t, "c", 0}, {a4t, "d", 0}, {a5t, "e", 0}};
    JsMirImportEntry* ie = jm_ensure_import(mt, fn_name, MIR_T_I64, 5, args, 0);
    jm_emit(mt, MIR_new_call_insn(mt->ctx, 7,
        MIR_new_ref_op(mt->ctx, ie->proto),
        MIR_new_ref_op(mt->ctx, ie->import), a1, a2, a3, a4, a5));
}

MIR_reg_t jm_emit_null(JsMirTranspiler* mt) {
    MIR_reg_t r = jm_new_reg(mt, "null", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    return r;
}

// v17: emit JS undefined value (for strict mode this coercion)
MIR_reg_t jm_emit_undefined(JsMirTranspiler* mt) {
    MIR_reg_t r = jm_new_reg(mt, "undef", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_JS_UNDEFINED)));
    return r;
}

// ============================================================================
// Boxing helpers
// ============================================================================

// Box int64 constant -> Item
MIR_reg_t jm_box_int_const(JsMirTranspiler* mt, int64_t value) {
    // If value is in the symbol collision range, promote to float
    if (value <= -(int64_t)JS_SYMBOL_BASE) {
        MIR_reg_t d = jm_new_reg(mt, "boxid", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
            MIR_new_reg_op(mt->ctx, d), MIR_new_double_op(mt->ctx, (double)value)));
        return jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, d));
    }
    // Inline i2it: result = ITEM_INT_TAG | (value & MASK56)
    MIR_reg_t r = jm_new_reg(mt, "boxi", MIR_T_I64);
    uint64_t tagged = ITEM_INT_TAG | ((uint64_t)value & MASK56);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, r),
        MIR_new_int_op(mt->ctx, (int64_t)tagged)));
    return r;
}

// v20: Emit writeback from param register to arguments[param_index]
void jm_arguments_writeback_param(JsMirTranspiler* mt, int param_index, MIR_reg_t val_reg) {
    if (mt->arguments_reg == 0) return;
    MIR_reg_t idx_reg = jm_box_int_const(mt, param_index);
    jm_call_3(mt, "js_property_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, mt->arguments_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, val_reg));
}

// Box int64 register -> Item (runtime range check)
// Avoids creating ints in the symbol collision range (< -JS_SYMBOL_BASE)
MIR_reg_t jm_box_int_reg(JsMirTranspiler* mt, MIR_reg_t val) {
    int64_t INT56_MAX_VAL = 0x007FFFFFFFFFFFFFLL;
    int64_t INT56_MIN_VAL = (int64_t)0xFF80000000000000LL;
    int64_t SYMBOL_LIMIT  = -(int64_t)JS_SYMBOL_BASE;  // values <= this are symbols

    MIR_reg_t result = jm_new_reg(mt, "boxi", MIR_T_I64);
    MIR_reg_t masked = jm_new_reg(mt, "mask", MIR_T_I64);
    MIR_reg_t tagged = jm_new_reg(mt, "tag", MIR_T_I64);
    MIR_reg_t le_max = jm_new_reg(mt, "le", MIR_T_I64);
    MIR_reg_t ge_min = jm_new_reg(mt, "ge", MIR_T_I64);
    MIR_reg_t gt_sym = jm_new_reg(mt, "gs", MIR_T_I64);  // > symbol limit
    MIR_reg_t in_range = jm_new_reg(mt, "rng", MIR_T_I64);
    MIR_reg_t in_range2 = jm_new_reg(mt, "rn2", MIR_T_I64);

    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LE, MIR_new_reg_op(mt->ctx, le_max),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, INT56_MAX_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_GE, MIR_new_reg_op(mt->ctx, ge_min),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, INT56_MIN_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_GT, MIR_new_reg_op(mt->ctx, gt_sym),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, SYMBOL_LIMIT)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, in_range),
        MIR_new_reg_op(mt->ctx, le_max), MIR_new_reg_op(mt->ctx, ge_min)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, in_range2),
        MIR_new_reg_op(mt->ctx, in_range), MIR_new_reg_op(mt->ctx, gt_sym)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND, MIR_new_reg_op(mt->ctx, masked),
        MIR_new_reg_op(mt->ctx, val), MIR_new_int_op(mt->ctx, (int64_t)MASK56)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, tagged),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_INT_TAG), MIR_new_reg_op(mt->ctx, masked)));

    MIR_label_t l_ok = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_ok),
        MIR_new_reg_op(mt->ctx, in_range2)));
    // out of int56 range or in symbol range: promote to float instead of returning error
    MIR_reg_t d_ovf = jm_new_reg(mt, "i2d_ovf", MIR_T_D);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_I2D,
        MIR_new_reg_op(mt->ctx, d_ovf), MIR_new_reg_op(mt->ctx, val)));
    MIR_reg_t float_boxed = jm_call_1(mt, "push_d", MIR_T_I64,
        MIR_T_D, MIR_new_reg_op(mt->ctx, d_ovf));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, float_boxed)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    jm_emit_label(mt, l_ok);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, tagged)));
    jm_emit_label(mt, l_end);
    return result;
}

// Box double -> Item via push_d
MIR_reg_t jm_box_float(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    return jm_call_1(mt, "push_d", MIR_T_I64, MIR_T_D, MIR_new_reg_op(mt->ctx, d_reg));
}

// Box string via s2it tagging: result = ptr ? (STR_TAG | ptr) : ITEM_NULL
MIR_reg_t jm_box_string(JsMirTranspiler* mt, MIR_reg_t ptr_reg) {
    MIR_reg_t result = jm_new_reg(mt, "boxs", MIR_T_I64);
    MIR_label_t l_nn = jm_new_label(mt);
    MIR_label_t l_end = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, MIR_new_label_op(mt->ctx, l_nn),
        MIR_new_reg_op(mt->ctx, ptr_reg)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)ITEM_NULL_VAL)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, l_end)));
    jm_emit_label(mt, l_nn);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)STR_TAG), MIR_new_reg_op(mt->ctx, ptr_reg)));
    jm_emit_label(mt, l_end);
    return result;
}

// Create a boxed string Item from a C string literal
// Calls heap_create_name(chars) -> String*, then boxes with s2it
MIR_reg_t jm_box_string_literal(JsMirTranspiler* mt, const char* str, int len) {
    // Intern the string at transpile time (handles embedded NUL bytes correctly).
    // For module compilation the string constant is baked into JIT code and
    // accessed at RUNTIME (after js_transpiler_destroy has freed mt->tp->name_pool).
    // Use context->name_pool (the persistent JS eval-context pool, created in
    // load_js_module and never freed) so the String* pointer remains valid.
    // Same applies to with-preamble mode (event handler compilation): the handler
    // functions are invoked long after the transpiler is destroyed.
    bool use_persistent_pool = (mt->is_module || mt->preamble_entries != nullptr)
                               && context && context->name_pool;
    NamePool* np = use_persistent_pool ? context->name_pool : mt->tp->name_pool;
    String* interned = name_pool_create_len(np, str, len);
    // Box directly: the interned String* is already a valid heap pointer
    // Use s2it(interned) as a compile-time constant Item value
    uint64_t item_val = s2it(interned);
    MIR_reg_t result = jm_new_reg(mt, "strlit", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV, MIR_new_reg_op(mt->ctx, result),
        MIR_new_int_op(mt->ctx, (int64_t)item_val)));
    return result;
}

// Phase-5C: emit either `js_property_set(obj, key, fn)` for regular methods or
// `js_install_user_accessor(obj, key, fn, is_setter)` for getter/setter
// accessors. Replaces the legacy pattern of writing to a `__get_X`/`__set_X`
// magic-key marker that was caught by the property-set intercept.
void jm_emit_install_method_or_accessor(JsMirTranspiler* mt,
    MIR_reg_t obj, MIR_reg_t key, MIR_reg_t fn_item,
    bool is_getter, bool is_setter) {
    if (is_getter || is_setter) {
        MIR_reg_t is_set = jm_new_reg(mt, "is_set", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
            MIR_new_reg_op(mt->ctx, is_set),
            MIR_new_int_op(mt->ctx, is_setter ? 1 : 0)));
        jm_call_4(mt, "js_install_user_accessor", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, is_set));
    } else {
        jm_call_3(mt, "js_property_set", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_item));
    }
}

// Helper: emit js_set_function_name call if name is non-empty, and formal_length if needed
void jm_emit_set_function_name(JsMirTranspiler* mt, MIR_reg_t fn_reg, const char* name, int formal_length ) {
    if (name && name[0]) {
        // Convert __private_X back to #X for ES spec compliance
        char priv_buf[256];
        const char* display_name = name;
        if (strncmp(name, "__private_", 10) == 0) {
            int len = snprintf(priv_buf, sizeof(priv_buf), "#%s", name + 10);
            display_name = priv_buf;
            (void)len;
        } else if (strncmp(name, "get __private_", 14) == 0) {
            int len = snprintf(priv_buf, sizeof(priv_buf), "get #%s", name + 14);
            display_name = priv_buf;
            (void)len;
        } else if (strncmp(name, "set __private_", 14) == 0) {
            int len = snprintf(priv_buf, sizeof(priv_buf), "set #%s", name + 14);
            display_name = priv_buf;
            (void)len;
        }
        MIR_reg_t name_reg = jm_box_string_literal(mt, display_name, strlen(display_name));
        jm_call_void_2(mt, "js_set_function_name",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_reg_op(mt->ctx, name_reg));
    }
    if (formal_length >= 0) {
        jm_call_void_2(mt, "js_set_formal_length",
            MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
            MIR_T_I64, MIR_new_int_op(mt->ctx, formal_length));
    }
}

// Helper: emit js_set_function_source call to store original source text for toString
void jm_emit_set_function_source(JsMirTranspiler* mt, MIR_reg_t fn_reg, JsFunctionNode* fn_node) {
    if (!fn_node || !mt->tp || !mt->tp->source) return;
    TSNode node = fn_node->base.node;
    if (ts_node_is_null(node)) return;
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end <= start || end > mt->tp->source_length) return;
    const char* text = mt->tp->source + start;
    uint32_t len = end - start;
    // Cap source text to avoid overly large string literals in MIR
    if (len > 65536) return;
    // Skip leading whitespace and comments, then strip "static" keyword if present.
    // Class static methods span from (optional comment + "static" keyword) through body.
    // We want source text to start at "get"/"set"/function-keyword, not "static".
    // Helper lambda: skip whitespace in-place
    auto skip_ws = [](const char* p, uint32_t& rem) {
        while (rem > 0 && (p[0] == ' ' || p[0] == '\t' || p[0] == '\n' || p[0] == '\r')) { p++; rem--; }
        return p;
    };
    (void)skip_ws; // suppress unused warning
    // Step 1: trim leading whitespace
    while (len > 0 && (text[0] == ' ' || text[0] == '\t' || text[0] == '\n' || text[0] == '\r')) { text++; len--; }
    // Step 2: skip leading block comments (/* ... */) and then check for "static"
    {
        const char* scan = text;
        uint32_t slen = len;
        // skip any leading block/line comments
        bool advanced = true;
        while (advanced && slen > 0) {
            advanced = false;
            while (slen > 0 && (scan[0] == ' ' || scan[0] == '\t' || scan[0] == '\n' || scan[0] == '\r')) { scan++; slen--; advanced = true; }
            if (slen >= 2 && scan[0] == '/' && scan[1] == '*') {
                // skip block comment
                scan += 2; slen -= 2;
                while (slen >= 2 && !(scan[0] == '*' && scan[1] == '/')) { scan++; slen--; }
                if (slen >= 2) { scan += 2; slen -= 2; }
                advanced = true;
            } else if (slen >= 2 && scan[0] == '/' && scan[1] == '/') {
                // skip line comment
                scan += 2; slen -= 2;
                while (slen > 0 && scan[0] != '\n') { scan++; slen--; }
                advanced = true;
            }
        }
        // Now scan points past any leading comments. Check if next token is "static".
        if (slen >= 7 && strncmp(scan, "static", 6) == 0 &&
            (scan[6] == ' ' || scan[6] == '\t' || scan[6] == '\n' || scan[6] == '/' )) {
            // Static method: advance scan past "static" and then skip whitespace/comments again
            scan += 6; slen -= 6;
            // skip whitespace/comments after "static"
            bool adv2 = true;
            while (adv2 && slen > 0) {
                adv2 = false;
                while (slen > 0 && (scan[0] == ' ' || scan[0] == '\t' || scan[0] == '\n' || scan[0] == '\r')) { scan++; slen--; adv2 = true; }
                if (slen >= 2 && scan[0] == '/' && scan[1] == '*') {
                    scan += 2; slen -= 2;
                    while (slen >= 2 && !(scan[0] == '*' && scan[1] == '/')) { scan++; slen--; }
                    if (slen >= 2) { scan += 2; slen -= 2; }
                    adv2 = true;
                }
            }
            text = scan;
            len = slen;
        }
    }
    // Tree-sitter may extend node ranges to include trailing comments.
    // Trim back to the actual closing '}' for block-bodied functions.
    if (len > 0 && fn_node->body && fn_node->body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
        while (len > 1 && text[len - 1] != '}') len--;
    }
    MIR_reg_t src_reg = jm_box_string_literal(mt, text, len);
    jm_call_void_2(mt, "js_set_function_source",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, src_reg));
}

// Helper: emit js_property_set(cls_obj, "__source_text__", source) so that
// Function.prototype.toString on the class returns the original source text
// (per ES spec: Function.prototype.toString on a class returns its source).
// Avoids the slow validateNativeFunctionSource fallback in test262 harness.
void jm_emit_set_class_source(JsMirTranspiler* mt, MIR_reg_t cls_obj, JsClassNode* cls_node) {
    if (!cls_node || !mt->tp || !mt->tp->source) return;
    TSNode node = cls_node->base.node;
    if (ts_node_is_null(node)) return;
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end <= start || end > mt->tp->source_length) return;
    const char* text = mt->tp->source + start;
    uint32_t len = end - start;
    if (len > 65536) return;
    // Trim leading whitespace
    while (len > 0 && (text[0] == ' ' || text[0] == '\t' || text[0] == '\n' || text[0] == '\r')) { text++; len--; }
    // Tree-sitter may extend the node end past trailing comments; trim to closing '}'
    while (len > 1 && text[len - 1] != '}') len--;
    MIR_reg_t key = jm_box_string_literal(mt, "__source_text__", 15);
    MIR_reg_t src_reg = jm_box_string_literal(mt, text, len);
    jm_call_3(mt, "js_property_set", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, cls_obj),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, key),
        MIR_T_I64, MIR_new_reg_op(mt->ctx, src_reg));
}

// Helper: emit js_set_formal_length if formal_length differs from param_count
void jm_emit_formal_length(JsMirTranspiler* mt, MIR_reg_t fn_reg, int formal_length) {
    if (formal_length < 0) return; // -1 = same as param_count, no correction needed
    jm_call_void_2(mt, "js_set_formal_length",
        MIR_T_I64, MIR_new_reg_op(mt->ctx, fn_reg),
        MIR_T_I64, MIR_new_int_op(mt->ctx, formal_length));
}

// v12: Build a compile-time stack trace string from the lexical function chain.
// Walks from current function up through parent_index to build:
//   "Error\n    at FuncName1\n    at FuncName2\n..."
MIR_reg_t jm_build_error_stack_string(JsMirTranspiler* mt, const char* error_type) {
    char buf[1024];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", error_type);

    // Walk the lexical function chain from current to top-level
    int fi = mt->current_func_index;
    while (fi >= 0 && fi < mt->func_count && pos < (int)sizeof(buf) - 32) {
        JsFuncCollected* fc = &mt->func_entries[fi];
        const char* js_name = NULL;
        if (fc->node && fc->node->name) {
            js_name = fc->node->name->chars;
        }
        if (js_name && js_name[0]) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\n    at %s", js_name);
        }
        fi = fc->parent_index;
    }

    return jm_box_string_literal(mt, buf, pos);
}

// ============================================================================
// Inline unboxing helpers (MIR instructions, no function calls)
// ============================================================================

// Unbox Item → native int64_t: sign-extend lower 56 bits
MIR_reg_t jm_emit_unbox_int(JsMirTranspiler* mt, MIR_reg_t item) {
    MIR_reg_t result = jm_new_reg(mt, "ubi", MIR_T_I64);
    // shift left 8, arithmetic shift right 8 for sign extension
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_LSH, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, item), MIR_new_int_op(mt->ctx, 8)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_RSH, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, result), MIR_new_int_op(mt->ctx, 8)));
    return result;
}

// Unbox Item → native double via it2d runtime function
MIR_reg_t jm_emit_unbox_float(JsMirTranspiler* mt, MIR_reg_t item) {
    // Safety: if item is already a native double, return it directly
    MIR_type_t rt = MIR_reg_type(mt->ctx, item, mt->current_func);
    if (rt == MIR_T_D) return item;
    if (rt == MIR_T_F) {
        MIR_reg_t d = jm_new_reg(mt, "f2d_ub", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_F2D,
            MIR_new_reg_op(mt->ctx, d), MIR_new_reg_op(mt->ctx, item)));
        return d;
    }
    return jm_call_1(mt, "it2d", MIR_T_D, MIR_T_I64, MIR_new_reg_op(mt->ctx, item));
}

// Convert native int64_t → native double
MIR_reg_t jm_emit_int_to_double(JsMirTranspiler* mt, MIR_reg_t int_reg) {
    MIR_reg_t result = jm_new_reg(mt, "i2d", MIR_T_D);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_I2D, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, int_reg)));
    return result;
}

// Convert native double → native int64_t (truncate)
MIR_reg_t jm_emit_double_to_int(JsMirTranspiler* mt, MIR_reg_t d_reg) {
    MIR_reg_t result = jm_new_reg(mt, "d2i", MIR_T_I64);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_D2I, MIR_new_reg_op(mt->ctx, result),
        MIR_new_reg_op(mt->ctx, d_reg)));
    return result;
}

// Ensure a register is native int64_t, converting from boxed if needed
MIR_reg_t jm_ensure_native_int(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type) {
    if (src_type == LMD_TYPE_INT) return reg;  // already native int
    if (src_type == LMD_TYPE_FLOAT) return jm_emit_double_to_int(mt, reg);
    // boxed Item of unknown type → call it2i for safe conversion
    // (handles INT, FLOAT, INT64, BOOL items correctly)
    return jm_call_1(mt, "it2i", MIR_T_I64, MIR_T_I64, MIR_new_reg_op(mt->ctx, reg));
}

// Ensure a register is native double, converting from int or boxed if needed
MIR_reg_t jm_ensure_native_float(JsMirTranspiler* mt, MIR_reg_t reg, TypeId src_type) {
    if (src_type == LMD_TYPE_FLOAT) return reg;  // already native double
    if (src_type == LMD_TYPE_INT) return jm_emit_int_to_double(mt, reg);
    // boxed Item → unbox
    return jm_emit_unbox_float(mt, reg);
}

// Box a native value into an Item based on its type
MIR_reg_t jm_box_native(JsMirTranspiler* mt, MIR_reg_t reg, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_INT:   return jm_box_int_reg(mt, reg);
    case LMD_TYPE_FLOAT: return jm_box_float(mt, reg);
    case LMD_TYPE_BOOL: {
        MIR_reg_t result = jm_new_reg(mt, "boxb", MIR_T_I64);
        uint64_t BOOL_TAG = (uint64_t)LMD_TYPE_BOOL << 56;
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_OR, MIR_new_reg_op(mt->ctx, result),
            MIR_new_int_op(mt->ctx, (int64_t)BOOL_TAG), MIR_new_reg_op(mt->ctx, reg)));
        return result;
    }
    default: return reg;  // already boxed
    }
}

// Safety net: ensure a register holds a boxed Item (I64).
// If it's a native double/float, box it; otherwise return as-is.
MIR_reg_t jm_ensure_boxed(JsMirTranspiler* mt, MIR_reg_t reg) {
    MIR_type_t rtype = MIR_reg_type(mt->ctx, reg, mt->current_func);
    if (rtype == MIR_T_D) return jm_box_float(mt, reg);
    if (rtype == MIR_T_F) {
        MIR_reg_t d = jm_new_reg(mt, "f2d_box", MIR_T_D);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_F2D,
            MIR_new_reg_op(mt->ctx, d), MIR_new_reg_op(mt->ctx, reg)));
        return jm_box_float(mt, d);
    }
    return reg;
}

// ============================================================================
// Type inference for expressions (jm_get_effective_type)
// ============================================================================

// Forward declarations
JsFuncCollected* jm_resolve_native_call(JsMirTranspiler* mt, JsCallNode* call);
JsFuncCollected* jm_find_collected_func(JsMirTranspiler* mt, JsFunctionNode* fn);
// Phase 5 forward declarations
String* jm_get_math_method(JsCallNode* call);
TypeId jm_math_return_type(String* method, JsMirTranspiler* mt, JsAstNode* arg0);
MIR_reg_t jm_transpile_math_native(JsMirTranspiler* mt, JsCallNode* call,
                                            String* method, TypeId target_type);
MIR_reg_t jm_transpile_math_call(JsMirTranspiler* mt, JsCallNode* call, String* method);
// P9 forward declarations
JsMirVarEntry* jm_get_typed_array_var(JsMirTranspiler* mt, JsAstNode* obj_node);
bool jm_typed_array_is_int(int ta_type);
MIR_reg_t jm_transpile_typed_array_get(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                               MIR_reg_t idx_native, int ta_type,
                                               MIR_reg_t h_data , MIR_reg_t h_len );
MIR_reg_t jm_transpile_typed_array_get_native(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                                      MIR_reg_t idx_native, int ta_type,
                                                      TypeId target_type,
                                                      MIR_reg_t h_data );
MIR_reg_t jm_transpile_typed_array_set(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                               MIR_reg_t idx_native, MIR_reg_t val_boxed,
                                               int ta_type,
                                               MIR_reg_t h_data , MIR_reg_t h_len );
MIR_reg_t jm_transpile_typed_array_length(JsMirTranspiler* mt, MIR_reg_t arr_reg);
// A2 forward declarations
JsMirVarEntry* jm_get_js_array_var(JsMirTranspiler* mt, JsAstNode* obj_node);
MIR_reg_t jm_transpile_array_get_inline(JsMirTranspiler* mt, MIR_reg_t arr_reg,
                                                MIR_reg_t idx_native,
                                                MIR_reg_t h_items , MIR_reg_t h_len );
// A5 forward declaration
void jm_scan_ctor_props(JsFuncCollected* fc, JsAstNode* body);

// Forward declaration — defined at line ~3786, needed by jm_get_effective_type and jm_transpile_as_native
int jm_ctor_prop_slot(JsFuncCollected* fc, const char* prop_name, int prop_len);

// Returns the inferred TypeId for a JS AST expression node.
// LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_STRING → known type
// LMD_TYPE_ANY → unknown (must use boxed path)
TypeId jm_get_effective_type(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return LMD_TYPE_ANY;

    switch (node->node_type) {
    case JS_AST_NODE_LITERAL: {
        JsLiteralNode* lit = (JsLiteralNode*)node;
        switch (lit->literal_type) {
        case JS_LITERAL_NUMBER: {
            double val = lit->value.number_value;
            if (lit->is_bigint) return LMD_TYPE_DECIMAL;
            // If source text has '.' or 'e'/'E', treat as FLOAT even if integral-valued
            // (e.g., 999999.0, 1e5 → FLOAT; 999999 → INT)
            if (lit->has_decimal) return LMD_TYPE_FLOAT;
            if (val == (double)(int64_t)val && val >= -36028797018963968.0 && val <= 36028797018963967.0)
                return LMD_TYPE_INT;
            return LMD_TYPE_FLOAT;
        }
        case JS_LITERAL_BOOLEAN:  return LMD_TYPE_BOOL;
        case JS_LITERAL_STRING:   return LMD_TYPE_STRING;
        case JS_LITERAL_NULL:     return LMD_TYPE_NULL;
        case JS_LITERAL_UNDEFINED: return LMD_TYPE_UNDEFINED;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_IDENTIFIER: {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var) return var->type_id;
        // P5: Check module-level variable type for arithmetic type inference.
        // When a MODVAR was initialized with a numeric literal, modvar_type is set
        // to LMD_TYPE_INT or LMD_TYPE_FLOAT; this enables native arithmetic paths.
        if (mt->module_consts) {
            JsModuleConstEntry mv_lookup;
            snprintf(mv_lookup.name, sizeof(mv_lookup.name), "%s", vname);
            JsModuleConstEntry* mv_mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &mv_lookup);
            if (mv_mc && mv_mc->const_type == MCONST_MODVAR &&
                (mv_mc->modvar_type == LMD_TYPE_INT || mv_mc->modvar_type == LMD_TYPE_FLOAT))
                return mv_mc->modvar_type;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* bin = (JsBinaryNode*)node;
        // comparison operators always return bool
        switch (bin->op) {
        case JS_OP_LT: case JS_OP_LE: case JS_OP_GT: case JS_OP_GE:
        case JS_OP_EQ: case JS_OP_NE: case JS_OP_STRICT_EQ: case JS_OP_STRICT_NE:
        case JS_OP_INSTANCEOF: case JS_OP_IN:
            return LMD_TYPE_BOOL;
        default: break;
        }
        // arithmetic operators: propagate types
        TypeId left_t  = jm_get_effective_type(mt, bin->left);
        TypeId right_t = jm_get_effective_type(mt, bin->right);
        switch (bin->op) {
        case JS_OP_ADD:
            // if either is string, result is string (JS concatenation)
            if (left_t == LMD_TYPE_STRING || right_t == LMD_TYPE_STRING)
                return LMD_TYPE_STRING;
            // if either is float, result is float
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            // both int → int
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_SUB: case JS_OP_MUL:
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_INT;
            return LMD_TYPE_ANY;
        case JS_OP_EXP:
            // pow() always returns double
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_DIV:
            // JS division always produces float (7/2 == 3.5)
            if (left_t == LMD_TYPE_INT && right_t == LMD_TYPE_INT)
                return LMD_TYPE_FLOAT;
            if (left_t == LMD_TYPE_FLOAT || right_t == LMD_TYPE_FLOAT)
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_MOD:
            // modulo uses fmod() → always returns float (handles x%0 → NaN)
            if ((left_t == LMD_TYPE_INT || left_t == LMD_TYPE_FLOAT) &&
                (right_t == LMD_TYPE_INT || right_t == LMD_TYPE_FLOAT))
                return LMD_TYPE_FLOAT;
            return LMD_TYPE_ANY;
        case JS_OP_BIT_AND: case JS_OP_BIT_OR: case JS_OP_BIT_XOR:
        case JS_OP_BIT_LSHIFT: case JS_OP_BIT_RSHIFT: case JS_OP_BIT_URSHIFT:
            return LMD_TYPE_INT;  // bitwise ops always produce int32
        case JS_OP_AND: case JS_OP_OR:
            return LMD_TYPE_ANY;  // logical AND/OR return one of the operands
        default:
            return LMD_TYPE_ANY;
        }
    }

    case JS_AST_NODE_UNARY_EXPRESSION: {
        JsUnaryNode* un = (JsUnaryNode*)node;
        switch (un->op) {
        case JS_OP_NOT:    return LMD_TYPE_BOOL;
        case JS_OP_TYPEOF: return LMD_TYPE_STRING;
        case JS_OP_BIT_NOT: return LMD_TYPE_INT;
        case JS_OP_PLUS: case JS_OP_ADD: {
            TypeId t = jm_get_effective_type(mt, un->operand);
            return (t == LMD_TYPE_FLOAT) ? LMD_TYPE_FLOAT : LMD_TYPE_INT;
        }
        case JS_OP_MINUS: case JS_OP_SUB: {
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_FLOAT) return LMD_TYPE_FLOAT;
            if (t == LMD_TYPE_INT) {
                // v18p: -0 produces FLOAT (-0.0), not INT
                if (un->operand && un->operand->node_type == JS_AST_NODE_LITERAL) {
                    JsLiteralNode* lit = (JsLiteralNode*)un->operand;
                    if (lit->literal_type == JS_LITERAL_NUMBER && lit->value.number_value == 0.0)
                        return LMD_TYPE_FLOAT;
                }
                return LMD_TYPE_INT;
            }
            return LMD_TYPE_ANY;
        }
        case JS_OP_INCREMENT: case JS_OP_DECREMENT: {
            if (!un->operand) return LMD_TYPE_ANY;
            TypeId t = jm_get_effective_type(mt, un->operand);
            if (t == LMD_TYPE_INT || t == LMD_TYPE_FLOAT) return t;
            return LMD_TYPE_ANY;
        }
        default: return LMD_TYPE_ANY;
        }
    }

    case JS_AST_NODE_ASSIGNMENT_EXPRESSION: {
        JsAssignmentNode* asgn = (JsAssignmentNode*)node;
        if (asgn->op == JS_OP_ASSIGN)
            return jm_get_effective_type(mt, asgn->right);
        // compound assignment: depends on operator and operand types
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_CONDITIONAL_EXPRESSION: {
        JsConditionalNode* cond = (JsConditionalNode*)node;
        TypeId t1 = jm_get_effective_type(mt, cond->consequent);
        TypeId t2 = jm_get_effective_type(mt, cond->alternate);
        if (t1 == t2) return t1;
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_SEQUENCE_EXPRESSION: {
        // v11: comma operator returns type of last expression
        JsSequenceNode* seq = (JsSequenceNode*)node;
        JsAstNode* child = seq->expressions;
        JsAstNode* last = NULL;
        while (child) { last = child; child = child->next; }
        return last ? jm_get_effective_type(mt, last) : LMD_TYPE_ANY;
    }

    case JS_AST_NODE_CALL_EXPRESSION: {
        // Phase 4: If callee resolves to a function with a native version
        // and all arg types match, the call returns the function's return type
        JsCallNode* call = (JsCallNode*)node;
        JsFuncCollected* fc = jm_resolve_native_call(mt, call);
        if (fc) return fc->return_type;
        // Phase 5: If callee is Math.xxx(), resolve return type at compile time
        String* math_method = jm_get_math_method(call);
        if (math_method) return jm_math_return_type(math_method, mt, call->arguments);
        // Phase 3.5: return type from any collected function (not just native-eligible)
        // Skip generators — they return iterator objects, not the inferred return type
        {
            JsFuncCollected* any_fc = jm_find_collected_func_for_call(mt, call);
            if (any_fc && any_fc->return_type != LMD_TYPE_ANY
                && any_fc->node && !any_fc->node->is_generator)
                return any_fc->return_type;
        }
        return LMD_TYPE_ANY;
    }

    case JS_AST_NODE_MEMBER_EXPRESSION: {
        // P9: typed array element type inference
        JsMemberNode* mem = (JsMemberNode*)node;
        if (mem->computed) {
            JsMirVarEntry* ta_var = jm_get_typed_array_var(mt, mem->object);
            if (ta_var) {
                return jm_typed_array_is_int(ta_var->typed_array_type)
                    ? LMD_TYPE_INT : LMD_TYPE_FLOAT;
            }
        }
        // .length returns INT only for known arrays, strings, functions, typed arrays
        if (!mem->computed && mem->property &&
            mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* prop = (JsIdentifierNode*)mem->property;
            if (prop->name && prop->name->len == 6 && strncmp(prop->name->chars, "length", 6) == 0) {
                // Only infer INT for known types where .length is guaranteed numeric
                TypeId obj_t = jm_get_effective_type(mt, mem->object);
                if (obj_t == LMD_TYPE_ARRAY || obj_t == LMD_TYPE_STRING)
                    return LMD_TYPE_INT;
                // Check if it's a typed array variable
                JsMirVarEntry* ta = jm_get_typed_array_var(mt, mem->object);
                if (ta)
                    return LMD_TYPE_INT;
                // For functions, .length is param_count (INT)
                if (obj_t == LMD_TYPE_FUNC)
                    return LMD_TYPE_INT;
                // Unknown object type — .length can be anything (e.g., {length: {toString: fn}})
                return LMD_TYPE_ANY;
            }

            // P3.4: TypeMap shape lookup — if the object has a full_type (TypeMap from TS interface),
            // look up the property name in the ShapeEntry chain to find the field type.
            Type* obj_full = jm_get_full_type(mt, mem->object);
            if (obj_full && obj_full->type_id == LMD_TYPE_MAP) {
                TypeMap* tm = (TypeMap*)obj_full;
                for (ShapeEntry* se = tm->shape; se; se = se->next) {
                    if (se->name && se->name->str && se->name->length == prop->name->len &&
                        memcmp(se->name->str, prop->name->chars, prop->name->len) == 0) {
                        if (se->type) return se->type->type_id;
                    }
                }
            }

            // P1: Constructor field type lookup — if the object variable was created via
            // `new ClassName(...)` and the property is a known constructor field, return
            // the field type detected from the constructor init expression.
            // Also handles `this.prop` in class methods via mt->current_class.
            if (mem->object->node_type == JS_AST_NODE_IDENTIFIER) {
                JsIdentifierNode* obj_id = (JsIdentifierNode*)mem->object;
                JsClassEntry* ce = nullptr;
                // Check for `this` in class method context
                if (mt->current_class &&
                    obj_id->name->len == 4 && strncmp(obj_id->name->chars, "this", 4) == 0) {
                    ce = mt->current_class;
                }
                // Check for named variable with class_entry
                if (!ce) {
                    char vname[128];
                    snprintf(vname, sizeof(vname), "_js_%.*s", (int)obj_id->name->len, obj_id->name->chars);
                    JsMirVarEntry* var = jm_find_var(mt, vname);
                    if (var && var->class_entry) ce = var->class_entry;
                }
                if (ce && ce->constructor && ce->constructor->fc) {
                    int p1_slot = jm_ctor_prop_slot(ce->constructor->fc,
                        prop->name->chars, (int)prop->name->len);
                    if (p1_slot >= 0) {
                        TypeId ft = ce->constructor->fc->ctor_prop_types[p1_slot];
                        if (ft == LMD_TYPE_INT || ft == LMD_TYPE_FLOAT)
                            return ft;
                    }
                }
            }
        }
        return LMD_TYPE_ANY;
    }

    default:
        return LMD_TYPE_ANY;
    }
}

// Returns the full Type* for an expression (richer than just TypeId).
// Checks variable scope for Type* carried from TS annotations.
// Returns NULL for unknown or non-compound types.
Type* jm_get_full_type(JsMirTranspiler* mt, JsAstNode* node) {
    if (!node) return NULL;
    if (node->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)node;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var) return var->full_type;
    }
    return NULL;
}

// Check if a type is native (not boxed)
bool jm_is_native_type(TypeId tid) {
    return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL;
}

// Helper: if a variable is in the current function's scope env, mark it and write-back.
// Called after jm_set_var or assignment to propagate value to shared scope env.
void jm_scope_env_mark_and_writeback(JsMirTranspiler* mt, const char* name, MIR_reg_t val_reg, TypeId type_id) {
    if (mt->scope_env_reg == 0) return;
    // Check if this var name is in the current function's scope env
    int fi = mt->current_func_index;
    if (fi < 0 || fi >= mt->func_count) return;
    JsFuncCollected* fc = &mt->func_entries[fi];
    if (!fc->has_scope_env) return;
    for (int s = 0; s < fc->scope_env_count; s++) {
        if (strcmp(name, fc->scope_env_names[s]) == 0) {
            // Determine the actual slot: when reusing parent env, use the
            // remapped slot (from the var's env_slot), not the local index.
            int slot = s;
            if (fc->reuse_parent_env) {
                JsMirVarEntry* var = jm_find_var(mt, name);
                if (var && var->in_scope_env) {
                    // Use preserved scope_env_slot (set during scope env setup)
                    slot = var->scope_env_slot;
                } else if (var && var->from_env) {
                    slot = var->env_slot;
                } else {
                    // Fallback: look up the correct slot from captures
                    for (int c = 0; c < fc->capture_count; c++) {
                        if (strcmp(name, fc->captures[c].name) == 0) {
                            int cap_slot = fc->captures[c].scope_env_slot;
                            if (cap_slot >= 0) slot = cap_slot;
                            break;
                        }
                    }
                }
            }
            // Mark the variable entry
            JsMirVarEntry* var = jm_find_var(mt, name);
            if (var) {
                var->in_scope_env = true;
                var->scope_env_slot = slot;
                var->scope_env_reg = mt->scope_env_reg;
            }
            // Write current value to scope env
            MIR_reg_t val = val_reg;
            if (jm_is_native_type(type_id))
                val = jm_box_native(mt, val_reg, type_id);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64, slot * (int)sizeof(uint64_t), mt->scope_env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, val)));

            return;
        }
    }
}

// v23: truthiness check with inline fast-path for known boolean Items.
// For known-BOOL expressions (comparisons, !expr), extracts the low bit directly
// instead of calling js_is_truthy (saves a function call).
MIR_reg_t jm_emit_is_truthy(JsMirTranspiler* mt, MIR_reg_t val, JsAstNode* expr) {
    if (expr && jm_get_effective_type(mt, expr) == LMD_TYPE_BOOL) {
        MIR_reg_t result = jm_new_reg(mt, "trthy", MIR_T_I64);
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_AND,
            MIR_new_reg_op(mt->ctx, result),
            MIR_new_reg_op(mt->ctx, val),
            MIR_new_int_op(mt->ctx, 1)));
        return result;
    }
    return jm_emit_uext8(mt, jm_call_1(mt, "js_is_truthy", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(mt->ctx, val)));
}

// v23b: transpile an expression for use as a branch condition (if/while/for/ternary).
// Returns raw int64 0/1 directly usable in MIR_BF/BT.
// For untyped binary comparisons, calls _raw facades to avoid box→unbox cycle.
// For everything else, falls back to jm_transpile_box_item + jm_emit_is_truthy.
MIR_reg_t jm_transpile_condition(JsMirTranspiler* mt, JsAstNode* expr);

// Forward declarations for native expression transpilation
MIR_reg_t jm_transpile_expression(JsMirTranspiler* mt, JsAstNode* expr);
MIR_reg_t jm_transpile_box_item(JsMirTranspiler* mt, JsAstNode* item);

// Transpile an expression returning a native register of the target type.
// Handles literals inline (no boxing), identifiers from typed vars, and
// recursive expressions. Falls back to unbox from boxed Item when needed.
MIR_reg_t jm_transpile_as_native(JsMirTranspiler* mt, JsAstNode* expr,
                                         TypeId expr_type, TypeId target_type) {
    // Literals: emit native constant directly (bypass boxing)
    if (expr && expr->node_type == JS_AST_NODE_LITERAL) {
        JsLiteralNode* lit = (JsLiteralNode*)expr;
        if (lit->literal_type == JS_LITERAL_NUMBER) {
            if (target_type == LMD_TYPE_FLOAT) {
                MIR_reg_t r = jm_new_reg(mt, "dlit", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_double_op(mt->ctx, lit->value.number_value)));
                return r;
            } else {
                MIR_reg_t r = jm_new_reg(mt, "ilit", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, (int64_t)lit->value.number_value)));
                return r;
            }
        }
    }

    // Identifiers: use native register directly if variable is typed
    if (expr && expr->node_type == JS_AST_NODE_IDENTIFIER) {
        JsIdentifierNode* id = (JsIdentifierNode*)expr;
        char vname[128];
        snprintf(vname, sizeof(vname), "_js_%.*s", (int)id->name->len, id->name->chars);
        JsMirVarEntry* var = jm_find_var(mt, vname);
        if (var && jm_is_native_type(var->type_id)) {
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, var->reg, var->type_id);
            else
                return jm_ensure_native_int(mt, var->reg, var->type_id);
        }
        // boxed variable: unbox
        MIR_reg_t boxed;
        if (var) {
            boxed = var->reg;
        } else if (mt->module_consts) {
            // check module-level variables (e.g. top-level let/var accessed from for-loop update)
            JsModuleConstEntry lookup;
            snprintf(lookup.name, sizeof(lookup.name), "%s", vname);
            JsModuleConstEntry* mc = (JsModuleConstEntry*)hashmap_get(mt->module_consts, &lookup);
            if (mc && mc->const_type == MCONST_MODVAR) {
                boxed = jm_call_1(mt, "js_get_module_var", MIR_T_I64,
                    MIR_T_I64, MIR_new_int_op(mt->ctx, (int64_t)mc->int_val));
            } else if (mc && mc->const_type == MCONST_INT) {
                // constant int: emit directly as native
                MIR_reg_t r = jm_new_reg(mt, "mcint", MIR_T_I64);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_int_op(mt->ctx, mc->int_val)));
                if (target_type == LMD_TYPE_FLOAT)
                    return jm_ensure_native_float(mt, r, LMD_TYPE_INT);
                return r;
            } else if (mc && mc->const_type == MCONST_FLOAT) {
                MIR_reg_t r = jm_new_reg(mt, "mcflt", MIR_T_D);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                    MIR_new_reg_op(mt->ctx, r),
                    MIR_new_double_op(mt->ctx, mc->float_val)));
                if (target_type == LMD_TYPE_INT)
                    return jm_ensure_native_int(mt, r, LMD_TYPE_FLOAT);
                return r;
            } else {
                boxed = jm_emit_null(mt);
            }
        } else {
            boxed = jm_emit_null(mt);
        }
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, boxed);
        else {
            // Use it2d + D2I for robust int extraction (handles INT, FLOAT, any numeric)
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, boxed);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    // Other expressions: determine if jm_transpile_expression returns native.
    // Must check specifically whether the native path is actually taken.
    if (expr && expr->node_type == JS_AST_NODE_BINARY_EXPRESSION) {
        JsBinaryNode* bin = (JsBinaryNode*)expr;
        TypeId lt = jm_get_effective_type(mt, bin->left);
        TypeId rt = jm_get_effective_type(mt, bin->right);
        bool left_num  = (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT);
        bool right_num = (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT);
        bool both_numeric = left_num && right_num;
        // Determine if the native path is actually taken
        bool native_binary = both_numeric &&
            bin->op != JS_OP_EXP && bin->op != JS_OP_AND && bin->op != JS_OP_OR;
        // Comparisons return native 0/1 only when BOTH sides are typed numeric.
        // With one untyped side, the comparison falls through to boxed runtime
        // and returns a boxed boolean Item, not a native value.
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_binary) {
            // Native path was taken: result is native int or double
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, expr_type);
            else
                return jm_ensure_native_int(mt, result, expr_type);
        }
        // Boxed path was taken: result is boxed Item, need to unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            // Use it2d + D2I for robust int extraction (handles INT, FLOAT, etc.)
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    if (expr && expr->node_type == JS_AST_NODE_UNARY_EXPRESSION) {
        JsUnaryNode* un = (JsUnaryNode*)expr;
        // Check if unary op takes the native path
        bool native_unary = false;
        if (un->operand) {
            TypeId op_type = jm_get_effective_type(mt, un->operand);
            bool op_numeric = (op_type == LMD_TYPE_INT || op_type == LMD_TYPE_FLOAT);
            switch (un->op) {
            case JS_OP_PLUS: case JS_OP_ADD:
            case JS_OP_MINUS: case JS_OP_SUB:
                native_unary = op_numeric;
                break;
            case JS_OP_INCREMENT: case JS_OP_DECREMENT:
                // Only native if operand is a typed identifier
                if (un->operand->node_type == JS_AST_NODE_IDENTIFIER) {
                    JsIdentifierNode* uid = (JsIdentifierNode*)un->operand;
                    char uvname[128];
                    snprintf(uvname, sizeof(uvname), "_js_%.*s", (int)uid->name->len, uid->name->chars);
                    JsMirVarEntry* uvar = jm_find_var(mt, uvname);
                    native_unary = uvar && (uvar->type_id == LMD_TYPE_INT || uvar->type_id == LMD_TYPE_FLOAT) && !uvar->from_env;
                }
                break;
            default:
                break;
            }
        }
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_unary) {
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, expr_type);
            else
                return jm_ensure_native_int(mt, result, expr_type);
        }
        // Boxed result: unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    if (expr && expr->node_type == JS_AST_NODE_ASSIGNMENT_EXPRESSION) {
        JsAssignmentNode* asgn = (JsAssignmentNode*)expr;
        bool native_assign = false;
        TypeId assign_var_type = LMD_TYPE_ANY;
        if (asgn->left && asgn->left->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* aid = (JsIdentifierNode*)asgn->left;
            char avname[128];
            snprintf(avname, sizeof(avname), "_js_%.*s", (int)aid->name->len, aid->name->chars);
            JsMirVarEntry* avar = jm_find_var(mt, avname);
            native_assign = avar && !avar->from_env &&
                            (avar->type_id == LMD_TYPE_INT || avar->type_id == LMD_TYPE_FLOAT);
            if (native_assign) assign_var_type = avar->type_id;
        }
        MIR_reg_t result = jm_transpile_expression(mt, expr);
        if (native_assign) {
            // use the variable's actual type (not expr_type from get_effective_type)
            // because P9 widening may have changed the var to FLOAT while
            // get_effective_type still reports the RHS type (e.g. INT for `J = 0`)
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, assign_var_type);
            else
                return jm_ensure_native_int(mt, result, assign_var_type);
        }
        // Boxed result: unbox
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, result);
        else {
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, result);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    // Phase 4: Call expressions — if native call, result is already native
    if (expr && expr->node_type == JS_AST_NODE_CALL_EXPRESSION) {
        JsCallNode* call = (JsCallNode*)expr;

        // Phase 5: Math.xxx() calls — use native C math when arg types known
        String* math_method = jm_get_math_method(call);
        if (math_method) {
            return jm_transpile_math_native(mt, call, math_method, target_type);
        }

        JsFuncCollected* fc = jm_resolve_native_call(mt, call);
        if (fc) {
            // jm_transpile_expression → jm_transpile_call → native call → native result
            MIR_reg_t result = jm_transpile_expression(mt, expr);
            if (target_type == LMD_TYPE_FLOAT)
                return jm_ensure_native_float(mt, result, fc->return_type);
            else
                return jm_ensure_native_int(mt, result, fc->return_type);
        }
        // Non-native call: result is boxed → unbox
        MIR_reg_t boxed = jm_transpile_expression(mt, expr);
        if (target_type == LMD_TYPE_FLOAT)
            return jm_emit_unbox_float(mt, boxed);
        else {
            // Use it2d + D2I for robust int extraction (handles INT, FLOAT, any numeric)
            MIR_reg_t as_dbl = jm_emit_unbox_float(mt, boxed);
            return jm_emit_double_to_int(mt, as_dbl);
        }
    }

    // P9: MEMBER_EXPRESSION — typed array element access returns native directly
    if (expr && expr->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
        JsMemberNode* mem = (JsMemberNode*)expr;

        // P1: Shaped class property access — emit native-returning slot functions.
        // When `body.x` is used in arithmetic and we know `x` is a float field,
        // call js_get_slot_f(obj, byte_offset) → native double, no boxing.
        // Also handles `this.prop` in class methods via mt->current_class.
        if (!mem->computed && !mem->optional &&
            mem->object && mem->object->node_type == JS_AST_NODE_IDENTIFIER &&
            mem->property && mem->property->node_type == JS_AST_NODE_IDENTIFIER) {
            JsIdentifierNode* p1_obj = (JsIdentifierNode*)mem->object;
            JsIdentifierNode* p1_prop = (JsIdentifierNode*)mem->property;
            JsClassEntry* p1_ce = nullptr;
            // Check for `this` in class method context
            if (mt->current_class &&
                p1_obj->name->len == 4 && strncmp(p1_obj->name->chars, "this", 4) == 0) {
                p1_ce = mt->current_class;
            }
            // Check for named variable with class_entry
            if (!p1_ce) {
                char p1_vname[132];
                snprintf(p1_vname, sizeof(p1_vname), "_js_%.*s", (int)p1_obj->name->len, p1_obj->name->chars);
                JsMirVarEntry* p1_var = jm_find_var(mt, p1_vname);
                if (p1_var && p1_var->class_entry) p1_ce = p1_var->class_entry;
            }
            if (p1_ce && p1_ce->constructor && p1_ce->constructor->fc) {
                JsFuncCollected* p1_fc = p1_ce->constructor->fc;
                int p1_slot = jm_ctor_prop_slot(p1_fc,
                    p1_prop->name->chars, (int)p1_prop->name->len);
                if (p1_slot >= 0) {
                    TypeId field_type = p1_fc->ctor_prop_types[p1_slot];
                    int64_t byte_offset = (int64_t)p1_slot * (int64_t)sizeof(void*);
                    // TRACE: log whenever R property is given a native slot read
                    if (p1_prop->name->len == 1 && p1_prop->name->chars[0] == 'R') {
                        log_error("TRACE P1 slot read .R: field_type=%d slot=%d offset=%d class='%s'",
                            (int)field_type, p1_slot, (int)byte_offset,
                            p1_ce->constructor && p1_ce->constructor->fc && p1_ce->constructor->fc->name ?
                            p1_ce->constructor->fc->name : "anon");
                    }
                    MIR_reg_t obj_reg = jm_transpile_box_item(mt, mem->object);
                    if (field_type == LMD_TYPE_FLOAT) {
                        // §7: Inline shape guard → direct memory load (no function call)
                        if (p1_ce->shape_cache_ptr) {
                            MIR_label_t l_fast = jm_new_label(mt);
                            MIR_label_t l_slow = jm_new_label(mt);
                            MIR_label_t l_end = jm_new_label(mt);
                            MIR_reg_t result_f = jm_new_reg(mt, "s7f", MIR_T_D);
                            // Load obj->type (offset 8)
                            MIR_reg_t shape_reg = jm_new_reg(mt, "s7s", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, shape_reg),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, obj_reg, 0, 1)));
                            // Load expected shape from cache slot
                            MIR_reg_t cache_addr_reg = jm_new_reg(mt, "s7a", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, cache_addr_reg),
                                MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)p1_ce->shape_cache_ptr)));
                            MIR_reg_t expected_reg = jm_new_reg(mt, "s7e", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, expected_reg),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, cache_addr_reg, 0, 1)));
                            // Compare shape pointers
                            MIR_reg_t match_reg = jm_new_reg(mt, "s7m", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                                MIR_new_reg_op(mt->ctx, match_reg),
                                MIR_new_reg_op(mt->ctx, shape_reg),
                                MIR_new_reg_op(mt->ctx, expected_reg)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                                MIR_new_label_op(mt->ctx, l_fast),
                                MIR_new_reg_op(mt->ctx, match_reg)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                                MIR_new_label_op(mt->ctx, l_slow)));
                            // Fast path: direct memory load — zero function calls
                            jm_emit_label(mt, l_fast);
                            MIR_reg_t data_reg = jm_new_reg(mt, "s7d", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, data_reg),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, obj_reg, 0, 1)));
                            MIR_reg_t fast_f = jm_new_reg(mt, "s7v", MIR_T_D);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                MIR_new_reg_op(mt->ctx, fast_f),
                                MIR_new_mem_op(mt->ctx, MIR_T_D, (int)byte_offset, data_reg, 0, 1)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                MIR_new_reg_op(mt->ctx, result_f),
                                MIR_new_reg_op(mt->ctx, fast_f)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                                MIR_new_label_op(mt->ctx, l_end)));
                            // Slow path: fall back to runtime function
                            jm_emit_label(mt, l_slow);
                            MIR_reg_t slow_f = jm_call_2(mt, "js_get_slot_f", MIR_T_D,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, byte_offset));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_DMOV,
                                MIR_new_reg_op(mt->ctx, result_f),
                                MIR_new_reg_op(mt->ctx, slow_f)));
                            jm_emit_label(mt, l_end);
                            log_debug("§7: inline shape guard float %.*s.%.*s → offset %d",
                                      (int)p1_obj->name->len, p1_obj->name->chars,
                                      (int)p1_prop->name->len, p1_prop->name->chars, (int)byte_offset);
                            if (target_type == LMD_TYPE_FLOAT)
                                return result_f;
                            else
                                return jm_emit_double_to_int(mt, result_f);
                        }
                        MIR_reg_t native_f = jm_call_2(mt, "js_get_slot_f", MIR_T_D,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, byte_offset));
                        log_debug("P1: native float load %.*s.%.*s → offset %d",
                                  (int)p1_obj->name->len, p1_obj->name->chars,
                                  (int)p1_prop->name->len, p1_prop->name->chars, (int)byte_offset);
                        if (target_type == LMD_TYPE_FLOAT)
                            return native_f;
                        else
                            return jm_emit_double_to_int(mt, native_f);
                    }
                    if (field_type == LMD_TYPE_INT) {
                        // §7: Inline shape guard → direct memory load (no function call)
                        if (p1_ce->shape_cache_ptr) {
                            MIR_label_t l_fast = jm_new_label(mt);
                            MIR_label_t l_slow = jm_new_label(mt);
                            MIR_label_t l_end = jm_new_label(mt);
                            MIR_reg_t result_i = jm_new_reg(mt, "s7i", MIR_T_I64);
                            // Load obj->type (offset 8)
                            MIR_reg_t shape_reg = jm_new_reg(mt, "s7s", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, shape_reg),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 8, obj_reg, 0, 1)));
                            // Load expected shape from cache slot
                            MIR_reg_t cache_addr_reg = jm_new_reg(mt, "s7a", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, cache_addr_reg),
                                MIR_new_int_op(mt->ctx, (int64_t)(uintptr_t)p1_ce->shape_cache_ptr)));
                            MIR_reg_t expected_reg = jm_new_reg(mt, "s7e", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, expected_reg),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 0, cache_addr_reg, 0, 1)));
                            // Compare shape pointers
                            MIR_reg_t match_reg = jm_new_reg(mt, "s7m", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_EQ,
                                MIR_new_reg_op(mt->ctx, match_reg),
                                MIR_new_reg_op(mt->ctx, shape_reg),
                                MIR_new_reg_op(mt->ctx, expected_reg)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT,
                                MIR_new_label_op(mt->ctx, l_fast),
                                MIR_new_reg_op(mt->ctx, match_reg)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                                MIR_new_label_op(mt->ctx, l_slow)));
                            // Fast path: direct memory load — zero function calls
                            jm_emit_label(mt, l_fast);
                            MIR_reg_t data_reg = jm_new_reg(mt, "s7d", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, data_reg),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, 16, obj_reg, 0, 1)));
                            MIR_reg_t fast_i = jm_new_reg(mt, "s7v", MIR_T_I64);
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, fast_i),
                                MIR_new_mem_op(mt->ctx, MIR_T_I64, (int)byte_offset, data_reg, 0, 1)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, result_i),
                                MIR_new_reg_op(mt->ctx, fast_i)));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                                MIR_new_label_op(mt->ctx, l_end)));
                            // Slow path: fall back to runtime function
                            jm_emit_label(mt, l_slow);
                            MIR_reg_t slow_i = jm_call_2(mt, "js_get_slot_i", MIR_T_I64,
                                MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg),
                                MIR_T_I64, MIR_new_int_op(mt->ctx, byte_offset));
                            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                                MIR_new_reg_op(mt->ctx, result_i),
                                MIR_new_reg_op(mt->ctx, slow_i)));
                            jm_emit_label(mt, l_end);
                            log_debug("§7: inline shape guard int %.*s.%.*s → offset %d",
                                      (int)p1_obj->name->len, p1_obj->name->chars,
                                      (int)p1_prop->name->len, p1_prop->name->chars, (int)byte_offset);
                            if (target_type == LMD_TYPE_INT)
                                return result_i;
                            else
                                return jm_ensure_native_float(mt, result_i, LMD_TYPE_INT);
                        }
                        MIR_reg_t native_i = jm_call_2(mt, "js_get_slot_i", MIR_T_I64,
                            MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg),
                            MIR_T_I64, MIR_new_int_op(mt->ctx, byte_offset));
                        log_debug("P1: native int load %.*s.%.*s → offset %d",
                                  (int)p1_obj->name->len, p1_obj->name->chars,
                                  (int)p1_prop->name->len, p1_prop->name->chars, (int)byte_offset);
                        if (target_type == LMD_TYPE_INT)
                            return native_i;
                        else
                            return jm_ensure_native_float(mt, native_i, LMD_TYPE_INT);
                    }
                }
            }
        }

        if (mem->computed) {
            JsMirVarEntry* ta_var = jm_get_typed_array_var(mt, mem->object);
            if (ta_var) {
                // Get native int index
                MIR_reg_t idx_native;
                TypeId idx_type = jm_get_effective_type(mt, mem->property);
                if (idx_type == LMD_TYPE_INT) {
                    idx_native = jm_transpile_as_native(mt, mem->property, idx_type, LMD_TYPE_INT);
                } else if (idx_type == LMD_TYPE_FLOAT) {
                    MIR_reg_t idx_float = jm_transpile_as_native(mt, mem->property, idx_type, LMD_TYPE_FLOAT);
                    idx_native = jm_emit_double_to_int(mt, idx_float);
                } else {
                    // Unknown type: might be a Symbol at runtime. Fall through to boxed path.
                    goto skip_ta_native;
                }
                return jm_transpile_typed_array_get_native(mt, ta_var->reg, idx_native,
                    ta_var->typed_array_type, target_type, ta_var->hoisted_data_reg);
            }
            skip_ta_native:

            // A3: Regular array element access — get boxed Item then unbox to target.
            // When used in a float expression (target_type == FLOAT), the caller needs
            // a native double. Get the boxed result via js_array_get_int (avoiding
            // boxing the index), then unbox to float directly.
            TypeId idx_type = jm_get_effective_type(mt, mem->property);
            if (idx_type == LMD_TYPE_INT) {
                MIR_reg_t idx_native = jm_transpile_as_native(mt, mem->property, idx_type, LMD_TYPE_INT);
                JsMirVarEntry* arr_var = jm_get_js_array_var(mt, mem->object);
                MIR_reg_t boxed_result;
                if (arr_var) {
                    boxed_result = jm_transpile_array_get_inline(mt, arr_var->reg, idx_native,
                        arr_var->hoisted_data_reg, arr_var->hoisted_len_reg);
                } else {
                    MIR_reg_t obj_reg = jm_transpile_box_item(mt, mem->object);
                    boxed_result = jm_call_2(mt, "js_array_get_int", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, obj_reg),
                        MIR_T_I64, MIR_new_reg_op(mt->ctx, idx_native));
                }
                if (target_type == LMD_TYPE_FLOAT)
                    return jm_emit_unbox_float(mt, boxed_result);
                else
                    return jm_emit_unbox_int(mt, boxed_result);
            }
        }
    }

    // All other expressions: get boxed value and unbox to target type
    MIR_reg_t boxed = jm_transpile_box_item(mt, expr);
    if (target_type == LMD_TYPE_FLOAT)
        return jm_emit_unbox_float(mt, boxed);
    else {
        // Use it2d + D2I for robust int extraction (handles INT, FLOAT, any numeric)
        MIR_reg_t as_dbl = jm_emit_unbox_float(mt, boxed);
        return jm_emit_double_to_int(mt, as_dbl);
    }
}

