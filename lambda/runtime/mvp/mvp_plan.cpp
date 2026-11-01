#include "mvp_internal.hpp"
#include "../interp.hpp"
#include "../type_contract.hpp"
#include "../mir_loop_analysis.hpp"
#include "../../../lib/host_paths.h"

bool mvp_reject(LambdaMvpModule* m, AstNode* node, const char* reason) {
    if (m->failed) return false;
    m->failed = true;
    LambdaSourcePoint point = lambda_source_point_at(m->tp->source,
        node ? node->source_span.start_byte : 0);
    SourceLocation location = {m->tp->reference, point.row + 1, point.column + 1,
        point.row + 1, point.column + 1, m->tp->source};
    LambdaError* error = err_createf(ERR_NOT_IMPLEMENTED, &location,
        "Lambda MIR MVP: %s", reason);
    if (error) {
        if (m->tp->errors) arraylist_append(m->tp->errors, error);
        else { err_print(error); err_free(error); }
    }
    m->tp->error_count++;
    log_error("lambda-mvp-admission: %s at byte %u", reason,
        node ? node->source_span.start_byte : 0);
    return false;
}

ValueRep mvp_rep(Type* type) {
    if (!type) return VALUE_REP_ITEM;
    // full contracts, including nullable and union types, select their carrier.
    return lambda_canonical_rep(type);
}

Type* mvp_contract(AstNode* node) {
    if (!node) return &TYPE_ANY;
    if (node->node_type == AST_NODE_PARAM) {
        TypeParam* param = lambda_type_param(node->type);
        return param && param->contract_type ? param->contract_type : node->type;
    }
    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* decl = (AstDeclaratorNode*)node;
        if (decl->declared_type) return decl->declared_type;
    }
    return node->type ? node->type : &TYPE_ANY;
}

MvpFunction* mvp_function(LambdaMvpModule* m, AstFuncNode* ast) {
    for (uint32_t i = 0; i < m->function_count; i++)
        if (m->functions[i].ast == ast) return &m->functions[i];
    return NULL;
}

MvpBinding* mvp_binding(LambdaMvpModule* m, NameEntry* entry) {
    AstBindingId id = ast_index_find_binding_entry(&m->tp->ast_index, entry);
    return id < m->binding_count ? &m->bindings[id] : NULL;
}

MvpBinding* mvp_local_binding(LambdaMvpModule* m, AstNode* node) {
    node = ast_unwrap_primary_to_leaf(node);
    return node && node->node_type == AST_NODE_IDENT ? mvp_binding(m, ((AstIdentNode*)node)->entry) : NULL;
}

Type* mvp_array_element(Type* contract) {
    LambdaArrayContractInfo info = {};
    if (!lambda_array_contract_info(contract, &info) || info.rank != 1) return NULL;
    Type* element = type_field_unwrap_simple_decl(info.immediate_element);
    return element == &TYPE_INT || element == &TYPE_BOOL || element == &TYPE_FLOAT ? element : NULL;
}

bool mvp_proc_has_final_return(AstFuncNode* fn) {
    AstNode* node = fn->body;
    while (node && (node->node_type == AST_NODE_CONTENT || node->node_type == AST_NODE_BLOCK)) {
        node = node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
        while (node && node->next) node = node->next;
    }
    return node && node->node_type == AST_NODE_RETURN_STAM;
}

// query transient scalar producer proofs; the shared AST remains the sole type graph (D8.2.5v3).
Type* mvp_value_contract(LambdaMvpModule* m, AstNode* source, int depth, bool dense_arrays) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n) return &TYPE_NULL;
    if (depth > 32) return &TYPE_ANY;
    // an indexed read can be absent even under a non-null element contract (S7.1.1v3).
    if (n->node_type == AST_NODE_INDEX_EXPR) {
        MvpBinding* b = mvp_local_binding(m, ((AstFieldNode*)n)->object);
        Type* element = dense_arrays && b ? mvp_array_element(b->contract) : NULL;
        return element ? element : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_binding(m, ((AstIdentNode*)n)->entry);
        return b ? b->contract : &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n;
        MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
        if (f) {
            if (f->abi.effects.may_return_error) return &TYPE_ANY;
            AstNamedNode* p = f->ast->param;
            for (AstNode* a = call->argument; a && p; a = a->next, p = (AstNamedNode*)p->next)
                if (lambda_static_boundary_relation(mvp_value_contract(m, a, depth + 1, dense_arrays), mvp_contract(p), true)
                        != STATIC_BOUNDARY_PROVEN) return &TYPE_ANY;
            return f->result_contract;
        }
        AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return &TYPE_ANY;
        SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
        if (info->fn == SYSPROC_CLOCK) return &TYPE_FLOAT;
        if (info->fn == SYSPROC_PRINT) return &TYPE_NULL;
        if (info->fn == SYSFUNC_STRING) return &TYPE_STRING;
        TypeId arg = mvp_value_contract(m, call->argument, depth + 1, dense_arrays)->type_id;
        if ((info->fn == SYSFUNC_ABS || info->fn == SYSFUNC_FLOOR || info->fn == SYSFUNC_SQRT) &&
                arg == LMD_TYPE_FLOAT) return &TYPE_FLOAT;
        if ((info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
                (arg == LMD_TYPE_FLOAT || arg == LMD_TYPE_INT))
            return info->fn == SYSFUNC_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
        return &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_BINARY) {
        AstBinaryNode* b = (AstBinaryNode*)n;
        TypeId left = mvp_value_contract(m, b->left, depth + 1, dense_arrays)->type_id;
        TypeId right = mvp_value_contract(m, b->right, depth + 1, dense_arrays)->type_id;
        bool numbers = (left == LMD_TYPE_INT || left == LMD_TYPE_FLOAT) &&
            (right == LMD_TYPE_INT || right == LMD_TYPE_FLOAT);
        if (numbers) {
            MirNumericOpPlan plan = {};
            if (em_numeric_op_plan(b->op, &plan, true)) {
                if (plan.is_comparison) return &TYPE_BOOL;
                return b->op == OPERATOR_DIV || left == LMD_TYPE_FLOAT || right == LMD_TYPE_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
            }
            if (b->op == OPERATOR_IDIV && left == LMD_TYPE_INT && right == LMD_TYPE_INT) return &TYPE_INT;
        }
        if ((b->op == OPERATOR_AND || b->op == OPERATOR_OR || b->op == OPERATOR_EQ || b->op == OPERATOR_NE) &&
                left == LMD_TYPE_BOOL && right == LMD_TYPE_BOOL) return &TYPE_BOOL;
        return &TYPE_ANY;
    }
    if (n->node_type == AST_NODE_UNARY) {
        AstUnaryNode* u = (AstUnaryNode*)n;
        Type* operand = mvp_value_contract(m, u->operand, depth + 1, dense_arrays);
        if (u->op == OPERATOR_NOT && operand->type_id == LMD_TYPE_BOOL) return &TYPE_BOOL;
        if ((u->op == OPERATOR_POS || u->op == OPERATOR_NEG) &&
                (operand->type_id == LMD_TYPE_INT || operand->type_id == LMD_TYPE_FLOAT)) return operand;
        return &TYPE_ANY;
    }
    return n->type ? type_field_unwrap_simple_decl(n->type) : &TYPE_ANY;
}

