#include "mvp_lmd.h"
#include "mvp_lmd_runtime.h"
#include "../js_transpiler.hpp"
#include "../../runtime/mir_emitter_shared.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/lambda-error.h"
#include "../../mir/mir-gen.h"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"
#include "../../../lib/time_util.h"
#include <math.h>

enum LmdKind { K_UNDEFINED = 1, K_NULL = 2, K_BOOL = 4, K_NUMBER = 8,
    K_STRING = 16, K_ARRAY = 32, K_FUNCTION = 64, K_ANY = 127 };
struct LmdFunction;
struct LmdBinding {
    NameEntry* entry;
    LmdFunction* owner;
    LmdFunction* target;
    AstNode* initializer;
    uint32_t slot;
    unsigned kinds;
    bool assigned;
    bool observed;
    bool external;
    bool native;
    uint8_t intrinsic;
    bool dominated;
    MIR_reg_t reg;
    int scalar_home;
};
struct LmdFunction {
    AstFuncNode* ast;
    MIR_item_t item;
    MIR_item_t forward;
    char name[40];
    uint32_t id;
    void* address;
    bool closed_calls;
};
struct MvpLmdProgram {
    JsTranspiler* frontend;
    MIR_context_t mir;
    MIR_module_t module;
    ArrayList* functions;
    ArrayList* bindings;
    Item* slots;
    uint32_t slot_count;
    char diagnostic[192];
    bool linked;
    MirImportEntry entry;
    LmdBinding intrinsics[3];
    LmdBinding** intrinsic_uses;
    HashMap* import_cache;
};
struct MvpLmdExecution {
    MvpLmdProgram program;
    EvalContext context;
    Item result;
    bool active;
    bool registered;
};
struct LmdControl {
    LmdControl* parent;
    MIR_label_t stop;
    MIR_label_t next;
    const char* label;
    int label_len;
    AstNode* loop_target;
};
struct LmdCompiler {
    MvpLmdProgram* program;
    LmdFunction* function;
    MirEmitter em;
    MIR_reg_t unit;
    MIR_reg_t arguments;
    MIR_reg_t argc;
    MIR_reg_t self;
    MIR_reg_t argument_span;
    MIR_insn_t argument_fixup;
    int argument_count;
    LmdControl* control;
    bool strict;
};
struct LmdValue { MIR_reg_t reg; unsigned kind; bool literal = false; double number = 0; };
struct LmdReference { LmdBinding* binding; LmdValue owner; MIR_reg_t key;
    bool key_known = false; int64_t index = 0; };

static LmdBinding* binding(MvpLmdProgram* p, NameEntry* entry) {
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->entry == entry) return b;
    }
    return NULL;
}
static const char* intrinsic_names[] = {"undefined", "NaN", "Infinity"};
static LmdBinding* identifier_binding(MvpLmdProgram* p, AstIdentNode* id) {
    if (id->entry) return binding(p, id->entry);
    AstNodeId index = ast_index_find(&p->frontend->ast_index, id);
    return index < p->frontend->ast_index.count ? p->intrinsic_uses[index] : NULL;
}
static LmdFunction* function(MvpLmdProgram* p, AstNode* ast) {
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        if ((AstNode*)f->ast == ast) return f;
    }
    return NULL;
}
static void diagnostic(MvpLmdProgram* p, AstNode* n, const char* reason) {
    if (!p->diagnostic[0]) snprintf(p->diagnostic, sizeof(p->diagnostic),
        "MVP scope error at byte %u: %s", n ? n->source_span.start_byte : 0, reason);
}
static bool named(String* s, const char* name) {
    return s && s->len == strlen(name) && !memcmp(s->chars, name, s->len);
}
static void collect_scope(MvpLmdProgram* p, LmdFunction* owner, NameScope* scope) {
    for (NameEntry* e = scope ? scope->first : NULL; e; e = e->next) {
        if (binding(p, e)) continue;
        LmdBinding* b = (LmdBinding*)mem_calloc(1, sizeof(LmdBinding), MEM_CAT_TEMP);
        b->entry = e;
        b->owner = owner;
        if (!owner->id && scope == ((AstScript*)p->frontend->ast_root)->global_vars) {
            for (int i = 0; i < 3; i++) if (named(e->name, intrinsic_names[i])) {
                if (e->is_lexical || (e->node && e->node->node_type == AST_NODE_FUNC))
                    diagnostic(p, e->node, "SyntaxError: restricted global declaration");
                b->intrinsic = (uint8_t)i + 1;
                b->kinds = i ? K_NUMBER : K_UNDEFINED;
            }
        }
        if (!owner->id && !b->intrinsic) b->slot = p->slot_count++;
        if (e->is_parameter) b->kinds = K_ANY;
        arraylist_append(p->bindings, b);
    }
}
struct LmdWalk { MvpLmdProgram* p; LmdFunction* owner; AstNode* parent; bool facts; };
static void walk(AstNode* n, LmdWalk* state);
static void child(AstNode* n, void* context) { walk(n, (LmdWalk*)context); }
static void walk(AstNode* n, LmdWalk* state) {
    if (!n) return;
    MvpLmdProgram* p = state->p;
    LmdWalk inner = *state;
    inner.parent = n;
    switch (n->node_type) {
    case AST_SCRIPT:
        collect_scope(p, state->owner, ((AstScript*)n)->global_vars); break;
    case AST_NODE_BLOCK:
        collect_scope(p, state->owner, ((AstBlockNode*)n)->vars); break;
    case AST_NODE_LOOP:
        collect_scope(p, state->owner, ((AstLoopControlNode*)n)->vars); break;
    case AST_NODE_IF_EXPR:
        collect_scope(p, state->owner, ((JsIfNode*)n)->consequent_vars);
        collect_scope(p, state->owner, ((JsIfNode*)n)->alternate_vars); break;
    case AST_NODE_MATCH_EXPR:
        collect_scope(p, state->owner, ((JsSwitchNode*)n)->vars); break;
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: {
        AstFuncNode* ast = (AstFuncNode*)n;
        LmdFunction* f = function(p, n);
        if (!f) {
            f = (LmdFunction*)mem_calloc(1, sizeof(LmdFunction), MEM_CAT_TEMP);
            f->ast = ast;
            f->id = (uint32_t)p->functions->length;
            snprintf(f->name, sizeof(f->name), "mvp_lmd_f%u", f->id);
            arraylist_append(p->functions, f);
        }
        JsAstParameterFacts params = js_ast_collect_parameter_facts(ast->params);
        JsAstFunctionFacts facts = js_ast_collect_function_facts(ast->params, ast->body);
        if (ast->is_async || ast->is_generator || params.has_non_simple_params ||
                facts.observations || facts.has_direct_eval || facts.has_with)
            diagnostic(p, n, "function needs features outside scalar/dense-array scope");
        inner.owner = f;
        collect_scope(p, f, ast->vars);
        if (ast->vars && ast->vars->parent && ast->vars->parent->is_function_name_scope)
            collect_scope(p, f, ast->vars->parent);
        if (ast->entry) {
            LmdBinding* b = binding(p, ast->entry);
            if (b) {
                if (b->target && b->target != f) b->assigned = true;
                b->target = f;
                b->kinds |= K_FUNCTION;
                // the Annex B publication exposes this identity even without a local read.
                if (b->entry->annex_b_outer_binding) b->observed = true;
            }
        }
        break;
    }
    case AST_NODE_LITERAL:
        if (((AstLiteralNode*)n)->is_bigint) diagnostic(p, n, "BigInt"); break;
    case AST_NODE_VAR_STAM:
        if (((AstVarDeclNode*)n)->is_using) diagnostic(p, n, "using declaration"); break;
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* d = (AstDeclaratorNode*)n;
        if (!d->id || d->id->node_type != AST_NODE_IDENT) diagnostic(p, n, "binding pattern");
        else {
            LmdBinding* b = binding(p, ((AstIdentNode*)d->id)->entry);
            if (b && d->init) {
                if (b->initializer) b->assigned = true;
                else {
                    b->initializer = d->init;
                    AstNode* parent = state->parent;
                    AstNode* grand = ast_index_parent(&p->frontend->ast_index, parent);
                    // switch cases can enter after an initializer; lexical scope alone is no proof.
                    b->dominated = (grand && (grand->node_type == AST_SCRIPT ||
                            (grand->node_type == AST_NODE_BLOCK &&
                             (b->entry->is_lexical || (((AstBlockNode*)grand)->vars &&
                              ((AstBlockNode*)grand)->vars->is_function_body))))) ||
                        (grand && grand->node_type == AST_NODE_LOOP &&
                         ((AstLoopControlNode*)grand)->init == parent);
                }
            }
        }
        break;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        if (!state->facts && !id->entry) {
            for (int i = 0; i < 3; i++) if (named(id->name, intrinsic_names[i]))
                p->intrinsic_uses[ast_index_find(&p->frontend->ast_index, n)] = &p->intrinsics[i];
        }
        LmdBinding* b = identifier_binding(p, id);
        bool declaration_target = state->parent &&
            ((state->parent->node_type == AST_NODE_VARIABLE_DECLARATOR &&
              ((AstDeclaratorNode*)state->parent)->id == n) ||
             ((state->parent->node_type == AST_NODE_FUNC || state->parent->node_type == AST_NODE_FUNC_EXPR ||
               state->parent->node_type == AST_NODE_ARROW_FUNC) &&
              ((AstFuncNode*)state->parent)->body != n));
        if (state->facts && b && !declaration_target) {
            if (b->owner != state->owner && (b->owner->id ||
                    (b->entry && b->entry->scope != ((AstScript*)p->frontend->ast_root)->global_vars)))
                diagnostic(p, n, "capture of enclosing local");
            if (b->owner != state->owner) {
                // a function may run before a textually earlier program initializer.
                b->external = true; b->dominated = false;
            }
            if (!state->parent || state->parent->node_type != AST_NODE_CALL_EXPR ||
                    ((AstCallNode*)state->parent)->callee != n) b->observed = true;
            if (b->initializer && n->source_span.start_byte < b->initializer->source_span.end_byte)
                b->dominated = false;
        }
        break;
    }
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* field = (AstFieldNode*)n;
        if (field->optional) diagnostic(p, n, "optional member");
        walk(field->object, &inner);
        if (field->computed) walk(field->property, &inner);
        else if (!field->property || field->property->node_type != AST_NODE_IDENT ||
                 !named(((AstIdentNode*)field->property)->name, "length"))
            diagnostic(p, n, "named property other than length");
        return;
    }
    case AST_NODE_CALL_EXPR:
        if (((AstCallNode*)n)->optional) diagnostic(p, n, "optional call"); break;
    case AST_NODE_ASSIGN:
        if (((AstAssignNode*)n)->left && ((AstAssignNode*)n)->left->node_type == AST_NODE_IDENT) {
            LmdBinding* b = binding(p, ((AstIdentNode*)((AstAssignNode*)n)->left)->entry);
            if (b) b->assigned = true;
        }
        break;
    case AST_NODE_UNARY:
        if (((AstUnaryNode*)n)->op == OPERATOR_JS_DELETE)
            diagnostic(p, n, "delete"); break;
    case AST_NODE_BINARY:
        if (((AstBinaryNode*)n)->op == OPERATOR_JS_INSTANCEOF || ((AstBinaryNode*)n)->op == OPERATOR_IN)
            diagnostic(p, n, "object/prototype operator"); break;
    case AST_NODE_ARRAY:
        for (AstNode* e = ((AstArrayNode*)n)->item; e; e = e->next)
            if (e->node_type == AST_NODE_NULL) diagnostic(p, e, "sparse array literal");
        if (ast_linked_node_count(((AstArrayNode*)n)->item) != ((AstArrayNode*)n)->length)
            diagnostic(p, n, "sparse array literal"); break;
    case AST_NODE_NULL: case AST_NODE_EXPR_STMT: case AST_NODE_RETURN_STAM:
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_SEQ:
    case AST_NODE_CONDITIONAL_EXPR:
    case AST_NODE_MATCH_ARM:
        break;
    default:
        if (n->node_type != JS_AST_NODE_LABELED_STATEMENT)
            diagnostic(p, n, "syntax outside MVP scope");
    }
    js_ast_visit_children(n, child, &inner);
}

