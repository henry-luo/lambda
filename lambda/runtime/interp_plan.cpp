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
    // P3: the walker resolves the type-list entry and evaluates its `that`
    // clause only through EvalMode::PREDICATE; raw Type* identity is never
    // used as a substitute for the constraint.
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
#undef K
    default: return "AST_NODE_<unknown>";
    }
}

typedef struct ScanCtx {
    bool ok;
    AstNodeType reject;
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

// `a[i] = v` normally needs a statically integral subscript before it can use
// T0's int64 COW bridge. A `to` source may be either an exact-integer range or
// a character range, but one explicit integer bound rules out the latter; any
// non-integer opposite bound then errors before the loop body. Range iteration
// yields an int on every successful such step. Keep the proof structural so
// another dynamic `any` binding cannot accidentally reach machine conversion.
static bool interp_range_loop_index_expr(AstNode* node) {
    if (interp_integer_literal(node)) return true;
    node = ast_unwrap_primary(node);
    if (!node) return false;
    if (node->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        if (!entry || !entry->node || entry->node->node_type != AST_NODE_FOR_CLAUSE) {
            return false;
        }
        AstLoopNode* loop = (AstLoopNode*)entry->node;
        AstNode* source = ast_unwrap_primary(loop->as);
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
    return interp_range_loop_index_expr(binary->left) &&
        interp_range_loop_index_expr(binary->right);
}

// The native concurrency analysis must conservatively classify an indirect
// `pn` call as await-capable because lowering cannot prove its runtime target.
// T0 may admit only the smaller immutable-alias case whose reachable bodies
// contain no task edge; this must not weaken the shared async analysis.
typedef struct InterpSyncProcScan {
    AstFuncNode* active[64];
    int active_count;
    bool ok;
} InterpSyncProcScan;

static AstFuncNode* interp_static_proc_binding(NameEntry* entry, int depth) {
    if (!entry || entry->is_mutable || entry->import || depth >= 16) return NULL;
    AstNode* declaration = entry->node;
    if (!declaration) return NULL;
    if (declaration->node_type == AST_NODE_PROC) return (AstFuncNode*)declaration;
    if (declaration->node_type != AST_NODE_VARIABLE_DECLARATOR) return NULL;
    AstNode* value = ast_unwrap_primary(((AstDeclaratorNode*)declaration)->init);
    if (!value) return NULL;
    if (value->node_type == AST_NODE_PROC) return (AstFuncNode*)value;
    if (value->node_type != AST_NODE_IDENT) return NULL;
    return interp_static_proc_binding(((AstIdentNode*)value)->entry, depth + 1);
}

static bool interp_proc_body_is_synchronous(AstFuncNode* fn,
        InterpSyncProcScan* scan);

typedef struct SatelliteScanCtx {
    bool ok;
} SatelliteScanCtx;

static void interp_scan_satellite_node(AstNode* node, void* opaque);

static void interp_sync_proc_visit(AstNode* node, void* ctx) {
    InterpSyncProcScan* scan = (InterpSyncProcScan*)ctx;
    if (!scan || !scan->ok || !node) return;
    // Nested definitions run only when a call below resolves to them, so an
    // unrelated async declaration cannot pin its synchronous owner to the JIT.
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) return;
    if (node->node_type == AST_NODE_START) {
        scan->ok = false;
        return;
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee_expr = ast_unwrap_primary(call->function);
        if (callee_expr && callee_expr->node_type == AST_NODE_SYS_FUNC) {
            SysFuncInfo* info = ((AstSysFuncNode*)callee_expr)->fn_info;
            if (info && info->is_async) {
                scan->ok = false;
                return;
            }
        } else if (callee_expr && callee_expr->type &&
                callee_expr->type->type_id == LMD_TYPE_FUNC &&
                ((TypeFunc*)callee_expr->type)->is_proc) {
            AstFuncNode* callee = ast_direct_call_function(call);
            if (!callee && callee_expr->node_type == AST_NODE_IDENT) {
                callee = interp_static_proc_binding(
                    ((AstIdentNode*)callee_expr)->entry, 0);
            }
            if (!callee || !interp_proc_body_is_synchronous(callee, scan)) {
                scan->ok = false;
                return;
            }
        }
    }
    interp_visit_children(node, interp_sync_proc_visit, scan);
}

static bool interp_proc_body_is_synchronous(AstFuncNode* fn,
        InterpSyncProcScan* scan) {
    if (!fn || !fn->body || !scan || !scan->ok || fn->is_async || fn->is_generator) {
        return false;
    }
    for (int index = 0; index < scan->active_count; index++) {
        if (scan->active[index] == fn) return true;
    }
    int active_capacity = (int)(sizeof(scan->active) / sizeof(scan->active[0]));
    if (scan->active_count >= active_capacity) return false;
    scan->active[scan->active_count++] = fn;
    interp_sync_proc_visit(fn->body, scan);
    scan->active_count--;
    return scan->ok;
}

// Async procedures are executed by their generated MIR resumable entry, not by
// the synchronous AST walker. Keep the same satellite structural boundary for
// that delegated body so captures, nested definitions, member-method layouts,
// and unsupported module bindings cannot cross the membrane accidentally.
static bool interp_async_proc_satellite_supported(AstFuncNode* fn) {
    if (!fn || !fn->body || fn->captures || fn->is_generator) return false;
    SatelliteScanCtx scan = {true};
    interp_scan_satellite_node(fn->body, &scan);
    return scan.ok;
}

// A delegated async body can contain `start(p)` edges that the outer T0 scan
// deliberately does not descend into. Mark those direct procedure targets so
// their values are published as boxed task entries before the generated body
// launches them; the flag is already the native analysis fact for that ABI.
static void interp_mark_task_entry(AstNode* node, void* opaque) {
    (void)opaque;
    if (!node) return;
    if (node->node_type == AST_NODE_START) {
        AstStartNode* start = (AstStartNode*)node;
        AstFuncNode* target = start->call
            ? ast_direct_call_function(start->call) : NULL;
        if (target && target->analysis) target->analysis->needs_task_context = true;
    }
    interp_visit_children(node, interp_mark_task_entry, NULL);
}

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
                member->object->type->type_id == LMD_TYPE_OBJECT
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
            if (!ast_direct_call_var_parameter_entries(call, signature, borrowed)) {
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
            TypeFunc* dynamic_signature = callee && callee->type &&
                    callee->type->type_id == LMD_TYPE_FUNC
                ? (TypeFunc*)callee->type : NULL;
            if (ast_type_func_has_var_parameter(dynamic_signature)) {
                // lambda_dynamic_call intentionally rejects mutable borrows;
                // retain whole-script fallback before that ABI boundary.
                sc->ok = false;
                sc->reject = AST_NODE_CALL_EXPR;
                return;
            }
        }
    }
    // AI11/AI12: task-backed definitions bypass T0 until their resumable frame
    // exists. The native pass is conservative for indirect pn calls, so admit
    // one only after the local immutable-alias proof above finds no task edge.
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) {
        AstFuncNode* fn = (AstFuncNode*)node;
        InterpSyncProcScan sync_scan = {.ok = true};
        bool task_backed = fn->analysis && (fn->analysis->may_await ||
            fn->analysis->needs_task_context);
        bool async_satellite = task_backed &&
            interp_async_proc_satellite_supported(fn);
        if (task_backed && async_satellite) {
            interp_visit_children(fn->body, interp_mark_task_entry, NULL);
        }
        if (fn->is_generator ||
                (task_backed && !async_satellite &&
                 !interp_proc_body_is_synchronous(fn, &sync_scan))) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        if (task_backed && async_satellite) return;
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
        // A mask or slice (`arr[arr gt 25] = 0`) is not a scalar index. A
        // The shared COW setters dispatch by the runtime owner layout. A
        // direct binding to a markup-derived Element can therefore use the
        // same map/array replacement path as a literal without assuming its
        // input-pool allocation or field representation.
        AstNode* key_expr = ast_unwrap_primary(ca->key);
        NameEntry* dynamic_index_entry = key_expr &&
                key_expr->node_type == AST_NODE_IDENT
            ? ((AstIdentNode*)key_expr)->entry : NULL;
        // A direct untyped non-loop binding reaches the same runtime int64
        // conversion in T0 and MIR. Loop values stay with the range proof
        // below: `to` also produces character ranges, so admitting an
        // AST_NODE_FOR_CLAUSE here would turn a character key into an int index.
        // Derived expressions remain outside this bridge to keep mask/slice
        // keys from entering the scalar COW setter.
        bool direct_untyped_binding_index = dynamic_index_entry &&
            !dynamic_index_entry->declared_type &&
            (!dynamic_index_entry->node ||
             dynamic_index_entry->node->node_type != AST_NODE_FOR_CLAUSE);
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
            (ca->key && ca->key->type &&
             (ca->key->type->type_id == LMD_TYPE_INT ||
              ca->key->type->type_id == LMD_TYPE_INT64)) ||
            direct_untyped_binding_index || interp_range_loop_index_expr(ca->key);
        indexed_key = indexed_key || direct_numeric_mask_assignment;
        bool open_any_array = entry &&
            ast_declared_type_is_open_any_array(entry->declared_type);
        bool open_item_binding = entry &&
            ast_declared_type_is_open_item(entry->declared_type);
        bool typed_map_root = entry && ast_declared_type_is_map(entry->declared_type);
        Type* typed_array_element = entry
            ? ast_declared_array_element(entry->declared_type) : NULL;
        LaneStorageDesc typed_array_lane = {};
        bool nullable_native_typed_array = typed_array_element &&
            lambda_type_lane_storage_desc(typed_array_element, &typed_array_lane) &&
            typed_array_lane.nullable &&
            (typed_array_lane.kind == LANE_STORAGE_POINTER ||
             typed_array_lane.kind == LANE_STORAGE_INT ||
             typed_array_lane.kind == LANE_STORAGE_BOOL ||
             typed_array_lane.kind == LANE_STORAGE_FLOAT64 ||
             typed_array_lane.kind == LANE_STORAGE_ITEM ||
             typed_array_lane.kind == LANE_STORAGE_SIZED_I64);
        bool direct_typed_array = path.count == 0 && typed_array_element &&
            (typed_array_element->type_id == LMD_TYPE_NUM_SIZED ||
             typed_array_element->type_id == LMD_TYPE_BOOL ||
             typed_array_element->type_id == LMD_TYPE_INT ||
             typed_array_element->type_id == LMD_TYPE_INT64 ||
             typed_array_element->type_id == LMD_TYPE_UINT64 ||
             typed_array_element->type_id == LMD_TYPE_FLOAT ||
             nullable_native_typed_array);
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
            interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        // `print` is the one Lambda-variadic entry the walker implements
        // directly (one pn_print per argument, as lowering emits); the other
        // variadic rows still have no generic dispatch to mirror.
        if (info && info->fn == SYSPROC_PRINT && info->func_ptr) {
            interp_visit_children(node, interp_scan_visit, ctx);
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
            interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        if (info && info->fn == SYSPROC_VMAP_SET) {
            interp_visit_children(node, interp_scan_visit, ctx);
            return;
        }
        if (!info || !info->func_ptr || info->c_arg_conv != C_ARG_ITEM ||
                info->arg_count < 0 || info->arg_count > 4) {
            log_debug("interp: sys func '%s' unsupported (arity=%d conv=%d ptr=%p)",
                info && info->name ? info->name : "<null>",
                info ? info->arg_count : -99, info ? (int)info->c_arg_conv : -1,
                info ? (void*)info->func_ptr : NULL);
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    interp_visit_children(node, interp_scan_visit, ctx);
}

bool interp_scan_supported(Script* script, AstNodeType* reject) {
    if (!script || !script->ast_root) return false;
    ScanCtx sc = {true, AST_NODE_NULL};
    interp_scan_visit(script->ast_root, &sc);
    if (reject) *reject = sc.reject;
    return sc.ok;
}

bool interp_named_sys_args_supported(const AstNode* callee) {
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC) return false;
    SysFuncInfo* info = ((const AstSysFuncNode*)callee)->fn_info;
    // A procedure can mutate its first Item. When that Item arrived from a
    // pipe, T0 has no direct binding to publish the required COW replacement.
    return info && !info->is_proc;
}

// ---------------------------------------------------------------------------
// Restricted evaluator modes (P3)
// ---------------------------------------------------------------------------

// A predicate has no user-code call edge.  This explicit list is deliberately
// smaller than the ordinary sysfunc surface: every row here is a value reader
// or scalar/text transform with no I/O, mutation, async, or callback path.
bool interp_eval_mode_allows_sys_func(EvalMode mode, const SysFuncInfo* info) {
    if (mode == EvalMode::RUNTIME) return true;
    if (!info || info->is_proc || !info->func_ptr || info->is_async) return false;
    switch (info->fn) {
    case SYSFUNC_LEN:
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

typedef struct InterpPredicateScan {
    bool ok;
} InterpPredicateScan;

static bool interp_predicate_node_supported(AstNode* node);

static void interp_predicate_scan_visit(AstNode* child, void* opaque) {
    InterpPredicateScan* scan = (InterpPredicateScan*)opaque;
    if (scan->ok && !interp_predicate_node_supported(child)) scan->ok = false;
}

static bool interp_predicate_children_supported(AstNode* node) {
    InterpPredicateScan scan = {true};
    interp_visit_children(node, interp_predicate_scan_visit, &scan);
    return scan.ok;
}

static bool interp_predicate_node_supported(AstNode* node) {
    if (!node) return false;
    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        AstNode* expr = ((AstPrimaryNode*)node)->expr;
        return !expr || interp_predicate_node_supported(expr);
    }
    case AST_NODE_LITERAL:
    case AST_NODE_CURRENT_ITEM:
    case AST_NODE_CURRENT_INDEX:
    case AST_NODE_TYPE:
        return true;
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)node)->op;
        return (op == OPERATOR_NOT || op == OPERATOR_NEG || op == OPERATOR_POS) &&
            interp_predicate_children_supported(node);
    }
    case AST_NODE_BINARY: {
        Operator op = ((AstBinaryNode*)node)->op;
        switch (op) {
        case OPERATOR_ADD: case OPERATOR_SUB: case OPERATOR_MUL:
        case OPERATOR_DIV: case OPERATOR_IDIV: case OPERATOR_MOD: case OPERATOR_POW:
        case OPERATOR_JOIN: case OPERATOR_AND: case OPERATOR_OR:
        case OPERATOR_EQ: case OPERATOR_NE: case OPERATOR_LT: case OPERATOR_LE:
        case OPERATOR_GT: case OPERATOR_GE: case OPERATOR_TO:
        case OPERATOR_IS: case OPERATOR_IS_NAN: case OPERATOR_IN: case OPERATOR_AT:
            return interp_predicate_children_supported(node);
        default:
            return false;
        }
    }
    case AST_NODE_IF_EXPR:
    case AST_NODE_INDEX_EXPR:
        return interp_predicate_children_supported(node);
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* field = (AstFieldNode*)node;
        // A dotted name is a compile-time key, not a binding read. Dynamic
        // member keys remain admissible only when their expression is pure.
        return interp_predicate_node_supported(field->object) &&
            (!field->field || field->field->node_type == AST_NODE_IDENT ||
                interp_predicate_node_supported(field->field));
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee = ast_unwrap_primary(call->function);
        if (!callee || callee->node_type != AST_NODE_SYS_FUNC ||
                !interp_eval_mode_allows_sys_func(EvalMode::PREDICATE,
                    ((AstSysFuncNode*)callee)->fn_info)) return false;
        for (AstNode* arg = call->argument; arg; arg = arg->next) {
            if (!interp_predicate_node_supported(arg)) return false;
        }
        return true;
    }
    default:
        // No identifiers, containers, assignments, lambdas/procedures,
        // handlers, pipes, loops, or arbitrary calls cross the predicate
        // boundary.  This keeps `that` independent of runtime effects (AI17).
        return false;
    }
}

