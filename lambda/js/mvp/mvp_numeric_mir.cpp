#include "mvp.h"

#include "../js_ast.hpp"
#include "../../runtime/ast-core.hpp"
#include "../../mir/mir.h"
#include "../../mir/mir-gen.h"
#include "../../../lib/mem.h"

#include <stdio.h>
#include <string.h>

typedef double (*MvpNumericMirEntry)(void);

typedef struct MvpNumericFunction {
    AstFuncNode* ast;
    MIR_item_t forward;
    MIR_item_t item;
    char name[32];
    size_t parameter_count;
} MvpNumericFunction;

typedef struct MvpNumericCompiler {
    MIR_context_t context;
    MIR_module_t module;
    MvpNumericFunction* functions;
    size_t function_count;
    uint32_t next_register;
} MvpNumericCompiler;

struct MvpNumericProgram {
    MvpNumericCompiler compiler;
    MvpNumericFunction function;
    void* entry;
    int generator_initialized;
};

typedef struct MvpNumericBinding {
    String* name;
    MIR_reg_t reg;
    int is_boolean;
} MvpNumericBinding;

typedef struct MvpNumericFunctionState {
    MvpNumericCompiler* compiler;
    MIR_item_t function_item;
    MIR_func_t function;
    MvpNumericBinding bindings[256];
    size_t binding_count;
} MvpNumericFunctionState;

typedef struct MvpNumericValue {
    MIR_reg_t reg;
    int is_boolean;
} MvpNumericValue;

static void* mvp_numeric_import_resolver(const char* name) {
    (void)name;
    return NULL;
}

static int mvp_numeric_string_equal(const String* left, const String* right) {
    return left && right && left->len == right->len &&
        memcmp(left->chars, right->chars, left->len) == 0;
}

static MvpNumericFunction* mvp_numeric_find_function(MvpNumericCompiler* compiler,
        const String* name) {
    if (!compiler || !name) return NULL;
    for (size_t i = 0; i < compiler->function_count; i++) {
        if (mvp_numeric_string_equal(compiler->functions[i].ast->name, name)) {
            return &compiler->functions[i];
        }
    }
    return NULL;
}

static MIR_reg_t mvp_numeric_new_register(MvpNumericFunctionState* state,
        MIR_type_t type, const char* prefix) {
    char name[48];
    int written = snprintf(name, sizeof(name), "%s_%u", prefix,
        state->compiler->next_register++);
    if (written <= 0 || (size_t)written >= sizeof(name)) return 0;
    return MIR_new_func_reg(state->compiler->context, state->function, type, name);
}

static int mvp_numeric_bind(MvpNumericFunctionState* state, String* name,
        MIR_reg_t reg, int is_boolean) {
    if (!state || !name || !reg) return 0;
    for (size_t i = 0; i < state->binding_count; i++) {
        if (mvp_numeric_string_equal(state->bindings[i].name, name)) {
            state->bindings[i].reg = reg;
            state->bindings[i].is_boolean = is_boolean;
            return 1;
        }
    }
    if (state->binding_count >= sizeof(state->bindings) / sizeof(state->bindings[0])) return 0;
    state->bindings[state->binding_count++] = (MvpNumericBinding){name, reg, is_boolean};
    return 1;
}

static MIR_reg_t mvp_numeric_lookup(const MvpNumericFunctionState* state,
        const String* name) {
    if (!state || !name) return 0;
    for (size_t i = 0; i < state->binding_count; i++) {
        if (mvp_numeric_string_equal(state->bindings[i].name, name)) return state->bindings[i].reg;
    }
    return 0;
}

static int mvp_numeric_binding_is_boolean(const MvpNumericFunctionState* state,
        const String* name) {
    if (!state || !name) return 0;
    for (size_t i = 0; i < state->binding_count; i++) {
        if (mvp_numeric_string_equal(state->bindings[i].name, name)) {
            return state->bindings[i].is_boolean;
        }
    }
    return 0;
}

static void mvp_numeric_emit(MvpNumericFunctionState* state, MIR_insn_t instruction) {
    MIR_append_insn(state->compiler->context, state->function_item, instruction);
}

static MvpNumericValue mvp_numeric_emit_expr(MvpNumericFunctionState* state,
        AstNode* node, int* ok);

