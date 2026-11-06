#pragma once

#include "ast.hpp"
#include "mir_emitter_shared.hpp"
#include "write_set.hpp"

struct MirPipeContext {
    bool in_pipe;
    MIR_reg_t item, index, parent, root;
};

// a single subject has no member key; provisos retain the traversal's
// parent, while match arms make the enclosing subject their parent (S10.1.3).
static inline MirPipeContext em_subject_context(MirEmitter* em,
        MirPipeContext saved, MIR_reg_t subject, bool reparent = false) {
    MIR_reg_t absent = em_new_reg(em, "subject_no_key", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, absent), MIR_new_int_op(em->ctx, ITEM_NULL)));
    return {true, subject, absent,
        saved.in_pipe ? (reparent ? saved.item : saved.parent) : absent,
        saved.in_pipe ? saved.root : subject};
}

typedef MIR_reg_t (*MirPipeBodyEmitter)(void*, AstPipeNode*, MirPipeContext, MIR_reg_t keys);

// S10.1.2v4/S10.1.6: pipes use the same keyed walk as for-in. The frontend
// owns body control/cleanup admission and implicit-context restoration.
static inline MIR_reg_t em_pipe_walk(MirEmitter* em, AstPipeNode* pipe,
        MIR_reg_t source, MirPipeContext outer, MirPipeBodyEmitter emit_body) {
    MIR_context_t ctx = em->ctx;
    MIR_reg_t kind = em_call_1(em, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(ctx, source), true);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_UEXT8,
        MIR_new_reg_op(ctx, kind), MIR_new_reg_op(ctx, kind)));
    MIR_reg_t scalar = em_new_reg(em, "pipe_scalar", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, scalar), MIR_new_int_op(ctx, 1)));
    MIR_label_t sequence = em_new_label(em), classified = em_new_label(em);
    MIR_reg_t band = em_new_reg(em, "pipe_container_band", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_SUB, MIR_new_reg_op(ctx, band),
        MIR_new_reg_op(ctx, kind), MIR_new_int_op(ctx, LMD_TYPE_CONTAINER)));
    em_emit_insn(em, MIR_new_insn(ctx, MIR_UBLE, MIR_new_label_op(ctx, sequence),
        MIR_new_reg_op(ctx, band), MIR_new_int_op(ctx, LMD_TYPE_VELMT - LMD_TYPE_CONTAINER)));
    const TypeId non_scalars[] = {LMD_TYPE_NULL, LMD_TYPE_PATH,
        LMD_TYPE_STRING, LMD_TYPE_SYMBOL, LMD_TYPE_BINARY};
    for (TypeId non_scalar : non_scalars)
        em_emit_insn(em, MIR_new_insn(ctx, MIR_BEQ, MIR_new_label_op(ctx, sequence),
            MIR_new_reg_op(ctx, kind), MIR_new_int_op(ctx, non_scalar)));
    em_emit_insn(em, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, classified)));
    em_emit_label(em, sequence);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, scalar), MIR_new_int_op(ctx, 0)));
    em_emit_label(em, classified);
    MIR_reg_t keys = em_call_1(em, "item_keys", MIR_T_P,
        MIR_T_I64, MIR_new_reg_op(ctx, source), true);
    MIR_reg_t length = em_call_3(em, "iter_len", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(ctx, source), MIR_T_P, MIR_new_reg_op(ctx, keys),
        MIR_T_I64, MIR_new_int_op(ctx, LOOP_KEY_ALL), true);
    MIR_label_t counted = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_BF, MIR_new_label_op(ctx, counted), MIR_new_reg_op(ctx, scalar)));
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, length), MIR_new_int_op(ctx, 1)));
    em_emit_label(em, counted);
    MIR_reg_t output = em_call_0(em, "array", MIR_T_P, true);
    if (em->root_call_value) em->root_call_value(em->call_owner, output, JIT_VALUE_RAW_GC_POINTER);
    MIR_reg_t index = em_new_reg(em, "pipe_index", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, index), MIR_new_int_op(ctx, 0)));
    MIR_label_t test = em_new_label(em), done = em_new_label(em), next = em_new_label(em);
    em_emit_label(em, test);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, done),
        MIR_new_reg_op(ctx, index), MIR_new_reg_op(ctx, length)));
    MIR_reg_t member = em_new_reg(em, "pipe_member", MIR_T_I64);
    MIR_label_t member_ready = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, member), MIR_new_reg_op(ctx, source)));
    em_emit_insn(em, MIR_new_insn(ctx, MIR_BT, MIR_new_label_op(ctx, member_ready), MIR_new_reg_op(ctx, scalar)));
    MIR_reg_t selected = em_call_4(em, "iter_val_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(ctx, source), MIR_T_P, MIR_new_reg_op(ctx, keys),
        MIR_T_I64, MIR_new_reg_op(ctx, index), MIR_T_I64, MIR_new_int_op(ctx, LOOP_KEY_ALL), true);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV,
        MIR_new_reg_op(ctx, member), MIR_new_reg_op(ctx, selected)));
    em_emit_label(em, member_ready);
    if (em->root_call_value) em->root_call_value(em->call_owner, member, JIT_VALUE_BOXED_ITEM);
    MIR_reg_t key = em_call_4(em, "iter_key_at", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(ctx, source), MIR_T_P, MIR_new_reg_op(ctx, keys),
        MIR_T_I64, MIR_new_reg_op(ctx, index), MIR_T_I64, MIR_new_int_op(ctx, LOOP_KEY_ALL), true);
    if (em->root_call_value) em->root_call_value(em->call_owner, key, JIT_VALUE_BOXED_ITEM);
    MIR_reg_t produced = emit_body(em->call_owner, pipe,
        {true, member, key, source, outer.in_pipe ? outer.root : source}, keys);
    bool filter = pipe->op == OPERATOR_FILTER;
    if (filter)
        em_emit_insn(em, MIR_new_insn(ctx, MIR_BNE, MIR_new_label_op(ctx, next),
            MIR_new_reg_op(ctx, produced), MIR_new_int_op(ctx, BOOL_TRUE)));
    em_call_void_2(em, "array_push", MIR_T_P, MIR_new_reg_op(ctx, output),
        MIR_T_I64, MIR_new_reg_op(ctx, filter ? member : produced), true);
    em_emit_label(em, next);
    em_emit_insn(em, MIR_new_insn(ctx, MIR_ADD, MIR_new_reg_op(ctx, index),
        MIR_new_reg_op(ctx, index), MIR_new_int_op(ctx, 1)));
    em_emit_insn(em, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, test)));
    em_emit_label(em, done);
    em_call_void_1(em, "symbol_key_list_free", MIR_T_P, MIR_new_reg_op(ctx, keys), true);
    return em_call_2(em, "pipe_end", MIR_T_I64, MIR_T_P, MIR_new_reg_op(ctx, output),
        MIR_T_I64, MIR_new_reg_op(ctx, source), true);
}