LambdaMvpModule* lambda_mvp_create(Transpiler* tp, MIR_context_t ctx) {
    LambdaMvpModule* m = (LambdaMvpModule*)mem_calloc(1, sizeof(*m), MEM_CAT_TEMP);
    if (m) { m->tp = tp; m->ctx = ctx; }
    return m;
}

// Demand-only interval query over the shared AST and immutable binding plans;
// it never publishes a second node-type/fact table (D8.2.5v3).
static MvpIntRange counter_bound(LambdaMvpModule*, NameEntry*, AstNode*, int depth = 0);
MvpIntRange mvp_int_range(LambdaMvpModule* m, AstNode* node, int depth) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 32) return {};
    int64_t literal;
    if (mir_decimal_int_literal(m->tp->source, node, &literal) && literal >= INT53_MIN && literal <= INT53_MAX)
        return {true, literal, literal};
    if (node->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_binding(m, ((AstIdentNode*)node)->entry);
        MvpIntRange range = b ? b->range : MvpIntRange{};
        if (!range.finite) return range;
        AstIndex* index = &m->tp->ast_index;
        AstNodeId id = ast_index_find(index, node);
        while ((id = ast_index_parent_id(index, id)) < index->count) {
            AstNode* parent = index->nodes[id];
            if (parent->node_type != AST_NODE_LOOP && parent->node_type != AST_NODE_FOR_EXPR) continue;
            if (parent->node_type != AST_NODE_LOOP) break;
            AstLoopControlNode* loop = (AstLoopControlNode*)parent;
            AstNodeId body = ast_index_find(index, loop->body);
            if (!ast_index_node_descends(index, ast_index_find(index, node), body)) break;
            MvpIntRange bound = counter_bound(m, b->entry, loop->cond, depth + 1);
            // An outer bound survives a nested loop only if that loop never
            // writes this binding, including writes after the current use.
            uint32_t count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index,
                ast_index_find_binding_entry(index, b->entry), &count);
            for (uint32_t i = 0; i < count; i++) {
                AstNode* use = index->nodes[uses[i]];
                if (use->node_type == AST_NODE_ASSIGN_STAM && (!bound.finite || use->source_span.start_byte < node->source_span.start_byte) &&
                        ast_index_node_descends(index, uses[i], body)) return range;
            }
            if (!bound.finite) continue;
            if (bound.finite && bound.upper >= range.lower && bound.upper < range.upper) range.upper = bound.upper;
            break;
        }
        return range;
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        MvpIntRange operand = mvp_int_range(m, unary->operand, depth + 1);
        if (operand.finite && unary->op == OPERATOR_NEG) return {true, -operand.upper, -operand.lower};
        return unary->op == OPERATOR_POS ? operand : MvpIntRange{};
    }
    if (node->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* binary = (AstBinaryNode*)node;
    MvpIntRange a = mvp_int_range(m, binary->left, depth + 1);
    MvpIntRange b = mvp_int_range(m, binary->right, depth + 1);
    if (!a.finite || !b.finite) return {};
    __int128 low, high;
    if (binary->op == OPERATOR_ADD) { low = (__int128)a.lower + b.lower; high = (__int128)a.upper + b.upper; }
    else if (binary->op == OPERATOR_SUB) { low = (__int128)a.lower - b.upper; high = (__int128)a.upper - b.lower; }
    else if (binary->op == OPERATOR_MUL) {
        __int128 corners[] = {(__int128)a.lower * b.lower, (__int128)a.lower * b.upper,
            (__int128)a.upper * b.lower, (__int128)a.upper * b.upper};
        low = high = corners[0];
        for (int i = 1; i < 4; i++) { if (corners[i] < low) low = corners[i]; if (corners[i] > high) high = corners[i]; }
    } else return {};
    if (low < INT53_MIN || high > INT53_MAX) return {};
    return {true, (int64_t)low, (int64_t)high};
}