static MvpNumericValue mvp_numeric_emit_number(MvpNumericFunctionState* state,
        double number, int* ok) {
    MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_D, "number");
    if (!result) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_DMOV,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_double_op(state->compiler->context, number)));
    return (MvpNumericValue){result, 0};
}

static MvpNumericValue mvp_numeric_emit_boolean(MvpNumericFunctionState* state,
        int boolean, int* ok) {
    MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_I64, "boolean");
    if (!result) {
        *ok = 0;
        return (MvpNumericValue){0, 1};
    }
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_MOV,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_int_op(state->compiler->context, boolean ? 1 : 0)));
    return (MvpNumericValue){result, 1};
}

static MvpNumericValue mvp_numeric_emit_bitwise(MvpNumericFunctionState* state,
        MvpNumericValue left, MvpNumericValue right, Operator operation, int* ok) {
    if (!*ok || !left.reg || !right.reg || left.is_boolean || right.is_boolean) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_insn_code_t opcode = MIR_XORS;
    switch (operation) {
    case OPERATOR_JS_BIT_AND: opcode = MIR_ANDS; break;
    case OPERATOR_JS_BIT_OR: opcode = MIR_ORS; break;
    case OPERATOR_JS_BIT_XOR: opcode = MIR_XORS; break;
    case OPERATOR_JS_LSHIFT: opcode = MIR_LSHS; break;
    case OPERATOR_JS_RSHIFT: opcode = MIR_RSHS; break;
    default:
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_reg_t left_integer = mvp_numeric_new_register(state, MIR_T_I64, "left_integer");
    MIR_reg_t right_integer = mvp_numeric_new_register(state, MIR_T_I64, "right_integer");
    MIR_reg_t integer_result = mvp_numeric_new_register(state, MIR_T_I64, "bitwise");
    MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_D, "bitwise_number");
    if (!left_integer || !right_integer || !integer_result || !result) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_D2I,
        MIR_new_reg_op(state->compiler->context, left_integer),
        MIR_new_reg_op(state->compiler->context, left.reg)));
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_D2I,
        MIR_new_reg_op(state->compiler->context, right_integer),
        MIR_new_reg_op(state->compiler->context, right.reg)));
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, opcode,
        MIR_new_reg_op(state->compiler->context, integer_result),
        MIR_new_reg_op(state->compiler->context, left_integer),
        MIR_new_reg_op(state->compiler->context, right_integer)));
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_I2D,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_reg_op(state->compiler->context, integer_result)));
    return (MvpNumericValue){result, 0};
}

static MvpNumericValue mvp_numeric_emit_binary(MvpNumericFunctionState* state,
        AstBinaryNode* binary, int* ok) {
    MvpNumericValue left = mvp_numeric_emit_expr(state, binary->left, ok);
    MvpNumericValue right = mvp_numeric_emit_expr(state, binary->right, ok);
    if (!*ok || !left.reg || !right.reg) return (MvpNumericValue){0, 0};
    if (binary->op == OPERATOR_JS_BIT_AND || binary->op == OPERATOR_JS_BIT_OR ||
            binary->op == OPERATOR_JS_BIT_XOR || binary->op == OPERATOR_JS_LSHIFT ||
            binary->op == OPERATOR_JS_RSHIFT) {
        return mvp_numeric_emit_bitwise(state, left, right, binary->op, ok);
    }
    if (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR) {
        if (!left.is_boolean || !right.is_boolean) {
            *ok = 0;
            return (MvpNumericValue){0, 0};
        }
        MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_I64, "logical");
        if (!result) {
            *ok = 0;
            return (MvpNumericValue){0, 1};
        }
        mvp_numeric_emit(state, MIR_new_insn(state->compiler->context,
            binary->op == OPERATOR_AND ? MIR_AND : MIR_OR,
            MIR_new_reg_op(state->compiler->context, result),
            MIR_new_reg_op(state->compiler->context, left.reg),
            MIR_new_reg_op(state->compiler->context, right.reg)));
        return (MvpNumericValue){result, 1};
    }
    MIR_insn_code_t opcode = MIR_DADD;
    int is_boolean = 0;
    switch (binary->op) {
    case OPERATOR_ADD: opcode = MIR_DADD; break;
    case OPERATOR_SUB: opcode = MIR_DSUB; break;
    case OPERATOR_MUL: opcode = MIR_DMUL; break;
    case OPERATOR_DIV: opcode = MIR_DDIV; break;
    case OPERATOR_EQ:
    case OPERATOR_JS_STRICT_EQ: opcode = MIR_DEQ; is_boolean = 1; break;
    case OPERATOR_NE:
    case OPERATOR_JS_STRICT_NE: opcode = MIR_DNE; is_boolean = 1; break;
    case OPERATOR_LT: opcode = MIR_DLT; is_boolean = 1; break;
    case OPERATOR_LE: opcode = MIR_DLE; is_boolean = 1; break;
    case OPERATOR_GT: opcode = MIR_DGT; is_boolean = 1; break;
    case OPERATOR_GE: opcode = MIR_DGE; is_boolean = 1; break;
    default:
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    if (left.is_boolean || right.is_boolean) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_reg_t result = mvp_numeric_new_register(state,
        is_boolean ? MIR_T_I64 : MIR_T_D, is_boolean ? "compare" : "binary");
    if (!result) {
        *ok = 0;
        return (MvpNumericValue){0, is_boolean};
    }
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, opcode,
        MIR_new_reg_op(state->compiler->context, result),
        MIR_new_reg_op(state->compiler->context, left.reg),
        MIR_new_reg_op(state->compiler->context, right.reg)));
    return (MvpNumericValue){result, is_boolean};
}