typedef void (*MirJoinCollectSource)(void*, AstLoopNode*, MIR_reg_t rows,
    MIR_reg_t keys, MIR_reg_t indices);
typedef MIR_reg_t (*MirJoinPriorKey)(void*, AstLoopNode* first,
    AstLoopNode* stop, MIR_reg_t tuple);

static inline MIR_reg_t em_query_array(MirEmitter* em) {
    MIR_reg_t array = em_call_0(em, "array_plain", MIR_T_P, true);
    if (em->root_call_value) em->root_call_value(em->call_owner, array, JIT_VALUE_RAW_GC_POINTER);
    return array;
}

typedef AstNode* (*MirKeyExprOf)(AstNode*);
static inline AstNode* em_group_key_expr(AstNode* node) { return ((AstGroupKey*)node)->expr; }
static inline AstNode* em_order_key_expr(AstNode* node) { return ((AstOrderSpec*)node)->expr; }
static inline AstNode* em_join_prior_expr(AstNode* node) { return ((AstJoinKey*)node)->prior_expr; }
static inline AstNode* em_join_new_expr(AstNode* node) { return ((AstJoinKey*)node)->new_expr; }

static inline MIR_reg_t em_query_key(MirEmitter* em, AstNode* first, MirKeyExprOf expression) {
    MIR_reg_t tuple = first && first->next ? em_query_array(em) : 0;
    for (AstNode* node = first; node; node = node->next) {
        MirValue part = em_require_rep(em, em_lower_value(em, expression(node), MIR_VALUE_ANY), VALUE_REP_ITEM);
        if (!tuple) return part.reg;
        em_call_void_2(em, "array_push_verbatim", MIR_T_P, MIR_new_reg_op(em->ctx, tuple),
            MIR_T_I64, MIR_new_reg_op(em->ctx, part.reg), true);
    }
    if (tuple) return tuple;
    MIR_reg_t absent = em_new_reg(em, "absent_query_key", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, absent), MIR_new_int_op(em->ctx, ITEM_NULL)));
    return absent;
}