static MvpIntRange counter_bound(LambdaMvpModule* m, NameEntry* counter, AstNode* condition, int depth) {
    condition = ast_unwrap_primary_to_leaf(condition);
    if (!condition || depth > 32 || condition->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* binary = (AstBinaryNode*)condition;
    if (binary->op == OPERATOR_AND) {
        MvpIntRange bound = counter_bound(m, counter, binary->left, depth + 1);
        return bound.finite ? bound : counter_bound(m, counter, binary->right, depth + 1);
    }
    AstNode* left = ast_unwrap_primary_to_leaf(binary->left);
    if ((binary->op != OPERATOR_LT && binary->op != OPERATOR_LE) || !left || left->node_type != AST_NODE_IDENT ||
            ((AstIdentNode*)left)->entry != counter) return {};
    MvpIntRange bound = mvp_int_range(m, binary->right, depth + 1);
    if (bound.finite && binary->op == OPERATOR_LT) bound.upper--;
    return bound;
}

static MvpIntRange bounded_counter(LambdaMvpModule* m, MvpBinding* binding,
        AstAssignStamNode* update, MvpIntRange initial) {
    AstNode* node = ast_unwrap_primary_to_leaf(update->value);
    if (!node || node->node_type != AST_NODE_BINARY) return {};
    AstBinaryNode* add = (AstBinaryNode*)node;
    AstNode* left = ast_unwrap_primary_to_leaf(add->left);
    MvpIntRange step = mvp_int_range(m, add->right);
    if (add->op != OPERATOR_ADD || !left || left->node_type != AST_NODE_IDENT ||
            ((AstIdentNode*)left)->entry != binding->entry ||
            !step.finite || step.lower < 0) return {};
    AstIndex* index = &m->tp->ast_index;
    AstNodeId id = ast_index_find(index, update);
    while ((id = ast_index_parent_id(index, id)) < index->count) {
        AstNode* parent = index->nodes[id];
        if (parent->node_type != AST_NODE_LOOP) continue;
        // the nearest loop must guard this counter; a nested loop cannot execute the update repeatedly under an outer guard.
        AstLoopControlNode* loop = (AstLoopControlNode*)parent;
        MvpIntRange bound = counter_bound(m, binding->entry, loop->cond);
        if (loop->form != LOOP_FORM_WHILE || !bound.finite ||
                !ast_index_node_descends(index, ast_index_find(index, update), ast_index_find(index, loop->body))) return {};
        if ((__int128)bound.upper + step.upper > INT53_MAX) return {};
        if (bound.upper + step.upper > initial.upper) initial.upper = bound.upper + step.upper;
        return initial;
    }
    return {};
}

static void plan_integer_ranges(LambdaMvpModule* m) {
    AstIndex* index = &m->tp->ast_index;
    bool changed;
    do {
        changed = false;
        for (AstBindingId id = 0; id < m->binding_count; id++) {
            MvpBinding* b = &m->bindings[id];
            if (b->range.finite || b->rep != VALUE_REP_INT_LANE || !b->entry || !b->entry->node) continue;
            if (b->entry->node->node_type == AST_NODE_PARAM) {
                MvpFunction* function = b->owner < m->function_count ? &m->functions[b->owner] : NULL;
                int position = 0;
                AstNamedNode* p = function ? function->ast->param : NULL;
                for (; p && p->entry != b->entry; p = (AstNamedNode*)p->next) position++;
                uint32_t count = 0;
                const AstNodeId* uses = ast_index_binding_uses(index, id, &count);
                bool written = false;
                for (uint32_t u = 0; u < count; u++) written |= index->nodes[uses[u]]->node_type == AST_NODE_ASSIGN_STAM;
                MvpIntRange range = {}; bool known = p && !written;
                // the admission set has only private direct calls; every edge must supply a finite bound.
                for (AstNodeId n = 0; n < index->count && known; n++) {
                    if (index->nodes[n]->node_type != AST_NODE_CALL_EXPR) continue;
                    AstCallNode* call = (AstCallNode*)index->nodes[n];
                    if (ast_direct_call_function(call) != function->ast) continue;
                    AstNode* argument = call->argument;
                    for (int i = 0; argument && i < position; i++) argument = argument->next;
                    MvpIntRange incoming = mvp_int_range(m, argument);
                    if (!incoming.finite) { known = false; break; }
                    if (!range.finite) range = incoming;
                    else {
                        if (incoming.lower < range.lower) range.lower = incoming.lower;
                        if (incoming.upper > range.upper) range.upper = incoming.upper;
                    }
                }
                if (known && range.finite) { b->range = range; changed = true; }
                continue;
            }
            if (b->entry->node->node_type == AST_NODE_FOR_CLAUSE) {
                AstNode* source = ast_unwrap_primary_to_leaf(((AstLoopNode*)b->entry->node)->as);
                if (!source || source->node_type != AST_NODE_BINARY || ((AstBinaryNode*)source)->op != OPERATOR_TO) continue;
                MvpIntRange first = mvp_int_range(m, ((AstBinaryNode*)source)->left);
                MvpIntRange last = mvp_int_range(m, ((AstBinaryNode*)source)->right);
                if (first.finite && last.finite && first.lower <= last.upper) {
                    b->range = {true, first.lower, last.upper}; changed = true;
                }
                continue;
            }
            if (b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
            MvpIntRange range = mvp_int_range(m, ((AstDeclaratorNode*)b->entry->node)->init);
            if (!range.finite) continue;
            uint32_t count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index, id, &count);
            // every write preserves reset values or a guarded nonnegative step;
            // multiple loops can safely reuse one counter.
            for (uint32_t i = 0; i < count && range.finite; i++) {
                AstNode* use = index->nodes[uses[i]];
                if (use->node_type != AST_NODE_ASSIGN_STAM) continue;
                AstAssignStamNode* update = (AstAssignStamNode*)use;
                MvpIntRange rhs = bounded_counter(m, b, update, range);
                if (!rhs.finite) rhs = mvp_int_range(m, update->value);
                if (!rhs.finite) { range.finite = false; break; }
                if (rhs.lower < range.lower) range.lower = rhs.lower;
                if (rhs.upper > range.upper) range.upper = rhs.upper;
            }
            if (range.finite) { b->range = range; changed = true; }
        }
    } while (changed);
}

static bool admitted_contract(Type* type) {
    if (!type) return true;
    type = type_field_unwrap_simple_decl(type);
    return type && (mvp_array_element(type) || type->type_id == LMD_TYPE_INT || type->type_id == LMD_TYPE_FLOAT ||
        type->type_id == LMD_TYPE_BOOL || type->type_id == LMD_TYPE_STRING ||
        type->type_id == LMD_TYPE_NULL || type->type_id == LMD_TYPE_ANY);
}

static MvpIntRange array_element_range(LambdaMvpModule* m, MvpBinding* binding) {
    if (!binding || mvp_array_element(binding->contract) != &TYPE_INT ||
            binding->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) return {};
    AstNode* initial = ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)binding->entry->node)->init);
    if (!initial || initial->node_type != AST_NODE_CALL_EXPR) return {};
    AstCallNode* fill = (AstCallNode*)initial;
    AstNode* callee = ast_unwrap_primary_to_leaf(fill->function);
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
            ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_FILL) return {};
    MvpIntRange range = mvp_int_range(m, fill->argument->next);
    if (!range.finite) return {};
    AstIndex* index = &m->tp->ast_index;
    // value copies detach before writing; only direct writes/borrows of this
    // root can change its element domain (S9.1.2, D4.4.6).
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        if (node->node_type == AST_NODE_ASSIGN_STAM &&
                ((AstAssignStamNode*)node)->target_entry == binding->entry) return {};
        if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM &&
                mvp_local_binding(m, ((AstAssignStamNode*)node)->object) == binding) {
            MvpIntRange rhs = mvp_int_range(m, ((AstAssignStamNode*)node)->value);
            if (!rhs.finite) return {};
            if (rhs.lower < range.lower) range.lower = rhs.lower;
            if (rhs.upper > range.upper) range.upper = rhs.upper;
        }
        if (node->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)node;
        AstFuncNode* function = ast_direct_call_function(call);
        AstNamedNode* param = function ? function->param : NULL;
        for (AstNode* arg = call->argument; arg && param; arg = arg->next, param = (AstNamedNode*)param->next) {
            TypeParam* p = lambda_type_param(param->type);
            if (p && p->is_var_param && mvp_local_binding(m, arg) == binding) return {};
        }
    }
    return range;
}