static Operator compound_operator(Operator op);
static unsigned binary_kind(Operator op, unsigned left, unsigned right) {
    if (op == OPERATOR_AND || op == OPERATOR_OR || op == OPERATOR_JS_NULLISH_COALESCE)
        return left | right;
    if (op == OPERATOR_ADD) return ((left | right) & K_STRING ? K_STRING : 0) |
        ((left & ~K_STRING) && (right & ~K_STRING) ? K_NUMBER : 0);
    MirNumericOpPlan plan;
    return em_numeric_op_plan(op, &plan) && plan.is_comparison ? K_BOOL : K_NUMBER;
}
static unsigned kind(MvpLmdProgram* p, AstNode* n) {
    if (!n) return K_UNDEFINED;
    switch (n->node_type) {
    case AST_NODE_LITERAL: {
        static const unsigned kinds[] = {K_NUMBER, K_STRING, K_BOOL, K_NULL, K_UNDEFINED};
        return kinds[((AstLiteralNode*)n)->literal_type];
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        LmdBinding* b = identifier_binding(p, id);
        if (b && b->intrinsic) return b->kinds;
        if (b) return b->kinds | (!b->dominated && !b->entry->is_lexical &&
            !b->entry->is_parameter && !b->target ? K_UNDEFINED : 0);
        return K_UNDEFINED;
    }
    case AST_NODE_ARRAY: return K_ARRAY;
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: return K_FUNCTION;
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)n)->op;
        return op == OPERATOR_NOT ? K_BOOL : op == OPERATOR_JS_TYPEOF ? K_STRING :
            op == OPERATOR_JS_VOID ? K_UNDEFINED : K_NUMBER;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        return binary_kind(b->op, kind(p, b->left), kind(p, b->right));
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        unsigned right = kind(p, a->right);
        if (a->op == OPERATOR_ASSIGN) return right;
        if (a->op == OPERATOR_JS_AND_ASSIGN || a->op == OPERATOR_JS_OR_ASSIGN ||
                a->op == OPERATOR_JS_NULLISH_ASSIGN) return kind(p, a->left) | right;
        return binary_kind(compound_operator(a->op), kind(p, a->left), right);
    }
    case AST_NODE_IF_EXPR: case AST_NODE_CONDITIONAL_EXPR:
        return kind(p, ((AstIfNode*)n)->then) | kind(p, ((AstIfNode*)n)->otherwise);
    case AST_NODE_SEQ: {
        AstNode* item = ((AstArrayNode*)n)->item;
        while (item && item->next) item = item->next;
        return kind(p, item);
    }
    default: return K_ANY;
    }
}
struct LmdTypes { MvpLmdProgram* p; bool changed; };
static void type_walk(AstNode* n, void* arg) {
    if (!n) return;
    LmdTypes* t = (LmdTypes*)arg;
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* callee = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(t->p, (AstIdentNode*)call->callee) : NULL;
        LmdFunction* f = callee ? callee->target : NULL;
        if (f && f->closed_calls) {
            AstNode* actual = call->arguments;
            for (AstNode* formal = f->ast->params; formal; formal = formal->next) {
                LmdBinding* b = binding(t->p, js_ast_parameter_binding_identifier(formal)->entry);
                unsigned incoming = actual ? kind(t->p, actual) : K_UNDEFINED;
                if ((b->kinds | incoming) != b->kinds) { b->kinds |= incoming; t->changed = true; }
                if (actual) actual = actual->next;
            }
        }
    }
    AstNode* target = NULL;
    unsigned mask = 0;
    if (n->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* d = (AstDeclaratorNode*)n;
        target = d->id;
        if (d->init) mask = kind(t->p, d->init);
    } else if (n->node_type == AST_NODE_ASSIGN) {
        AstAssignNode* a = (AstAssignNode*)n;
        target = a->left;
        mask = kind(t->p, n);
    } else if (n->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
             ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT)) {
        target = ((AstUnaryNode*)n)->operand;
        mask = K_NUMBER;
    }
    if (target && target->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(t->p, (AstIdentNode*)target);
        if (b && !b->intrinsic && (b->kinds | mask) != b->kinds) { b->kinds |= mask; t->changed = true; }
    }
    js_ast_visit_children(n, type_walk, t);
}

static MIR_op_t reg(LmdCompiler* c, MIR_reg_t r) { return MIR_new_reg_op(c->em.ctx, r); }
static MIR_op_t integer(LmdCompiler* c, uint64_t v) { return MIR_new_uint_op(c->em.ctx, v); }
static MIR_label_t label(LmdCompiler* c) { return em_new_label(&c->em); }
static void put_label(LmdCompiler* c, MIR_label_t l) { em_emit_label(&c->em, l); }
static void jump(LmdCompiler* c, MIR_label_t l) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_JMP, MIR_new_label_op(c->em.ctx, l)));
}
static void move(LmdCompiler* c, MIR_reg_t dst, MIR_op_t value, bool number = false) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, number ? MIR_DMOV : MIR_MOV, reg(c, dst), value));
}
static MIR_reg_t constant(LmdCompiler* c, uint64_t value) {
    MIR_reg_t r = em_new_reg(&c->em, "constant", MIR_T_I64); move(c, r, integer(c, value)); return r;
}
static LmdValue number(LmdCompiler* c, double value) {
    MIR_reg_t r = em_new_reg(&c->em, "number", MIR_T_D);
    move(c, r, MIR_new_double_op(c->em.ctx, value), true); return {r, K_NUMBER, true, value};
}
static MIR_reg_t op(LmdCompiler* c, MIR_insn_code_t code, MIR_op_t a, MIR_op_t b,
        MIR_type_t type = MIR_T_I64) {
    MIR_reg_t r = em_new_reg(&c->em, "value", type);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, code, reg(c, r), a, b)); return r;
}
static void branch(LmdCompiler* c, MIR_insn_code_t code, MIR_label_t l,
        MIR_op_t a, MIR_op_t b) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, code, MIR_new_label_op(c->em.ctx, l), a, b));
}
static void branch_truth(LmdCompiler* c, MIR_label_t l, MIR_reg_t r, bool yes = true) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, yes ? MIR_BT : MIR_BF,
        MIR_new_label_op(c->em.ctx, l), reg(c, r)));
}
static void root_value(void* owner, MIR_reg_t r, JitValueClass cls) {
    LmdCompiler* c = (LmdCompiler*)owner;
    MirFrameState* f = &c->em.frame;
    if (!em_root_note_candidate(&f->gc_candidates, &f->gc_candidate_count,
            &f->gc_candidate_capacity, &f->gc_candidate_by_reg,
            &f->gc_candidate_by_reg_capacity, r, cls, 0)) abort();
}
static LmdValue boxed(LmdCompiler* c, MIR_reg_t r, unsigned hint = K_ANY) {
    root_value(c, r, JIT_VALUE_BOXED_ITEM); return {r, hint | 128};
}
static bool is_boxed(LmdValue v) { return v.kind & 128; }
static unsigned semantic(LmdValue v) { return v.kind & 127; }
static LmdValue expression(LmdCompiler* c, AstNode* n);
static LmdValue box(LmdCompiler* c, LmdValue value);
static MIR_reg_t to_number(LmdCompiler* c, LmdValue value);
static MIR_reg_t truth(LmdCompiler* c, LmdValue value);
static void statement(LmdCompiler* c, AstNode* n);
static void fail(LmdCompiler* c, int kind, AstNode* n) {
    MIR_reg_t error = em_call_2(&c->em, "mvp_lmd_fail", MIR_T_I64, MIR_T_I64,
        integer(c, kind), MIR_T_I64, integer(c, n ? n->source_span.start_byte : 0), true);
    em_stage_function_return(&c->em, reg(c, error));
}
static void check_error(LmdCompiler* c, MIR_reg_t result) {
    MIR_label_t ok = label(c);
    MIR_reg_t tag = op(c, MIR_URSH, reg(c, result), integer(c, 56));
    branch(c, MIR_BNE, ok, reg(c, tag), integer(c, LMD_TYPE_ERROR));
    em_stage_function_return(&c->em, reg(c, result)); put_label(c, ok);
}
static MIR_reg_t payload(LmdCompiler* c, MIR_reg_t value) {
    return op(c, MIR_AND, reg(c, value), integer(c, ITEM_INT_PAYLOAD_MASK));
}
static MIR_reg_t tag(LmdCompiler* c, LmdValue value) {
    unsigned k = semantic(value);
    if (k && !(k & (k - 1))) {
        TypeId tid = k == K_NUMBER ? LMD_TYPE_FLOAT : k == K_STRING ? LMD_TYPE_STRING :
            k == K_BOOL ? LMD_TYPE_BOOL : k == K_NULL ? LMD_TYPE_NULL :
            k == K_UNDEFINED ? LMD_TYPE_UNDEFINED : k == K_ARRAY ? LMD_TYPE_ARRAY : LMD_TYPE_FUNC;
        return constant(c, tid);
    }
    MIR_reg_t result = em_new_reg(&c->em, "type", MIR_T_I64);
    MIR_label_t numeric = label(c), done = label(c);
    MIR_reg_t mask = op(c, MIR_AND, reg(c, value.reg), integer(c, ITEM_DBL_MASK));
    branch_truth(c, numeric, mask);
    move(c, result, reg(c, op(c, MIR_URSH, reg(c, value.reg), integer(c, 56))));
    branch_truth(c, done, result);
    branch(c, MIR_BEQ, done, reg(c, value.reg), integer(c, 0));
    move(c, result, reg(c, em_load_at(&c->em, value.reg, 0, MIR_T_U8, "container_type")));
    jump(c, done);
    put_label(c, numeric); move(c, result, integer(c, LMD_TYPE_FLOAT));
    put_label(c, done); return result;
}
static MIR_reg_t cold_box(void* owner, MIR_reg_t value) {
    LmdCompiler* c = (LmdCompiler*)owner;
    int id = em_scalar_home_new(&c->em);
    MIR_reg_t home = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, id));
    em_store_at(&c->em, home, 0, MIR_T_D, value);
    MIR_reg_t r = op(c, MIR_OR, reg(c, home), integer(c, (uint64_t)LMD_TYPE_FLOAT << 56));
    em_scalar_home_bind(&c->em, id, r); return r;
}
static MIR_reg_t cold_unbox(void* owner, MIR_reg_t item) {
    LmdCompiler* c = (LmdCompiler*)owner;
    return em_unbox_f64_noninline_item(&c->em, c,
        [](void* owner, MIR_reg_t item) -> MIR_reg_t {
            LmdCompiler* c = (LmdCompiler*)owner;
            return em_load_at(&c->em, payload(c, item), 0, MIR_T_D, "wide_number");
        }, item);
}
static LmdValue box(LmdCompiler* c, LmdValue value) {
    if (is_boxed(value)) return value;
    if (value.kind == K_NUMBER && value.literal) {
        double* literal = (double*)pool_alloc(c->program->frontend->pool, sizeof(double));
        *literal = value.number;
        return boxed(c, constant(c, lambda_float_ptr_to_item(literal).item), K_NUMBER);
    }
    if (value.kind == K_NUMBER)
        return boxed(c, em_box_f64_to_item(&c->em, c, cold_box, value.reg), K_NUMBER);
    if (value.kind == K_BOOL)
        return boxed(c, op(c, MIR_OR, reg(c, value.reg), integer(c, (uint64_t)LMD_TYPE_BOOL << 56)), K_BOOL);
    return boxed(c, value.reg, value.kind);
}