static inline MIR_reg_t em_query_name(MirEmitter* em, String* name) {
    MIR_reg_t item = em_new_reg(em, "query_name", MIR_T_I64);
    if (name) {
        MIR_reg_t string = em_call_1(em, "heap_create_name", MIR_T_P,
            MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)name->chars), true);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, item),
            MIR_new_reg_op(em->ctx, string), MIR_new_int_op(em->ctx, (uint64_t)LMD_TYPE_STRING << 56)));
    } else em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, item), MIR_new_int_op(em->ctx, ITEM_NULL)));
    if (em->root_call_value) em->root_call_value(em->call_owner, item, JIT_VALUE_BOXED_ITEM);
    return item;
}

// S14.1.3: both emitters materialize each join source once and use the same
// stable hash/cross-product stages; only lexical binding differs by backend.
static inline MIR_reg_t em_join_stream(MirEmitter* em, AstLoopNode* first,
        MirJoinCollectSource collect, MirJoinPriorKey prior_key) {
    MIR_context_t ctx = em->ctx;
    auto op = [ctx](MIR_reg_t reg) { return MIR_new_reg_op(ctx, reg); };
    auto number = [ctx](int64_t n) { return MIR_new_int_op(ctx, n); };
    auto absent = [&]() {
        MIR_reg_t reg = em_new_reg(em, "absent_join_indices", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV, op(reg), number(ITEM_NULL)));
        return reg;
    };
    MIR_reg_t rows = em_query_array(em);
    MIR_reg_t indices = first->index_name ? em_query_array(em) : 0;
    collect(em->call_owner, first, rows, 0, indices);
    MIR_reg_t tuples = em_call_4(em, "fn_join_seed_tuples", MIR_T_P,
        MIR_T_I64, op(rows), MIR_T_I64, op(em_query_name(em, first->name)),
        MIR_T_I64, op(em_query_name(em, first->index_name)), MIR_T_I64, op(indices ? indices : absent()), true);
    if (em->root_call_value) em->root_call_value(em->call_owner, tuples, JIT_VALUE_RAW_GC_POINTER);
    for (AstLoopNode* source = (AstLoopNode*)first->next; source; source = (AstLoopNode*)source->next) {
        rows = em_query_array(em);
        indices = source->index_name ? em_query_array(em) : 0;
        MIR_reg_t keys = source->on ? em_query_array(em) : 0;
        collect(em->call_owner, source, rows, keys, indices);
        MIR_reg_t name = em_query_name(em, source->name);
        MIR_reg_t index_name = em_query_name(em, source->index_name);
        MIR_reg_t index_values = indices ? indices : absent();
        if (!source->on) {
            tuples = em_call_5(em, "fn_cross_join_tuples", MIR_T_P,
                MIR_T_I64, op(tuples), MIR_T_I64, op(rows), MIR_T_I64, op(name),
                MIR_T_I64, op(index_name), MIR_T_I64, op(index_values), true);
        } else {
            MIR_reg_t prior_keys = em_query_array(em);
            MIR_reg_t length = em_call_1(em, "fn_len", MIR_T_I64, MIR_T_I64, op(tuples), true);
            MIR_reg_t cursor = em_new_reg(em, "join_probe", MIR_T_I64);
            em_emit_insn(em, MIR_new_insn(ctx, MIR_MOV, op(cursor), number(0)));
            MIR_label_t head = em_new_label(em), done = em_new_label(em);
            em_emit_label(em, head);
            em_emit_insn(em, MIR_new_insn(ctx, MIR_BGE, MIR_new_label_op(ctx, done), op(cursor), op(length)));
            MIR_reg_t tuple = em_call_2(em, "item_at", MIR_T_I64,
                MIR_T_I64, op(tuples), MIR_T_I64, op(cursor), true);
            MIR_reg_t key = prior_key(em->call_owner, first, source, tuple);
            em_call_void_2(em, "array_push_verbatim", MIR_T_P, op(prior_keys), MIR_T_I64, op(key), true);
            em_emit_insn(em, MIR_new_insn(ctx, MIR_ADD, op(cursor), op(cursor), number(1)));
            em_emit_insn(em, MIR_new_insn(ctx, MIR_JMP, MIR_new_label_op(ctx, head)));
            em_emit_label(em, done);
            tuples = em_call_8(em, "fn_hash_join_tuples", MIR_T_P,
                MIR_T_I64, op(tuples), MIR_T_I64, op(prior_keys), MIR_T_I64, op(rows), MIR_T_I64, op(keys),
                MIR_T_I64, op(name), MIR_T_I64, number(source->optional),
                MIR_T_I64, op(index_name), MIR_T_I64, op(index_values), true);
        }
        if (em->root_call_value) em->root_call_value(em->call_owner, tuples, JIT_VALUE_RAW_GC_POINTER);
    }
    return tuples;
}