bool mvp_array_sum_plan(LambdaMvpModule* m, AstLoopControlNode* loop, MvpArraySumPlan* plan) {
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY || !loop->body ||
            loop->body->node_type != AST_NODE_CONTENT) return false;
    AstBinaryNode* compare = (AstBinaryNode*)condition;
    MvpBinding* counter = mvp_local_binding(m, compare->left);
    AstNode* limit = ast_unwrap_primary_to_leaf(compare->right);
    MvpBinding* bound = mvp_local_binding(m, limit);
    if (compare->op != OPERATOR_LT || !counter || counter->rep != VALUE_REP_INT_LANE ||
            (!bound && (!limit || limit->node_type != AST_NODE_PRIMARY)) ||
            mvp_value_contract(m, limit)->type_id != LMD_TYPE_INT || bound == counter) return false;
    AstNode* first = ((AstListNode*)loop->body)->item;
    AstNode* second = first ? first->next : NULL;
    if (!first || !second || second->next || first->node_type != AST_NODE_ASSIGN_STAM ||
            second->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstAssignStamNode* assignment = (AstAssignStamNode*)first;
    AstAssignStamNode* increment = (AstAssignStamNode*)second;
    MvpBinding* sum = mvp_binding(m, assignment->target_entry);
    AstNode* add_node = ast_unwrap_primary_to_leaf(assignment->value);
    AstNode* step_node = ast_unwrap_primary_to_leaf(increment->value);
    if (!sum || sum == counter || sum == bound || sum->rep != VALUE_REP_INT_LANE ||
            increment->target_entry != counter->entry || !add_node || !step_node ||
            add_node->node_type != AST_NODE_BINARY || step_node->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* add = (AstBinaryNode*)add_node;
    AstBinaryNode* step = (AstBinaryNode*)step_node;
    int64_t literal;
    if (add->op != OPERATOR_ADD || step->op != OPERATOR_ADD || mvp_local_binding(m, add->left) != sum ||
            mvp_local_binding(m, step->left) != counter ||
            !mir_decimal_int_literal(m->tp->source, step->right, &literal) || literal != 1) return false;
    AstNode* read = ast_unwrap_primary_to_leaf(add->right);
    if (!read || read->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* access = (AstFieldNode*)read;
    MvpBinding* array = mvp_local_binding(m, access->object);
    if (!array || mvp_local_binding(m, access->field) != counter) return false;
    MvpIntRange elements = array_element_range(m, array);
    if (!elements.finite) return false;
    int64_t magnitude = elements.lower < 0 ? -elements.lower : elements.lower;
    int64_t upper = elements.upper < 0 ? -elements.upper : elements.upper;
    if (upper > magnitude) magnitude = upper;
    *plan = {array, counter, sum, limit, magnitude ? INT53_MAX / magnitude : INT53_MAX};
    return true;
}

static void plan_return(MvpFunction* f, Type* type, bool external) {
    ValueRep rep = external ? VALUE_REP_ITEM : mvp_rep(type);
    bool native = rep != VALUE_REP_ITEM;
    f->result_contract = type;
    f->abi.entry = {native ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY,
        true, false, false, external};
    f->abi.result.normal = {type ? type->type_id : LMD_TYPE_ANY, rep,
        native ? SCALAR_RETURN_NONE : SCALAR_RETURN_DYNAMIC};
    f->abi.result.error = {LMD_TYPE_ERROR, VALUE_REP_ITEM, SCALAR_RETURN_NONE};
    f->abi.result.shape = native ? (f->abi.effects.may_return_error ? RETURN_SHAPE_NATIVE_ERROR : RETURN_SHAPE_NATIVE)
        : RETURN_SHAPE_ITEM_SCALAR;
    f->abi.result.companion = em_companion_transport(f->abi.result.shape, external);
    f->abi.result.error_lane = FN_ERROR_LANE_NONE;
}

static bool admitted_call(LambdaMvpModule* m, AstCallNode* call) {
    if (call->interp_has_named_args || call->interp_has_spread_args || call->pipe_inject)
        return mvp_reject(m, call, "named, spread and pipe arguments are not admitted yet");
    if (call->propagate) return mvp_reject(m, call, "explicit error propagation is not admitted yet");
    AstNode* callee = ast_unwrap_primary_to_leaf(call->function);
    AstFuncNode* target = ast_direct_call_function(call);
    int count = ast_linked_node_count(call->argument);
    if (target) {
        MvpFunction* f = mvp_function(m, target);
        if (!f || count != f->abi.param_count)
            return mvp_reject(m, call, "a direct call requires an admitted fixed-arity definition");
        AstNamedNode* param = f->ast->param;
        for (AstNode* arg = call->argument; arg; arg = arg->next, param = (AstNamedNode*)param->next) {
            TypeParam* p = lambda_type_param(param->type);
            if (p && p->is_var_param) {
                MvpBinding* b = mvp_local_binding(m, arg);
                if (!b || !mvp_array_element(b->contract) ||
                        !lambda_array_contract_compatible(b->contract, mvp_contract(param), true))
                    return mvp_reject(m, arg, "var array arguments require an invariant local binding");
            }
        }
        return true;
    }
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC)
        return mvp_reject(m, call, "indirect calls are not admitted yet");
    SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
    if (!info || count > LAMBDA_MAX_FUNCTION_ARGS)
        return mvp_reject(m, call, "unsupported builtin argument count");
    switch (info->fn) {
    case SYSPROC_PRINT:
        return count == 1 || mvp_reject(m, call, "print currently requires one argument");
    case SYSPROC_CLOCK: case SYSFUNC_ABS: case SYSFUNC_FLOOR: case SYSFUNC_SQRT:
    case SYSFUNC_INT: case SYSFUNC_FLOAT: case SYSFUNC_STRING:
    case SYSFUNC_LEN: case SYSFUNC_BXOR: case SYSFUNC_SHL: case SYSFUNC_FILL:
        return count == info->arg_count || mvp_reject(m, call, "builtin arity mismatch");
    default:
        return mvp_reject(m, call, "builtin is outside the MVP subset admission set");
    }
}

// Module scalar slots have no GC payload. Admit only initialization that cannot
// call back into a definition before its immutable dependencies are initialized.
static bool module_scalar_initializer(LambdaMvpModule* m, AstNode* source, int depth = 0) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || depth > 32) return false;
    TypeId type = mvp_value_contract(m, n)->type_id;
    if (type != LMD_TYPE_INT && type != LMD_TYPE_FLOAT && type != LMD_TYPE_BOOL) return false;
    if (n->node_type == AST_NODE_PRIMARY) return true;
    if (n->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_local_binding(m, n);
        return b && b->owner == AST_FUNCTION_ID_INVALID && !b->entry->is_mutable &&
            b->entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
            b->entry->node->source_span.start_byte < n->source_span.start_byte &&
            module_scalar_initializer(m, ((AstDeclaratorNode*)b->entry->node)->init, depth + 1);
    }
    if (n->node_type == AST_NODE_BINARY) return
        module_scalar_initializer(m, ((AstBinaryNode*)n)->left, depth + 1) &&
        module_scalar_initializer(m, ((AstBinaryNode*)n)->right, depth + 1);
    return n->node_type == AST_NODE_UNARY &&
        module_scalar_initializer(m, ((AstUnaryNode*)n)->operand, depth + 1);
}

