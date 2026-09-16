#include "mvp.h"

#include "../js_ast.hpp"
#include "../../runtime/ast-core.hpp"
#include "../../mir/mir.h"
#include "../../mir/mir-gen.h"
#include "../../../lib/mem.h"

#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

typedef uint64_t (*MvpGenericEntry)(MvpExecution* execution, uint64_t receiver);

typedef struct MvpGenericFunction {
    AstFuncNode* ast;
    AstClassNode* owner_class;
    MIR_item_t forward;
    MIR_item_t item;
    MvpJitFunctionDescriptor descriptor;
    char name[32];
    size_t parameter_count;
    int has_rest_parameter;
    int is_method;
    int is_static_method;
    int is_closure;
    int captures_lexical_this;
    int unavailable;
    int needs_environment;
} MvpGenericFunction;

typedef struct MvpGenericGlobal {
    String* name;
    AstNode* initializer;
} MvpGenericGlobal;

typedef struct MvpGenericCompiler {
    MIR_context_t context;
    MIR_module_t module;
    MvpGenericFunction* functions;
    size_t function_count;
    uint32_t next_register;
    uint32_t next_symbol;
    const AstNode* last_node;
    AstScript* script;
    MvpGenericGlobal globals[256];
    size_t global_count;
    MvpNumericProgram** numeric_programs;
    size_t numeric_program_count;
} MvpGenericCompiler;

typedef struct MvpGenericBinding {
    String* name;
    MIR_reg_t reg;
    int stored_in_environment;
} MvpGenericBinding;

typedef struct MvpGenericControlTarget {
    MIR_label_t break_target;
    MIR_label_t continue_target;
    const char* label;
    int label_length;
} MvpGenericControlTarget;

typedef struct MvpGenericState {
    MvpGenericCompiler* compiler;
    MIR_item_t function_item;
    MIR_func_t function;
    MIR_reg_t execution;
    MIR_reg_t receiver;
    MvpGenericBinding bindings[512];
    size_t binding_count;
    size_t root_count;
    size_t root_budget;
    // This remains valid across nested calls because every activation owns its
    // own frame. Direct stores avoid a host call for every spilled guest value.
    MIR_reg_t root_values;
    MvpGenericControlTarget controls[64];
    size_t control_count;
    MIR_reg_t environment;
    MIR_reg_t capture_environment;
    MIR_label_t error_exit;
    AstClassNode* owner_class;
    int is_closure;
    int captures_lexical_this;
    int is_static_method;
    int is_main;
} MvpGenericState;

static char mvp_generic_failure[160];

static void mvp_generic_set_failure(const char* stage, size_t index) {
    if (mvp_generic_failure[0]) return;
    int written = snprintf(mvp_generic_failure, sizeof(mvp_generic_failure),
        "MVP generic MIR lowering stopped in %s #%zu", stage, index);
    if (written <= 0 || (size_t)written >= sizeof(mvp_generic_failure)) {
        mvp_generic_failure[sizeof(mvp_generic_failure) - 1] = '\0';
    }
}

static void mvp_generic_set_node_failure(const char* stage, const AstNode* node) {
    if (mvp_generic_failure[0]) return;
    int written = snprintf(mvp_generic_failure, sizeof(mvp_generic_failure),
        "MVP generic MIR lowering does not support %s AST node type %u", stage,
        node ? (unsigned)node->node_type : 0);
    if (written <= 0 || (size_t)written >= sizeof(mvp_generic_failure)) {
        mvp_generic_failure[sizeof(mvp_generic_failure) - 1] = '\0';
    }
}

static void mvp_generic_set_execution_failure(const MvpExecution* execution) {
    if (mvp_generic_failure[0]) return;
    const char* message = mvp_execution_error(execution);
    int written = snprintf(mvp_generic_failure, sizeof(mvp_generic_failure),
        "MVP runtime execution failed: %s", message ? message : "unknown failure");
    if (written <= 0 || (size_t)written >= sizeof(mvp_generic_failure)) {
        mvp_generic_failure[sizeof(mvp_generic_failure) - 1] = '\0';
    }
}

static void mvp_generic_set_function_node_failure(const MvpGenericFunction* function,
        const AstNode* node) {
    if (mvp_generic_failure[0]) return;
    const String* name = function && function->ast ? function->ast->name : NULL;
    int written = snprintf(mvp_generic_failure, sizeof(mvp_generic_failure),
        "MVP generic MIR lowering does not support function %.*s at byte %u, AST node type %u at byte %u",
        name ? (int)name->len : 7, name ? name->chars : "unknown",
        function && function->ast ? function->ast->source_span.start_byte : 0,
        node ? (unsigned)node->node_type : 0, node ? node->source_span.start_byte : 0);
    if (written <= 0 || (size_t)written >= sizeof(mvp_generic_failure)) {
        mvp_generic_failure[sizeof(mvp_generic_failure) - 1] = '\0';
    }
}

static void mvp_generic_set_identifier_failure(const String* name) {
    if (mvp_generic_failure[0]) return;
    int written = snprintf(mvp_generic_failure, sizeof(mvp_generic_failure),
        "MVP generic MIR lowering cannot resolve identifier %.*s",
        name ? (int)name->len : 7, name ? name->chars : "unknown");
    if (written <= 0 || (size_t)written >= sizeof(mvp_generic_failure)) {
        mvp_generic_failure[sizeof(mvp_generic_failure) - 1] = '\0';
    }
}

const char* mvp_generic_last_failure(void) {
    return mvp_generic_failure[0] ? mvp_generic_failure : NULL;
}

static void* mvp_generic_import_resolver(const char* name) {
    if (!name) return NULL;
    if (strcmp(name, "mvp_execution_enter_frame") == 0) return (void*)mvp_execution_enter_frame;
    if (strcmp(name, "mvp_execution_leave_frame") == 0) return (void*)mvp_execution_leave_frame;
    if (strcmp(name, "mvp_execution_set_root") == 0) return (void*)mvp_execution_set_root;
    if (strcmp(name, "mvp_execution_has_error") == 0) return (void*)mvp_execution_has_error;
    if (strcmp(name, "mvp_execution_global_get") == 0) return (void*)mvp_execution_global_get;
    if (strcmp(name, "mvp_execution_global_set") == 0) return (void*)mvp_execution_global_set;
    if (strcmp(name, "mvp_execution_global_object") == 0) {
        return (void*)mvp_execution_global_object;
    }
    if (strcmp(name, "mvp_op_literal_string") == 0) return (void*)mvp_op_literal_string;
    if (strcmp(name, "mvp_op_add") == 0) return (void*)mvp_op_add;
    if (strcmp(name, "mvp_op_sub") == 0) return (void*)mvp_op_sub;
    if (strcmp(name, "mvp_op_mul") == 0) return (void*)mvp_op_mul;
    if (strcmp(name, "mvp_op_div") == 0) return (void*)mvp_op_div;
    if (strcmp(name, "mvp_op_mod") == 0) return (void*)mvp_op_mod;
    if (strcmp(name, "mvp_op_bit_and") == 0) return (void*)mvp_op_bit_and;
    if (strcmp(name, "mvp_op_bit_or") == 0) return (void*)mvp_op_bit_or;
    if (strcmp(name, "mvp_op_bit_xor") == 0) return (void*)mvp_op_bit_xor;
    if (strcmp(name, "mvp_op_bit_lshift") == 0) return (void*)mvp_op_bit_lshift;
    if (strcmp(name, "mvp_op_bit_urshift") == 0) return (void*)mvp_op_bit_urshift;
    if (strcmp(name, "mvp_op_neg") == 0) return (void*)mvp_op_neg;
    if (strcmp(name, "mvp_op_equal") == 0) return (void*)mvp_op_equal;
    if (strcmp(name, "mvp_op_not_equal") == 0) return (void*)mvp_op_not_equal;
    if (strcmp(name, "mvp_op_strict_equal") == 0) return (void*)mvp_op_strict_equal;
    if (strcmp(name, "mvp_op_strict_not_equal") == 0) return (void*)mvp_op_strict_not_equal;
    if (strcmp(name, "mvp_op_less_than") == 0) return (void*)mvp_op_less_than;
    if (strcmp(name, "mvp_op_less_equal") == 0) return (void*)mvp_op_less_equal;
    if (strcmp(name, "mvp_op_greater_than") == 0) return (void*)mvp_op_greater_than;
    if (strcmp(name, "mvp_op_greater_equal") == 0) return (void*)mvp_op_greater_equal;
    if (strcmp(name, "mvp_op_in") == 0) return (void*)mvp_op_in;
    if (strcmp(name, "mvp_op_bit_rshift") == 0) return (void*)mvp_op_bit_rshift;
    if (strcmp(name, "mvp_op_bit_not") == 0) return (void*)mvp_op_bit_not;
    if (strcmp(name, "mvp_op_not") == 0) return (void*)mvp_op_not;
    if (strcmp(name, "mvp_op_typeof") == 0) return (void*)mvp_op_typeof;
    if (strcmp(name, "mvp_op_truthy") == 0) return (void*)mvp_op_truthy;
    if (strcmp(name, "mvp_op_throw") == 0) return (void*)mvp_op_throw;
    if (strcmp(name, "mvp_host_hrtime_bigint") == 0) return (void*)mvp_host_hrtime_bigint;
    if (strcmp(name, "mvp_host_performance_now") == 0) return (void*)mvp_host_performance_now;
    if (strcmp(name, "mvp_host_math_random") == 0) return (void*)mvp_host_math_random;
    if (strcmp(name, "mvp_host_stdout_write") == 0) return (void*)mvp_host_stdout_write;
    if (strcmp(name, "mvp_host_console_log") == 0) return (void*)mvp_host_console_log;
    if (strcmp(name, "mvp_host_number") == 0) return (void*)mvp_host_number;
    if (strcmp(name, "mvp_host_bigint") == 0) return (void*)mvp_host_bigint;
    if (strcmp(name, "mvp_host_string") == 0) return (void*)mvp_host_string;
    if (strcmp(name, "mvp_host_parse_int") == 0) return (void*)mvp_host_parse_int;
    if (strcmp(name, "mvp_host_parse_float") == 0) return (void*)mvp_host_parse_float;
    if (strcmp(name, "mvp_host_json_parse") == 0) return (void*)mvp_host_json_parse;
    if (strcmp(name, "mvp_host_json_stringify") == 0) return (void*)mvp_host_json_stringify;
    if (strcmp(name, "mvp_host_require") == 0) return (void*)mvp_host_require;
    if (strcmp(name, "mvp_host_argv_get") == 0) return (void*)mvp_host_argv_get;
    if (strcmp(name, "mvp_host_string_from_char_code") == 0) {
        return (void*)mvp_host_string_from_char_code;
    }
    if (strcmp(name, "mvp_host_array_is_array") == 0) return (void*)mvp_host_array_is_array;
    if (strcmp(name, "mvp_host_is_nan") == 0) return (void*)mvp_host_is_nan;
    if (strcmp(name, "mvp_host_number_is_nan") == 0) return (void*)mvp_host_number_is_nan;
    if (strcmp(name, "mvp_host_object_define_property") == 0) {
        return (void*)mvp_host_object_define_property;
    }
    if (strcmp(name, "mvp_host_object_get_prototype") == 0) {
        return (void*)mvp_host_object_get_prototype;
    }
    if (strcmp(name, "mvp_host_object_to_string") == 0) {
        return (void*)mvp_host_object_to_string;
    }
    if (strcmp(name, "mvp_host_object_has_own") == 0) {
        return (void*)mvp_host_object_has_own;
    }
    if (strcmp(name, "mvp_host_array_new") == 0) return (void*)mvp_host_array_new;
    if (strcmp(name, "mvp_host_array_empty") == 0) return (void*)mvp_host_array_empty;
    if (strcmp(name, "mvp_host_array_one") == 0) return (void*)mvp_host_array_one;
    if (strcmp(name, "mvp_host_typed_array_new") == 0) return (void*)mvp_host_typed_array_new;
    if (strcmp(name, "mvp_host_object_new") == 0) return (void*)mvp_host_object_new;
    if (strcmp(name, "mvp_host_accessor_new") == 0) return (void*)mvp_host_accessor_new;
    if (strcmp(name, "mvp_host_environment_new") == 0) {
        return (void*)mvp_host_environment_new;
    }
    if (strcmp(name, "mvp_host_map_new") == 0) return (void*)mvp_host_map_new;
    if (strcmp(name, "mvp_host_error_new") == 0) return (void*)mvp_host_error_new;
    if (strcmp(name, "mvp_host_date_new") == 0) return (void*)mvp_host_date_new;
    if (strcmp(name, "mvp_host_date_now") == 0) return (void*)mvp_host_date_now;
    if (strcmp(name, "mvp_host_regexp_new") == 0) return (void*)mvp_host_regexp_new;
    if (strcmp(name, "mvp_host_regexp_test_bind") == 0) {
        return (void*)mvp_host_regexp_test_bind;
    }
    if (strcmp(name, "mvp_host_numeric_call0") == 0) return (void*)mvp_host_numeric_call0;
    if (strcmp(name, "mvp_host_numeric_call1") == 0) return (void*)mvp_host_numeric_call1;
    if (strcmp(name, "mvp_host_numeric_call2") == 0) return (void*)mvp_host_numeric_call2;
    if (strcmp(name, "mvp_host_numeric_call3") == 0) return (void*)mvp_host_numeric_call3;
    if (strcmp(name, "mvp_host_numeric_call4") == 0) return (void*)mvp_host_numeric_call4;
    if (strcmp(name, "mvp_host_numeric_call5") == 0) return (void*)mvp_host_numeric_call5;
    if (strcmp(name, "mvp_host_numeric_call6") == 0) return (void*)mvp_host_numeric_call6;
    if (strcmp(name, "mvp_host_numeric_call7") == 0) return (void*)mvp_host_numeric_call7;
    if (strcmp(name, "mvp_host_numeric_call8") == 0) return (void*)mvp_host_numeric_call8;
    if (strcmp(name, "mvp_host_numeric_function") == 0) {
        return (void*)mvp_host_numeric_function;
    }
    if (strcmp(name, "mvp_host_math_sin") == 0) return (void*)mvp_host_math_sin;
    if (strcmp(name, "mvp_host_math_cos") == 0) return (void*)mvp_host_math_cos;
    if (strcmp(name, "mvp_host_math_sqrt") == 0) return (void*)mvp_host_math_sqrt;
    if (strcmp(name, "mvp_host_math_abs") == 0) return (void*)mvp_host_math_abs;
    if (strcmp(name, "mvp_host_math_max") == 0) return (void*)mvp_host_math_max;
    if (strcmp(name, "mvp_host_math_max3") == 0) return (void*)mvp_host_math_max3;
    if (strcmp(name, "mvp_host_math_min") == 0) return (void*)mvp_host_math_min;
    if (strcmp(name, "mvp_host_math_floor") == 0) return (void*)mvp_host_math_floor;
    if (strcmp(name, "mvp_host_math_ceil") == 0) return (void*)mvp_host_math_ceil;
    if (strcmp(name, "mvp_host_math_trunc") == 0) return (void*)mvp_host_math_trunc;
    if (strcmp(name, "mvp_host_math_round") == 0) return (void*)mvp_host_math_round;
    if (strcmp(name, "mvp_host_jit_function") == 0) return (void*)mvp_host_jit_function;
    if (strcmp(name, "mvp_host_jit_closure") == 0) return (void*)mvp_host_jit_closure;
    if (strcmp(name, "mvp_host_static_literal") == 0) return (void*)mvp_host_static_literal;
    if (strcmp(name, "mvp_host_class_new") == 0) return (void*)mvp_host_class_new;
    if (strcmp(name, "mvp_host_class_define_method") == 0) return (void*)mvp_host_class_define_method;
    if (strcmp(name, "mvp_host_class_define_static") == 0) return (void*)mvp_host_class_define_static;
    if (strcmp(name, "mvp_op_member_get") == 0) return (void*)mvp_op_member_get;
    if (strcmp(name, "mvp_op_named_member_get") == 0) return (void*)mvp_op_named_member_get;
    if (strcmp(name, "mvp_op_named_member_get_hashed") == 0) {
        return (void*)mvp_op_named_member_get_hashed;
    }
    if (strcmp(name, "mvp_op_object_named_own_get_hashed") == 0) {
        return (void*)mvp_op_object_named_own_get_hashed;
    }
    if (strcmp(name, "mvp_op_named_member_set") == 0) return (void*)mvp_op_named_member_set;
    if (strcmp(name, "mvp_op_named_member_set_hashed") == 0) {
        return (void*)mvp_op_named_member_set_hashed;
    }
    if (strcmp(name, "mvp_op_optional_member_get") == 0) {
        return (void*)mvp_op_optional_member_get;
    }
    if (strcmp(name, "mvp_op_optional_named_member_get") == 0) {
        return (void*)mvp_op_optional_named_member_get;
    }
    if (strcmp(name, "mvp_op_optional_named_member_get_hashed") == 0) {
        return (void*)mvp_op_optional_named_member_get_hashed;
    }
    if (strcmp(name, "mvp_op_member_set") == 0) return (void*)mvp_op_member_set;
    if (strcmp(name, "mvp_op_enumerable_keys") == 0) return (void*)mvp_op_enumerable_keys;
    if (strcmp(name, "mvp_op_array_fill") == 0) return (void*)mvp_op_array_fill;
    if (strcmp(name, "mvp_op_array_push") == 0) return (void*)mvp_op_array_push;
    if (strcmp(name, "mvp_op_array_spread") == 0) return (void*)mvp_op_array_spread;
    if (strcmp(name, "mvp_op_construct0") == 0) return (void*)mvp_op_construct0;
    if (strcmp(name, "mvp_op_construct1") == 0) return (void*)mvp_op_construct1;
    if (strcmp(name, "mvp_op_construct2") == 0) return (void*)mvp_op_construct2;
    if (strcmp(name, "mvp_op_construct3") == 0) return (void*)mvp_op_construct3;
    if (strcmp(name, "mvp_op_construct4") == 0) return (void*)mvp_op_construct4;
    if (strcmp(name, "mvp_op_construct5") == 0) return (void*)mvp_op_construct5;
    if (strcmp(name, "mvp_op_construct6") == 0) return (void*)mvp_op_construct6;
    if (strcmp(name, "mvp_op_construct7") == 0) return (void*)mvp_op_construct7;
    if (strcmp(name, "mvp_op_construct8") == 0) return (void*)mvp_op_construct8;
    if (strcmp(name, "mvp_op_call0") == 0) return (void*)mvp_op_call0;
    if (strcmp(name, "mvp_op_call1") == 0) return (void*)mvp_op_call1;
    if (strcmp(name, "mvp_op_call2") == 0) return (void*)mvp_op_call2;
    if (strcmp(name, "mvp_op_call3") == 0) return (void*)mvp_op_call3;
    if (strcmp(name, "mvp_op_call4") == 0) return (void*)mvp_op_call4;
    if (strcmp(name, "mvp_op_call5") == 0) return (void*)mvp_op_call5;
    if (strcmp(name, "mvp_op_call6") == 0) return (void*)mvp_op_call6;
    if (strcmp(name, "mvp_op_call7") == 0) return (void*)mvp_op_call7;
    if (strcmp(name, "mvp_op_call8") == 0) return (void*)mvp_op_call8;
    if (strcmp(name, "mvp_op_call9") == 0) return (void*)mvp_op_call9;
    if (strcmp(name, "mvp_op_super0") == 0) return (void*)mvp_op_super0;
    if (strcmp(name, "mvp_op_super1") == 0) return (void*)mvp_op_super1;
    if (strcmp(name, "mvp_op_super2") == 0) return (void*)mvp_op_super2;
    if (strcmp(name, "mvp_op_super3") == 0) return (void*)mvp_op_super3;
    if (strcmp(name, "mvp_op_super4") == 0) return (void*)mvp_op_super4;
    if (strcmp(name, "mvp_op_super5") == 0) return (void*)mvp_op_super5;
    if (strcmp(name, "mvp_op_super6") == 0) return (void*)mvp_op_super6;
    if (strcmp(name, "mvp_op_super7") == 0) return (void*)mvp_op_super7;
    if (strcmp(name, "mvp_op_super8") == 0) return (void*)mvp_op_super8;
    return NULL;
}