// the shared AST proves a store-back region; runtime ancestor sharing decides
// whether its initial child can be borrowed or needs a snapshot (D5.4.4).
static inline MIR_reg_t em_bind_rmw_handle(MirEmitter* em, MIR_reg_t owner,
        MIR_reg_t child, int links, const MIR_reg_t* keys, bool specialize_root = false) {
    // the deeper helper can materialize indexed ancestors; only a proven
    // zero-link call selects the audited noncollecting entry.
    if (links == 0 && specialize_root) return em_call_2(em, "cow_bind_rmw_root", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, owner),
        MIR_T_I64, MIR_new_reg_op(em->ctx, child), true);
    return em_call_5(em, "cow_bind_rmw_handle", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, owner),
        MIR_T_I64, MIR_new_reg_op(em->ctx, child),
        MIR_T_I64, MIR_new_int_op(em->ctx, links),
        MIR_T_I64, MIR_new_reg_op(em->ctx, keys[0]),
        MIR_T_I64, MIR_new_reg_op(em->ctx, keys[1]), true);
}

// static path syntax has no evaluated operands; both emitters use the same
// constructors and the activation's owning pool (D2.3.5, D5.1.2).
static inline MIR_reg_t em_literal_path(MirEmitter* em, AstPathNode* node) {
    MIR_reg_t pool = em_load_at(em, em->frame.runtime, offsetof(Context, pool), MIR_T_P, "path_pool");
    MIR_reg_t path = node->authority
        ? em_call_3(em, "path_new_authority", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, pool),
            MIR_T_I64, MIR_new_int_op(em->ctx, node->scheme), MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)node->authority->chars), true)
        : em_call_2(em, "path_new", MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, pool),
            MIR_T_I64, MIR_new_int_op(em->ctx, node->scheme), true);
    for (int i = 0; i < node->segment_count; i++) {
        AstPathSegment* part = &node->segments[i];
        const char* unary = part->type == LPATH_SEG_WILDCARD ? "path_wildcard" :
            part->type == LPATH_SEG_WILDCARD_REC ? "path_wildcard_recursive" :
            part->type == LPATH_SEG_PARENT ? "path_select_parent" :
            part->type == LPATH_SEG_ROOT ? "path_select_root" : NULL;
        if (unary) path = em_call_2(em, unary, MIR_T_P, MIR_T_P, MIR_new_reg_op(em->ctx, pool),
            MIR_T_P, MIR_new_reg_op(em->ctx, path), true);
        else path = em_call_3(em, part->type == LPATH_SEG_INT ? "path_extend_int" : "path_extend", MIR_T_P,
            MIR_T_P, MIR_new_reg_op(em->ctx, pool), MIR_T_P, MIR_new_reg_op(em->ctx, path),
            part->type == LPATH_SEG_INT ? MIR_T_I64 : MIR_T_P,
            MIR_new_int_op(em->ctx, part->type == LPATH_SEG_INT ? part->int_value : (intptr_t)(part->name ? part->name->chars : "")), true);
    }
    return path;
}