static MvpNumericValue mvp_numeric_emit_call(MvpNumericFunctionState* state,
        AstCallNode* call, int* ok) {
    if (!call || !call->callee || call->callee->node_type != AST_NODE_IDENT) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    String* name = ((AstIdentNode*)call->callee)->name;
    MvpNumericFunction* target = mvp_numeric_find_function(state->compiler, name);
    if (!target || !target->forward) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_op_t arguments[64] = {};
    size_t argument_count = 0;
    for (AstNode* argument = call->arguments; argument; argument = argument->next) {
        if (argument_count >= sizeof(arguments) / sizeof(arguments[0])) {
            *ok = 0;
            return (MvpNumericValue){0, 0};
        }
        MvpNumericValue value = mvp_numeric_emit_expr(state, argument, ok);
        if (!*ok || !value.reg || value.is_boolean) return (MvpNumericValue){0, 0};
        arguments[argument_count++] = MIR_new_reg_op(state->compiler->context, value.reg);
    }
    if (argument_count != target->parameter_count) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_var_t parameters[64] = {};
    for (size_t i = 0; i < argument_count; i++) parameters[i] = (MIR_var_t){MIR_T_D, "arg", 0};
    char prototype_name[48];
    int written = snprintf(prototype_name, sizeof(prototype_name), "mvp_call_%p",
        (void*)call);
    if (written <= 0 || (size_t)written >= sizeof(prototype_name)) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_type_t return_type = MIR_T_D;
    MIR_item_t prototype = MIR_new_proto_arr(state->compiler->context, prototype_name,
        1, &return_type, argument_count, parameters);
    MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_D, "call");
    if (!prototype || !result) {
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    MIR_op_t operands[67] = {};
    operands[0] = MIR_new_ref_op(state->compiler->context, prototype);
    operands[1] = MIR_new_ref_op(state->compiler->context, target->forward);
    operands[2] = MIR_new_reg_op(state->compiler->context, result);
    for (size_t i = 0; i < argument_count; i++) operands[3 + i] = arguments[i];
    mvp_numeric_emit(state, MIR_new_insn_arr(state->compiler->context, MIR_CALL,
        argument_count + 3, operands));
    return (MvpNumericValue){result, 0};
}