static int mvp_generic_string_equal(const String* left, const String* right) {
    return left && right && left->len == right->len &&
        memcmp(left->chars, right->chars, left->len) == 0;
}

static MIR_reg_t mvp_generic_new_register(MvpGenericState* state, const char* prefix) {
    char name[48];
    int written = snprintf(name, sizeof(name), "%s_%u", prefix,
        state->compiler->next_register++);
    if (written <= 0 || (size_t)written >= sizeof(name)) return 0;
    return MIR_new_func_reg(state->compiler->context, state->function, MIR_T_I64, name);
}

static void mvp_generic_emit(MvpGenericState* state, MIR_insn_t instruction) {
    MIR_append_insn(state->compiler->context, state->function_item, instruction);
}

static int mvp_generic_bind(MvpGenericState* state, String* name, MIR_reg_t reg) {
    if (!state || !name || (!reg && !state->environment)) return 0;
    for (size_t index = 0; index < state->binding_count; index++) {
        if (mvp_generic_string_equal(state->bindings[index].name, name)) {
            state->bindings[index].reg = reg;
            state->bindings[index].stored_in_environment = state->environment != 0;
            return 1;
        }
    }
    if (state->binding_count >= sizeof(state->bindings) / sizeof(state->bindings[0])) return 0;
    state->bindings[state->binding_count++] = (MvpGenericBinding){name, reg,
        state->environment != 0};
    return 1;
}

static MvpGenericBinding* mvp_generic_lookup_binding(MvpGenericState* state, String* name) {
    if (!state || !name) return 0;
    for (size_t index = state->binding_count; index > 0; index--) {
        MvpGenericBinding* binding = &state->bindings[index - 1];
        if (mvp_generic_string_equal(binding->name, name)) return binding;
    }
    return NULL;
}

static int mvp_generic_has_global_name(const MvpGenericCompiler* compiler, String* name) {
    if (!compiler || !name) return 0;
    for (size_t index = 0; index < compiler->global_count; index++) {
        MvpGenericGlobal global = compiler->globals[index];
        if (mvp_generic_string_equal(global.name, name)) return 1;
    }
    return 0;
}

static int mvp_generic_is_class_name(const MvpGenericCompiler* compiler, String* name) {
    if (!compiler || !compiler->script || !name) return 0;
    for (AstNode* node = compiler->script->body; node; node = node->next) {
        if (node->node_type == AST_NODE_CLASS &&
                mvp_generic_string_equal(((AstClassNode*)node)->name, name)) return 1;
    }
    return 0;
}

static int mvp_generic_is_top_level_function_name(const MvpGenericCompiler* compiler,
        String* name) {
    if (!compiler || !compiler->script || !name) return 0;
    for (AstNode* node = compiler->script->body; node; node = node->next) {
        if (node->node_type == AST_NODE_FUNC &&
                mvp_generic_string_equal(((AstFuncNode*)node)->name, name)) return 1;
    }
    return 0;
}

static int mvp_generic_is_global_name(const MvpGenericCompiler* compiler, String* name) {
    return mvp_generic_has_global_name(compiler, name) ||
        mvp_generic_is_class_name(compiler, name) ||
        mvp_generic_is_top_level_function_name(compiler, name);
}

static int mvp_generic_collect_globals(MvpGenericCompiler* compiler, AstScript* script) {
    if (!compiler || !script) return 0;
    for (AstNode* statement = script->body; statement; statement = statement->next) {
        if (statement->node_type != AST_NODE_VAR_STAM) continue;
        AstVarDeclNode* declaration = (AstVarDeclNode*)statement;
        for (AstNode* item = declaration->declarations; item; item = item->next) {
            if (item->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                    compiler->global_count >= sizeof(compiler->globals) /
                        sizeof(compiler->globals[0])) return 0;
            AstDeclaratorNode* declarator = (AstDeclaratorNode*)item;
            if (!declarator->id || declarator->id->node_type != AST_NODE_IDENT) return 0;
            compiler->globals[compiler->global_count++] = (MvpGenericGlobal){
                ((AstIdentNode*)declarator->id)->name, declarator->init};
        }
    }
    return 1;
}

static int mvp_generic_emit_error_guard(MvpGenericState* state);

static int mvp_generic_import_defers_error_exit(const char* name) {
    // These pure value operations report any exceptional state through the
    // execution object. The enclosing entry checks it before exposing a
    // completion, so their hot paths do not need an eager branch each time.
    static const char* names[] = {
        "mvp_execution_has_error", "mvp_execution_set_root", "mvp_execution_leave_frame",
        "mvp_op_add", "mvp_op_sub", "mvp_op_mul", "mvp_op_div", "mvp_op_mod",
        "mvp_op_bit_and", "mvp_op_bit_or", "mvp_op_bit_xor", "mvp_op_bit_lshift",
        "mvp_op_bit_urshift", "mvp_op_neg", "mvp_op_equal", "mvp_op_not_equal",
        "mvp_op_strict_equal", "mvp_op_strict_not_equal", "mvp_op_less_than",
        "mvp_op_less_equal", "mvp_op_greater_than", "mvp_op_greater_equal", "mvp_op_in",
        "mvp_op_bit_rshift", "mvp_op_bit_not", "mvp_op_not", "mvp_op_typeof",
        "mvp_op_truthy", "mvp_op_object_named_own_get_hashed",
    };
    for (size_t index = 0; name && index < sizeof(names) / sizeof(names[0]); index++) {
        if (strcmp(name, names[index]) == 0) return 1;
    }
    return 0;
}

static int mvp_generic_call_import(MvpGenericState* state, const char* name,
        MIR_type_t result_type, MIR_reg_t result, size_t argument_count,
        MIR_var_t* arguments, MIR_op_t* operands) {
    char prototype_name[64];
    int written = snprintf(prototype_name, sizeof(prototype_name), "mvp_i_%u",
        state->compiler->next_symbol++);
    if (written <= 0 || (size_t)written >= sizeof(prototype_name)) return 0;
    MIR_type_t result_types[1] = {result_type};
    MIR_item_t prototype = MIR_new_proto_arr(state->compiler->context, prototype_name,
        result ? 1 : 0, result ? result_types : NULL, argument_count, arguments);
    MIR_item_t import = MIR_new_import(state->compiler->context, name);
    if (!prototype || !import) return 0;
    MIR_op_t call_operands[68] = {};
    if (argument_count + (result ? 3 : 2) > sizeof(call_operands) / sizeof(call_operands[0])) return 0;
    size_t offset = 0;
    call_operands[offset++] = MIR_new_ref_op(state->compiler->context, prototype);
    call_operands[offset++] = MIR_new_ref_op(state->compiler->context, import);
    if (result) call_operands[offset++] = MIR_new_reg_op(state->compiler->context, result);
    for (size_t index = 0; index < argument_count; index++) call_operands[offset++] = operands[index];
    mvp_generic_emit(state, MIR_new_insn_arr(state->compiler->context, MIR_CALL,
        offset, call_operands));
    if (mvp_generic_import_defers_error_exit(name)) {
        return 1;
    }
    return mvp_generic_emit_error_guard(state);
}

static int mvp_generic_emit_void_call(MvpGenericState* state, const char* name,
        size_t argument_count, MIR_var_t* arguments, MIR_op_t* operands) {
    return mvp_generic_call_import(state, name, MIR_T_UNDEF, 0, argument_count,
        arguments, operands);
}

static int mvp_generic_emit_error_guard(MvpGenericState* state) {
    if (!state || !state->error_exit) return 0;
    MIR_reg_t failed = mvp_generic_new_register(state, "failed");
    if (!failed) return 0;
    // The generic compiler owns this private execution ABI.  Read its failure
    // byte directly so a semantic helper has one call boundary, not a second
    // imported poll after every operation.
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, failed),
        MIR_new_mem_op(state->compiler->context, MIR_T_U8,
            (MIR_disp_t)offsetof(MvpExecution, error), state->execution, 0, 1)));
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BT,
        MIR_new_label_op(state->compiler->context, state->error_exit),
        MIR_new_reg_op(state->compiler->context, failed)));
    return 1;
}

static int mvp_generic_pin(MvpGenericState* state, MIR_reg_t value) {
    if (!state || !value || !state->root_values) return 0;
    if (state->root_count >= state->root_budget) {
        mvp_generic_set_failure("function root slots", state->root_budget);
        return 0;
    }
    MIR_disp_t offset = (MIR_disp_t)(state->root_count++ * sizeof(MvpValue));
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_mem_op(state->compiler->context, MIR_T_I64, offset,
            state->root_values, 0, 1),
        MIR_new_reg_op(state->compiler->context, value)));
    return 1;
}

static MIR_reg_t mvp_generic_emit_constant(MvpGenericState* state, MvpValue value,
        const char* prefix, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, prefix);
    if (!result) {
        *ok = 0;
        return 0;
    }
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_int_op(state->compiler->context, (int64_t)value.bits)));
    if (!mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_expression(MvpGenericState* state,
        AstNode* node, int* ok);
static MIR_reg_t mvp_generic_emit_runtime_binary(MvpGenericState* state,
        const char* name, MIR_reg_t left, MIR_reg_t right, int* ok);
static MIR_reg_t mvp_generic_emit_member_key(MvpGenericState* state,
        AstFieldNode* member, int* ok);
static MIR_reg_t mvp_generic_emit_named_member_get(MvpGenericState* state,
        MIR_reg_t object, String* name, int optional, int own_data, int* ok);
static MIR_reg_t mvp_generic_emit_string(MvpGenericState* state, String* string,
        int* ok);
static MIR_reg_t mvp_generic_emit_host_noargs(MvpGenericState* state, const char* name,
        int* ok);
static MvpGenericFunction* mvp_generic_find_function_ast(MvpGenericCompiler* compiler,
        AstFuncNode* ast);
static MvpNumericProgram* mvp_generic_find_numeric_program(MvpGenericCompiler* compiler,
        AstFuncNode* function);
static MIR_reg_t mvp_generic_emit_binding_write(MvpGenericState* state,
        MvpGenericBinding* binding, MIR_reg_t value, int* ok);
static MIR_reg_t mvp_generic_emit_member_set(MvpGenericState* state, MIR_reg_t object,
        MIR_reg_t key, MIR_reg_t value, int* ok);
static MIR_reg_t mvp_generic_emit_named_member_set(MvpGenericState* state,
        MIR_reg_t object, String* name, MIR_reg_t value, int* ok);
static int mvp_generic_assign_pattern(MvpGenericState* state, AstNode* pattern,
        MIR_reg_t value, int* ok);
static MIR_reg_t mvp_generic_emit_argument_array(MvpGenericState* state,
        AstNode* arguments, int* ok);

static MIR_reg_t mvp_generic_emit_global_get(MvpGenericState* state, String* name,
        int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "global");
    MIR_var_t arguments[3] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "address", 0},
        {MIR_T_I64, "length", 0}};
    MIR_op_t operands[3] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)(name ? name->chars : NULL)),
        MIR_new_int_op(state->compiler->context, (int64_t)(name ? name->len : 0)),
    };
    if (!name || !result || !mvp_generic_call_import(state, "mvp_execution_global_get",
            MIR_T_I64, result, 3, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MvpNumericProgram* mvp_generic_find_numeric_program(MvpGenericCompiler* compiler,
        AstFuncNode* function) {
    if (!compiler || !function) return NULL;
    for (size_t index = 0; index < compiler->numeric_program_count; index++) {
        if (mvp_numeric_program_matches(compiler->numeric_programs[index], function)) {
            return compiler->numeric_programs[index];
        }
    }
    return NULL;
}

static int mvp_generic_emit_global_set(MvpGenericState* state, String* name,
        MIR_reg_t value) {
    MIR_var_t arguments[4] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "address", 0},
        {MIR_T_I64, "length", 0}, {MIR_T_I64, "value", 0}};
    MIR_op_t operands[4] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)(name ? name->chars : NULL)),
        MIR_new_int_op(state->compiler->context, (int64_t)(name ? name->len : 0)),
        MIR_new_reg_op(state->compiler->context, value),
    };
    return name && value && mvp_generic_emit_void_call(state, "mvp_execution_global_set", 4,
        arguments, operands);
}

static MIR_reg_t mvp_generic_emit_dynamic_call(MvpGenericState* state,
        AstCallNode* call, int* ok) {
    MIR_reg_t receiver = 0;
    MIR_reg_t function = 0;
    if (call->callee && call->callee->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* member = (AstFieldNode*)call->callee;
        receiver = mvp_generic_emit_expression(state, member->object, ok);
        if (!*ok || !receiver) return 0;
        if (!member->computed && member->field && member->field->node_type == AST_NODE_IDENT) {
            function = mvp_generic_emit_named_member_get(state, receiver,
                ((AstIdentNode*)member->field)->name, member->optional, 0, ok);
        } else {
            MIR_reg_t key = mvp_generic_emit_member_key(state, member, ok);
            function = !*ok || !key ? 0 : mvp_generic_emit_runtime_binary(state,
                member->optional ? "mvp_op_optional_member_get" : "mvp_op_member_get",
                receiver, key, ok);
        }
    } else {
        receiver = mvp_generic_emit_constant(state, mvp_value_undefined(), "receiver", ok);
        function = mvp_generic_emit_expression(state, call ? call->callee : NULL, ok);
    }
    if (!*ok || !receiver || !function) {
        mvp_generic_set_failure("dynamic callee", function ? 1 : 0);
        return 0;
    }
    MIR_op_t operands[12] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, function),
        MIR_new_reg_op(state->compiler->context, receiver),
    };
    MIR_var_t arguments[12] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "function", 0},
        {MIR_T_I64, "receiver", 0}};
    size_t argument_count = 3;
    for (AstNode* argument = call->arguments; argument; argument = argument->next) {
        if (argument_count >= sizeof(arguments) / sizeof(arguments[0])) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
        if (!*ok || !value) {
            mvp_generic_set_failure("dynamic argument", argument_count - 3);
            return 0;
        }
        operands[argument_count] = MIR_new_reg_op(state->compiler->context, value);
        arguments[argument_count] = (MIR_var_t){MIR_T_I64, "argument", 0};
        argument_count++;
    }
    char helper[24] = {};
    int written = snprintf(helper, sizeof(helper), "mvp_op_call%zu", argument_count - 3);
    MIR_reg_t result = mvp_generic_new_register(state, "dynamic_call");
    if (written <= 0 || (size_t)written >= sizeof(helper) || !result ||
            !mvp_generic_call_import(state, helper, MIR_T_I64, result, argument_count,
                arguments, operands) || !mvp_generic_pin(state, result)) {
        mvp_generic_set_failure("dynamic call", argument_count - 3);
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_numeric_call(MvpGenericState* state, AstCallNode* call,
        MvpNumericProgram* program, int* ok) {
    size_t parameter_count = mvp_numeric_program_parameter_count(program);
    if (!program || !mvp_numeric_program_entry(program) || parameter_count > 8) {
        *ok = 0;
        return 0;
    }
    MIR_op_t operands[10] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_int_op(state->compiler->context,
            (int64_t)(uintptr_t)mvp_numeric_program_entry(program)),
    };
    MIR_var_t arguments[10] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "entry", 0}};
    size_t argument_count = 0;
    for (AstNode* argument = call ? call->arguments : NULL; argument; argument = argument->next) {
        if (argument->node_type == AST_NODE_SPREAD || argument_count >= parameter_count) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
        if (!*ok || !value) return 0;
        operands[argument_count + 2] = MIR_new_reg_op(state->compiler->context, value);
        arguments[argument_count + 2] = (MIR_var_t){MIR_T_I64, "argument", 0};
        argument_count++;
    }
    if (argument_count != parameter_count) {
        *ok = 0;
        return 0;
    }
    char helper[32] = {};
    int written = snprintf(helper, sizeof(helper), "mvp_host_numeric_call%zu", parameter_count);
    MIR_reg_t result = mvp_generic_new_register(state, "numeric_call");
    if (written <= 0 || (size_t)written >= sizeof(helper) || !result ||
            !mvp_generic_call_import(state, helper, MIR_T_I64, result, parameter_count + 2,
                arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_super_call(MvpGenericState* state, AstCallNode* call,
        int* ok) {
    MIR_op_t operands[10] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, state->receiver),
    };
    MIR_var_t arguments[10] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "receiver", 0}};
    size_t argument_count = 2;
    for (AstNode* argument = call->arguments; argument; argument = argument->next) {
        if (argument_count >= sizeof(arguments) / sizeof(arguments[0])) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
        if (!*ok || !value) return 0;
        operands[argument_count] = MIR_new_reg_op(state->compiler->context, value);
        arguments[argument_count] = (MIR_var_t){MIR_T_I64, "argument", 0};
        argument_count++;
    }
    char helper[24] = {};
    int written = snprintf(helper, sizeof(helper), "mvp_op_super%zu", argument_count - 2);
    MIR_reg_t result = mvp_generic_new_register(state, "super_call");
    if (written <= 0 || (size_t)written >= sizeof(helper) || !result ||
            !mvp_generic_call_import(state, helper, MIR_T_I64, result, argument_count,
                arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_descriptor(MvpGenericState* state,
        MvpJitFunctionDescriptor* descriptor, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "descriptor");
    if (!result || !descriptor) {
        *ok = 0;
        return 0;
    }
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)descriptor)));
    return result;
}