bool interp_predicate_supported(AstNode* predicate) {
    return interp_predicate_node_supported(predicate);
}

// ---------------------------------------------------------------------------
// Slot assignment
// ---------------------------------------------------------------------------

typedef struct PlanCtx {
    Script* script;
    FnFramePlan* plan;        // plan currently being filled
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

static void plan_assign_entry(PlanCtx* pc, NameEntry* entry) {
    if (!entry || entry->storage_assigned) return;
    plan_mark_cow_owned(entry);
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
        if (e->import) {
            // Imported names consume no slot in this module's slab.
            plan_resolve_import(e);
            continue;
        }
        plan_assign_entry(pc, e);
    }
}

// Scratch need: the maximum number of Items that must stay live in frame slots
// across a child evaluation or a MAY_GC helper call. A property of expression
// shape, never of data size — container literals accumulate through one rooted
// builder, so they cost 1 regardless of element count.
static uint32_t plan_need(AstNode* node);

// Pattern testing is not ordinary expression evaluation: a non-constrained
// leaf materializes its value and keeps that value live while fn_is/fn_eq runs,
// whereas a constrained leaf owns three occurrence homes for its predicate.
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
    if (pattern->node_type == AST_NODE_CONSTRAINED_TYPE) {
        AstConstrainedTypeNode* constrained = (AstConstrainedTypeNode*)pattern;
        return 3 + plan_need(constrained->constraint);
    }
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
        // Arguments are rooted in their own RootSpan (the same shape
        // lambda_dynamic_call uses); only the callee occupies a frame slot.
        AstCallNode* c = (AstCallNode*)node;
        uint32_t fn = plan_need(c->function);
        uint32_t args = 1 + plan_need_max_siblings(c->argument);
        return fn > args ? fn : args;
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
        // A constrained pattern holds the current occurrence plus index,
        // parent, and root while its predicate walks, all before any helper.
        return 3 + plan_need(((AstConstrainedTypeNode*)node)->constraint);
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
    case AST_NODE_MAP:
        return 1 + plan_need_max_siblings(((AstMapNode*)node)->item);
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
    case AST_NODE_KEY_EXPR:
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