static MvpNumericValue mvp_numeric_emit_expr(MvpNumericFunctionState* state,
        AstNode* node, int* ok) {
    if (!node || !*ok) return (MvpNumericValue){0, 0};
    switch (node->node_type) {
    case AST_NODE_LITERAL: {
        AstLiteralNode* literal = (AstLiteralNode*)node;
        if (literal->literal_type == AST_LITERAL_NUMBER) {
            return mvp_numeric_emit_number(state, literal->value.number_value, ok);
        }
        if (literal->literal_type == AST_LITERAL_BOOLEAN) {
            return mvp_numeric_emit_boolean(state, literal->value.boolean_value, ok);
        }
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    case AST_NODE_IDENT: {
        String* name = ((AstIdentNode*)node)->name;
        MIR_reg_t reg = mvp_numeric_lookup(state, name);
        if (!reg) *ok = 0;
        return (MvpNumericValue){reg, mvp_numeric_binding_is_boolean(state, name)};
    }
    case AST_NODE_BINARY:
        return mvp_numeric_emit_binary(state, (AstBinaryNode*)node, ok);
    case AST_NODE_CALL_EXPR:
        return mvp_numeric_emit_call(state, (AstCallNode*)node, ok);
    case AST_NODE_UNARY: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        MvpNumericValue operand = mvp_numeric_emit_expr(state, unary->operand, ok);
        if (!*ok || !operand.reg) return (MvpNumericValue){0, 0};
        if (unary->op == OPERATOR_POS) return operand;
        if (unary->op == OPERATOR_NEG && !operand.is_boolean) {
            MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_D, "negative");
            if (!result) {
                *ok = 0;
                return (MvpNumericValue){0, 0};
            }
            mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_DNEG,
                MIR_new_reg_op(state->compiler->context, result),
                MIR_new_reg_op(state->compiler->context, operand.reg)));
            return (MvpNumericValue){result, 0};
        }
        if ((unary->op == OPERATOR_JS_INCREMENT || unary->op == OPERATOR_JS_DECREMENT) &&
                unary->operand && unary->operand->node_type == AST_NODE_IDENT &&
                !operand.is_boolean) {
            MIR_insn_code_t operation = unary->op == OPERATOR_JS_INCREMENT ? MIR_DADD : MIR_DSUB;
            mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, operation,
                MIR_new_reg_op(state->compiler->context, operand.reg),
                MIR_new_reg_op(state->compiler->context, operand.reg),
                MIR_new_double_op(state->compiler->context, 1.0)));
            return (MvpNumericValue){operand.reg, 0};
        }
        if (unary->op == OPERATOR_NOT) {
            MIR_reg_t result = mvp_numeric_new_register(state, MIR_T_I64, "not");
            if (!result) {
                *ok = 0;
                return (MvpNumericValue){0, 1};
            }
            if (operand.is_boolean) {
                mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_EQ,
                    MIR_new_reg_op(state->compiler->context, result),
                    MIR_new_reg_op(state->compiler->context, operand.reg),
                    MIR_new_int_op(state->compiler->context, 0)));
            } else {
                mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_DEQ,
                    MIR_new_reg_op(state->compiler->context, result),
                    MIR_new_reg_op(state->compiler->context, operand.reg),
                    MIR_new_double_op(state->compiler->context, 0.0)));
            }
            return (MvpNumericValue){result, 1};
        }
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* assignment = (AstAssignNode*)node;
        if (!assignment->left || assignment->left->node_type != AST_NODE_IDENT) {
            *ok = 0;
            return (MvpNumericValue){0, 0};
        }
        MvpNumericValue value = mvp_numeric_emit_expr(state, assignment->right, ok);
        String* name = ((AstIdentNode*)assignment->left)->name;
        MIR_reg_t destination = mvp_numeric_lookup(state, name);
        int destination_is_boolean = mvp_numeric_binding_is_boolean(state, name);
        if (!*ok || !destination) {
            *ok = 0;
            return (MvpNumericValue){0, 0};
        }
        if (assignment->op == OPERATOR_ASSIGN) {
            if (value.is_boolean != destination_is_boolean) {
                *ok = 0;
                return (MvpNumericValue){0, 0};
            }
            mvp_numeric_emit(state, MIR_new_insn(state->compiler->context,
                value.is_boolean ? MIR_MOV : MIR_DMOV,
                MIR_new_reg_op(state->compiler->context, destination),
                MIR_new_reg_op(state->compiler->context, value.reg)));
            return (MvpNumericValue){destination, destination_is_boolean};
        }
        if (destination_is_boolean || value.is_boolean) {
            *ok = 0;
            return (MvpNumericValue){0, 0};
        }
        if (assignment->op == OPERATOR_JS_BIT_AND_ASSIGN ||
                assignment->op == OPERATOR_JS_BIT_OR_ASSIGN ||
                assignment->op == OPERATOR_JS_BIT_XOR_ASSIGN ||
                assignment->op == OPERATOR_JS_LSHIFT_ASSIGN ||
                assignment->op == OPERATOR_JS_RSHIFT_ASSIGN) {
            Operator operation = assignment->op == OPERATOR_JS_BIT_AND_ASSIGN
                ? OPERATOR_JS_BIT_AND : assignment->op == OPERATOR_JS_BIT_OR_ASSIGN
                ? OPERATOR_JS_BIT_OR : assignment->op == OPERATOR_JS_BIT_XOR_ASSIGN
                ? OPERATOR_JS_BIT_XOR : assignment->op == OPERATOR_JS_LSHIFT_ASSIGN
                ? OPERATOR_JS_LSHIFT : OPERATOR_JS_RSHIFT;
            MvpNumericValue result = mvp_numeric_emit_bitwise(state,
                (MvpNumericValue){destination, 0}, value, operation, ok);
            if (!*ok || !result.reg) return (MvpNumericValue){0, 0};
            mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_DMOV,
                MIR_new_reg_op(state->compiler->context, destination),
                MIR_new_reg_op(state->compiler->context, result.reg)));
            return (MvpNumericValue){destination, 0};
        }
        MIR_insn_code_t operation = MIR_DADD;
        switch (assignment->op) {
        case OPERATOR_JS_ADD_ASSIGN: operation = MIR_DADD; break;
        case OPERATOR_JS_SUB_ASSIGN: operation = MIR_DSUB; break;
        case OPERATOR_JS_MUL_ASSIGN: operation = MIR_DMUL; break;
        case OPERATOR_JS_DIV_ASSIGN: operation = MIR_DDIV; break;
        default:
            *ok = 0;
            return (MvpNumericValue){0, 0};
        }
        mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, operation,
            MIR_new_reg_op(state->compiler->context, destination),
            MIR_new_reg_op(state->compiler->context, destination),
            MIR_new_reg_op(state->compiler->context, value.reg)));
        return (MvpNumericValue){destination, 0};
    }
    default:
        *ok = 0;
        return (MvpNumericValue){0, 0};
    }
}