static MIR_reg_t mvp_generic_emit_function_value(MvpGenericState* state,
        MvpGenericFunction* function, int* ok) {
    MvpNumericProgram* numeric = function && !function->is_closure
        ? mvp_generic_find_numeric_program(state->compiler, function->ast) : NULL;
    if (numeric) {
        MIR_reg_t callable = mvp_generic_new_register(state, "numeric_function");
        MIR_var_t arguments[3] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "entry", 0},
            {MIR_T_I64, "parameter_count", 0}};
        MIR_op_t operands[3] = {
            MIR_new_reg_op(state->compiler->context, state->execution),
            MIR_new_int_op(state->compiler->context,
                (int64_t)(uintptr_t)mvp_numeric_program_entry(numeric)),
            MIR_new_int_op(state->compiler->context,
                (int64_t)mvp_numeric_program_parameter_count(numeric)),
        };
        if (!*ok || !callable || !mvp_generic_call_import(state,
                "mvp_host_numeric_function", MIR_T_I64, callable, 3, arguments, operands) ||
                !mvp_generic_pin(state, callable)) {
            *ok = 0;
            return 0;
        }
        return callable;
    }
    MIR_reg_t descriptor = mvp_generic_emit_descriptor(state,
        function ? &function->descriptor : NULL, ok);
    MIR_reg_t callable = mvp_generic_new_register(state, "function");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "descriptor", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, descriptor),
    };
    if (!*ok || !descriptor || !callable || !mvp_generic_call_import(state,
            "mvp_host_jit_function", MIR_T_I64, callable, 2, arguments, operands) ||
            !mvp_generic_pin(state, callable)) {
        *ok = 0;
        return 0;
    }
    return callable;
}

static MIR_reg_t mvp_generic_emit_function_closure(MvpGenericState* state,
        MvpGenericFunction* target, int* ok) {
    MIR_reg_t captures = state && state->environment ? state->environment
        : state && state->is_closure ? state->capture_environment : 0;
    if (!target || !target->is_closure || !captures) {
        *ok = 0;
        return 0;
    }
    MIR_reg_t descriptor = mvp_generic_emit_descriptor(state, &target->descriptor, ok);
    MIR_reg_t closure = mvp_generic_new_register(state, "closure");
    MIR_var_t arguments[3] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "descriptor", 0},
        {MIR_T_I64, "captures", 0}};
    MIR_op_t operands[3] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, descriptor),
        MIR_new_reg_op(state->compiler->context, captures),
    };
    if (!*ok || !descriptor || !closure || !mvp_generic_call_import(state,
            "mvp_host_jit_closure", MIR_T_I64, closure, 3, arguments, operands) ||
            !mvp_generic_pin(state, closure)) {
        *ok = 0;
        return 0;
    }
    return closure;
}

static MIR_reg_t mvp_generic_emit_closure(MvpGenericState* state, AstFuncNode* function,
        int* ok) {
    MvpGenericFunction* target = mvp_generic_find_function_ast(state->compiler, function);
    return mvp_generic_emit_function_closure(state, target, ok);
}

static MIR_reg_t mvp_generic_emit_class(MvpGenericState* state, AstClassNode* class_node,
        int* ok) {
    if (!class_node || !class_node->name || !class_node->body ||
            class_node->body->node_type != AST_NODE_BLOCK) {
        *ok = 0;
        return 0;
    }
    MIR_reg_t parent = class_node->superclass ? mvp_generic_emit_expression(state,
        class_node->superclass, ok) : mvp_generic_emit_constant(state,
        mvp_value_undefined(), "parent", ok);
    MIR_reg_t class_value = mvp_generic_new_register(state, "class");
    MIR_var_t class_arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "parent", 0}};
    MIR_op_t class_operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, parent),
    };
    if (!*ok || !parent || !class_value || !mvp_generic_call_import(state,
            "mvp_host_class_new", MIR_T_I64, class_value, 2, class_arguments,
            class_operands) || !mvp_generic_pin(state, class_value) ||
            !mvp_generic_bind(state, class_node->name, class_value)) {
        *ok = 0;
        return 0;
    }
    MvpGenericBinding* binding = mvp_generic_lookup_binding(state, class_node->name);
    if (!binding || !mvp_generic_emit_binding_write(state, binding, class_value, ok) || !*ok ||
            !mvp_generic_emit_global_set(state, class_node->name, class_value)) {
        *ok = 0;
        return 0;
    }
    AstBlockNode* body = (AstBlockNode*)class_node->body;
    for (AstNode* member = body->statements; member; member = member->next) {
        if (member->node_type == AST_NODE_FIELD) {
            AstClassFieldNode* field = (AstClassFieldNode*)member;
            if (!field->is_static || !field->key || field->key->node_type != AST_NODE_IDENT) {
                mvp_generic_set_node_failure("class field", member);
                *ok = 0;
                return 0;
            }
            MIR_reg_t key = mvp_generic_emit_string(state, ((AstIdentNode*)field->key)->name,
                ok);
            MIR_reg_t value = field->value ? mvp_generic_emit_expression(state, field->value,
                ok) : mvp_generic_emit_constant(state, mvp_value_undefined(), "undefined", ok);
            MIR_reg_t stored = !*ok || !key || !value ? 0 : mvp_generic_emit_member_set(state,
                class_value, key, value, ok);
            if (!*ok || !stored) return 0;
            continue;
        }
        if (member->node_type != AST_NODE_METHOD) {
            mvp_generic_set_node_failure("class member", member);
            *ok = 0;
            return 0;
        }
        AstMethodNode* method = (AstMethodNode*)member;
        if (method->computed || !method->key ||
                method->key->node_type != AST_NODE_IDENT ||
                (method->kind != AstMethodNode::JS_METHOD_METHOD &&
                 method->kind != AstMethodNode::JS_METHOD_CONSTRUCTOR)) {
            mvp_generic_set_node_failure("class method", member);
            *ok = 0;
            return 0;
        }
        MvpGenericFunction* function = mvp_generic_find_function_ast(state->compiler,
            (AstFuncNode*)method);
        MIR_reg_t callable = mvp_generic_emit_function_value(state, function, ok);
        if (!*ok || !callable) return 0;
        MIR_reg_t key = mvp_generic_emit_string(state,
            ((AstIdentNode*)method->key)->name, ok);
        MIR_reg_t defined = mvp_generic_new_register(state, "defined_method");
        MIR_var_t define_arguments[4] = {{MIR_T_P, "execution", 0},
            {MIR_T_I64, "class", 0}, {MIR_T_I64, "key", 0},
            {MIR_T_I64, "method", 0}};
        MIR_op_t define_operands[4] = {
            MIR_new_reg_op(state->compiler->context, state->execution),
            MIR_new_reg_op(state->compiler->context, class_value),
            MIR_new_reg_op(state->compiler->context, key),
            MIR_new_reg_op(state->compiler->context, callable),
        };
        const char* define_name = method->static_method ? "mvp_host_class_define_static"
            : "mvp_host_class_define_method";
        if (!*ok || !key || !defined || !mvp_generic_call_import(state,
                define_name, MIR_T_I64, defined, 4, define_arguments,
                define_operands) || !mvp_generic_pin(state, defined)) {
            *ok = 0;
            return 0;
        }
    }
    return class_value;
}

static MIR_reg_t mvp_generic_emit_expression(MvpGenericState* state,
        AstNode* node, int* ok);
static MIR_reg_t mvp_generic_emit_runtime_binary(MvpGenericState* state,
        const char* name, MIR_reg_t left, MIR_reg_t right, int* ok);
static int mvp_generic_emit_truthy(MvpGenericState* state, MIR_reg_t value,
        MIR_reg_t* out_condition);

static MIR_reg_t mvp_generic_emit_bytes(MvpGenericState* state, const char* bytes,
        size_t byte_length, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "string");
    MIR_var_t arguments[3] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "address", 0},
        {MIR_T_I64, "length", 0}};
    MIR_op_t operands[3] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)bytes),
        MIR_new_int_op(state->compiler->context, (int64_t)byte_length),
    };
    if (!result || !mvp_generic_call_import(state, "mvp_op_literal_string",
            MIR_T_I64, result, 3, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_string(MvpGenericState* state, String* string,
        int* ok) {
    if (!string) {
        mvp_generic_set_failure("string literal", 0);
        *ok = 0;
        return 0;
    }
    return mvp_generic_emit_bytes(state, string->chars, string->len, ok);
}

static MvpGenericFunction* mvp_generic_find_function(MvpGenericCompiler* compiler,
        String* name) {
    if (!compiler || !name) return NULL;
    for (size_t index = 0; index < compiler->function_count; index++) {
        if (!compiler->functions[index].is_method &&
                mvp_generic_string_equal(compiler->functions[index].ast->name, name)) {
            return &compiler->functions[index];
        }
    }
    return NULL;
}

static MvpGenericFunction* mvp_generic_find_function_ast(MvpGenericCompiler* compiler,
        AstFuncNode* ast) {
    if (!compiler || !ast) return NULL;
    for (size_t index = 0; index < compiler->function_count; index++) {
        if (compiler->functions[index].ast == ast) return &compiler->functions[index];
    }
    return NULL;
}

static int mvp_generic_identifier_named(AstNode* node, const char* name);

static int mvp_generic_class_has_script_subclass(MvpGenericCompiler* compiler,
        AstClassNode* class_node) {
    if (!compiler || !compiler->script || !class_node || !class_node->name) return 1;
    for (AstNode* node = compiler->script->body; node; node = node->next) {
        if (node->node_type != AST_NODE_CLASS) continue;
        AstClassNode* candidate = (AstClassNode*)node;
        if (candidate != class_node && candidate->superclass &&
                mvp_generic_string_equal(candidate->superclass->node_type == AST_NODE_IDENT
                    ? ((AstIdentNode*)candidate->superclass)->name : NULL, class_node->name)) {
            return 1;
        }
    }
    return 0;
}

static MvpGenericFunction* mvp_generic_find_direct_self_method(MvpGenericState* state,
        AstCallNode* call) {
    // A script-local class without a subclass cannot use this call site for
    // virtual dispatch within the benchmark MVP feature set.
    if (!state || !state->owner_class || !call || !call->callee ||
            call->callee->node_type != AST_NODE_MEMBER_EXPR ||
            mvp_generic_class_has_script_subclass(state->compiler, state->owner_class)) {
        return NULL;
    }
    AstFieldNode* member = (AstFieldNode*)call->callee;
    if (member->computed || member->optional || !mvp_generic_identifier_named(member->object,
            "this") || !member->field || member->field->node_type != AST_NODE_IDENT) {
        return NULL;
    }
    String* name = ((AstIdentNode*)member->field)->name;
    for (size_t index = 0; index < state->compiler->function_count; index++) {
        MvpGenericFunction* candidate = &state->compiler->functions[index];
        AstMethodNode* method = candidate->is_method ? (AstMethodNode*)candidate->ast : NULL;
        if (candidate->owner_class == state->owner_class && candidate->is_method &&
                candidate->is_static_method == state->is_static_method && method &&
                method->kind == AstMethodNode::JS_METHOD_METHOD &&
                mvp_generic_string_equal(method->key && method->key->node_type == AST_NODE_IDENT
                    ? ((AstIdentNode*)method->key)->name : NULL, name)) {
            return candidate;
        }
    }
    return NULL;
}

static int mvp_generic_identifier_named(AstNode* node, const char* name) {
    if (!node || node->node_type != AST_NODE_IDENT || !name) return 0;
    String* value = ((AstIdentNode*)node)->name;
    size_t length = strlen(name);
    return value && value->len == length && memcmp(value->chars, name, length) == 0;
}

static int mvp_generic_member_named(AstNode* node, const char* object_name,
        const char* field_name) {
    if (!node || node->node_type != AST_NODE_MEMBER_EXPR) return 0;
    AstFieldNode* member = (AstFieldNode*)node;
    return !member->computed && mvp_generic_identifier_named(member->object, object_name) &&
        mvp_generic_identifier_named(member->field, field_name);
}

static int mvp_generic_regexp_test_bind(AstNode* callee) {
    AstFieldNode* bind = callee && callee->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)callee : NULL;
    AstFieldNode* test = bind && bind->object && bind->object->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)bind->object : NULL;
    AstFieldNode* prototype = test && test->object &&
            test->object->node_type == AST_NODE_MEMBER_EXPR ? (AstFieldNode*)test->object : NULL;
    return bind && test && prototype && !bind->computed && !test->computed &&
        !prototype->computed && mvp_generic_identifier_named(bind->field, "bind") &&
        mvp_generic_identifier_named(test->field, "test") &&
        mvp_generic_identifier_named(prototype->field, "prototype") &&
        mvp_generic_identifier_named(prototype->object, "RegExp");
}

static int mvp_generic_object_prototype_call(AstNode* callee, const char* method) {
    AstFieldNode* call = callee && callee->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)callee : NULL;
    AstFieldNode* member = call && call->object && call->object->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)call->object : NULL;
    AstFieldNode* prototype = member && member->object &&
            member->object->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)member->object : NULL;
    return call && member && prototype && !call->computed && !member->computed &&
        !prototype->computed && mvp_generic_identifier_named(call->field, "call") &&
        mvp_generic_identifier_named(member->field, method) &&
        mvp_generic_identifier_named(prototype->field, "prototype") &&
        mvp_generic_identifier_named(prototype->object, "Object");
}

static int mvp_generic_host_call_name(AstNode* callee, const char** out_name,
        int* out_argument_count) {
    if (mvp_generic_identifier_named(callee, "Number")) {
        *out_name = "mvp_host_number";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_identifier_named(callee, "String")) {
        *out_name = "mvp_host_string";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_identifier_named(callee, "parseInt")) {
        *out_name = "mvp_host_parse_int";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_identifier_named(callee, "parseFloat")) {
        *out_name = "mvp_host_parse_float";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_identifier_named(callee, "require")) {
        *out_name = "mvp_host_require";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_identifier_named(callee, "isNaN")) {
        *out_name = "mvp_host_is_nan";
        *out_argument_count = 1;
        return 1;
    }
    if (!callee || callee->node_type != AST_NODE_MEMBER_EXPR) return 0;
    AstFieldNode* member = (AstFieldNode*)callee;
    if (mvp_generic_member_named(member->object, "process", "hrtime") &&
            mvp_generic_identifier_named(member->field, "bigint")) {
        *out_name = "mvp_host_hrtime_bigint";
        *out_argument_count = 0;
        return 1;
    }
    if (mvp_generic_member_named(callee, "performance", "now")) {
        *out_name = "mvp_host_performance_now";
        *out_argument_count = 0;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "random")) {
        *out_name = "mvp_host_math_random";
        *out_argument_count = 0;
        return 1;
    }
    if (mvp_generic_member_named(member->object, "process", "stdout") &&
            mvp_generic_identifier_named(member->field, "write")) {
        *out_name = "mvp_host_stdout_write";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "console", "log")) {
        *out_name = "mvp_host_console_log";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "String", "fromCharCode")) {
        *out_name = "mvp_host_string_from_char_code";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Array", "isArray")) {
        *out_name = "mvp_host_array_is_array";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Object", "defineProperty")) {
        *out_name = "mvp_host_object_define_property";
        *out_argument_count = 3;
        return 1;
    }
    if (mvp_generic_member_named(callee, "JSON", "parse")) {
        *out_name = "mvp_host_json_parse";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "JSON", "stringify")) {
        *out_name = "mvp_host_json_stringify";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Object", "getPrototypeOf")) {
        *out_name = "mvp_host_object_get_prototype";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Number", "isNaN")) {
        *out_name = "mvp_host_number_is_nan";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "sin")) {
        *out_name = "mvp_host_math_sin";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "cos")) {
        *out_name = "mvp_host_math_cos";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "sqrt")) {
        *out_name = "mvp_host_math_sqrt";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "abs")) {
        *out_name = "mvp_host_math_abs";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "max")) {
        *out_name = "mvp_host_math_max";
        *out_argument_count = 2;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "min")) {
        *out_name = "mvp_host_math_min";
        *out_argument_count = 2;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "floor")) {
        *out_name = "mvp_host_math_floor";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "ceil")) {
        *out_name = "mvp_host_math_ceil";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "trunc")) {
        *out_name = "mvp_host_math_trunc";
        *out_argument_count = 1;
        return 1;
    }
    if (mvp_generic_member_named(callee, "Math", "round")) {
        *out_name = "mvp_host_math_round";
        *out_argument_count = 1;
        return 1;
    }
    return 0;
}