bool lambda_mvp_plan(LambdaMvpModule* m) {
    if (!m || !m->tp->ast_index.graph_published) return false;
    if (lambda_tier_selected() != LAMBDA_TIER_JIT)
        return mvp_reject(m, NULL, "select LAMBDA_EXEC_BACKEND=jit explicitly");
    AstIndex* index = &m->tp->ast_index;
    m->nullable_int = lambda_type_nullable_normalized(m->tp->pool, &TYPE_INT);
    m->nullable_bool = lambda_type_nullable_normalized(m->tp->pool, &TYPE_BOOL);
    m->nullable_float = lambda_type_nullable_normalized(m->tp->pool, &TYPE_FLOAT);
    m->function_count = index->function_count;
    m->binding_count = index->binding_count;
    m->functions = (MvpFunction*)mem_calloc(m->function_count, sizeof(MvpFunction), MEM_CAT_TEMP);
    m->bindings = (MvpBinding*)mem_calloc(m->binding_count, sizeof(MvpBinding), MEM_CAT_TEMP);
    if ((m->function_count && !m->functions) || (m->binding_count && !m->bindings))
        return mvp_reject(m, NULL, "compiler allocation failed");
    for (uint32_t i = 0; i < m->function_count; i++) {
        MvpFunction* f = &m->functions[i];
        f->ast = (AstFuncNode*)index->functions[i].node;
        f->id = i;
        if (!f->ast || (f->ast->node_type != AST_NODE_FUNC && f->ast->node_type != AST_NODE_PROC) ||
                index->functions[i].parent != AST_FUNCTION_ID_INVALID)
            return mvp_reject(m, f->ast, "only top-level fn/pn definitions are admitted");
        TypeFunc* signature = (TypeFunc*)f->ast->type;
        if (signature->is_public || signature->is_variadic || signature->binder_count ||
                f->ast->captures || f->ast->is_async ||
                (f->ast->analysis && f->ast->analysis->may_await))
            return mvp_reject(m, f->ast, "exported, variadic, captured or suspending definitions are not admitted");
        char name[64];
        snprintf(name, sizeof(name), "mvp_%u_%s", i, f->ast->name ? f->ast->name->chars : "fn");
        f->name = name_pool_create_name(m->tp->name_pool, name)->chars;
        f->abi.param_count = ast_linked_node_count(f->ast->param);
        if (f->abi.param_count > LAMBDA_MAX_FUNCTION_ARGS)
            return mvp_reject(m, f->ast, "too many native operands");
        f->abi.params = (FnParamAnalysis*)mem_calloc(f->abi.param_count, sizeof(FnParamAnalysis), MEM_CAT_TEMP);
        int n = 0;
        for (AstNamedNode* param = f->ast->param; param; param = (AstNamedNode*)param->next) {
            TypeParam* p = lambda_type_param(param->type);
            Type* contract = mvp_contract(param);
            if (!admitted_contract(contract) || (p && (p->is_optional || p->default_value ||
                    (p->is_var_param && !mvp_array_element(contract)))))
                return mvp_reject(m, param, "parameter contract is outside the MVP subset");
            f->abi.params[n++] = {contract->type_id, mvp_rep(contract), 0};
        }
        Type* result = f->ast->declared_return_type ? f->ast->declared_return_type : signature->returned;
        if (!admitted_contract(result)) return mvp_reject(m, f->ast, "return contract is outside the MVP subset");
        plan_return(f, result ? result : &TYPE_ANY, false);
        if (f->ast->node_type == AST_NODE_PROC && f->abi.result.normal.abi_rep != VALUE_REP_ITEM &&
                !mvp_proc_has_final_return(f->ast))
            return mvp_reject(m, f->ast, "a native procedure currently requires a final return statement");
        // open scalar contracts can reach checked admission and contained errors.
        for (int p = 0; p < f->abi.param_count; p++)
            if (f->abi.params[p].canonical_rep == VALUE_REP_ITEM) {
                f->abi.effects.may_gc = true; f->abi.effects.may_return_error = true;
            }
        if (f->ast->name && strcmp(f->ast->name->chars, "main") == 0) {
            if (f->abi.param_count) return mvp_reject(m, f->ast, "main arguments are not admitted yet");
            m->main = f;
        }
    }
    m->entry.name = "main";
    m->entry.id = AST_FUNCTION_ID_INVALID;
    plan_return(&m->entry, &TYPE_ANY, true);
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        b->entry = index->bindings[i];
        b->contract = mvp_contract(b->entry ? b->entry->node : NULL);
        if (b->entry && b->entry->node && b->entry->node->node_type == AST_NODE_FOR_CLAUSE)
            b->contract = &TYPE_INT;
        b->rep = mvp_rep(b->contract);
        AstNodeId node_id = ast_index_find(index, b->entry ? b->entry->node : NULL);
        b->owner = node_id < index->count ? index->owner_functions[node_id] : AST_FUNCTION_ID_INVALID;
    }
    // admission consumes the shared index, including bodies that are never called.
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        AstFunctionId owner = index->owner_functions[i];
        if (owner < m->function_count) m->functions[owner].inline_cost++;
        bool procedural = owner < m->function_count && m->functions[owner].ast->node_type == AST_NODE_PROC;
        if (!procedural && (node->node_type == AST_SCRIPT || node->node_type == AST_NODE_CONTENT || node->node_type == AST_NODE_BLOCK)) {
            AstNode* item = node->node_type == AST_SCRIPT ? ((AstScript*)node)->child :
                node->node_type == AST_NODE_CONTENT ? ((AstListNode*)node)->item : ((AstBlockNode*)node)->statements;
            int values = 0;
            for (; item; item = item->next)
                if (!is_declaration_node(item) && !is_side_effect_stam(item->node_type) && ++values > 1)
                    return mvp_reject(m, node, "multi-value functional content is not admitted yet");
        }
        switch (node->node_type) {
        case AST_SCRIPT: case AST_NODE_CONTENT: case AST_NODE_BLOCK:
        case AST_NODE_EXPR_STMT: case AST_NODE_PRIMARY: case AST_NODE_IDENT:
        case AST_NODE_TYPE: case AST_NODE_SYS_FUNC: case AST_NODE_FUNC: case AST_NODE_PROC:
        case AST_NODE_PARAM: case AST_NODE_RETURN_STAM: case AST_NODE_IF_EXPR:
        case AST_NODE_LET_STAM: case AST_NODE_VAR_STAM:
        case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_FOR_CLAUSE:
            break;
        case AST_NODE_VARIABLE_DECLARATOR: {
            AstDeclaratorNode* d = (AstDeclaratorNode*)node;
            if (d->is_type_definition || (!admitted_contract(mvp_contract(d)) &&
                    (d->declared_type || !lambda_type_is_union(d->type))))
                return mvp_reject(m, node, "binding contract is outside the MVP subset");
            break;
        }
        case AST_NODE_ASSIGN_STAM: case AST_NODE_INDEX_ASSIGN_STAM: {
            AstAssignStamNode* a = (AstAssignStamNode*)node;
            if (a->object && a->key) {
                MvpBinding* b = mvp_local_binding(m, a->object);
                if (!b || !mvp_array_element(b->contract) || b->owner != owner)
                    return mvp_reject(m, node, "indexed writes require a local primitive array binding");
            } else if (!a->target_entry || a->object || a->key)
                return mvp_reject(m, node, "assignment requires a resolved local binding");
            break;
        }
        case AST_NODE_INDEX_EXPR:
            break;
        case AST_NODE_ARRAY: {
            Type* element = NULL;
            for (AstNode* item = ((AstArrayNode*)node)->item; item; item = item->next) {
                Type* type = mvp_value_contract(m, item);
                if ((type->type_id != LMD_TYPE_INT && type->type_id != LMD_TYPE_FLOAT && type->type_id != LMD_TYPE_BOOL) ||
                        (element && type->type_id != element->type_id))
                    return mvp_reject(m, node, "array literals require homogeneous primitive members");
                element = type;
            }
            break;
        }
        case AST_NODE_FOR_EXPR: {
            AstForNode* f = (AstForNode*)node;
            AstLoopNode* loop = (AstLoopNode*)f->loop;
            AstNode* source = loop ? ast_unwrap_primary_to_leaf(loop->as) : NULL;
            if (!f->discard_result || !loop || loop->next || loop->index_entry || loop->on ||
                    loop->key_only || loop->key_filter || loop->optional || f->let_clause || f->where ||
                    f->group || f->order || f->limit || f->offset || !source || source->node_type != AST_NODE_BINARY ||
                    ((AstBinaryNode*)source)->op != OPERATOR_TO ||
                    mvp_value_contract(m, ((AstBinaryNode*)source)->left)->type_id != LMD_TYPE_INT ||
                    mvp_value_contract(m, ((AstBinaryNode*)source)->right)->type_id != LMD_TYPE_INT)
                return mvp_reject(m, node, "for requires a single procedural integer range");
            break;
        }
        case AST_NODE_LOOP:
            if (((AstLoopControlNode*)node)->form != LOOP_FORM_WHILE)
                return mvp_reject(m, node, "only while loops are admitted in the scalar MVP");
            break;
        case AST_NODE_CALL_EXPR:
            if (!admitted_call(m, (AstCallNode*)node)) return false;
            break;
        case AST_NODE_BINARY: {
            Operator op = ((AstBinaryNode*)node)->op;
            if (op == OPERATOR_TO) {
                AstNodeId parent = ast_index_parent_id(index, i);
                while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
                    parent = ast_index_parent_id(index, parent);
                if (parent < index->count && index->nodes[parent]->node_type == AST_NODE_FOR_CLAUSE) break;
            }
            if (op != OPERATOR_ADD && op != OPERATOR_SUB && op != OPERATOR_MUL &&
                op != OPERATOR_DIV && op != OPERATOR_IDIV && op != OPERATOR_MOD &&
                op != OPERATOR_EQ && op != OPERATOR_NE && op != OPERATOR_LT &&
                op != OPERATOR_LE && op != OPERATOR_GT && op != OPERATOR_GE &&
                op != OPERATOR_AND && op != OPERATOR_OR && op != OPERATOR_JOIN)
                return mvp_reject(m, node, "binary operator is not admitted yet");
            break;
        }
        case AST_NODE_UNARY: {
            Operator op = ((AstUnaryNode*)node)->op;
            if (op != OPERATOR_NEG && op != OPERATOR_NOT && op != OPERATOR_POS)
                return mvp_reject(m, node, "unary operator is not admitted yet");
            break;
        }
        default: return mvp_reject(m, node, interp_node_kind_name(node->node_type));
        }
    }
    // a direct-call fixed point keeps scalar-only recursion free of GC ceremony.
    bool changed;
    do {
        changed = false;
        for (AstNodeId i = 0; i < index->count; i++) {
            AstNode* node = index->nodes[i];
            AstFunctionId owner = index->owner_functions[i];
            MvpFunction* f = owner < m->function_count ? &m->functions[owner] : &m->entry;
            bool collects = false, fails = false, reenters = false;
            if (node->node_type == AST_NODE_ARRAY || node->node_type == AST_NODE_INDEX_EXPR || node->node_type == AST_NODE_FOR_EXPR ||
                    node->node_type == AST_NODE_INDEX_ASSIGN_STAM)
                collects = fails = true;
            if (node->node_type == AST_NODE_CALL_EXPR) {
                AstCallNode* call = (AstCallNode*)node;
                MvpFunction* callee = mvp_function(m, ast_direct_call_function(call));
                if (callee) {
                    collects = callee->abi.effects.may_gc;
                    fails = callee->abi.effects.may_return_error;
                    reenters = callee->abi.effects.may_reenter;
                    AstNamedNode* param = callee->ast->param;
                    for (AstNode* arg = call->argument; arg; arg = arg->next, param = (AstNamedNode*)param->next)
                        fails |= lambda_static_boundary_relation(mvp_value_contract(m, arg), mvp_contract(param), true) != STATIC_BOUNDARY_PROVEN;
                }
                else {
                    AstNode* target = ast_unwrap_primary_to_leaf(call->function);
                    SysFuncInfo* info = ((AstSysFuncNode*)target)->fn_info;
                    JitImportMetadata metadata = {};
                    TypeId input = mvp_value_contract(m, call->argument)->type_id;
                    bool native_math = (info->fn == SYSFUNC_ABS || info->fn == SYSFUNC_FLOOR || info->fn == SYSFUNC_SQRT) && input == LMD_TYPE_FLOAT;
                    bool native_cast = (info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) &&
                        (input == LMD_TYPE_INT || input == LMD_TYPE_FLOAT);
                    bool known = jit_import_get_metadata(native_math ? info->native_c_name : info->c_func_name, &metadata);
                    collects = !known || metadata.gc_effect != JIT_EFFECT_NO_GC;
                    reenters = !known || metadata.reentry_effect != JIT_REENTRY_NO;
                    if (native_cast) collects = reenters = false;
                    fails = mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
                }
            } else if (node->node_type == AST_NODE_BINARY &&
                    ((AstBinaryNode*)node)->op == OPERATOR_JOIN) { collects = true; fails = true; }
            // unresolved boundary admission may allocate an error even on a native function.
            if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
                AstDeclaratorNode* d = (AstDeclaratorNode*)node;
                MvpBinding* b = mvp_binding(m, d->entry);
                Type* source = mvp_value_contract(m, d->init);
                if (!d->declared_type && b && source->type_id == LMD_TYPE_ANY && b->contract != &TYPE_ANY) {
                    b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM; changed = true;
                }
                fails |= d->declared_type && lambda_static_boundary_relation(source, d->declared_type, false) != STATIC_BOUNDARY_PROVEN;
            }
            if (node->node_type == AST_NODE_ASSIGN_STAM) {
                AstAssignStamNode* a = (AstAssignStamNode*)node;
                MvpBinding* b = mvp_binding(m, a->target_entry);
                Type* source = mvp_value_contract(m, a->value);
                AstDeclaratorNode* declaration = b && b->entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR
                    ? (AstDeclaratorNode*)b->entry->node : NULL;
                if (declaration && !declaration->declared_type && source->type_id == LMD_TYPE_ANY && b->contract != &TYPE_ANY) {
                    b->contract = &TYPE_ANY; b->rep = VALUE_REP_ITEM; changed = true;
                }
                fails |= !b || lambda_static_boundary_relation(source, b->contract, false) != STATIC_BOUNDARY_PROVEN;
            }
            if (node->node_type == AST_NODE_RETURN_STAM) {
                AstNode* v = ((AstReturnNode*)node)->value;
                fails |= lambda_static_boundary_relation(mvp_value_contract(m, v), f->result_contract, false) != STATIC_BOUNDARY_PROVEN;
            }
            if (node->node_type == AST_NODE_BINARY || node->node_type == AST_NODE_UNARY)
                fails |= mvp_value_contract(m, node)->type_id == LMD_TYPE_ANY;
            collects |= fails;
            if (collects && !f->abi.effects.may_gc) { f->abi.effects.may_gc = true; changed = true; }
            if (fails && !f->abi.effects.may_return_error) { f->abi.effects.may_return_error = true; changed = true; }
            if (reenters && !f->abi.effects.may_reenter) { f->abi.effects.may_reenter = true; changed = true; }
        }
    } while (changed);
    if (m->main) {
        m->entry.abi.effects.may_gc |= m->main->abi.effects.may_gc;
        m->entry.abi.effects.may_reenter |= m->main->abi.effects.may_reenter;
        m->entry.abi.effects.may_return_error |= m->main->abi.effects.may_return_error;
    }
    // publish one final return ABI after recursive error effects have converged (D8.4.2v2).
    for (uint32_t i = 0; i < m->function_count; i++)
        plan_return(&m->functions[i], m->functions[i].result_contract, false);
    plan_integer_ranges(m);
    for (AstNodeId i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        AstNode* object = NULL; AstNode* key = NULL;
        if (node->node_type == AST_NODE_INDEX_EXPR) {
            object = ((AstFieldNode*)node)->object; key = ((AstFieldNode*)node)->field;
        } else if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            object = ((AstAssignStamNode*)node)->object; key = ((AstAssignStamNode*)node)->key;
        }
        MvpBinding* b = mvp_local_binding(m, object);
        if (!b || !mvp_array_element(b->contract)) continue;
        MvpIntRange range = mvp_int_range(m, key);
        if (range.finite && range.lower >= 0 && range.upper < INT53_MAX && range.upper + 1 > b->array_extent)
            b->array_extent = range.upper + 1;
    }
    bool module_scalars = false;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (!b->entry || !b->entry->node || b->owner != AST_FUNCTION_ID_INVALID ||
                b->entry->is_mutable || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* d = (AstDeclaratorNode*)b->entry->node;
        uint32_t use_count = 0;
        const AstNodeId* uses = ast_index_binding_uses(&m->tp->ast_index, i, &use_count);
        bool cross_function = false;
        for (uint32_t use = 0; use < use_count; use++)
            cross_function |= m->tp->ast_index.owner_functions[uses[use]] != AST_FUNCTION_ID_INVALID;
        if (!cross_function) continue;
        if (!module_scalar_initializer(m, d->init) ||
                (d->declared_type && lambda_static_boundary_relation(mvp_value_contract(m, d->init), b->contract, false) != STATIC_BOUNDARY_PROVEN) ||
                (b->rep != VALUE_REP_F64 && b->rep != VALUE_REP_INT_LANE && b->rep != VALUE_REP_I64)) continue;
        b->module_scalar = true;
        module_scalars = true;
    }
    // This slice has constant-only initialization. A top-level call could
    // observe a later scalar before its slot was initialized (S7.7.6).
    for (AstNodeId i = 0; module_scalars && i < m->tp->ast_index.count; i++)
        if (m->tp->ast_index.owner_functions[i] == AST_FUNCTION_ID_INVALID &&
                m->tp->ast_index.nodes[i]->node_type == AST_NODE_CALL_EXPR)
            return mvp_reject(m, m->tp->ast_index.nodes[i], "shared module scalars require call-free initialization");
    return !m->failed;
}