static int mvp_numeric_emit_statement(MvpNumericFunctionState* state,
        AstNode* node);

static int mvp_numeric_emit_statements(MvpNumericFunctionState* state,
        AstNode* statements) {
    for (AstNode* statement = statements; statement; statement = statement->next) {
        if (!mvp_numeric_emit_statement(state, statement)) return 0;
    }
    return 1;
}

static int mvp_numeric_emit_variable(MvpNumericFunctionState* state,
        AstVarDeclNode* declaration) {
    for (AstNode* item = declaration->declarations; item; item = item->next) {
        if (item->node_type != AST_NODE_VARIABLE_DECLARATOR) return 0;
        AstDeclaratorNode* declarator = (AstDeclaratorNode*)item;
        if (!declarator->id || declarator->id->node_type != AST_NODE_IDENT || !declarator->init) return 0;
        int ok = 1;
        MvpNumericValue value = mvp_numeric_emit_expr(state, declarator->init, &ok);
        if (!ok || !value.reg) return 0;
        MIR_reg_t local = mvp_numeric_new_register(state, value.is_boolean ? MIR_T_I64 : MIR_T_D,
            "local");
        if (!local || !mvp_numeric_bind(state, ((AstIdentNode*)declarator->id)->name, local,
                value.is_boolean)) return 0;
        mvp_numeric_emit(state, MIR_new_insn(state->compiler->context,
            value.is_boolean ? MIR_MOV : MIR_DMOV,
            MIR_new_reg_op(state->compiler->context, local),
            MIR_new_reg_op(state->compiler->context, value.reg)));
    }
    return 1;
}

static int mvp_numeric_emit_condition(MvpNumericFunctionState* state, AstNode* node,
        MIR_reg_t* out_condition) {
    int ok = 1;
    MvpNumericValue value = mvp_numeric_emit_expr(state, node, &ok);
    if (!ok || !value.reg) return 0;
    if (value.is_boolean) {
        *out_condition = value.reg;
        return 1;
    }
    MIR_reg_t condition = mvp_numeric_new_register(state, MIR_T_I64, "truthy");
    if (!condition) return 0;
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_DNE,
        MIR_new_reg_op(state->compiler->context, condition),
        MIR_new_reg_op(state->compiler->context, value.reg),
        MIR_new_double_op(state->compiler->context, 0.0)));
    *out_condition = condition;
    return 1;
}