static MIR_reg_t mvp_generic_emit_host_call(MvpGenericState* state,
        AstCallNode* call, const char* name, int required_argument_count, int* ok) {
    MIR_op_t source_arguments[8] = {};
    size_t source_count = 0;
    for (AstNode* argument = call->arguments; argument; argument = argument->next) {
        if (source_count >= sizeof(source_arguments) / sizeof(source_arguments[0])) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
        if (!*ok || !value) return 0;
        source_arguments[source_count++] = MIR_new_reg_op(state->compiler->context, value);
    }
    if (source_count != (size_t)required_argument_count) {
        *ok = 0;
        return 0;
    }
    MIR_reg_t result = mvp_generic_new_register(state, "host");
    MIR_var_t arguments[9] = {{MIR_T_P, "execution", 0}};
    MIR_op_t operands[9] = {MIR_new_reg_op(state->compiler->context, state->execution)};
    for (size_t index = 0; index < source_count; index++) {
        arguments[index + 1] = (MIR_var_t){MIR_T_I64, "value", 0};
        operands[index + 1] = source_arguments[index];
    }
    size_t argument_count = source_count + 1;
    if (!result || !mvp_generic_call_import(state, name, MIR_T_I64, result,
            argument_count, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_host_one(MvpGenericState* state, const char* name,
        MIR_reg_t value, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "host");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "value", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, value),
    };
    if (!result || !mvp_generic_call_import(state, name, MIR_T_I64, result, 2,
            arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_array_fill(MvpGenericState* state, AstCallNode* call,
        int* ok) {
    AstFieldNode* member = (AstFieldNode*)call->callee;
    MIR_reg_t object = mvp_generic_emit_expression(state, member->object, ok);
    AstNode* argument = call->arguments;
    MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
    if (!*ok || !object || !value || !argument || argument->next) {
        *ok = 0;
        return 0;
    }
    return mvp_generic_emit_runtime_binary(state, "mvp_op_array_fill", object, value, ok);
}

static MIR_reg_t mvp_generic_emit_member_key(MvpGenericState* state,
        AstFieldNode* member, int* ok) {
    if (!member || !member->field) {
        mvp_generic_set_failure("member key", 0);
        *ok = 0;
        return 0;
    }
    if (member->computed) return mvp_generic_emit_expression(state, member->field, ok);
    if (member->field->node_type != AST_NODE_IDENT) {
        mvp_generic_set_node_failure("member key", member->field);
        *ok = 0;
        return 0;
    }
    return mvp_generic_emit_string(state, ((AstIdentNode*)member->field)->name, ok);
}

static MIR_reg_t mvp_generic_emit_named_member_get(MvpGenericState* state,
        MIR_reg_t object, String* name, int optional, int own_data, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "named_member");
    MIR_var_t arguments[5] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "object", 0},
        {MIR_T_I64, "key_address", 0}, {MIR_T_I64, "key_length", 0},
        {MIR_T_I64, "key_hash", 0}};
    MIR_op_t operands[5] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, object),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)(name ? name->chars : NULL)),
        MIR_new_int_op(state->compiler->context, (int64_t)(name ? name->len : 0)),
        MIR_new_int_op(state->compiler->context, (int64_t)mvp_string_hash_bytes(
            name ? name->chars : NULL, name ? name->len : 0)),
    };
    const char* helper = own_data ? "mvp_op_object_named_own_get_hashed"
        : optional ? "mvp_op_optional_named_member_get_hashed"
        : "mvp_op_named_member_get_hashed";
    if (!name || !object || !result || !mvp_generic_call_import(state, helper, MIR_T_I64,
            result, 5, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_member(MvpGenericState* state, AstFieldNode* member,
        int* ok) {
    if (mvp_generic_member_named((AstNode*)member, "Math", "PI")) {
        return mvp_generic_emit_constant(state, mvp_value_from_number(3.14159265358979323846),
            "math_pi", ok);
    }
    if (mvp_generic_member_named((AstNode*)member, "performance", "now")) {
        return mvp_generic_emit_constant(state, mvp_value_from_number(1.0),
            "performance_now", ok);
    }
    AstFieldNode* argv = member && member->computed && member->object &&
            member->object->node_type == AST_NODE_MEMBER_EXPR
        ? (AstFieldNode*)member->object : NULL;
    if (argv && mvp_generic_member_named((AstNode*)argv, "process", "argv")) {
        MIR_reg_t index = mvp_generic_emit_expression(state, member->field, ok);
        return !*ok || !index ? 0 : mvp_generic_emit_host_one(state, "mvp_host_argv_get",
            index, ok);
    }
    MIR_reg_t object = mvp_generic_emit_expression(state, member ? member->object : NULL, ok);
    if (!*ok || !object) return 0;
    if (!member->computed && member->field && member->field->node_type == AST_NODE_IDENT) {
        int own_data = !member->optional && state->owner_class && !state->is_static_method &&
            mvp_generic_identifier_named(member->object, "this");
        return mvp_generic_emit_named_member_get(state, object,
            ((AstIdentNode*)member->field)->name, member->optional, own_data, ok);
    }
    MIR_reg_t key = mvp_generic_emit_member_key(state, member, ok);
    if (!*ok || !key) return 0;
    return mvp_generic_emit_runtime_binary(state, member->optional
        ? "mvp_op_optional_member_get" : "mvp_op_member_get", object, key, ok);
}

static const char* mvp_generic_array_constructor_helper(AstNode* callee) {
    if (mvp_generic_identifier_named(callee, "Array")) return "mvp_host_array_new";
    if (mvp_generic_identifier_named(callee, "Int32Array") ||
            mvp_generic_identifier_named(callee, "Uint8Array") ||
            mvp_generic_identifier_named(callee, "Float64Array")) {
        return "mvp_host_typed_array_new";
    }
    return NULL;
}

static const char* mvp_generic_error_constructor_helper(AstNode* callee) {
    return mvp_generic_identifier_named(callee, "Error") ? "mvp_host_error_new" : NULL;
}

static const char* mvp_generic_object_constructor_helper(AstNode* callee) {
    if (mvp_generic_identifier_named(callee, "Date")) return "mvp_host_date_new";
    if (mvp_generic_identifier_named(callee, "RegExp")) return "mvp_host_regexp_new";
    return NULL;
}

static MIR_reg_t mvp_generic_emit_new(MvpGenericState* state, AstCallNode* call, int* ok) {
    if (call && mvp_generic_identifier_named(call->callee, "Array")) {
        if (!call->arguments) return mvp_generic_emit_host_noargs(state,
            "mvp_host_array_empty", ok);
        if (!call->arguments->next) return mvp_generic_emit_host_call(state, call,
            "mvp_host_array_one", 1, ok);
        return mvp_generic_emit_argument_array(state, call->arguments, ok);
    }
    if (call && mvp_generic_identifier_named(call->callee, "Object") && !call->arguments) {
        return mvp_generic_emit_host_noargs(state, "mvp_host_object_new", ok);
    }
    const char* helper = call ? mvp_generic_array_constructor_helper(call->callee) : NULL;
    if (helper) return mvp_generic_emit_host_call(state, call, helper, 1, ok);
    helper = call ? mvp_generic_error_constructor_helper(call->callee) : NULL;
    if (helper) return mvp_generic_emit_host_call(state, call, helper, 1, ok);
    if (call && mvp_generic_identifier_named(call->callee, "Map") && !call->arguments) {
        return mvp_generic_emit_host_call(state, call, "mvp_host_map_new", 0, ok);
    }
    if (call && mvp_generic_identifier_named(call->callee, "Date") && !call->arguments) {
        return mvp_generic_emit_host_noargs(state, "mvp_host_date_now", ok);
    }
    helper = call ? mvp_generic_object_constructor_helper(call->callee) : NULL;
    if (helper) return mvp_generic_emit_host_call(state, call, helper,
        strcmp(helper, "mvp_host_regexp_new") == 0 ? 2 : 1, ok);
    // Constructors are mutable values: a declaration's prototype and static
    // properties must be read from its evaluated binding before construction.
    MIR_reg_t function = mvp_generic_emit_expression(state, call ? call->callee : NULL, ok);
    if (!*ok || !function) return 0;
    MIR_op_t operands[10] = {MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, function)};
    MIR_var_t arguments[10] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "function", 0}};
    size_t argument_count = 2;
    for (AstNode* argument = call->arguments; argument; argument = argument->next) {
        if (argument_count >= sizeof(arguments) / sizeof(arguments[0])) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
        if (!*ok || !value) return 0;
        operands[argument_count] = MIR_new_reg_op(state->compiler->context, value);
        arguments[argument_count] = (MIR_var_t){MIR_T_I64, "argument", 0};
        argument_count++;
    }
    char construct_name[28] = {};
    int written = snprintf(construct_name, sizeof(construct_name), "mvp_op_construct%zu",
        argument_count - 2);
    MIR_reg_t result = mvp_generic_new_register(state, "instance");
    if (written <= 0 || (size_t)written >= sizeof(construct_name) || !result ||
            !mvp_generic_call_import(state, construct_name, MIR_T_I64, result,
                argument_count, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_host_noargs(MvpGenericState* state, const char* name,
        int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "host");
    MIR_var_t argument = {MIR_T_P, "execution", 0};
    MIR_op_t operand = MIR_new_reg_op(state->compiler->context, state->execution);
    if (!result || !mvp_generic_call_import(state, name, MIR_T_I64, result, 1,
            &argument, &operand) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_member_set(MvpGenericState* state, MIR_reg_t object,
        MIR_reg_t key, MIR_reg_t value, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "store");
    MIR_var_t arguments[4] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "object", 0},
        {MIR_T_I64, "key", 0}, {MIR_T_I64, "value", 0}};
    MIR_op_t operands[4] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, object),
        MIR_new_reg_op(state->compiler->context, key),
        MIR_new_reg_op(state->compiler->context, value),
    };
    if (!result || !mvp_generic_call_import(state, "mvp_op_member_set", MIR_T_I64,
            result, 4, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_named_member_set(MvpGenericState* state,
        MIR_reg_t object, String* name, MIR_reg_t value, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "named_store");
    MIR_var_t arguments[6] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "object", 0},
        {MIR_T_I64, "key_address", 0}, {MIR_T_I64, "key_length", 0},
        {MIR_T_I64, "key_hash", 0}, {MIR_T_I64, "value", 0}};
    MIR_op_t operands[6] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, object),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)(name ? name->chars : NULL)),
        MIR_new_int_op(state->compiler->context, (int64_t)(name ? name->len : 0)),
        MIR_new_int_op(state->compiler->context, (int64_t)mvp_string_hash_bytes(
            name ? name->chars : NULL, name ? name->len : 0)),
        MIR_new_reg_op(state->compiler->context, value),
    };
    if (!name || !object || !value || !result || !mvp_generic_call_import(state,
            "mvp_op_named_member_set_hashed", MIR_T_I64, result, 6, arguments, operands) ||
            !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_binding_read(MvpGenericState* state,
        MvpGenericBinding* binding, int* ok) {
    if (!binding) return 0;
    if (!binding->stored_in_environment) return binding->reg;
    MIR_reg_t key = mvp_generic_emit_string(state, binding->name, ok);
    if (!*ok || !key) return 0;
    return mvp_generic_emit_runtime_binary(state, "mvp_op_member_get", state->environment,
        key, ok);
}

static MIR_reg_t mvp_generic_emit_binding_write(MvpGenericState* state,
        MvpGenericBinding* binding, MIR_reg_t value, int* ok) {
    if (!binding || !value) {
        *ok = 0;
        return 0;
    }
    if (!binding->stored_in_environment) {
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
            MIR_new_reg_op(state->compiler->context, binding->reg),
            MIR_new_reg_op(state->compiler->context, value)));
        if (!mvp_generic_pin(state, binding->reg)) *ok = 0;
        return binding->reg;
    }
    MIR_reg_t key = mvp_generic_emit_string(state, binding->name, ok);
    if (!*ok || !key) return 0;
    return mvp_generic_emit_member_set(state, state->environment, key, value, ok);
}

static MIR_reg_t mvp_generic_emit_capture_read(MvpGenericState* state, String* name,
        int* ok) {
    if (!state || !state->is_closure || !state->capture_environment || !name) {
        if (!mvp_generic_failure[0]) {
            int written = snprintf(mvp_generic_failure, sizeof(mvp_generic_failure),
                "MVP generic MIR lowering cannot capture identifier %.*s from this scope",
                name ? (int)name->len : 7, name ? name->chars : "unknown");
            if (written <= 0 || (size_t)written >= sizeof(mvp_generic_failure)) {
                mvp_generic_failure[sizeof(mvp_generic_failure) - 1] = '\0';
            }
        }
        return 0;
    }
    MIR_reg_t key = mvp_generic_emit_string(state, name, ok);
    if (!*ok || !key) {
        mvp_generic_set_failure("closure capture key", state->root_count);
        return 0;
    }
    MIR_reg_t result = mvp_generic_emit_runtime_binary(state, "mvp_op_member_get",
        state->capture_environment, key, ok);
    if (!*ok || !result) mvp_generic_set_failure("closure capture value", state->root_count);
    return result;
}

static MIR_reg_t mvp_generic_emit_capture_write(MvpGenericState* state, String* name,
        MIR_reg_t value, int* ok) {
    if (!state || !state->is_closure || !state->capture_environment || !name || !value) {
        *ok = 0;
        return 0;
    }
    MIR_reg_t key = mvp_generic_emit_string(state, name, ok);
    if (!*ok || !key) return 0;
    return mvp_generic_emit_member_set(state, state->capture_environment, key, value, ok);
}

static MIR_reg_t mvp_generic_emit_environment_new(MvpGenericState* state, int* ok) {
    MIR_reg_t environment = mvp_generic_new_register(state, "environment");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "parent", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        state->is_closure ? MIR_new_reg_op(state->compiler->context,
            state->capture_environment) : MIR_new_int_op(state->compiler->context,
            (int64_t)mvp_value_null().bits),
    };
    if (!environment || !mvp_generic_call_import(state, "mvp_host_environment_new", MIR_T_I64,
            environment, 2, arguments, operands) || !mvp_generic_pin(state, environment)) {
        *ok = 0;
        return 0;
    }
    return environment;
}

static int mvp_generic_emit_environment_store(MvpGenericState* state, const char* name,
        MIR_reg_t value) {
    int ok = 1;
    MIR_reg_t key = mvp_generic_emit_bytes(state, name, strlen(name), &ok);
    MIR_reg_t stored = mvp_generic_emit_member_set(state, state->environment, key, value, &ok);
    return ok && key && stored;
}

static int mvp_generic_static_literal_supported(AstNode* node) {
    if (!node) return 0;
    if (node->node_type == AST_NODE_LITERAL) {
        return !((AstLiteralNode*)node)->is_bigint;
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        return (unary->op == OPERATOR_POS || unary->op == OPERATOR_NEG) && unary->operand &&
            unary->operand->node_type == AST_NODE_LITERAL &&
            ((AstLiteralNode*)unary->operand)->literal_type == AST_LITERAL_NUMBER;
    }
    if (node->node_type == AST_NODE_ARRAY) {
        for (AstNode* item = ((AstArrayNode*)node)->elements; item; item = item->next) {
            if (item->node_type == AST_NODE_SPREAD || !mvp_generic_static_literal_supported(item)) {
                return 0;
            }
        }
        return 1;
    }
    if (node->node_type != AST_NODE_MAP) return 0;
    for (AstNode* item = ((AstMapNode*)node)->properties; item; item = item->next) {
        if (item->node_type != AST_NODE_PROPERTY) return 0;
        AstPropertyNode* property = (AstPropertyNode*)item;
        if (property->computed || !property->key ||
                (property->key->node_type != AST_NODE_IDENT &&
                 property->key->node_type != AST_NODE_LITERAL) ||
                !mvp_generic_static_literal_supported(property->value)) {
            return 0;
        }
    }
    return 1;
}

static MIR_reg_t mvp_generic_emit_static_literal(MvpGenericState* state, AstNode* node,
        int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "static_literal");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "node", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_int_op(state->compiler->context, (int64_t)(uintptr_t)node),
    };
    if (!result || !mvp_generic_call_import(state, "mvp_host_static_literal", MIR_T_I64,
            result, 2, arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_object(MvpGenericState* state, AstMapNode* map,
        int* ok) {
    if (mvp_generic_static_literal_supported((AstNode*)map)) {
        return mvp_generic_emit_static_literal(state, (AstNode*)map, ok);
    }
    MIR_reg_t object = mvp_generic_emit_host_noargs(state, "mvp_host_object_new", ok);
    if (!*ok || !object) return 0;
    for (AstNode* item = map ? map->properties : NULL; item; item = item->next) {
        if (item->node_type != AST_NODE_PROPERTY) {
            mvp_generic_set_node_failure("object literal", item);
            *ok = 0;
            return 0;
        }
        AstPropertyNode* property = (AstPropertyNode*)item;
        MIR_reg_t key = 0;
        if (property->computed) key = mvp_generic_emit_expression(state, property->key, ok);
        else if (property->key && property->key->node_type == AST_NODE_IDENT) {
            key = mvp_generic_emit_string(state, ((AstIdentNode*)property->key)->name, ok);
        } else {
            key = mvp_generic_emit_expression(state, property->key, ok);
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, property->value, ok);
        if (property->is_getter) {
            value = !*ok || !value ? 0 : mvp_generic_emit_host_one(state,
                "mvp_host_accessor_new", value, ok);
        } else if (property->is_setter) {
            mvp_generic_set_node_failure("object setter", (AstNode*)property);
            *ok = 0;
        }
        if (!*ok || !key || !value || !mvp_generic_emit_member_set(state, object, key,
                value, ok)) return 0;
    }
    return object;
}

static MIR_reg_t mvp_generic_emit_array(MvpGenericState* state, AstArrayNode* array,
        int* ok) {
    if (mvp_generic_static_literal_supported((AstNode*)array)) {
        return mvp_generic_emit_static_literal(state, (AstNode*)array, ok);
    }
    MIR_reg_t size = mvp_generic_emit_constant(state, mvp_value_from_number(0), "length", ok);
    if (!*ok || !size) return 0;
    // The array constructor takes the source length value; inline its small
    // import call so literals share the same backing storage implementation.
    MIR_reg_t object = mvp_generic_new_register(state, "array");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "length", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, size),
    };
    if (!object || !mvp_generic_call_import(state, "mvp_host_array_new", MIR_T_I64,
            object, 2, arguments, operands) || !mvp_generic_pin(state, object)) {
        *ok = 0;
        return 0;
    }
    for (AstNode* item = array ? array->elements : NULL; item; item = item->next) {
        AstNode* source = item && item->node_type == AST_NODE_SPREAD
            ? ((AstUnaryNode*)item)->operand : item;
        MIR_reg_t value = mvp_generic_emit_expression(state, source, ok);
        const char* helper = item && item->node_type == AST_NODE_SPREAD
            ? "mvp_op_array_spread" : "mvp_op_array_push";
        if (!*ok || !source || !value || !mvp_generic_emit_runtime_binary(state, helper, object,
                value, ok)) return 0;
    }
    return object;
}

static MIR_reg_t mvp_generic_emit_argument_array(MvpGenericState* state,
        AstNode* arguments, int* ok) {
    MIR_reg_t size = mvp_generic_emit_constant(state, mvp_value_from_number(0), "length", ok);
    MIR_reg_t array = mvp_generic_new_register(state, "rest");
    MIR_var_t parameters[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "length", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, size),
    };
    if (!*ok || !size || !array || !mvp_generic_call_import(state, "mvp_host_array_new",
            MIR_T_I64, array, 2, parameters, operands) || !mvp_generic_pin(state, array)) {
        *ok = 0;
        return 0;
    }
    for (AstNode* argument = arguments; argument; argument = argument->next) {
        AstNode* source = argument->node_type == AST_NODE_SPREAD
            ? ((AstUnaryNode*)argument)->operand : argument;
        MIR_reg_t value = mvp_generic_emit_expression(state, source, ok);
        const char* helper = argument->node_type == AST_NODE_SPREAD
            ? "mvp_op_array_spread" : "mvp_op_array_push";
        if (!*ok || !source || !value || !mvp_generic_emit_runtime_binary(state, helper,
                array, value, ok)) return 0;
    }
    return array;
}

static MIR_reg_t mvp_generic_emit_template(MvpGenericState* state,
        JsTemplateLiteralNode* literal, int* ok) {
    MIR_reg_t result = mvp_generic_emit_bytes(state, "", 0, ok);
    AstNode* expression = literal ? literal->expressions : NULL;
    for (AstNode* quasi = literal ? literal->quasis : NULL; quasi; quasi = quasi->next) {
        if (quasi->node_type != JS_AST_NODE_TEMPLATE_ELEMENT) {
            mvp_generic_set_node_failure("template literal", quasi);
            *ok = 0;
            return 0;
        }
        String* cooked = ((JsTemplateElementNode*)quasi)->cooked;
        MIR_reg_t text = mvp_generic_emit_string(state, cooked, ok);
        result = !*ok || !result || !text ? 0 : mvp_generic_emit_runtime_binary(state,
            "mvp_op_add", result, text, ok);
        if (!*ok || !result) return 0;
        if (!expression) continue;
        MIR_reg_t value = mvp_generic_emit_expression(state, expression, ok);
        result = !*ok || !value ? 0 : mvp_generic_emit_runtime_binary(state, "mvp_op_add",
            result, value, ok);
        if (!*ok || !result) return 0;
        expression = expression->next;
    }
    if (expression) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_conditional(MvpGenericState* state,
        AstIfNode* conditional, int* ok) {
    MIR_reg_t test = mvp_generic_emit_expression(state, conditional ? conditional->test : NULL, ok);
    MIR_reg_t condition = 0;
    MIR_reg_t result = mvp_generic_new_register(state, "conditional");
    MIR_label_t alternate = MIR_new_label(state->compiler->context);
    MIR_label_t done = MIR_new_label(state->compiler->context);
    if (!*ok || !test || !result || !alternate || !done ||
            !mvp_generic_emit_truthy(state, test, &condition)) {
        *ok = 0;
        return 0;
    }
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BF,
        MIR_new_label_op(state->compiler->context, alternate),
        MIR_new_reg_op(state->compiler->context, condition)));
    MIR_reg_t consequent = mvp_generic_emit_expression(state, conditional->consequent, ok);
    if (!*ok || !consequent) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_reg_op(state->compiler->context, consequent)));
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
        MIR_new_label_op(state->compiler->context, done)));
    mvp_generic_emit(state, alternate);
    MIR_reg_t alternative = mvp_generic_emit_expression(state, conditional->alternate, ok);
    if (!*ok || !alternative) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_reg_op(state->compiler->context, alternative)));
    mvp_generic_emit(state, done);
    if (!mvp_generic_pin(state, result)) *ok = 0;
    return result;
}

