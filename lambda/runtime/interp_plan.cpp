// Frame-plan pass for the T0 AST interpreter (AI5).
//
// Two products, both derived from one complete Lambda AST traversal:
//   1. `interp_plan_script` — assigns NameEntry slots + BindingStorage classes
//      and computes each function's static activation shape (FnFramePlan).
//   2. `interp_scan_supported` — the whole-AST pre-scan that decides whether a
//      Script can run under T0 at all, so an unsupported kind produces a
//      counted whole-module fallback instead of a silent wrong answer (R4).
//
// The traversal owns Lambda extension edges and delegates shared layouts to
// `ast_visit_core_children`; `LangProfile::visit_ext_children` remains the
// seam for language-specific children (D8.2.4).

#include "interp.hpp"
#include "re2_wrapper.hpp"
#include "safety_analyzer.hpp"
#include "type_contract.hpp"
#include "../../lib/log.h"

// ---------------------------------------------------------------------------
// Complete Lambda child traversal
// ---------------------------------------------------------------------------

typedef struct InterpCoreChildAdapter { InterpAstChildVisitor visit; void* ctx; } InterpCoreChildAdapter;

static void interp_visit_core_child(AstNode* child, AstNode* parent, void* opaque) {
    InterpCoreChildAdapter* adapter = (InterpCoreChildAdapter*)opaque;
    if (!adapter || !adapter->visit || !child || child == parent->next) return;
    for (AstNode* item = child; item; item = item->next) {
        adapter->visit(item, adapter->ctx);
    }
}

void interp_visit_children(AstNode* node, InterpAstChildVisitor visit, void* ctx) {
    if (!node || !visit) return;
#define V(field) do { AstNode* _c = (AstNode*)(field); if (_c) visit(_c, ctx); } while (0)
#define VLIST(field) do { \
        for (AstNode* _i = (AstNode*)(field); _i; _i = _i->next) visit(_i, ctx); \
    } while (0)
    switch (node->node_type) {
    case AST_NODE_ELEMENT:
        VLIST(((AstElementNode*)node)->item);
        VLIST(((AstElementNode*)node)->content);
        break;
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
        // build_assignment_statement_from_parts keeps the parsed LHS in the
        // shared `left` alias while the Lambda statement's real operands are
        // the decomposed place: object + key chain + value. The core layout
        // visits `left`/`right`, so delegating here would scan the LHS as an
        // ordinary *read* -- and `m[i, j] = v` would then be rejected by the
        // N-D INDEX_EXPR read guard, even though T0 never evaluates that node
        // (interp.cpp AST_NODE_INDEX_ASSIGN_STAM uses object/key/value).
        V(((AstCompoundAssignNode*)node)->object);
        VLIST(((AstCompoundAssignNode*)node)->key);
        V(((AstCompoundAssignNode*)node)->value);
        break;
    case AST_NODE_FOR_EXPR: {
        AstForNode* fr = (AstForNode*)node;
        VLIST(fr->loop);
        VLIST(fr->let_clause);
        V(fr->where);
        V(fr->group);
        VLIST(fr->order);
        V(fr->limit);
        V(fr->offset);
        V(fr->then);
        break;
    }
    case AST_NODE_FOR_CLAUSE: {
        AstLoopNode* lp = (AstLoopNode*)node;
        V(lp->as);
        V(lp->on);
        VLIST(lp->join_keys);
        break;
    }
    case AST_NODE_JOIN_KEY:
        V(((AstJoinKey*)node)->prior_expr);
        V(((AstJoinKey*)node)->new_expr);
        break;
    case AST_NODE_ORDER_SPEC:       V(((AstOrderSpec*)node)->expr); break;
    case AST_NODE_GROUP_CLAUSE:     VLIST(((AstGroupClause*)node)->keys); break;
    case AST_NODE_GROUP_KEY:        V(((AstGroupKey*)node)->expr); break;
    case AST_NODE_PATH_INDEX_EXPR:
        V(((AstPathIndexNode*)node)->base_path);
        V(((AstPathIndexNode*)node)->segment_expr);
        break;
    case AST_NODE_NAVIGATION_EXPR:  V(((AstNavigationNode*)node)->object); break;
    case AST_NODE_QUERY_EXPR:       V(((AstQueryNode*)node)->object); break;
    case AST_NODE_LIST:
        VLIST(((AstListNode*)node)->declare);
        VLIST(((AstListNode*)node)->item);
        break;
    case AST_NODE_CONSTRAINED_TYPE:
        V(((AstConstrainedTypeNode*)node)->base);
        V(((AstConstrainedTypeNode*)node)->constraint);
        break;
    case AST_NODE_OBJECT_TYPE: {
        AstObjectTypeNode* ot = (AstObjectTypeNode*)node;
        V(ot->base_type);
        VLIST(ot->item);
        VLIST(ot->content);
        VLIST(ot->methods);
        VLIST(ot->constraints);
        break;
    }
    case AST_NODE_PATTERN_SEQ:      VLIST(((AstPatternSeqNode*)node)->first); break;
    case AST_NODE_PATTERN_RANGE:
        V(((AstPatternRangeNode*)node)->start);
        V(((AstPatternRangeNode*)node)->end);
        break;
    case AST_NODE_PATTERN_ISLAND:   V(((AstPatternIslandNode*)node)->pattern); break;
    case AST_NODE_VIEW: {
        AstViewNode* vw = (AstViewNode*)node;
        V(vw->pattern);
        VLIST(vw->param);
        V(vw->body);
        for (AstStateEntry* s = vw->state; s; s = s->next_state) visit((AstNode*)s, ctx);
        for (AstEventHandler* h = vw->handler; h; h = h->next_handler) visit((AstNode*)h, ctx);
        break;
    }
    case AST_NODE_STATE_ENTRY:      V(((AstStateEntry*)node)->value); break;
    case AST_NODE_EVENT_HANDLER:
        VLIST(((AstEventHandler*)node)->param);
        V(((AstEventHandler*)node)->body);
        break;
    default: {
        InterpCoreChildAdapter adapter = {visit, ctx};
        ast_visit_core_children(node, interp_visit_core_child, &adapter);
        break;
    }
    }
#undef VLIST
#undef V
}

// ---------------------------------------------------------------------------
// Pre-scan: which node kinds the P0 walker can execute
// ---------------------------------------------------------------------------

static bool interp_kind_supported(AstNodeType kind) {
    switch (kind) {
    // --- P0 core subset (design §4.9 families 1-3 plus construction) ---
    case AST_SCRIPT:
    case AST_NODE_PRIMARY:
    case AST_NODE_IDENT:
    case AST_NODE_UNARY:
    case AST_NODE_BINARY:
    case AST_NODE_IF_EXPR:
    case AST_NODE_LET_STAM:
    case AST_NODE_VARIABLE_DECLARATOR:
    case AST_NODE_DECOMPOSE:
    case AST_NODE_CALL_EXPR:
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PARAM:
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
    case AST_NODE_ARRAY:
    case AST_NODE_LIST:
    case AST_NODE_MAP:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_CONTENT:
    case AST_NODE_SYS_FUNC:
    // --- P1.4: procedural statements ---
    case AST_NODE_PROC:
    case AST_NODE_VAR_STAM:
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_LOOP:
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_RETURN_STAM:
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_PIPE_FILE_STAM:
    // --- Tier 3: the CRUD statements and the transaction block (PTH60v3) ---
    case AST_NODE_CRUD_STAM:
    case AST_NODE_OPEN_STAM:
    // --- P1.1: comprehensions ---
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_CLAUSE:
    case AST_NODE_ORDER_SPEC:
    case AST_NODE_GROUP_CLAUSE:
    case AST_NODE_GROUP_KEY:
    case AST_NODE_JOIN_KEY:
    // --- P1.5: modules ---
    case AST_NODE_IMPORT:
    case AST_NODE_PUB_STAM:
    // --- P1.2: type expressions as values ---
    case AST_NODE_TYPE:
    case AST_NODE_TYPE_STAM:
    case AST_NODE_BINARY_TYPE:
    case AST_NODE_UNARY_TYPE:
    case AST_NODE_CONTENT_TYPE:
    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE:
    case AST_NODE_MAP_TYPE:
    case AST_NODE_ELMT_TYPE:
    // P3: the walker resolves the type-list entry and calls its `that`
    // predicate function, a child the scan reaches as any function
    // (S11.4.11); raw Type* identity never substitutes for the constraint.
    case AST_NODE_FUNC_TYPE:
    case AST_NODE_CONSTRAINED_TYPE:
    // --- P1 object literals and interpreted methods ---
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_OBJECT_LITERAL:
    // Named patterns are compiled into Script::type_list by the shared
    // prepass before T0 starts. Their bodies are build-time syntax, not
    // ordinary evaluator expressions (D8.1.1v2).
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
    case AST_NODE_PATTERN_RANGE:
    case AST_NODE_PATTERN_CHAR_CLASS:
    case AST_NODE_PATTERN_SEQ:
    case AST_NODE_PATTERN_ISLAND:
    // --- P1.3: documents, paths, queries ---
    case AST_NODE_ELEMENT:
    // View and edit declarations register an interpreter body against the
    // active `~` context; unsupported native editor operations still fail
    // closed through the scan below.
    case AST_NODE_VIEW:
    // The indexed scan visits these declarations before their bodies.
    case AST_NODE_STATE_ENTRY:
    case AST_NODE_EVENT_HANDLER:
    // `start` launches a task on its own activation (RA12).
    case AST_NODE_START:
    // --- P1.2: match ---
    case AST_NODE_MATCH_EXPR:
    case AST_NODE_MATCH_ARM:
    case AST_NODE_SPREAD:
    // --- P1.1: pipes and implicit contexts ---
    case AST_NODE_PIPE:
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
    case AST_NODE_LAST_INDEX:
    case AST_NODE_PATH_EXPR:
    case AST_NODE_PATH_INDEX_EXPR:
    case AST_NODE_NAVIGATION_EXPR:
    case AST_NODE_QUERY_EXPR:
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM:
    case AST_NODE_CURRENT_ERROR:
    case AST_NODE_NAMED_ARG:
        return true;
    default:
        return false;
    }
}

const char* interp_node_kind_name(AstNodeType kind) {
    switch (kind) {
#define K(name) case name: return #name;
    K(AST_NODE_NULL) K(AST_SCRIPT) K(AST_NODE_PRIMARY) K(AST_NODE_LITERAL)
    K(AST_NODE_IDENT) K(AST_NODE_UNARY) K(AST_NODE_SPREAD) K(AST_NODE_BINARY)
    K(AST_NODE_VARIABLE_DECLARATOR) K(AST_NODE_CALL_EXPR) K(AST_NODE_MEMBER_EXPR)
    K(AST_NODE_INDEX_EXPR) K(AST_NODE_IF_EXPR) K(AST_NODE_ARRAY) K(AST_NODE_MAP)
    K(AST_NODE_KEY_EXPR) K(AST_NODE_MATCH_EXPR) K(AST_NODE_MATCH_ARM)
    K(AST_NODE_SEQ) K(AST_NODE_LIST) K(AST_NODE_BLOCK) K(AST_NODE_PARAM)
    K(AST_NODE_LOOP) K(AST_NODE_BREAK_STAM)
    K(AST_NODE_CONTINUE_STAM) K(AST_NODE_RETURN_STAM) K(AST_NODE_RAISE_STAM)
    K(AST_NODE_RAISE_EXPR) K(AST_NODE_VAR_STAM) K(AST_NODE_ASSIGN_STAM)
    K(AST_NODE_LET_STAM) K(AST_NODE_PUB_STAM) K(AST_NODE_IMPORT)
    K(AST_NODE_FUNC) K(AST_NODE_FUNC_EXPR)
    K(AST_NODE_PROC) K(AST_NODE_PIPE) K(AST_NODE_CURRENT_ITEM)
    K(AST_NODE_CURRENT_INDEX) K(AST_NODE_LAST_INDEX) K(AST_NODE_CONTENT)
    K(AST_NODE_ELEMENT) K(AST_NODE_DECOMPOSE) K(AST_NODE_FOR_CLAUSE)
    K(AST_NODE_ORDER_SPEC) K(AST_NODE_GROUP_CLAUSE) K(AST_NODE_GROUP_KEY) K(AST_NODE_JOIN_KEY)
    K(AST_NODE_FOR_EXPR) K(AST_NODE_INDEX_ASSIGN_STAM) K(AST_NODE_MEMBER_ASSIGN_STAM)
    K(AST_NODE_PIPE_FILE_STAM) K(AST_NODE_TYPE_STAM) K(AST_NODE_PATH_EXPR)
    K(AST_NODE_PATH_INDEX_EXPR) K(AST_NODE_NAVIGATION_EXPR) K(AST_NODE_QUERY_EXPR)
    K(AST_NODE_SYS_FUNC) K(AST_NODE_NAMED_ARG) K(AST_NODE_TYPE)
    K(AST_NODE_CONTENT_TYPE) K(AST_NODE_LIST_TYPE) K(AST_NODE_ARRAY_TYPE)
    K(AST_NODE_MAP_TYPE) K(AST_NODE_ELMT_TYPE) K(AST_NODE_FUNC_TYPE)
    K(AST_NODE_BINARY_TYPE) K(AST_NODE_UNARY_TYPE) K(AST_NODE_CONSTRAINED_TYPE)
    K(AST_NODE_OBJECT_TYPE) K(AST_NODE_OBJECT_LITERAL) K(AST_NODE_STRING_PATTERN)
    K(AST_NODE_SYMBOL_PATTERN) K(AST_NODE_PATTERN_RANGE) K(AST_NODE_PATTERN_CHAR_CLASS)
    K(AST_NODE_PATTERN_SEQ) K(AST_NODE_VIEW) K(AST_NODE_STATE_ENTRY)
    K(AST_NODE_START) K(AST_NODE_EVENT_HANDLER) K(AST_NODE_HANDLER_EXPR)
    K(AST_NODE_HANDLER_STAM) K(AST_NODE_CURRENT_ERROR) K(AST_NODE_PATTERN_ISLAND)
    K(AST_NODE_CRUD_STAM) K(AST_NODE_OPEN_STAM)
#undef K
    default: return "AST_NODE_<unknown>";
    }
}