static MIR_reg_t to_number(LmdCompiler* c, LmdValue value) {
    if (!is_boxed(value) && value.kind == K_NUMBER) return value.reg;
    if (!is_boxed(value) && value.kind == K_BOOL) {
        MIR_reg_t r = em_new_reg(&c->em, "boolean_number", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, value.reg))); return r;
    }
    value = box(c, value);
    unsigned k = semantic(value);
    if (k == K_NUMBER) return em_unbox_f64_item(&c->em, c, cold_unbox, value.reg);
    if (k == K_STRING) return em_call_1(&c->em, "mvp_lmd_string_to_number", MIR_T_D,
        MIR_T_P, reg(c, payload(c, value.reg)), true);
    if (k == K_NULL || k == K_UNDEFINED) return number(c, k == K_NULL ? 0 : NAN).reg;
    MIR_reg_t tid = tag(c, value), result = em_new_reg(&c->em, "converted_number", MIR_T_D);
    MIR_label_t done = label(c);
    TypeId types[] = {LMD_TYPE_FLOAT, LMD_TYPE_STRING, LMD_TYPE_BOOL, LMD_TYPE_NULL, LMD_TYPE_UNDEFINED};
    MIR_label_t cases[5];
    for (int i = 0; i < 5; i++) {
        cases[i] = label(c); branch(c, MIR_BEQ, cases[i], reg(c, tid), integer(c, types[i]));
    }
    fail(c, LMD_MVP_CAPABILITY, NULL);
    for (int i = 0; i < 5; i++) {
        put_label(c, cases[i]); MIR_reg_t r;
        if (i == 0) r = em_unbox_f64_item(&c->em, c, cold_unbox, value.reg);
        else if (i == 1) r = em_call_1(&c->em, "mvp_lmd_string_to_number", MIR_T_D,
            MIR_T_P, reg(c, payload(c, value.reg)), true);
        else if (i == 2) r = to_number(c, {payload(c, value.reg), K_BOOL});
        else r = number(c, i == 3 ? 0 : NAN).reg;
        move(c, result, reg(c, r), true); jump(c, done);
    }
    put_label(c, done); return result;
}
static MIR_reg_t truth(LmdCompiler* c, LmdValue v) {
    unsigned k = semantic(v);
    if (k == K_NUMBER) {
        MIR_reg_t d = to_number(c, v);
        MIR_reg_t nonzero = op(c, MIR_DNE, reg(c, d), MIR_new_double_op(c->em.ctx, 0));
        return op(c, MIR_AND, reg(c, nonzero), reg(c, op(c, MIR_DEQ, reg(c, d), reg(c, d))));
    }
    if (k == K_BOOL) return is_boxed(v) ? payload(c, v.reg) : v.reg;
    if (k == K_NULL || k == K_UNDEFINED) return constant(c, 0);
    if (k == K_ARRAY || k == K_FUNCTION) return constant(c, 1);
    if (k == K_STRING) return op(c, MIR_NE,
        reg(c, em_load_at(&c->em, payload(c, v.reg), offsetof(String, len), MIR_T_U32, "string_length")), integer(c, 0));
    MIR_reg_t tid = tag(c, v), result = em_new_reg(&c->em, "truth", MIR_T_I64);
    MIR_label_t done = label(c), numeric = label(c), string = label(c), boolean = label(c);
    move(c, result, integer(c, 0));
    branch(c, MIR_BEQ, done, reg(c, tid), integer(c, LMD_TYPE_NULL));
    branch(c, MIR_BEQ, done, reg(c, tid), integer(c, LMD_TYPE_UNDEFINED));
    branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_FLOAT));
    branch(c, MIR_BEQ, string, reg(c, tid), integer(c, LMD_TYPE_STRING));
    branch(c, MIR_BEQ, boolean, reg(c, tid), integer(c, LMD_TYPE_BOOL));
    move(c, result, integer(c, 1)); jump(c, done);
    const unsigned kinds[] = {K_NUMBER, K_STRING, K_BOOL};
    MIR_label_t labels[] = {numeric, string, boolean};
    for (int i = 0; i < 3; i++) {
        put_label(c, labels[i]); move(c, result, reg(c, truth(c, {v.reg, kinds[i] | 128}))); jump(c, done);
    }
    put_label(c, done); return result;
}
static MIR_reg_t nullish(LmdCompiler* c, LmdValue v) {
    if (!(semantic(v) & (K_NULL | K_UNDEFINED))) return constant(c, 0);
    MIR_reg_t t = tag(c, v);
    return op(c, MIR_OR, reg(c, op(c, MIR_EQ, reg(c, t), integer(c, LMD_TYPE_NULL))),
        reg(c, op(c, MIR_EQ, reg(c, t), integer(c, LMD_TYPE_UNDEFINED))));
}
static LmdValue text(LmdCompiler* c, const char* bytes) {
    String* s = name_pool_create_len(c->program->frontend->name_pool, bytes, strlen(bytes));
    return boxed(c, constant(c, s2it(s)), K_STRING);
}
static LmdValue to_string(LmdCompiler* c, LmdValue v) {
    unsigned k = semantic(v);
    if (k == K_STRING) return box(c, v);
    if (k == K_NUMBER) {
        MIR_reg_t r = em_call_1(&c->em, "mvp_lmd_number_to_string", MIR_T_I64, MIR_T_D,
            reg(c, to_number(c, v)), true); check_error(c, r); return boxed(c, r, K_STRING);
    }
    if (k == K_NULL || k == K_UNDEFINED) return text(c, k == K_NULL ? "null" : "undefined");
    if (k == K_BOOL) {
        MIR_reg_t result = em_new_reg(&c->em, "boolean_string", MIR_T_I64);
        MIR_label_t done = label(c);
        move(c, result, reg(c, text(c, "false").reg));
        branch_truth(c, done, truth(c, v), false);
        move(c, result, reg(c, text(c, "true").reg));
        put_label(c, done); return boxed(c, result, K_STRING);
    }
    MIR_reg_t tid = tag(c, v), result = em_new_reg(&c->em, "primitive_string", MIR_T_I64);
    MIR_label_t done = label(c);
    const TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_FLOAT, LMD_TYPE_BOOL, LMD_TYPE_NULL, LMD_TYPE_UNDEFINED};
    const unsigned kinds[] = {K_STRING, K_NUMBER, K_BOOL, K_NULL, K_UNDEFINED};
    MIR_label_t cases[5];
    for (int i = 0; i < 5; i++) {
        cases[i] = label(c); branch(c, MIR_BEQ, cases[i], reg(c, tid), integer(c, types[i]));
    }
    fail(c, LMD_MVP_CAPABILITY, NULL);
    for (int i = 0; i < 5; i++) {
        put_label(c, cases[i]); move(c, result, reg(c, to_string(c, {v.reg, kinds[i] | 128}).reg)); jump(c, done);
    }
    put_label(c, done); return boxed(c, result, K_STRING);
}
static MIR_reg_t int32(LmdCompiler* c, LmdValue v) {
    MIR_reg_t d = to_number(c, v);
    MIR_label_t zero = label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "int32", MIR_T_I64);
    MIR_reg_t bounded = em_call_2(&c->em, "fmod", MIR_T_D, MIR_T_D, reg(c, d),
        MIR_T_D, MIR_new_double_op(c->em.ctx, 4294967296.0), true);
    branch_truth(c, zero, op(c, MIR_DEQ, reg(c, bounded), reg(c, bounded)), false);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, result), reg(c, bounded)));
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_EXT32, reg(c, result), reg(c, result)));
    jump(c, done); put_label(c, zero); move(c, result, integer(c, 0)); put_label(c, done); return result;
}