static MIR_reg_t mvp_generic_emit_runtime_binary(MvpGenericState* state,
        const char* name, MIR_reg_t left, MIR_reg_t right, int* ok) {
    MIR_reg_t result = mvp_generic_new_register(state, "op");
    MIR_var_t arguments[3] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "left", 0},
        {MIR_T_I64, "right", 0}};
    MIR_op_t operands[3] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, left),
        MIR_new_reg_op(state->compiler->context, right),
    };
    if (!result || !mvp_generic_call_import(state, name, MIR_T_I64, result, 3,
            arguments, operands) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static const char* mvp_generic_compound_assignment_helper(Operator op) {
    switch (op) {
    case OPERATOR_JS_ADD_ASSIGN: return "mvp_op_add";
    case OPERATOR_JS_SUB_ASSIGN: return "mvp_op_sub";
    case OPERATOR_JS_MUL_ASSIGN: return "mvp_op_mul";
    case OPERATOR_JS_DIV_ASSIGN: return "mvp_op_div";
    case OPERATOR_JS_MOD_ASSIGN: return "mvp_op_mod";
    case OPERATOR_JS_BIT_AND_ASSIGN: return "mvp_op_bit_and";
    case OPERATOR_JS_BIT_OR_ASSIGN: return "mvp_op_bit_or";
    case OPERATOR_JS_BIT_XOR_ASSIGN: return "mvp_op_bit_xor";
    case OPERATOR_JS_LSHIFT_ASSIGN: return "mvp_op_bit_lshift";
    case OPERATOR_JS_RSHIFT_ASSIGN: return "mvp_op_bit_rshift";
    case OPERATOR_JS_URSHIFT_ASSIGN: return "mvp_op_bit_urshift";
    default: return NULL;
    }
}

static MIR_reg_t mvp_generic_emit_binary(MvpGenericState* state,
        AstBinaryNode* binary, int* ok) {
    MIR_reg_t left = mvp_generic_emit_expression(state, binary->left, ok);
    if (!*ok || !left) return 0;
    if (binary->op == OPERATOR_JS_INSTANCEOF &&
            mvp_generic_identifier_named(binary->right, "Array")) {
        return mvp_generic_emit_host_one(state, "mvp_host_array_is_array", left, ok);
    }
    if (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR) {
        MIR_reg_t result = mvp_generic_new_register(state, "logical");
        MIR_reg_t condition = 0;
        MIR_label_t done = MIR_new_label(state->compiler->context);
        if (!result || !done || !mvp_generic_emit_truthy(state, left, &condition)) {
            mvp_generic_set_failure("logical setup", state->root_count);
            *ok = 0;
            return 0;
        }
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
            MIR_new_reg_op(state->compiler->context, result),
            MIR_new_reg_op(state->compiler->context, left)));
        MIR_insn_code_t branch = binary->op == OPERATOR_AND ? MIR_BF : MIR_BT;
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, branch,
            MIR_new_label_op(state->compiler->context, done),
            MIR_new_reg_op(state->compiler->context, condition)));
        MIR_reg_t right = mvp_generic_emit_expression(state, binary->right, ok);
        if (!*ok || !right) {
            mvp_generic_set_failure("logical right", state->root_count);
            return 0;
        }
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
            MIR_new_reg_op(state->compiler->context, result),
            MIR_new_reg_op(state->compiler->context, right)));
        mvp_generic_emit(state, done);
        if (!mvp_generic_pin(state, result)) {
            mvp_generic_set_failure("logical result", state->root_count);
            *ok = 0;
        }
        return result;
    }
    MIR_reg_t right = mvp_generic_emit_expression(state, binary->right, ok);
    if (!*ok || !right) return 0;
    const char* helper = NULL;
    switch (binary->op) {
    case OPERATOR_ADD: helper = "mvp_op_add"; break;
    case OPERATOR_SUB: helper = "mvp_op_sub"; break;
    case OPERATOR_MUL: helper = "mvp_op_mul"; break;
    case OPERATOR_DIV: helper = "mvp_op_div"; break;
    case OPERATOR_MOD: helper = "mvp_op_mod"; break;
    case OPERATOR_JS_BIT_AND: helper = "mvp_op_bit_and"; break;
    case OPERATOR_JS_BIT_OR: helper = "mvp_op_bit_or"; break;
    case OPERATOR_JS_BIT_XOR: helper = "mvp_op_bit_xor"; break;
    case OPERATOR_JS_LSHIFT: helper = "mvp_op_bit_lshift"; break;
    case OPERATOR_JS_URSHIFT: helper = "mvp_op_bit_urshift"; break;
    case OPERATOR_EQ: helper = "mvp_op_equal"; break;
    case OPERATOR_NE: helper = "mvp_op_not_equal"; break;
    case OPERATOR_JS_STRICT_EQ: helper = "mvp_op_strict_equal"; break;
    case OPERATOR_JS_STRICT_NE: helper = "mvp_op_strict_not_equal"; break;
    case OPERATOR_LT: helper = "mvp_op_less_than"; break;
    case OPERATOR_LE: helper = "mvp_op_less_equal"; break;
    case OPERATOR_GT: helper = "mvp_op_greater_than"; break;
    case OPERATOR_GE: helper = "mvp_op_greater_equal"; break;
    case OPERATOR_IN: helper = "mvp_op_in"; break;
    case OPERATOR_JS_RSHIFT: helper = "mvp_op_bit_rshift"; break;
    default:
        mvp_generic_set_failure("binary operator", binary->op);
        mvp_generic_set_node_failure("binary expression", (AstNode*)binary);
        *ok = 0;
        return 0;
    }
    return mvp_generic_emit_runtime_binary(state, helper, left, right, ok);
}

static MIR_reg_t mvp_generic_emit_call(MvpGenericState* state, AstCallNode* call,
        int* ok) {
    if (!call || !call->callee) {
        *ok = 0;
        return 0;
    }
    if (mvp_generic_regexp_test_bind(call->callee)) {
        return mvp_generic_emit_host_call(state, call, "mvp_host_regexp_test_bind", 1, ok);
    }
    if (mvp_generic_identifier_named(call->callee, "Array")) {
        if (!call->arguments) return mvp_generic_emit_host_noargs(state,
            "mvp_host_array_empty", ok);
        if (!call->arguments->next) return mvp_generic_emit_host_call(state, call,
            "mvp_host_array_one", 1, ok);
        return mvp_generic_emit_argument_array(state, call->arguments, ok);
    }
    if (mvp_generic_object_prototype_call(call->callee, "toString")) {
        return mvp_generic_emit_host_call(state, call, "mvp_host_object_to_string", 1, ok);
    }
    if (mvp_generic_object_prototype_call(call->callee, "hasOwnProperty")) {
        return mvp_generic_emit_host_call(state, call, "mvp_host_object_has_own", 2, ok);
    }
    if (call->callee->node_type == AST_NODE_MEMBER_EXPR &&
            mvp_generic_identifier_named(((AstFieldNode*)call->callee)->field, "fill")) {
        return mvp_generic_emit_array_fill(state, call, ok);
    }
    if (mvp_generic_identifier_named(call->callee, "super")) {
        return mvp_generic_emit_super_call(state, call, ok);
    }
    if (mvp_generic_member_named(call->callee, "Math", "max")) {
        size_t argument_count = 0;
        for (AstNode* argument = call->arguments; argument; argument = argument->next) {
            argument_count++;
        }
        if (argument_count == 3) return mvp_generic_emit_host_call(state, call,
            "mvp_host_math_max3", 3, ok);
    }
    const char* host_name = NULL;
    int host_argument_count = 0;
    if (mvp_generic_host_call_name(call->callee, &host_name, &host_argument_count)) {
        return mvp_generic_emit_host_call(state, call, host_name, host_argument_count, ok);
    }
    MvpGenericFunction* target = call->callee->node_type == AST_NODE_IDENT
        ? mvp_generic_find_function(state->compiler, ((AstIdentNode*)call->callee)->name)
        : NULL;
    int direct_self_method = 0;
    if (!target) {
        target = mvp_generic_find_direct_self_method(state, call);
        direct_self_method = target != NULL;
    }
    MvpNumericProgram* numeric = target && !target->is_closure
        ? mvp_generic_find_numeric_program(state->compiler, target->ast) : NULL;
    if (numeric) return mvp_generic_emit_numeric_call(state, call, numeric, ok);
    if (!target || !target->forward) return mvp_generic_emit_dynamic_call(state, call, ok);
    MIR_op_t call_arguments[66] = {};
    size_t argument_count = 0;
    call_arguments[argument_count++] = MIR_new_reg_op(state->compiler->context,
        state->execution);
    call_arguments[argument_count++] = direct_self_method
        ? MIR_new_reg_op(state->compiler->context, state->receiver)
        : MIR_new_int_op(state->compiler->context, (int64_t)mvp_value_undefined().bits);
    size_t fixed_parameter_count = target->parameter_count -
        (target->has_rest_parameter ? 1 : 0);
    AstNode* argument = call->arguments;
    for (size_t index = 0; argument && index < fixed_parameter_count;
            index++, argument = argument->next) {
        if (argument->node_type == AST_NODE_SPREAD) {
            mvp_generic_set_node_failure("fixed call argument", argument);
            *ok = 0;
            return 0;
        }
        if (argument_count >= sizeof(call_arguments) / sizeof(call_arguments[0])) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_expression(state, argument, ok);
        if (!*ok || !value) return 0;
        call_arguments[argument_count++] = MIR_new_reg_op(state->compiler->context, value);
    }
    if (target->has_rest_parameter) {
        MIR_reg_t rest = mvp_generic_emit_argument_array(state, argument, ok);
        if (!*ok || !rest) return 0;
        call_arguments[argument_count++] = MIR_new_reg_op(state->compiler->context, rest);
        argument = NULL;
    }
    if (argument) {
        *ok = 0;
        return 0;
    }
    while (argument_count - 2 < target->parameter_count) {
        call_arguments[argument_count++] = MIR_new_int_op(state->compiler->context,
            (int64_t)mvp_value_undefined().bits);
    }
    MIR_var_t parameters[66] = {};
    parameters[0] = (MIR_var_t){MIR_T_P, "execution", 0};
    parameters[1] = (MIR_var_t){MIR_T_I64, "receiver", 0};
    for (size_t index = 2; index < argument_count; index++) {
        parameters[index] = (MIR_var_t){MIR_T_I64, "argument", 0};
    }
    char prototype_name[48];
    int written = snprintf(prototype_name, sizeof(prototype_name), "mvp_c_%u",
        state->compiler->next_symbol++);
    if (written <= 0 || (size_t)written >= sizeof(prototype_name)) {
        *ok = 0;
        return 0;
    }
    MIR_type_t result_type = MIR_T_I64;
    MIR_item_t prototype = MIR_new_proto_arr(state->compiler->context, prototype_name,
        1, &result_type, argument_count, parameters);
    MIR_reg_t result = mvp_generic_new_register(state, "call");
    if (!prototype || !result) {
        *ok = 0;
        return 0;
    }
    MIR_op_t operands[68] = {};
    operands[0] = MIR_new_ref_op(state->compiler->context, prototype);
    operands[1] = MIR_new_ref_op(state->compiler->context, target->forward);
    operands[2] = MIR_new_reg_op(state->compiler->context, result);
    for (size_t index = 0; index < argument_count; index++) operands[index + 3] = call_arguments[index];
    mvp_generic_emit(state, MIR_new_insn_arr(state->compiler->context, MIR_CALL,
        argument_count + 3, operands));
    if (!mvp_generic_emit_error_guard(state) || !mvp_generic_pin(state, result)) {
        *ok = 0;
        return 0;
    }
    return result;
}