typedef struct ScanCtx {
    bool ok;
    AstNodeType reject;
    // The index mode checks each published node once. Recursive mode remains
    // for the satellite-local admission scan below.
    bool indexed;
    const AstIndex* index;
    AstNodeId skip_end;
} ScanCtx;

// An outer write to an N-D ArrayNum replaces a row slice, not one scalar leaf.
// Follow plain alias declarations so that `var b: any[] = a; b[0] = ...` does
// not admit a generic COW setter for a shape it cannot preserve.
static bool interp_binding_is_ndim_array(NameEntry* entry, int depth) {
    if (!entry || depth >= AST_COW_PATH_MAX || !entry->node ||
            entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) {
        return false;
    }
    // The any[] declaration boundary widens an N-D numeric literal to a boxed
    // Array, so its later scalar index writes do not need row-aware admission.
    if (ast_declared_type_is_open_any_array(entry->declared_type)) return false;
    AstNode* init = ast_unwrap_primary(((AstDeclaratorNode*)entry->node)->init);
    if (!init) return false;
    if (init->node_type == AST_NODE_IDENT) {
        return interp_binding_is_ndim_array(((AstIdentNode*)init)->entry, depth + 1);
    }
    if (init->node_type != AST_NODE_ARRAY) return false;
    int64_t shape[AST_COW_PATH_MAX] = {};
    ArrayNumElemType element = ELEM_INT;
    return detect_ndim_literal(init, shape, AST_COW_PATH_MAX, &element, true) >= 2;
}

static bool interp_integer_literal(AstNode* node) {
    AstNode* original = node;
    node = ast_unwrap_primary(node);
    if (!node) {
        // Number literals are childless PRIMARY nodes; their primary type,
        // rather than an absent inner AST node, is the integer proof.
        return original && original->type &&
            (original->type->type_id == LMD_TYPE_INT ||
             original->type->type_id == LMD_TYPE_INT64);
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        return (unary->op == OPERATOR_NEG || unary->op == OPERATOR_POS) &&
            interp_integer_literal(unary->operand);
    }
    return node->node_type == AST_NODE_LITERAL && node->type &&
        (node->type->type_id == LMD_TYPE_INT ||
         node->type->type_id == LMD_TYPE_INT64);
}

// `a[i, j]` reaches ArrayNum's existing N-D helpers only when the binding is a
// direct numeric literal (or its plain alias) and every coordinate is a fixed
// integer. The static proof keeps generic tuple-like indexing and effectful
// coordinate evaluation on MIR while retaining c15's axis-bound behavior.
// Keep the N-D read proof aligned with the runtime carrier. `reshape` and
// `transpose` return ArrayNum views even though their registry result type is
// `any`; rejecting those aliases would route valid ArrayNum indexing to MIR
// solely because the type graph forgets the concrete carrier.
static bool interp_array_num_expr(AstNode* node, int depth) {
    if (!node || depth >= 16) return false;
    node = ast_unwrap_primary(node);
    if (!node) return false;
    if (node->node_type == AST_NODE_ARRAY) {
        int64_t shape[AST_COW_PATH_MAX] = {};
        ArrayNumElemType element = ELEM_INT;
        return detect_ndim_literal(node, shape, AST_COW_PATH_MAX,
            &element, true) >= 1;
    }
    if (node->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        if (!entry || !entry->node || entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR) {
            return false;
        }
        return interp_array_num_expr(((AstDeclaratorNode*)entry->node)->init, depth + 1);
    }
    if (node->node_type != AST_NODE_CALL_EXPR) return false;
    AstCallNode* call = (AstCallNode*)node;
    AstNode* callee = ast_unwrap_primary(call->function);
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return false;
    SysFuncInfo* info = ((AstSysFuncNode*)callee)->fn_info;
    if (!info || (info->fn != SYSFUNC_RESHAPE && info->fn != SYSFUNC_TRANSPOSE)) {
        return false;
    }
    return call->argument && interp_array_num_expr(call->argument, depth + 1);
}

static bool interp_direct_ndim_indices(AstNode* object, AstNode* first_index) {
    object = ast_unwrap_primary(object);
    if (!interp_array_num_expr(object, 0)) return false;
    int count = 0;
    for (AstNode* index = first_index; index; index = index->next) {
        if (count >= AST_COW_PATH_MAX || !interp_integer_literal(index)) return false;
        count++;
    }
    return count >= 2;
}