static int mvp_numeric_emit_loop(MvpNumericFunctionState* state,
        AstLoopControlNode* loop) {
    if (!loop || !loop->test || !loop->body) return 0;
    if (loop->init && !mvp_numeric_emit_statement(state, loop->init)) return 0;
    MIR_label_t test = MIR_new_label(state->compiler->context);
    MIR_label_t done = MIR_new_label(state->compiler->context);
    if (!test || !done) return 0;
    mvp_numeric_emit(state, test);
    MIR_reg_t condition = 0;
    if (!mvp_numeric_emit_condition(state, loop->test, &condition)) return 0;
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_BF,
        MIR_new_label_op(state->compiler->context, done),
        MIR_new_reg_op(state->compiler->context, condition)));
    if (!mvp_numeric_emit_statement(state, loop->body)) return 0;
    if (loop->update) {
        int ok = 1;
        (void)mvp_numeric_emit_expr(state, loop->update, &ok);
        if (!ok) return 0;
    }
    mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
        MIR_new_label_op(state->compiler->context, test)));
    mvp_numeric_emit(state, done);
    return 1;
}

static int mvp_numeric_emit_statement(MvpNumericFunctionState* state,
        AstNode* node) {
    if (!node) return 1;
    switch (node->node_type) {
    case AST_NODE_BLOCK:
        return mvp_numeric_emit_statements(state, ((AstBlockNode*)node)->statements);
    case AST_NODE_EXPR_STMT: {
        int ok = 1;
        (void)mvp_numeric_emit_expr(state, ((AstExprStmtNode*)node)->expression, &ok);
        return ok;
    }
    case AST_NODE_VAR_STAM:
        return mvp_numeric_emit_variable(state, (AstVarDeclNode*)node);
    case AST_NODE_LOOP:
        return mvp_numeric_emit_loop(state, (AstLoopControlNode*)node);
    case AST_NODE_RETURN_STAM: {
        int ok = 1;
        MvpNumericValue value = mvp_numeric_emit_expr(state,
            ((AstReturnNode*)node)->value, &ok);
        if (!ok || !value.reg || value.is_boolean) return 0;
        mvp_numeric_emit(state, MIR_new_ret_insn(state->compiler->context, 1,
            MIR_new_reg_op(state->compiler->context, value.reg)));
        return 1;
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* conditional = (AstIfNode*)node;
        MIR_reg_t condition = 0;
        if (!mvp_numeric_emit_condition(state, conditional->test, &condition)) return 0;
        MIR_label_t alternate = MIR_new_label(state->compiler->context);
        MIR_label_t done = MIR_new_label(state->compiler->context);
        if (!alternate || !done) return 0;
        mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_BF,
            MIR_new_label_op(state->compiler->context, alternate),
            MIR_new_reg_op(state->compiler->context, condition)));
        if (!mvp_numeric_emit_statement(state, conditional->consequent)) return 0;
        mvp_numeric_emit(state, MIR_new_insn(state->compiler->context, MIR_JMP,
            MIR_new_label_op(state->compiler->context, done)));
        mvp_numeric_emit(state, alternate);
        if (conditional->alternate && !mvp_numeric_emit_statement(state, conditional->alternate)) return 0;
        mvp_numeric_emit(state, done);
        return 1;
    }
    default:
        return 0;
    }
}

static size_t mvp_numeric_count_parameters(const AstFuncNode* function) {
    size_t count = 0;
    for (AstNode* parameter = function ? function->params : NULL; parameter;
            parameter = parameter->next) {
        if (parameter->node_type != AST_NODE_IDENT) return SIZE_MAX;
        count++;
    }
    return count;
}