// computed keys are values; following literal steps remain keys rather than
// path-property access (S2.4.2v5). Each helper result uses the shared root hook.
static inline MIR_reg_t em_computed_path(MirEmitter* em, AstPathIndexNode* node) {
    MirValue base = em_require_rep(em,
        em_lower_value(em, node->base_path, MIR_VALUE_ANY), VALUE_REP_ITEM);
    MirValue key = em_require_rep(em,
        em_lower_value(em, node->segment_expr, MIR_VALUE_ANY), VALUE_REP_ITEM);
    MIR_reg_t path = em_call_2(em, "fn_path_key", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, base.reg),
        MIR_T_I64, MIR_new_reg_op(em->ctx, key.reg), true);
    for (int i = 0; i < node->segment_count; i++) {
        AstPathSegment* step = &node->segments[i];
        path = em_call_4(em, "fn_path_step", MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, path),
            MIR_T_I64, MIR_new_int_op(em->ctx, step->type),
            MIR_T_P, MIR_new_int_op(em->ctx, (intptr_t)(step->name ? step->name->chars : NULL)),
            MIR_T_I64, MIR_new_int_op(em->ctx, step->int_value), true);
    }
    return path;
}

struct MirNavigationContext {
    bool active;
    MIR_reg_t parent;
    MIR_reg_t root;
};

// S8.2.5: wildcard members walk items instead of reading a spelled name.
static inline MIR_reg_t em_wildcard_step(MirEmitter* em, AstFieldNode* node, int wildcard) {
    MirValue owner = em_require_rep(em,
        em_lower_value(em, node->object, MIR_VALUE_ANY), VALUE_REP_ITEM);
    return em_call_1(em, wildcard == 1 ? "fn_step_items" : "fn_step_closure", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, owner.reg), true);
}

// the cursor lanes belong to the activation; ordinary values use the shared
// path/run navigation kernel (S8.2.6, S10.4.3v2, S10.5.3v2).
static inline MIR_reg_t em_navigation_value(MirEmitter* em, AstNavigationNode* node,
        MirNavigationContext cursor, ValueRep* result_rep) {
    *result_rep = VALUE_REP_ITEM;
    if (node->object) {
        MirValue object = em_lower_value(em, node->object, MIR_VALUE_ANY);
        if (object.semantic_type == LMD_TYPE_PATH) {
            MirValue path = em_require_rep(em, object, VALUE_REP_RAW_GC_POINTER);
            MIR_reg_t pool = em_load_at(em, em->frame.runtime, offsetof(Context, pool), MIR_T_P, "path_pool");
            *result_rep = VALUE_REP_RAW_GC_POINTER;
            return em_call_2(em, node->root ? "path_select_root" : "path_select_parent", MIR_T_P,
                MIR_T_P, MIR_new_reg_op(em->ctx, pool), MIR_T_P, MIR_new_reg_op(em->ctx, path.reg), true);
        }
        if (cursor.active && ast_navigation_chain_has_current(node->object)) {
            if (node->root && cursor.root) return cursor.root;
            AstNode* parent = ast_navigation_direct_parent(node->object);
            if (!node->root && parent)
                return em_require_rep(em, em_lower_value(em, parent, MIR_VALUE_ANY), VALUE_REP_ITEM).reg;
            AstNode* source = ast_unwrap_primary(node->object);
            bool consumed = false;
            if (source && source->node_type == AST_NODE_NAVIGATION_EXPR) {
                AstNavigationNode* inner = (AstNavigationNode*)source;
                AstNode* subject = ast_unwrap_primary(inner->object);
                consumed = inner->root || (subject && subject->node_type == AST_NODE_CURRENT_ITEM);
            }
            if (!node->root && !consumed && cursor.parent) return cursor.parent;
        } else {
            MirValue item = em_require_rep(em, object, VALUE_REP_ITEM);
            return em_call_2(em, "fn_navigate_value", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(em->ctx, item.reg),
                MIR_T_I64, MIR_new_int_op(em->ctx, node->root ? 1 : 0), true);
        }
    }
    MIR_reg_t result = em_new_reg(em, "absent_navigation", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result),
        MIR_new_int_op(em->ctx, ITEM_NULL)));
    return result;
}

