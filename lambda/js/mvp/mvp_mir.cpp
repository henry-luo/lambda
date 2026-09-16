#include "mvp.h"

#include "../js_ast.hpp"
#include "../../runtime/ast-core.hpp"
#include "../../mir/mir.h"
#include "../../mir/mir-gen.h"
#include "../../../lib/str.h"

#include <string.h>

typedef uint64_t (*MvpMirEntry)(void);

int mvp_execute_numeric_ast(const AstNode* root, MvpValue* out);
int mvp_execute_generic_ast(const AstNode* root, MvpExecutionResult* out);
const char* mvp_generic_last_failure(void);

static void* mvp_mir_import_resolver(const char* name) {
    // The first direct-MIR slice has no imports. Keeping this resolver local
    // prevents MIR from consulting the old JS or Lambda helper import table.
    (void)name;
    return NULL;
}

static int mvp_literal_value(const AstNode* node, MvpValue* out) {
    if (!node || !out || node->node_type != AST_NODE_LITERAL) return 0;
    const AstLiteralNode* literal = (const AstLiteralNode*)node;
    switch (literal->literal_type) {
    case AST_LITERAL_NUMBER:
        *out = mvp_value_from_number(literal->value.number_value);
        return 1;
    case AST_LITERAL_BOOLEAN:
        *out = mvp_value_bool(literal->value.boolean_value);
        return 1;
    case AST_LITERAL_NULL:
        *out = mvp_value_null();
        return 1;
    case AST_LITERAL_UNDEFINED:
        *out = mvp_value_undefined();
        return 1;
    default:
        return 0;
    }
}

static int mvp_program_literal_value(const AstNode* root, MvpValue* out) {
    if (!root || root->node_type != AST_SCRIPT || !out) return 0;
    const AstScript* script = (const AstScript*)root;
    const AstNode* statement = script->body;
    if (!statement || statement->next || statement->node_type != AST_NODE_EXPR_STMT) return 0;
    const AstExprStmtNode* expression_statement = (const AstExprStmtNode*)statement;
    return mvp_literal_value(expression_statement->expression, out);
}

static int mvp_execute_literal(MvpValue value, MvpValue* out) {
    MIR_context_t context = MIR_init();
    if (!context) return 0;
    MIR_module_t module = MIR_new_module(context, "js_mvp");
    MIR_type_t return_type = MIR_T_I64;
    MIR_item_t function_item = MIR_new_func_arr(context, "mvp_main", 1,
        &return_type, 0, NULL);
    MIR_append_insn(context, function_item, MIR_new_ret_insn(context, 1,
        MIR_new_int_op(context, (int64_t)value.bits)));
    MIR_finish_func(context);
    MIR_finish_module(context);
    MIR_load_module(context, module);
    MIR_gen_init(context);
    MIR_gen_set_optimize_level(context, 2);
    MIR_link(context, MIR_set_gen_interface, mvp_mir_import_resolver);
    MvpMirEntry entry = (MvpMirEntry)function_item->addr;
    if (entry) *out = (MvpValue){entry()};
    MIR_gen_finish(context);
    MIR_finish(context);
    return entry != NULL;
}

MvpExecutionResult mvp_execute_source(const char* source, size_t source_length) {
    MvpExecutionResult result = {mvp_value_undefined(), 0, {0}, NULL};
    MvpAstUnit* unit = mvp_ast_parse(source, source_length);
    if (!unit) {
        str_copy(result.error, sizeof(result.error), "MVP parser or early-error failure",
            sizeof("MVP parser or early-error failure") - 1);
        return result;
    }
    MvpValue value = mvp_value_undefined();
    if (mvp_execute_numeric_ast((const AstNode*)mvp_ast_root(unit), &value)) {
        result.value = value;
        result.ok = 1;
        mvp_ast_destroy(unit);
        return result;
    }
    if (mvp_execute_generic_ast((const AstNode*)mvp_ast_root(unit), &result)) {
        result.ok = 1;
        mvp_ast_destroy(unit);
        return result;
    }
    if (!mvp_program_literal_value((const AstNode*)mvp_ast_root(unit), &value)) {
        const char* failure = mvp_generic_last_failure();
        if (failure) str_copy(result.error, sizeof(result.error), failure, strlen(failure));
        else str_copy(result.error, sizeof(result.error),
            "MVP lowering does not yet support this executable AST",
            sizeof("MVP lowering does not yet support this executable AST") - 1);
        mvp_ast_destroy(unit);
        return result;
    }
    if (!mvp_execute_literal(value, &result.value)) {
        str_copy(result.error, sizeof(result.error), "MVP MIR link failed",
            sizeof("MVP MIR link failed") - 1);
        mvp_ast_destroy(unit);
        return result;
    }
    result.ok = 1;
    mvp_ast_destroy(unit);
    return result;
}