static void plan_handler(PlanCtx* outer, AstEventHandler* handler) {
    if (!outer || !handler || handler->interp_planned) return;

    PlanCtx pc = {};
    pc.script = outer->script;
    pc.plan = &handler->interp_plan;
    pc.storage = BINDING_STORAGE_REGISTER;
    // The event parameter is overlaid by the view activation; its slot is
    // harmless but keeps all handler-scope names consistently addressable.
    plan_assign_scope(&pc, handler->vars);
    plan_walk(handler->body, &pc);

    // View-handler activation publishes the matched model in one additional
    // frame home before evaluating the body (D5.3.3). Include that home in the
    // handler plan or the first callee expression can exhaust the scratch
    // window while the model is still live.
    uint32_t body_need = 1 + plan_need(handler->body);
    if (body_need > pc.max_scratch) pc.max_scratch = body_need;
    plan_finish(&pc);
    if (pc.failed) {
        outer->failed = true;
        return;
    }
    handler->interp_planned = true;
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
        if (!call->propagate && is_recursive_call(call, fn)) {
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

// Enter a nested function definition: a fresh plan, a fresh slot space.
static void plan_function(PlanCtx* outer, AstFuncNode* fn) {
    if (!fn || !fn->body) return;
    if (!plan_ensure_analysis(outer, fn)) { outer->failed = true; return; }
    if (fn->analysis->frame_plan.planned) return;

    PlanCtx pc = {0};
    pc.script = outer->script;
    pc.plan = &fn->analysis->frame_plan;
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

    plan_walk(fn->body, &pc);
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
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_ARROW_FUNC:
        // A definition's own name binds in the enclosing scope; its body opens
        // a separate activation, so it never consumes enclosing slots.
        plan_function(pc, (AstFuncNode*)node);
        return;
    case AST_NODE_EVENT_HANDLER:
        plan_handler(pc, (AstEventHandler*)node);
        return;
    case AST_NODE_VARIABLE_DECLARATOR:
        plan_assign_entry(pc, ((AstDeclaratorNode*)node)->entry);
        break;
    case AST_NODE_PARAM:
    case AST_NODE_KEY_EXPR:
        plan_assign_entry(pc, ((AstNamedNode*)node)->entry);
        break;
    case AST_NODE_LIST:
    case AST_NODE_CONTENT:
        plan_assign_scope(pc, ((AstListNode*)node)->vars);
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

// Satellites reuse T0's planned module slab. Task-backed procedures carry the
// resumable task ABI, while the bounded synchronous path admits only reads and
// stable imports; captures, nested definitions, generators, and replacement
// writes remain on T0 (D8.1.1v2 §5.2-§5.3).
bool interp_satellite_import_supported(const NameEntry* entry) {
    // Function-local imported names are not part of the module-global scope
    // walk. Rebind that view lazily to the already-planned export slot before
    // the satellite membrane checks its ABI (D8.1.1v2 / D7.2.1).
    if (entry && entry->import && !entry->import_owner && entry->import->script &&
            !entry->import->is_cross_lang && entry->node) {
        NameEntry* mutable_entry = (NameEntry*)entry;
        AstScript* owner_root = (AstScript*)entry->import->script->ast_root;
        for (NameEntry* exported = owner_root && owner_root->global_vars
                ? owner_root->global_vars->first : NULL;
                exported; exported = exported->next) {
            if (exported->node != entry->node || !exported->storage_assigned) continue;
            mutable_entry->slot = exported->slot;
            mutable_entry->binding_storage = exported->binding_storage;
            mutable_entry->import_owner = entry->import->script;
            mutable_entry->storage_assigned = true;
            break;
        }
    }
    if (!entry || !entry->import || entry->import->is_cross_lang ||
            !entry->import_owner || !entry->storage_assigned ||
            entry->binding_storage != BINDING_STORAGE_MODULE || entry->slot < 0) {
        return false;
    }
    // The target module must already be a planned T0 module; the satellite
    // embeds this stable module id and slot rather than asking MIR to link a
    // missing generated import symbol.
    bool supported = entry->import_owner->interp_supported &&
        entry->import_owner->interp_planned;
    return supported;
}

static void interp_scan_satellite_node(AstNode* node, void* opaque) {
    SatelliteScanCtx* sc = (SatelliteScanCtx*)opaque;
    if (!node || !sc->ok) return;

    switch (node->node_type) {
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_ARROW_FUNC:
        // Nested definitions need an explicit cross-satellite closure contract.
        sc->ok = false;
        return;
    case AST_NODE_MEMBER_ASSIGN_STAM:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_ASSIGN_STAM:
    case AST_NODE_VAR_STAM:
    case AST_NODE_OBJECT_TYPE:
    case AST_NODE_VIEW:
        // Procedural writes (including a local var) and indexed/member stores
        // need the T0 frame's replacement channel. A satellite has no safe
        // publication path for those roots (D3.3.1 / D5.2).
        sc->ok = false;
        return;
    case AST_NODE_MATCH_EXPR:
        // Pattern arms carry compiled regex/type-list state that is owned by
        // the T0 module activation. A satellite has no equivalent pattern
        // image, so keep the whole match expression in T0 (D5.2).
        sc->ok = false;
        return;
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* callee = ast_unwrap_primary(call->function);
        AstFuncNode* direct = ast_direct_call_function(call);
        TypeFunc* signature = direct && ((AstNode*)direct)->type &&
                ((AstNode*)direct)->type->type_id == LMD_TYPE_FUNC
            ? (TypeFunc*)((AstNode*)direct)->type : NULL;
        if (signature && ast_type_func_has_var_parameter(signature)) {
            // Even an exact direct call carries borrowed roots; the satellite
            // ABI cannot preserve the caller's var write-back slots.
            sc->ok = false;
            return;
        }
        if (callee && callee->node_type != AST_NODE_SYS_FUNC && !direct) {
            // A satellite cannot prove the target ABI for an indirect Lambda
            // call. An `any` callee may resolve to a `var` procedure after
            // promotion, but the boxed dispatcher has no caller-root
            // write-back channel (D3.3.1 / D5.2).
            sc->ok = false;
            return;
        }
        if (callee && callee->node_type == AST_NODE_IDENT && !direct) {
            NameEntry* entry = ((AstIdentNode*)callee)->entry;
            AstNode* binding = entry ? entry->node : NULL;
            bool local_dynamic = entry && !entry->import && binding &&
                binding->node_type != AST_NODE_FUNC &&
                binding->node_type != AST_NODE_FUNC_EXPR &&
                binding->node_type != AST_NODE_PROC;
            if (local_dynamic) {
                // The boxed dispatcher deliberately has no mutable-borrow
                // channel for an indirect local target. Pin this caller so a
                // var-parameter edge cannot be deferred at runtime (D3.3.1).
                sc->ok = false;
                return;
            }
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
                !interp_satellite_import_supported(entry)) {
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
    if (!fn || !fn->analysis || !fn->body || fn->captures || fn->is_generator) {
        return false;
    }
    bool task_backed = fn->node_type == AST_NODE_PROC &&
        (fn->analysis->may_await || fn->analysis->needs_task_context);
    if (task_backed) return interp_async_proc_satellite_supported((AstFuncNode*)fn);
    if (fn->is_async || fn->analysis->may_await || fn->analysis->needs_task_context) {
        return false;
    }
    TypeFunc* signature = (TypeFunc*)((AstNode*)fn)->type;
    if (ast_type_func_has_var_parameter(signature)) {
        // T0's direct-borrow path publishes a replacement into its active
        // caller frame. A satellite has only the boxed dynamic ABI, which
        // deliberately has no mutable-borrow write-back channel.
        return false;
    }
    for (TypeParam* param = signature ? signature->param : NULL;
            param; param = param->next) {
        Type* contract = param->contract_type ? param->contract_type :
            (Type*)param;
        TypeId tid = contract ? contract->type_id : LMD_TYPE_ANY;
        bool structured_contract = contract && tid == LMD_TYPE_TYPE &&
            contract->kind != TYPE_KIND_SIMPLE;
        if (tid == LMD_TYPE_ANY || tid == LMD_TYPE_ARRAY ||
                tid == LMD_TYPE_ARRAY_NUM || tid == LMD_TYPE_MAP ||
                tid == LMD_TYPE_ELEMENT || tid == LMD_TYPE_OBJECT ||
                tid == LMD_TYPE_VMAP || structured_contract) {
            // Broad/aggregate parameters need the full interpreter's Item
            // contract. The satellite ABI's raw carrier specialization can
            // otherwise turn typed arrays, structured contracts, or map state
            // into a valid-looking but incorrect value (D2.2.2, D5.2).
            return false;
        }
    }
    // Keep the satellite admission gate aligned with the complete T0
    // capability scanner. The old satellite-only walk checked nested
    // definitions and imports but skipped system ABI, COW, index-shape, and
    // call-signature guards; complex promoted bodies then ran a MIR subset
    // that silently dropped layout/PDF/editor state (D8.1.1v4).
    ScanCtx full_scan = {true, AST_NODE_NULL};
    interp_scan_visit(fn->body, &full_scan);
    if (!full_scan.ok) return false;
    SatelliteScanCtx sc = {true};
    interp_scan_satellite_node((AstNode*)fn->body, &sc);
    return sc.ok;
}