static MIR_reg_t mvp_generic_emit_expression(MvpGenericState* state,
        AstNode* node, int* ok) {
    if (!state || !node || !*ok) return 0;
    state->compiler->last_node = node;
    switch (node->node_type) {
    case AST_NODE_LITERAL: {
        AstLiteralNode* literal = (AstLiteralNode*)node;
        if (literal->literal_type == AST_LITERAL_NUMBER) {
            if (literal->is_bigint) {
                MIR_reg_t text = mvp_generic_emit_string(state, literal->bigint_str, ok);
                return !*ok || !text ? 0 : mvp_generic_emit_host_one(state, "mvp_host_bigint",
                    text, ok);
            }
            return mvp_generic_emit_constant(state,
                mvp_value_from_number(literal->value.number_value), "number", ok);
        }
        if (literal->literal_type == AST_LITERAL_BOOLEAN) {
            return mvp_generic_emit_constant(state, mvp_value_bool(literal->value.boolean_value),
                "boolean", ok);
        }
        if (literal->literal_type == AST_LITERAL_NULL) {
            return mvp_generic_emit_constant(state, mvp_value_null(), "null", ok);
        }
        if (literal->literal_type == AST_LITERAL_UNDEFINED) {
            return mvp_generic_emit_constant(state, mvp_value_undefined(), "undefined", ok);
        }
        if (literal->literal_type == AST_LITERAL_STRING) return mvp_generic_emit_string(state,
            literal->value.string_value, ok);
        mvp_generic_set_failure("literal", literal->literal_type);
        *ok = 0;
        return 0;
    }
    case AST_NODE_IDENT: {
        if (mvp_generic_identifier_named(node, "this")) {
            if (!state->is_closure || !state->captures_lexical_this) return state->receiver;
            return mvp_generic_emit_capture_read(state, ((AstIdentNode*)node)->name, ok);
        }
        if (mvp_generic_identifier_named(node, "globalThis")) {
            return mvp_generic_emit_host_noargs(state, "mvp_execution_global_object", ok);
        }
        if (mvp_generic_identifier_named(node, "undefined")) {
            return mvp_generic_emit_constant(state, mvp_value_undefined(), "undefined", ok);
        }
        if (mvp_generic_identifier_named(node, "Infinity")) {
            return mvp_generic_emit_constant(state, mvp_value_from_number(INFINITY),
                "infinity", ok);
        }
        if (mvp_generic_identifier_named(node, "NaN")) {
            return mvp_generic_emit_constant(state, mvp_value_from_number(NAN), "nan", ok);
        }
        MvpGenericBinding* binding = mvp_generic_lookup_binding(state,
            ((AstIdentNode*)node)->name);
        MIR_reg_t result = binding ? mvp_generic_emit_binding_read(state, binding, ok) : 0;
        if (!result) {
            if (mvp_generic_is_global_name(state->compiler, ((AstIdentNode*)node)->name)) {
                return mvp_generic_emit_global_get(state, ((AstIdentNode*)node)->name, ok);
            }
            MvpGenericFunction* function = mvp_generic_find_function(state->compiler,
                ((AstIdentNode*)node)->name);
            if (function && !function->is_closure) {
                return mvp_generic_emit_function_value(state, function, ok);
            }
            if (function && function->is_closure && state->is_closure) {
                return mvp_generic_emit_capture_read(state, ((AstIdentNode*)node)->name, ok);
            }
            if (function) return mvp_generic_emit_function_closure(state, function, ok);
            if (!state->is_closure) {
                return mvp_generic_emit_global_get(state, ((AstIdentNode*)node)->name, ok);
            }
            result = mvp_generic_emit_capture_read(state, ((AstIdentNode*)node)->name, ok);
            if (!result) {
                mvp_generic_set_identifier_failure(((AstIdentNode*)node)->name);
                *ok = 0;
            }
        }
        return result;
    }
    case AST_NODE_BINARY:
        return mvp_generic_emit_binary(state, (AstBinaryNode*)node, ok);
    case AST_NODE_CALL_EXPR:
        return mvp_generic_emit_call(state, (AstCallNode*)node, ok);
    case AST_NODE_NEW_EXPR:
        return mvp_generic_emit_new(state, (AstCallNode*)node, ok);
    case AST_NODE_MEMBER_EXPR:
        return mvp_generic_emit_member(state, (AstFieldNode*)node, ok);
    case AST_NODE_MAP:
        return mvp_generic_emit_object(state, (AstMapNode*)node, ok);
    case AST_NODE_ARRAY:
        return mvp_generic_emit_array(state, (AstArrayNode*)node, ok);
    case AST_NODE_SEQ: {
        MIR_reg_t result = 0;
        for (AstNode* item = ((AstArrayNode*)node)->elements; item; item = item->next) {
            result = mvp_generic_emit_expression(state, item, ok);
            if (!*ok || !result) return 0;
        }
        if (!result) *ok = 0;
        return result;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL:
        return mvp_generic_emit_template(state, (JsTemplateLiteralNode*)node, ok);
    case JS_AST_NODE_REGEX: {
        JsRegexNode* regex = (JsRegexNode*)node;
        if (!regex->pattern || regex->pattern_len < 0 || regex->flags_len < 0) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t pattern = mvp_generic_emit_bytes(state, regex->pattern,
            (size_t)regex->pattern_len, ok);
        MIR_reg_t flags = !*ok || !pattern ? 0 : mvp_generic_emit_bytes(state,
            regex->flags ? regex->flags : "", (size_t)regex->flags_len, ok);
        MIR_reg_t result = mvp_generic_new_register(state, "regex");
        MIR_var_t arguments[3] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "pattern", 0},
            {MIR_T_I64, "flags", 0}};
        MIR_op_t operands[3] = {
            MIR_new_reg_op(state->compiler->context, state->execution),
            MIR_new_reg_op(state->compiler->context, pattern),
            MIR_new_reg_op(state->compiler->context, flags),
        };
        if (!*ok || !flags || !result || !mvp_generic_call_import(state,
                "mvp_host_regexp_new", MIR_T_I64, result, 3, arguments, operands) ||
                !mvp_generic_pin(state, result)) {
            *ok = 0;
            return 0;
        }
        return result;
    }
    case AST_NODE_ARROW_FUNC:
        return mvp_generic_emit_closure(state, (AstFuncNode*)node, ok);
    case AST_NODE_FUNC_EXPR:
        return mvp_generic_emit_closure(state, (AstFuncNode*)node, ok);
    case AST_NODE_FUNC:
        // Object accessors are represented by the shared function AST node.
        // Treat their value as a lexical callable when it appears in an
        // expression; declaration statements are handled separately below.
        return mvp_generic_emit_closure(state, (AstFuncNode*)node, ok);
    case AST_NODE_CONDITIONAL_EXPR:
        return mvp_generic_emit_conditional(state, (AstIfNode*)node, ok);
    case AST_NODE_UNARY: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if ((unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) &&
                unary->operand && unary->operand->node_type == AST_NODE_MEMBER_EXPR) {
            AstFieldNode* member = (AstFieldNode*)unary->operand;
            MIR_reg_t object = mvp_generic_emit_expression(state, member->object, ok);
            String* name = !member->computed && member->field &&
                    member->field->node_type == AST_NODE_IDENT
                ? ((AstIdentNode*)member->field)->name : NULL;
            int own_data = !member->optional && state->owner_class && !state->is_static_method &&
                mvp_generic_identifier_named(member->object, "this");
            MIR_reg_t key = name ? 0 : mvp_generic_emit_member_key(state, member, ok);
            MIR_reg_t operand = !*ok || !object || (!name && !key) ? 0 : name
                ? mvp_generic_emit_named_member_get(state, object, name, member->optional,
                    own_data, ok)
                : mvp_generic_emit_runtime_binary(state, "mvp_op_member_get", object, key, ok);
            MIR_reg_t old = mvp_generic_new_register(state, "prior");
            if (!*ok || !operand || !old) {
                *ok = 0;
                return 0;
            }
            mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
                MIR_new_reg_op(state->compiler->context, old),
                MIR_new_reg_op(state->compiler->context, operand)));
            if (!mvp_generic_pin(state, old)) {
                *ok = 0;
                return 0;
            }
            MIR_reg_t one = mvp_generic_emit_constant(state, mvp_value_from_number(1.0),
                "one", ok);
            MIR_reg_t updated = mvp_generic_emit_runtime_binary(state,
                unary->op == OPERATOR_JS_INCREMENT ? "mvp_op_add" : "mvp_op_sub",
                operand, one, ok);
            MIR_reg_t stored = !*ok || !updated ? 0 : name
                ? mvp_generic_emit_named_member_set(state, object, name, updated, ok)
                : mvp_generic_emit_member_set(state, object, key, updated, ok);
            if (!*ok || !stored) return 0;
            return unary->prefix ? stored : old;
        }
        MvpGenericBinding* binding = unary->operand && unary->operand->node_type == AST_NODE_IDENT
            ? mvp_generic_lookup_binding(state, ((AstIdentNode*)unary->operand)->name) : NULL;
        MIR_reg_t operand = mvp_generic_emit_expression(state, unary->operand, ok);
        if (!*ok || !operand) return 0;
        if (unary->op == OPERATOR_POS) return mvp_generic_emit_host_one(state,
            "mvp_host_number", operand, ok);
        if (unary->op == OPERATOR_JS_VOID) return mvp_generic_emit_constant(state,
            mvp_value_undefined(), "undefined", ok);
        if ((unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) &&
                unary->operand && unary->operand->node_type == AST_NODE_IDENT) {
            MIR_reg_t old = mvp_generic_new_register(state, "prior");
            if (!old) {
                *ok = 0;
                return 0;
            }
            mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
                MIR_new_reg_op(state->compiler->context, old),
                MIR_new_reg_op(state->compiler->context, operand)));
            if (!mvp_generic_pin(state, old)) {
                *ok = 0;
                return 0;
            }
            MIR_reg_t one = mvp_generic_emit_constant(state, mvp_value_from_number(1.0),
                "one", ok);
            MIR_reg_t updated = mvp_generic_emit_runtime_binary(state,
                unary->op == OPERATOR_JS_INCREMENT ? "mvp_op_add" : "mvp_op_sub",
                operand, one, ok);
            if (!*ok || !updated) return 0;
            String* name = ((AstIdentNode*)unary->operand)->name;
            int is_global = mvp_generic_is_global_name(state->compiler, name);
            MIR_reg_t stored = binding ? mvp_generic_emit_binding_write(state, binding, updated,
                ok) : is_global || !state->is_closure ? updated
                : mvp_generic_emit_capture_write(state, name, updated, ok);
            if (*ok && stored && (is_global || !state->is_closure) &&
                    !mvp_generic_emit_global_set(state, name, updated)) *ok = 0;
            if (!*ok || !stored) return 0;
            return unary->prefix ? stored : old;
        }
        const char* helper = unary->op == OPERATOR_NEG ? "mvp_op_neg"
            : unary->op == OPERATOR_NOT ? "mvp_op_not"
            : unary->op == OPERATOR_JS_BIT_NOT ? "mvp_op_bit_not"
            : unary->op == OPERATOR_JS_TYPEOF ? "mvp_op_typeof" : NULL;
        if (!helper) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t result = mvp_generic_new_register(state, "unary");
        MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "value", 0}};
        MIR_op_t operands[2] = {
            MIR_new_reg_op(state->compiler->context, state->execution),
            MIR_new_reg_op(state->compiler->context, operand),
        };
        if (!result || !mvp_generic_call_import(state, helper, MIR_T_I64, result, 2,
                arguments, operands) || !mvp_generic_pin(state, result)) {
            *ok = 0;
            return 0;
        }
        return result;
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* assignment = (AstAssignNode*)node;
        if (!assignment->left) {
            *ok = 0;
            return 0;
        }
        if (assignment->left->node_type == AST_NODE_MEMBER_EXPR) {
            AstFieldNode* member = (AstFieldNode*)assignment->left;
            MIR_reg_t object = mvp_generic_emit_expression(state, member->object, ok);
            String* name = !member->computed && member->field &&
                    member->field->node_type == AST_NODE_IDENT
                ? ((AstIdentNode*)member->field)->name : NULL;
            int own_data = !member->optional && state->owner_class && !state->is_static_method &&
                mvp_generic_identifier_named(member->object, "this");
            MIR_reg_t key = name ? 0 : mvp_generic_emit_member_key(state, member, ok);
            if (!*ok || !object || (!name && !key)) return 0;
            if (assignment->op == OPERATOR_ASSIGN) {
                MIR_reg_t value = mvp_generic_emit_expression(state, assignment->right, ok);
                if (!*ok || !value) return 0;
                return name ? mvp_generic_emit_named_member_set(state, object, name, value, ok)
                    : mvp_generic_emit_member_set(state, object, key, value, ok);
            }
            MIR_reg_t prior = name ? mvp_generic_emit_named_member_get(state, object, name,
                member->optional, own_data, ok) : mvp_generic_emit_runtime_binary(state,
                "mvp_op_member_get", object, key, ok);
            MIR_reg_t right = mvp_generic_emit_expression(state, assignment->right, ok);
            const char* helper = mvp_generic_compound_assignment_helper(assignment->op);
            if (!*ok || !prior || !right || !helper) {
                *ok = 0;
                return 0;
            }
            MIR_reg_t value = mvp_generic_emit_runtime_binary(state, helper, prior, right, ok);
            if (!*ok || !value) return 0;
            return name ? mvp_generic_emit_named_member_set(state, object, name, value, ok)
                : mvp_generic_emit_member_set(state, object, key, value, ok);
        }
        if (assignment->left->node_type == AST_NODE_ARRAY_PATTERN) {
            if (assignment->op != OPERATOR_ASSIGN) {
                *ok = 0;
                return 0;
            }
            MIR_reg_t source = mvp_generic_emit_expression(state, assignment->right, ok);
            if (!*ok || !source || !mvp_generic_assign_pattern(state, assignment->left,
                    source, ok)) {
                *ok = 0;
                return 0;
            }
            return source;
        }
        if (assignment->left->node_type != AST_NODE_IDENT) {
            *ok = 0;
            return 0;
        }
        String* name = ((AstIdentNode*)assignment->left)->name;
        MvpGenericBinding* binding = mvp_generic_lookup_binding(state, name);
        int is_global = mvp_generic_is_global_name(state->compiler, name);
        if (assignment->op == OPERATOR_ASSIGN) {
            MIR_reg_t source = mvp_generic_emit_expression(state, assignment->right, ok);
            if (!*ok || !source) return 0;
            if (binding) {
                MIR_reg_t stored = mvp_generic_emit_binding_write(state, binding, source, ok);
                if (*ok && stored && is_global &&
                        !mvp_generic_emit_global_set(state, name, source)) *ok = 0;
                return stored;
            }
            if (is_global) {
                if (!mvp_generic_emit_global_set(state, name, source)) *ok = 0;
                return source;
            }
            if (!state->is_closure) {
                if (!mvp_generic_emit_global_set(state, name, source)) *ok = 0;
                return source;
            }
            return mvp_generic_emit_capture_write(state, name, source, ok);
        }
        MIR_reg_t destination = binding ? mvp_generic_emit_binding_read(state, binding, ok)
            : is_global || !state->is_closure ? mvp_generic_emit_global_get(state, name, ok)
            : mvp_generic_emit_capture_read(state, name, ok);
        MIR_reg_t source = mvp_generic_emit_expression(state, assignment->right, ok);
        if (!*ok || !source) return 0;
        const char* helper = mvp_generic_compound_assignment_helper(assignment->op);
        if (!destination || !helper) {
            *ok = 0;
            return 0;
        }
        MIR_reg_t value = mvp_generic_emit_runtime_binary(state, helper, destination, source, ok);
        if (!*ok || !value) return 0;
        if (binding) {
            MIR_reg_t stored = mvp_generic_emit_binding_write(state, binding, value, ok);
            if (*ok && stored && is_global &&
                    !mvp_generic_emit_global_set(state, name, value)) *ok = 0;
            return stored;
        }
        if (is_global) {
            if (!mvp_generic_emit_global_set(state, name, value)) *ok = 0;
            return value;
        }
        return mvp_generic_emit_capture_write(state, name, value, ok);
    }
    default:
        *ok = 0;
        return 0;
    }
}

static int mvp_generic_emit_statement(MvpGenericState* state, AstNode* node,
        MIR_reg_t* out_last);

static int mvp_generic_declare_pattern(MvpGenericState* state, AstNode* pattern) {
    if (!state || !pattern) return 0;
    if (pattern->node_type == AST_NODE_IDENT) {
        MIR_reg_t local = mvp_generic_new_register(state, "pattern_local");
        return local && mvp_generic_bind(state, ((AstIdentNode*)pattern)->name, local);
    }
    if (pattern->node_type != AST_NODE_ARRAY_PATTERN) return 0;
    AstArrayNode* array = (AstArrayNode*)pattern;
    for (AstNode* element = array->elements; element; element = element->next) {
        if (element->node_type == AST_NODE_NULL) continue;
        if (!mvp_generic_declare_pattern(state, element)) return 0;
    }
    return 1;
}

static int mvp_generic_assign_pattern(MvpGenericState* state, AstNode* pattern,
        MIR_reg_t value, int* ok) {
    if (!state || !pattern || !value || !ok || !*ok) return 0;
    if (pattern->node_type == AST_NODE_IDENT) {
        String* name = ((AstIdentNode*)pattern)->name;
        MvpGenericBinding* binding = mvp_generic_lookup_binding(state, name);
        int is_global = mvp_generic_is_global_name(state->compiler, name);
        MIR_reg_t stored = binding ? mvp_generic_emit_binding_write(state, binding, value, ok)
            : is_global || !state->is_closure ? value
            : mvp_generic_emit_capture_write(state, name, value, ok);
        if (*ok && stored && (is_global || !state->is_closure) &&
                !mvp_generic_emit_global_set(state, name, value)) {
            *ok = 0;
        }
        return *ok && stored;
    }
    if (pattern->node_type != AST_NODE_ARRAY_PATTERN) return 0;
    AstArrayNode* array = (AstArrayNode*)pattern;
    size_t index = 0;
    for (AstNode* element = array->elements; element; element = element->next, index++) {
        if (element->node_type == AST_NODE_NULL) continue;
        MIR_reg_t key = mvp_generic_emit_constant(state, mvp_value_from_number((double)index),
            "pattern_index", ok);
        MIR_reg_t item = !*ok || !key ? 0 : mvp_generic_emit_runtime_binary(state,
            "mvp_op_member_get", value, key, ok);
        if (!*ok || !item || !mvp_generic_assign_pattern(state, element, item, ok)) return 0;
    }
    return 1;
}

static int mvp_generic_emit_statements(MvpGenericState* state, AstNode* statements,
        MIR_reg_t* out_last) {
    for (AstNode* statement = statements; statement; statement = statement->next) {
        if (!mvp_generic_emit_statement(state, statement, out_last)) return 0;
    }
    return 1;
}

static int mvp_generic_emit_variable(MvpGenericState* state, AstVarDeclNode* declaration) {
    for (AstNode* node = declaration->declarations; node; node = node->next) {
        if (node->node_type != AST_NODE_VARIABLE_DECLARATOR) return 0;
        AstDeclaratorNode* declarator = (AstDeclaratorNode*)node;
        if (!declarator->id || (declarator->id->node_type != AST_NODE_IDENT &&
                declarator->id->node_type != AST_NODE_ARRAY_PATTERN)) return 0;
        int ok = 1;
        MIR_reg_t value = declarator->init ? mvp_generic_emit_expression(state,
            declarator->init, &ok) : mvp_generic_emit_constant(state,
            mvp_value_undefined(), "undefined", &ok);
        if (declarator->id->node_type == AST_NODE_ARRAY_PATTERN) {
            if (!ok || !value || !mvp_generic_declare_pattern(state, declarator->id) ||
                    !mvp_generic_assign_pattern(state, declarator->id, value, &ok)) return 0;
            continue;
        }
        MIR_reg_t local = mvp_generic_new_register(state, "local");
        if (!ok || !value || !local || !mvp_generic_bind(state,
                ((AstIdentNode*)declarator->id)->name, local)) return 0;
        MvpGenericBinding* binding = mvp_generic_lookup_binding(state,
            ((AstIdentNode*)declarator->id)->name);
        if (!binding || !mvp_generic_emit_binding_write(state, binding, value, &ok) || !ok) {
            return 0;
        }
        if (state->is_main && mvp_generic_is_global_name(state->compiler,
                ((AstIdentNode*)declarator->id)->name) && !mvp_generic_emit_global_set(state,
                ((AstIdentNode*)declarator->id)->name, value)) return 0;
    }
    return 1;
}

static int mvp_generic_emit_truthy(MvpGenericState* state, MIR_reg_t value,
        MIR_reg_t* out_condition) {
    MIR_reg_t condition = mvp_generic_new_register(state, "condition");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "value", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_reg_op(state->compiler->context, value),
    };
    if (!condition || !mvp_generic_call_import(state, "mvp_op_truthy", MIR_T_I64,
            condition, 2, arguments, operands)) return 0;
    *out_condition = condition;
    return 1;
}

static int mvp_generic_emit_leave_and_return(MvpGenericState* state, MIR_reg_t value) {
    MIR_var_t argument = {MIR_T_P, "execution", 0};
    MIR_op_t operand = MIR_new_reg_op(state->compiler->context, state->execution);
    if (!mvp_generic_emit_void_call(state, "mvp_execution_leave_frame", 1,
            &argument, &operand)) return 0;
    mvp_generic_emit(state, MIR_new_ret_insn(state->compiler->context, 1,
        MIR_new_reg_op(state->compiler->context, value)));
    return 1;
}

static int mvp_generic_emit_error_exit(MvpGenericState* state) {
    if (!state || !state->error_exit) return 0;
    mvp_generic_emit(state, state->error_exit);
    MIR_reg_t undefined = mvp_generic_new_register(state, "error_undefined");
    if (!undefined) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, undefined),
        MIR_new_int_op(state->compiler->context, (int64_t)mvp_value_undefined().bits)));
    return mvp_generic_emit_leave_and_return(state, undefined);
}

static int mvp_generic_push_control(MvpGenericState* state, MIR_label_t break_target,
        MIR_label_t continue_target) {
    if (!state || !break_target || state->control_count >=
            sizeof(state->controls) / sizeof(state->controls[0])) return 0;
    state->controls[state->control_count++] = (MvpGenericControlTarget){break_target,
        continue_target, NULL, 0};
    return 1;
}

static int mvp_generic_push_labeled_control(MvpGenericState* state, MIR_label_t break_target,
        MIR_label_t continue_target, const char* label, int label_length) {
    if (!mvp_generic_push_control(state, break_target, continue_target)) return 0;
    MvpGenericControlTarget* target = &state->controls[state->control_count - 1];
    target->label = label;
    target->label_length = label_length;
    return label && label_length > 0;
}

static void mvp_generic_pop_control(MvpGenericState* state) {
    if (state && state->control_count) state->control_count--;
}

static int mvp_generic_emit_control_jump(MvpGenericState* state,
        const AstBreakContinueNode* control, int is_continue) {
    if (!state || !state->control_count) return 0;
    MIR_label_t label = 0;
    for (size_t index = state->control_count; index > 0; index--) {
        MvpGenericControlTarget target = state->controls[index - 1];
        if (control && control->label_len > 0) {
            if (!target.label || target.label_length != control->label_len ||
                    memcmp(target.label, control->label, (size_t)control->label_len) != 0) {
                continue;
            }
            label = is_continue ? target.continue_target : target.break_target;
            break;
        }
        if (!is_continue || target.continue_target) {
            label = is_continue ? target.continue_target : target.break_target;
            break;
        }
    }
    if (!label) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
        MIR_new_label_op(state->compiler->context, label)));
    return 1;
}

static int mvp_generic_emit_loop(MvpGenericState* state, AstLoopControlNode* loop,
        MIR_reg_t* out_last) {
    if (!loop || !loop->test || !loop->body) return 0;
    if (loop->init && !mvp_generic_emit_statement(state, loop->init, out_last)) return 0;
    MIR_label_t test = MIR_new_label(state->compiler->context);
    MIR_label_t update = MIR_new_label(state->compiler->context);
    MIR_label_t done = MIR_new_label(state->compiler->context);
    if (!test || !update || !done) return 0;
    if (loop->form == LOOP_FORM_DO_WHILE) {
        // A do-while body must run before its first test; continue transfers
        // to that test instead of re-entering the body directly.
        MIR_label_t body = MIR_new_label(state->compiler->context);
        if (!body) return 0;
        mvp_generic_emit(state, body);
        if (!mvp_generic_push_control(state, done, test)) return 0;
        int body_ok = mvp_generic_emit_statement(state, loop->body, out_last);
        mvp_generic_pop_control(state);
        if (!body_ok) return 0;
        mvp_generic_emit(state, test);
        int ok = 1;
        MIR_reg_t test_value = mvp_generic_emit_expression(state, loop->test, &ok);
        MIR_reg_t condition = 0;
        if (!ok || !test_value || !mvp_generic_emit_truthy(state, test_value, &condition)) {
            return 0;
        }
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BT,
            MIR_new_label_op(state->compiler->context, body),
            MIR_new_reg_op(state->compiler->context, condition)));
        mvp_generic_emit(state, done);
        return 1;
    }
    mvp_generic_emit(state, test);
    int ok = 1;
    MIR_reg_t test_value = mvp_generic_emit_expression(state, loop->test, &ok);
    MIR_reg_t condition = 0;
    if (!ok || !test_value || !mvp_generic_emit_truthy(state, test_value, &condition)) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BF,
        MIR_new_label_op(state->compiler->context, done),
        MIR_new_reg_op(state->compiler->context, condition)));
    if (!mvp_generic_push_control(state, done, update)) return 0;
    int body_ok = mvp_generic_emit_statement(state, loop->body, out_last);
    mvp_generic_pop_control(state);
    if (!body_ok) return 0;
    mvp_generic_emit(state, update);
    if (loop->update) {
        (void)mvp_generic_emit_expression(state, loop->update, &ok);
        if (!ok) return 0;
    }
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
        MIR_new_label_op(state->compiler->context, test)));
    mvp_generic_emit(state, done);
    return 1;
}