// Scalar stores retain boxed keys through the checked COW setter (S7.1.3v2).
// Arithmetic over an admitted dynamic binding uses that same runtime check;
// it need not acquire a declared int type to enter T0. Keep character loops
// and vector/slice syntax outside this path; their store shapes differ.
static bool interp_checked_scalar_index_expr(AstNode* node) {
    if (node && node->type &&
            (node->type->type_id == LMD_TYPE_INT ||
             node->type->type_id == LMD_TYPE_INT64)) return true;
    if (interp_integer_literal(node)) return true;
    node = ast_unwrap_primary(node);
    if (!node) return false;
    if (node->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        if (!entry) return false;
        if (!entry->declared_type && (!entry->node ||
                (entry->node->node_type != AST_NODE_FOR_CLAUSE &&
                 entry->node->node_type != AST_NODE_FOR_INDEX))) return true;
        if (!entry->node || entry->node->node_type != AST_NODE_FOR_CLAUSE) return false;
        AstLoopNode* loop = (AstLoopNode*)entry->node;
        AstNode* source = ast_unwrap_primary(loop->as);
        // one integer `to` bound excludes a character range on every success
        return source && source->node_type == AST_NODE_BINARY &&
            ((AstBinaryNode*)source)->op == OPERATOR_TO &&
            (interp_integer_literal(((AstBinaryNode*)source)->left) ||
             interp_integer_literal(((AstBinaryNode*)source)->right));
    }
    if (node->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* binary = (AstBinaryNode*)node;
    if (binary->op != OPERATOR_ADD && binary->op != OPERATOR_SUB &&
            binary->op != OPERATOR_MUL) {
        return false;
    }
    return interp_checked_scalar_index_expr(binary->left) &&
        interp_checked_scalar_index_expr(binary->right);
}

typedef struct SatelliteScanCtx {
    bool ok;
    // T27-6: the innermost node kind that refused, so a pinned hot function
    // is a one-line diagnosis instead of a missing promotion log line.
    AstNodeType reject;
} SatelliteScanCtx;

static void interp_scan_satellite_node(AstNode* node, void* opaque);

static void interp_scan_visit(AstNode* node, void* ctx) {
    ScanCtx* sc = (ScanCtx*)ctx;
    if (!sc->ok || !node) return;
    if (!interp_kind_supported(node->node_type)) {
        sc->ok = false;
        sc->reject = node->node_type;
        return;
    }
    if (node->node_type == AST_NODE_VIEW) {
        AstViewNode* view = (AstViewNode*)node;
        if (!view->body) {
            // Both view and edit bodies are ordinary `~` activations. The
            // scanner still rejects any editor-only native ABI encountered
            // below, but the template boundary itself needs no generated
            // function pointer.
            sc->ok = false;
            sc->reject = AST_NODE_VIEW;
            return;
        }
        if (!sc->indexed) {
            if (view->pattern) interp_scan_visit(view->pattern, ctx);
            for (AstNode* param = (AstNode*)view->param; param; param = param->next) {
                interp_scan_visit(param, ctx);
            }
            if (sc->ok && view->body) interp_scan_visit(view->body, ctx);
            for (AstStateEntry* state = view->state; sc->ok && state;
                    state = state->next_state) {
                if (state->value) interp_scan_visit(state->value, ctx);
            }
            for (AstEventHandler* handler = view->handler; sc->ok && handler;
                    handler = handler->next_handler) {
                if (handler->param) interp_scan_visit((AstNode*)handler->param, ctx);
                if (handler->body) interp_scan_visit(handler->body, ctx);
            }
        }
        return;
    }
    if (node->node_type == AST_NODE_PIPE) {
        AstBinaryNode* pipe = (AstBinaryNode*)node;
        AstNode* right = ast_unwrap_primary(pipe->right);
        if (pipe->op == OPERATOR_PIPE && right &&
                right->node_type == AST_NODE_CALL_EXPR &&
                ast_call_has_named_args((AstCallNode*)right) &&
                !interp_named_sys_args_supported(ast_unwrap_primary(
                    ((AstCallNode*)right)->function))) {
            // The aggregate pipe injects one positional argument outside the
            // call AST. Only pure system rows lower named operands in source
            // order; Lambda formals still need a merged/reordered ABI.
            sc->ok = false;
            sc->reject = AST_NODE_NAMED_ARG;
            return;
        }
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* call_callee = ast_unwrap_primary(call->function);
        AstFieldNode* member = call_callee &&
                call_callee->node_type == AST_NODE_MEMBER_EXPR
            ? (AstFieldNode*)call_callee : NULL;
        AstNode* method_name = member ? ast_unwrap_primary(member->field) : NULL;
        AstNode* receiver = member ? ast_unwrap_primary(member->object) : NULL;
        AstIdentNode* receiver_ident = receiver && receiver->node_type == AST_NODE_IDENT
            ? (AstIdentNode*)receiver : NULL;
        AstIdentNode* method_ident = method_name && method_name->node_type == AST_NODE_IDENT
            ? (AstIdentNode*)method_name : NULL;
        TypeObject* receiver_type = member && member->object && member->object->type &&
                type_nominal_record(member->object->type) != NULL
            ? (TypeObject*)member->object->type : NULL;
        TypeMethod* object_method = receiver_type && method_ident
            ? ast_lookup_object_method(receiver_type, method_ident->name) : NULL;
        if (object_method && object_method->ast_def) {
            // T0 captures an object receiver in a dedicated closure slot. A
            // direct root is the only shape that can publish the COW receiver
            // replacement before a procedural method starts mutating fields.
            if (!receiver_ident || !receiver_ident->entry || receiver_ident->entry->import ||
                    object_method->ast_def->captures ||
                    ast_type_func_has_var_parameter(object_method->fn_type) ||
                    ast_call_has_named_args(call) ||
                    (object_method->is_proc &&
                     (!call->is_proc_method || !receiver_ident->entry->is_mutable))) {
                sc->ok = false;
                sc->reject = AST_NODE_CALL_EXPR;
                return;
            }
        }
        if (call_callee && call_callee->node_type == AST_NODE_SYS_FUNC) {
            SysFuncInfo* info = ((AstSysFuncNode*)call_callee)->fn_info;
            if (info && info->fn == SYSPROC_VMAP_SET) {
                AstNode* owner = ast_unwrap_primary(call->argument);
                NameEntry* owner_entry = owner && owner->node_type == AST_NODE_IDENT
                    ? ((AstIdentNode*)owner)->entry : NULL;
                // The shared VMap COW entry has a replacement channel only
                // for a direct local binding. Dynamic/import receivers remain
                // on MIR rather than mutating an owner T0 cannot publish.
                if (!owner_entry || owner_entry->import || !call->argument->next ||
                        !call->argument->next->next || call->argument->next->next->next) {
                    sc->ok = false;
                    sc->reject = AST_NODE_SYS_FUNC;
                    return;
                }
            }
        }
        AstFuncNode* direct = ast_direct_call_function(call);
        if (call_callee && call_callee->node_type == AST_NODE_IDENT) {
            AstIdentNode* imported = (AstIdentNode*)call_callee;
            AstImportNode* import = imported->entry ? imported->entry->import : NULL;
            if (import && import->is_cross_lang && import->script &&
                    import->script->profile == &js_profile &&
                    ast_call_has_named_args(call)) {
                // The hosted JS membrane exposes a fixed positional bridge;
                // it has no Lambda formal-name adapter to reorder arguments.
                sc->ok = false;
                sc->reject = AST_NODE_NAMED_ARG;
                return;
            }
        }
        if (ast_call_has_named_args(call) && !direct &&
                !interp_named_sys_args_supported(call_callee)) {
            // Dynamic calls have no formal layout. Pure system rows are the
            // one exception: MIR discards their labels and keeps source order.
            sc->ok = false;
            sc->reject = AST_NODE_NAMED_ARG;
            return;
        }
        TypeFunc* signature = direct && ((AstNode*)direct)->type &&
                ((AstNode*)direct)->type->type_id == LMD_TYPE_FUNC
            ? (TypeFunc*)((AstNode*)direct)->type : NULL;
        if (ast_type_func_has_var_parameter(signature)) {
            NameEntry* borrowed[LAMBDA_MAX_FUNCTION_ARGS] = {0};
            // CW25: pass the argument-node array. It is what tells the shared
            // helper that this caller implements place borrows (`f(var m[1])`),
            // and eval_call in interp.cpp passes it -- admitting less here than
            // the walker executes pinned whole files to the JIT for a shape T0
            // already runs.
            AstNode* borrow_args[LAMBDA_MAX_FUNCTION_ARGS] = {0};
            if (!ast_direct_call_var_parameter_entries(call, signature, borrowed,
                    borrow_args)) {
                // A `var` argument is a caller-owned writable binding. T0 can
                // publish a replacement only for an exact direct identifier;
                // a dynamic, optional, variadic, aliased, or expression
                // argument has no equivalent write-back target.
                sc->ok = false;
                sc->reject = AST_NODE_CALL_EXPR;
                return;
            }
        } else if (!direct) {
            AstNode* callee = ast_unwrap_primary(call->function);
            TypeFunc* dynamic_signature = callee
                ? lambda_type_func_signature(callee->type) : NULL;
            if (ast_type_func_has_var_parameter(dynamic_signature)) {
                // lambda_dynamic_call intentionally rejects mutable borrows;
                // retain whole-script fallback before that ABI boundary.
                sc->ok = false;
                sc->reject = AST_NODE_CALL_EXPR;
                return;
            }
        }
    }
    // A generator body has no T0 suspension of its own yet; Lambda procedures
    // that suspend run here like any other (RA12).
    if ((node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) && ((AstFuncNode*)node)->is_generator) {
        sc->ok = false;
        sc->reject = node->node_type;
        return;
    }
    // `a[i] = v`, `a.f = v`, and nested paths through a plain binding root use
    // cow_path_set: it owns every detach/relink decision (S9.1.2), while T0
    // only publishes its replacement root. The alias mark comes from
    // cow_bind_var at the binding boundary, exactly as lowering does. Still
    // gated: a declared map/array contract, whose checked setters
    // validate the full occurrence contract before installing a replacement.
    if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM ||
            node->node_type == AST_NODE_MEMBER_ASSIGN_STAM) {
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
        AstCowPath path = {};
        // ca->object is only the immediate parent on a nested write; use the
        // collected root or `h[0][0] = v` would be rejected before T0 runs it.
        bool has_path = ast_collect_cow_path(&path, ca->object);
        NameEntry* entry = has_path && path.root &&
                path.root->node_type == AST_NODE_IDENT
            ? ((AstIdentNode*)path.root)->entry : NULL;
        // The shared COW setters dispatch by the runtime owner layout. A
        // direct binding to a markup-derived Element can therefore use the
        // same map/array replacement path as a literal without assuming its
        // input-pool allocation or field representation.
        // MIR routes a typed numeric-array key through fn_index_assign, whose
        // runtime mask validation owns the bool-lane and shape checks. Source
        // numeric literals retain ARRAY AST type until their ArrayNum builds.
        bool direct_numeric_mask_assignment =
            ast_is_direct_numeric_mask_assignment(node);
        bool direct_ndim_scalar_assignment =
            node->node_type == AST_NODE_INDEX_ASSIGN_STAM && path.count == 0 &&
            ca->key && ca->key->next &&
            interp_direct_ndim_indices(ca->object, ca->key);
        bool indexed_key = node->node_type != AST_NODE_INDEX_ASSIGN_STAM ||
            (ca->key && !ca->key->next &&
             interp_checked_scalar_index_expr(ca->key));
        // A multi-coordinate scalar store has already proved each key is an
        // integral coordinate; treating its linked key list as an unsupported
        // dynamic index forced the whole T0 module to fall back despite the
        // checked N-D runtime path being available.
        indexed_key = indexed_key || direct_numeric_mask_assignment ||
            direct_ndim_scalar_assignment;
        bool open_any_array = entry &&
            ast_declared_type_is_open_any_array(entry->declared_type);
        bool open_item_binding = entry &&
            ast_declared_type_is_open_item(entry->declared_type);
        bool typed_map_root = entry && ast_declared_type_is_map(entry->declared_type);
        Type* typed_array_element = entry
            ? ast_declared_array_element(entry->declared_type) : NULL;
        // Every declared T[] root has a checked interpreter path. Earlier
        // gating admitted only scalar direct lanes, which made named-map and
        // nested T[][] stores fall back even though lambda_array_path_set_checked
        // owns their full-contract validation and COW publication.
        bool direct_typed_array = typed_array_element != NULL;
        // An open any[] declaration is generic in MIR, while the T0 literal
        // builder may initially produce an N-D ArrayNum. Keep its row store
        // pinned until that declaration boundary has a shared reifier; routing
        // it through scalar COW would flatten a row and change the value shape.
        bool ndim_row_write = path.count == 0 &&
            interp_binding_is_ndim_array(entry, 0);
        if (!has_path || !entry || entry->import || !indexed_key ||
                (ndim_row_write && !direct_numeric_mask_assignment &&
                 !direct_ndim_scalar_assignment) ||
                (entry->node && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                 ((AstDeclaratorNode*)entry->node)->declared_type &&
                 !open_item_binding && !open_any_array && !typed_map_root &&
                 !direct_typed_array)) {
            // An `any` / `any[]` root has no narrower contract to validate;
            // ordinary COW is therefore the same owner boundary as MIR. Other
            // dynamic index shapes remain gated; typed maps/arrays use their
            // checked setters, and an N-D root needs row-aware assignment
            // rather than a scalar COW store that would silently narrow a row.
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // Lambda imports still require one shared planned slab. Hosted JavaScript
    // imports are different: their namespace is already evaluated and rooted
    // by the JS runtime, so the binding is resolved through that membrane.
    if (node->node_type == AST_NODE_IMPORT) {
        AstImportNode* imp = (AstImportNode*)node;
        // An aliased import (`import alias: path`) adds *qualified* entries
        // (`alias.member`) via push_qualified_name, but each carries the same
        // `node` + `import` pair as the plain entry, so plan_resolve_import
        // binds it through the identical declaration-node match. Only the
        // namespace/default/cross-language shapes have no walker equivalent.
        bool hosted_js = imp->is_cross_lang && imp->script &&
            imp->script->profile == &js_profile;
        if (imp->namespace_name || imp->default_name || !imp->script ||
                (imp->is_cross_lang && !hosted_js) ||
                (!imp->is_cross_lang && !imp->script->interp_supported)) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // Comprehension clauses with their own lowering shapes. Ordered streams,
    // grouped rows, and equi-join tuple streams share the runtime helpers MIR
    // uses; only malformed group bindings remain fail-closed.
    if (node->node_type == AST_NODE_FOR_EXPR) {
        AstForNode* fr = (AstForNode*)node;
        if (fr->group && !fr->group->entry) {
            // Every grouped form needs a real post-group binding; joined rows
            // are collected by the same tuple-group path as MIR.
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // `t[i, j]` carries a chain of index expressions for one N-D subscript.
    // The direct numeric-literal slice shares ArrayNum's established N-D
    // helpers; generic or effectful coordinate expressions remain on MIR.
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* field = (AstFieldNode*)node;
        if (field->field && field->field->next &&
                !interp_direct_ndim_indices(field->object, field->field)) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // `{*:base, k: v}` records the merge on the *shape entry* and leaves the
    // raw value expression as the item (build_ast.cpp), so eval_map's positional
    // fill already hands map_fill_items exactly what lowering does — the shared
    // filler is what interprets the spread marker. No gate is needed.
    // A system function whose entry takes native words (the bitwise/shift
    // family) or more arguments than the P0 dispatch table covers.
    if (node->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)node)->fn_info;
        // A variadic sys func (arg_count -1) has bespoke per-call lowering —
        // `print` for instance emits one pn_print per argument with separators —
        // so there is no generic dispatch to mirror yet.
        // The native bitwise rows are admitted only through the shared
        // Item-level wrapper policy. It preserves the static all-int fast path
        // while the existing runtime helpers retain sized/full/bigint lanes.
        if (interp_native_sys_item_supported(info)) {
            if (!sc->indexed) interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        // `print` is the one Lambda-variadic entry the walker implements
        // directly (one pn_print per argument, as lowering emits); the other
        // variadic rows still have no generic dispatch to mirror.
        if (info && info->fn == SYSPROC_PRINT && info->func_ptr) {
            if (!sc->indexed) interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        // Math entries carrying a native lane (floor/ceil/round/trunc/abs …)
        // preserve their argument's declared type: lowering keeps `trunc(n:int)`
        // in the int lane, while the boxed helper alone yields float
        // (test/lambda/proc/native_math_type_preserving.ls). That lane choice is
        // type-inference work, the same gap that keeps the bitwise family out.
        // The type-preserving math family (floor/ceil/round/trunc/abs) is
        // interpreted with a result re-narrowed by the call's static type
        // (eval_call, interp.cpp), which is the same input lowering uses. Only
        // an integer lane other than plain `int` is still gated: those carry
        // widths the boxed helper does not model.
        if (info && info->native_c_name && info->native_func_ptr &&
                !sysfunc_native_math_always_float(info->fn) &&
                node->type && node->type->type_id != LMD_TYPE_INT &&
                node->type->type_id != LMD_TYPE_FLOAT &&
                node->type->type_id != LMD_TYPE_ANY) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        // VMap creation and `set` have no boxed entry (func_ptr is NULL by
        // design); the walker mirrors their direct lowering paths instead.
        if (info && info->fn == SYSFUNC_VMAP_NEW && info->arg_count <= 1) {
            if (!sc->indexed) interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        // `select` packs its variadic handles at the call, as lowering does.
        if (info && (info->fn == SYSPROC_VMAP_SET || info->fn == SYSPROC_SELECT)) {
            if (!sc->indexed) interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        // The boxed caller in interp.cpp handles up to five Item arguments; the
        // cap here was one short of it and excluded any script touching a
        // five-argument row (set_base_and_extent), sending the whole file to
        // the JIT (ESO113).
        if (!info || !info->func_ptr ||
                !sysfunc_args_require_rep(info, VALUE_REP_ITEM) ||
                info->arg_count < 0 || info->arg_count > 5) {
            log_debug("interp: sys func '%s' unsupported (arity=%d all_item=%d ptr=%p)",
                info && info->name ? info->name : "<null>",
                info ? info->arg_count : -99,
                info && sysfunc_args_require_rep(info, VALUE_REP_ITEM),
                info ? (void*)info->func_ptr : NULL);
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    if (!sc->indexed) interp_visit_children(node, interp_scan_visit, ctx);
}

static AstIndexProfileSupport interp_profile_support_node(
        const AstIndex* index, AstNodeId node_id, void* context) {
    ScanCtx* sc = (ScanCtx*)context;
    if (!index || !sc || node_id >= index->count) {
        return AST_INDEX_PROFILE_REJECT;
    }
    sc->skip_end = AST_NODE_ID_INVALID;
    interp_scan_visit(index->nodes[node_id], sc);
    if (!sc->ok) return AST_INDEX_PROFILE_REJECT;
    return sc->skip_end > node_id && sc->skip_end <= index->count
        ? AST_INDEX_PROFILE_SKIP_SUBTREE : AST_INDEX_PROFILE_ACCEPT;
}

bool interp_scan_supported(Script* script, AstNodeType* reject) {
    if (!script || !script->ast_root) return false;
    ScanCtx sc = {true, script->interp_reject_kind};
    AstIndex* index = &script->ast_index;
    if (index->graph_published) {
        sc.indexed = true;
        sc.index = index;
        sc.ok = ast_index_scan_profile_support(index,
            interp_profile_support_node, &sc);
    } else {
        interp_scan_visit(script->ast_root, &sc);
    }
    if (reject) *reject = sc.reject;
    return sc.ok;
}

bool interp_named_sys_args_supported(const AstNode* callee) {
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return false;
    SysFuncInfo* info = ((const AstSysFuncNode*)callee)->fn_info;
    // A procedure can mutate its first Item. When that Item arrived from a
    // pipe, T0 has no direct binding to publish the required COW replacement.
    // The suspension builtins (`wait`, `select`, ...) mutate no argument.
    return info && (!info->is_proc || info->is_async);
}

// ---------------------------------------------------------------------------
// Restricted evaluator mode (P3)
// ---------------------------------------------------------------------------

// A constant fold has no user-code call edge.  This explicit list is
// deliberately smaller than the ordinary sysfunc surface: every row here is a
// value reader or scalar/text transform with no I/O, mutation, async, or
// callback path.
bool interp_eval_mode_allows_sys_func(EvalMode mode, const SysFuncInfo* info) {
    if (mode == EvalMode::RUNTIME) return true;
    if (!info || info->is_proc || !info->func_ptr || info->is_async) return false;
    switch (info->fn) {
    case SYSFUNC_LEN:
    case SYSFUNC_COUNT:
    case SYSFUNC_TYPE:
    case SYSFUNC_NAME:
    case SYSFUNC_INT:
    case SYSFUNC_INT64:
    case SYSFUNC_FLOAT:
    case SYSFUNC_DECIMAL:
    case SYSFUNC_STRING:
    case SYSFUNC_ABS:
    case SYSFUNC_ROUND:
    case SYSFUNC_FLOOR:
    case SYSFUNC_CEIL:
    case SYSFUNC_TRUNC:
    case SYSFUNC_CONTAINS:
    case SYSFUNC_STARTS_WITH:
    case SYSFUNC_ENDS_WITH:
    case SYSFUNC_TRIM:
    case SYSFUNC_TRIM_START:
    case SYSFUNC_TRIM_END:
    case SYSFUNC_LOWER:
    case SYSFUNC_UPPER:
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Slot assignment
// ---------------------------------------------------------------------------

typedef struct PlanCtx {
    Script* script;
    FnFramePlan* plan;        // plan currently being filled
    const AstFuncNode* function;
    uint32_t next_slot;       // next named-binding index within this plan
    uint32_t param_count;
    uint32_t max_scratch;
    BindingStorage storage;   // REGISTER inside functions, MODULE at top level
    bool is_variadic;
    bool failed;
} PlanCtx;

// A binding owns its container when the initializer produces a fresh one, or
// when it aliases a binding that already owns one -- the same forward
// propagation the JIT performs over MirVarEntry::cow_owned. Declarations are
// visited in source order, so the source binding is always decided first.
static void plan_mark_cow_owned(NameEntry* entry) {
    AstNode* decl = entry ? entry->node : NULL;
    if (!decl || decl->node_type != AST_NODE_VARIABLE_DECLARATOR) return;
    AstNode* init = ast_unwrap_primary(((AstDeclaratorNode*)decl)->init);
    if (!init) return;
    if (init->node_type == AST_NODE_IDENT) {
        NameEntry* src = ((AstIdentNode*)init)->entry;
        entry->cow_owned = src && src->cow_owned;
        return;
    }
    entry->cow_owned = ast_expr_produces_owned_container(init);
}

// A destination is a written contract, never the current value's inferred tag.
// Only fresh result positions adopt it; argument evaluation keeps its own order.
static bool plan_scalar_boundary_proven(AstNode* producer, Type* contract) {
    Type* plain = type_field_unwrap_simple_decl(contract);
    if (!producer || (plain != &TYPE_INT && plain != &TYPE_BOOL &&
            plain != &TYPE_FLOAT && plain != &TYPE_STRING)) return false;
    return lambda_static_boundary_relation(producer->type, contract, false) ==
            STATIC_BOUNDARY_PROVEN &&
        lambda_boundary_is_redundant(producer->type, contract) &&
        !ast_expr_may_defect(producer, 0);
}

static void plan_destination(Pool* pool, AstNode* node, Type* contract, int depth = 0,
        bool construction_only = false) {
    node = ast_unwrap_primary(node);
    if (!node || !contract || depth > 64 ||
            lambda_type_contract_has_binder(contract, true)) return;
    switch (node->node_type) {
    case AST_NODE_CALL_EXPR: {
        // argument hints may construct layouts, but admission failures stay at call entry
        if (construction_only) return;
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee = ast_unwrap_primary(call->function);
        LambdaArrayContractInfo array = {};
        if (callee && callee->node_type == AST_NODE_SYS_FUNC &&
                ((AstSysFuncNode*)callee)->fn_info &&
                ((AstSysFuncNode*)callee)->fn_info->fn == SYSFUNC_FILL &&
                lambda_array_contract_info(contract, &array))
            call->interp_array_destination = contract;
        return;
    }
    case AST_NODE_MAP: {
        AstMapNode* literal = (AstMapNode*)node;
        TypeMap* target = (TypeMap*)lambda_type_nonnull_map_contract(contract);
        // a structural literal cannot manufacture nominal identity at admission
        if (!target || type_nominal_record((Type*)target) ||
                !target->is_trusted_contract || literal->has_computed_key ||
                !ast_map_contract_storage_valid(target) ||
                !ast_map_literal_keys_follow_contract(literal, target)) return;
        literal->interp_destination = target;
        literal->interp_fields = (InterpMapFieldPlan*)pool_calloc(pool,
            (size_t)target->length * sizeof(InterpMapFieldPlan));
        ShapeEntry* field = target->shape;
        int index = 0;
        for (AstNode* item = literal->item; item;
                item = item->next, field = typemap_next_field(target, field), index++) {
            AstNode* producer = ((AstNamedNode*)item)->as;
            if (literal->interp_fields) {
                literal->interp_fields[index].boundary = interp_boundary_plan_create(pool, field->type);
                literal->interp_fields[index].statically_proven =
                    plan_scalar_boundary_proven(producer, field->type);
            }
            plan_destination(pool, producer, field->type, depth + 1, construction_only);
        }
        return;
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* branch = (AstIfNode*)node;
        plan_destination(pool, branch->then, contract, depth + 1, construction_only);
        plan_destination(pool, branch->otherwise, contract, depth + 1, construction_only);
        return;
    }
    case AST_NODE_MATCH_EXPR:
        for (AstMatchArm* arm = ((AstMatchNode*)node)->first_arm; arm;
                arm = (AstMatchArm*)arm->next)
            plan_destination(pool, arm->body, contract, depth + 1, construction_only);
        return;
    case AST_NODE_CONTENT:
    case AST_NODE_LIST: {
        int values = 0, declarations = 0, statements = 0;
        AstNode* result = interp_proc_block_last_value((AstListNode*)node,
            &values, &declarations, &statements);
        if (values == 1) plan_destination(pool, result, contract, depth + 1, construction_only);
        return;
    }
    default:
        return;
    }
}

static void plan_place(PlanCtx* pc, AstCompoundAssignNode* assignment) {
    if (assignment->interp_place) return;
    InterpPlacePlan* place = (InterpPlacePlan*)pool_calloc(pc->script->pool,
        sizeof(InterpPlacePlan));
    if (!place) { pc->failed = true; return; }
    if (!ast_collect_cow_path(&place->path, assignment->object) || !place->path.root ||
            place->path.root->node_type != AST_NODE_IDENT) return;
    place->root = ((AstIdentNode*)place->path.root)->entry;
    if (!place->root) return;
    int64_t index_mask = 0;
    place->leaf_contract = lambda_type_contract_has_binder(place->root->declared_type, true)
        ? NULL : ast_map_path_leaf_contract(place->root->declared_type,
        &place->path, assignment->key,
        assignment->node_type == AST_NODE_MEMBER_ASSIGN_STAM, &index_mask);
    place->key_shape = (uint64_t)(place->path.count + 1) |
        ((uint64_t)place->root->is_var_param << 8) | ((uint64_t)index_mask << 16);
    assignment->interp_place = place;
}

static void plan_assign_entry(PlanCtx* pc, NameEntry* entry) {
    if (!entry || entry->storage_assigned) return;
    plan_mark_cow_owned(entry);
    entry->interp_boundary = interp_boundary_plan_create(pc->script->pool,
        entry->declared_type);
    if (entry->interp_boundary && entry->node && entry->node->node_type == AST_NODE_PARAM) {
        // a type parameter's binder lives on TypeParam, outside its declared contract
        TypeParam* parameter = lambda_type_param(entry->node->type);
        entry->interp_boundary->uses_binder |= parameter && parameter->binder;
    }
    if (pc->next_slot > UINT16_MAX) { pc->failed = true; return; }
    entry->slot = (int32_t)pc->next_slot++;
    entry->binding_storage = pc->storage;
    entry->storage_assigned = true;
}

// A definition may have no prior lowering analysis (notably an anonymous
// function evaluated only by T0), so frame planning owns its plan allocation.
static FnAnalysis* plan_ensure_analysis(PlanCtx* pc, AstFuncNode* fn) {
    if (!fn) return NULL;
    if (!fn->analysis) {
        fn->analysis = (FnAnalysis*)pool_calloc(pc->script->pool, sizeof(FnAnalysis));
        if (!fn->analysis) pc->failed = true;
    }
    return fn->analysis;
}

// An imported name is a view onto another module's binding: resolve it to the
// declaring module's slot instead of giving it one here. The declaring Script
// has already been loaded (and planned) by the time its importer builds, so
// this is a build-time resolution, not a runtime lookup.
static bool plan_resolve_import(NameEntry* entry) {
    if (!entry->import || !entry->import->script || !entry->node) return false;
    Script* owner = entry->import->script;
    if (entry->import->is_cross_lang) {
        // Hosted modules publish rooted namespace values instead of Lambda
        // module slabs. Mark the binding as externally resolved; the walker
        // reads it through the language membrane at each use site.
        entry->import_owner = owner;
        entry->binding_storage = BINDING_STORAGE_MODULE;
        entry->storage_assigned = true;
        entry->slot = -1;
        return true;
    }
    AstScript* owner_root = (AstScript*)owner->ast_root;
    if (!owner_root) return false;
    for (NameEntry* d = owner_root->global_vars ? owner_root->global_vars->first : NULL;
            d; d = d->next) {
        if (d->node != entry->node || !d->storage_assigned) continue;
        entry->slot = d->slot;
        entry->binding_storage = d->binding_storage;
        entry->import_owner = owner;
        entry->storage_assigned = true;
        return true;
    }
    return false;
}

// Captured names read through the closure env, not through a frame slot.
static void plan_assign_scope(PlanCtx* pc, NameScope* scope) {
    if (!scope) return;
    for (NameEntry* e = scope->first; e; e = e->next) {
        if (e->is_binder && e->binder && e->binder->parameter_name != e->name) {
            // `as T` has no value binding. The physical parameter owning its
            // site receives its normal slot; T is read from binder_env (TG9).
            continue;
        }
        if (e->import) {
            // Imported names consume no slot in this module's slab.
            plan_resolve_import(e);
            continue;
        }
        plan_assign_entry(pc, e);
    }
}

// A capture's dense slot is stable for the lifetime of its immutable function
// definition. Link each read occurrence while the existing frame-plan walk is
// already classifying bindings, so the hot walker need not rescan FnCapture.
static void plan_link_capture_identifier(PlanCtx* pc, AstIdentNode* ident) {
    if (!ident) return;
    ident->interp_capture_owner = NULL;
    ident->interp_frame_slot_read = false;
    if (!pc || !pc->function || !ident->entry) return;
    uint16_t slot = 0;
    for (FnCapture* capture = pc->function->captures; capture;
            capture = capture->next, slot++) {
        if (capture->entry != ident->entry) continue;
        ident->interp_capture_owner = (const AstNode*)pc->function;
        ident->interp_capture_slot = slot;
        return;
    }
    // Not a capture: a register binding read here is one of this function's
    // own frame slots, unless it names something the generic read resolves
    // elsewhere (eval_expr's IDENT arm and interp_read_binding_at_capture_slot).
    NameEntry* entry = ident->entry;
    AstNode* decl = entry->node;
    bool special_decl = decl && (decl->node_type == AST_NODE_KEY_EXPR ||
        decl->node_type == AST_NODE_OBJECT_TYPE ||
        decl->node_type == AST_NODE_STRING_PATTERN ||
        decl->node_type == AST_NODE_SYMBOL_PATTERN ||
        (decl->node_type == AST_NODE_VARIABLE_DECLARATOR &&
            ((AstDeclaratorNode*)decl)->is_type_definition));
    ident->interp_frame_slot_read = entry->storage_assigned &&
        entry->binding_storage == BINDING_STORAGE_REGISTER &&
        !entry->is_binder && !entry->import && !special_decl && entry->slot >= 0;
}

static void plan_link_call_shape(Pool* pool, AstCallNode* call) {
    // An oversized list retains the existing walker-side scan and runtime
    // diagnostic instead of narrowing its source count into the shared plan.
    (void)ast_plan_call_shape(call);
    AstFuncNode* target = ast_direct_call_function(call);
    if (!target || !call->interp_call_shape_planned ||
            call->interp_source_argc > LAMBDA_MAX_FUNCTION_ARGS) return;
    AstNode* resolved[LAMBDA_MAX_FUNCTION_ARGS] = {};
    ast_resolve_call_args(call->argument, target, call->interp_source_argc, resolved);
    int index = 0;
    for (AstNamedNode* parameter = target->param; parameter && index < LAMBDA_MAX_FUNCTION_ARGS;
            parameter = (AstNamedNode*)parameter->next, index++) {
        TypeParam* type = lambda_type_param(parameter->type);
        if (type && (type->binder || type->is_var_param)) continue;
        // fresh argument records can adopt their admitted layout without rebuilding at entry
        plan_destination(pool, resolved[index], parameter->declared_type, 0, true);
    }
}

// Scratch need: the maximum number of Items that must stay live in frame slots
// across a child evaluation or a MAY_GC helper call. A property of expression
// shape, never of data size — container literals accumulate through one rooted
// builder, so they cost 1 regardless of element count.
static uint32_t plan_need(AstNode* node);

// A constrained type that `is` or a match arm names -- inline, by name, or
// through an alias chain -- roots one predicate closure while each layer's
// call runs (interp_eval_constrained_predicate). The bodies are planned as
// those functions' own frames, so a body naming its own type costs its caller
// nothing more (LR03-28). Zero when the node names none.
static uint32_t plan_constrained_type_need(AstNode* node) {
    return ast_constrained_type(node) ? 1 : 0;
}

// Pattern testing is not ordinary expression evaluation: a non-constrained
// leaf materializes its value and keeps that value live while fn_is/fn_eq runs,
// whereas a constrained leaf roots its predicate closure across the call.
// Keep this shared with MATCH_ARM so a type-name pattern cannot borrow the
// match frame's signal slot at a GC safepoint.
static uint32_t plan_match_pattern_need(AstNode* pattern) {
    if (!pattern) return 0;
    if (pattern->node_type == AST_NODE_BINARY_TYPE) {
        AstBinaryNode* binary = (AstBinaryNode*)pattern;
        if (binary->op == OPERATOR_UNION) {
            uint32_t left = plan_match_pattern_need(binary->left);
            uint32_t right = plan_match_pattern_need(binary->right);
            return left > right ? left : right;
        }
    }
    // a named arm (`case Pos:`) runs its predicates too, so it is costed as
    // the inline form; one slot overflowed the frame
    uint32_t constrained = plan_constrained_type_need(pattern);
    if (constrained) return constrained;
    uint32_t value = plan_need(pattern);
    return value > 1 ? value : 1;
}

static uint32_t plan_need_max_siblings(AstNode* first) {
    uint32_t best = 0;
    for (AstNode* n = first; n; n = n->next) {
        uint32_t need = plan_need(n);
        if (need > best) best = need;
    }
    return best;
}

typedef struct NeedAcc { uint32_t best; } NeedAcc;

static void plan_need_child(AstNode* child, void* ctx) {
    NeedAcc* acc = (NeedAcc*)ctx;
    uint32_t need = plan_need(child);
    if (need > acc->best) acc->best = need;
}

static uint32_t plan_need(AstNode* node) {
    if (!node) return 0;
    switch (node->node_type) {
    case AST_NODE_IDENT:
    case AST_NODE_LITERAL:
    case AST_NODE_SYS_FUNC:
    case AST_NODE_TYPE:
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
    case AST_NODE_LAST_INDEX:
    case AST_NODE_CURRENT_ERROR:
        // Function definitions build their closure through one helper call;
        // their bodies are planned separately, under their own frame.
        return 0;
    case AST_NODE_PRIMARY:
        return plan_need(((AstPrimaryNode*)node)->expr);
    case AST_NODE_UNARY:
        // The operand is published before the helper call, so one slot is live
        // at the call itself.
        return 1 + plan_need(((AstUnaryNode*)node)->operand);
    case AST_NODE_BINARY:
    case AST_NODE_PIPE: {
        AstBinaryNode* b = (AstBinaryNode*)node;
        uint32_t l = plan_need(b->left);
        // Left is held while right runs, and both are published across the
        // helper call, so the call itself has two slots live.
        // The mapping context owns five additional homes beside the source
        // while its right side runs; nested pipes retain the enclosing homes.
        uint32_t r = 6 + plan_need(b->right);
        // `x is T` for a named constrained T keeps x while T's predicates run
        if (node->node_type == AST_NODE_BINARY && b->op == OPERATOR_IS) {
            uint32_t constrained = plan_constrained_type_need(b->right);
            if (constrained && 1 + constrained > r) r = 1 + constrained;
        }
        // Mapping pipes retain the source, result, item, index, parent, and
        // root occurrence homes while evaluating each right-hand expression.
        uint32_t at_call = 6;
        uint32_t best = l > r ? l : r;
        return best > at_call ? best : at_call;
    }
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        // owner, key and value are each published to a slot and all three stay
        // live across the *_cow call, which may detach a copy. Budgeting fewer
        // makes those slots alias the rest of the frame and the write corrupts
        // unrelated bindings under collection pressure.
        AstCompoundAssignNode* ca = (AstCompoundAssignNode*)node;
        uint32_t o = plan_need(ca->object);
        uint32_t k = 1 + plan_need(ca->key);
        uint32_t v = 2 + plan_need(ca->value);
        uint32_t at_call = 3;
        uint32_t best = o > k ? o : k;
        if (v > best) best = v;
        // A NESTED target (`b.xs[0] = v`) takes the cow_path_set branch, which
        // is a deeper shape than the flat one this estimate was written for: it
        // holds value, path, terminal AND owner slots live simultaneously
        // across the call, and evaluates each path segment's key one level
        // below the first three. Budgeting the flat three overflowed the frame
        // and the write was dropped -- `b.xs[0] = 99` reported "scratch
        // overflow depth=5 cap=5" and left the array untouched.
        AstNode* target_object = ast_unwrap_primary(ca->object);
        if (target_object && (target_object->node_type == AST_NODE_MEMBER_EXPR ||
                target_object->node_type == AST_NODE_INDEX_EXPR)) {
            uint32_t nested = 4;                        // owner_slot is deepest
            uint32_t nested_value = 1 + plan_need(ca->value);
            uint32_t nested_key = 3 + plan_need(ca->key);
            uint32_t nested_seg = 3 + plan_need(ca->object);
            if (nested_value > nested) nested = nested_value;
            if (nested_key > nested) nested = nested_key;
            if (nested_seg > nested) nested = nested_seg;
            if (nested > best) best = nested;
        }
        return best > at_call ? best : at_call;
    }
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* f = (AstFieldNode*)node;
        uint32_t o = plan_need(f->object);
        uint32_t k = 1 + plan_need(f->field);
        uint32_t at_call = 2;
        uint32_t best = o > k ? o : k;
        return best > at_call ? best : at_call;
    }
    case AST_NODE_NAVIGATION_EXPR: {
        // A direct occurrence chain is evaluated once for the navigation
        // result and may be re-evaluated as its parent carrier; reserve both
        // homes in addition to the child expression's normal demand.
        AstNavigationNode* nav = (AstNavigationNode*)node;
        uint32_t child = plan_need(nav->object);
        uint32_t at_call = child + 3;
        return at_call > 3 ? at_call : 3;
    }
    case AST_NODE_CALL_EXPR:
    case AST_NODE_NEW_EXPR: {
        // The ordinary dynamic-call route borrows a bounded argument span
        // from its activation window. Reserve one additional operand for a
        // possible pipe injection; named and special calls retain their
        // existing RootSpan route and harmlessly leave this reservation idle.
        AstCallNode* c = (AstCallNode*)node;
        uint32_t fn = plan_need(c->function);
        uint32_t argument_slots = 1;
        for (AstNode* argument = c->argument; argument; argument = argument->next) {
            if (argument_slots == UINT32_MAX) return UINT32_MAX;
            argument_slots++;
        }
        uint32_t argument_need = plan_need_max_siblings(c->argument);
        if (argument_slots > UINT32_MAX - 1 ||
                argument_need > UINT32_MAX - argument_slots - 1) {
            return UINT32_MAX;
        }
        // The callee root remains live while each argument expression runs.
        uint32_t args = 1 + argument_slots + argument_need;
        uint32_t best = fn > args ? fn : args;
        // CW25 place borrow (`f(var m.rows[i])`): eval_call holds the prepared
        // root, the key path and the segment key it is evaluating -- three
        // scratch homes above the callee slot -- while each segment key runs.
        // Budgeting only the flat call shape overflowed the frame on typed
        // hashmap's `int_slot_set(hm.values, …)` ("scratch overflow depth=7
        // cap=7") once T0 admitted this argument shape.
        if (node->node_type == AST_NODE_CALL_EXPR) {
            AstFuncNode* direct = ast_direct_call_function(c);
            TypeFunc* signature = direct && ((AstNode*)direct)->type &&
                    ((AstNode*)direct)->type->type_id == LMD_TYPE_FUNC
                ? (TypeFunc*)((AstNode*)direct)->type : NULL;
            if (signature && ast_type_func_has_var_parameter(signature)) {
                NameEntry* borrowed[LAMBDA_MAX_FUNCTION_ARGS] = {0};
                AstNode* borrow_args[LAMBDA_MAX_FUNCTION_ARGS] = {0};
                if (ast_direct_call_var_parameter_entries(c, signature, borrowed,
                        borrow_args)) {
                    for (int index = 0; index < LAMBDA_MAX_FUNCTION_ARGS; index++) {
                        if (borrowed[index] || !borrow_args[index]) continue;
                        AstCowPath place = {};
                        if (!ast_collect_cow_path(&place, borrow_args[index])) continue;
                        uint32_t keys = 0;
                        for (int seg = 0; seg < place.count; seg++) {
                            uint32_t key = plan_need(place.segment[seg]);
                            if (key > keys) keys = key;
                        }
                        uint32_t borrow = 1 + 3 + keys;
                        if (borrow > best) best = borrow;
                    }
                }
            }
        }
        return best;
    }
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM: {
        // The operand remains rooted while either arm runs. The arms are
        // mutually exclusive, so only the selected arm contributes to the
        // maximum beyond that operand home.
        AstHandlerNode* handler = (AstHandlerNode*)node;
        uint32_t operand = plan_need(handler->operand);
        uint32_t body = 1 + plan_need(handler->body);
        uint32_t value = handler->value_body
            ? 1 + plan_need(handler->value_body) : 0;
        uint32_t best = operand > body ? operand : body;
        return value > best ? value : best;
    }
    case AST_NODE_IF_EXPR:
    case AST_NODE_CONDITIONAL_EXPR: {
        AstIfNode* i = (AstIfNode*)node;
        uint32_t best = plan_need(i->cond);
        uint32_t t = plan_need(i->then);
        uint32_t e = plan_need(i->otherwise);
        if (t > best) best = t;
        if (e > best) best = e;   // branches do not stack
        return best;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        uint32_t scrutinee = plan_need(match->scrutinee);
        uint32_t arms = plan_need_max_siblings((AstNode*)match->first_arm);
        // eval_match keeps `~`, `~#`, parent, and root live while an arm is
        // tested, so the arm's own shape starts above those four homes.
        uint32_t guarded_arms = 4 + arms;
        return scrutinee > guarded_arms ? scrutinee : guarded_arms;
    }
    case AST_NODE_MATCH_ARM: {
        AstMatchArm* arm = (AstMatchArm*)node;
        uint32_t pattern = plan_match_pattern_need(arm->pattern);
        uint32_t body = plan_need(arm->body);
        return pattern > body ? pattern : body;
    }
    case AST_NODE_CONSTRAINED_TYPE:
        // the node evaluates to its type-list entry; its body is planned as
        // its predicate function's frame, and a test roots that closure
        // (plan_constrained_type_need)
        return 0;
    case AST_NODE_ARRAY:
    case AST_NODE_SEQ:
    case AST_NODE_CONTENT:
        return 1 + plan_need_max_siblings(((AstArrayNode*)node)->item);
    case AST_NODE_LIST: {
        AstListNode* block = (AstListNode*)node;
        uint32_t decls = plan_need_max_siblings(block->declare);
        uint32_t items = 1 + plan_need_max_siblings(block->item);
        return decls > items ? decls : items;
    }
    case AST_NODE_FOR_EXPR: {
        AstForNode* fr = (AstForNode*)node;
        bool has_join = false;
        for (AstNode* item = fr->loop; item; item = item->next) {
            AstLoopNode* loop = (AstLoopNode*)item;
            if (loop->on || loop->join_keys || loop->optional) {
                has_join = true;
                break;
            }
        }
        if (has_join) {
            // Tuple materialization keeps source rows, join keys and prior
            // tuples live while a key/body can recurse. The explicit surplus
            // is a rooting safety margin, never a dynamic frame-growth path.
            uint32_t widest = plan_need_max_siblings(fr->loop);
            uint32_t candidates[] = {
                plan_need_max_siblings(fr->let_clause), plan_need(fr->where),
                plan_need(fr->then), plan_need_max_siblings(fr->order),
                plan_need(fr->limit), plan_need(fr->offset)
            };
            for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
                if (candidates[i] > widest) widest = candidates[i];
            }
            return 14 + widest;
        }
        if (!fr->group) {
            // eval_for always reserves the output and ordering-key homes,
            // even when the enclosing statement discards the stream;
            // interp_for_level then publishes one collection home before
            // evaluating each loop source. The old one-home floor omitted
            // those fixed homes, so nested call/for bodies could exhaust the
            // statically planned window at a GC safepoint.
            NeedAcc acc = {0};
            interp_visit_children(node, plan_need_child, &acc);
            return 3 + acc.best;
        }
        // Group materialization keeps the output/key streams, source, row
        // stream, key stream, and current row alive while row clauses run.
        // Multi-key groups add a tuple and a key-part home. This explicit
        // floor prevents grouped clauses from borrowing an unrelated slot at
        // a GC safepoint.
        uint32_t source = plan_need((AstNode*)fr->loop);
        uint32_t row_clause = plan_need_max_siblings(fr->let_clause);
        uint32_t where = plan_need(fr->where);
        if (where > row_clause) row_clause = where;
        uint32_t group_key = 0;
        int key_count = 0;
        for (AstGroupKey* key = fr->group->keys; key;
                key = (AstGroupKey*)((AstNode*)key)->next) {
            uint32_t need = plan_need(key->expr);
            if (need > group_key) group_key = need;
            key_count++;
        }
        uint32_t collect = 6 + row_clause;
        uint32_t key_collect = (key_count > 1 ? 8 : 7) + group_key;
        if (key_collect > collect) collect = key_collect;
        uint32_t source_collect = 2 + source;
        if (source_collect > collect) collect = source_collect;
        uint32_t post = plan_need(fr->then);
        uint32_t order = plan_need_max_siblings(fr->order);
        if (order > post) post = order;
        post += 4;  // output/key streams, groups stream, current group
        uint32_t final_clause = plan_need(fr->offset);
        uint32_t limit = plan_need(fr->limit);
        if (limit > final_clause) final_clause = limit;
        final_clause += 3;  // output/key streams plus selected stream
        if (post > collect) collect = post;
        return final_clause > collect ? final_clause : collect;
    }
    case AST_NODE_MAP: {
        AstMapNode* map = (AstMapNode*)node;
        if (!map->has_computed_key) return 1 + plan_need_max_siblings(map->item);
        uint32_t best = 1;  // the runtime-built map owner
        for (AstNode* item = map->item; item; item = item->next) {
            AstNamedNode* named = item->node_type == AST_NODE_KEY_EXPR
                ? (AstNamedNode*)item : NULL;
            uint32_t need = named && named->is_spread
                ? 1 + plan_need(named->as) : 2 + plan_need(named ? named->as : item);
            if (named && !named->is_spread) {
                uint32_t key_need = 1 + plan_need(named->key);
                if (key_need > need) need = key_need;
            }
            if (need > best) best = need;
        }
        return best;
    }
    case AST_NODE_ELEMENT: {
        AstElementNode* element = (AstElementNode*)node;
        if (!element->has_computed_key) {
            NeedAcc acc = {0};
            interp_visit_children(node, plan_need_child, &acc);
            return 1 + acc.best;
        }
        uint32_t best = 1;  // element owner
        for (AstNode* item = element->item; item; item = item->next) {
            AstNamedNode* named = item->node_type == AST_NODE_KEY_EXPR
                ? (AstNamedNode*)item : NULL;
            uint32_t need = named && named->is_spread
                ? 1 + plan_need(named->as) : 2 + plan_need(named ? named->as : item);
            if (named && !named->is_spread) {
                uint32_t key_need = 1 + plan_need(named->key);
                if (key_need > need) need = key_need;
            }
            if (need > best) best = need;
        }
        uint32_t content_need = 1 + plan_need(element->content);
        return content_need > best ? content_need : best;
    }
    case AST_NODE_OBJECT_LITERAL: {
        AstObjectLiteralNode* literal = (AstObjectLiteralNode*)node;
        // eval_object_literal keeps its optional spread home in scope even for
        // an ordinary typed literal, then publishes the fresh object in a
        // second home after all fields have been evaluated. The old one-home
        // floor undercounted a literal nested in a content accumulator and
        // let the fresh object borrow the frame's signal boundary.
        uint32_t need = 2 + plan_need_max_siblings(literal->item);
        if (ast_object_literal_spread_value(literal)) {
            // A typed `*:source` literal keeps the source and a member key
            // alive while object_fill performs numeric coercion; the ordinary
            // one-home literal floor omitted those two simultaneous homes.
            uint32_t spread_need = 3 + plan_need_max_siblings(literal->item);
            if (spread_need > need) need = spread_need;
        }
        return need;
    }
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* declarator = (AstDeclaratorNode*)node;
        uint32_t need = plan_need(declarator->init);
        if (declarator->entry && declarator->entry->cow_borrow_lowered) {
            // CW34: the read-modify-write bind keeps the root, the read value
            // and up to two intermediate keys in scratch while it re-evaluates
            // the path keys for the spine test (cow_bind_rmw_handle).
            AstCowPath place = {};
            uint32_t keys = 0;
            if (ast_collect_cow_path(&place, declarator->init)) {
                for (int seg = 0; seg + 1 < place.count; seg++) {
                    uint32_t key = plan_need(place.segment[seg]);
                    if (key > keys) keys = key;
                }
            }
            if (4 + keys > need) need = 4 + keys;
        }
        return declarator->declared_type && need < 1 ? 1 : need;
    }
    case AST_NODE_PARAM: {
        AstNamedNode* named = (AstNamedNode*)node;
        uint32_t need = plan_need(named->as);
        // A declared binding roots its source while the checked numeric/array
        // boundary may allocate. Without this floor, `fn f(x: int) { x }`
        // planned no scratch slot even though parameter entry must convert x.
        return named->declared_type && need < 1 ? 1 : need;
    }
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* named = (AstNamedNode*)node;
        int key_need = plan_need(named->key);
        int value_need = plan_need(named->as);
        return key_need > value_need ? key_need : value_need;
    }
    case AST_NODE_NAMED_ARG:
        // The destination is a named slot or the enclosing builder, not scratch.
        return plan_need(((AstNamedNode*)node)->as);
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_VAR_STAM:
    case AST_NODE_TYPE_STAM:
        return plan_need_max_siblings(((AstLetNode*)node)->declare);
    default: {
        // Conservative for every kind not yet in the table: assume the node
        // holds one Item of its own across the widest child. Undercounting is
        // the one failure mode that loses a root (R1), so the fallback rounds up.
        NeedAcc acc = {0};
        interp_visit_children(node, plan_need_child, &acc);
        return 1 + acc.best;
    }
    }
}

static void plan_walk(AstNode* node, void* ctx);
static void plan_finish(PlanCtx* pc);

static void plan_handler(PlanCtx* outer, AstViewNode* view,
        AstEventHandler* handler) {
    if (!outer || !handler || handler->interp_planned) return;

    PlanCtx pc = {};
    pc.script = outer->script;
    pc.plan = &handler->interp_plan;
    pc.storage = BINDING_STORAGE_REGISTER;
    // Body lets and state bindings use the same slots in the view and handler.
    pc.next_slot = outer->next_slot;
    // The event parameter is overlaid by the view activation; its slot is
    // harmless but keeps all handler-scope names consistently addressable.
    plan_assign_scope(&pc, handler->vars);
    plan_walk(handler->body, &pc);

    // View-handler activation publishes the matched model in one additional
    // frame home before evaluating the body (D5.3.3). Include that home in the
    // handler plan or the first callee expression can exhaust the scratch
    // window while the model is still live.
    uint32_t body_need = 1 + plan_need(handler->body);
    if (view->body && view->body->node_type == AST_NODE_CONTENT) {
        for (AstNode* item = ((AstListNode*)view->body)->item;
                item; item = item->next) {
            if (!is_view_handler_body_binding(item->node_type)) continue;
            uint32_t declaration_need = 1 + plan_need(item);
            if (declaration_need > body_need) body_need = declaration_need;
        }
    }
    if (body_need > pc.max_scratch) pc.max_scratch = body_need;
    plan_finish(&pc);
    if (pc.failed) {
        outer->failed = true;
        return;
    }
    handler->interp_planned = true;
}

static void plan_view(PlanCtx* outer, AstViewNode* view) {
    if (!view || !view->body || view->interp_planned) return;
    PlanCtx pc = {};
    pc.script = outer->script;
    pc.plan = &view->interp_plan;
    pc.storage = BINDING_STORAGE_REGISTER;
    plan_assign_scope(&pc, view->vars);
    for (AstStateEntry* state = view->state; state; state = state->next_state) {
        plan_walk(state->value, &pc);
        // The model occupies a scratch home while a state default evaluates.
        uint32_t state_need = 1 + plan_need(state->value);
        if (state_need > pc.max_scratch) pc.max_scratch = state_need;
    }
    plan_walk(view->body, &pc);
    uint32_t body_need = 1 + plan_need(view->body);
    if (body_need > pc.max_scratch) pc.max_scratch = body_need;
    plan_finish(&pc);
    if (pc.failed) { outer->failed = true; return; }
    for (AstEventHandler* handler = view->handler; handler;
            handler = handler->next_handler) {
        plan_handler(&pc, view, handler);
        if (pc.failed) { outer->failed = true; return; }
    }
    view->interp_planned = true;
}

// Marks self-recursive calls that sit in tail position, so the walker can turn
// them into a loop. Tail position propagates exactly where lowering's
// `in_tail_position` does: the body itself, the last value of a content block,
// both arms of an `if`, and a `return` operand.
static void plan_mark_tail_calls(AstNode* node, AstFuncNode* fn) {
    while (node && node->node_type == AST_NODE_PRIMARY &&
            ((AstPrimaryNode*)node)->expr) {
        node = ((AstPrimaryNode*)node)->expr;
    }
    if (!node) return;
    switch (node->node_type) {
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        // A propagating call (`f(...)^`) still has to inspect its result, so it
        // is not a tail position even though it is syntactically last.
        // a colour-guarded call keeps its entry so the check runs (S12.1.4v2)
        if (!call->propagate && !call->fn_colour_guard && is_recursive_call(call, fn)) {
            call->interp_self_tail_call = true;
        }
        break;
    }
    case AST_NODE_IF_EXPR:
        plan_mark_tail_calls(((AstIfNode*)node)->then, fn);
        plan_mark_tail_calls(((AstIfNode*)node)->otherwise, fn);
        break;
    case AST_NODE_RETURN_STAM:
        plan_mark_tail_calls(((AstReturnNode*)node)->value, fn);
        break;
    case AST_NODE_BLOCK: {
        // The direct parser wraps a function body in a block of expression
        // statements. Tail position belongs only to that block's final
        // statement; skipping this wrapper leaves direct-parser self recursion
        // on the native interpreter stack instead of the established TCO loop.
        AstNode* last = ((AstBlockNode*)node)->statements;
        while (last && last->next) last = last->next;
        if (last) plan_mark_tail_calls(last, fn);
        break;
    }
    case AST_NODE_EXPR_STMT:
        plan_mark_tail_calls(((AstExprStmtNode*)node)->expression, fn);
        break;
    case AST_NODE_CONTENT:
    case AST_NODE_LIST: {
        // Only the block's value expression is a tail position; declarations
        // and side-effect statements are not, and a multi-value block builds a
        // list, so none of its items are either.
        AstListNode* block = (AstListNode*)node;
        AstNode* last_value = NULL;
        int value_count = 0;
        for (AstNode* item = block->item; item; item = item->next) {
            if (is_declaration_node(item->node_type) ||
                    is_side_effect_stam(item->node_type)) {
                // A trailing `return` is a side-effect statement but still
                // carries the function's result.
                if (item->node_type == AST_NODE_RETURN_STAM) {
                    plan_mark_tail_calls(item, fn);
                }
                continue;
            }
            value_count++;
            last_value = item;
        }
        if (value_count == 1) plan_mark_tail_calls(last_value, fn);
        break;
    }
    default:
        break;
    }
}

static void plan_finish(PlanCtx* pc) {
    FnFramePlan* plan = pc->plan;
    if (!plan) return;
    plan->param_count = (uint16_t)pc->param_count;
    uint32_t named = pc->next_slot;
    plan->local_count = (uint16_t)(named - pc->param_count);
    plan->vargs_index = pc->is_variadic ? (uint16_t)named : UINT16_MAX;
    plan->scratch_depth = (uint16_t)pc->max_scratch;
    uint32_t total = named + (pc->is_variadic ? 1 : 0) + 1 /* signal slot */ +
        pc->max_scratch;
    if (total > UINT16_MAX) { pc->failed = true; return; }
    plan->total_slots = (uint16_t)total;
    plan->planned = true;
}

// A procedural block's value is its LAST value expression; every earlier one,
// and every declaration, loop and side-effect statement, runs as a statement.
// Returns that item (NULL when the block has none) and the three counts.
AstNode* interp_proc_block_last_value(AstListNode* list_node,
        int* value_count, int* decl_count, int* stam_count) {
    AstNode* last_executable = NULL;
    for (AstNode* scan = list_node->item; scan; scan = scan->next) {
        if (!is_declaration_node(scan->node_type)) last_executable = scan;
    }
    AstNode* last_value = NULL;
    for (AstNode* item = list_node->item; item; item = item->next) {
        if (is_declaration_node(item->node_type)) { (*decl_count)++; continue; }
        if (is_side_effect_stam(item->node_type) ||
                is_proc_flow_side_effect_node(item, last_executable) ||
                item->node_type == AST_NODE_LOOP ||
                ast_for_discards_result(item)) {
            (*stam_count)++;
            continue;
        }
        (*value_count)++;
        last_value = item;
    }
    return last_value;
}

// Record a block's procedural shape once; eval_content read it from the
// items at every evaluation (3% of a loop-heavy T0 profile). Counts that do
// not fit the fields leave the block unscanned, so it keeps the live scan.
static void plan_scan_proc_block(AstListNode* list_node) {
    int values = 0, decls = 0, stams = 0;
    AstNode* last_value = interp_proc_block_last_value(list_node, &values, &decls, &stams);
    if (values > UINT16_MAX || decls > UINT16_MAX || stams > UINT16_MAX) return;
    list_node->interp_proc_last_value = last_value;
    list_node->interp_proc_value_count = (uint16_t)values;
    list_node->interp_proc_decl_count = (uint16_t)decls;
    list_node->interp_proc_stam_count = (uint16_t)stams;
    list_node->interp_proc_scanned = true;
}

// D8.1.1v14: a `while` that is a direct statement of a `pn` body can hand the
// running activation to compiled code at its head test. There T0's whole live
// state is the frame's named slots, and the statements from the loop to the
// end of the body form a function of them (the continuation). Nested loops
// are not numbered: their back-edges count toward the enclosing handoff loop.
static void plan_mark_handoff_loops(AstFuncNode* fn) {
    if (((AstNode*)fn)->node_type != AST_NODE_PROC) return;
    AstNode* body = ast_unwrap_primary(fn->body);
    if (!body || (body->node_type != AST_NODE_CONTENT &&
            body->node_type != AST_NODE_LIST)) return;
    uint8_t ordinal = 0;
    for (AstNode* item = ((AstListNode*)body)->item; item; item = item->next) {
        if (item->node_type != AST_NODE_LOOP) continue;
        AstLoopControlNode* loop = (AstLoopControlNode*)item;
        if (loop->form != LOOP_FORM_WHILE) continue;
        if (ordinal >= INTERP_HANDOFF_LOOP_MAX) break;
        loop->interp_handoff_ordinal = ++ordinal;
    }
}

// Enter a nested function definition: a fresh plan, a fresh slot space.
static void plan_function(PlanCtx* outer, AstFuncNode* fn) {
    if (!fn || !fn->body) return;
    if (!plan_ensure_analysis(outer, fn)) { outer->failed = true; return; }
    if (fn->analysis->frame_plan.planned) return;

    PlanCtx pc = {0};
    pc.script = outer->script;
    pc.plan = &fn->analysis->frame_plan;
    pc.function = fn;
    pc.storage = BINDING_STORAGE_REGISTER;
    TypeFunc* signature = (TypeFunc*)((AstNode*)fn)->type;
    pc.is_variadic = signature && signature->type_id == LMD_TYPE_FUNC &&
        signature->is_variadic;

    // Parameters are the first entries pushed into the function scope, so the
    // scope walk gives them slots 0..n-1 in declaration order. push_name never
    // writes AstNamedNode::entry, so the structural param list is the only
    // authority on how many of those leading slots are parameters.
    plan_assign_scope(&pc, fn->vars);
    int param_index = 0;
    for (AstNamedNode* p = fn->param; p; p = (AstNamedNode*)((AstNode*)p)->next) {
        if (!p->entry || !p->entry->storage_assigned || p->entry->slot != param_index) {
            log_error("frame-plan: parameter %d of '%s' is not at its frame slot",
                param_index, fn->name ? fn->name->chars : "<anon>");
            pc.failed = true;
            break;
        }
        uint32_t parameter_need = plan_need((AstNode*)p);
        if (parameter_need > pc.max_scratch) pc.max_scratch = parameter_need;
        param_index++;
    }
    pc.param_count = (uint32_t)param_index;

    if (signature && signature->has_explicit_return_contract) {
        pc.plan->return_boundary = interp_boundary_plan_create(pc.script->pool,
            signature->return_contract);
        plan_destination(pc.script->pool, fn->body, signature->return_contract);
    }
    plan_walk(fn->body, &pc);
    plan_mark_handoff_loops(fn);
    // should_use_tco is lowering's own eligibility test (named, not a closure,
    // has a tail-recursive call), so both tiers turn the same functions into
    // loops and a deep tail recursion cannot overflow in only one of them (R8).
    if (should_use_tco(fn)) plan_mark_tail_calls(fn->body, fn);
    uint32_t body_need = plan_need(fn->body);
    if (body_need > pc.max_scratch) pc.max_scratch = body_need;
    if (signature && signature->has_explicit_return_contract &&
            pc.max_scratch < 1) {
        // Return-contract admission roots the computed value while the shared
        // checker may allocate. A literal-only body otherwise plans zero
        // scratch slots and the checker would borrow the signal home (S7.7.2).
        pc.max_scratch = 1;
    }
    plan_finish(&pc);
    if (pc.failed) outer->failed = true;

    log_debug("frame-plan: fn '%s' params=%u locals=%u scratch=%u total=%u",
        fn->name ? fn->name->chars : "<anon>",
        (unsigned)pc.plan->param_count, (unsigned)pc.plan->local_count,
        (unsigned)pc.plan->scratch_depth, (unsigned)pc.plan->total_slots);
}

static void plan_walk(AstNode* node, void* ctx) {
    PlanCtx* pc = (PlanCtx*)ctx;
    if (!node || pc->failed) return;
    switch (node->node_type) {
    case AST_NODE_IDENT:
        plan_link_capture_identifier(pc, (AstIdentNode*)node);
        break;
    case AST_NODE_CALL_EXPR:
    case AST_NODE_NEW_EXPR:
        plan_link_call_shape(pc->script->pool, (AstCallNode*)node);
        break;
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_ARROW_FUNC:
        // A definition's own name binds in the enclosing scope; its body opens
        // a separate activation, so it never consumes enclosing slots.
        plan_function(pc, (AstFuncNode*)node);
        return;
    case AST_NODE_VIEW:
        plan_view(pc, (AstViewNode*)node);
        return;
    case AST_NODE_EVENT_HANDLER:
        // The owning view plans handlers after its body bindings.
        return;
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)node;
        plan_assign_entry(pc, declaration->entry);
        declaration->interp_boundary_proven =
            plan_scalar_boundary_proven(declaration->init, declaration->declared_type);
        plan_destination(pc->script->pool, declaration->init, declaration->declared_type);
        break;
    }
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* access = (AstFieldNode*)node;
        TypeMap* shape = (TypeMap*)lambda_type_nonnull_map_contract(
            access->object ? access->object->type : NULL);
        if (shape && shape->is_trusted_contract && !access->computed && access->field &&
                access->field->node_type == AST_NODE_IDENT) {
            String* name = ((AstIdentNode*)access->field)->name;
            ShapeEntry* field = name ? typemap_shape_lookup_last(shape, name->chars, (int)name->len) : NULL;
            if (field && field->name && !(field->flags & JSPD_IS_ACCESSOR)) {
                access->interp_field = (InterpFieldPlan*)pool_calloc(pc->script->pool,
                    sizeof(InterpFieldPlan));
                if (access->interp_field) {
                    access->interp_field->shape = shape;
                    access->interp_field->field = field;
                }
            }
        }
        break;
    }
    case AST_NODE_INDEX_EXPR: {
        AstFieldNode* access = (AstFieldNode*)node;
        AstNode* object = ast_unwrap_primary(access->object);
        NameEntry* entry = object && object->node_type == AST_NODE_IDENT
            ? ((AstIdentNode*)object)->entry : NULL;
        InterpBoundaryPlan* boundary = entry ? entry->interp_boundary : NULL;
        ArrayNumElemType element;
        if (boundary && boundary->array.rank == 1 && access->field &&
                !access->field->next && lambda_array_num_elem_type_for_contract(
                    boundary->array.leaf_element, &element)) {
            // the declared lane predicts a decoder; the live carrier still guards it
            access->interp_field = (InterpFieldPlan*)pool_calloc(pc->script->pool,
                sizeof(InterpFieldPlan));
            if (access->interp_field) {
                access->interp_field->numeric_index = true;
                access->interp_field->numeric_element = element;
            }
        }
        break;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        binary->interp_int_arithmetic =
            (binary->op == OPERATOR_ADD || binary->op == OPERATOR_SUB ||
             binary->op == OPERATOR_MUL) &&
            binary->left && binary->left->type && binary->left->type->type_id == LMD_TYPE_INT &&
            binary->right && binary->right->type && binary->right->type->type_id == LMD_TYPE_INT;
        break;
    }
    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* assignment = (AstAssignStamNode*)node;
        assignment->interp_boundary_proven = plan_scalar_boundary_proven(assignment->value,
            assignment->target_entry ? assignment->target_entry->declared_type : NULL);
        plan_destination(pc->script->pool, assignment->value, assignment->target_entry
            ? assignment->target_entry->declared_type : NULL);
        break;
    }
    case AST_NODE_RETURN_STAM:
        plan_destination(pc->script->pool, ((AstReturnNode*)node)->value, pc->plan->return_boundary
            ? pc->plan->return_boundary->contract : NULL);
        break;
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
        plan_place(pc, (AstCompoundAssignNode*)node);
        break;
    case AST_NODE_PARAM:
    case AST_NODE_KEY_EXPR:
        plan_assign_entry(pc, ((AstNamedNode*)node)->entry);
        break;
    case AST_NODE_LIST:
    case AST_NODE_CONTENT:
        plan_assign_scope(pc, ((AstListNode*)node)->vars);
        plan_scan_proc_block((AstListNode*)node);
        break;
    case AST_NODE_FOR_EXPR:
        plan_assign_scope(pc, ((AstForNode*)node)->vars);
        break;
    case AST_NODE_GROUP_CLAUSE:
        // `into` is registered in a deliberately detached post-group scope,
        // so it is not reached by the owning for scope walk above.
        plan_assign_entry(pc, ((AstGroupClause*)node)->entry);
        break;
    case AST_NODE_LOOP:
        plan_assign_scope(pc, ((AstWhileNode*)node)->vars);
        break;
    case AST_NODE_BLOCK:
        plan_assign_scope(pc, ((AstBlockNode*)node)->vars);
        break;
    default:
        break;
    }
    interp_visit_children(node, plan_walk, ctx);
}

bool interp_plan_script(Script* script) {
    if (!script || !script->ast_root) return false;
    if (script->interp_planned) return true;

    AstScript* root = (AstScript*)script->ast_root;
    // T0 bypasses MIR's module prepass, so register named patterns here before
    // any identifier can materialize its TypePattern through the type list.
    if (!compile_script_pattern_definitions(script->pool, script->type_list, root->child)) {
        log_error("frame-plan: pattern prepass failed for '%s'", script->reference);
        return false;
    }
    PlanCtx pc = {0};
    pc.script = script;
    pc.plan = &script->interp_plan;
    // Module-level bindings live in the per-context module slab, not in a
    // per-activation window: they outlive the top-level frame (D7.2.1/AI6).
    pc.storage = BINDING_STORAGE_MODULE;
    plan_assign_scope(&pc, root->global_vars);

    for (AstNode* item = root->child; item; item = item->next) plan_walk(item, &pc);
    uint32_t body_need = 0;
    for (AstNode* item = root->child; item; item = item->next) {
        uint32_t need = plan_need(item);
        if (need > body_need) body_need = need;
    }
    // The top-level content list itself accumulates through one rooted builder.
    if (body_need + 1 > pc.max_scratch) pc.max_scratch = body_need + 1;

    uint32_t module_slots = pc.next_slot;
    plan_finish(&pc);
    if (pc.failed) {
        log_error("frame-plan: slot budget exceeded for '%s'", script->reference);
        return false;
    }
    // Module bindings are slab-resident, so the top-level frame window holds
    // only its signal slot plus scratch.
    script->interp_slab_count = module_slots;
    script->interp_plan.param_count = 0;
    script->interp_plan.local_count = 0;
    script->interp_plan.vargs_index = UINT16_MAX;
    script->interp_plan.total_slots = (uint16_t)(1 + pc.max_scratch);
    script->interp_planned = true;

    log_info("frame-plan: script '%s' module_slots=%u scratch=%u total=%u",
        script->reference ? script->reference : "<none>",
        (unsigned)module_slots, (unsigned)pc.max_scratch,
        (unsigned)script->interp_plan.total_slots);
    return true;
}

bool interp_plan_repl_fragment(Script* script, AstNode* fragment) {
    if (!script || !script->ast_root || !fragment || !script->interp_planned) {
        return false;
    }
    AstScript* root = (AstScript*)script->ast_root;
    // A REPL cell may introduce a new named pattern after the module plan was
    // sealed; keep its runtime TypePattern registration module-local as well.
    if (!compile_script_pattern_definitions(script->pool, script->type_list, fragment)) {
        log_error("frame-plan: REPL pattern prepass failed");
        return false;
    }
    PlanCtx pc = {};
    pc.script = script;
    pc.plan = &script->interp_plan;
    pc.storage = BINDING_STORAGE_MODULE;
    // Earlier cells hold persistent closures and values in these slots. Start
    // after them so appending a REPL cell cannot renumber a live binding.
    pc.next_slot = script->interp_slab_count;
    pc.max_scratch = script->interp_plan.scratch_depth;
    plan_assign_scope(&pc, root->global_vars);
    for (AstNode* item = fragment; item; item = item->next) plan_walk(item, &pc);
    uint32_t need = plan_need(fragment);
    if (need + 1 > pc.max_scratch) pc.max_scratch = need + 1;
    if (pc.failed || pc.next_slot > UINT16_MAX || pc.max_scratch > UINT16_MAX - 1) {
        log_error("frame-plan: REPL fragment exceeds module/frame slot budget");
        return false;
    }
    script->interp_slab_count = pc.next_slot;
    if (pc.max_scratch > script->interp_plan.scratch_depth) {
        script->interp_plan.scratch_depth = (uint16_t)pc.max_scratch;
        script->interp_plan.total_slots = (uint16_t)(1 + pc.max_scratch);
    }
    log_debug("frame-plan: REPL fragment module_slots=%u scratch=%u",
        (unsigned)script->interp_slab_count,
        (unsigned)script->interp_plan.scratch_depth);
    return true;
}

// ---------------------------------------------------------------------------
// P2 satellite eligibility
// ---------------------------------------------------------------------------

// Satellites reuse T0's planned module slab. The bounded path admits only
// reads and stable imports; captures, nested definitions, generators, and
// replacement writes remain on T0 (D8.1.1v2 §5.2-§5.3).
bool interp_satellite_import_binding(const Script* importer,
        const NameEntry* entry, Script** out_owner, int* out_slot) {
    if (out_owner) *out_owner = NULL;
    if (out_slot) *out_slot = -1;
    if (!entry || !entry->import || entry->import->is_cross_lang) return false;
    Script* owner = entry && entry->import && !entry->import->is_cross_lang
        ? lambda_ast_overlay_import_script(importer, entry->import) : NULL;
    if (!owner && entry) owner = entry->import_owner;
    bool storage_assigned = entry->storage_assigned;
    BindingStorage storage = entry->binding_storage;
    int slot = entry->slot;
    // Function-local imported names do not receive their own module-slab
    // entry. Derive their exported slot for this lowering only; cache templates
    // must not retain a prior execution's resolution in NameEntry (D8.5.1v2).
    if ((!storage_assigned || storage != BINDING_STORAGE_MODULE || slot < 0) &&
            owner && entry->node) {
        AstScript* owner_root = (AstScript*)owner->ast_root;
        for (NameEntry* exported = owner_root && owner_root->global_vars
                ? owner_root->global_vars->first : NULL;
                exported; exported = exported->next) {
            if (exported->node != entry->node || !exported->storage_assigned ||
                    exported->binding_storage != BINDING_STORAGE_MODULE ||
                    exported->slot < 0) continue;
            storage_assigned = true;
            storage = exported->binding_storage;
            slot = exported->slot;
            break;
        }
    }
    if (!owner || !storage_assigned || storage != BINDING_STORAGE_MODULE || slot < 0) {
        return false;
    }
    if (out_owner) *out_owner = owner;
    if (out_slot) *out_slot = slot;
    return true;
}

bool interp_satellite_import_supported(const Script* importer,
        const NameEntry* entry) {
    Script* owner = NULL;
    if (!interp_satellite_import_binding(importer, entry, &owner, NULL)) return false;
    // The target module must already be a planned T0 module; the satellite
    // embeds this stable module id and slot rather than asking MIR to link a
    // missing generated import symbol.
    bool supported = owner->interp_supported && owner->interp_planned;
    return supported;
}

// T21-3b (D8.1.1v6): an untyped `var` parameter crosses a tier boundary
// through the CW33 home-transport cells (Context::mir_var_homes) -- the
// satellite prologue and epilogue implement the generated side, and T0
// sets/consumes the same cells around its calls. D8.1.1v8 admitted TYPED
// `var` parameters (`var x: float[]`, `var p: Rec`) with a pin on bodies
// that REBIND one, because the generated entry then had no home for such a
// position. D8.1.1v10: every publishable `var` position now consumes its
// home in the generated prologue and publishes its final boxed value in the
// epilogue (the `_b` wrapper forwards T0's cell to the raw body), so a
// rebind reaches the T0 caller exactly as an untyped one does and the pin
// is gone (fixture `interp_typed_var_rebind.ls`, `cow_var_typed_rebind.ls`).

// T27-6: a `match` needs no T0-owned pattern image when every arm pattern
// is a type name, a scalar literal, or a union of those -- MIR lowers them as
// `fn_is`/`fn_eq` against loads from the image's own type list and const
// pool, exactly as it lowers `x is T` and `x == lit`. Named string/symbol
// patterns, ranges, and constrained `that` arms stay in T0 (D8.1.1v9).
static bool interp_satellite_match_pattern_supported(AstNode* pattern) {
    while (pattern && pattern->node_type == AST_NODE_PRIMARY &&
            ((AstPrimaryNode*)pattern)->expr) {
        pattern = ((AstPrimaryNode*)pattern)->expr;
    }
    if (!pattern) return false;
    switch (pattern->node_type) {
    case AST_NODE_PRIMARY:  // a primary without an inner expression is a literal
    case AST_NODE_TYPE:
    case AST_NODE_LITERAL:
        return true;
    case AST_NODE_BINARY_TYPE: {
        AstBinaryNode* bi = (AstBinaryNode*)pattern;
        return bi->op == OPERATOR_UNION &&
            interp_satellite_match_pattern_supported(bi->left) &&
            interp_satellite_match_pattern_supported(bi->right);
    }
    default:
        return false;
    }
}

static bool interp_satellite_match_supported(AstMatchNode* match) {
    for (AstNode* arm = (AstNode*)match->first_arm; arm; arm = arm->next) {
        AstMatchArm* match_arm = (AstMatchArm*)arm;
        // the default arm has no pattern to lower
        if (match_arm->pattern &&
                !interp_satellite_match_pattern_supported(match_arm->pattern)) {
            return false;
        }
    }
    return true;
}

static void interp_scan_satellite_node_kind(AstNode* node, SatelliteScanCtx* sc);

static void interp_scan_satellite_node(AstNode* node, void* opaque) {
    SatelliteScanCtx* sc = (SatelliteScanCtx*)opaque;
    if (!node || !sc->ok) return;
    interp_scan_satellite_node_kind(node, sc);
    // children refuse first, so the first recorded kind is the innermost one
    if (!sc->ok && sc->reject == AST_NODE_NULL) sc->reject = node->node_type;
}

static void interp_scan_satellite_node_kind(AstNode* node, SatelliteScanCtx* sc) {
    switch (node->node_type) {
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_ARROW_FUNC:
        // Nested definitions need an explicit cross-satellite closure contract.
        sc->ok = false;
        return;
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_VIEW:
        sc->ok = false;
        return;
    // T21-3 / D8.1.1v6: local `var` declarations, rebinding assignments and
    // (T21-3b) indexed/member stores are all owned by the promoted activation:
    // a store through a local or a value parameter replaces a MIR register
    // the satellite roots itself, a store through a module binding goes to
    // the shared slab, and a store through an untyped `var` parameter is
    // published to the caller's home by the CW33 epilogue. Refusing them had
    // pinned nearly every procedural body to T0.
    case AST_NODE_MATCH_EXPR:
        // Pattern arms that carry compiled regex/type-list state are owned by
        // the T0 module activation; a satellite has no equivalent pattern
        // image, so those keep the whole match expression in T0 (D5.2). Type
        // and literal arms have no such state (T27-6).
        if (!interp_satellite_match_supported((AstMatchNode*)node)) {
            sc->ok = false;
            return;
        }
        break;
    case AST_NODE_BINARY:
        // `is` on a constrained type calls its predicate function (S11.4.11),
        // which a satellite image does not carry, as a constrained arm above
        if (((AstBinaryNode*)node)->op == OPERATOR_IS &&
                ast_constrained_type(((AstBinaryNode*)node)->right)) {
            sc->ok = false;
            return;
        }
        break;
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee = ast_unwrap_primary(call->function);
        AstFuncNode* direct = ast_direct_call_function(call);
        if (callee && callee->node_type != AST_NODE_SYS_FUNC && !direct) {
            // A satellite cannot prove the target ABI for an indirect Lambda
            // call. An `any` callee may resolve to a `var` procedure after
            // promotion, and the boxed dispatcher has no caller-root
            // write-back channel to defer that edge to (D3.3.1 / D5.2).
            //
            // Deliberately broader than "local dynamic binding". A named
            // `fn`/`pn` callee never reaches this point at all -- for those
            // ast_direct_call_function resolves the target and `direct` is
            // non-NULL -- so restricting the reject to non-function local
            // bindings would leave imported indirect callees and computed or
            // member callees admitted on an ABI nothing can prove.
            sc->ok = false;
            return;
        }
        break;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* ident = (AstIdentNode*)node;
        NameEntry* entry = ident->entry;
        if (!entry) break;
        if (entry->node && entry->node->node_type == AST_NODE_KEY_EXPR) {
            // Object-method field identifiers resolve through the receiver's
            // shape, not a stable scalar/module slot. The satellite native
            // lane cannot reconstruct that receiver field contract (D2.2.2,
            // D5.2), so keep the method in T0.
            sc->ok = false;
            return;
        }
        bool hosted_js = entry->import && entry->import->is_cross_lang &&
            entry->import->script && entry->import->script->profile == &js_profile;
        if (entry->import && !hosted_js &&
                !interp_satellite_import_supported(NULL, entry)) {
            sc->ok = false;
        }
        break;
    }
    default:
        break;
    }
    if (sc->ok) interp_visit_children(node, interp_scan_satellite_node, sc);
}