static int mvp_numeric_compile_function(MvpNumericCompiler* compiler,
        MvpNumericFunction* function) {
    MIR_var_t parameters[64] = {};
    if (!compiler || !function || function->parameter_count > 64) return 0;
    size_t index = 0;
    for (AstNode* parameter = function->ast->params; parameter; parameter = parameter->next) {
        String* name = ((AstIdentNode*)parameter)->name;
        if (!name || index >= function->parameter_count) return 0;
        parameters[index++] = (MIR_var_t){MIR_T_D, name->chars, 0};
    }
    MIR_type_t return_type = MIR_T_D;
    MIR_item_t item = MIR_new_func_arr(compiler->context, function->name, 1,
        &return_type, function->parameter_count, parameters);
    if (!item) return 0;
    function->item = item;
    MvpNumericFunctionState state = {compiler, item, MIR_get_item_func(compiler->context, item), {}, 0};
    int ok = state.function != NULL;
    for (size_t i = 0; ok && i < function->parameter_count; i++) {
        AstNode* parameter = function->ast->params;
        for (size_t j = 0; j < i; j++) parameter = parameter->next;
        MIR_reg_t reg = MIR_reg(compiler->context, parameters[i].name, state.function);
        ok = reg && mvp_numeric_bind(&state, ((AstIdentNode*)parameter)->name, reg, 0);
    }
    if (ok) ok = mvp_numeric_emit_statements(&state, function->ast->body
        ? ((AstBlockNode*)function->ast->body)->statements : NULL);
    if (ok) {
        MvpNumericValue zero = mvp_numeric_emit_number(&state, 0.0, &ok);
        if (ok) mvp_numeric_emit(&state, MIR_new_ret_insn(compiler->context, 1,
            MIR_new_reg_op(compiler->context, zero.reg)));
    }
    MIR_finish_func(compiler->context);
    return ok;
}

static int mvp_numeric_compile_main(MvpNumericCompiler* compiler,
        AstScript* script, MIR_item_t* out_main) {
    MIR_type_t return_type = MIR_T_D;
    MIR_item_t item = MIR_new_func_arr(compiler->context, "mvp_numeric_main", 1,
        &return_type, 0, NULL);
    if (!item) return 0;
    MvpNumericFunctionState state = {compiler, item, MIR_get_item_func(compiler->context, item), {}, 0};
    AstNode* final_expression = NULL;
    int ok = state.function != NULL;
    for (AstNode* statement = script->body; ok && statement; statement = statement->next) {
        if (statement->node_type == AST_NODE_FUNC) continue;
        if (statement->node_type == AST_NODE_EXPR_STMT) {
            AstNode* expression = ((AstExprStmtNode*)statement)->expression;
            if (expression && expression->node_type == AST_NODE_LITERAL &&
                    ((AstLiteralNode*)expression)->literal_type == AST_LITERAL_STRING) continue;
            if (statement->next) {
                ok = mvp_numeric_emit_statement(&state, statement);
            } else {
                final_expression = expression;
            }
            continue;
        }
        ok = 0;
    }
    if (!final_expression) ok = 0;
    if (ok) {
        MvpNumericValue value = mvp_numeric_emit_expr(&state, final_expression, &ok);
        if (ok && value.reg && !value.is_boolean) {
            mvp_numeric_emit(&state, MIR_new_ret_insn(compiler->context, 1,
                MIR_new_reg_op(compiler->context, value.reg)));
        } else {
            ok = 0;
        }
    }
    MIR_finish_func(compiler->context);
    *out_main = item;
    return ok;
}

MvpNumericProgram* mvp_numeric_program_create(void* function_ast) {
    AstFuncNode* ast = (AstFuncNode*)function_ast;
    if (!ast || !ast->name) return NULL;
    MvpNumericProgram* program = (MvpNumericProgram*)mem_calloc(1,
        sizeof(MvpNumericProgram), MEM_CAT_JS_RUNTIME);
    if (!program) return NULL;
    program->compiler.context = MIR_init();
    program->compiler.functions = &program->function;
    program->compiler.function_count = 1;
    if (!program->compiler.context) {
        mem_free(program);
        return NULL;
    }
    program->compiler.module = MIR_new_module(program->compiler.context,
        "js_mvp_numeric_kernel");
    size_t parameter_count = mvp_numeric_count_parameters(ast);
    int ok = program->compiler.module != NULL && parameter_count != SIZE_MAX;
    if (ok) {
        program->function.ast = ast;
        program->function.parameter_count = parameter_count;
        int written = snprintf(program->function.name, sizeof(program->function.name),
            "mvp_numeric_kernel");
        ok = written > 0 && (size_t)written < sizeof(program->function.name);
    }
    if (ok) {
        program->function.forward = MIR_new_forward(program->compiler.context,
            program->function.name);
        ok = program->function.forward != NULL;
    }
    if (ok) ok = mvp_numeric_compile_function(&program->compiler, &program->function);
    if (program->compiler.module) MIR_finish_module(program->compiler.context);
    if (ok) {
        MIR_load_module(program->compiler.context, program->compiler.module);
        MIR_gen_init(program->compiler.context);
        program->generator_initialized = 1;
        MIR_gen_set_optimize_level(program->compiler.context, 2);
        MIR_link(program->compiler.context, MIR_set_gen_interface,
            mvp_numeric_import_resolver);
        program->entry = program->function.item ? program->function.item->addr : NULL;
        ok = program->entry != NULL;
    }
    if (!ok) {
        if (program->compiler.context) {
            if (program->generator_initialized) MIR_gen_finish(program->compiler.context);
            MIR_finish(program->compiler.context);
        }
        mem_free(program);
        return NULL;
    }
    return program;
}