static LmdValue binary_value(LmdCompiler* c, Operator operation, LmdValue left, LmdValue right);
static LmdValue equal(LmdCompiler* c, LmdValue left, LmdValue right, bool loose) {
    if (semantic(left) == K_NUMBER && semantic(right) == K_NUMBER)
        return {op(c, MIR_DEQ, reg(c, to_number(c, left)), reg(c, to_number(c, right))), K_BOOL};
    left = box(c, left); right = box(c, right);
    MIR_reg_t lt = tag(c, left), rt = tag(c, right);
    MIR_reg_t result = em_new_reg(&c->em, "equal", MIR_T_I64);
    MIR_label_t same = label(c), numeric = label(c), strings = label(c), done = label(c);
    move(c, result, integer(c, 0));
    branch(c, MIR_BEQ, same, reg(c, lt), reg(c, rt));
    if (loose) {
        MIR_reg_t ln = nullish(c, left), rn = nullish(c, right);
        MIR_reg_t both_null = op(c, MIR_AND, reg(c, ln), reg(c, rn));
        move(c, result, reg(c, both_null)); branch_truth(c, done, result);
        branch_truth(c, done, op(c, MIR_OR, reg(c, ln), reg(c, rn)));
        // arrays and functions are both JS Objects; different objects compare false.
        MIR_reg_t lo = op(c, MIR_OR,
            reg(c, op(c, MIR_EQ, reg(c, lt), integer(c, LMD_TYPE_ARRAY))),
            reg(c, op(c, MIR_EQ, reg(c, lt), integer(c, LMD_TYPE_FUNC))));
        MIR_reg_t ro = op(c, MIR_OR,
            reg(c, op(c, MIR_EQ, reg(c, rt), integer(c, LMD_TYPE_ARRAY))),
            reg(c, op(c, MIR_EQ, reg(c, rt), integer(c, LMD_TYPE_FUNC))));
        branch_truth(c, done, op(c, MIR_AND, reg(c, lo), reg(c, ro)));
        // conversion covers the complete admitted Boolean/Number/String primitive domain.
        const TypeId primitive_types[] = {LMD_TYPE_BOOL, LMD_TYPE_FLOAT, LMD_TYPE_STRING};
        for (TypeId t : primitive_types) {
            MIR_label_t next = label(c);
            branch(c, MIR_BNE, next, reg(c, lt), integer(c, t));
            branch(c, MIR_BEQ, numeric, reg(c, rt), integer(c, LMD_TYPE_BOOL));
            branch(c, MIR_BEQ, numeric, reg(c, rt), integer(c, LMD_TYPE_FLOAT));
            branch(c, MIR_BEQ, numeric, reg(c, rt), integer(c, LMD_TYPE_STRING));
            fail(c, LMD_MVP_CAPABILITY, NULL); put_label(c, next);
        }
        // object/primitive comparisons need the excluded ToPrimitive protocol.
        fail(c, LMD_MVP_CAPABILITY, NULL);
    }
    jump(c, done);
    put_label(c, same);
    branch(c, MIR_BEQ, numeric, reg(c, lt), integer(c, LMD_TYPE_FLOAT));
    branch(c, MIR_BEQ, strings, reg(c, lt), integer(c, LMD_TYPE_STRING));
    move(c, result, reg(c, op(c, MIR_EQ, reg(c, left.reg), reg(c, right.reg)))); jump(c, done);
    put_label(c, numeric);
    move(c, result, reg(c, op(c, MIR_DEQ, reg(c, to_number(c, left)), reg(c, to_number(c, right))))); jump(c, done);
    put_label(c, strings);
    MIR_reg_t order = em_call_2(&c->em, "mvp_lmd_string_compare", MIR_T_I64,
        MIR_T_I64, reg(c, left.reg), MIR_T_I64, reg(c, right.reg), true);
    move(c, result, reg(c, op(c, MIR_EQ, reg(c, order), integer(c, 0))));
    put_label(c, done); return {result, K_BOOL};
}
static LmdValue binary_value(LmdCompiler* c, Operator operation, LmdValue left, LmdValue right) {
    if (operation == OPERATOR_EQ || operation == OPERATOR_NE ||
            operation == OPERATOR_JS_STRICT_EQ || operation == OPERATOR_JS_STRICT_NE) {
        LmdValue result = equal(c, left, right, operation == OPERATOR_EQ || operation == OPERATOR_NE);
        if (operation == OPERATOR_NE || operation == OPERATOR_JS_STRICT_NE)
            result.reg = op(c, MIR_XOR, reg(c, result.reg), integer(c, 1));
        return result;
    }
    if (operation == OPERATOR_ADD && ((semantic(left) | semantic(right)) & K_STRING)) {
        MIR_reg_t result = em_new_reg(&c->em, "addition", MIR_T_I64);
        MIR_label_t strings = label(c), done = label(c);
        MIR_reg_t lt = tag(c, left), rt = tag(c, right);
        branch(c, MIR_BEQ, strings, reg(c, lt), integer(c, LMD_TYPE_STRING));
        branch(c, MIR_BEQ, strings, reg(c, rt), integer(c, LMD_TYPE_STRING));
        move(c, result, reg(c, box(c, binary_value(c, OPERATOR_ADD,
            {to_number(c, left), K_NUMBER}, {to_number(c, right), K_NUMBER})).reg)); jump(c, done);
        put_label(c, strings);
        LmdValue l = to_string(c, left), r = to_string(c, right);
        MIR_reg_t joined = em_call_2(&c->em, "mvp_lmd_string_concat", MIR_T_I64,
            MIR_T_I64, reg(c, l.reg), MIR_T_I64, reg(c, r.reg), true);
        check_error(c, joined); move(c, result, reg(c, joined)); put_label(c, done);
        return boxed(c, result, K_NUMBER | K_STRING);
    }
    MirNumericOpPlan plan = {};
    bool planned = em_numeric_op_plan(operation, &plan);
    if (planned && plan.is_comparison && ((semantic(left) & semantic(right)) & K_STRING)) {
        MIR_reg_t result = em_new_reg(&c->em, "relational", MIR_T_I64);
        MIR_label_t numeric = label(c), done = label(c);
        branch(c, MIR_BNE, numeric, reg(c, tag(c, left)), integer(c, LMD_TYPE_STRING));
        branch(c, MIR_BNE, numeric, reg(c, tag(c, right)), integer(c, LMD_TYPE_STRING));
        left = box(c, left); right = box(c, right);
        MIR_reg_t order = em_call_2(&c->em, "mvp_lmd_string_compare", MIR_T_I64,
            MIR_T_I64, reg(c, left.reg), MIR_T_I64, reg(c, right.reg), true);
        move(c, result, reg(c, op(c, plan.i64_opcode, reg(c, order), integer(c, 0)))); jump(c, done);
        put_label(c, numeric);
        move(c, result, reg(c, op(c, plan.f64_opcode, reg(c, to_number(c, left)), reg(c, to_number(c, right)))));
        put_label(c, done); return {result, K_BOOL};
    }
    if (planned) {
        MIR_reg_t l = to_number(c, left), r = to_number(c, right);
        if (plan.helper_name) return {em_call_2(&c->em, plan.helper_name, MIR_T_D,
            MIR_T_D, reg(c, l), MIR_T_D, reg(c, r), true), K_NUMBER};
        return {op(c, plan.f64_opcode, reg(c, l), reg(c, r), plan.is_comparison ? MIR_T_I64 : MIR_T_D),
            plan.is_comparison ? K_BOOL : K_NUMBER};
    }
    if (operation == OPERATOR_JS_EXP) return {em_call_2(&c->em, "mvp_lmd_number_pow", MIR_T_D,
        MIR_T_D, reg(c, to_number(c, left)), MIR_T_D, reg(c, to_number(c, right)), true), K_NUMBER};
    MIR_reg_t l = int32(c, left), r = int32(c, right);
    MIR_insn_code_t code;
    switch (operation) {
    case OPERATOR_JS_BIT_AND: code = MIR_ANDS; break;
    case OPERATOR_JS_BIT_OR: code = MIR_ORS; break;
    case OPERATOR_JS_BIT_XOR: code = MIR_XORS; break;
    case OPERATOR_JS_LSHIFT: code = MIR_LSHS; break;
    case OPERATOR_JS_RSHIFT: code = MIR_RSHS; break;
    case OPERATOR_JS_URSHIFT: code = MIR_URSHS; break;
    default: diagnostic(c->program, NULL, "operator outside MVP scope"); return number(c, NAN);
    }
    if (operation == OPERATOR_JS_LSHIFT || operation == OPERATOR_JS_RSHIFT || operation == OPERATOR_JS_URSHIFT)
        r = op(c, MIR_AND, reg(c, r), integer(c, 31));
    MIR_reg_t bits = op(c, code, reg(c, l), reg(c, r));
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx,
        operation == OPERATOR_JS_URSHIFT ? MIR_UEXT32 : MIR_EXT32, reg(c, bits), reg(c, bits)));
    MIR_reg_t result = em_new_reg(&c->em, "bitwise_number", MIR_T_D);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, result), reg(c, bits)));
    return {result, K_NUMBER};
}
static MIR_reg_t module_slots(LmdCompiler* c) {
    return em_load_at(&c->em, c->unit, offsetof(MvpLmdProgram, slots), MIR_T_I64, "program_slots");
}
static LmdValue stable_item(LmdCompiler* c, MIR_reg_t value) {
    int home = em_scalar_home_new(&c->em);
    MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home));
    MIR_reg_t result = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, value, address);
    em_scalar_home_bind(&c->em, home, result); return boxed(c, result);
}
static MIR_reg_t binding_item(LmdCompiler* c, LmdBinding* b) {
    return !b->owner->id ? em_load_at(&c->em, module_slots(c),
        b->slot * sizeof(Item), MIR_T_I64, "program_value") : b->reg;
}
static void check_tdz(LmdCompiler* c, LmdBinding* b, MIR_reg_t value, AstNode* site) {
    if (b->entry->is_lexical && !b->entry->is_function_name_binding && !b->dominated) {
        MIR_label_t ok = label(c);
        branch(c, MIR_BNE, ok, reg(c, value), integer(c, ITEM_JS_TDZ));
        fail(c, LMD_MVP_REFERENCE, site); put_label(c, ok);
    }
}
static LmdValue read_binding(LmdCompiler* c, LmdBinding* b, AstNode* site) {
    if (!b) { fail(c, LMD_MVP_REFERENCE, site); return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED); }
    if (b->intrinsic) return b->intrinsic == 1
        ? boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED)
        : number(c, b->intrinsic == 2 ? NAN : INFINITY);
    MIR_reg_t source = b->native ? b->reg : binding_item(c, b);
    if (!b->native) check_tdz(c, b, source, site);
    MIR_reg_t copy = em_new_reg(&c->em, "read", b->native && b->kinds == K_NUMBER ? MIR_T_D : MIR_T_I64);
    move(c, copy, reg(c, source), b->native && b->kinds == K_NUMBER);
    if (b->native) return {copy, b->kinds};
    bool known = b->dominated || (b->target && b->target->ast->entry == b->entry &&
        b->entry->node && b->entry->node->node_type == AST_NODE_FUNC && !b->entry->is_annex_b_companion);
    if (known && !(b->kinds & K_NUMBER)) return boxed(c, copy, b->kinds);
    LmdValue result = stable_item(c, copy);
    if (known) result.kind = b->kinds | 128;
    return result;
}
static void write_binding(LmdCompiler* c, LmdBinding* b, LmdValue v, AstNode* site, bool initialize = false) {
    if (!b) { fail(c, c->strict ? LMD_MVP_REFERENCE : LMD_MVP_CAPABILITY, site); return; }
    if (b->intrinsic) { if (c->strict) fail(c, LMD_MVP_TYPE, site); return; }
    if (!initialize && !b->native) check_tdz(c, b, binding_item(c, b), site);
    if (!initialize && (b->entry->is_const || b->entry->is_function_name_binding)) {
        if (!b->entry->is_function_name_binding || c->strict) fail(c, LMD_MVP_TYPE, site);
        return;
    }
    if (b->native) {
        move(c, b->reg, reg(c, b->kinds == K_NUMBER ? to_number(c, v) : truth(c, v)), b->kinds == K_NUMBER);
    } else {
        v = box(c, v);
        if (!b->owner->id) em_call_void_4(&c->em, "owned_item_slot_store", MIR_T_P, reg(c, module_slots(c)),
            MIR_T_I64, integer(c, c->program->slot_count), MIR_T_I64, integer(c, b->slot),
            MIR_T_I64, reg(c, v.reg), true);
        else {
            if (!b->scalar_home) b->scalar_home = em_scalar_home_new(&c->em);
            MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, b->scalar_home));
            MIR_reg_t owned = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, v.reg, address);
            move(c, b->reg, reg(c, owned)); em_scalar_home_bind(&c->em, b->scalar_home, b->reg);
        }
    }
}
static MIR_reg_t property_key(LmdCompiler* c, LmdValue value) {
    if (value.literal) return constant(c, value.number >= 0 && value.number < UINT32_MAX &&
        value.number == trunc(value.number) ? (uint64_t)value.number : (uint64_t)-2);
    unsigned k = semantic(value);
    MIR_reg_t result = em_new_reg(&c->em, "index", MIR_T_I64);
    MIR_label_t numeric = label(c), done = label(c);
    if (k != K_NUMBER) {
        if (k != K_STRING) branch(c, MIR_BEQ, numeric, reg(c, tag(c, value)), integer(c, LMD_TYPE_FLOAT));
        LmdValue string = to_string(c, value);
        move(c, result, reg(c, em_call_1(&c->em, "mvp_lmd_string_key", MIR_T_I64,
            MIR_T_P, reg(c, payload(c, string.reg)), true))); jump(c, done);
    }
    put_label(c, numeric);
    MIR_reg_t d = to_number(c, value);
    move(c, result, MIR_new_int_op(c->em.ctx, -2));
    branch_truth(c, done, op(c, MIR_DGE, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
    branch_truth(c, done, op(c, MIR_DLT, reg(c, d), MIR_new_double_op(c->em.ctx, UINT32_MAX)), false);
    MIR_reg_t i = em_new_reg(&c->em, "integer_index", MIR_T_I64);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, i), reg(c, d)));
    MIR_reg_t round = em_new_reg(&c->em, "index_roundtrip", MIR_T_D);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, round), reg(c, i)));
    branch_truth(c, done, op(c, MIR_DEQ, reg(c, d), reg(c, round)), false);
    move(c, result, reg(c, i)); put_label(c, done); return result;
}
static LmdReference reference(LmdCompiler* c, AstNode* n) {
    if (n->node_type == AST_NODE_IDENT) return {identifier_binding(c->program, (AstIdentNode*)n), {}, 0};
    AstFieldNode* field = (AstFieldNode*)n;
    LmdValue owner = box(c, expression(c, field->object));
    bool known = !field->computed || field->property->node_type == AST_NODE_LITERAL;
    int64_t index = -1;
    MIR_reg_t key;
    if (known) {
        if (field->computed) {
            AstLiteralNode* literal = (AstLiteralNode*)field->property;
            if (literal->literal_type == AST_LITERAL_STRING) index = mvp_lmd_string_key(literal->value.string_value);
            else if (literal->literal_type == AST_LITERAL_NUMBER) {
                double d = literal->value.number_value;
                index = d >= 0 && d < UINT32_MAX && d == trunc(d) ? (int64_t)d : -2;
            } else index = -2;
        }
        key = constant(c, (uint64_t)index);
    } else key = property_key(c, expression(c, field->property));
    if (known) {
        if (index < -1) fail(c, LMD_MVP_CAPABILITY, n);
    } else {
        MIR_label_t valid = label(c);
        branch(c, MIR_BGE, valid, reg(c, key), MIR_new_int_op(c->em.ctx, -1));
        fail(c, LMD_MVP_CAPABILITY, n); put_label(c, valid);
    }
    return {NULL, owner, key, known, index};
}
static void property_failure(LmdCompiler* c, LmdValue owner, AstNode* site) {
    MIR_label_t type = label(c);
    branch_truth(c, type, nullish(c, owner));
    fail(c, LMD_MVP_CAPABILITY, site); put_label(c, type);
    fail(c, LMD_MVP_TYPE, site);
}
static LmdValue array_read(LmdCompiler* c, LmdReference ref) {
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "array_length");
    if (ref.key_known && ref.index == -1) {
        MIR_reg_t d = em_new_reg(&c->em, "length_number", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, d), reg(c, size)));
        return {d, K_NUMBER};
    }
    MIR_label_t length = ref.key_known ? NULL : label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "element", MIR_T_I64);
    if (length) branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    move(c, result, integer(c, ITEM_JS_UNDEFINED));
    branch(c, MIR_UBGE, done, reg(c, ref.key), reg(c, size));
    move(c, result, reg(c, stable_item(c, em_load_at(&c->em,
        em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, "element")).reg));
    if (length) {
        jump(c, done); put_label(c, length);
        LmdReference length_ref = ref; length_ref.key_known = true; length_ref.index = -1;
        move(c, result, reg(c, box(c, array_read(c, length_ref)).reg));
    }
    put_label(c, done); return boxed(c, result);
}
static LmdValue string_read(LmdCompiler* c, LmdReference ref) {
    if (ref.key_known && ref.index == -1) {
        MIR_reg_t string = payload(c, ref.owner.reg);
        MIR_reg_t bytes = op(c, MIR_ADD, reg(c, string), integer(c, offsetof(String, chars)));
        MIR_reg_t size = em_call_2(&c->em, "utf8_to_utf16_length", MIR_T_I64, MIR_T_P, reg(c, bytes),
            MIR_T_I64, reg(c, em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "byte_length")), true);
        MIR_reg_t d = em_new_reg(&c->em, "length_number", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, d), reg(c, size)));
        return {d, K_NUMBER};
    }
    MIR_label_t length = ref.key_known ? NULL : label(c), done = ref.key_known ? NULL : label(c);
    MIR_reg_t result = em_new_reg(&c->em, "string_property", MIR_T_I64);
    if (length) branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    MIR_reg_t at = em_call_2(&c->em, "mvp_lmd_string_at", MIR_T_I64,
        MIR_T_I64, reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.key), true);
    check_error(c, at); move(c, result, reg(c, at));
    if (length) {
        jump(c, done); put_label(c, length);
        LmdReference length_ref = ref; length_ref.key_known = true; length_ref.index = -1;
        move(c, result, reg(c, box(c, string_read(c, length_ref)).reg)); put_label(c, done);
    }
    return boxed(c, result, length ? K_ANY : K_STRING | K_UNDEFINED);
}
static LmdValue read_reference(LmdCompiler* c, LmdReference ref, AstNode* site) {
    if (!ref.key) return read_binding(c, ref.binding, site);
    if (semantic(ref.owner) == K_ARRAY) return array_read(c, ref);
    if (semantic(ref.owner) == K_STRING) return string_read(c, ref);
    MIR_reg_t tid = tag(c, ref.owner), result = em_new_reg(&c->em, "property", MIR_T_I64);
    MIR_label_t array = label(c), string = label(c), done = label(c);
    branch(c, MIR_BEQ, array, reg(c, tid), integer(c, LMD_TYPE_ARRAY));
    branch(c, MIR_BEQ, string, reg(c, tid), integer(c, LMD_TYPE_STRING));
    property_failure(c, ref.owner, site);
    put_label(c, array); move(c, result, reg(c, box(c, array_read(c, ref)).reg)); jump(c, done);
    put_label(c, string); move(c, result, reg(c, box(c, string_read(c, ref)).reg));
    put_label(c, done); return boxed(c, result);
}
static void array_index_write(LmdCompiler* c, LmdReference ref, LmdValue value) {
    LmdValue item = box(c, value);
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length");
    MIR_label_t done = label(c), slow = label(c), store = label(c);
    branch(c, MIR_UBGT, slow, reg(c, ref.key), reg(c, size));
    // only out-of-band Numbers require an owned scalar home; zero sentinels store verbatim.
    MIR_reg_t wide_tag = op(c, MIR_URSH, reg(c, item.reg), integer(c, 56));
    MIR_label_t immediate = label(c);
    branch(c, MIR_BNE, immediate, reg(c, wide_tag), integer(c, LMD_TYPE_FLOAT));
    branch(c, MIR_UBGT, slow, reg(c, item.reg), integer(c, ITEM_FLOAT_N0)); put_label(c, immediate);
    branch(c, MIR_UBLT, store, reg(c, ref.key), reg(c, size));
    MIR_reg_t capacity = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_CAPACITY, MIR_T_I64, "capacity");
    MIR_reg_t extra = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_EXTRA, MIR_T_I64, "tail_homes");
    branch(c, MIR_UBGE, slow, reg(c, size), reg(c, op(c, MIR_SUB, reg(c, capacity), reg(c, extra))));
    put_label(c, store);
    em_store_at(&c->em, em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, item.reg);
    branch(c, MIR_UBLT, done, reg(c, ref.key), reg(c, size));
    em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64,
        op(c, MIR_ADD, reg(c, size), integer(c, 1))); jump(c, done);
    put_label(c, slow);
    MIR_reg_t stored = em_call_3(&c->em, "mvp_lmd_array_store", MIR_T_I64, MIR_T_I64,
        reg(c, ref.owner.reg), MIR_T_I64, reg(c, ref.key), MIR_T_I64, reg(c, item.reg), true);
    check_error(c, stored); put_label(c, done);
}
static void array_length_write(LmdCompiler* c, LmdReference ref, LmdValue value, AstNode* site) {
    MIR_reg_t d = to_number(c, value), i = em_new_reg(&c->em, "new_length", MIR_T_I64);
    MIR_label_t range_error = label(c), capability = label(c), done = label(c);
    branch_truth(c, range_error, op(c, MIR_DGE, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
    branch_truth(c, range_error, op(c, MIR_DLE, reg(c, d), MIR_new_double_op(c->em.ctx, UINT32_MAX)), false);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, i), reg(c, d)));
    MIR_reg_t round = em_new_reg(&c->em, "length_roundtrip", MIR_T_D);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, round), reg(c, i)));
    branch_truth(c, range_error, op(c, MIR_DEQ, reg(c, d), reg(c, round)), false);
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "old_length");
    branch(c, MIR_UBGT, capability, reg(c, i), reg(c, size));
    em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, i); jump(c, done);
    put_label(c, range_error); fail(c, LMD_MVP_RANGE, site);
    put_label(c, capability); fail(c, LMD_MVP_CAPABILITY, site); put_label(c, done);
}
static void write_reference(LmdCompiler* c, LmdReference ref, LmdValue value, AstNode* site) {
    if (!ref.key) { write_binding(c, ref.binding, value, site); return; }
    if (semantic(ref.owner) != K_ARRAY) {
        MIR_label_t array = label(c);
        branch(c, MIR_BEQ, array, reg(c, tag(c, ref.owner)), integer(c, LMD_TYPE_ARRAY));
        property_failure(c, ref.owner, site); put_label(c, array);
    }
    if (ref.key_known) {
        if (ref.index == -1) array_length_write(c, ref, value, site);
        else array_index_write(c, ref, value);
    } else {
        MIR_label_t length = label(c), done = label(c);
        branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        array_index_write(c, ref, value); jump(c, done);
        put_label(c, length); array_length_write(c, ref, value, site); put_label(c, done);
    }
}

