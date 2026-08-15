// Frame-plan pass for the T0 AST interpreter (AI5).
//
// Two products, both derived from one complete Lambda AST traversal:
//   1. `interp_plan_script` — assigns NameEntry slots + BindingStorage classes
//      and computes each function's static activation shape (FnFramePlan).
//   2. `interp_scan_supported` — the whole-AST pre-scan that decides whether a
//      Script can run under T0 at all, so an unsupported kind produces a
//      counted whole-module fallback instead of a silent wrong answer (R4).
//
// The traversal is interpreter-owned rather than `ast_visit_core_children`:
// that visitor is the core cross-language contract feeding AstIndex, and it
// stops at `default:` for most Lambda-only kinds (CONTENT, LET_STAM, ELEMENT,
// FOR_EXPR, …). Extending it would change AstIndex on the default path, which
// P0/P1 must leave untouched; `LangProfile::visit_ext_children` is the
// designated seam for unifying the two later (design §9).

#include "interp.hpp"
#include "safety_analyzer.hpp"
#include "../../lib/log.h"

// ---------------------------------------------------------------------------
// Complete Lambda child traversal
// ---------------------------------------------------------------------------

typedef void (*InterpChildFn)(AstNode* child, void* ctx);

// Visits every structural child edge of `node`, excluding `node->next` (the
// caller owns sibling iteration) and excluding NameEntry->node declaration
// links — the AST is a DAG and those links are reads, never evaluation edges.
static void interp_visit_children(AstNode* node, InterpChildFn visit, void* ctx) {
    if (!node || !visit) return;
#define V(field) do { AstNode* _c = (AstNode*)(field); if (_c) visit(_c, ctx); } while (0)
#define VLIST(field) do { \
        for (AstNode* _i = (AstNode*)(field); _i; _i = _i->next) visit(_i, ctx); \
    } while (0)
    switch (node->node_type) {
    case AST_SCRIPT:                VLIST(((AstScript*)node)->child); break;
    case AST_NODE_PRIMARY:          V(((AstPrimaryNode*)node)->expr); break;
    case AST_NODE_UNARY:            V(((AstUnaryNode*)node)->operand); break;
    case AST_NODE_SPREAD:           V(((AstSpreadNode*)node)->argument); break;
    case AST_NODE_BINARY:
    case AST_NODE_PIPE:
        V(((AstBinaryNode*)node)->left);
        V(((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_ASSIGN:
    case AST_NODE_KEY_EXPR:
    case AST_NODE_PARAM:
    case AST_NODE_NAMED_ARG:
    case AST_NODE_STRING_PATTERN:
    case AST_NODE_SYMBOL_PATTERN:
        V(((AstNamedNode*)node)->as);
        break;
    case AST_NODE_DECOMPOSE:        V(((AstDecomposeNode*)node)->as); break;
    case AST_NODE_CALL_EXPR:
    case AST_NODE_NEW_EXPR:
        V(((AstCallNode*)node)->function);
        VLIST(((AstCallNode*)node)->argument);
        break;
    case AST_NODE_MEMBER_EXPR:
    case AST_NODE_INDEX_EXPR:
        V(((AstFieldNode*)node)->object);
        V(((AstFieldNode*)node)->field);
        break;
    case AST_NODE_IF_EXPR:
    case AST_NODE_CONDITIONAL_EXPR:
        V(((AstIfNode*)node)->cond);
        V(((AstIfNode*)node)->then);
        V(((AstIfNode*)node)->otherwise);
        break;
    case AST_NODE_ARRAY:
    case AST_NODE_SEQ:
    case AST_NODE_CONTENT:
    case AST_NODE_CONTENT_TYPE:
        VLIST(((AstArrayNode*)node)->item);
        break;
    case AST_NODE_LIST:
        // A list block keeps its `(let x = …, body)` bindings in `declare`,
        // separate from its value items.
        VLIST(((AstListNode*)node)->declare);
        VLIST(((AstListNode*)node)->item);
        break;
    case AST_NODE_MAP:
    case AST_NODE_OBJECT_LITERAL:
        VLIST(((AstMapNode*)node)->item);
        break;
    case AST_NODE_ELEMENT:
        VLIST(((AstElementNode*)node)->item);
        VLIST(((AstElementNode*)node)->content);
        break;
    case AST_NODE_MATCH_EXPR:
        V(((AstMatchNode*)node)->scrutinee);
        VLIST(((AstMatchNode*)node)->first_arm);
        break;
    case AST_NODE_MATCH_ARM:
        V(((AstMatchArm*)node)->pattern);
        V(((AstMatchArm*)node)->body);
        break;
    case AST_NODE_BLOCK:            VLIST(((AstBlockNode*)node)->statements); break;
    case AST_NODE_EXPR_STMT:        V(((AstExprStmtNode*)node)->expression); break;
    case AST_NODE_LET_STAM:
    case AST_NODE_PUB_STAM:
    case AST_NODE_VAR_STAM:
    case AST_NODE_TYPE_STAM:
        VLIST(((AstLetNode*)node)->declare);
        break;
    case AST_NODE_WHILE_STAM:
    case AST_NODE_DO_WHILE_STAM:
        V(((AstWhileNode*)node)->cond);
        V(((AstWhileNode*)node)->body);
        break;
    case AST_NODE_FOR_STAM:
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
    case AST_NODE_LOOP: {
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
    case AST_NODE_RETURN_STAM:
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
        V(((AstReturnNode*)node)->value);
        break;
    case AST_NODE_ASSIGN_STAM:
        V(((AstAssignStamNode*)node)->value);
        break;
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
        V(((AstCompoundAssignNode*)node)->object);
        V(((AstCompoundAssignNode*)node)->key);
        V(((AstCompoundAssignNode*)node)->value);
        break;
    case AST_NODE_PIPE_FILE_STAM:
        V(((AstBinaryNode*)node)->left);
        V(((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_HANDLER_EXPR:
    case AST_NODE_HANDLER_STAM:
        V(((AstHandlerNode*)node)->operand);
        V(((AstHandlerNode*)node)->body);
        break;
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC:
    case AST_NODE_ARROW_FUNC:
        VLIST(((AstFuncNode*)node)->params);
        V(((AstFuncNode*)node)->body);
        break;
    case AST_NODE_START:            V(((AstStartNode*)node)->call); break;
    case AST_NODE_PATH_INDEX_EXPR:
        V(((AstPathIndexNode*)node)->base_path);
        V(((AstPathIndexNode*)node)->segment_expr);
        break;
    case AST_NODE_PARENT_EXPR:      V(((AstParentNode*)node)->object); break;
    case AST_NODE_QUERY_EXPR:       V(((AstQueryNode*)node)->object); break;
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
    case AST_NODE_BINARY_TYPE:
        V(((AstBinaryNode*)node)->left);
        V(((AstBinaryNode*)node)->right);
        break;
    case AST_NODE_UNARY_TYPE:       V(((AstUnaryNode*)node)->operand); break;
    case AST_NODE_LIST_TYPE:
    case AST_NODE_ARRAY_TYPE:       VLIST(((AstArrayNode*)node)->item); break;
    case AST_NODE_MAP_TYPE:
    case AST_NODE_ELMT_TYPE:        VLIST(((AstMapNode*)node)->item); break;
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
    case AST_NODE_IMPORT:           VLIST(((AstImportNode*)node)->specifiers); break;
    default:
        // Leaves: IDENT, LITERAL, SYS_FUNC, TYPE, CURRENT_*, BREAK/CONTINUE,
        // PATH_EXPR, PATTERN_CHAR_CLASS, IMPORT_SPECIFIER.
        break;
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
    case AST_NODE_ASSIGN:
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
    case AST_NODE_WHILE_STAM:
    case AST_NODE_DO_WHILE_STAM:
    case AST_NODE_BREAK_STAM:
    case AST_NODE_CONTINUE_STAM:
    case AST_NODE_RETURN_STAM:
    case AST_NODE_RAISE_STAM:
    case AST_NODE_RAISE_EXPR:
    case AST_NODE_INDEX_ASSIGN_STAM:
    case AST_NODE_MEMBER_ASSIGN_STAM:
    // --- P1.1: comprehensions ---
    case AST_NODE_FOR_EXPR:
    case AST_NODE_FOR_STAM:
    case AST_NODE_LOOP:
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
    K(AST_NODE_ASSIGN) K(AST_NODE_CALL_EXPR) K(AST_NODE_MEMBER_EXPR)
    K(AST_NODE_INDEX_EXPR) K(AST_NODE_IF_EXPR) K(AST_NODE_ARRAY) K(AST_NODE_MAP)
    K(AST_NODE_KEY_EXPR) K(AST_NODE_MATCH_EXPR) K(AST_NODE_MATCH_ARM)
    K(AST_NODE_SEQ) K(AST_NODE_LIST) K(AST_NODE_BLOCK) K(AST_NODE_PARAM)
    K(AST_NODE_FOR_STAM) K(AST_NODE_WHILE_STAM) K(AST_NODE_BREAK_STAM)
    K(AST_NODE_CONTINUE_STAM) K(AST_NODE_RETURN_STAM) K(AST_NODE_RAISE_STAM)
    K(AST_NODE_RAISE_EXPR) K(AST_NODE_VAR_STAM) K(AST_NODE_ASSIGN_STAM)
    K(AST_NODE_LET_STAM) K(AST_NODE_PUB_STAM) K(AST_NODE_IMPORT)
    K(AST_NODE_DO_WHILE_STAM) K(AST_NODE_FUNC) K(AST_NODE_FUNC_EXPR)
    K(AST_NODE_PROC) K(AST_NODE_PIPE) K(AST_NODE_CURRENT_ITEM)
    K(AST_NODE_CURRENT_INDEX) K(AST_NODE_LAST_INDEX) K(AST_NODE_CONTENT)
    K(AST_NODE_ELEMENT) K(AST_NODE_DECOMPOSE) K(AST_NODE_LOOP)
    K(AST_NODE_ORDER_SPEC) K(AST_NODE_GROUP_CLAUSE) K(AST_NODE_JOIN_KEY)
    K(AST_NODE_FOR_EXPR) K(AST_NODE_INDEX_ASSIGN_STAM) K(AST_NODE_MEMBER_ASSIGN_STAM)
    K(AST_NODE_PIPE_FILE_STAM) K(AST_NODE_TYPE_STAM) K(AST_NODE_PATH_EXPR)
    K(AST_NODE_PATH_INDEX_EXPR) K(AST_NODE_PARENT_EXPR) K(AST_NODE_QUERY_EXPR)
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

static void interp_scan_visit(AstNode* node, void* ctx) {
    ScanCtx* sc = (ScanCtx*)ctx;
    if (!sc->ok || !node) return;
    if (!interp_kind_supported(node->node_type)) {
        sc->ok = false;
        sc->reject = node->node_type;
        return;
    }
    // AI11/AI12: suspension-capable and view-bearing definitions bypass T0
    // entirely until P2's first-call satellite compile exists.
    if (node->node_type == AST_NODE_FUNC || node->node_type == AST_NODE_FUNC_EXPR ||
            node->node_type == AST_NODE_PROC) {
        AstFuncNode* fn = (AstFuncNode*)node;
        if (fn->is_async || fn->is_generator ||
                (fn->analysis && (fn->analysis->may_await ||
                                  fn->analysis->needs_task_context))) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        // Lowering turns a self-tail-call into a loop, so a tail-recursive
        // definition never grows the native stack there. The walker recurses
        // per call and would fault where the JIT completes — self-tail-call
        // iteration is P1.4 (AIO1/R8), so these stay on JIT until then.
        if (should_use_tco(fn)) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        // A variadic definition binds its rest arguments to `varg()`, which the
        // generated wrapper installs from a trailing physical parameter. The
        // walker has no vararg context yet, so those definitions stay on JIT.
        TypeFunc* signature = (TypeFunc*)node->type;
        if (signature && signature->type_id == LMD_TYPE_FUNC && signature->is_variadic) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // N-D numeric literals become one shaped ArrayNum via array_num_new_ndim;
    // building them the generic way would diverge silently, so the whole
    // script falls back instead.
    if (node->node_type == AST_NODE_ARRAY) {
        TypeArray* arr_type = (TypeArray*)node->type;
        if (arr_type && arr_type->nested &&
                (arr_type->nested->type_id == LMD_TYPE_UINT64 ||
                 arr_type->nested->type_id == LMD_TYPE_NUM_SIZED)) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        for (AstNode* item = ((AstArrayNode*)node)->item; item; item = item->next) {
            AstNode* value = item;
            while (value && value->node_type == AST_NODE_PRIMARY &&
                    ((AstPrimaryNode*)value)->expr) {
                value = ((AstPrimaryNode*)value)->expr;
            }
            if (!value) continue;
            if (value->node_type == AST_NODE_ARRAY) {
                sc->ok = false;
                sc->reject = node->node_type;
                return;
            }
        }
    }
    // A compound assignment through a nested path (`a.b.c = v`) has its own
    // COW path-set lowering; only a plain binding root is covered here.
    if (node->node_type == AST_NODE_INDEX_ASSIGN_STAM ||
            node->node_type == AST_NODE_MEMBER_ASSIGN_STAM) {
        AstNode* target = ((AstCompoundAssignNode*)node)->object;
        while (target && target->node_type == AST_NODE_PRIMARY &&
                ((AstPrimaryNode*)target)->expr) {
            target = ((AstPrimaryNode*)target)->expr;
        }
        if (!target || target->node_type != AST_NODE_IDENT ||
                !((AstIdentNode*)target)->entry) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // Comprehension clauses with their own lowering shapes — grouping tables,
    // sort keys, and equi-join tuple streams. The core iterate/where/emit path
    // is implemented; these clauses stay on the JIT until their slices land.
    if (node->node_type == AST_NODE_FOR_EXPR || node->node_type == AST_NODE_FOR_STAM) {
        AstForNode* fr = (AstForNode*)node;
        if (fr->group || fr->order || fr->limit || fr->offset) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        for (AstNode* l = fr->loop; l; l = l->next) {
            AstLoopNode* lp = (AstLoopNode*)l;
            if (lp->on || lp->join_keys || lp->optional) {
                sc->ok = false;
                sc->reject = AST_NODE_LOOP;
                return;
            }
        }
    }
    // `t[i, j]` carries a chain of index expressions for one N-D subscript;
    // the P0 walker evaluates a single index, so multi-axis reads fall back.
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstNode* index = ((AstFieldNode*)node)->field;
        if (index && index->next) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
    }
    // `{*:base, k: v}` carries the spread source as a bare, non-KEY_EXPR item;
    // its fields merge into the literal's shape rather than filling one slot,
    // which the P0 positional fill does not model.
    if (node->node_type == AST_NODE_MAP || node->node_type == AST_NODE_OBJECT_LITERAL) {
        for (AstNode* item = ((AstMapNode*)node)->item; item; item = item->next) {
            if (item->node_type != AST_NODE_KEY_EXPR) {
                sc->ok = false;
                sc->reject = node->node_type;
                return;
            }
        }
    }
    // A native-scalar type annotation on a binding is a coercion boundary:
    // lowering unboxes the initializer into the declared lane and reboxes it,
    // so `let pairs: float = 7 div 2` yields a float where the boxed walker
    // would keep the int. Declaration-boundary contracts are P1.4; until then
    // an annotated binding routes the whole script to the JIT.
    if (node->node_type == AST_NODE_ASSIGN) {
        // `let a^err = …` splits a value-or-error result across two bindings;
        // that is the error channel, which lands in P1.4.
        if (((AstNamedNode*)node)->error_name) {
            sc->ok = false;
            sc->reject = node->node_type;
            return;
        }
        Type* declared = ((AstNamedNode*)node)->declared_type;
        if (declared) {
            TypeId tid = declared->type_id;
            if (tid == LMD_TYPE_ARRAY && ((TypeArray*)declared)->nested) {
                tid = ((TypeArray*)declared)->nested->type_id;
            }
            switch (tid) {
            case LMD_TYPE_INT: case LMD_TYPE_INT64: case LMD_TYPE_UINT64:
            case LMD_TYPE_FLOAT: case LMD_TYPE_FLOAT64: case LMD_TYPE_NUM_SIZED:
            case LMD_TYPE_BOOL: case LMD_TYPE_DECIMAL:
                sc->ok = false;
                sc->reject = node->node_type;
                return;
            default:
                break;
            }
        }
    }
    // A system function whose entry takes native words (the bitwise/shift
    // family) or more arguments than the P0 dispatch table covers.
    if (node->node_type == AST_NODE_SYS_FUNC) {
        SysFuncInfo* info = ((AstSysFuncNode*)node)->fn_info;
        if (!info || !info->func_ptr || info->c_arg_conv != C_ARG_ITEM ||
                info->arg_count < 0 || info->arg_count > 5) {
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
    bool failed;
} PlanCtx;

static void plan_assign_entry(PlanCtx* pc, NameEntry* entry) {
    if (!entry || entry->storage_assigned) return;
    if (pc->next_slot > UINT16_MAX) { pc->failed = true; return; }
    entry->slot = (int32_t)pc->next_slot++;
    entry->binding_storage = pc->storage;
    entry->storage_assigned = true;
}

// Back-links a declaration node to its NameEntry so the walker can store an
// initializer without re-searching the scope. push_name leaves these unset for
// `let` and for every function/loop node whose AstNamedNode alias overlaps a
// real field; this pass is build-time, so filling them mutates no runtime state.
// build_ast leaves `analysis` NULL for definitions no lowering pass touched;
// T0 still needs somewhere to hang the plan and the declaration binding.
static FnAnalysis* plan_ensure_analysis(PlanCtx* pc, AstFuncNode* fn) {
    if (!fn) return NULL;
    if (!fn->analysis) {
        fn->analysis = (FnAnalysis*)pool_calloc(pc->script->pool, sizeof(FnAnalysis));
        if (!fn->analysis) pc->failed = true;
    }
    return fn->analysis;
}

static void plan_backlink_entry(PlanCtx* pc, NameEntry* entry) {
    AstNode* decl = entry ? entry->node : NULL;
    if (!decl) return;
    switch (decl->node_type) {
    case AST_NODE_ASSIGN:
    case AST_NODE_PARAM:
    case AST_NODE_KEY_EXPR: {
        AstNamedNode* named = (AstNamedNode*)decl;
        if (!named->entry) named->entry = entry;
        break;
    }
    case AST_NODE_FUNC:
    case AST_NODE_FUNC_EXPR:
    case AST_NODE_PROC: {
        FnAnalysis* analysis = plan_ensure_analysis(pc, (AstFuncNode*)decl);
        if (analysis && !analysis->decl_entry) analysis->decl_entry = entry;
        break;
    }
    default:
        break;
    }
}

// Captured names read through the closure env, not through a frame slot.
static void plan_assign_scope(PlanCtx* pc, NameScope* scope) {
    if (!scope) return;
    for (NameEntry* e = scope->first; e; e = e->next) {
        plan_assign_entry(pc, e);
        plan_backlink_entry(pc, e);
    }
}

// Scratch need: the maximum number of Items that must stay live in frame slots
// across a child evaluation or a MAY_GC helper call. A property of expression
// shape, never of data size — container literals accumulate through one rooted
// builder, so they cost 1 regardless of element count.
static uint32_t plan_need(AstNode* node);

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
        uint32_t r = 1 + plan_need(b->right);
        uint32_t at_call = 2;
        uint32_t best = l > r ? l : r;
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
    case AST_NODE_CALL_EXPR:
    case AST_NODE_NEW_EXPR: {
        // Arguments are rooted in their own RootSpan (the same shape
        // lambda_dynamic_call uses); only the callee occupies a frame slot.
        AstCallNode* c = (AstCallNode*)node;
        uint32_t fn = plan_need(c->function);
        uint32_t args = 1 + plan_need_max_siblings(c->argument);
        return fn > args ? fn : args;
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
    case AST_NODE_MAP:
    case AST_NODE_OBJECT_LITERAL:
        return 1 + plan_need_max_siblings(((AstMapNode*)node)->item);
    case AST_NODE_KEY_EXPR:
    case AST_NODE_ASSIGN:
    case AST_NODE_PARAM:
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

static void plan_finish(PlanCtx* pc) {
    FnFramePlan* plan = pc->plan;
    if (!plan) return;
    plan->param_count = (uint16_t)pc->param_count;
    uint32_t named = pc->next_slot;
    plan->local_count = (uint16_t)(named - pc->param_count);
    plan->scratch_depth = (uint16_t)pc->max_scratch;
    uint32_t total = named + 1 /* signal slot */ + pc->max_scratch;
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
        param_index++;
    }
    pc.param_count = (uint32_t)param_index;

    plan_walk(fn->body, &pc);
    uint32_t body_need = plan_need(fn->body);
    if (body_need > pc.max_scratch) pc.max_scratch = body_need;
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
    case AST_NODE_ASSIGN:
    case AST_NODE_PARAM:
    case AST_NODE_KEY_EXPR:
        plan_assign_entry(pc, ((AstNamedNode*)node)->entry);
        break;
    case AST_NODE_LIST:
    case AST_NODE_CONTENT:
        plan_assign_scope(pc, ((AstListNode*)node)->vars);
        break;
    case AST_NODE_FOR_STAM:
    case AST_NODE_FOR_EXPR:
        plan_assign_scope(pc, ((AstForNode*)node)->vars);
        break;
    case AST_NODE_WHILE_STAM:
    case AST_NODE_DO_WHILE_STAM:
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
    script->interp_plan.total_slots = (uint16_t)(1 + pc.max_scratch);
    script->interp_planned = true;

    log_info("frame-plan: script '%s' module_slots=%u scratch=%u total=%u",
        script->reference ? script->reference : "<none>",
        (unsigned)module_slots, (unsigned)pc.max_scratch,
        (unsigned)script->interp_plan.total_slots);
    return true;
}