void mvp_numeric_program_destroy(MvpNumericProgram* program) {
    if (!program) return;
    if (program->compiler.context) {
        if (program->generator_initialized) MIR_gen_finish(program->compiler.context);
        MIR_finish(program->compiler.context);
    }
    mem_free(program);
}

int mvp_numeric_program_matches(const MvpNumericProgram* program,
        const void* function_ast) {
    return program && program->function.ast == function_ast;
}

void* mvp_numeric_program_entry(const MvpNumericProgram* program) {
    return program ? program->entry : NULL;
}

size_t mvp_numeric_program_parameter_count(const MvpNumericProgram* program) {
    return program ? program->function.parameter_count : 0;
}

int mvp_execute_numeric_ast(const AstNode* root, MvpValue* out) {
    if (!root || root->node_type != AST_SCRIPT || !out) return 0;
    AstScript* script = (AstScript*)root;
    size_t function_count = 0;
    for (AstNode* node = script->body; node; node = node->next) {
        if (node->node_type == AST_NODE_FUNC) function_count++;
    }
    MvpNumericFunction* functions = NULL;
    if (function_count) {
        functions = (MvpNumericFunction*)mem_calloc(function_count,
            sizeof(MvpNumericFunction), MEM_CAT_JS_RUNTIME);
        if (!functions) return 0;
    }
    MvpNumericCompiler compiler = {MIR_init(), NULL, functions, function_count, 0};
    if (!compiler.context) {
        if (functions) mem_free(functions);
        return 0;
    }
    compiler.module = MIR_new_module(compiler.context, "js_mvp_numeric");
    size_t index = 0;
    int ok = compiler.module != NULL;
    for (AstNode* node = script->body; ok && node; node = node->next) {
        if (node->node_type != AST_NODE_FUNC) continue;
        AstFuncNode* function = (AstFuncNode*)node;
        size_t parameter_count = mvp_numeric_count_parameters(function);
        if (!function->name || parameter_count == SIZE_MAX || index >= function_count) {
            ok = 0;
            break;
        }
        MvpNumericFunction* entry = &functions[index];
        entry->ast = function;
        entry->parameter_count = parameter_count;
        int written = snprintf(entry->name, sizeof(entry->name), "mvp_fn_%zu", index);
        if (written <= 0 || (size_t)written >= sizeof(entry->name)) {
            ok = 0;
            break;
        }
        entry->forward = MIR_new_forward(compiler.context, entry->name);
        if (!entry->forward) {
            ok = 0;
            break;
        }
        index++;
    }
    MIR_item_t main_item = NULL;
    for (size_t i = 0; ok && i < function_count; i++) ok = mvp_numeric_compile_function(&compiler, &functions[i]);
    if (ok) ok = mvp_numeric_compile_main(&compiler, script, &main_item);
    // MIR owns an active module even when an unsupported AST stops lowering.
    // Finish it before context teardown so fail-closed compilation is leak-free.
    if (compiler.module) MIR_finish_module(compiler.context);
    if (ok) {
        MIR_load_module(compiler.context, compiler.module);
        MIR_gen_init(compiler.context);
        MIR_gen_set_optimize_level(compiler.context, 2);
        MIR_link(compiler.context, MIR_set_gen_interface, mvp_numeric_import_resolver);
        MvpNumericMirEntry entry = main_item ? (MvpNumericMirEntry)main_item->addr : NULL;
        if (entry) *out = mvp_value_from_number(entry());
        else ok = 0;
        MIR_gen_finish(compiler.context);
    }
    MIR_finish(compiler.context);
    if (functions) mem_free(functions);
    return ok;
}