typedef MIR_reg_t (*MirCrudKeyReader)(void* owner, AstNode* key);

// S12.4: each recorded edit uses the same runtime ABI in both compilers.
static inline MIR_reg_t em_crud_statement(MirEmitter* em, AstCrudNode* crud,
        MirCrudKeyReader read_key, MirResultCheck check_result) {
    if (crud->write_op == CRUD_OP_SEQUENCE) {
        MIR_reg_t last = em_new_reg(em, "edit_sequence", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
            MIR_new_reg_op(em->ctx, last), MIR_new_int_op(em->ctx, ITEM_NULL)));
        for (AstNode* clause = crud->value; clause; clause = clause->next)
            last = em_crud_statement(em, (AstCrudNode*)clause, read_key, check_result);
        return last;
    }
    MIR_reg_t result;
    if (crud->write_op == CRUD_OP_COMMIT || crud->write_op == CRUD_OP_ROLLBACK) {
        result = em_call_0(em, crud->write_op == CRUD_OP_COMMIT ? "fn_commit" : "fn_rollback", MIR_T_I64, true);
    } else {
        MirValue target = em_require_rep(em,
            em_lower_value(em, crud->object, MIR_VALUE_ANY), VALUE_REP_ITEM);
        MirValue item = em_require_rep(em,
            em_lower_value(em, crud->value, MIR_VALUE_ANY), VALUE_REP_ITEM);
        if (crud->write_op == WRITE_OP_BEFORE || crud->write_op == WRITE_OP_AFTER ||
                crud->write_op == WRITE_OP_INTO) {
            const char* helper = crud->write_op == WRITE_OP_BEFORE ? "fn_put_before" :
                crud->write_op == WRITE_OP_AFTER ? "fn_put_after" : "fn_put_into";
            result = em_call_2(em, helper, MIR_T_I64, MIR_T_I64, MIR_new_reg_op(em->ctx, item.reg),
                MIR_T_I64, MIR_new_reg_op(em->ctx, target.reg), true);
        } else {
            bool remove = crud->write_op == WRITE_OP_DELETE;
            if (crud->key) {
                MIR_reg_t key = read_key(em->call_owner, crud->key);
                result = remove ? em_call_2(em, "fn_del_member", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(em->ctx, target.reg),
                    MIR_T_I64, MIR_new_reg_op(em->ctx, key), true) :
                    em_call_3(em, "fn_put_member", MIR_T_I64,
                        MIR_T_I64, MIR_new_reg_op(em->ctx, target.reg),
                        MIR_T_I64, MIR_new_reg_op(em->ctx, key),
                        MIR_T_I64, MIR_new_reg_op(em->ctx, item.reg), true);
            } else result = remove ? em_call_1(em, "fn_del_node", MIR_T_I64,
                MIR_T_I64, MIR_new_reg_op(em->ctx, target.reg), true) :
                em_call_2(em, "fn_put_node", MIR_T_I64,
                    MIR_T_I64, MIR_new_reg_op(em->ctx, target.reg),
                    MIR_T_I64, MIR_new_reg_op(em->ctx, item.reg), true);
        }
    }
    check_result(em->call_owner, result);
    return result;
}