bool interp_satellite_supported(const AstFuncNode* fn) {
    return interp_satellite_refusal(fn) == NULL;
}

const char* interp_satellite_refusal(const AstFuncNode* fn) {
    if (!fn || !fn->analysis || !fn->body) return "no-analysis";
    if (fn->captures) return "captures";
    if (fn->is_generator) return "generator";
    if (fn->is_async) return "async";
    TypeFunc* signature = (TypeFunc*)((AstNode*)fn)->type;
    for (TypeParam* param = signature ? signature->param : NULL;
            param; param = param->next) {
        Type* contract = param->contract_type ? param->contract_type :
            (Type*)param;
        TypeId tid = contract ? contract->type_id : LMD_TYPE_ANY;
        // D8.1.1v7: aggregate and structured VALUE parameters (`float[]`,
        // `Rec`, `map`) are admitted. The v5 pin argued the satellite's raw
        // carrier specialization could mis-decode them, but a satellite is
        // entered through its boxed `_b` wrapper, which admits each argument
        // under the declared contract exactly as the eager module compiler's
        // wrapper does before the raw entry sees it (D2.2.2, D3.2.1); the
        // pin kept nbody2, pnpoly2, gcbench2, deriv2, ray2 and array1 in T0
        // at 9-110x their JIT time. A typed `var` parameter stays pinned
        // above (no CW33 home under the raw-lane ABI).
        (void)contract; (void)tid;
    }
    // Keep the satellite admission gate aligned with the complete T0
    // capability scanner. The old satellite-only walk checked nested
    // definitions and imports but skipped system ABI, COW, index-shape, and
    // call-signature guards; complex promoted bodies then ran a MIR subset
    // that silently dropped layout/PDF/editor state (D8.1.1v4).
    ScanCtx full_scan = {true, AST_NODE_NULL};
    interp_scan_visit(fn->body, &full_scan);
    if (!full_scan.ok) return interp_node_kind_name(full_scan.reject);
    SatelliteScanCtx sc = {true, AST_NODE_NULL};
    interp_scan_satellite_node((AstNode*)fn->body, &sc);
    return sc.ok ? NULL : interp_node_kind_name(sc.reject);
}