static LmdValue new_function(LmdCompiler* c, LmdFunction* f) {
    MIR_reg_t value = em_call_2(&c->em, "mvp_lmd_function_new", MIR_T_I64,
        MIR_T_I64, integer(c, f->id), MIR_T_P, reg(c, c->unit), true);
    check_error(c, value); return boxed(c, value, K_FUNCTION);
}
static LmdValue call(LmdCompiler* c, AstCallNode* n) {
    LmdBinding* b = n->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(c->program, (AstIdentNode*)n->callee) : NULL;
    LmdFunction* direct = b && b->target && !b->assigned ? b->target : NULL;
    LmdValue callee = direct && !b->observed ? boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED)
        : box(c, expression(c, n->callee));
    int count = ast_linked_node_count(n->arguments);
    LmdValue* values = (LmdValue*)mem_calloc((size_t)count, sizeof(LmdValue), MEM_CAT_TEMP);
    int i = 0;
    for (AstNode* a = n->arguments; a; a = a->next) values[i++] = box(c, expression(c, a));
    MIR_reg_t target = 0;
    if (!direct) {
        MIR_label_t valid = label(c);
        branch(c, MIR_BEQ, valid, reg(c, tag(c, callee)), integer(c, LMD_TYPE_FUNC));
        fail(c, LMD_MVP_TYPE, (AstNode*)n); put_label(c, valid);
        valid = label(c);
        MIR_reg_t abi = em_load_at(&c->em, callee.reg, offsetof(Function, entry_abi), MIR_T_U8, "callable_abi");
        branch(c, MIR_BEQ, valid, reg(c, abi), integer(c, FN_ENTRY_ABI_MVP_LMD));
        fail(c, LMD_MVP_CAPABILITY, (AstNode*)n); put_label(c, valid);
        target = em_load_at(&c->em, callee.reg, offsetof(Function, ptr), MIR_T_I64, "entry");
    }
    MIR_reg_t span = 0;
    if (count) {
        if (count > c->argument_count) c->argument_count = count;
        span = em_deferred_root_span_base(&c->em, &c->argument_span, &c->argument_fixup);
        for (i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, values[i].reg);
    }
    mem_free(values);
    MIR_type_t types[] = {MIR_T_P, MIR_T_P, MIR_T_P, MIR_T_I64, MIR_T_I64};
    MIR_op_t args[] = {reg(c, c->em.frame.runtime), reg(c, c->unit), span ? reg(c, span) : integer(c, 0),
        integer(c, count), reg(c, callee.reg)};
    MirImportEntry copy = c->program->entry; copy.call.abi_args = copy.abi_args;
    em_before_resolved_call(&c->em, "mvp_lmd_entry", &copy.call, 5, types, args);
    MIR_reg_t result = em_new_reg(&c->em, "call_result", MIR_T_I64);
    MIR_insn_t insn = mir_new_call_with_target(c->em.ctx, copy.proto,
        direct ? MIR_new_ref_op(c->em.ctx, direct->forward) : reg(c, target), result, 5, args);
    mir_append_emit_insn(c->em.ctx, c->em.func_item, insn);
    em_after_resolved_call(&c->em, "mvp_lmd_entry", &copy.call, insn, 0, MIR_T_I64);
    // consume the context companion before any safepoint or rooted store (D5.2.1v3).
    MIR_reg_t companion = em_load_at(&c->em, c->em.frame.runtime,
        offsetof(Context, mir_companion_slot), MIR_T_I64, "companion");
    int home = em_scalar_home_new(&c->em);
    MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home));
    MIR_reg_t resolved = em_resolve_pending_pair(&c->em, result, companion, address);
    em_scalar_home_bind(&c->em, home, resolved);
    if (count) {
        MIR_reg_t absent = constant(c, ITEM_JS_UNDEFINED);
        for (i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, absent);
    }
    check_error(c, resolved); return boxed(c, resolved);
}
static Operator compound_operator(Operator op) {
    static const Operator operations[] = {OPERATOR_ADD, OPERATOR_SUB, OPERATOR_MUL,
        OPERATOR_DIV, OPERATOR_MOD, OPERATOR_JS_EXP, OPERATOR_JS_BIT_AND,
        OPERATOR_JS_BIT_OR, OPERATOR_JS_BIT_XOR, OPERATOR_JS_LSHIFT,
        OPERATOR_JS_RSHIFT, OPERATOR_JS_URSHIFT};
    return operations[op - OPERATOR_JS_ADD_ASSIGN];
}
static LmdValue logical(LmdCompiler* c, Operator operation, AstNode* left, AstNode* right,
        bool assignment = false) {
    LmdReference ref = {};
    LmdValue value;
    if (assignment) { ref = reference(c, left); value = read_reference(c, ref, left); }
    else value = expression(c, left);
    bool numeric = kind(c->program, left) == K_NUMBER && kind(c->program, right) == K_NUMBER;
    MIR_reg_t result = em_new_reg(&c->em, "short_circuit", numeric ? MIR_T_D : MIR_T_I64);
    MIR_label_t done = label(c);
    move(c, result, reg(c, numeric ? to_number(c, value) : box(c, value).reg), numeric);
    bool is_and = operation == OPERATOR_AND || operation == OPERATOR_JS_AND_ASSIGN;
    bool is_null = operation == OPERATOR_JS_NULLISH_COALESCE || operation == OPERATOR_JS_NULLISH_ASSIGN;
    branch_truth(c, done, is_null ? nullish(c, value) : truth(c, value), !is_and && !is_null);
    LmdValue rhs = expression(c, right);
    if (assignment) write_reference(c, ref, rhs, left);
    move(c, result, reg(c, numeric ? to_number(c, rhs) : box(c, rhs).reg), numeric);
    put_label(c, done); return numeric ? LmdValue{result, K_NUMBER} : boxed(c, result);
}
static LmdValue expression(LmdCompiler* c, AstNode* n) {
    if (!n) return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    switch (n->node_type) {
    case AST_NODE_LITERAL: {
        AstLiteralNode* l = (AstLiteralNode*)n;
        if (l->literal_type == AST_LITERAL_NUMBER) return number(c, l->value.number_value);
        if (l->literal_type == AST_LITERAL_STRING)
            return boxed(c, constant(c, s2it(l->value.string_value)), K_STRING);
        if (l->literal_type == AST_LITERAL_BOOLEAN) return {constant(c, l->value.boolean_value), K_BOOL};
        return boxed(c, constant(c, l->literal_type == AST_LITERAL_NULL ? ITEM_NULL : ITEM_JS_UNDEFINED),
            l->literal_type == AST_LITERAL_NULL ? K_NULL : K_UNDEFINED);
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        LmdBinding* b = identifier_binding(c->program, id);
        return read_binding(c, b, n);
    }
    case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC:
        return new_function(c, function(c->program, n));
    case AST_NODE_CALL_EXPR: return call(c, (AstCallNode*)n);
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: return read_reference(c, reference(c, n), n);
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        if (a->op == OPERATOR_JS_AND_ASSIGN || a->op == OPERATOR_JS_OR_ASSIGN || a->op == OPERATOR_JS_NULLISH_ASSIGN)
            return logical(c, a->op, a->left, a->right, true);
        LmdReference ref = reference(c, a->left);
        LmdValue old = {};
        if (a->op != OPERATOR_ASSIGN) old = read_reference(c, ref, a->left);
        LmdValue rhs = expression(c, a->right);
        if (a->op != OPERATOR_ASSIGN) rhs = binary_value(c, compound_operator(a->op), old, rhs);
        write_reference(c, ref, rhs, n); return rhs;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)n;
        if (u->op == OPERATOR_JS_INCREMENT || u->op == OPERATOR_JS_DECREMENT) {
            LmdReference ref = reference(c, u->operand);
            MIR_reg_t before = to_number(c, read_reference(c, ref, n));
            LmdValue after = binary_value(c, u->op == OPERATOR_JS_INCREMENT ? OPERATOR_ADD : OPERATOR_SUB,
                {before, K_NUMBER}, number(c, 1));
            write_reference(c, ref, after, n); return u->prefix ? after : LmdValue{before, K_NUMBER};
        }
        if (u->op == OPERATOR_JS_TYPEOF && u->operand->node_type == AST_NODE_IDENT &&
                !identifier_binding(c->program, (AstIdentNode*)u->operand)) return text(c, "undefined");
        LmdValue v = expression(c, u->operand);
        if (u->op == OPERATOR_NOT) return {op(c, MIR_XOR, reg(c, truth(c, v)), integer(c, 1)), K_BOOL};
        if (u->op == OPERATOR_JS_VOID) return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        if (u->op == OPERATOR_JS_TYPEOF) {
            MIR_reg_t tid = tag(c, v), result = em_new_reg(&c->em, "typeof", MIR_T_I64);
            MIR_label_t done = label(c);
            TypeId types[] = {LMD_TYPE_UNDEFINED, LMD_TYPE_BOOL, LMD_TYPE_FLOAT, LMD_TYPE_STRING, LMD_TYPE_FUNC};
            const char* names[] = {"undefined", "boolean", "number", "string", "function"};
            move(c, result, reg(c, text(c, "object").reg));
            for (int i = 0; i < 5; i++) {
                MIR_label_t next = label(c);
                branch(c, MIR_BNE, next, reg(c, tid), integer(c, types[i]));
                move(c, result, reg(c, text(c, names[i]).reg)); jump(c, done); put_label(c, next);
            }
            put_label(c, done); return boxed(c, result, K_STRING);
        }
        if (u->op == OPERATOR_JS_BIT_NOT) {
            MIR_reg_t bits = op(c, MIR_XOR, reg(c, int32(c, v)), integer(c, UINT64_MAX));
            MIR_reg_t d = em_new_reg(&c->em, "bit_not", MIR_T_D);
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, d), reg(c, bits))); return {d, K_NUMBER};
        }
        MIR_reg_t d = to_number(c, v);
        if (u->op == OPERATOR_POS) return {d, K_NUMBER};
        if (v.literal) return number(c, -v.number);
        MIR_reg_t result = em_new_reg(&c->em, "negate", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_DNEG, reg(c, result), reg(c, d))); return {result, K_NUMBER};
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        if (b->op == OPERATOR_AND || b->op == OPERATOR_OR || b->op == OPERATOR_JS_NULLISH_COALESCE)
            return logical(c, b->op, b->left, b->right);
        LmdValue left = expression(c, b->left), right = expression(c, b->right);
        return binary_value(c, b->op, left, right);
    }
    case AST_NODE_CONDITIONAL_EXPR: {
        AstIfNode* b = (AstIfNode*)n;
        bool numeric = kind(c->program, n) == K_NUMBER;
        MIR_reg_t result = em_new_reg(&c->em, "conditional", numeric ? MIR_T_D : MIR_T_I64);
        MIR_label_t alternate = label(c), done = label(c);
        branch_truth(c, alternate, truth(c, expression(c, b->test)), false);
        LmdValue yes = expression(c, b->then);
        move(c, result, reg(c, numeric ? to_number(c, yes) : box(c, yes).reg), numeric); jump(c, done);
        put_label(c, alternate); LmdValue no = expression(c, b->otherwise);
        move(c, result, reg(c, numeric ? to_number(c, no) : box(c, no).reg), numeric);
        put_label(c, done); return numeric ? LmdValue{result, K_NUMBER} : boxed(c, result);
    }
    case AST_NODE_SEQ: {
        LmdValue v = {};
        for (AstNode* a = ((AstArrayNode*)n)->item; a; a = a->next) v = expression(c, a);
        return v;
    }
    case AST_NODE_ARRAY: {
        AstArrayNode* a = (AstArrayNode*)n;
        MIR_reg_t result = em_call_0(&c->em, "array", MIR_T_P, false);
        root_value(c, result, JIT_VALUE_RAW_GC_POINTER);
        MIR_reg_t reserved = em_call_2(&c->em, "array_reserve_append_slots", MIR_T_I64,
            MIR_T_P, reg(c, result), MIR_T_I64, integer(c, (uint64_t)a->length * 2), true);
        MIR_label_t ok = label(c); branch_truth(c, ok, reserved); fail(c, LMD_MVP_MEMORY, n); put_label(c, ok);
        uint32_t i = 0;
        for (AstNode* e = a->item; e; e = e->next, i++) {
            LmdValue value = expression(c, e);
            write_reference(c, {NULL, boxed(c, result, K_ARRAY), constant(c, i), true, i}, value, e);
        }
        return boxed(c, result, K_ARRAY);
    }
    default: diagnostic(c->program, n, "expression outside MVP scope");
        return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    }
}