bool lambda_mvp_lower(LambdaMvpModule* m) {
    if (!m || m->failed) return false;
    m->module = MIR_new_module(m->ctx, "lambda_mvp");
    m->imports = em_import_cache_new(32);
    m->consts = MIR_new_bss(m->ctx, "_mod_consts_ptr", sizeof(void*));
    MIR_new_bss(m->ctx, "_mod_layout", sizeof(LambdaModuleLayout));
    MIR_new_bss(m->ctx, "_mod_type_list_ptr", sizeof(void*));
    for (uint32_t i = 0; i < m->binding_count; i++) if (m->bindings[i].module_scalar) {
        char name[40]; snprintf(name, sizeof(name), "mvp_module_scalar_%u", i);
        m->bindings[i].module_slot = MIR_new_bss(m->ctx, name, sizeof(uint64_t));
    }
    for (uint32_t i = 0; i < m->function_count; i++)
        m->functions[i].target = MIR_new_forward(m->ctx, m->functions[i].name);
    for (uint32_t i = 0; i < m->function_count && !m->failed; i++)
        mvp_emit_function(m, &m->functions[i]);
    if (!m->failed) mvp_emit_function(m, &m->entry);
    MIR_finish_module(m->ctx);
    return !m->failed;
}

bool lambda_mvp_finalize(LambdaMvpModule* m, LambdaMvpStats* stats) {
    if (!m || m->failed || !m->module) return false;
    mir_dump_finalized(m->ctx, host_path_dir(HOST_PATH_TEMP, "mir_dump.txt"), false);
    MIR_load_module(m->ctx, m->module);
    if (stats) *stats = m->stats;
    return true;
}

void lambda_mvp_destroy(LambdaMvpModule* m) {
    if (!m) return;
    for (uint32_t i = 0; m->functions && i < m->function_count; i++) mem_free(m->functions[i].abi.params);
    mem_free(m->functions);
    mem_free(m->bindings);
    if (m->imports) hashmap_free(m->imports);
    mem_free(m);
}
