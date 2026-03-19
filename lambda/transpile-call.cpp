/**
 * transpile-call.cpp — Function call transpilation
 *
 * Extracted from transpile.cpp (Phase 4.3) for better modularity.
 * Contains: transpile_call_expr and its exclusive helper functions
 * (call argument handling, tail-call optimization, native math dispatch,
 * bitwise arg emission, etc.)
 */

#include "transpiler.hpp"
#include "safety_analyzer.hpp"
#include "../lib/log.h"

// Shared functions defined in transpile.cpp
extern Type TYPE_ANY, TYPE_INT;
void transpile_expr(Transpiler* tp, AstNode *expr_node);
void transpile_box_item(Transpiler* tp, AstNode *node);
bool emit_zero_value(Transpiler* tp, TypeId tid);
bool value_emits_native_type(Transpiler* tp, AstNode* value, TypeId target_type);
const char* get_container_unbox_fn(TypeId type_id);
bool callee_returns_retitem(AstCallNode* call_node);
bool current_func_returns_retitem(Transpiler* tp);
bool can_use_unboxed_call(AstCallNode* call_node, AstFuncNode* fn_node);
bool has_typed_params(AstFuncNode* fn_node);
Type* resolve_native_ret_type(AstFuncNode* fn_node);

// ============================================================================
// Static helpers (exclusive to call transpilation)
// ============================================================================

// Helper to check if type is numeric (int, int64, or float)
static inline bool is_numeric_type(TypeId t) {
    return t == LMD_TYPE_INT || t == LMD_TYPE_INT64 || t == LMD_TYPE_FLOAT;
}

// Helper to check if type is integer (int or int64)
static inline bool is_integer_type(TypeId t) {
    return t == LMD_TYPE_INT || t == LMD_TYPE_INT64;
}

// Helper to check if an expression produces a native C type (not a tagged Item).
// Native types are emitted as raw C int64_t/double/bool by transpile_expr.
// Item (ANY) types need it2l() to extract the integer value from the tagged representation.
static inline bool is_native_type(AstNode* arg) {
    if (!arg->type) return false;
    TypeId t = arg->type->type_id;
    return t == LMD_TYPE_INT || t == LMD_TYPE_INT64 || t == LMD_TYPE_FLOAT || t == LMD_TYPE_BOOL;
}

// Emit a bitwise operation argument as int64_t.
// For native-typed expressions: use (int64_t) cast (already raw C values).
// For Item-typed expressions: use it2l() to properly unbox the tagged Item.
static void emit_bitwise_arg(Transpiler* tp, AstNode* arg) {
    if (is_native_type(arg)) {
        strbuf_append_str(tp->code_buf, "(int64_t)(");
    } else {
        strbuf_append_str(tp->code_buf, "it2l(");
    }
    transpile_expr(tp, arg);
    strbuf_append_char(tp->code_buf, ')');
}

// Check if a sys func call can use native C math function (single-arg)
// Returns the C function name if applicable, NULL otherwise
static const char* can_use_native_math(AstSysFuncNode* sys_fn_node, AstNode* arg) {
    if (!sys_fn_node || !sys_fn_node->fn_info || !arg || !arg->type) return NULL;

    // Check if argument has known numeric type
    TypeId arg_type = arg->type->type_id;
    if (!is_numeric_type(arg_type)) return NULL;

    // Use the registry's native_c_name for single-arg native functions
    if (sys_fn_node->fn_info->native_c_name && sys_fn_node->fn_info->native_arg_count == 1) {
        return sys_fn_node->fn_info->native_c_name;
    }
    return NULL;
}

// Check if a sys func call can use native two-arg math function (pow, atan2, hypot, min, max)
// Returns the C function name if applicable, NULL otherwise
static const char* can_use_native_math_binary(AstSysFuncNode* sys_fn_node, AstNode* arg1, AstNode* arg2) {
    if (!sys_fn_node || !sys_fn_node->fn_info || !arg1 || !arg2 || !arg1->type || !arg2->type) return NULL;

    TypeId type1 = arg1->type->type_id;
    TypeId type2 = arg2->type->type_id;
    if (!is_numeric_type(type1) || !is_numeric_type(type2)) return NULL;

    // Use the registry's native_c_name for two-arg native functions
    if (sys_fn_node->fn_info->native_c_name && sys_fn_node->fn_info->native_arg_count == 2) {
        return sys_fn_node->fn_info->native_c_name;
    }
    return NULL;
}

// ============================================================================
// Call argument transpilation
// ============================================================================