static void scope_initialize(LmdCompiler* c, NameScope* scope) {
    for (NameEntry* e = scope ? scope->first : NULL; e; e = e->next) {
        LmdBinding* b = binding(c->program, e);
        if (!b || b->owner != c->function || e->is_parameter || b->native || b->intrinsic) continue;
        if (e->is_function_name_binding) {
            write_binding(c, b, boxed(c, c->self, K_FUNCTION), e->node, true);
        } else if (b->target && e->node && e->node->node_type == AST_NODE_FUNC &&
                   b->target->ast->entry == e && !e->is_annex_b_companion) {
            if (b->observed || b->assigned) write_binding(c, b, new_function(c, b->target), e->node, true);
        } else write_binding(c, b, boxed(c, constant(c, e->is_lexical ? ITEM_JS_TDZ : ITEM_JS_UNDEFINED)), e->node, true);
    }
}
static void statements(LmdCompiler* c, AstNode* first) {
    for (AstNode* n = first; n; n = n->next) statement(c, n);
}
static void control_completion(LmdCompiler* c) {
    // if/iteration/switch initialize their own script completion to undefined.
    if (!c->function->id) move(c, c->em.frame.return_reg, integer(c, ITEM_JS_UNDEFINED));
}
static void loop(LmdCompiler* c, AstLoopControlNode* n, const char* name = NULL, int length = 0) {
    control_completion(c);
    scope_initialize(c, n->vars);
    MIR_label_t start = label(c), test = label(c), next = label(c), done = label(c);
    LmdControl control = {c->control, done, next, name, length, (AstNode*)n}; c->control = &control;
    for (LmdControl* outer = control.parent; outer; outer = outer->parent) {
        if (outer->loop_target == (AstNode*)n) { outer->next = next; outer->stop = done; }
    }
    if (n->init) {
        if (n->init->node_type == AST_NODE_VAR_STAM) statement(c, n->init);
        else (void)expression(c, n->init);
    }
    if (n->form != LOOP_FORM_DO_WHILE) jump(c, test);
    put_label(c, start); statement(c, n->body);
    put_label(c, next); if (n->update) (void)expression(c, n->update);
    put_label(c, test);
    if (n->cond) branch_truth(c, start, truth(c, expression(c, n->cond)));
    else jump(c, start);
    put_label(c, done); c->control = control.parent;
}
static void statement(LmdCompiler* c, AstNode* n) {
    if (!n) return;
    switch (n->node_type) {
    case AST_SCRIPT:
        scope_initialize(c, ((AstScript*)n)->global_vars); statements(c, ((AstScript*)n)->body); break;
    case AST_NODE_BLOCK:
        scope_initialize(c, ((AstBlockNode*)n)->vars); statements(c, ((AstBlockNode*)n)->statements); break;
    case AST_NODE_NULL: break;
    case AST_NODE_FUNC: {
        AstFuncNode* f = (AstFuncNode*)n;
        LmdBinding* b = binding(c->program, f->entry);
        if (b && b->entry->annex_b_outer_binding) {
            LmdBinding* outer = binding(c->program, b->entry->annex_b_outer_binding);
            write_binding(c, outer, read_binding(c, b, n), n, true);
        }
        break;
    }
    case AST_NODE_VAR_STAM:
        for (AstNode* d = ((AstVarDeclNode*)n)->declarations; d; d = d->next) {
            AstDeclaratorNode* declaration = (AstDeclaratorNode*)d;
            LmdBinding* b = binding(c->program, ((AstIdentNode*)declaration->id)->entry);
            if (declaration->init || ((AstVarDeclNode*)n)->kind != JS_VAR_VAR)
                write_binding(c, b, expression(c, declaration->init), d, true);
        }
        break;
    case AST_NODE_EXPR_STMT: {
        LmdValue evaluated = expression(c, ((AstExprStmtNode*)n)->expression);
        if (c->function->id) break;
        LmdValue v = box(c, evaluated);
        // retain the latest expression completion; declarations do not reset it.
        move(c, c->em.frame.return_reg, reg(c, v.reg)); break;
    }
    case AST_NODE_RETURN_STAM:
        em_stage_function_return(&c->em, reg(c, box(c, expression(c, ((AstReturnNode*)n)->value)).reg)); break;
    case AST_NODE_IF_EXPR: {
        control_completion(c);
        JsIfNode* f = (JsIfNode*)n;
        MIR_label_t alternate = label(c), done = label(c);
        branch_truth(c, alternate, truth(c, expression(c, f->test)), false);
        scope_initialize(c, f->consequent_vars); statement(c, f->then); jump(c, done);
        put_label(c, alternate); scope_initialize(c, f->alternate_vars); statement(c, f->otherwise);
        put_label(c, done); break;
    }
    case AST_NODE_LOOP: loop(c, (AstLoopControlNode*)n); break;
    case AST_NODE_MATCH_EXPR: {
        control_completion(c);
        JsSwitchNode* s = (JsSwitchNode*)n;
        LmdValue discriminant = box(c, expression(c, s->discriminant));
        scope_initialize(c, s->vars);
        int count = ast_linked_node_count(s->cases);
        MIR_label_t* cases = (MIR_label_t*)mem_calloc(count, sizeof(MIR_label_t), MEM_CAT_TEMP);
        MIR_label_t done = label(c), fallback = done;
        LmdControl control = {c->control, done, NULL, NULL, 0, NULL}; c->control = &control;
        int i = 0;
        for (AstNode* a = s->cases; a; a = a->next, i++) {
            cases[i] = label(c); AstMatchArm* arm = (AstMatchArm*)a;
            if (!arm->test) fallback = cases[i];
            else branch_truth(c, cases[i], equal(c, discriminant, expression(c, arm->test), false).reg);
        }
        jump(c, fallback); i = 0;
        for (AstNode* a = s->cases; a; a = a->next, i++) {
            put_label(c, cases[i]); statements(c, ((AstMatchArm*)a)->body);
        }
        put_label(c, done); c->control = control.parent; mem_free(cases); break;
    }
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: {
        AstBreakContinueNode* b = (AstBreakContinueNode*)n;
        bool next = n->node_type == AST_NODE_CONTINUE_STAM;
        LmdControl* control = c->control;
        for (; control; control = control->parent) {
            if (b->label) {
                if (!control->label || control->label_len != b->label_len ||
                    memcmp(control->label, b->label, b->label_len)) continue;
            } else if (control->label && !control->next) continue;
            if (next && !control->next) continue;
            jump(c, next ? control->next : control->stop); break;
        }
        if (!control) diagnostic(c->program, n, "invalid control target"); break;
    }
    default:
        if (n->node_type == JS_AST_NODE_LABELED_STATEMENT) {
            JsLabeledStatementNode* l = (JsLabeledStatementNode*)n;
            if (l->body->node_type == AST_NODE_LOOP) loop(c, (AstLoopControlNode*)l->body, l->label, l->label_len);
            else {
                MIR_label_t done = label(c);
                AstNode* target = l->body;
                while (target->node_type == JS_AST_NODE_LABELED_STATEMENT)
                    target = ((JsLabeledStatementNode*)target)->body;
                LmdControl control = {c->control, done, NULL, l->label, l->label_len,
                    target->node_type == AST_NODE_LOOP ? target : NULL}; c->control = &control;
                statement(c, l->body); put_label(c, done); c->control = control.parent;
            }
        } else diagnostic(c->program, n, "statement outside MVP scope");
    }
}