static int mvp_generic_emit_for_of(MvpGenericState* state, AstForOfNode* loop,
        MIR_reg_t* out_last, int enumerate_keys) {
    if (!loop || loop->is_await || !loop->left || !loop->right || !loop->body) return 0;
    int array_pattern = loop->left->node_type == AST_NODE_ARRAY_PATTERN;
    if (!array_pattern && loop->left->node_type != AST_NODE_IDENT) return 0;
    int ok = 1;
    MIR_reg_t iterable = mvp_generic_emit_expression(state, loop->right, &ok);
    if (ok && iterable && enumerate_keys) {
        iterable = mvp_generic_emit_host_one(state, "mvp_op_enumerable_keys", iterable, &ok);
    }
    MIR_reg_t index = mvp_generic_emit_constant(state, mvp_value_from_number(0.0),
        "iterator_index", &ok);
    MIR_reg_t item = mvp_generic_new_register(state, "iterator_item");
    String* binding_name = array_pattern ? NULL : ((AstIdentNode*)loop->left)->name;
    if (!ok || !iterable || !index || !item || (!array_pattern && !binding_name)) return 0;
    if (array_pattern) {
        if (!mvp_generic_declare_pattern(state, loop->left)) return 0;
    } else if (!mvp_generic_bind(state, binding_name, item)) {
        return 0;
    }
    MvpGenericBinding* binding = binding_name ? mvp_generic_lookup_binding(state, binding_name)
        : NULL;
    static const char length_name[] = "length";
    MIR_reg_t length_key = mvp_generic_emit_bytes(state, length_name,
        sizeof(length_name) - 1, &ok);
    MIR_reg_t length = mvp_generic_emit_runtime_binary(state, "mvp_op_member_get",
        iterable, length_key, &ok);
    MIR_label_t test = MIR_new_label(state->compiler->context);
    MIR_label_t advance = MIR_new_label(state->compiler->context);
    MIR_label_t done = MIR_new_label(state->compiler->context);
    if (!ok || !length_key || !length || !test || !advance || !done) return 0;
    mvp_generic_emit(state, test);
    MIR_reg_t before_end = mvp_generic_emit_runtime_binary(state, "mvp_op_less_than",
        index, length, &ok);
    MIR_reg_t condition = 0;
    if (!ok || !before_end || !mvp_generic_emit_truthy(state, before_end, &condition)) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BF,
        MIR_new_label_op(state->compiler->context, done),
        MIR_new_reg_op(state->compiler->context, condition)));
    MIR_reg_t element = mvp_generic_emit_runtime_binary(state, "mvp_op_member_get",
        iterable, index, &ok);
    if (!ok || !element) return 0;
    if (array_pattern) {
        if (!mvp_generic_assign_pattern(state, loop->left, element, &ok)) return 0;
    } else if (!binding || !mvp_generic_emit_binding_write(state, binding, element, &ok) ||
            !ok) {
        return 0;
    }
    if (!mvp_generic_push_control(state, done, advance)) return 0;
    int body_ok = mvp_generic_emit_statement(state, loop->body, out_last);
    mvp_generic_pop_control(state);
    if (!body_ok) return 0;
    mvp_generic_emit(state, advance);
    MIR_reg_t one = mvp_generic_emit_constant(state, mvp_value_from_number(1.0), "one", &ok);
    MIR_reg_t next = mvp_generic_emit_runtime_binary(state, "mvp_op_add", index, one, &ok);
    if (!ok || !one || !next) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, index),
        MIR_new_reg_op(state->compiler->context, next)));
    if (!mvp_generic_pin(state, index)) return 0;
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
        MIR_new_label_op(state->compiler->context, test)));
    mvp_generic_emit(state, done);
    return 1;
}

static int mvp_generic_emit_switch(MvpGenericState* state, AstMatchNode* switched,
        MIR_reg_t* out_last) {
    if (!state || !switched || !switched->discriminant) return 0;
    MIR_label_t labels[128] = {};
    AstMatchArm* cases[128] = {};
    size_t case_count = 0;
    size_t default_index = SIZE_MAX;
    for (AstNode* node = switched->cases; node; node = node->next) {
        if (node->node_type != AST_NODE_MATCH_ARM || case_count >=
                sizeof(cases) / sizeof(cases[0])) return 0;
        cases[case_count] = (AstMatchArm*)node;
        labels[case_count] = MIR_new_label(state->compiler->context);
        if (!labels[case_count]) return 0;
        if (!cases[case_count]->test) default_index = case_count;
        case_count++;
    }
    MIR_label_t done = MIR_new_label(state->compiler->context);
    int ok = 1;
    MIR_reg_t discriminant = mvp_generic_emit_expression(state, switched->discriminant, &ok);
    if (!ok || !discriminant || !done) return 0;
    for (size_t index = 0; index < case_count; index++) {
        if (!cases[index]->test) continue;
        MIR_reg_t test = mvp_generic_emit_expression(state, cases[index]->test, &ok);
        MIR_reg_t equal = !ok || !test ? 0 : mvp_generic_emit_runtime_binary(state,
            "mvp_op_strict_equal", discriminant, test, &ok);
        MIR_reg_t condition = 0;
        if (!ok || !equal || !mvp_generic_emit_truthy(state, equal, &condition)) return 0;
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BT,
            MIR_new_label_op(state->compiler->context, labels[index]),
            MIR_new_reg_op(state->compiler->context, condition)));
    }
    mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
        MIR_new_label_op(state->compiler->context,
            default_index == SIZE_MAX ? done : labels[default_index])));
    if (!mvp_generic_push_control(state, done, 0)) return 0;
    for (size_t index = 0; index < case_count; index++) {
        mvp_generic_emit(state, labels[index]);
        if (!mvp_generic_emit_statements(state, cases[index]->consequent, out_last)) {
            mvp_generic_pop_control(state);
            return 0;
        }
    }
    mvp_generic_pop_control(state);
    mvp_generic_emit(state, done);
    return 1;
}

static int mvp_generic_emit_statement(MvpGenericState* state, AstNode* node,
        MIR_reg_t* out_last) {
    if (!node) return 1;
    state->compiler->last_node = node;
    switch (node->node_type) {
    case AST_NODE_BLOCK:
        return mvp_generic_emit_statements(state, ((AstBlockNode*)node)->statements, out_last);
    case AST_NODE_EXPR_STMT: {
        int ok = 1;
        MIR_reg_t value = mvp_generic_emit_expression(state,
            ((AstExprStmtNode*)node)->expression, &ok);
        if (ok && value && out_last) *out_last = value;
        return ok;
    }
    case AST_NODE_ASSIGN: {
        int ok = 1;
        MIR_reg_t value = mvp_generic_emit_expression(state, node, &ok);
        if (ok && value && out_last) *out_last = value;
        return ok;
    }
    case AST_NODE_LITERAL: {
        int ok = 1;
        MIR_reg_t value = mvp_generic_emit_expression(state, node, &ok);
        if (ok && value && out_last) *out_last = value;
        return ok;
    }
    case AST_NODE_VAR_STAM:
        return mvp_generic_emit_variable(state, (AstVarDeclNode*)node);
    case AST_NODE_FUNC: {
        AstFuncNode* declaration = (AstFuncNode*)node;
        MvpGenericFunction* function = mvp_generic_find_function_ast(state->compiler,
            declaration);
        int ok = 1;
        MIR_reg_t callable = mvp_generic_emit_function_closure(state, function, &ok);
        MIR_reg_t local = mvp_generic_new_register(state, "function_local");
        if (!ok || !callable || !local || !declaration->name || !mvp_generic_bind(state,
                declaration->name, local)) return 0;
        MvpGenericBinding* binding = mvp_generic_lookup_binding(state, declaration->name);
        if (!binding || !mvp_generic_emit_binding_write(state, binding, callable, &ok)) return 0;
        if (out_last) *out_last = callable;
        return ok;
    }
    case AST_NODE_CLASS: {
        int ok = 1;
        MIR_reg_t value = mvp_generic_emit_class(state, (AstClassNode*)node, &ok);
        if (ok && value && out_last) *out_last = value;
        return ok;
    }
    case AST_NODE_LOOP:
        return mvp_generic_emit_loop(state, (AstLoopControlNode*)node, out_last);
    case AST_NODE_FOR_OF_STAM:
        return mvp_generic_emit_for_of(state, (AstForOfNode*)node, out_last, 0);
    case AST_NODE_FOR_IN_STAM:
        return mvp_generic_emit_for_of(state, (AstForOfNode*)node, out_last, 1);
    case AST_NODE_MATCH_EXPR:
        return mvp_generic_emit_switch(state, (AstMatchNode*)node, out_last);
    case AST_NODE_BREAK_STAM:
        return mvp_generic_emit_control_jump(state, (AstBreakContinueNode*)node, 0);
    case AST_NODE_CONTINUE_STAM:
        return mvp_generic_emit_control_jump(state, (AstBreakContinueNode*)node, 1);
    case JS_AST_NODE_LABELED_STATEMENT: {
        JsLabeledStatementNode* labeled = (JsLabeledStatementNode*)node;
        MIR_label_t done = MIR_new_label(state->compiler->context);
        if (!done || !labeled->body || !mvp_generic_push_labeled_control(state, done, 0,
                labeled->label, labeled->label_len)) return 0;
        int body_ok = mvp_generic_emit_statement(state, (AstNode*)labeled->body, out_last);
        mvp_generic_pop_control(state);
        if (!body_ok) return 0;
        mvp_generic_emit(state, done);
        return 1;
    }
    case AST_NODE_RETURN_STAM: {
        int ok = 1;
        MIR_reg_t value = ((AstReturnNode*)node)->value
            ? mvp_generic_emit_expression(state, ((AstReturnNode*)node)->value, &ok)
            : mvp_generic_emit_constant(state, mvp_value_undefined(), "undefined", &ok);
        return ok && value && mvp_generic_emit_leave_and_return(state, value);
    }
    case AST_NODE_RAISE_STAM: {
        int ok = 1;
        AstReturnNode* raised = (AstReturnNode*)node;
        MIR_reg_t value = raised->value ? mvp_generic_emit_expression(state, raised->value, &ok)
            : mvp_generic_emit_constant(state, mvp_value_undefined(), "undefined", &ok);
        MIR_reg_t result = mvp_generic_new_register(state, "throw_result");
        MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "value", 0}};
        MIR_op_t operands[2] = {
            MIR_new_reg_op(state->compiler->context, state->execution),
            MIR_new_reg_op(state->compiler->context, value),
        };
        if (!ok || !value || !result || !mvp_generic_call_import(state, "mvp_op_throw",
                MIR_T_I64, result, 2, arguments, operands)) return 0;
        return mvp_generic_emit_leave_and_return(state, result);
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* conditional = (AstIfNode*)node;
        int ok = 1;
        MIR_reg_t test_value = mvp_generic_emit_expression(state, conditional->test, &ok);
        MIR_reg_t condition = 0;
        if (!ok || !test_value || !mvp_generic_emit_truthy(state, test_value, &condition)) return 0;
        MIR_label_t alternate = MIR_new_label(state->compiler->context);
        MIR_label_t done = MIR_new_label(state->compiler->context);
        if (!alternate || !done) return 0;
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_BF,
            MIR_new_label_op(state->compiler->context, alternate),
            MIR_new_reg_op(state->compiler->context, condition)));
        if (!mvp_generic_emit_statement(state, conditional->consequent, out_last)) return 0;
        mvp_generic_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
            MIR_new_label_op(state->compiler->context, done)));
        mvp_generic_emit(state, alternate);
        if (conditional->alternate && !mvp_generic_emit_statement(state,
                conditional->alternate, out_last)) return 0;
        mvp_generic_emit(state, done);
        return 1;
    }
    default:
        mvp_generic_set_node_failure("statement", node);
        return 0;
    }
}

static size_t mvp_generic_count_nodes(AstNode* node) {
    size_t count = 0;
    for (AstNode* current = node; current; current = current->next) {
        count++;
        switch (current->node_type) {
        case AST_NODE_BLOCK:
            count += mvp_generic_count_nodes(((AstBlockNode*)current)->statements);
            break;
        case JS_AST_NODE_LABELED_STATEMENT:
            count += mvp_generic_count_nodes((AstNode*)((JsLabeledStatementNode*)current)->body);
            break;
        case AST_NODE_EXPR_STMT:
            count += mvp_generic_count_nodes(((AstExprStmtNode*)current)->expression);
            break;
        case AST_NODE_LITERAL:
        case AST_NODE_IDENT:
            break;
        case AST_NODE_BINARY: {
            AstBinaryNode* binary = (AstBinaryNode*)current;
            count += mvp_generic_count_nodes(binary->left);
            count += mvp_generic_count_nodes(binary->right);
            break;
        }
        case AST_NODE_UNARY:
        case AST_NODE_SPREAD:
            count += mvp_generic_count_nodes(((AstUnaryNode*)current)->operand);
            break;
        case AST_NODE_ASSIGN: {
            AstAssignNode* assignment = (AstAssignNode*)current;
            count += mvp_generic_count_nodes(assignment->left);
            count += mvp_generic_count_nodes(assignment->right);
            break;
        }
        case AST_NODE_CALL_EXPR: {
            AstCallNode* call = (AstCallNode*)current;
            count += mvp_generic_count_nodes(call->callee);
            count += mvp_generic_count_nodes(call->arguments);
            break;
        }
        case AST_NODE_NEW_EXPR: {
            AstCallNode* call = (AstCallNode*)current;
            count += mvp_generic_count_nodes(call->callee);
            count += mvp_generic_count_nodes(call->arguments);
            break;
        }
        case AST_NODE_MEMBER_EXPR: {
            AstFieldNode* member = (AstFieldNode*)current;
            count += mvp_generic_count_nodes(member->object);
            count += mvp_generic_count_nodes(member->field);
            break;
        }
        case AST_NODE_ARRAY:
            count += mvp_generic_count_nodes(((AstArrayNode*)current)->elements);
            break;
        case AST_NODE_MAP:
            count += mvp_generic_count_nodes(((AstMapNode*)current)->properties);
            break;
        case AST_NODE_PROPERTY: {
            AstPropertyNode* property = (AstPropertyNode*)current;
            count += mvp_generic_count_nodes(property->key);
            count += mvp_generic_count_nodes(property->value);
            break;
        }
        case AST_NODE_VAR_STAM:
            count += mvp_generic_count_nodes(((AstVarDeclNode*)current)->declarations);
            break;
        case AST_NODE_VARIABLE_DECLARATOR: {
            AstDeclaratorNode* declarator = (AstDeclaratorNode*)current;
            count += mvp_generic_count_nodes(declarator->id);
            count += mvp_generic_count_nodes(declarator->init);
            break;
        }
        case AST_NODE_LOOP: {
            AstLoopControlNode* loop = (AstLoopControlNode*)current;
            count += mvp_generic_count_nodes(loop->init);
            count += mvp_generic_count_nodes(loop->test);
            count += mvp_generic_count_nodes(loop->update);
            count += mvp_generic_count_nodes(loop->body);
            break;
        }
        case AST_NODE_FOR_OF_STAM:
        case AST_NODE_FOR_IN_STAM: {
            AstForOfNode* loop = (AstForOfNode*)current;
            count += mvp_generic_count_nodes(loop->left);
            count += mvp_generic_count_nodes(loop->init);
            count += mvp_generic_count_nodes(loop->right);
            count += mvp_generic_count_nodes(loop->body);
            break;
        }
        case AST_NODE_IF_EXPR: {
            AstIfNode* conditional = (AstIfNode*)current;
            count += mvp_generic_count_nodes(conditional->test);
            count += mvp_generic_count_nodes(conditional->consequent);
            count += mvp_generic_count_nodes(conditional->alternate);
            break;
        }
        case AST_NODE_CONDITIONAL_EXPR: {
            AstIfNode* conditional = (AstIfNode*)current;
            count += mvp_generic_count_nodes(conditional->test);
            count += mvp_generic_count_nodes(conditional->consequent);
            count += mvp_generic_count_nodes(conditional->alternate);
            break;
        }
        case AST_NODE_MATCH_EXPR: {
            AstMatchNode* switched = (AstMatchNode*)current;
            count += mvp_generic_count_nodes(switched->discriminant);
            count += mvp_generic_count_nodes(switched->cases);
            break;
        }
        case AST_NODE_MATCH_ARM: {
            AstMatchArm* match = (AstMatchArm*)current;
            count += mvp_generic_count_nodes(match->test);
            count += mvp_generic_count_nodes(match->consequent);
            break;
        }
        case AST_NODE_RETURN_STAM:
            count += mvp_generic_count_nodes(((AstReturnNode*)current)->value);
            break;
        case AST_NODE_CLASS: {
            AstClassNode* class_node = (AstClassNode*)current;
            count += mvp_generic_count_nodes(class_node->superclass);
            count += mvp_generic_count_nodes(class_node->body);
            break;
        }
        default:
            break;
        }
    }
    return count;
}

static size_t mvp_generic_parameter_count(AstFuncNode* function) {
    size_t count = 0;
    for (AstNode* parameter = function ? function->params : NULL; parameter;
            parameter = parameter->next) {
        AstNode* name = parameter;
        if (parameter->node_type == AST_NODE_ASSIGN_PATTERN) name =
            ((AstAssignNode*)parameter)->left;
        if (name && name->node_type == AST_NODE_REST_ELEMENT) name =
            ((AstUnaryNode*)name)->operand;
        if (!name || name->node_type != AST_NODE_IDENT) return SIZE_MAX;
        count++;
    }
    return count;
}

static String* mvp_generic_parameter_name(AstNode* parameter) {
    AstNode* name = parameter;
    if (parameter && parameter->node_type == AST_NODE_ASSIGN_PATTERN) name =
        ((AstAssignNode*)parameter)->left;
    if (name && name->node_type == AST_NODE_REST_ELEMENT) name = ((AstUnaryNode*)name)->operand;
    return name && name->node_type == AST_NODE_IDENT ? ((AstIdentNode*)name)->name : NULL;
}

static int mvp_generic_function_has_rest_parameter(AstFuncNode* function) {
    AstNode* parameter = function ? function->params : NULL;
    if (!parameter) return 0;
    while (parameter->next) parameter = parameter->next;
    if (parameter->node_type == AST_NODE_ASSIGN_PATTERN) parameter =
        ((AstAssignNode*)parameter)->left;
    return parameter && parameter->node_type == AST_NODE_REST_ELEMENT;
}

static AstNode* mvp_generic_parameter_default(AstNode* parameter) {
    return parameter && parameter->node_type == AST_NODE_ASSIGN_PATTERN
        ? ((AstAssignNode*)parameter)->right : NULL;
}

typedef struct MvpGenericNestedFunctionCount {
    size_t count;
} MvpGenericNestedFunctionCount;

static void mvp_generic_count_nested_callable(JsAstNode* node, void* opaque) {
    MvpGenericNestedFunctionCount* count = (MvpGenericNestedFunctionCount*)opaque;
    if (!node || !count) return;
    if (node->node_type == AST_NODE_ARROW_FUNC || node->node_type == AST_NODE_FUNC ||
            node->node_type == AST_NODE_FUNC_EXPR) count->count++;
    js_ast_visit_children(node, mvp_generic_count_nested_callable, opaque);
}