// helper function to transpile a single argument with type coercion
void transpile_call_argument(Transpiler* tp, AstNode* arg, TypeParam* param_type, bool is_sys_func) {
    if (!arg) {
        // use default value if available
        if (param_type && param_type->default_value) {
            log_debug("using default value for param type=%d, default type=%d",
                param_type->type_id, param_type->default_value->type->type_id);
            // for optional params with default, box since function expects Item type
            if (param_type->is_optional) {
                transpile_box_item(tp, param_type->default_value);
            } else if (param_type->type_id == LMD_TYPE_ANY) {
                transpile_box_item(tp, param_type->default_value);
            } else {
                transpile_expr(tp, param_type->default_value);
            }
        } else {
            // No default value - use null/zero for missing parameters
            // For typed params, use native zero values to match the function's native param types
            if (param_type && !param_type->is_optional) {
                if (!emit_zero_value(tp, param_type->type_id)) {
                    strbuf_append_str(tp->code_buf, "ITEM_NULL");
                }
            } else {
                strbuf_append_str(tp->code_buf, "ITEM_NULL");
            }
        }
        return;
    }

    // For named arguments, get the actual value
    AstNode* value = arg;
    if (arg->node_type == AST_NODE_NAMED_ARG) {
        AstNamedNode* named_arg = (AstNamedNode*)arg;
        value = named_arg->as;
    }

    log_debug("transpile_call_argument: type=%d, node_type=%d",
        value->type ? value->type->type_id : -1, value->node_type);

    // For system functions, box DateTime arguments
    if (is_sys_func && value->type->type_id == LMD_TYPE_DTIME) {
        log_debug("transpile_call_argument: BOXING DateTime for sys func");
        transpile_box_item(tp, value);
    }
    // for optional params, always box to Item since function expects Item type
    else if (param_type && param_type->is_optional) {
        transpile_box_item(tp, value);
    }
    // boxing based on arg type and fn definition type
    else if (param_type) {
        if (param_type->type_id == value->type->type_id &&
            param_type->type_id != LMD_TYPE_STRING &&
            param_type->type_id != LMD_TYPE_BINARY &&
            param_type->type_id != LMD_TYPE_MAP &&
            param_type->type_id != LMD_TYPE_OBJECT &&
            param_type->type_id != LMD_TYPE_ELEMENT) {
            // Fast path: same type, not STRING/BINARY/MAP/OBJECT/ELEMENT
            // (container pointer types may have Item vs Map*/Object*/Element* mismatch)
            transpile_expr(tp, value);
        }
        else if (param_type->type_id == LMD_TYPE_FLOAT) {
            if ((value->type->type_id == LMD_TYPE_INT || value->type->type_id == LMD_TYPE_INT64 ||
                value->type->type_id == LMD_TYPE_FLOAT)) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2d(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else if (param_type->type_id == LMD_TYPE_INT64) {
            if (value->type->type_id == LMD_TYPE_INT || value->type->type_id == LMD_TYPE_INT64) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "((int64_t)");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2l(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                log_error("Error: incompatible argument type for int64 parameter");
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        else if (param_type->type_id == LMD_TYPE_INT) {
            if (value->type->type_id == LMD_TYPE_INT) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_INT64 || value->type->type_id == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "((int64_t)");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2i(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                log_error("Error: incompatible argument type for int parameter");
                strbuf_append_str(tp->code_buf, "null");
            }
        }
        // STRING/BINARY param: extract String* from Item, but skip roundtrip when
        // value already emits native String* (e.g., typed local variable, string literal)
        // (Issue #16: `: string` annotation makes C param `String*`, but callers may pass `Item`,
        //  or the arg may be a sys func returning Item despite STRING semantic type)
        else if (param_type->type_id == LMD_TYPE_STRING || param_type->type_id == LMD_TYPE_BINARY) {
            if (value_emits_native_type(tp, value, param_type->type_id)) {
                // value already produces String* — emit directly, no boxing roundtrip
                transpile_expr(tp, value);
            } else {
                strbuf_append_str(tp->code_buf, "it2s(");
                transpile_box_item(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
        }
        else if (param_type->type_id == LMD_TYPE_BOOL) {
            if (value->type->type_id == LMD_TYPE_BOOL) {
                transpile_expr(tp, value);
            }
            else if (value->type->type_id == LMD_TYPE_ANY) {
                strbuf_append_str(tp->code_buf, "it2b(");
                transpile_expr(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
            else {
                transpile_box_item(tp, value);
            }
        }
        // container pointer types: safely unbox Item to Map*/Object*/Element*/List*/Array*/Range*/Path*
        else if (get_container_unbox_fn(param_type->type_id)) {
            if (value_emits_native_type(tp, value, param_type->type_id)) {
                // value already produces native pointer — emit directly
                transpile_expr(tp, value);
            } else {
                // value produces Item — use safe type-checking unbox helper
                const char* unbox_fn = get_container_unbox_fn(param_type->type_id);
                strbuf_append_str(tp->code_buf, unbox_fn);
                strbuf_append_char(tp->code_buf, '(');
                transpile_box_item(tp, value);
                strbuf_append_char(tp->code_buf, ')');
            }
        }
        else {
            transpile_box_item(tp, value);
        }
    }
    else {
        transpile_box_item(tp, value);
    }
}

// ============================================================================
// Named argument helpers
// ============================================================================

// find parameter by name in a function's parameter list
AstNamedNode* find_param_by_name(AstFuncNode* fn_node, String* name) {
    if (!name) return NULL;
    AstNamedNode* param = fn_node->param;
    while (param) {
        if (param->name && strcmp(param->name->chars, name->chars) == 0) {
            return param;
        }
        param = (AstNamedNode*)param->next;
    }
    return NULL;
}

// ============================================================================
// Tail-call optimization
// ============================================================================

/**
 * Transpile a tail-recursive call as a goto statement.
 * Transforms: factorial(n-1, acc*n) into:
 *   { int _t0 = _n - 1; int _t1 = _acc * _n; _n = _t0; _acc = _t1; goto tco_start; }
 *
 * Note: We use temporary variables to handle cases like f(b, a) where args are swapped.
 */
void transpile_tail_call(Transpiler* tp, AstCallNode* call_node, AstFuncNode* tco_func) {
    log_debug("transpile_tail_call: converting recursive call to goto");

    // Arguments are NOT in tail position (they're evaluated before the goto)
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    // Generate: ({ temp assignments; param = temp; ...; goto tco_start; ITEM_NULL; })
    // The ITEM_NULL at end is never reached but satisfies the expression type
    strbuf_append_str(tp->code_buf, "({ ");

    // Count params and args
    int param_count = 0;
    AstNamedNode* param = tco_func->param;
    while (param) { param_count++; param = (AstNamedNode*)param->next; }

    // First pass: assign arguments to temporary variables
    // This handles cases like factorial(b, a) where we swap values
    AstNode* arg = call_node->argument;
    param = tco_func->param;
    int arg_idx = 0;

    while (arg && param) {
        // Generate: TypeX _tN = <arg>;
        Type* param_type = param->type;
        write_type(tp->code_buf, param_type);
        strbuf_append_format(tp->code_buf, " tco_tmp%d = ", arg_idx);
        // For Item (untyped) parameters, use box_item to ensure proper boxing
        // (e.g., literal 1 must become i2it(1) for Item parameters)
        if (!param_type || param_type->type_id == LMD_TYPE_ANY) {
            transpile_box_item(tp, arg);
        } else {
            // For typed parameters, check if the argument's type is Item/ANY
            // and needs unboxing to match the parameter's native type
            TypeId arg_tid = arg->type ? arg->type->type_id : LMD_TYPE_ANY;
            TypeId param_tid = param_type->type_id;
            if (arg_tid != param_tid && (arg_tid == LMD_TYPE_ANY || arg_tid == LMD_TYPE_NULL)) {
                // argument is Item but parameter is typed — unbox
                if (param_tid == LMD_TYPE_INT) {
                    strbuf_append_str(tp->code_buf, "it2i(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (param_tid == LMD_TYPE_INT64) {
                    strbuf_append_str(tp->code_buf, "it2l(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (param_tid == LMD_TYPE_FLOAT) {
                    strbuf_append_str(tp->code_buf, "it2d(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (param_tid == LMD_TYPE_BOOL) {
                    strbuf_append_str(tp->code_buf, "it2b(");
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else if (get_container_unbox_fn(param_tid)) {
                    // safely unbox Item to container pointer
                    const char* unbox_fn = get_container_unbox_fn(param_tid);
                    strbuf_append_str(tp->code_buf, unbox_fn);
                    strbuf_append_char(tp->code_buf, '(');
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                } else {
                    transpile_expr(tp, arg);
                }
            } else if (get_container_unbox_fn(param_tid)) {
                // same type but may need Item→pointer unbox
                if (value_emits_native_type(tp, arg, param_tid)) {
                    transpile_expr(tp, arg);
                } else {
                    const char* unbox_fn = get_container_unbox_fn(param_tid);
                    strbuf_append_str(tp->code_buf, unbox_fn);
                    strbuf_append_char(tp->code_buf, '(');
                    transpile_expr(tp, arg);
                    strbuf_append_char(tp->code_buf, ')');
                }
            } else {
                transpile_expr(tp, arg);
            }
        }
        strbuf_append_str(tp->code_buf, "; ");

        arg = arg->next;
        param = (AstNamedNode*)param->next;
        arg_idx++;
    }

    // Second pass: assign temporaries to parameters
    param = tco_func->param;
    for (int i = 0; i < arg_idx; i++) {
        if (!param) break;
        strbuf_append_str(tp->code_buf, "_");
        strbuf_append_str_n(tp->code_buf, param->name->chars, param->name->len);
        strbuf_append_format(tp->code_buf, " = tco_tmp%d; ", i);
        param = (AstNamedNode*)param->next;
    }

    // Restore tail position before generating the goto
    tp->in_tail_position = prev_in_tail;

    // Jump back to function start
    strbuf_append_str(tp->code_buf, "goto tco_start; ");

    // The expression type - never reached but keeps C happy
    Type* ret_type = ((TypeFunc*)tco_func->type)->returned;
    if (!ret_type) ret_type = &TYPE_ANY;
    if (ret_type->type_id == LMD_TYPE_INT) {
        strbuf_append_str(tp->code_buf, "0; })");
    } else if (ret_type->type_id == LMD_TYPE_FLOAT) {
        strbuf_append_str(tp->code_buf, "0.0; })");
    } else if (ret_type->type_id == LMD_TYPE_BOOL) {
        strbuf_append_str(tp->code_buf, "false; })");
    } else {
        strbuf_append_str(tp->code_buf, "ITEM_NULL; })");
    }
}

/**
 * Check if a call expression is a tail-recursive call to the TCO function.
 */
bool is_tco_tail_call(Transpiler* tp, AstCallNode* call_node) {
    if (!tp->tco_func || !tp->in_tail_position) return false;
    return is_recursive_call(call_node, tp->tco_func);
}

// ============================================================================
// Main call expression transpilation
// ============================================================================

void transpile_call_expr(Transpiler* tp, AstCallNode *call_node) {
    log_debug("transpile call expr");
    // Defensive validation: ensure all required pointers and components are valid
    if (!call_node || !call_node->function || !call_node->function->type) {
        log_error("Error: invalid call_node");
        strbuf_append_str(tp->code_buf, "ITEM_ERROR");
        return;
    }

    // '^' propagation: emit opening wrapper before the call
    // Use RetItem for structured error checking instead of sentinel-value LMD_TYPE_ERROR
    int prop_id = -1;
    bool prop_callee_retitem = false;  // true if callee returns RetItem directly
    if (call_node->propagate) {
        prop_id = tp->temp_var_counter;  // will be incremented at the end
        prop_callee_retitem = callee_returns_retitem(call_node);
        strbuf_append_format(tp->code_buf, "({RetItem _ri%d=", prop_id);
        if (!prop_callee_retitem) {
            // Unknown callee returns Item — wrap in item_to_ri() for uniform RetItem handling
            strbuf_append_str(tp->code_buf, "item_to_ri(");
        }
    }

    // TCO: Check if this is a tail-recursive call that should be transformed to goto
    if (is_tco_tail_call(tp, call_node)) {
        transpile_tail_call(tp, call_node, tp->tco_func);
        return;
    }

    // For non-TCO calls, arguments are NOT in tail position
    bool prev_in_tail = tp->in_tail_position;
    tp->in_tail_position = false;

    // write the function name/ptr
    TypeFunc *fn_type = NULL;
    AstFuncNode* fn_node = NULL;  // used for named args lookup
    bool is_sys_func = (call_node->function->node_type == AST_NODE_SYS_FUNC);
    bool is_fn_variable = false;  // true if calling through a function variable (need fn_call)
    bool is_direct_call = true;   // true if we can use direct function call

    if (is_sys_func) {
        AstSysFuncNode* sys_fn_node = (AstSysFuncNode*)call_node->function;
        AstNode* first_arg = call_node->argument;
        AstNode* second_arg = first_arg ? first_arg->next : NULL;
        SysFunc fn_id = sys_fn_node->fn_info->fn;

        // ==== PRIORITY 1: Integer-specific unboxed functions ====
        // These take precedence over generic float-based native math
        if (first_arg && !first_arg->next && first_arg->type) {
            TypeId arg_type = first_arg->type->type_id;

            // Integer abs (prefer fn_abs_i over fabs for integers)
            if (fn_id == SYSFUNC_ABS && is_integer_type(arg_type)) {
                strbuf_append_str(tp->code_buf, "i2it(fn_abs_i((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, ")))");
                return;
            }

            // Sign function with integer arg
            if (fn_id == SYSFUNC_SIGN && is_integer_type(arg_type)) {
                strbuf_append_str(tp->code_buf, "i2it(fn_sign_i((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, ")))");
                return;
            }

            // Integer floor/ceil/round/trunc (identity for integers)
            if ((fn_id == SYSFUNC_FLOOR || fn_id == SYSFUNC_CEIL ||
                 fn_id == SYSFUNC_ROUND || fn_id == SYSFUNC_TRUNC) && is_integer_type(arg_type)) {
                // For integers, floor/ceil/round is identity - just return the value boxed
                strbuf_append_str(tp->code_buf, "i2it((int64_t)(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }

            // bnot(a) — unary bitwise NOT: inline as ~a
            if (fn_id == SYSFUNC_BNOT) {
                strbuf_append_str(tp->code_buf, "(~");
                emit_bitwise_arg(tp, first_arg);
                strbuf_append_char(tp->code_buf, ')');
                return;
            }

            // ==== Native len() for typed collections/strings ====
            if (fn_id == SYSFUNC_LEN) {
                if (arg_type == LMD_TYPE_ARRAY) {
                    strbuf_append_str(tp->code_buf, "fn_len_l((List*)(");
                    transpile_expr(tp, first_arg);
                    strbuf_append_str(tp->code_buf, "))");
                    return;
                }
                if (arg_type == LMD_TYPE_ARRAY || arg_type == LMD_TYPE_ARRAY_INT ||
                    arg_type == LMD_TYPE_ARRAY_INT64 || arg_type == LMD_TYPE_ARRAY_FLOAT) {
                    strbuf_append_str(tp->code_buf, "fn_len_a((Array*)(");
                    transpile_expr(tp, first_arg);
                    strbuf_append_str(tp->code_buf, "))");
                    return;
                }
                if (arg_type == LMD_TYPE_STRING || arg_type == LMD_TYPE_SYMBOL) {
                    strbuf_append_str(tp->code_buf, "fn_len_s(");
                    transpile_expr(tp, first_arg);
                    strbuf_append_char(tp->code_buf, ')');
                    return;
                }
                if (arg_type == LMD_TYPE_ELEMENT) {
                    strbuf_append_str(tp->code_buf, "fn_len_e((Element*)(");
                    transpile_expr(tp, first_arg);
                    strbuf_append_str(tp->code_buf, "))");
                    return;
                }
                // Fallback: use generic fn_len(Item) for unknown types
            }
        }

        // ==== Bitwise binary functions — inline as C operators ====
        // band/bor/bxor: always safe to inline (no UB).
        // shl/shr: use guarded ternary to handle shift amounts outside [0,63].
        if (sys_fn_node->fn_info->c_arg_conv == C_ARG_NATIVE &&
            sys_fn_node->fn_info->arg_count == 2 &&
            first_arg && second_arg && !second_arg->next) {
            SysFunc fn_id = sys_fn_node->fn_info->fn;
            const char* c_op = NULL;
            bool is_shift = false;
            switch (fn_id) {
            case SYSFUNC_BAND: c_op = "&";  break;
            case SYSFUNC_BOR:  c_op = "|";  break;
            case SYSFUNC_BXOR: c_op = "^";  break;
            case SYSFUNC_SHL:  c_op = "<<"; is_shift = true; break;
            case SYSFUNC_SHR:  c_op = ">>"; is_shift = true; break;
            default: break;
            }
            if (c_op) {
                if (is_shift) {
                    // emit: (((b) >= 0 && (b) < 64) ? ((a) OP (b)) : 0)
                    // b is evaluated twice, but shift amounts are typically literals
                    strbuf_append_str(tp->code_buf, "((");
                    emit_bitwise_arg(tp, second_arg);
                    strbuf_append_str(tp->code_buf, ">=0&&");
                    emit_bitwise_arg(tp, second_arg);
                    strbuf_append_str(tp->code_buf, "<64)?(");
                    emit_bitwise_arg(tp, first_arg);
                    strbuf_append_str(tp->code_buf, c_op);
                    emit_bitwise_arg(tp, second_arg);
                    strbuf_append_str(tp->code_buf, "):0)");
                } else {
                    // emit: ((a) OP (b))
                    strbuf_append_str(tp->code_buf, "(");
                    emit_bitwise_arg(tp, first_arg);
                    strbuf_append_str(tp->code_buf, c_op);
                    emit_bitwise_arg(tp, second_arg);
                    strbuf_append_str(tp->code_buf, ")");
                }
                return;
            }
            // fallback for unknown bitwise: use function call
            strbuf_append_str(tp->code_buf, sys_fn_node->fn_info->c_func_name);
            strbuf_append_char(tp->code_buf, '(');
            emit_bitwise_arg(tp, first_arg);
            strbuf_append_char(tp->code_buf, ',');
            emit_bitwise_arg(tp, second_arg);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }

        // ==== PRIORITY 2: Native C math functions (single argument, double-based) ====
        const char* native_func = can_use_native_math(sys_fn_node, first_arg);

        if (native_func && first_arg && !first_arg->next) {
            // Use native C math: push_d(c_func((double)arg))
            // The result needs to be boxed since native math returns double
            strbuf_append_str(tp->code_buf, "push_d(");
            strbuf_append_str(tp->code_buf, native_func);
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, first_arg);
            strbuf_append_str(tp->code_buf, ")))");
            return;  // done - skip normal argument handling
        }

        // Check if we can use native two-argument math function (pow, atan2, hypot, min, max)
        const char* native_binary_math = can_use_native_math_binary(sys_fn_node, first_arg, second_arg);
        if (native_binary_math && first_arg && second_arg && !second_arg->next) {
            // Use native binary math: push_d(fn_pow_u((double)arg1, (double)arg2))
            strbuf_append_str(tp->code_buf, "push_d(");
            strbuf_append_str(tp->code_buf, native_binary_math);
            strbuf_append_str(tp->code_buf, "((double)(");
            transpile_expr(tp, first_arg);
            strbuf_append_str(tp->code_buf, "),(double)(");
            transpile_expr(tp, second_arg);
            strbuf_append_str(tp->code_buf, ")))");
            return;
        }

        // Check for remaining single-arg unboxed functions (float sign)
        // Note: abs, int sign, floor/ceil/round for int, and neg are handled elsewhere
        if (first_arg && !first_arg->next && first_arg->type) {
            TypeId arg_type = first_arg->type->type_id;

            // Float sign (integer sign handled in PRIORITY 1)
            if (fn_id == SYSFUNC_SIGN && arg_type == LMD_TYPE_FLOAT) {
                strbuf_append_str(tp->code_buf, "i2it(fn_sign_f(");
                transpile_expr(tp, first_arg);
                strbuf_append_str(tp->code_buf, "))");
                return;
            }
        }

        // ==== VMap: map() and m.set(k, v) ====
        if (fn_id == SYSFUNC_VMAP_NEW) {
            if (!first_arg) {
                // map() → vmap_new()
                strbuf_append_str(tp->code_buf, "vmap_new()");
            } else {
                // map([k1, v1, k2, v2, ...]) → vmap_from_array(arr)
                strbuf_append_str(tp->code_buf, "vmap_from_array(");
                transpile_box_item(tp, first_arg);
                strbuf_append_char(tp->code_buf, ')');
            }
            return;
        }
        if (sys_fn_node->fn_info->fn == SYSPROC_VMAP_SET) {
            // m.set(k, v) → vmap_set(m, k, v)
            strbuf_append_str(tp->code_buf, "vmap_set(");
            transpile_box_item(tp, first_arg);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, second_arg);
            strbuf_append_char(tp->code_buf, ',');
            transpile_box_item(tp, second_arg->next);
            strbuf_append_char(tp->code_buf, ')');
            return;
        }

        // Use pre-computed c_func_name from the registry (e.g., "fn_len", "fn_symbol1", "pn_print")
        strbuf_append_str(tp->code_buf, sys_fn_node->fn_info->c_func_name);
    }
    else {
        TypeId callee_type_id = call_node->function->type ? call_node->function->type->type_id : LMD_TYPE_NULL;

        // Check if callee is a parameter or variable that should be treated as a function
        // even if it doesn't have explicit function type (e.g., let add10 = make_adder(10); add10(5))
        bool is_callable_param = false;
        bool is_callable_variable = false;  // variable that might hold a function at runtime
        bool is_callable_call_expr = false; // callee is a call expression (e.g., make_adder(10)(5))
        AstPrimaryNode *primary_fn_node = call_node->function->node_type == AST_NODE_PRIMARY ?
            (AstPrimaryNode*)call_node->function : null;

        // Early check for undefined function call - prevents crash
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            if (!ident_node->entry) {
                // Calling an undefined identifier - emit error
                log_error("Error: call to undefined function '%.*s'",
                    (int)ident_node->name->len, ident_node->name->chars);
                tp->error_count++;
                strbuf_append_str(tp->code_buf, "ItemError");
                return;
            }
        }

        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            AstNode* entry_node = ident_node->entry ? ident_node->entry->node : NULL;
            if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                // This is a parameter being used as a callable
                is_callable_param = true;
            }
            else if (entry_node && entry_node->node_type == AST_NODE_ASSIGN) {
                // This is a let variable being called - might hold a function at runtime
                // We can't statically know it's a function, but we'll use fn_call dynamically
                is_callable_variable = true;
                log_debug("callable variable (let) detected: %.*s",
                    (int)ident_node->name->len, ident_node->name->chars);
            }
        }

        // Check if callee is a call expression (chained call like make_adder(10)(5))
        // Note: call expressions may be wrapped in a primary expression
        if (call_node->function->node_type == AST_NODE_CALL_EXPR) {
            is_callable_call_expr = true;
            log_debug("callable call expression detected (direct)");
        }
        else if (primary_fn_node && primary_fn_node->expr &&
                 primary_fn_node->expr->node_type == AST_NODE_CALL_EXPR) {
            is_callable_call_expr = true;
            log_debug("callable call expression detected (wrapped in primary)");
        }

        // Check if callee is a member/index expression (e.g., obj.method() or arr[idx]())
        // These may be wrapped in a primary expression
        bool is_callable_member = false;
        if (call_node->function->node_type == AST_NODE_MEMBER_EXPR ||
            call_node->function->node_type == AST_NODE_INDEX_EXPR) {
            is_callable_member = true;
        }
        else if (primary_fn_node && primary_fn_node->expr &&
                 (primary_fn_node->expr->node_type == AST_NODE_MEMBER_EXPR ||
                  primary_fn_node->expr->node_type == AST_NODE_INDEX_EXPR)) {
            is_callable_member = true;
        }

        if (callee_type_id == LMD_TYPE_FUNC || is_callable_param || is_callable_variable || is_callable_call_expr || is_callable_member) {
            if (callee_type_id == LMD_TYPE_FUNC) {
                fn_type = (TypeFunc*)call_node->function->type;
            }
            if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
                AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
                AstNode* entry_node = ident_node->entry ? ident_node->entry->node : NULL;
                if (entry_node && (entry_node->node_type == AST_NODE_FUNC ||
                    entry_node->node_type == AST_NODE_FUNC_EXPR || entry_node->node_type == AST_NODE_PROC)) {
                    // Direct function reference - use direct call
                    fn_node = (AstFuncNode*)entry_node;  // save for named args lookup
                    write_fn_name_ex(tp->code_buf, fn_node,
                        (AstImportNode*)ident_node->entry->import,
                        NULL);
                } else if (entry_node && entry_node->node_type == AST_NODE_PARAM) {
                    // Function parameter - use fn_call for dynamic dispatch
                    is_fn_variable = true;
                    is_direct_call = false;
                    log_debug("callable parameter detected: %.*s",
                        (int)ident_node->name->len, ident_node->name->chars);
                } else {
                    // Variable holding a function - use fn_call for dynamic dispatch
                    is_fn_variable = true;
                    is_direct_call = false;
                    log_debug("function variable detected: %.*s",
                        (int)ident_node->name->len, ident_node->name->chars);
                }
            }
            else if (call_node->function->node_type == AST_NODE_INDEX_EXPR ||
                     call_node->function->node_type == AST_NODE_MEMBER_EXPR) {
                // Function from array/map access - need dynamic call
                is_fn_variable = true;
                is_direct_call = false;
                log_debug("function from index/member expression");
            }
            else {
                // Other expression returning function - use dynamic call
                is_fn_variable = true;
                is_direct_call = false;
                log_debug("function from expression");
            }

            // For anonymous functions referenced directly (not through variable), use ->ptr
            if (fn_type && fn_type->is_anonymous && !is_fn_variable) {
                transpile_expr(tp, call_node->function);
                strbuf_append_str(tp->code_buf, "->ptr");
            }
        } else { // handle Item
            log_debug("call function type is not func");
            strbuf_append_str(tp->code_buf, "ITEM_ERROR");
            return;
        }
    }

    // count arguments and check for named arguments
    int arg_count = 0;
    bool has_named_args = false;
    AstNode* arg = call_node->argument;
    while (arg) {
        if (arg->node_type == AST_NODE_NAMED_ARG) {
            has_named_args = true;
        }
        arg_count++;
        arg = arg->next;
    }

    int expected_count = fn_type ? fn_type->param_count : -1;
    bool is_variadic = fn_type ? fn_type->is_variadic : false;

    // For function variables, use fn_callN() for efficiency or fn_call() for many args
    if (is_fn_variable) {
        // Get the function pointer expression
        AstPrimaryNode *primary_fn_node = call_node->function->node_type == AST_NODE_PRIMARY ?
            (AstPrimaryNode*)call_node->function : null;

        // Check if callee is a parameter with Item type (needs extraction from tagged pointer)
        bool needs_item_extraction = false;
        AstNode* callee_entry_node = nullptr;
        if (primary_fn_node && primary_fn_node->expr && primary_fn_node->expr->node_type == AST_NODE_IDENT) {
            AstIdentNode* ident_node = (AstIdentNode*)primary_fn_node->expr;
            callee_entry_node = ident_node->entry ? ident_node->entry->node : nullptr;
            if (callee_entry_node && callee_entry_node->node_type == AST_NODE_PARAM) {
                AstNamedNode* param_node = (AstNamedNode*)callee_entry_node;
                // If parameter doesn't have explicit function type, it's Item type
                if (!param_node->type || param_node->type->type_id != LMD_TYPE_FUNC) {
                    needs_item_extraction = true;
                }
            }
        }

        if (arg_count <= 3 && !is_variadic) {
            // Use specialized fn_callN for common cases (avoids list allocation)
            strbuf_append_format(tp->code_buf, "fn_call%d(", arg_count);

            // Emit function expression - Function* is a direct pointer like other containers
            if (primary_fn_node && primary_fn_node->expr && callee_entry_node) {
                if (needs_item_extraction) {
                    // Cast Item to Function* (direct pointer, no masking needed)
                    strbuf_append_str(tp->code_buf, "(Function*)");
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                }
            } else {
                strbuf_append_str(tp->code_buf, "(Function*)");
                transpile_expr(tp, call_node->function);
            }

            // Emit arguments
            arg = call_node->argument;
            while (arg) {
                strbuf_append_char(tp->code_buf, ',');
                transpile_box_item(tp, arg);
                arg = arg->next;
            }
            strbuf_append_char(tp->code_buf, ')');
        } else {
            // Use fn_call with List for more arguments
            strbuf_append_str(tp->code_buf, "fn_call(");

            // Emit function expression - Function* is a direct pointer like other containers
            if (primary_fn_node && primary_fn_node->expr && callee_entry_node) {
                if (needs_item_extraction) {
                    // Cast Item to Function* (direct pointer, no masking needed)
                    strbuf_append_str(tp->code_buf, "(Function*)");
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                } else {
                    write_var_name(tp->code_buf, (AstNamedNode*)callee_entry_node, NULL);
                }
            } else {
                strbuf_append_str(tp->code_buf, "(Function*)");
                transpile_expr(tp, call_node->function);
            }

            // Build argument list
            strbuf_append_str(tp->code_buf, ",({Item fa[]={");
            arg = call_node->argument;
            bool first = true;
            while (arg) {
                if (!first) strbuf_append_char(tp->code_buf, ',');
                transpile_box_item(tp, arg);
                arg = arg->next;
                first = false;
            }
            strbuf_append_format(tp->code_buf,
                "}; List fl={.type_id=%d,.items=fa,.length=%d,.capacity=%d}; &fl;}))",
                LMD_TYPE_ARRAY, arg_count, arg_count);
        }
        return;  // Done with function variable call
    }

    // Direct function call (original logic)
    // transpile the params
    strbuf_append_str(tp->code_buf, "(");
    bool has_output_arg = false;

    if (has_named_args && fn_node) {
        // named arguments: need to reorder arguments based on parameter names
        log_debug("handling named arguments");

        // create array to hold resolved arguments for each param position
        // using stack allocation for reasonable param counts
        #define MAX_PARAMS 32
        AstNode* resolved_args[MAX_PARAMS] = {0};

        // first pass: collect positional args and named args
        int positional_index = 0;
        arg = call_node->argument;
        while (arg) {
            if (arg->node_type == AST_NODE_NAMED_ARG) {
                // find the parameter by name
                AstNamedNode* named_arg = (AstNamedNode*)arg;
                AstNamedNode* param = find_param_by_name(fn_node, named_arg->name);
                if (param) {
                    // find the index of this param
                    int param_index = 0;
                    AstNamedNode* p = fn_node->param;
                    while (p && p != param) {
                        p = (AstNamedNode*)p->next;
                        param_index++;
                    }
                    if (param_index < MAX_PARAMS) {
                        if (resolved_args[param_index]) {
                            log_error("Error: duplicate argument for parameter '%s'", named_arg->name->chars);
                        }
                        resolved_args[param_index] = arg;
                        log_debug("named arg '%s' -> param index %d", named_arg->name->chars, param_index);
                    }
                } else {
                    log_error("Error: unknown parameter name '%s'", named_arg->name->chars);
                }
            } else {
                // positional argument
                if (positional_index < MAX_PARAMS && !resolved_args[positional_index]) {
                    resolved_args[positional_index] = arg;
                    log_debug("positional arg -> param index %d", positional_index);
                }
                positional_index++;
            }
            arg = arg->next;
        }

        // output arguments in parameter order
        TypeParam* param_type = fn_type ? fn_type->param : NULL;
        for (int i = 0; i < expected_count && i < MAX_PARAMS; i++) {
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            has_output_arg = true;

            AstNode* resolved_arg = resolved_args[i];
            transpile_call_argument(tp, resolved_arg, param_type, is_sys_func);
            param_type = param_type ? param_type->next : NULL;
        }

        // add variadic args list for variadic functions
        if (is_variadic) {
            if (has_output_arg) strbuf_append_char(tp->code_buf, ',');
            // collect any named args that exceed param_count into vargs
            strbuf_append_str(tp->code_buf, "null");  // TODO: named variadic args not supported yet
            has_output_arg = true;
        }
        #undef MAX_PARAMS
    }
    else {
        // no named arguments: simple positional processing
        arg = call_node->argument;
        TypeParam* param_type = fn_type ? fn_type->param : NULL;
        int arg_index = 0;

        // first pass: output regular parameters
        while (arg && (expected_count < 0 || arg_index < expected_count)) {
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            has_output_arg = true;

            transpile_call_argument(tp, arg, param_type, is_sys_func);

            arg = arg->next;
            param_type = param_type ? param_type->next : NULL;
            arg_index++;
        }

        // Fill missing arguments with default values or ITEM_NULL
        while (param_type) {
            log_debug("filling missing argument with default/null for param");
            if (has_output_arg) {
                strbuf_append_char(tp->code_buf, ',');
            }
            transpile_call_argument(tp, NULL, param_type, is_sys_func);
            has_output_arg = true;
            param_type = param_type->next;
        }

        // handle variadic arguments
        if (is_variadic) {
            if (has_output_arg) strbuf_append_char(tp->code_buf, ',');

            // count variadic args
            int varg_count = 0;
            AstNode* varg = arg;
            while (varg) { varg_count++; varg = varg->next; }

            if (varg_count == 0) {
                strbuf_append_str(tp->code_buf, "null");  // no variadic args
            } else {
                // build inline list: ({Item va[]={...}; List vl={...}; &vl;})
                strbuf_append_str(tp->code_buf, "({Item va[]={");
                bool first_varg = true;
                while (arg) {
                    if (!first_varg) strbuf_append_char(tp->code_buf, ',');
                    transpile_box_item(tp, arg);
                    arg = arg->next;
                    first_varg = false;
                }
                strbuf_append_format(tp->code_buf,
                    "}; List vl={.type_id=%d,.items=va,.length=%d,.capacity=%d}; &vl;})",
                    LMD_TYPE_ARRAY, varg_count, varg_count);
            }
            has_output_arg = true;
        } else {
            // discard extra arguments with warning (non-variadic case)
            while (arg) {
                log_warn("param_mismatch: discarding extra argument %d (function expects %d params)",
                    arg_index + 1, expected_count);
                arg = arg->next;
                arg_index++;
            }
        }
    }

    strbuf_append_char(tp->code_buf, ')');

    // '^' propagation: wrap call result in error check using RetItem
    if (call_node->propagate) {
        if (!prop_callee_retitem) {
            strbuf_append_char(tp->code_buf, ')');  // close item_to_ri(
        }
        // use unique counter for temp variable to avoid name collisions
        int prop_id = tp->temp_var_counter++;
        // close the statement expression: check RetItem.err, return error or yield value
        if (current_func_returns_retitem(tp)) {
            // enclosing function returns RetItem — forward the entire RetItem as-is
            strbuf_append_format(tp->code_buf, "; if(_ri%d.err) return _ri%d; _ri%d.value;})", prop_id, prop_id, prop_id);
        } else {
            // enclosing function returns Item (closure/method) — return the error Item from .value
            strbuf_append_format(tp->code_buf, "; if(_ri%d.err) return _ri%d.value; _ri%d.value;})", prop_id, prop_id, prop_id);
        }
    }

    tp->in_tail_position = prev_in_tail;
}