struct LmdImport { const char* name; void* address; JitImportMetadata audit; };
#define SCALAR JIT_VALUE_NON_GC_SCALAR
#define ITEM JIT_VALUE_BOXED_ITEM
#define GC_PTR JIT_VALUE_RAW_GC_POINTER
#define RAW_PTR JIT_VALUE_RAW_NON_GC_POINTER
#define ARG(i, cls) JIT_ARG_CLASS(i, cls)
#define AUDIT(gc, cls, args) {gc, JIT_REENTRY_NO, cls, args, \
    JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE) | JIT_IMPORT_NUMBER_STACK_PRESERVES | \
    JIT_IMPORT_ARGS_BORROWED_AUDITED, JIT_EXCEPTION_PRESERVES, 0}
static const LmdImport imports[] = {
    {"mvp_lmd_fail", (void*)mvp_lmd_fail, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"mvp_lmd_string_to_number", (void*)mvp_lmd_string_to_number, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR))},
    {"mvp_lmd_number_to_string", (void*)mvp_lmd_number_to_string, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR))},
    {"mvp_lmd_string_concat", (void*)mvp_lmd_string_concat, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_string_compare", (void*)mvp_lmd_string_compare, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_string_at", (void*)mvp_lmd_string_at, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_number_pow", (void*)mvp_lmd_number_pow, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"mvp_lmd_string_key", (void*)mvp_lmd_string_key, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR))},
    {"mvp_lmd_array_store", (void*)mvp_lmd_array_store, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,ITEM))},
    {"mvp_lmd_function_new", (void*)mvp_lmd_function_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,RAW_PTR))},
    {"array", (void*)array, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, 0)},
    {"array_reserve_append_slots", (void*)array_reserve_append_slots, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"owned_item_slot_store", (void*)owned_item_slot_store, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,SCALAR)|ARG(2,SCALAR)|ARG(3,ITEM))},
    {"lambda_item_adopt_scalar_home", (void*)lambda_item_adopt_scalar_home, AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR))},
    {"utf8_to_utf16_length", (void*)utf8_to_utf16_length, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,SCALAR))},
    {"fmod", (void*)(double (*)(double, double))fmod, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},

};
static bool import_metadata(const char* name, JitImportMetadata* out) {
    for (const LmdImport& entry : imports) if (!strcmp(name, entry.name)) { *out = entry.audit; return true; }
    if (!strcmp(name, "lambda_side_stack_ensure_for") || !strcmp(name, "lambda_stack_is_exhausted"))
        return jit_import_get_metadata(name, out);
    log_error("mvp-lmd-import: rejected import %s", name); abort();
}
static void* import_resolver(const char* name) {
    for (const LmdImport& entry : imports) if (!strcmp(name, entry.name)) return entry.address;
    for (int i = 0; i < jit_runtime_import_count; i++)
        if (!strcmp(name, jit_runtime_imports[i].name) &&
            (!strcmp(name, "lambda_side_stack_ensure_for") || !strcmp(name, "lambda_stack_is_exhausted")))
            return (void*)jit_runtime_imports[i].func;
    log_error("mvp-lmd-link: rejected import %s", name); return NULL;
}

static const MIR_var_t entry_arguments[] = {{MIR_T_P, "runtime", 0}, {MIR_T_P, "unit", 0},
    {MIR_T_P, "arguments", 0}, {MIR_T_I64, "argc", 0}, {MIR_T_I64, "self", 0}};