static size_t mvp_generic_count_nested_callables(AstNode* node) {
    MvpGenericNestedFunctionCount count = {};
    if (node) js_ast_visit_children((JsAstNode*)node, mvp_generic_count_nested_callable,
        &count);
    return count.count;
}

static int mvp_generic_emit_frame_enter(MvpGenericState* state) {
    MIR_reg_t values = mvp_generic_new_register(state, "root_values");
    MIR_var_t arguments[2] = {{MIR_T_P, "execution", 0}, {MIR_T_I64, "roots", 0}};
    MIR_op_t operands[2] = {
        MIR_new_reg_op(state->compiler->context, state->execution),
        MIR_new_int_op(state->compiler->context, (int64_t)state->root_budget),
    };
    if (!values || !mvp_generic_call_import(state, "mvp_execution_enter_frame", MIR_T_P,
            values, 2, arguments, operands)) return 0;
    state->root_values = values;
    return 1;
}

static int mvp_generic_compile_function(MvpGenericCompiler* compiler,
        MvpGenericFunction* function) {
    if (!compiler || !function || function->parameter_count > 64) return 0;
    if (function->unavailable) return 1;
    MIR_var_t parameters[67] = {};
    parameters[0] = (MIR_var_t){MIR_T_P, "mvp_exec", 0};
    parameters[1] = (MIR_var_t){MIR_T_I64, "mvp_receiver", 0};
    size_t index = 2;
    if (function->is_closure) {
        parameters[index++] = (MIR_var_t){MIR_T_I64, "mvp_capture", 0};
    }
    for (AstNode* parameter = function->ast->params; parameter; parameter = parameter->next) {
        String* name = mvp_generic_parameter_name(parameter);
        if (!name || index >= sizeof(parameters) / sizeof(parameters[0])) return 0;
        parameters[index++] = (MIR_var_t){MIR_T_I64, name->chars, 0};
    }
    MIR_type_t return_type = MIR_T_I64;
    MIR_item_t item = MIR_new_func_arr(compiler->context, function->name, 1, &return_type,
        function->parameter_count + (function->is_closure ? 3 : 2), parameters);
    if (!item) return 0;
    function->item = item;
    MvpGenericState state = {};
    state.compiler = compiler;
    state.function_item = item;
    state.function = MIR_get_item_func(compiler->context, item);
    state.root_budget = mvp_generic_count_nodes(function->ast->body) * 8 +
        function->parameter_count + 32;
    state.execution = MIR_reg(compiler->context, "mvp_exec", state.function);
    state.receiver = MIR_reg(compiler->context, "mvp_receiver", state.function);
    state.owner_class = function->owner_class;
    state.is_closure = function->is_closure;
    state.captures_lexical_this = function->captures_lexical_this;
    state.is_static_method = function->is_static_method;
    state.capture_environment = function->is_closure ? MIR_reg(compiler->context,
        "mvp_capture", state.function) : 0;
    state.error_exit = MIR_new_label(compiler->context);
    int ok = state.function && state.execution && state.receiver && state.error_exit &&
        mvp_generic_emit_frame_enter(&state);
    if (!ok) mvp_generic_set_failure("function entry", 0);
    if (ok && function->needs_environment) {
        state.environment = mvp_generic_emit_environment_new(&state, &ok);
        MIR_reg_t lexical_this = state.receiver;
        if (!function->is_closure || !function->captures_lexical_this) {
            if (!mvp_generic_emit_environment_store(&state, "this", lexical_this)) ok = 0;
        } else {
            MIR_reg_t this_key = mvp_generic_emit_bytes(&state, "this", 4, &ok);
            lexical_this = mvp_generic_emit_runtime_binary(&state, "mvp_op_member_get",
                state.capture_environment, this_key, &ok);
            if (!ok || !this_key || !lexical_this || !mvp_generic_emit_environment_store(&state,
                    "this", lexical_this)) ok = 0;
        }
    }
    for (size_t parameter_index = 0; ok && parameter_index < function->parameter_count;
            parameter_index++) {
        AstNode* parameter = function->ast->params;
        for (size_t skip = 0; skip < parameter_index; skip++) parameter = parameter->next;
        size_t parameter_offset = function->is_closure ? 3 : 2;
        MIR_reg_t reg = MIR_reg(compiler->context, parameters[parameter_index + parameter_offset].name,
            state.function);
        MvpGenericBinding* binding = NULL;
        String* name = mvp_generic_parameter_name(parameter);
        ok = reg && name && mvp_generic_bind(&state, name, reg);
        if (ok) binding = mvp_generic_lookup_binding(&state, name);
        if (ok) {
            int write_ok = 1;
            (void)mvp_generic_emit_binding_write(&state, binding, reg, &write_ok);
            ok = write_ok;
        }
        AstNode* default_value = mvp_generic_parameter_default(parameter);
        if (ok && default_value) {
            int default_ok = 1;
            MIR_reg_t undefined = mvp_generic_emit_constant(&state, mvp_value_undefined(),
                "undefined", &default_ok);
            MIR_reg_t is_undefined = !default_ok || !undefined ? 0
                : mvp_generic_emit_runtime_binary(&state, "mvp_op_strict_equal", reg,
                    undefined, &default_ok);
            MIR_reg_t use_default = 0;
            MIR_label_t keep_argument = MIR_new_label(compiler->context);
            if (!default_ok || !is_undefined || !keep_argument ||
                    !mvp_generic_emit_truthy(&state, is_undefined, &use_default)) {
                ok = 0;
            } else {
                mvp_generic_emit(&state, MIR_new_insn(compiler->context, MIR_BF,
                    MIR_new_label_op(compiler->context, keep_argument),
                    MIR_new_reg_op(compiler->context, use_default)));
                MIR_reg_t value = mvp_generic_emit_expression(&state, default_value,
                    &default_ok);
                if (!default_ok || !value || !mvp_generic_emit_binding_write(&state, binding,
                        value, &default_ok)) {
                    ok = 0;
                }
                mvp_generic_emit(&state, keep_argument);
            }
        }
        if (!ok) mvp_generic_set_failure("function parameter", parameter_index);
    }
    MIR_reg_t last = 0;
    if (ok && function->ast->body && function->ast->body->node_type == AST_NODE_BLOCK) {
        ok = mvp_generic_emit_statements(&state, ((AstBlockNode*)function->ast->body)->statements,
            &last);
    } else if (ok && function->is_closure && function->ast->body) {
        last = mvp_generic_emit_expression(&state, function->ast->body, &ok);
        if (ok && last) ok = mvp_generic_emit_leave_and_return(&state, last);
    } else {
        ok = 0;
    }
    if (ok) {
        MIR_reg_t undefined = mvp_generic_emit_constant(&state, mvp_value_undefined(),
            "undefined", &ok);
        if (ok) ok = mvp_generic_emit_leave_and_return(&state, undefined);
    }
    if (ok) ok = mvp_generic_emit_error_exit(&state);
    MIR_finish_func(compiler->context);
    return ok;
}

static int mvp_generic_compile_main(MvpGenericCompiler* compiler, AstScript* script,
        MIR_item_t* out_main) {
    MIR_var_t parameters[2] = {{MIR_T_P, "mvp_exec", 0}, {MIR_T_I64, "mvp_receiver", 0}};
    MIR_type_t return_type = MIR_T_I64;
    MIR_item_t item = MIR_new_func_arr(compiler->context, "mvp_generic_main", 1,
        &return_type, 2, parameters);
    if (!item) return 0;
    MvpGenericState state = {};
    state.compiler = compiler;
    state.function_item = item;
    state.function = MIR_get_item_func(compiler->context, item);
    state.root_budget = mvp_generic_count_nodes(script->body) * 8 + 32;
    state.execution = MIR_reg(compiler->context, "mvp_exec", state.function);
    state.receiver = MIR_reg(compiler->context, "mvp_receiver", state.function);
    state.is_main = 1;
    state.error_exit = MIR_new_label(compiler->context);
    int ok = state.function && state.execution && state.receiver && state.error_exit &&
        mvp_generic_emit_frame_enter(&state);
    if (ok && mvp_generic_count_nested_callables((AstNode*)script)) {
        state.environment = mvp_generic_emit_environment_new(&state, &ok);
        if (ok && !mvp_generic_emit_environment_store(&state, "this", state.receiver)) ok = 0;
    }
    // Function declarations bind before ordinary script evaluation.  Their
    // values are therefore available to exports and calls which precede the
    // declaration text, while the declaration itself has no run-time action.
    for (AstNode* statement = script->body; ok && statement; statement = statement->next) {
        if (statement->node_type != AST_NODE_FUNC) continue;
        MvpGenericFunction* function = mvp_generic_find_function_ast(compiler,
            (AstFuncNode*)statement);
        MIR_reg_t value = mvp_generic_emit_function_value(&state, function, &ok);
        if (!ok || !value || !mvp_generic_emit_global_set(&state,
                ((AstFuncNode*)statement)->name, value)) {
            ok = 0;
        }
    }
    MIR_reg_t last = 0;
    for (AstNode* statement = script->body; ok && statement; statement = statement->next) {
        if (statement->node_type == AST_NODE_FUNC) continue;
        ok = mvp_generic_emit_statement(&state, statement, &last);
    }
    if (ok && !last) last = mvp_generic_emit_constant(&state, mvp_value_undefined(),
        "undefined", &ok);
    if (ok) ok = mvp_generic_emit_leave_and_return(&state, last);
    if (ok) ok = mvp_generic_emit_error_exit(&state);
    MIR_finish_func(compiler->context);
    *out_main = item;
    return ok;
}

static size_t mvp_generic_class_method_count(AstClassNode* class_node) {
    if (!class_node || !class_node->body || class_node->body->node_type != AST_NODE_BLOCK) {
        return 0;
    }
    size_t count = 0;
    for (AstNode* member = ((AstBlockNode*)class_node->body)->statements; member;
            member = member->next) {
        if (member->node_type == AST_NODE_METHOD) count++;
    }
    return count;
}

static int mvp_generic_add_function(MvpGenericCompiler* compiler, AstFuncNode* function,
        AstClassNode* owner_class, int is_method, int is_closure, size_t* index) {
    if (!compiler || !function || !index || (!function->name && !is_closure) ||
            *index >= compiler->function_count) return 0;
    size_t parameter_count = mvp_generic_parameter_count(function);
    if (parameter_count == SIZE_MAX || (!is_method && parameter_count > 64)) return 0;
    MvpGenericFunction* entry = &compiler->functions[*index];
    entry->ast = function;
    entry->owner_class = owner_class;
    entry->parameter_count = parameter_count;
    entry->descriptor.parameter_count = parameter_count;
    entry->has_rest_parameter = mvp_generic_function_has_rest_parameter(function);
    entry->is_method = is_method;
    entry->is_static_method = is_method && ((AstMethodNode*)function)->static_method;
    entry->is_closure = is_closure;
    entry->captures_lexical_this = is_closure && function->node_type == AST_NODE_ARROW_FUNC;
    entry->unavailable = is_method && parameter_count > 9;
    entry->needs_environment = mvp_generic_count_nested_callables(function->body) > 0;
    entry->descriptor.unavailable = entry->unavailable;
    entry->descriptor.captures_receiver = entry->captures_lexical_this;
    entry->descriptor.captures_environment = is_closure;
    int written = snprintf(entry->name, sizeof(entry->name), "mvp_gfn_%zu", *index);
    if (written <= 0 || (size_t)written >= sizeof(entry->name)) return 0;
    if (!entry->unavailable && !is_closure) {
        entry->forward = MIR_new_forward(compiler->context, entry->name);
        if (!entry->forward) return 0;
    }
    (*index)++;
    return 1;
}

typedef struct MvpGenericNestedFunctionAdd {
    MvpGenericCompiler* compiler;
    size_t* index;
    int ok;
} MvpGenericNestedFunctionAdd;

static void mvp_generic_add_nested_callable(JsAstNode* node, void* opaque) {
    MvpGenericNestedFunctionAdd* add = (MvpGenericNestedFunctionAdd*)opaque;
    if (!node || !add || !add->ok) return;
    if ((node->node_type == AST_NODE_ARROW_FUNC || node->node_type == AST_NODE_FUNC ||
            node->node_type == AST_NODE_FUNC_EXPR) &&
            !mvp_generic_find_function_ast(add->compiler, (AstFuncNode*)node)) {
        add->ok = mvp_generic_add_function(add->compiler, (AstFuncNode*)node, NULL, 0, 1,
            add->index);
        if (!add->ok) return;
    }
    js_ast_visit_children(node, mvp_generic_add_nested_callable, opaque);
}

int mvp_execute_generic_ast(const AstNode* root, MvpExecutionResult* out) {
    if (!root || root->node_type != AST_SCRIPT || !out) return 0;
    mvp_generic_failure[0] = '\0';
    AstScript* script = (AstScript*)root;
    size_t function_count = 0;
    for (AstNode* node = script->body; node; node = node->next) {
        if (node->node_type == AST_NODE_FUNC) function_count++;
        else if (node->node_type == AST_NODE_CLASS) {
            function_count += mvp_generic_class_method_count((AstClassNode*)node);
        }
    }
    size_t top_level_function_count = 0;
    for (AstNode* node = script->body; node; node = node->next) {
        if (node->node_type == AST_NODE_FUNC) top_level_function_count++;
    }
    size_t nested_callable_count = mvp_generic_count_nested_callables((AstNode*)script);
    if (nested_callable_count < top_level_function_count) return 0;
    function_count += nested_callable_count - top_level_function_count;
    MvpGenericFunction* functions = function_count ? (MvpGenericFunction*)mem_calloc(
        function_count, sizeof(MvpGenericFunction), MEM_CAT_JS_RUNTIME) : NULL;
    if (function_count && !functions) return 0;
    MvpGenericCompiler compiler = {};
    compiler.context = MIR_init();
    compiler.functions = functions;
    compiler.function_count = function_count;
    compiler.script = script;
    if (!compiler.context) {
        mvp_generic_set_failure("MIR initialization", 0);
        if (functions) mem_free(functions);
        return 0;
    }
    compiler.module = MIR_new_module(compiler.context, "js_mvp_generic");
    int ok = compiler.module != NULL;
    if (ok && !mvp_generic_collect_globals(&compiler, script)) {
        mvp_generic_set_failure("global declarations", 0);
        ok = 0;
    }
    size_t index = 0;
    for (AstNode* node = script->body; ok && node; node = node->next) {
        if (node->node_type == AST_NODE_FUNC) {
            if (!mvp_generic_add_function(&compiler, (AstFuncNode*)node, NULL, 0, 0, &index)) {
                mvp_generic_set_failure("function declaration", index);
                ok = 0;
            }
            continue;
        }
        if (node->node_type != AST_NODE_CLASS) continue;
        AstClassNode* class_node = (AstClassNode*)node;
        if (!class_node->body || class_node->body->node_type != AST_NODE_BLOCK) {
            mvp_generic_set_failure("class declaration", index);
            ok = 0;
            break;
        }
        for (AstNode* member = ((AstBlockNode*)class_node->body)->statements;
                ok && member; member = member->next) {
            if (member->node_type == AST_NODE_FIELD) continue;
            if (member->node_type != AST_NODE_METHOD || !mvp_generic_add_function(&compiler,
                    (AstFuncNode*)member, class_node, 1, 0, &index)) {
                mvp_generic_set_failure("class method", index);
                ok = 0;
            }
        }
    }
    MvpGenericNestedFunctionAdd add = {&compiler, &index, ok};
    if (ok) js_ast_visit_children((JsAstNode*)script, mvp_generic_add_nested_callable, &add);
    if (!add.ok) {
        mvp_generic_set_failure("nested function inventory", index);
        ok = 0;
    }
    if (ok && index != function_count) {
        mvp_generic_set_failure("function inventory", index);
        ok = 0;
    }
    if (ok && function_count) {
        compiler.numeric_programs = (MvpNumericProgram**)mem_calloc(function_count,
            sizeof(MvpNumericProgram*), MEM_CAT_JS_RUNTIME);
        if (compiler.numeric_programs) {
            for (size_t function_index = 0; function_index < function_count; function_index++) {
                if (functions[function_index].is_closure) continue;
                MvpNumericProgram* program = mvp_numeric_program_create(functions[function_index].ast);
                if (program) compiler.numeric_programs[compiler.numeric_program_count++] = program;
            }
        }
    }
    for (size_t function_index = 0; ok && function_index < function_count; function_index++) {
        ok = mvp_generic_compile_function(&compiler, &functions[function_index]);
        if (!ok) {
            if (compiler.last_node) mvp_generic_set_function_node_failure(
                &functions[function_index], compiler.last_node);
            else mvp_generic_set_failure("function body", function_index);
        }
    }
    MIR_item_t main_item = NULL;
    if (ok) {
        ok = mvp_generic_compile_main(&compiler, script, &main_item);
        if (!ok) {
            if (compiler.last_node) mvp_generic_set_node_failure("script body", compiler.last_node);
            else mvp_generic_set_failure("script body", 0);
        }
    }
    if (compiler.module) MIR_finish_module(compiler.context);
    if (ok) {
        MIR_load_module(compiler.context, compiler.module);
        MIR_gen_init(compiler.context);
        MIR_gen_set_optimize_level(compiler.context, 2);
        MIR_link(compiler.context, MIR_set_gen_interface, mvp_generic_import_resolver);
        for (size_t function_index = 0; function_index < function_count; function_index++) {
            functions[function_index].descriptor.entry = functions[function_index].item
                ? functions[function_index].item->addr : NULL;
            if (!functions[function_index].descriptor.entry &&
                    !functions[function_index].descriptor.unavailable) {
                mvp_generic_set_failure("function native entry", function_index);
                ok = 0;
            }
        }
        MvpGenericEntry entry = main_item ? (MvpGenericEntry)main_item->addr : NULL;
        MvpExecution* execution = entry ? (MvpExecution*)mem_calloc(1, sizeof(MvpExecution),
            MEM_CAT_JS_RUNTIME) : NULL;
        if (!entry || !execution) {
            mvp_generic_set_failure("native entry", 0);
            ok = 0;
        } else {
            mvp_execution_init(execution, 1);
            if (mvp_execution_has_error(execution)) {
                mvp_generic_set_failure("execution setup", 0);
                mvp_execution_destroy(execution);
                mem_free(execution);
                ok = 0;
            } else {
                MvpValue value = {entry(execution, mvp_value_undefined().bits)};
                mvp_execution_set_root(execution, 0, value.bits);
                if (mvp_execution_has_error(execution)) {
                    mvp_generic_set_execution_failure(execution);
                    mvp_execution_destroy(execution);
                    mem_free(execution);
                    ok = 0;
                } else {
                    out->value = value;
                    out->execution = execution;
                    ok = 1;
                }
            }
        }
        MIR_gen_finish(compiler.context);
    }
    MIR_finish(compiler.context);
    for (size_t numeric_index = 0; numeric_index < compiler.numeric_program_count;
            numeric_index++) {
        mvp_numeric_program_destroy(compiler.numeric_programs[numeric_index]);
    }
    if (compiler.numeric_programs) mem_free(compiler.numeric_programs);
    if (functions) mem_free(functions);
    return ok;
}