static bool compile_function(MvpLmdProgram* p, LmdFunction* f) {
    LmdCompiler c = {}; c.program = p; c.function = f;
    c.em.ctx = p->mir; c.em.call_owner = &c; c.em.root_call_value = root_value;
    c.em.lookup_import_metadata = import_metadata; c.em.import_cache = p->import_cache;
    c.em.helper_results_skip_rehome = true;
    c.em.name_pool = p->frontend->name_pool;
    MIR_type_t result_type = MIR_T_I64;
    // the MIR API interns descriptor names; keep the reusable template immutable.
    MIR_var_t arguments[5]; memcpy(arguments, entry_arguments, sizeof(arguments));
    f->item = MIR_new_func_arr(p->mir, f->name, 1, &result_type, 5, arguments);
    c.em.func_item = f->item; c.em.func = MIR_get_item_func(p->mir, f->item);
    MirFrameState* frame = &c.em.frame;
    frame->active = true; frame->number_active = true; frame->item_return = true;
    frame->return_type = MIR_T_I64; frame->scalar_return_mode = SCALAR_RETURN_F64;
    frame->return_lane_kind = RETURN_LANE_SCALAR;
    frame->runtime = MIR_reg(p->mir, "runtime", c.em.func);
    c.unit = MIR_reg(p->mir, "unit", c.em.func);
    c.arguments = MIR_reg(p->mir, "arguments", c.em.func);
    c.argc = MIR_reg(p->mir, "argc", c.em.func);
    c.self = MIR_reg(p->mir, "self", c.em.func);
    for (const MIR_var_t& arg : entry_arguments)
        em_function_argument_register(&c.em, MIR_reg(p->mir, arg.name, c.em.func));
    frame->root_base = em_new_reg(&c.em, "root_frame", MIR_T_I64);
    frame->root_end = em_new_reg(&c.em, "root_end", MIR_T_I64);
    frame->number_base = em_new_reg(&c.em, "number_frame", MIR_T_I64);
    frame->return_reg = em_new_reg(&c.em, "completion", MIR_T_I64);
    root_value(&c, frame->return_reg, JIT_VALUE_BOXED_ITEM);
    frame->anchor = label(&c); frame->return_label = label(&c);
    frame->plan.entry_kind = FN_ENTRY_BOXED_BODY;
    frame->plan.entry_mode = MIR_ENTRY_CHECKED; frame->plan.debug_name = f->name;
    em_plan_bind_return(&frame->plan, NULL, true);
    put_label(&c, frame->anchor);
    move(&c, frame->return_reg, integer(&c, ITEM_JS_UNDEFINED));
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        b->scalar_home = 0;
        if (b->owner != f || (!f->id && !b->native)) continue;
        b->reg = em_new_reg(&c.em, "binding", b->native && b->kinds == K_NUMBER ? MIR_T_D : MIR_T_I64);
        if (!b->native) root_value(&c, b->reg, JIT_VALUE_BOXED_ITEM);
    }
    if (f->ast) {
        c.strict = f->ast->has_use_strict_directive || (f->ast->vars && f->ast->vars->strict);
        scope_initialize(&c, f->ast->vars);
        if (f->ast->vars && f->ast->vars->parent && f->ast->vars->parent->is_function_name_scope)
            scope_initialize(&c, f->ast->vars->parent);
        uint64_t i = 0;
        for (AstNode* a = f->ast->params; a; a = a->next, i++) {
            AstIdentNode* id = js_ast_parameter_binding_identifier(a);
            LmdBinding* b = binding(p, id->entry);
            MIR_reg_t value = em_new_reg(&c.em, "parameter", MIR_T_I64);
            if (f->closed_calls && !(b->kinds & K_UNDEFINED)) {
                move(&c, value, reg(&c, em_load_at(&c.em, c.arguments, i * sizeof(Item), MIR_T_I64, "argument")));
            } else {
                MIR_label_t absent = label(&c), done = label(&c);
                branch(&c, MIR_UBLE, absent, reg(&c, c.argc), integer(&c, i));
                move(&c, value, reg(&c, em_load_at(&c.em, c.arguments, i * sizeof(Item), MIR_T_I64, "argument")));
                jump(&c, done); put_label(&c, absent); move(&c, value, integer(&c, ITEM_JS_UNDEFINED));
                put_label(&c, done);
            }
            write_binding(&c, b, boxed(&c, value, b->kinds), a, true);
        }
        if (f->ast->body->node_type == AST_NODE_BLOCK) {
            statement(&c, f->ast->body);
            em_stage_function_return(&c.em, integer(&c, ITEM_JS_UNDEFINED));
        } else em_stage_function_return(&c.em, reg(&c, box(&c, expression(&c, f->ast->body)).reg));
    } else {
        c.strict = ((AstScript*)p->frontend->ast_root)->has_use_strict_directive;
        statement(&c, p->frontend->ast_root); jump(&c, frame->return_label);
    }
    put_label(&c, frame->return_label);
    MIR_reg_t result, companion;
    em_build_pending_pair(&c.em, frame->return_reg, &result, &companion);
    em_store_frame_top(&c.em, frame->runtime, offsetof(Context, mir_companion_slot), companion);
    em_store_frame_top(&c.em, frame->runtime, offsetof(Context, side_root_top), frame->root_base);
    em_store_frame_top(&c.em, frame->runtime, offsetof(Context, side_number_top), frame->number_base);
    em_emit_insn(&c.em, MIR_new_ret_insn(p->mir, 1, reg(&c, result)));
    frame->fixed_root_slots = c.argument_count;
    MirRootWriteBackResult roots = {};
    if (!em_finalize_semantic_root_write_back(&c.em, frame->root_base, frame->anchor, false, 0,
        &frame->gc_candidates, &frame->gc_candidate_count, &frame->gc_candidate_capacity,
        &frame->gc_candidate_by_reg, &frame->gc_candidate_by_reg_capacity,
        frame->gc_call_sites, frame->gc_call_site_count, &roots, f->name)) {
        diagnostic(p, (AstNode*)f->ast, "root planning failed");
    }
    frame->root_slot_count = roots.stable_slots + roots.scratch_slots;
    frame->root_store_count = roots.inserted_stores;
    if (c.argument_fixup) c.argument_fixup->ops[2].u.i = (int64_t)frame->root_slot_count * sizeof(Item);
    frame->root_slot_count += c.argument_count;
    em_finalize_scalar_homes(&c.em);
    em_finalize_frame_prologue(&c.em, frame->plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit), offsetof(Context, side_number_commit_limit));
    // reservation/stack failure has no frame to unwind and allocates nothing.
    em_store_at(&c.em, frame->runtime, offsetof(Context, mir_companion_slot), MIR_T_I64, constant(&c, 0));
    em_emit_insn(&c.em, MIR_new_ret_insn(p->mir, 1, integer(&c, ITEM_ERROR)));
    em_finalize_function_metadata(&c.em);
    MIR_finish_func(p->mir);
    em_frame_dispose(&c.em);
    return !p->diagnostic[0];
}
extern "C" Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* p) {
    LmdFunction* f = (LmdFunction*)p->functions->data[code_id];
    Function* value = (Function*)heap_calloc(sizeof(Function), LMD_TYPE_FUNC);
    if (!value) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function_init_abi(value, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
    value->arity = (uint8_t)ast_linked_node_count(f->ast->params);
    value->ptr = (fn_ptr)f->address;
    value->runtime_context = context;
    value->def = f->ast;
    return Item{.item = (uint64_t)value};
}
static int admission_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    p->functions = arraylist_new(8); p->bindings = arraylist_new(16);
    LmdFunction* main = (LmdFunction*)mem_calloc(1, sizeof(LmdFunction), MEM_CAT_TEMP);
    str_copy(main->name, sizeof(main->name), "mvp_lmd_program", 15);
    arraylist_append(p->functions, main);
    p->intrinsic_uses = (LmdBinding**)mem_calloc(p->frontend->ast_index.count,
        sizeof(LmdBinding*), MEM_CAT_TEMP);
    for (int i = 0; i < 3; i++) {
        p->intrinsics[i].owner = main; p->intrinsics[i].intrinsic = (uint8_t)i + 1;
        p->intrinsics[i].kinds = i ? K_NUMBER : K_UNDEFINED;
    }
    LmdWalk gather = {p, main, NULL, false}; walk(p->frontend->ast_root, &gather);
    gather.facts = true; walk(p->frontend->ast_root, &gather);
    return !p->diagnostic[0];
}
static int representation_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    // only stable declarations used exclusively as direct callees have a closed incoming domain.
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->target && !b->assigned && !b->observed && b->entry->node &&
                b->entry->node->node_type == AST_NODE_FUNC && !b->entry->is_annex_b_companion)
            b->target->closed_calls = true;
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->entry->is_parameter && b->owner->closed_calls) b->kinds = 0;
    }
    LmdTypes types = {p, true};
    while (types.changed) { types.changed = false; type_walk(p->frontend->ast_root, &types); }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->kinds) b->kinds = K_ANY;
        b->native = !b->external && (b->kinds == K_NUMBER || b->kinds == K_BOOL) &&
            (b->entry->is_parameter ? b->owner->closed_calls : b->initializer && b->dominated);
    }
    return true;
}
static int lower_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    p->import_cache = em_import_cache_new(32);
    p->mir = MIR_init(); p->module = MIR_new_module(p->mir, "js_mvp_lmd");
    MIR_type_t result_type = MIR_T_I64;
    MIR_var_t arguments[5]; memcpy(arguments, entry_arguments, sizeof(arguments));
    p->entry.proto = MIR_new_proto_arr(p->mir, "mvp_lmd_signature", 1, &result_type, 5, arguments);
    p->entry.audit = (JitImportMetadata)AUDIT(JIT_EFFECT_MAY_GC, SCALAR,
        ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,RAW_PTR)|ARG(3,SCALAR)|ARG(4,ITEM));
    em_normalize_import_call(&p->entry, MIR_T_I64, 5, arguments, 1);
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->forward = MIR_new_forward(p->mir, f->name);
    }
    bool ok = true;
    for (int i = 0; i < p->functions->length && ok; i++) ok = compile_function(p, (LmdFunction*)p->functions->data[i]);
    MIR_finish_module(p->mir); mir_dump_finalized(p->mir, NULL, false);
    if (!ok) return false;
    MIR_load_module(p->mir, p->module); MIR_gen_init(p->mir); p->linked = true;
    MIR_gen_set_optimize_level(p->mir, 2); MIR_link(p->mir, MIR_set_gen_interface, import_resolver);
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i]; f->address = MIR_gen(p->mir, f->item);
    }
    return true;
}
static bool compile(MvpLmdProgram* p, const char* source, size_t length) {
    p->frontend = js_transpiler_create(NULL);
    if (!p->frontend) return false;
    if (length > UINT32_MAX) { diagnostic(p, NULL, "source is too large"); return false; }
    // retained AST definitions and diagnostics need source storage for the execution's lifetime.
    char* retained = (char*)pool_alloc(p->frontend->pool, length + 1);
    if (!retained) { diagnostic(p, NULL, "source allocation failed"); return false; }
    memcpy(retained, source, length); retained[length] = '\0';
    p->frontend->strict_js = true;
    if (!js_transpiler_parse_c(p->frontend, retained, length, JS_PARSE_SCRIPT) ||
            p->frontend->has_errors || !p->frontend->ast_root) {
        diagnostic(p, NULL, "JS parsing or validation failed"); return false;
    }
    // continue the retained frontend schedule; lowering consumes its bound, validated index.
    CompilerPassManager* manager = &p->frontend->pass_manager;
    const CompilerPassSpec passes[] = {
        {"mvp-admit", COMPILER_FACT_FRONTEND | COMPILER_FACT_INDEXED,
            COMPILER_FACT_COLLECTED | COMPILER_FACT_CAPTURES | COMPILER_FACT_ENV_LAYOUT,
            admission_pass, p},
        {"mvp-plan", COMPILER_FACT_COLLECTED | COMPILER_FACT_CAPTURES | COMPILER_FACT_ENV_LAYOUT,
            COMPILER_FACT_INFERRED | COMPILER_FACT_ANALYZED | COMPILER_FACT_PLANNED,
            representation_pass, p},
        {"mvp-lower", COMPILER_FACT_PLANNED,
            COMPILER_FACT_MIR_LOWERED | COMPILER_FACT_FINALIZED | COMPILER_FACT_LINKED,
            lower_pass, p},
    };
    for (const CompilerPassSpec& pass : passes)
        if (!compiler_pass_manager_add(manager, &pass)) return false;
    return compiler_pass_manager_run(manager, NULL);
}
MvpLmdExecution* mvp_lmd_execute(const char* source, size_t length, double* execution_ms) {
    if (execution_ms) *execution_ms = 0;
    MvpLmdExecution* e = (MvpLmdExecution*)mem_calloc(1, sizeof(MvpLmdExecution), MEM_CAT_EVAL);
    if (!e) return NULL;
    e->result = Item{.item = ITEM_ERROR};
    if (!source || !compile(&e->program, source, length)) return e;
    if (context) { diagnostic(&e->program, NULL, "execution needs its own unbound evaluator thread"); return e; }
    lambda_stack_init(); e->context.stack_limit = _lambda_stack_limit;
    if (!eval_context_init(&e->context)) return e;
    e->active = true; heap_init();
    if (!e->context.heap) return e;
    MvpLmdProgram* p = &e->program;
    if (p->slot_count) {
        p->slots = (Item*)mem_calloc((size_t)p->slot_count * 2, sizeof(Item), MEM_CAT_EVAL);
        if (!p->slots || !heap_try_register_gc_root_range((uint64_t*)p->slots, (int)p->slot_count)) return e;
        e->registered = true;
    }
    LmdFunction* main = (LmdFunction*)p->functions->data[0];
    typedef Item (*Entry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item);
    uint64_t started = execution_ms ? time_now_ns() : 0;
    Item result = ((Entry)main->address)(&e->context, p, NULL, 0, Item{.item = ITEM_JS_UNDEFINED});
    if (execution_ms) *execution_ms = time_elapsed_ms_f(started, time_now_ns());
    result = lambda_item_resolve_pending_slot(result);
    e->result = runtime_publish_result(&e->context, result);
    return e;
}
Item mvp_lmd_result(const MvpLmdExecution* e) { return e ? e->result : Item{.item = ITEM_ERROR}; }
const char* mvp_lmd_diagnostic(const MvpLmdExecution* e) {
    if (!e) return "MVP allocation failed";
    if (e->program.diagnostic[0]) return e->program.diagnostic;
    if (get_type_id(e->result) == LMD_TYPE_ERROR) {
        LambdaError* error = it2err(e->result);
        return error && error->message ? error->message : "MVP execution failed";
    }
    return NULL;
}
void mvp_lmd_dump(const MvpLmdExecution* e, FILE* output) {
    if (e && e->program.mir && output) MIR_output(e->program.mir, output);
}
void mvp_lmd_destroy(MvpLmdExecution* e) {
    if (!e) return;
    MvpLmdProgram* p = &e->program;
    if (e->active) {
        if (e->registered) heap_unregister_gc_root_range((uint64_t*)p->slots);
        if (e->context.heap) heap_destroy();
        eval_context_shutdown(&e->context);
    }
    mem_free(p->slots); mem_free(p->intrinsic_uses);
    if (p->mir) { if (p->linked) MIR_gen_finish(p->mir); MIR_finish(p->mir); }
    if (p->functions) {
        for (int i = 0; i < p->functions->length; i++) mem_free(p->functions->data[i]);
        arraylist_free(p->functions);
    }
    if (p->bindings) {
        for (int i = 0; i < p->bindings->length; i++) mem_free(p->bindings->data[i]);
        arraylist_free(p->bindings);
    }
    if (p->import_cache) hashmap_free(p->import_cache);
    if (p->frontend) js_transpiler_destroy(p->frontend);
    mem_free(e);
}
