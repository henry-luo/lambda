#include "mvp_lmd.h"
#include "mvp_lmd_runtime.h"
#include "../js_transpiler.hpp"
#include "../../runtime/mir_emitter_shared.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/heap_api.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/lambda-error.h"
#include "../../input/input.hpp"
#include "../../mir/mir-gen.h"
#include "../../../lib/mem.h"
#include "../../../lib/utf.h"
#include "../../../lib/time_util.h"
#include <math.h>

enum LmdKind { K_UNDEFINED = 1, K_NULL = 2, K_BOOL = 4, K_NUMBER = 8,
    K_STRING = 16, K_ARRAY = 32, K_FUNCTION = 64, K_OBJECT = 128, K_MAP = 256,
    K_TYPED_ARRAY = 512, K_ANY = 1023, K_BOXED = 1024 };
enum LmdIntrinsic { I_UNDEFINED = 1, I_NAN, I_INFINITY, I_MAP, I_OBJECT,
    I_MATH, I_INT32_ARRAY, I_UINT8_ARRAY, I_FLOAT64_ARRAY, I_ARRAY, I_STRING, I_ERROR, I_COUNT };
static const char* intrinsic_names[] = {"undefined", "NaN", "Infinity", "Map", "Object",
    "Math", "Int32Array", "Uint8Array", "Float64Array", "Array", "String", "Error"};
static const ArrayNumElemType typed_lanes[] = {ELEM_INT32, ELEM_UINT8, ELEM_FLOAT64};
static const struct { const char* name; const char* native; int arity; } math_operations[] = {
    {"sqrt", "sqrt", 1}, {"sin", "sin", 1}, {"floor", "floor", 1}, {"trunc", "trunc", 1},
    {"abs", "fn_abs_f", 1}, {"min", "fn_min2_u", 2}, {"max", "fn_max2_u", 2},
    {"ceil", "ceil", 1}, {"cos", "cos", 1}};
struct LmdRange { double lower = 0, upper = 0; unsigned state = 0; };
// lane bits follow typed_lanes; ITEMS admits only proven unchanged numeric literals.
enum { LMD_ARRAY_ITEMS = 8, LMD_ARRAY_UNKNOWN = 16 };
struct LmdArrayFacts { unsigned lanes = 0; LmdRange length; LmdRange elements; };
struct LmdFunction;
struct LmdObjectPlan;
struct LmdShapeSet { int count; LmdObjectPlan* plans[4]; };
struct LmdValue;
struct LmdScalarField;
struct LmdInlineFrame;
struct LmdBinding {
    NameEntry* entry;
    LmdFunction* owner;
    LmdFunction* target;
    AstNode* initializer;
    uint32_t slot;
    unsigned kinds;
    unsigned seed_kinds;
    LmdRange range;
    AstNode* update;
    unsigned writes;
    unsigned range_changes;
    bool assigned;
    bool observed;
    bool external;
    bool native;
    bool integer;
    uint8_t intrinsic;
    bool dominated;
    MIR_reg_t reg;
    int scalar_home;
    LmdObjectPlan* shape_hint;
    LmdShapeSet shapes;
    LmdArrayFacts array;
    unsigned array_changes;
    bool array_observed;
    MIR_reg_t number_present;
    LmdRange present_range;
    unsigned present_range_changes;
    bool class_name;
};
struct LmdFunction {
    AstFuncNode* ast;
    MIR_item_t item;
    MIR_item_t forward;
    char name[40];
    uint32_t id;
    void* address;
    bool closed_calls;
    bool direct_args;
    LmdObjectPlan* shape_hint;
    LmdShapeSet shapes;
    bool native;
    bool integer;
    bool integer_region;
    unsigned returns;
    LmdRange range;
    unsigned range_changes;
    FnReturnAnalysis return_abi;
    MirImportEntry entry;
    MIR_var_t signature[LAMBDA_MAX_FUNCTION_ARGS];
    int arity;
    LmdArrayFacts array;
    unsigned array_changes;
    MvpLmdClass* home;
};
struct LmdObjectPlan { AstNode* literal; TypeMap* blueprint; TypeMap* shape;
    LmdBinding* scalar_binding; LmdScalarField* scalar_fields; };
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
    LmdBinding intrinsics[I_COUNT - 1];
    TypeNominal families[2];
    TypeMap* roots[2];
    ArrayList* objects;
    LmdBinding** intrinsic_uses;
    HashMap* import_cache;
    unsigned* literal_kinds;
    Item* character_items;
    bool immutable_objects;
    bool array_holes;
    ArrayList* classes;
    bool receiver_abi;
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
    MIR_reg_t iteration_owner = 0;
    MIR_reg_t iteration_active = 0;
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
    MIR_label_t tail_entry;
    LmdControl* control;
    bool strict;
    LmdInlineFrame* inlining;
    MIR_label_t integer_bail;
    MIR_reg_t receiver;
    MIR_reg_t new_target;
};
struct LmdValue { MIR_reg_t reg; unsigned kind; bool literal = false; double number = 0; bool integer = false; LmdRange range;
    MIR_reg_t number_present = 0; };
struct LmdScalarField { LmdValue value; unsigned kinds; bool written; bool integer;
    LmdRange range; int home; AstNode* initializer; };
struct LmdInlineFrame { LmdInlineFrame* parent; LmdFunction* function; int count;
    LmdBinding* bindings[32]; LmdScalarField slots[32];
    LmdObjectPlan* plans[32]; bool guarded[32]; };
struct LmdReference { LmdBinding* binding; LmdValue owner; MIR_reg_t key;
    bool key_known = false; int64_t index = 0; MIR_reg_t name = 0; LmdObjectPlan* plan = NULL; ShapeEntry* field = NULL; int method = 0; LmdShapeSet shapes = {}; String* spelling = NULL; bool guarded = false;
    unsigned lanes = 0; bool numeric_key = false; bool in_bounds = false; LmdRange elements;
    MIR_reg_t key_present = 0; String* string_literal = NULL; };

static LmdBinding* binding(MvpLmdProgram* p, NameEntry* entry) {
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->entry == entry) return b;
    }
    return NULL;
}
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
static String* member_spelling(AstFieldNode* member) {
    if (!member->computed) return ((AstIdentNode*)member->property)->name;
    return member->property->node_type == AST_NODE_LITERAL &&
        ((AstLiteralNode*)member->property)->literal_type == AST_LITERAL_STRING
            ? ((AstLiteralNode*)member->property)->value.string_value : NULL;
}
static int math_operation(MvpLmdProgram* p, AstCallNode* call) {
    if (call->callee->node_type != AST_NODE_MEMBER_EXPR && call->callee->node_type != AST_NODE_INDEX_EXPR)
        return -1;
    AstFieldNode* member = (AstFieldNode*)call->callee;
    LmdBinding* owner = member->object->node_type == AST_NODE_IDENT
        ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
    if (!owner || owner->intrinsic != I_MATH) return -1;
    String* key = member_spelling(member);
    for (int i = 0; i < (int)(sizeof(math_operations) / sizeof(math_operations[0])); i++)
        if (named(key, math_operations[i].name)) return i;
    return -1;
}
static MvpLmdClass* class_plan(MvpLmdProgram* p, AstClassNode* ast) {
    for (int i = 0; i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        if (cls->ast == ast) return cls;
    }
    MvpLmdClass* cls = (MvpLmdClass*)pool_calloc(p->frontend->pool, sizeof(MvpLmdClass));
    if (!cls) return NULL;
    cls->ast = ast; cls->program = p; cls->constructor_id = -1;
    cls->slot = p->slot_count; p->slot_count += 3;
    cls->nominal.type_name = {ast->name->chars, ast->name->len};
    cls->nominal.struct_kind = LMD_TYPE_MAP;
    cls->nominal.extension = &mvp_lmd_class_extension; cls->nominal.extension_data = cls;
    cls->prototype_nominal = cls->nominal;
    TypeMap* shapes[] = {&cls->shape, &cls->prototype_shape, &cls->static_shape};
    for (int i = 0; i < 3; i++) {
        shapes[i]->type_id = LMD_TYPE_MAP; shapes[i]->type_index = -1;
        shapes[i]->is_nominal = true;
        shapes[i]->nominal = i ? &cls->prototype_nominal : &cls->nominal;
    }
    arraylist_append(p->classes, cls);
    return cls;
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
    case AST_NODE_FOR_OF_STAM: {
        JsForOfNode* loop = (JsForOfNode*)n;
        collect_scope(p, state->owner, loop->vars);
        if (loop->is_await || loop->init) diagnostic(p, n, "for-of initializer/await");
        AstNode* head = loop->left;
        if (head->node_type == AST_NODE_ARRAY_PATTERN) {
            AstArrayNode* pair = (AstArrayNode*)head;
            if (pair->length != 2 || ast_linked_node_count(pair->item) != 2)
                diagnostic(p, head, "for-of pair binding");
            for (AstNode* e = pair->item; e; e = e->next)
                if (e->node_type != AST_NODE_IDENT) diagnostic(p, e, "for-of pair binding");
        } else if (head->node_type != AST_NODE_IDENT) diagnostic(p, head, "for-of binding");
        AstNode* first = head->node_type == AST_NODE_ARRAY_PATTERN ? ((AstArrayNode*)head)->item : head;
        for (AstNode* e = first; e; e = head->node_type == AST_NODE_ARRAY_PATTERN ? e->next : NULL) {
            if (e->node_type != AST_NODE_IDENT) continue;
            LmdBinding* binding = identifier_binding(p, (AstIdentNode*)e);
            if (binding) { binding->kinds = K_ANY; binding->assigned = true; binding->dominated = false; }
        }
        break;
    }
    case AST_NODE_ARRAY_PATTERN: break;
    case AST_NODE_CLASS: {
        AstClassNode* cls = (AstClassNode*)n;
        if (!state->parent || state->parent->node_type != AST_SCRIPT || !cls->name) {
            diagnostic(p, n, "classes require a named top-level declaration"); return;
        }
        p->receiver_abi = true; p->immutable_objects = false;
        if (!class_plan(p, cls)) { diagnostic(p, n, "class allocation"); return; }
        collect_scope(p, state->owner, cls->entry ? cls->entry->scope : NULL);
        NameEntry* names[] = {cls->entry, cls->outer_entry};
        for (NameEntry* entry : names) {
            LmdBinding* b = binding(p, entry);
            if (b) { b->kinds = K_FUNCTION; b->class_name = true; }
        }
        walk(cls->superclass, &inner); walk(cls->body, &inner); return;
    }
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL:
        for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next)
            if (e->node_type != AST_NODE_PROPERTY) diagnostic(p, e, "object spread");
        break;
    case AST_NODE_PROPERTY: {
        AstPropertyNode* prop = (AstPropertyNode*)n;
        if (prop->computed || prop->key->node_type != AST_NODE_IDENT) p->immutable_objects = false;
        if (prop->method || prop->is_getter || prop->is_setter) diagnostic(p, n, "object method/accessor");
        if (prop->computed) walk(prop->key, &inner);
        else if (prop->key->node_type == AST_NODE_IDENT && named(((AstIdentNode*)prop->key)->name, "__proto__"))
            diagnostic(p, n, "__proto__");
        else if (prop->key->node_type == AST_NODE_LITERAL &&
                 ((AstLiteralNode*)prop->key)->literal_type == AST_LITERAL_STRING &&
                 named(((AstLiteralNode*)prop->key)->value.string_value, "__proto__")) diagnostic(p, n, "__proto__");
        walk(prop->value, &inner); return;
    }
    case AST_NODE_NEW_EXPR:
        if (((AstCallNode*)n)->callee->node_type == AST_NODE_IDENT &&
                named(((AstIdentNode*)((AstCallNode*)n)->callee)->name, "Array")) p->array_holes = true;
        break;
    case AST_NODE_IF_EXPR:
        collect_scope(p, state->owner, ((JsIfNode*)n)->consequent_vars);
        collect_scope(p, state->owner, ((JsIfNode*)n)->alternate_vars); break;
    case AST_NODE_MATCH_EXPR:
        collect_scope(p, state->owner, ((JsSwitchNode*)n)->vars); break;
    case AST_NODE_METHOD:
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
        if (n->node_type == AST_NODE_METHOD) {
            AstMethodNode* method = (AstMethodNode*)n;
            AstNode* parent = ast_index_parent(&p->frontend->ast_index, state->parent);
            if (!parent || parent->node_type != AST_NODE_CLASS || method->computed ||
                    method->key->node_type != AST_NODE_IDENT ||
                    method->kind == AstMethodNode::JS_METHOD_GET || method->kind == AstMethodNode::JS_METHOD_SET ||
                    named(((AstIdentNode*)method->key)->name, "__proto__") ||
                    ((AstIdentNode*)method->key)->name->chars[0] == '#') {
                diagnostic(p, n, "class member outside basic method scope"); return;
            }
            f->home = class_plan(p, (AstClassNode*)parent);
            if (method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) f->home->constructor_id = f->id;
        }
        JsAstParameterFacts params = js_ast_collect_parameter_facts(ast->params);
        JsAstFunctionFacts facts = js_ast_collect_function_facts(ast->params, ast->body);
        if (ast->is_async || ast->is_generator || params.has_non_simple_params ||
                (facts.observations & (f->home ? JS_AST_OBSERVES_ARGUMENTS : 255)) ||
                facts.has_direct_eval || facts.has_with)
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
        if (f->home) {
            for (AstNode* a = ast->params; a; a = a->next) walk(a, &inner);
            walk(ast->body, &inner); return;
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
                if (!state->facts) b->writes++;
                if (b->initializer && b->initializer != d->init) b->assigned = true;
                else if (!b->initializer) {
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
            for (int i = 0; i < I_COUNT - 1; i++) if (named(id->name, intrinsic_names[i]))
                p->intrinsic_uses[ast_index_find(&p->frontend->ast_index, n)] = &p->intrinsics[i];
        }
        LmdBinding* b = identifier_binding(p, id);
        bool declaration_target = state->parent &&
            ((state->parent->node_type == AST_NODE_VARIABLE_DECLARATOR &&
              ((AstDeclaratorNode*)state->parent)->id == n) ||
             ((state->parent->node_type == AST_NODE_FUNC || state->parent->node_type == AST_NODE_FUNC_EXPR ||
               state->parent->node_type == AST_NODE_ARROW_FUNC || state->parent->node_type == AST_NODE_METHOD) &&
              ((AstFuncNode*)state->parent)->body != n));
        if (state->facts && b && !declaration_target) {
            AstNode* parent = state->parent;
            AstNode* grand = parent ? ast_index_parent(&p->frontend->ast_index, parent) : NULL;
            bool member = parent && (parent->node_type == AST_NODE_MEMBER_EXPR ||
                parent->node_type == AST_NODE_INDEX_EXPR) && ((AstFieldNode*)parent)->object == n;
            bool write = grand && ((grand->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)grand)->left == parent) ||
                (grand->node_type == AST_NODE_UNARY && (((AstUnaryNode*)grand)->op == OPERATOR_JS_DELETE ||
                 ((AstUnaryNode*)grand)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)grand)->op == OPERATOR_JS_DECREMENT)) ||
                (grand->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)grand)->callee == parent));
            if (!member || write) b->array_observed = true;
            if (!b->class_name && b->owner != state->owner && (b->owner->id ||
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
        AstNode* parent = state->parent;
        bool write = parent && ((parent->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)parent)->left == n) ||
                (parent->node_type == AST_NODE_UNARY && (((AstUnaryNode*)parent)->op == OPERATOR_JS_DELETE ||
                 ((AstUnaryNode*)parent)->op == OPERATOR_JS_INCREMENT || ((AstUnaryNode*)parent)->op == OPERATOR_JS_DECREMENT)));
        if (write) p->immutable_objects = false;
        if (field->object->node_type == AST_NODE_IDENT && named(((AstIdentNode*)field->object)->name, "super") &&
                (write || field->computed))
            diagnostic(p, n, "super property mutation or computed key");
        if (field->optional) diagnostic(p, n, "optional member");
        walk(field->object, &inner);
        if (field->computed) walk(field->property, &inner);
        else if (!field->property || field->property->node_type != AST_NODE_IDENT)
            diagnostic(p, n, "invalid named property");
        else if (named(((AstIdentNode*)field->property)->name, "__proto__")) diagnostic(p, n, "__proto__");
        return;
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        if (call->callee->node_type == AST_NODE_IDENT && named(((AstIdentNode*)call->callee)->name, "Array"))
            p->array_holes = true;
        if (call->optional) diagnostic(p, n, "optional call");
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR || call->callee->node_type == AST_NODE_INDEX_EXPR) {
            String* method = member_spelling((AstFieldNode*)call->callee);
            // method mutation invalidates the same closed-unit literal facts as indexed writes.
            if (!method || named(method, "push") || named(method, "pop") || named(method, "fill"))
                p->immutable_objects = false;
        }
        break;
    }
    case AST_NODE_ASSIGN:
        if (((AstAssignNode*)n)->left && (((AstAssignNode*)n)->left->node_type == AST_NODE_ARRAY ||
                ((AstAssignNode*)n)->left->node_type == AST_NODE_ARRAY_PATTERN)) {
            AstAssignNode* assignment = (AstAssignNode*)n;
            if (assignment->op != OPERATOR_ASSIGN) diagnostic(p, n, "compound destructuring");
            for (AstNode* e = ((AstArrayNode*)assignment->left)->item; e; e = e->next) {
                if (e->node_type != AST_NODE_IDENT) { diagnostic(p, e, "destructuring target"); continue; }
                LmdBinding* b = binding(p, ((AstIdentNode*)e)->entry);
                if (b) {
                    b->assigned = true;
                    // literal RHS elements have independent facts; an arbitrary array may contain anything.
                    if (assignment->right->node_type != AST_NODE_ARRAY) {
                        b->kinds = K_ANY; b->dominated = false;
                        b->array = {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
                    }
                    if (!state->facts) b->writes++;
                }
            }
        } else if (((AstAssignNode*)n)->left && ((AstAssignNode*)n)->left->node_type == AST_NODE_IDENT) {
            LmdBinding* b = binding(p, ((AstIdentNode*)((AstAssignNode*)n)->left)->entry);
            if (b) {
                b->assigned = true;
                if (!state->facts) { b->writes++; b->update = n; }
            }
        }
        break;
    case AST_NODE_UNARY:
        if (((AstUnaryNode*)n)->op == OPERATOR_JS_DELETE) p->array_holes = true;
        if (!state->facts && (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
                ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT) &&
                ((AstUnaryNode*)n)->operand->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)((AstUnaryNode*)n)->operand);
            if (b) { b->writes++; b->update = n; }
        }
        if (((AstUnaryNode*)n)->op == OPERATOR_JS_DELETE &&
                ((AstUnaryNode*)n)->operand->node_type != AST_NODE_MEMBER_EXPR &&
                ((AstUnaryNode*)n)->operand->node_type != AST_NODE_INDEX_EXPR)
            diagnostic(p, n, "delete target"); break;
    case AST_NODE_BINARY: break;
    case AST_NODE_ARRAY:
        for (AstNode* e = ((AstArrayNode*)n)->item; e; e = e->next)
            if (e->node_type == AST_NODE_NULL) diagnostic(p, e, "sparse array literal");
        if (ast_linked_node_count(((AstArrayNode*)n)->item) != ((AstArrayNode*)n)->length)
            diagnostic(p, n, "sparse array literal"); break;
    case AST_NODE_NULL: case AST_NODE_EXPR_STMT: case AST_NODE_RETURN_STAM: case AST_NODE_RAISE_STAM:
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM: case AST_NODE_SEQ:
    case AST_NODE_CONDITIONAL_EXPR:
    case JS_AST_NODE_TEMPLATE_LITERAL: case JS_AST_NODE_TEMPLATE_ELEMENT:
    case AST_NODE_MATCH_ARM:
        break;
    default:
        if (n->node_type != JS_AST_NODE_LABELED_STATEMENT)
            diagnostic(p, n, "syntax outside MVP scope");
    }
    js_ast_visit_children(n, child, &inner);
}

static Operator compound_operator(Operator op);
static LmdFunction* direct_target(MvpLmdProgram* p, AstCallNode* call) {
    LmdBinding* b = call->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
    return b && b->target && !b->assigned ? b->target : NULL;
}
static unsigned binary_kind(Operator op, unsigned left, unsigned right) {
    if (op == OPERATOR_AND || op == OPERATOR_OR || op == OPERATOR_JS_NULLISH_COALESCE)
        return left | right;
    if (op == OPERATOR_ADD) return ((left | right) & K_STRING ? K_STRING : 0) |
        ((left & ~K_STRING) && (right & ~K_STRING) ? K_NUMBER : 0);
    if (op == OPERATOR_JS_STRICT_EQ || op == OPERATOR_JS_STRICT_NE || op == OPERATOR_IN || op == OPERATOR_JS_INSTANCEOF) return K_BOOL;
    MirNumericOpPlan plan;
    return em_numeric_op_plan(op, &plan) && plan.is_comparison ? K_BOOL : K_NUMBER;
}
static bool numeric_operands(Operator op, unsigned left, unsigned right) {
    if (op == OPERATOR_EQ || op == OPERATOR_NE || op == OPERATOR_JS_STRICT_EQ ||
            op == OPERATOR_JS_STRICT_NE || op == OPERATOR_JS_INSTANCEOF) return false;
    if (op == OPERATOR_ADD) return !((left | right) & K_STRING);
    MirNumericOpPlan plan = {};
    if (em_numeric_op_plan(op, &plan)) return !plan.is_comparison || !((left & right) & K_STRING);
    return op == OPERATOR_JS_EXP || (op >= OPERATOR_JS_BIT_AND && op <= OPERATOR_JS_URSHIFT);
}
static LmdScalarField* scalar_field(MvpLmdProgram* p, AstNode* node);
static AstNode* scalar_field_initializer(MvpLmdProgram* p, AstNode* node);
static LmdArrayFacts array_facts(MvpLmdProgram* p, AstNode* n);
static LmdRange range(MvpLmdProgram* p, AstNode* n, bool scoped = false, bool present_only = false);
static LmdRange finite_range(double lower, double upper);
static bool finite_integer(double value);
static AstNode* enclosing_loop(MvpLmdProgram* p, AstNode* node, LmdFunction* owner, bool include_for_of = false);
static unsigned immutable_member_kind(MvpLmdProgram* p, String* name) {
    if (!p->immutable_objects || !name) return K_ANY;
    unsigned result = 0;
    const AstIndex* index = &p->frontend->ast_index;
    for (uint32_t id = 0; id < index->count; id++) {
        AstNode* object = index->nodes[id];
        if (object->node_type != AST_NODE_MAP && object->node_type != AST_NODE_OBJECT_LITERAL) continue;
        unsigned field = K_UNDEFINED;
        for (AstNode* e = ((AstMapNode*)object)->properties; e; e = e->next) {
            AstPropertyNode* prop = (AstPropertyNode*)e;
            String* key = ((AstIdentNode*)prop->key)->name;
            if (key->len == name->len && !memcmp(key->chars, name->chars, name->len))
                field = p->literal_kinds[ast_index_find(index, e)];
        }
        result |= field;
    }
    return result;
}
static unsigned kind(MvpLmdProgram* p, AstNode* n, LmdInlineFrame* facts = NULL) {
    if (!n) return K_UNDEFINED;
    switch (n->node_type) {
    case AST_NODE_LITERAL: {
        static const unsigned kinds[] = {K_NUMBER, K_STRING, K_BOOL, K_NULL, K_UNDEFINED};
        return kinds[((AstLiteralNode*)n)->literal_type];
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        if (named(id->name, "this") || named(id->name, "super") || named(id->name, "new.target")) return K_ANY;
        LmdBinding* b = identifier_binding(p, id);
        for (int i = 0; facts && i < facts->count; i++)
            if (facts->bindings[i] == b) return facts->slots[i].kinds;
        if (b && b->intrinsic) return b->kinds;
        if (b) return b->kinds | (!b->dominated && !b->entry->is_lexical &&
            !b->entry->is_parameter && !b->target ? K_UNDEFINED : 0);
        return K_UNDEFINED;
    }
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* member = (AstFieldNode*)n;
        unsigned owner = kind(p, member->object, facts);
        if (!owner) return 0;
        String* spelling = member_spelling(member);
        if ((owner == K_TYPED_ARRAY || owner == K_ARRAY || owner == K_STRING) && named(spelling, "length"))
            return K_NUMBER;
        if ((owner == K_TYPED_ARRAY && !named(spelling, "fill")) ||
                (owner == K_ARRAY && array_facts(p, member->object).lanes == LMD_ARRAY_ITEMS)) {
            LmdArrayFacts array = array_facts(p, member->object);
            LmdRange index = member->computed ? range(p, member->property, true) : LmdRange{};
            return index.state == 1 && array.length.state == 1 && index.lower >= 0 && index.upper < array.length.lower
                ? K_NUMBER : K_NUMBER | K_UNDEFINED;
        }
        LmdBinding* b = !member->computed && member->object->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
        for (int i = 0; b && facts && i < facts->count; i++) {
            if (facts->bindings[i] != b || !facts->guarded[i]) continue;
            String* name = ((AstIdentNode*)member->property)->name;
            ShapeEntry* field = typemap_hash_lookup(facts->plans[i]->blueprint, name->chars, name->len);
            if (!field) break;
            TypeId tid = field->type->type_id;
            return tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT ? K_NUMBER :
                tid == LMD_TYPE_BOOL ? K_BOOL : tid == LMD_TYPE_STRING ? K_STRING :
                tid == LMD_TYPE_NULL ? K_NULL : tid == LMD_TYPE_UNDEFINED ? K_UNDEFINED : K_ANY;
        }
        LmdScalarField* field = scalar_field(p, n);
        if (field) return field->kinds;
        // every successful plain-object read comes from a literal in this closed MVP unit.
        return (owner & K_OBJECT) && !(owner & ~(K_OBJECT | K_NULL | K_UNDEFINED))
            ? immutable_member_kind(p, spelling) : K_ANY;
    }
    case JS_AST_NODE_TEMPLATE_LITERAL: return K_STRING;
    case AST_NODE_ARRAY: return K_ARRAY;
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: return K_OBJECT;
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* ctor = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
        return ctor && ctor->intrinsic == I_ARRAY ? K_ARRAY :
            ctor && ctor->intrinsic >= I_INT32_ARRAY && ctor->intrinsic <= I_FLOAT64_ARRAY ? K_TYPED_ARRAY :
            ctor && ctor->intrinsic == I_MAP ? K_MAP : K_OBJECT | K_ARRAY | K_TYPED_ARRAY | K_FUNCTION;
    }
    case AST_NODE_CALL_EXPR: {
        if (math_operation(p, (AstCallNode*)n) >= 0) return K_NUMBER;
        AstNode* callee = ((AstCallNode*)n)->callee;
        LmdBinding* builtin = callee->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)callee) : NULL;
        if (builtin && builtin->intrinsic == I_ARRAY) return K_ARRAY;
        if (callee->node_type == AST_NODE_MEMBER_EXPR || callee->node_type == AST_NODE_INDEX_EXPR) {
            AstFieldNode* member = (AstFieldNode*)callee;
            unsigned receiver = kind(p, member->object, facts);
            String* spelling = member_spelling(member);
            if (named(spelling, "fill") && (!receiver || receiver == K_TYPED_ARRAY || receiver == K_ARRAY)) return receiver;
            if (receiver == K_ARRAY && named(spelling, "push")) return K_NUMBER;
            if (receiver == K_ARRAY && named(spelling, "join")) return K_STRING;
            if (receiver == K_STRING && named(spelling, "charCodeAt")) return K_NUMBER;
            if (receiver == K_STRING && (named(spelling, "charAt") || named(spelling, "repeat"))) return K_STRING;
            LmdBinding* object = member->object->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)member->object) : NULL;
            if (object && object->intrinsic == I_STRING && named(spelling, "fromCharCode")) return K_STRING;
            // bottom is not an unknown receiver: closed-call arguments may arrive on the next iteration.
            if (!receiver) return 0;
        }
        LmdFunction* f = direct_target(p, (AstCallNode*)n);
        return f ? f->returns : K_ANY;
    }
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC: case AST_NODE_CLASS: return K_FUNCTION;
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)n)->op;
        return op == OPERATOR_NOT || op == OPERATOR_JS_DELETE ? K_BOOL : op == OPERATOR_JS_TYPEOF ? K_STRING :
            op == OPERATOR_JS_VOID ? K_UNDEFINED : K_NUMBER;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        return binary_kind(b->op, kind(p, b->left, facts), kind(p, b->right, facts));
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        unsigned right = kind(p, a->right, facts);
        if (a->op == OPERATOR_ASSIGN) return right;
        if (a->op == OPERATOR_JS_AND_ASSIGN || a->op == OPERATOR_JS_OR_ASSIGN ||
                a->op == OPERATOR_JS_NULLISH_ASSIGN) return kind(p, a->left, facts) | right;
        return binary_kind(compound_operator(a->op), kind(p, a->left, facts), right);
    }
    case AST_NODE_IF_EXPR: case AST_NODE_CONDITIONAL_EXPR:
        return kind(p, ((AstIfNode*)n)->then, facts) | kind(p, ((AstIfNode*)n)->otherwise, facts);
    case AST_NODE_SEQ: {
        AstNode* item = ((AstArrayNode*)n)->item;
        while (item && item->next) item = item->next;
        return kind(p, item, facts);
    }
    default: return K_ANY;
    }
}
static LmdArrayFacts array_facts(MvpLmdProgram* p, AstNode* n) {
    if (!n) return {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
    switch (n->node_type) {
    case AST_NODE_IDENT: {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)n);
        if (!b || b->intrinsic) break;
        if ((!b->array_observed || p->immutable_objects) && b->writes == 1 && b->initializer &&
                b->initializer->node_type == AST_NODE_ARRAY) return array_facts(p, b->initializer);
        LmdArrayFacts result = b->array;
        // aliases are safe only when the entire closed unit excludes property mutation.
        if ((result.lanes & LMD_ARRAY_UNKNOWN) ||
                ((result.lanes & LMD_ARRAY_ITEMS) && !p->immutable_objects)) result = {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
        return result;
    }
    case AST_NODE_ARRAY: {
        AstArrayNode* a = (AstArrayNode*)n;
        LmdRange elements = {};
        for (AstNode* e = a->item; e; e = e->next) {
            AstNode* literal = e->node_type == AST_NODE_UNARY && ((AstUnaryNode*)e)->op == OPERATOR_NEG
                ? ((AstUnaryNode*)e)->operand : e;
            if (literal->node_type != AST_NODE_LITERAL ||
                    ((AstLiteralNode*)literal)->literal_type != AST_LITERAL_NUMBER) return {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
            LmdRange r = range(p, e);
            if (r.state != 1 || elements.state == 2) elements = {0, 0, 2};
            else elements = elements.state ? finite_range(fmin(elements.lower, r.lower), fmax(elements.upper, r.upper)) : r;
        }
        return {LMD_ARRAY_ITEMS, finite_range(a->length, a->length), elements};
    }
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* ctor = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(p, (AstIdentNode*)call->callee) : NULL;
        if (!ctor || ctor->intrinsic < I_INT32_ARRAY || ctor->intrinsic > I_FLOAT64_ARRAY) break;
        LmdRange length = call->arguments ? range(p, call->arguments) : finite_range(0, 0);
        if (length.state == 2) length = finite_range(0, INT53_MAX);
        if (length.state == 1) length = finite_range(fmax(0, length.lower), fmax(0, length.upper));
        return {1u << (ctor->intrinsic - I_INT32_ARRAY), length};
    }
    case AST_NODE_CALL_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        if (call->callee->node_type == AST_NODE_MEMBER_EXPR) {
            AstFieldNode* member = (AstFieldNode*)call->callee;
            if (!member->computed && named(((AstIdentNode*)member->property)->name, "fill")) {
                LmdArrayFacts result = array_facts(p, member->object);
                if (!(result.lanes & (LMD_ARRAY_ITEMS | LMD_ARRAY_UNKNOWN))) return result;
            }
        }
        LmdFunction* f = direct_target(p, call);
        return f && !(f->array.lanes & LMD_ARRAY_UNKNOWN) ? f->array : LmdArrayFacts{LMD_ARRAY_UNKNOWN, {0, 0, 2}};
    }
    case AST_NODE_ASSIGN:
        if (((AstAssignNode*)n)->op == OPERATOR_ASSIGN) return array_facts(p, ((AstAssignNode*)n)->right);
        break;
    case AST_NODE_CONDITIONAL_EXPR: {
        LmdArrayFacts a = array_facts(p, ((AstIfNode*)n)->then), b = array_facts(p, ((AstIfNode*)n)->otherwise);
        if (!a.lanes) return b;
        if (!b.lanes) return a;
        LmdRange length = !a.length.state || !b.length.state ? LmdRange{} :
            a.length.state == 1 && b.length.state == 1
                ? finite_range(fmin(a.length.lower, b.length.lower), fmax(a.length.upper, b.length.upper)) : LmdRange{0, 0, 2};
        return {a.lanes | b.lanes, length};
    }
    default: break;
    }
    return {LMD_ARRAY_UNKNOWN, {0, 0, 2}};
}
// only proven abrupt paths exclude the implicit undefined return.
static bool falls_through(AstNode* n) {
    if (!n) return true;
    if (n->node_type == AST_NODE_RETURN_STAM) return false;
    if (n->node_type == AST_NODE_BLOCK) {
        for (AstNode* s = ((AstBlockNode*)n)->statements; s; s = s->next)
            if (!falls_through(s)) return false;
    } else if (n->node_type == AST_NODE_IF_EXPR) {
        JsIfNode* branch = (JsIfNode*)n;
        return falls_through(branch->then) || falls_through(branch->otherwise);
    }
    return true;
}
struct LmdTypes { MvpLmdProgram* p; bool changed; LmdFunction* owner = NULL; LmdInlineFrame* facts = NULL; };
static void visit_destructured_assignments(AstNode* node, void (*visit)(AstNode*, void*), void* state) {
    if (node->node_type != AST_NODE_ASSIGN) return;
    AstAssignNode* assignment = (AstAssignNode*)node;
    if ((assignment->left->node_type != AST_NODE_ARRAY &&
            assignment->left->node_type != AST_NODE_ARRAY_PATTERN) ||
            assignment->right->node_type != AST_NODE_ARRAY) return;
    AstNode* value = ((AstArrayNode*)assignment->right)->item;
    for (AstNode* target = ((AstArrayNode*)assignment->left)->item; target; target = target->next) {
        // reuse ordinary assignment transfer without changing the indexed source AST.
        AstAssignNode element = *assignment;
        element.left = target; element.right = value;
        visit((AstNode*)&element, state);
        if (value) value = value->next;
    }
}
static void union_kinds(LmdTypes* t, unsigned* target, unsigned incoming) {
    if ((*target | incoming) != *target) { *target |= incoming; t->changed = true; }
}
static void type_walk(AstNode* n, void* arg) {
    if (!n) return;
    LmdTypes* t = (LmdTypes*)arg;
    visit_destructured_assignments(n, type_walk, arg);
    if (n->node_type == AST_NODE_PROPERTY && t->p->immutable_objects && !t->facts)
        union_kinds(t, &t->p->literal_kinds[ast_index_find(&t->p->frontend->ast_index, n)],
            kind(t->p, ((AstPropertyNode*)n)->value, t->facts));
    if (n->node_type == AST_NODE_FUNC || n->node_type == AST_NODE_FUNC_EXPR ||
            n->node_type == AST_NODE_ARROW_FUNC || n->node_type == AST_NODE_METHOD) {
        LmdFunction* f = function(t->p, n);
        LmdTypes inner = {t->p, false, f};
        union_kinds(&inner, &f->returns, f->ast->body->node_type == AST_NODE_BLOCK
            ? (falls_through(f->ast->body) ? K_UNDEFINED : 0) : kind(t->p, f->ast->body, t->facts));
        union_kinds(&inner, &f->array.lanes, f->ast->body->node_type == AST_NODE_BLOCK
            ? (falls_through(f->ast->body) ? LMD_ARRAY_UNKNOWN : 0) : array_facts(t->p, f->ast->body).lanes);
        js_ast_visit_children(n, type_walk, &inner);
        t->changed |= inner.changed;
        return;
    }
    if (n->node_type == AST_NODE_RETURN_STAM && t->owner) {
        union_kinds(t, &t->owner->returns, kind(t->p, ((AstReturnNode*)n)->value, t->facts));
        union_kinds(t, &t->owner->array.lanes, array_facts(t->p, ((AstReturnNode*)n)->value).lanes);
    }
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n;
        LmdFunction* f = direct_target(t->p, call);
        if (f && f->closed_calls) {
            AstNode* actual = call->arguments;
            for (AstNode* formal = f->ast->params; formal; formal = formal->next) {
                LmdBinding* b = binding(t->p, js_ast_parameter_binding_identifier(formal)->entry);
                unsigned incoming = actual ? kind(t->p, actual, t->facts) : K_UNDEFINED;
                union_kinds(t, &b->kinds, incoming);
                union_kinds(t, &b->array.lanes, array_facts(t->p, actual).lanes);
                if (actual) actual = actual->next;
            }
        }
    }
    AstNode* target = NULL;
    unsigned mask = 0;
    if (n->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* d = (AstDeclaratorNode*)n;
        target = d->id;
        if (d->init) mask = kind(t->p, d->init, t->facts);
    } else if (n->node_type == AST_NODE_ASSIGN) {
        AstAssignNode* a = (AstAssignNode*)n;
        target = a->left;
        mask = kind(t->p, n, t->facts);
    } else if (n->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
             ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT)) {
        target = ((AstUnaryNode*)n)->operand;
        mask = K_NUMBER;
    }
    if (target && target->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(t->p, (AstIdentNode*)target);
        unsigned* destination = b ? &b->kinds : NULL;
        for (int i = 0; t->facts && i < t->facts->count; i++)
            if (t->facts->bindings[i] == b) destination = &t->facts->slots[i].kinds;
        if (b && !b->intrinsic) union_kinds(t, destination, mask);
        if (b && !b->intrinsic) union_kinds(t, &b->array.lanes, array_facts(t->p,
            n->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)n)->init : n).lanes);
    }
    if (target && target->node_type == AST_NODE_MEMBER_EXPR) {
        LmdScalarField* field = scalar_field(t->p, target);
        if (field) union_kinds(t, &field->kinds, mask);
    }
    js_ast_visit_children(n, type_walk, t);
}

// ranges describe finite JS integer values only; unknown includes fractional and signed-zero values.
static LmdRange finite_range(double lower, double upper) {
    return isfinite(lower) && isfinite(upper) && lower >= INT53_MIN && upper <= INT53_MAX
        ? LmdRange{lower, upper, 1} : LmdRange{0, 0, 2};
}
static LmdRange range_binary(Operator operation, LmdRange left, LmdRange right) {
    if (!left.state || !right.state) return {};
    if (left.state != 1 || right.state != 1) return {0, 0, 2};
    if (operation == OPERATOR_ADD) return finite_range(left.lower + right.lower, left.upper + right.upper);
    if (operation == OPERATOR_SUB) return finite_range(left.lower - right.upper, left.upper - right.lower);
    if (operation == OPERATOR_MUL) {
        if ((left.lower <= 0 && left.upper >= 0 && right.lower < 0) ||
                (right.lower <= 0 && right.upper >= 0 && left.lower < 0)) return {0, 0, 2};
        double products[] = {left.lower * right.lower, left.lower * right.upper,
            left.upper * right.lower, left.upper * right.upper};
        double lo = products[0], hi = lo;
        for (double product : products) { lo = fmin(lo, product); hi = fmax(hi, product); }
        return finite_range(lo, hi);
    }
    if (operation == OPERATOR_MOD && left.lower >= 0 && right.lower > 0)
        return finite_range(0, fmin(left.upper, right.upper - 1));
    return {0, 0, 2};
}
static LmdBinding* linear_index(MvpLmdProgram* p, AstNode* node, double* offset) {
    *offset = 0;
    if (node && node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* shifted = (AstBinaryNode*)node;
        if ((shifted->op != OPERATOR_ADD && shifted->op != OPERATOR_SUB) ||
                shifted->right->node_type != AST_NODE_LITERAL ||
                ((AstLiteralNode*)shifted->right)->literal_type != AST_LITERAL_NUMBER) return NULL;
        *offset = ((AstLiteralNode*)shifted->right)->value.number_value;
        if (!finite_integer(*offset)) return NULL;
        if (shifted->op == OPERATOR_SUB) *offset = -*offset;
        node = shifted->left;
    }
    return node && node->node_type == AST_NODE_IDENT ? identifier_binding(p, (AstIdentNode*)node) : NULL;
}
static LmdRange range(MvpLmdProgram* p, AstNode* n, bool scoped, bool present_only) {
    if (!n) return {0, 0, 2};
    // absence contributes no payload; ordinary arithmetic must still see its unknown/NaN range.
    if (present_only && kind(p, n) == K_UNDEFINED) return {};
    switch (n->node_type) {
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: {
        AstFieldNode* member = (AstFieldNode*)n;
        unsigned owner = kind(p, member->object);
        String* spelling = member_spelling(member);
        if ((owner == K_ARRAY || owner == K_TYPED_ARRAY || owner == K_STRING) && named(spelling, "length")) {
            LmdArrayFacts facts = array_facts(p, member->object);
            return facts.length.state == 1 ? facts.length : finite_range(0, owner == K_TYPED_ARRAY ? INT53_MAX : UINT32_MAX);
        }
        LmdArrayFacts facts = array_facts(p, member->object);
        if (member->computed && facts.lanes && !(facts.lanes & LMD_ARRAY_UNKNOWN)) {
            // kind admission uses this site's bounds; retain the same proof for its integer payload.
            LmdRange index = range(p, member->property, true);
            if (!index.state || !facts.length.state) return {};
            bool numeric_key = !(kind(p, member->property) & ~(K_NUMBER | K_UNDEFINED));
            if ((present_only && numeric_key) || (index.state == 1 && facts.length.state == 1 && index.lower >= 0 && index.upper < facts.length.lower)) {
                // a numeric-array parameter may have no integer-content proof; that is unknown, not bottom.
                if (facts.lanes == LMD_ARRAY_ITEMS) return facts.elements.state ? facts.elements : LmdRange{0, 0, 2};
                if (!(facts.lanes & ~3u)) return finite_range(facts.lanes & 1 ? INT32_MIN : 0,
                    facts.lanes & 1 ? INT32_MAX : UINT8_MAX);
            }
        }
        AstNode* value = scalar_field_initializer(p, n);
        return value ? range(p, value) : LmdRange{0, 0, 2};
    }
    case AST_NODE_LITERAL: {
        AstLiteralNode* literal = (AstLiteralNode*)n;
        if (literal->literal_type != AST_LITERAL_NUMBER) break;
        double value = literal->value.number_value;
        if (value != trunc(value) || (value == 0 && signbit(value))) break;
        return finite_range(value, value);
    }
    case AST_NODE_IDENT: {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)n);
        // a successful const read has passed TDZ even when a closed function reads a module constant.
        bool immutable = b && b->entry && b->entry->is_const && b->writes == 1;
        if (!b || b->intrinsic || ((!immutable && b->external) ||
                !(immutable || b->dominated || b->entry->is_parameter))) return {0, 0, 2};
        LmdRange result = present_only && b->kinds == (K_NUMBER | K_UNDEFINED) ? b->present_range : b->range;
        if (!scoped || result.state != 1 || b->writes != 2) return result;
        AstNode* child = n;
        for (AstNode* parent = ast_index_parent(&p->frontend->ast_index, child); parent;
                child = parent, parent = ast_index_parent(&p->frontend->ast_index, parent)) {
            if (parent->node_type != AST_NODE_LOOP) continue;
            AstLoopControlNode* loop = (AstLoopControlNode*)parent;
            if (loop->form == LOOP_FORM_DO_WHILE) continue;
            if (loop->body != child || !loop->cond ||
                    loop->cond->node_type != AST_NODE_BINARY) continue;
            // a nested loop can revisit the read after writing, before this condition is retested.
            if (loop->update != b->update && (!b->update ||
                    enclosing_loop(p, b->update, b->owner, true) != parent ||
                    ast_index_find(&p->frontend->ast_index, n) >=
                    ast_index_find(&p->frontend->ast_index, b->update))) continue;
            AstBinaryNode* cond = (AstBinaryNode*)loop->cond;
            double offset;
            if (linear_index(p, cond->left, &offset) != b ||
                    finite_range(result.lower + offset, result.upper + offset).state != 1) continue;
            LmdRange limit = range(p, cond->right);
            if (limit.state != 1) continue;
            limit = finite_range(limit.lower - offset, limit.upper - offset);
            if (limit.state != 1) continue;
            double lo = result.lower, hi = result.upper;
            if (cond->op == OPERATOR_LT || cond->op == OPERATOR_LE)
                hi = fmin(hi, limit.upper - (cond->op == OPERATOR_LT ? 1 : 0));
            if (cond->op == OPERATOR_GT || cond->op == OPERATOR_GE)
                lo = fmax(lo, limit.lower + (cond->op == OPERATOR_GT ? 1 : 0));
            if (lo <= hi) result = finite_range(lo, hi);
        }
        return result;
    }
    case AST_NODE_CALL_EXPR: {
        LmdFunction* f = direct_target(p, (AstCallNode*)n);
        // explicit numeric returns do not exclude an implicit undefined return.
        return f && f->returns == K_NUMBER ? f->range : LmdRange{0, 0, 2};
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        return a->op == OPERATOR_ASSIGN ? range(p, a->right, scoped, present_only) :
            range_binary(compound_operator(a->op), range(p, a->left, scoped), range(p, a->right, scoped));
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        if (b->op >= OPERATOR_JS_BIT_AND && b->op <= OPERATOR_JS_URSHIFT)
            return finite_range(b->op == OPERATOR_JS_URSHIFT ? 0 : INT32_MIN,
                b->op == OPERATOR_JS_URSHIFT ? UINT32_MAX : INT32_MAX);
        return range_binary(b->op, range(p, b->left, scoped), range(p, b->right, scoped));
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)n;
        LmdRange r = range(p, u->operand, scoped);
        if (u->op == OPERATOR_POS) return r;
        if (u->op == OPERATOR_JS_BIT_NOT) return finite_range(INT32_MIN, INT32_MAX);
        if (u->op == OPERATOR_NEG && r.state == 1 && (r.upper < 0 || r.lower > 0))
            return finite_range(-r.upper, -r.lower);
        if (u->op == OPERATOR_JS_INCREMENT || u->op == OPERATOR_JS_DECREMENT) {
            LmdRange after = range_binary(u->op == OPERATOR_JS_INCREMENT ? OPERATOR_ADD : OPERATOR_SUB, r, {1, 1, 1});
            // postfix consumers see the snapshot; include the write for the binding fixed point too.
            if (!u->prefix && r.state == 1 && after.state == 1)
                return finite_range(fmin(r.lower, after.lower), fmax(r.upper, after.upper));
            return after;
        }
        break;
    }
    default: break;
    }
    return {0, 0, 2};
}
static bool same_binding(MvpLmdProgram* p, AstNode* n, LmdBinding* b) {
    return n && n->node_type == AST_NODE_IDENT && identifier_binding(p, (AstIdentNode*)n) == b;
}
static LmdRange update_delta(MvpLmdProgram* p, LmdBinding* b) {
    AstNode* n = b->update;
    if (!n) return {0, 0, 2};
    if (n->node_type == AST_NODE_UNARY) {
        double step = ((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ? 1 : -1;
        return {step, step, 1};
    }
    AstAssignNode* a = (AstAssignNode*)n;
    Operator operation = a->op == OPERATOR_ASSIGN ? OPERATOR_ASSIGN : compound_operator(a->op);
    AstNode* rhs = a->right;
    if (operation == OPERATOR_ASSIGN && rhs->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)rhs;
        if (!same_binding(p, binary->left, b)) return {0, 0, 2};
        operation = binary->op; rhs = binary->right;
    }
    LmdRange delta = range(p, rhs);
    if (operation == OPERATOR_SUB && delta.state == 1) return {-delta.upper, -delta.lower, 1};
    return operation == OPERATOR_ADD ? delta : LmdRange{0, 0, 2};
}
static bool loop_linear(MvpLmdProgram* p, AstNode* n) {
    if (!n) return true;
    if (n->node_type == AST_NODE_LOOP || n->node_type == AST_NODE_BREAK_STAM ||
            n->node_type == AST_NODE_CONTINUE_STAM || n->node_type == AST_NODE_FUNC ||
            n->node_type == AST_NODE_FUNC_EXPR || n->node_type == AST_NODE_ARROW_FUNC) return false;
    struct Check { MvpLmdProgram* p; bool ok; } check = {p, true};
    js_ast_visit_children(n, [](AstNode* child, void* opaque) {
        Check* c = (Check*)opaque; c->ok &= loop_linear(c->p, child);
    }, &check);
    return check.ok;
}
static AstNode* enclosing_loop(MvpLmdProgram* p, AstNode* node, LmdFunction* owner, bool include_for_of) {
    for (AstNode* parent = ast_index_parent(&p->frontend->ast_index, node);
            parent && parent != (AstNode*)owner->ast;
            parent = ast_index_parent(&p->frontend->ast_index, parent))
        if (parent->node_type == AST_NODE_LOOP ||
                (include_for_of && parent->node_type == AST_NODE_FOR_OF_STAM)) return parent;
    return NULL;
}
static bool loop_reset_before(MvpLmdProgram* p, LmdBinding* binding, AstLoopControlNode* loop) {
    if (!binding->initializer) return false;
    AstIndex* index = &p->frontend->ast_index;
    AstNode* declaration = ast_index_parent(index, binding->initializer);
    if (!declaration || declaration->node_type != AST_NODE_VARIABLE_DECLARATOR) return false;
    AstNode* statement = ast_index_parent(index, declaration);
    if (statement == loop->init) return true;
    AstNode* block = ast_index_parent(index, (AstNode*)loop);
    // a direct preceding declaration executes on every entry to the containing block.
    return block && block->node_type == AST_NODE_BLOCK && statement &&
        ast_index_parent(index, statement) == block &&
        ast_index_find(index, statement) < ast_index_find(index, (AstNode*)loop);
}
// a unique additive write in a counted loop has a finite activation-wide bound.
static LmdRange counted_range(MvpLmdProgram* p, LmdBinding* b) {
    if (b->writes != 2 || !b->initializer || !b->update || b->external) return {0, 0, 2};
    AstNode* ancestor = enclosing_loop(p, b->update, b->owner);
    if (!ancestor) return {0, 0, 2};
    AstLoopControlNode* loop = (AstLoopControlNode*)ancestor;
    if (loop->form == LOOP_FORM_DO_WHILE) return {0, 0, 2};
    // condition writes also execute on the final failed test, outside the body trip count.
    AstNode* location = b->update;
    while (location && location != loop->body && location != ancestor)
        location = ast_index_parent(&p->frontend->ast_index, location);
    if (location != loop->body && loop->update != b->update) return {0, 0, 2};
    if (!loop->cond || loop->cond->node_type != AST_NODE_BINARY) return {0, 0, 2};
    AstBinaryNode* cond = (AstBinaryNode*)loop->cond;
    AstNode* index_node = cond->left;
    bool square = index_node && index_node->node_type == AST_NODE_BINARY &&
        ((AstBinaryNode*)index_node)->op == OPERATOR_MUL;
    if (square) {
        AstBinaryNode* product = (AstBinaryNode*)index_node;
        if (product->left->node_type != AST_NODE_IDENT || product->right->node_type != AST_NODE_IDENT ||
                ((AstIdentNode*)product->left)->entry != ((AstIdentNode*)product->right)->entry)
            return {0, 0, 2};
        index_node = product->left;
    }
    double offset;
    LmdBinding* index = linear_index(p, index_node, &offset);
    if (!index || index->writes != 2 || !index->initializer || !index->update || index->external) return {0, 0, 2};
    bool reset = loop_reset_before(p, index, loop) && (b == index || loop_reset_before(p, b, loop));
    if ((!reset && enclosing_loop(p, ancestor, b->owner)) ||
            ((!reset || b != index) && !loop_linear(p, loop->body))) return {0, 0, 2};
    AstNode* write = ast_index_parent(&p->frontend->ast_index, index->update);
    if (loop->update != index->update && (!write || write->node_type != AST_NODE_EXPR_STMT ||
            ast_index_parent(&p->frontend->ast_index, write) != loop->body)) return {0, 0, 2};
    LmdRange initial = range(p, index->initializer), limit = range(p, cond->right);
    LmdRange step = update_delta(p, index), start = range(p, b->initializer), delta = update_delta(p, b);
    if (!square && cond->right->node_type == AST_NODE_IDENT) {
        LmdBinding* bound = identifier_binding(p, (AstIdentNode*)cond->right);
        if (bound && bound != index && !bound->external && bound->writes == 2 && bound->update &&
                loop_reset_before(p, bound, loop) && enclosing_loop(p, bound->update, bound->owner) == ancestor) {
            LmdRange change = update_delta(p, bound);
            // an inward-moving opposite cursor cannot extend the induction variable's trip count.
            if (change.state == 1 &&
                    (((cond->op == OPERATOR_LT || cond->op == OPERATOR_LE) && change.upper <= 0) ||
                     ((cond->op == OPERATOR_GT || cond->op == OPERATOR_GE) && change.lower >= 0)))
                limit = range(p, bound->initializer);
        }
    }
    if (!initial.state || !limit.state || !step.state || !start.state || !delta.state) return {};
    if (initial.state != 1 || limit.state != 1 || step.state != 1 || start.state != 1 || delta.state != 1)
        return {0, 0, 2};
    if (offset) {
        // normalize affine tests only while both the shifted bound and arithmetic remain exact.
        limit = finite_range(limit.lower - offset, limit.upper - offset);
        if (limit.state != 1) return {0, 0, 2};
    }
    if (square) {
        if (step.lower <= 0 || limit.upper < 0 ||
                (cond->op != OPERATOR_LT && cond->op != OPERATOR_LE)) return {0, 0, 2};
        // a loose square-root bound includes JS multiplication rounding and the final update.
        limit = finite_range(0, floor(sqrt(limit.upper)) + 1);
    }
    int64_t distance, stride;
    if (step.lower > 0 && (cond->op == OPERATOR_LT || cond->op == OPERATOR_LE))
        { distance = (int64_t)limit.upper - (int64_t)initial.lower; stride = (int64_t)step.lower; }
    else if (step.upper < 0 && (cond->op == OPERATOR_GT || cond->op == OPERATOR_GE))
        { distance = (int64_t)initial.upper - (int64_t)limit.lower; stride = -(int64_t)step.upper; }
    else return {0, 0, 2};
    // differences fit i64; divide before multiplying so even int53 endpoint proofs stay exact.
    int64_t count = distance < 0 ? 0 : distance / stride + 1;
    int64_t lo = (int64_t)start.lower, hi = (int64_t)start.upper;
    int64_t down = delta.lower < 0 ? -(int64_t)delta.lower : 0;
    int64_t up = delta.upper > 0 ? (int64_t)delta.upper : 0;
    if ((down && count > (lo - INT53_MIN) / down) ||
            (up && count > (INT53_MAX - hi) / up)) return {0, 0, 2};
    LmdRange result = finite_range((double)(lo - count * down), (double)(hi + count * up));
    if (offset) {
        double index_lo = initial.lower + (step.lower < 0 ? count * step.lower : 0);
        double index_hi = initial.upper + (step.upper > 0 ? count * step.upper : 0);
        if (finite_range(index_lo + offset, index_hi + offset).state != 1) return {0, 0, 2};
    }
    return result;
}
struct LmdRanges { MvpLmdProgram* p; LmdFunction* owner; bool changed; };
static void join_range(LmdRanges* t, LmdRange* into, unsigned* changes, LmdRange incoming) {
    if (!incoming.state || into->state == 2) return;
    if (into->state == 1 && incoming.state == 1)
        incoming = {fmin(into->lower, incoming.lower), fmax(into->upper, incoming.upper), 1};
    if (into->state == incoming.state && into->lower == incoming.lower && into->upper == incoming.upper) return;
    // cyclic growth without a counted-loop proof widens, rather than assuming a benchmark bound.
    if (++*changes > 16) incoming = {0, 0, 2};
    *into = incoming; t->changed = true;
}
static void range_walk(AstNode* n, void* opaque) {
    if (!n) return;
    LmdRanges* t = (LmdRanges*)opaque;
    MvpLmdProgram* p = t->p;
    visit_destructured_assignments(n, range_walk, opaque);
    if (n->node_type == AST_NODE_FUNC || n->node_type == AST_NODE_FUNC_EXPR || n->node_type == AST_NODE_ARROW_FUNC || n->node_type == AST_NODE_METHOD) {
        LmdFunction* f = function(p, n); LmdRanges inner = {p, f, false};
        if (f->ast->body->node_type != AST_NODE_BLOCK)
            join_range(&inner, &f->range, &f->range_changes, range(p, f->ast->body));
        if (f->ast->body->node_type != AST_NODE_BLOCK)
            join_range(&inner, &f->array.length, &f->array_changes, array_facts(p, f->ast->body).length);
        js_ast_visit_children(n, range_walk, &inner); t->changed |= inner.changed; return;
    }
    if (n->node_type == AST_NODE_RETURN_STAM && t->owner) {
        join_range(t, &t->owner->range, &t->owner->range_changes, range(p, ((AstReturnNode*)n)->value));
        join_range(t, &t->owner->array.length, &t->owner->array_changes, array_facts(p, ((AstReturnNode*)n)->value).length);
    }
    if (n->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)n; LmdFunction* f = direct_target(p, call);
        if (f && f->closed_calls) {
            AstNode* actual = call->arguments;
            for (AstNode* formal = f->ast->params; formal; formal = formal->next) {
                LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(formal)->entry);
                join_range(t, &b->range, &b->range_changes, range(p, actual));
                join_range(t, &b->array.length, &b->array_changes, array_facts(p, actual).length);
                if (actual) actual = actual->next;
            }
        }
    }
    AstNode* target = n->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)n)->id :
        n->node_type == AST_NODE_ASSIGN ? ((AstAssignNode*)n)->left :
        n->node_type == AST_NODE_UNARY && (((AstUnaryNode*)n)->op == OPERATOR_JS_INCREMENT ||
            ((AstUnaryNode*)n)->op == OPERATOR_JS_DECREMENT) ? ((AstUnaryNode*)n)->operand : NULL;
    if (target && target->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)target);
        if (b && !b->intrinsic) {
            AstNode* value = n->node_type == AST_NODE_VARIABLE_DECLARATOR ? ((AstDeclaratorNode*)n)->init : n;
            LmdRange incoming = b->update == n ? counted_range(p, b) : range(p, value);
            if (b->update == n && incoming.state == 2) incoming = range(p, value);
            join_range(t, &b->range, &b->range_changes, incoming);
            if (b->kinds == (K_NUMBER | K_UNDEFINED))
                join_range(t, &b->present_range, &b->present_range_changes, range(p, value, false, true));
            join_range(t, &b->array.length, &b->array_changes, array_facts(p, value).length);
        }
    }
    js_ast_visit_children(n, range_walk, t);
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
static bool finite_integer(double value) {
    return isfinite(value) && value >= INT53_MIN && value <= INT53_MAX &&
        value == trunc(value) && !(value == 0 && signbit(value));
}
static LmdValue number(LmdCompiler* c, double value) {
    bool integral = finite_integer(value);
    // inlined arithmetic retains its callee's numeric region, independently of the caller.
    LmdFunction* owner = c->inlining ? c->inlining->function : c->function;
    bool native_int = integral && (c->integer_bail || !owner->native || owner->integer_region);
    MIR_reg_t r = em_new_reg(&c->em, "number", native_int ? MIR_T_I64 : MIR_T_D);
    move(c, r, native_int ? MIR_new_int_op(c->em.ctx, (int64_t)value) :
        MIR_new_double_op(c->em.ctx, value), !native_int);
    return {r, K_NUMBER, true, value, native_int, integral ? finite_range(value, value) : LmdRange{0, 0, 2}};
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
    root_value(c, r, JIT_VALUE_BOXED_ITEM); return {r, hint | K_BOXED};
}
static bool is_boxed(LmdValue v) { return v.kind & K_BOXED; }
static unsigned semantic(LmdValue v) { return v.kind & K_ANY; }
static LmdValue expression(LmdCompiler* c, AstNode* n, bool borrow_scalar = false, bool numeric = false);
static LmdValue box(LmdCompiler* c, LmdValue value);
static MIR_reg_t to_number(LmdCompiler* c, LmdValue value);
static MIR_reg_t truth(LmdCompiler* c, LmdValue value);
static MIR_reg_t tag(LmdCompiler* c, LmdValue value);
static void branch_condition(LmdCompiler* c, AstNode* node, MIR_label_t target, bool yes = true);
static void statement(LmdCompiler* c, AstNode* n);
static MIR_reg_t scalar_reg(LmdCompiler* c, LmdValue value, unsigned kind, bool integral = false) {
    if (integral) {
        bool optional = kind == (K_NUMBER | K_UNDEFINED);
        if (value.integer && (optional || !value.number_present)) return value.reg;
        MIR_reg_t result = em_new_reg(&c->em, "proven_integer", MIR_T_I64);
        MIR_label_t absent = optional ? label(c) : NULL;
        if (optional) {
            move(c, result, integer(c, 0));
            if (value.number_present) branch_truth(c, absent, value.number_present, false);
            else branch(c, MIR_BEQ, absent, reg(c, tag(c, value)), integer(c, LMD_TYPE_UNDEFINED));
            value.number_present = 0; value.kind = K_NUMBER | (value.kind & K_BOXED);
        }
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, result), reg(c, to_number(c, value))));
        if (absent) put_label(c, absent);
        return result;
    }
    return (kind & K_NUMBER) ? to_number(c, value) : truth(c, value);
}
static void release_iterators(LmdCompiler* c, LmdControl* through = NULL) {
    for (LmdControl* control = c->control; control && control != through; control = control->parent) {
        if (!control->iteration_active) continue;
        MIR_label_t done = label(c);
        branch_truth(c, done, control->iteration_active, false);
        MIR_reg_t count = em_load_at(&c->em, control->iteration_owner,
            offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64, "active_cursors");
        em_store_at(&c->em, control->iteration_owner, offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64,
            op(c, MIR_SUB, reg(c, count), integer(c, 1)));
        move(c, control->iteration_active, integer(c, 0)); put_label(c, done);
    }
}
static void return_error(LmdCompiler* c, MIR_reg_t error) {
    release_iterators(c);
    MIR_op_t placeholder = c->em.frame.return_type == MIR_T_D
        ? MIR_new_double_op(c->em.ctx, 0) : integer(c, 0);
    em_stage_function_return(&c->em, c->function->native ? placeholder : reg(c, error), error);
}
static void return_value(LmdCompiler* c, LmdValue value) {
    release_iterators(c);
    MIR_reg_t result = c->function->native
        ? scalar_reg(c, value, c->function->returns, c->function->integer)
        : box(c, value).reg;
    if (c->function->home && ((AstMethodNode*)c->function->ast)->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) {
        result = em_call_3(&c->em, "mvp_lmd_constructor_result", MIR_T_I64,
            MIR_T_I64, reg(c, result), MIR_T_I64, reg(c, c->receiver),
            MIR_T_I64, integer(c, c->function->home->ast->superclass != NULL), true);
    }
    em_stage_function_return(&c->em, reg(c, result));
}
static void fail(LmdCompiler* c, int kind, AstNode* n) {
    MIR_reg_t error = em_call_2(&c->em, "mvp_lmd_fail", MIR_T_I64, MIR_T_I64,
        integer(c, kind), MIR_T_I64, integer(c, n ? n->source_span.start_byte : 0), true);
    return_error(c, error);
}
static void check_error(LmdCompiler* c, MIR_reg_t result) {
    MIR_label_t ok = label(c);
    MIR_reg_t tag = op(c, MIR_URSH, reg(c, result), integer(c, 56));
    branch(c, MIR_BNE, ok, reg(c, tag), integer(c, LMD_TYPE_ERROR));
    return_error(c, result); put_label(c, ok);
}
static MIR_reg_t payload(LmdCompiler* c, MIR_reg_t value) {
    return op(c, MIR_AND, reg(c, value), integer(c, ITEM_INT_PAYLOAD_MASK));
}
static MIR_reg_t tag(LmdCompiler* c, LmdValue value) {
    unsigned k = semantic(value);
    if (value.number_present) {
        MIR_reg_t result = constant(c, LMD_TYPE_UNDEFINED);
        MIR_label_t done = label(c);
        branch_truth(c, done, value.number_present, false);
        move(c, result, integer(c, LMD_TYPE_FLOAT)); put_label(c, done); return result;
    }
    if (k && !(k & (k - 1))) {
        TypeId tid = k == K_NUMBER ? LMD_TYPE_FLOAT : k == K_STRING ? LMD_TYPE_STRING :
            k == K_BOOL ? LMD_TYPE_BOOL : k == K_NULL ? LMD_TYPE_NULL :
            k == K_UNDEFINED ? LMD_TYPE_UNDEFINED : k == K_ARRAY ? LMD_TYPE_ARRAY :
            k == K_TYPED_ARRAY ? LMD_TYPE_ARRAY_NUM :
            k == K_OBJECT || k == K_MAP ? LMD_TYPE_MAP : LMD_TYPE_FUNC;
        return constant(c, tid);
    }
    MIR_reg_t result = em_new_reg(&c->em, "type", MIR_T_I64);
    MIR_label_t numeric = label(c), done = label(c);
    MIR_reg_t mask = op(c, MIR_AND, reg(c, value.reg), integer(c, ITEM_DBL_MASK));
    branch_truth(c, numeric, mask);
    move(c, result, reg(c, op(c, MIR_URSH, reg(c, value.reg), integer(c, 56))));
    // INT is a runtime subtype; primitive dispatch still sees JS Number.
    branch(c, MIR_BEQ, numeric, reg(c, result), integer(c, LMD_TYPE_INT));
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
    MIR_reg_t r = em_store_f64_home(&c->em, value, home);
    em_scalar_home_bind(&c->em, id, r); return r;
}
static MIR_reg_t cold_unbox(void* owner, MIR_reg_t item) {
    LmdCompiler* c = (LmdCompiler*)owner;
    MIR_reg_t result = em_new_reg(&c->em, "numeric_item", MIR_T_D);
    MIR_label_t floating = label(c), done = label(c);
    MIR_reg_t type = op(c, MIR_URSH, reg(c, item), integer(c, 56));
    // packed integers need no Float sentinel or payload-pointer checks.
    branch(c, MIR_BNE, floating, reg(c, type), integer(c, LMD_TYPE_INT));
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, result),
        reg(c, em_unbox_finite_int_item(&c->em, item))));
    jump(c, done); put_label(c, floating);
    MIR_reg_t value = em_unbox_f64_noninline_item(&c->em, c,
        [](void* owner, MIR_reg_t item) -> MIR_reg_t {
            LmdCompiler* c = (LmdCompiler*)owner;
            return em_load_at(&c->em, payload(c, item), 0, MIR_T_D, "wide_number");
        }, item);
    move(c, result, reg(c, value), true); put_label(c, done); return result;
}
static LmdValue box(LmdCompiler* c, LmdValue value) {
    if (is_boxed(value)) return value;
    if (value.number_present) {
        MIR_reg_t result = constant(c, ITEM_JS_UNDEFINED);
        MIR_label_t done = label(c);
        branch_truth(c, done, value.number_present, false);
        LmdValue present = value; present.kind = K_NUMBER; present.number_present = 0;
        move(c, result, reg(c, box(c, present).reg));
        put_label(c, done); return boxed(c, result, K_NUMBER | K_UNDEFINED);
    }
    if (value.literal && value.range.state == 1)
        return boxed(c, constant(c, i2it((int64_t)value.number)), K_NUMBER);
    if (value.integer) return boxed(c, em_box_finite_int_item(&c->em, value.reg), K_NUMBER);
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

static MIR_reg_t coerce_noninline_number(void* owner, MIR_reg_t item) {
    LmdCompiler* c = (LmdCompiler*)owner;
    MIR_reg_t tid = op(c, MIR_URSH, reg(c, item), integer(c, 56));
    MIR_reg_t result = em_new_reg(&c->em, "converted_number", MIR_T_D);
    MIR_label_t numeric = label(c), done = label(c);
    branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_INT));
    branch(c, MIR_BEQ, numeric, reg(c, tid), integer(c, LMD_TYPE_FLOAT));
    TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_BOOL, LMD_TYPE_NULL, LMD_TYPE_UNDEFINED};
    MIR_label_t cases[4];
    for (int i = 0; i < 4; i++) {
        cases[i] = label(c); branch(c, MIR_BEQ, cases[i], reg(c, tid), integer(c, types[i]));
    }
    fail(c, LMD_MVP_CAPABILITY, NULL);
    put_label(c, numeric); move(c, result, reg(c, cold_unbox(c, item)), true); jump(c, done);
    for (int i = 0; i < 4; i++) {
        put_label(c, cases[i]); MIR_reg_t r;
        if (i == 0) r = em_call_1(&c->em, "mvp_lmd_string_to_number", MIR_T_D,
            MIR_T_P, reg(c, payload(c, item)), true);
        else if (i == 1) r = to_number(c, {payload(c, item), K_BOOL});
        else r = to_number(c, number(c, i == 2 ? 0 : NAN));
        move(c, result, reg(c, r), true); jump(c, done);
    }
    put_label(c, done); return result;
}
static MIR_reg_t to_number(LmdCompiler* c, LmdValue value) {
    // integer optional lanes keep absence separate until JS numeric coercion needs NaN.
    if (value.number_present) {
        if (!value.integer) return value.reg;
        MIR_reg_t result = to_number(c, number(c, NAN));
        MIR_label_t done = label(c);
        branch_truth(c, done, value.number_present, false);
        LmdValue present = value; present.kind = K_NUMBER; present.number_present = 0;
        move(c, result, reg(c, to_number(c, present)), true);
        put_label(c, done); return result;
    }
    if (!is_boxed(value) && value.kind == K_NUMBER && !value.integer) return value.reg;
    if (value.literal) {
        MIR_reg_t r = em_new_reg(&c->em, "number_literal", MIR_T_D);
        move(c, r, MIR_new_double_op(c->em.ctx, value.number), true); return r;
    }
    if (value.integer || (!is_boxed(value) && value.kind == K_BOOL)) {
        MIR_reg_t r = em_new_reg(&c->em, "boolean_number", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, value.reg))); return r;
    }
    value = box(c, value);
    unsigned k = semantic(value);
    if (k == K_NUMBER) return em_unbox_f64_item(&c->em, c, cold_unbox, value.reg);
    if (k == (K_NUMBER | K_UNDEFINED)) {
        MIR_reg_t result = to_number(c, number(c, NAN));
        MIR_label_t done = label(c);
        branch(c, MIR_BEQ, done, reg(c, value.reg), integer(c, ITEM_JS_UNDEFINED));
        move(c, result, reg(c, em_unbox_f64_item(&c->em, c, cold_unbox, value.reg)), true);
        put_label(c, done); return result;
    }
    if (k == K_STRING) return em_call_1(&c->em, "mvp_lmd_string_to_number", MIR_T_D,
        MIR_T_P, reg(c, payload(c, value.reg)), true);
    if (k == K_NULL || k == K_UNDEFINED) return to_number(c, number(c, k == K_NULL ? 0 : NAN));
    // consume the shared inline-number guard once; only other encodings need JS coercion.
    return em_unbox_f64_item(&c->em, c, coerce_noninline_number, value.reg);
}
static MIR_reg_t truth(LmdCompiler* c, LmdValue v) {
    unsigned k = semantic(v);
    if (v.number_present) {
        LmdValue present = v; present.kind = K_NUMBER; present.number_present = 0;
        return op(c, MIR_AND, reg(c, v.number_present), reg(c, truth(c, present)));
    }
    if (k == K_NUMBER) {
        if (v.integer) return op(c, MIR_NE, reg(c, v.reg), integer(c, 0));
        MIR_reg_t d = to_number(c, v);
        MIR_reg_t nonzero = op(c, MIR_DNE, reg(c, d), MIR_new_double_op(c->em.ctx, 0));
        return op(c, MIR_AND, reg(c, nonzero), reg(c, op(c, MIR_DEQ, reg(c, d), reg(c, d))));
    }
    if (k == K_BOOL) return is_boxed(v) ? payload(c, v.reg) : v.reg;
    if (k == K_NULL || k == K_UNDEFINED) return constant(c, 0);
    if (k == K_ARRAY || k == K_TYPED_ARRAY || k == K_FUNCTION || k == K_OBJECT || k == K_MAP) return constant(c, 1);
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
        put_label(c, labels[i]); move(c, result, reg(c, truth(c, {v.reg, kinds[i] | K_BOXED}))); jump(c, done);
    }
    put_label(c, done); return result;
}
static MIR_reg_t nullish(LmdCompiler* c, LmdValue v) {
    if (!(semantic(v) & (K_NULL | K_UNDEFINED))) return constant(c, 0);
    MIR_reg_t t = tag(c, v);
    return op(c, MIR_OR, reg(c, op(c, MIR_EQ, reg(c, t), integer(c, LMD_TYPE_NULL))),
        reg(c, op(c, MIR_EQ, reg(c, t), integer(c, LMD_TYPE_UNDEFINED))));
}
static void branch_condition(LmdCompiler* c, AstNode* node, MIR_label_t target, bool yes) {
    // a condition consumes truth directly; short-circuit operands need no boxed merge.
    if (node->node_type == AST_NODE_UNARY && ((AstUnaryNode*)node)->op == OPERATOR_NOT) {
        branch_condition(c, ((AstUnaryNode*)node)->operand, target, !yes); return;
    }
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        if (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR) {
            bool skip_on = binary->op == OPERATOR_OR;
            MIR_label_t skip = skip_on == yes ? target : label(c);
            branch_condition(c, binary->left, skip, skip_on);
            branch_condition(c, binary->right, target, yes);
            if (skip != target) put_label(c, skip);
            return;
        }
    }
    branch_truth(c, target, truth(c, expression(c, node)), yes);
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
    v = box(c, v);
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
        put_label(c, cases[i]); move(c, result, reg(c, to_string(c, {v.reg, kinds[i] | K_BOXED}).reg)); jump(c, done);
    }
    put_label(c, done); return boxed(c, result, K_STRING);
}
static MIR_reg_t integer_lane(LmdCompiler* c, MIR_reg_t d, double lower, double upper,
        MIR_label_t miss, bool exact) {
    branch_truth(c, miss, op(c, MIR_DGE, reg(c, d), MIR_new_double_op(c->em.ctx, lower)), false);
    branch_truth(c, miss, op(c, MIR_DLE, reg(c, d), MIR_new_double_op(c->em.ctx, upper)), false);
    MIR_reg_t i = em_new_reg(&c->em, "integer", MIR_T_I64);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, i), reg(c, d)));
    if (exact) {
        MIR_reg_t round = em_new_reg(&c->em, "integer_roundtrip", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, round), reg(c, i)));
        branch_truth(c, miss, op(c, MIR_DEQ, reg(c, d), reg(c, round)), false);
    }
    return i;
}
static MIR_reg_t int32(LmdCompiler* c, LmdValue v) {
    if (v.integer) {
        MIR_reg_t result = em_new_reg(&c->em, "int32", MIR_T_I64);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_EXT32, reg(c, result), reg(c, v.reg)));
        return result;
    }
    MIR_reg_t d = to_number(c, v);
    MIR_label_t zero = label(c), done = label(c), slow = label(c), narrow = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "int32", MIR_T_I64);
    // truncating finite bounded Numbers before EXT32 is exactly ToInt32, including fractions.
    move(c, result, reg(c, integer_lane(c, d, -9007199254740992.0, 9007199254740992.0, slow, false)));
    jump(c, narrow); put_label(c, slow);
    MIR_reg_t bounded = em_call_2(&c->em, "fmod", MIR_T_D, MIR_T_D, reg(c, d),
        MIR_T_D, MIR_new_double_op(c->em.ctx, 4294967296.0), true);
    branch_truth(c, zero, op(c, MIR_DEQ, reg(c, bounded), reg(c, bounded)), false);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, result), reg(c, bounded)));
    put_label(c, narrow);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_EXT32, reg(c, result), reg(c, result)));
    jump(c, done); put_label(c, zero); move(c, result, integer(c, 0)); put_label(c, done); return result;
}

static LmdValue binary_value(LmdCompiler* c, Operator operation, LmdValue left, LmdValue right);
static LmdValue equal(LmdCompiler* c, LmdValue left, LmdValue right, bool loose) {
    if (!loose && (semantic(left) == K_NULL || semantic(right) == K_NULL ||
            semantic(left) == K_UNDEFINED || semantic(right) == K_UNDEFINED))
        return {op(c, MIR_EQ, reg(c, box(c, left).reg), reg(c, box(c, right).reg)), K_BOOL};
    if (left.integer && right.integer) {
        MIR_reg_t result = op(c, MIR_EQ, reg(c, left.reg), reg(c, right.reg));
        if (left.number_present || right.number_present) {
            // absent payloads are zero; equality also requires matching presence.
            MIR_reg_t lp = left.number_present ? left.number_present : constant(c, 1);
            MIR_reg_t rp = right.number_present ? right.number_present : constant(c, 1);
            result = op(c, MIR_AND, reg(c, result), reg(c, op(c, MIR_EQ, reg(c, lp), reg(c, rp))));
        }
        return {result, K_BOOL};
    }
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
        // physical container kinds all participate in JS object identity (S1.11).
        MIR_reg_t objects[2] = {constant(c, 0), constant(c, 0)};
        MIR_reg_t types[2] = {lt, rt};
        const TypeId object_types[] = {LMD_TYPE_ARRAY, LMD_TYPE_ARRAY_NUM, LMD_TYPE_MAP, LMD_TYPE_FUNC};
        for (int i = 0; i < 2; i++) for (TypeId type : object_types)
            move(c, objects[i], reg(c, op(c, MIR_OR, reg(c, objects[i]),
                reg(c, op(c, MIR_EQ, reg(c, types[i]), integer(c, type))))));
        branch_truth(c, done, op(c, MIR_AND, reg(c, objects[0]), reg(c, objects[1])));
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
// keep guarded integer results as Items so an overflowing edge can widen in the same body.
static bool integer_binary(LmdCompiler* c, Operator operation, LmdValue left,
        LmdValue right, LmdValue* out) {
    MirNumericOpPlan plan = {};
    if (left.number_present || right.number_present) return false;
    if (!em_numeric_op_plan(operation, &plan, true) ||
            (!plan.is_comparison && operation != OPERATOR_ADD && operation != OPERATOR_SUB &&
             operation != OPERATOR_MOD)) return false;
    if ((!left.integer && !is_boxed(left)) || (!right.integer && !is_boxed(right)) ||
            !(semantic(left) & semantic(right) & K_NUMBER)) return false;
    MIR_label_t miss = label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "integer_result", MIR_T_I64);
    LmdValue operands[] = {left, right};
    MIR_reg_t ints[2];
    for (int i = 0; i < 2; i++) {
        if (operands[i].integer) ints[i] = operands[i].reg;
        else {
            MIR_reg_t type = op(c, MIR_URSH, reg(c, operands[i].reg), integer(c, 56));
            branch(c, MIR_BNE, miss, reg(c, type), integer(c, LMD_TYPE_INT));
            ints[i] = em_unbox_finite_int_item(&c->em, operands[i].reg);
        }
    }
    if (operation == OPERATOR_MOD) branch(c, MIR_BEQ, miss, reg(c, ints[1]), integer(c, 0));
    MIR_reg_t value = op(c, plan.i64_opcode, reg(c, ints[0]), reg(c, ints[1]));
    if (!plan.is_comparison) {
        if (operation == OPERATOR_MOD) {
            // a negative exact multiple has JS -0, which has no finite integer carrier.
            MIR_label_t nonzero = label(c);
            branch(c, MIR_BNE, nonzero, reg(c, value), integer(c, 0));
            branch(c, MIR_BLT, miss, reg(c, ints[0]), integer(c, 0)); put_label(c, nonzero);
        } else em_branch_int53_outside(&c->em, value, miss);
        value = em_box_finite_int_item(&c->em, value);
    }
    move(c, result, reg(c, value)); jump(c, done); put_label(c, miss);
    MIR_reg_t l = to_number(c, left), r = to_number(c, right);
    LmdValue slow = plan.helper_name ? LmdValue{em_call_2(&c->em, plan.helper_name, MIR_T_D,
        MIR_T_D, reg(c, l), MIR_T_D, reg(c, r), true), K_NUMBER} :
        LmdValue{op(c, plan.f64_opcode, reg(c, l), reg(c, r), plan.is_comparison ? MIR_T_I64 : MIR_T_D),
            plan.is_comparison ? K_BOOL : K_NUMBER};
    move(c, result, reg(c, plan.is_comparison ? slow.reg : box(c, slow).reg)); put_label(c, done);
    *out = plan.is_comparison ? LmdValue{result, K_BOOL} : boxed(c, result, K_NUMBER);
    return true;
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
        bool definite = semantic(left) == K_STRING || semantic(right) == K_STRING;
        MIR_reg_t result = em_new_reg(&c->em, "addition", MIR_T_I64);
        MIR_label_t strings = label(c), done = label(c);
        if (!definite) {
            MIR_reg_t lt = tag(c, left), rt = tag(c, right);
            branch(c, MIR_BEQ, strings, reg(c, lt), integer(c, LMD_TYPE_STRING));
            branch(c, MIR_BEQ, strings, reg(c, rt), integer(c, LMD_TYPE_STRING));
            LmdValue numeric_left = left, numeric_right = right;
            numeric_left.kind &= ~K_STRING; numeric_right.kind &= ~K_STRING;
            move(c, result, reg(c, box(c, binary_value(c, OPERATOR_ADD,
                numeric_left, numeric_right)).reg)); jump(c, done);
        }
        put_label(c, strings);
        LmdValue l = to_string(c, left), r = to_string(c, right);
        MIR_reg_t joined = em_call_2(&c->em, "mvp_lmd_string_concat", MIR_T_I64,
            MIR_T_I64, reg(c, l.reg), MIR_T_I64, reg(c, r.reg), true);
        check_error(c, joined); move(c, result, reg(c, joined)); put_label(c, done);
        return boxed(c, result, definite ? K_STRING : K_NUMBER | K_STRING);
    }
    MirNumericOpPlan plan = {};
    bool planned = em_numeric_op_plan(operation, &plan, true);
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
        bool integral = left.integer && right.integer && !left.number_present && !right.number_present;
        if (left.integer && right.integer && plan.is_comparison) {
            MIR_reg_t result = op(c, plan.i64_opcode, reg(c, left.reg), reg(c, right.reg));
            // undefined is unordered; the zero payload is only a storage convention.
            if (left.number_present) result = op(c, MIR_AND, reg(c, result), reg(c, left.number_present));
            if (right.number_present) result = op(c, MIR_AND, reg(c, result), reg(c, right.number_present));
            return {result, K_BOOL};
        }
        if (c->integer_bail && integral && !plan.is_comparison) {
            MIR_reg_t l = left.reg, r = right.reg, value;
            if (operation == OPERATOR_MUL) {
                bool right_factor = right.literal && right.number > 0;
                LmdValue factor = right_factor ? right : left;
                MIR_reg_t input = right_factor ? l : r;
                // admission requires a positive literal factor; guard before multiplying.
                uint64_t maximum = INT53_MAX / (uint64_t)factor.number;
                branch(c, MIR_UBGT, c->integer_bail, reg(c, input), integer(c, maximum));
            }
            if (operation == OPERATOR_DIV) {
                uint64_t divisor = (uint64_t)right.number;
                branch_truth(c, c->integer_bail, op(c, MIR_AND, reg(c, l), integer(c, divisor - 1)));
                unsigned shift = 0;
                for (; divisor > 1; divisor >>= 1) shift++;
                value = op(c, MIR_URSH, reg(c, l), integer(c, shift));
            } else value = op(c, plan.i64_opcode, reg(c, l), reg(c, r));
            // operands are nonnegative int53 values; add/sub intermediates fit i64.
            if (operation == OPERATOR_ADD || operation == OPERATOR_SUB)
                branch(c, MIR_UBGT, c->integer_bail, reg(c, value), integer(c, INT53_MAX));
            bool positive = operation == OPERATOR_ADD ? left.range.lower > 0 || right.range.lower > 0 :
                operation == OPERATOR_MUL ? left.range.lower > 0 && right.range.lower > 0 :
                operation == OPERATOR_DIV && left.range.lower > 0;
            return {value, K_NUMBER, false, 0, true, finite_range(positive ? 1 : 0, INT53_MAX)};
        }
        if (left.literal && right.literal && left.integer && right.integer &&
                (operation == OPERATOR_ADD || operation == OPERATOR_SUB || operation == OPERATOR_MUL))
            return number(c, operation == OPERATOR_ADD ? left.number + right.number :
                operation == OPERATOR_SUB ? left.number - right.number : left.number * right.number);
        LmdRange bounds = range_binary(operation, left.range, right.range);
        if (integral && bounds.state == 1 &&
                (operation == OPERATOR_ADD || operation == OPERATOR_SUB || operation == OPERATOR_MUL || operation == OPERATOR_MOD)) {
            return {op(c, plan.i64_opcode, reg(c, left.reg), reg(c, right.reg)), K_NUMBER, false, 0, true, bounds};
        }
        LmdValue integer_result;
        if (integer_binary(c, operation, left, right, &integer_result)) return integer_result;
        MIR_reg_t l = to_number(c, left), r = to_number(c, right);
        if (operation == OPERATOR_MOD && right.literal && right.number >= 1 &&
                right.number <= 9007199254740992.0 && right.number == trunc(right.number)) {
            uint64_t divisor = (uint64_t)right.number;
            if (!(divisor & (divisor - 1))) {
                MIR_label_t slow = label(c), done = label(c);
                MIR_reg_t result = em_new_reg(&c->em, "remainder", MIR_T_D);
                // positive exact integers admit masking; zero/negative/fractional values retain fmod's sign rules.
                MIR_reg_t i = integer_lane(c, l, 1, 9007199254740991.0, slow, true);
                MIR_reg_t bits = op(c, MIR_AND, reg(c, i), integer(c, divisor - 1));
                em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, result), reg(c, bits)));
                jump(c, done); put_label(c, slow);
                move(c, result, reg(c, em_call_2(&c->em, plan.helper_name, MIR_T_D,
                    MIR_T_D, reg(c, l), MIR_T_D, reg(c, r), true)), true);
                put_label(c, done); return {result, K_NUMBER};
            }
        }
        if (operation == OPERATOR_DIV && right.literal && right.number >= 1 && isfinite(right.number)) {
            int exponent;
            if (frexp(right.number, &exponent) == 0.5)
                // division by a positive power of two has the same single rounding as exact scaling.
                return {op(c, MIR_DMUL, reg(c, l), MIR_new_double_op(c->em.ctx, 1.0 / right.number), MIR_T_D), K_NUMBER};
        }
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
    return {bits, K_NUMBER, false, 0, true, finite_range(
        operation == OPERATOR_JS_URSHIFT ? 0 : INT32_MIN,
        operation == OPERATOR_JS_URSHIFT ? UINT32_MAX : INT32_MAX)};
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
static LmdValue read_local_value(LmdCompiler* c, LmdValue value, bool borrow_scalar = false);
static void scalar_field_write(LmdCompiler* c, LmdScalarField* field, LmdValue value);
static LmdScalarField* inline_slot(LmdCompiler* c, LmdBinding* binding) {
    for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent)
        for (int i = 0; i < frame->count; i++) if (frame->bindings[i] == binding) return &frame->slots[i];
    return NULL;
}
static LmdValue read_binding(LmdCompiler* c, LmdBinding* b, AstNode* site, bool borrow_scalar = false) {
    if (LmdScalarField* slot = inline_slot(c, b)) {
        if (is_boxed(slot->value)) check_tdz(c, b, slot->value.reg, site);
        return read_local_value(c, slot->value, borrow_scalar);
    }
    if (!b) { fail(c, LMD_MVP_REFERENCE, site); return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED); }
    if (b->intrinsic >= 4) { fail(c, LMD_MVP_CAPABILITY, site); return boxed(c, constant(c, ITEM_JS_UNDEFINED)); }
    if (b->intrinsic) return b->intrinsic == 1
        ? boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED)
        : number(c, b->intrinsic == 2 ? NAN : INFINITY);
    MIR_reg_t source = b->native ? b->reg : binding_item(c, b);
    if (!b->native) check_tdz(c, b, source, site);
    bool floating = b->native && (b->kinds & K_NUMBER) && !b->integer;
    MIR_reg_t copy = em_new_reg(&c->em, "read", floating ? MIR_T_D : MIR_T_I64);
    move(c, copy, reg(c, source), floating);
    if (b->native) {
        MIR_reg_t present = 0;
        if (b->number_present) {
            present = em_new_reg(&c->em, "present_snapshot", MIR_T_I64);
            move(c, present, reg(c, b->number_present));
        }
        return {copy, b->kinds, false, 0, b->integer, range(c->program, site, true, present != 0), present};
    }
    // parameter kinds cover every incoming call and assignment, including missing arguments.
    bool known = b->entry->is_parameter || b->dominated || (b->entry->is_const && b->writes == 1) ||
        (b->target && b->target->ast->entry == b->entry &&
        b->entry->node && b->entry->node->node_type == AST_NODE_FUNC && !b->entry->is_annex_b_companion);
    if (known && !(b->kinds & K_NUMBER)) return boxed(c, copy, b->kinds);
    LmdValue result = borrow_scalar ? boxed(c, copy) : stable_item(c, copy);
    if (known) result.kind = b->kinds | K_BOXED;
    return result;
}
static void write_binding(LmdCompiler* c, LmdBinding* b, LmdValue v, AstNode* site, bool initialize = false) {
    if (!b) { fail(c, c->strict ? LMD_MVP_REFERENCE : LMD_MVP_CAPABILITY, site); return; }
    if (b->intrinsic >= 4) { fail(c, LMD_MVP_CAPABILITY, site); return; }
    if (b->intrinsic) { if (c->strict) fail(c, LMD_MVP_TYPE, site); return; }
    LmdScalarField* local = inline_slot(c, b);
    if (!initialize && (local ? is_boxed(local->value) : !b->native))
        check_tdz(c, b, local ? local->value.reg : binding_item(c, b), site);
    if (!initialize && (b->entry->is_const || b->entry->is_function_name_binding)) {
        if (!b->entry->is_function_name_binding || c->strict) fail(c, LMD_MVP_TYPE, site);
        return;
    }
    if (local) {
        if (c->integer_bail && v.range.lower < 1)
            branch(c, MIR_BEQ, c->integer_bail, reg(c, v.reg), integer(c, 0));
        scalar_field_write(c, local, v); return;
    }
    if (b->native) {
        if (b->number_present) {
            MIR_reg_t present = v.number_present ? v.number_present :
                op(c, MIR_NE, reg(c, tag(c, v)), integer(c, LMD_TYPE_UNDEFINED));
            move(c, b->number_present, reg(c, present));
            move(c, b->reg, reg(c, scalar_reg(c, v, b->kinds, b->integer)), !b->integer);
        } else move(c, b->reg, reg(c, scalar_reg(c, v, b->kinds, b->integer)), b->kinds == K_NUMBER && !b->integer);
    } else {
        v = box(c, v);
        if (!b->owner->id) em_call_void_4(&c->em, "owned_item_slot_store", MIR_T_P, reg(c, module_slots(c)),
            MIR_T_I64, integer(c, c->program->slot_count), MIR_T_I64, integer(c, b->slot),
            MIR_T_I64, reg(c, v.reg), true);
        else if (!(semantic(v) & K_NUMBER)) move(c, b->reg, reg(c, v.reg));
        else {
            if (!b->scalar_home) b->scalar_home = em_scalar_home_new(&c->em);
            MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, b->scalar_home));
            MIR_reg_t owned = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, v.reg, address);
            move(c, b->reg, reg(c, owned)); em_scalar_home_bind(&c->em, b->scalar_home, b->reg);
        }
    }
}
static MIR_reg_t property_key(LmdCompiler* c, LmdValue value, bool typed = false) {
    double maximum = typed ? INT53_MAX : UINT32_MAX - 1.0;
    int64_t invalid = typed ? -3 : -2;
    if (value.literal) return constant(c, value.number >= 0 && value.number <= maximum &&
        value.number == trunc(value.number) ? (uint64_t)value.number : (uint64_t)invalid);
    if (value.integer) {
        bool bounded = value.range.state == 1 && value.range.lower >= 0 && value.range.upper <= maximum;
        if (bounded && !value.number_present)
            return value.reg;
        MIR_reg_t index = constant(c, value.number_present ? (uint64_t)-2 : (uint64_t)invalid);
        MIR_label_t done = label(c);
        if (value.number_present) {
            branch_truth(c, done, value.number_present, false);
            move(c, index, MIR_new_int_op(c->em.ctx, invalid));
        }
        if (!bounded) branch(c, MIR_UBGT, done, reg(c, value.reg), integer(c, (uint64_t)maximum));
        move(c, index, reg(c, value.reg)); put_label(c, done); return index;
    }
    unsigned k = semantic(value);
    MIR_reg_t result = em_new_reg(&c->em, "index", MIR_T_I64);
    MIR_label_t numeric = label(c), done = label(c);
    if (value.number_present) {
        move(c, result, MIR_new_int_op(c->em.ctx, -2));
        branch_truth(c, done, value.number_present, false);
    } else if (k != K_NUMBER) {
        if (k != K_STRING) branch(c, MIR_BEQ, numeric, reg(c, tag(c, value)), integer(c, LMD_TYPE_FLOAT));
        LmdValue string = to_string(c, value);
        move(c, result, reg(c, em_call_2(&c->em, "mvp_lmd_string_key", MIR_T_I64,
            MIR_T_P, reg(c, payload(c, string.reg)), MIR_T_I64, integer(c, typed), true))); jump(c, done);
    }
    put_label(c, numeric);
    MIR_reg_t d = to_number(c, value);
    move(c, result, MIR_new_int_op(c->em.ctx, invalid));
    MIR_reg_t i = integer_lane(c, d, 0, maximum, done, true);
    move(c, result, reg(c, i)); put_label(c, done); return result;
}
static bool shape_union(LmdShapeSet* target, LmdShapeSet incoming) {
    bool changed = false;
    for (int i = 0; i < incoming.count; i++) {
        bool present = false;
        for (int j = 0; j < target->count; j++) present |= target->plans[j] == incoming.plans[i];
        if (!present && target->count < 4) {
            target->plans[target->count++] = incoming.plans[i]; changed = true;
        }
    }
    return changed;
}
static LmdShapeSet object_shapes(MvpLmdProgram* p, AstNode* node, int depth = 0) {
    if (!node || depth >= 16) return {};
    if (node->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(p, (AstIdentNode*)node);
        return b ? b->shapes : LmdShapeSet{};
    }
    if (node->node_type == AST_NODE_CALL_EXPR) {
        LmdFunction* f = direct_target(p, (AstCallNode*)node);
        return f ? f->shapes : LmdShapeSet{};
    }
    if (node->node_type == AST_NODE_MEMBER_EXPR) {
        AstFieldNode* field = (AstFieldNode*)node;
        if (field->computed) return {};
        String* key = ((AstIdentNode*)field->property)->name;
        LmdShapeSet owners = object_shapes(p, field->object, depth + 1), result = {};
        for (int i = 0; i < owners.count; i++) {
            for (AstNode* e = ((AstMapNode*)owners.plans[i]->literal)->properties; e; e = e->next) {
                AstPropertyNode* prop = (AstPropertyNode*)e;
                String* name = ((AstIdentNode*)prop->key)->name;
                if (key->len == name->len && !memcmp(key->chars, name->chars, key->len))
                    shape_union(&result, object_shapes(p, prop->value, depth + 1));
            }
        }
        return result;
    }
    if (node->node_type == AST_NODE_CONDITIONAL_EXPR) {
        LmdShapeSet result = object_shapes(p, ((AstIfNode*)node)->then, depth + 1);
        shape_union(&result, object_shapes(p, ((AstIfNode*)node)->otherwise, depth + 1));
        return result;
    }
    for (int i = 0; p->objects && i < p->objects->length; i++) {
        LmdObjectPlan* plan = (LmdObjectPlan*)p->objects->data[i];
        if (plan->literal == node) return {1, {plan}};
    }
    return {};
}
static LmdObjectPlan* object_plan(MvpLmdProgram* p, AstNode* node) {
    return object_shapes(p, node).plans[0];
}
static LmdScalarField* scalar_field(MvpLmdProgram* p, AstNode* node) {
    AstFieldNode* member = (AstFieldNode*)node;
    if (member->computed || member->object->node_type != AST_NODE_IDENT) return NULL;
    LmdBinding* b = identifier_binding(p, (AstIdentNode*)member->object);
    LmdObjectPlan* plan = b ? b->shape_hint : NULL;
    if (!plan || plan->scalar_binding != b) return NULL;
    String* key = ((AstIdentNode*)member->property)->name;
    ShapeEntry* field = typemap_hash_lookup(plan->blueprint, key->chars, key->len);
    int i = 0;
    FOR_EACH_MAP_FIELD(plan->blueprint, entry) {
        if (entry == field) return &plan->scalar_fields[i];
        i++;
    }
    return NULL;
}
static AstNode* scalar_field_initializer(MvpLmdProgram* p, AstNode* node) {
    LmdScalarField* field = scalar_field(p, node);
    return field && !field->written ? field->initializer : NULL;
}
static LmdScalarField* scalar_reference(LmdReference ref) {
    if (!ref.plan || !ref.plan->scalar_binding) return NULL;
    int index = 0;
    FOR_EACH_MAP_FIELD(ref.plan->blueprint, field) {
        if (field == ref.field) return &ref.plan->scalar_fields[index];
        index++;
    }
    return NULL;
}
static LmdValue read_local_value(LmdCompiler* c, LmdValue value, bool borrow_scalar) {
    if (is_boxed(value)) {
        if (borrow_scalar || !(semantic(value) & K_NUMBER)) {
            MIR_reg_t copy = em_new_reg(&c->em, "local_borrow", MIR_T_I64);
            move(c, copy, reg(c, value.reg)); return boxed(c, copy, semantic(value));
        }
        LmdValue copy = stable_item(c, value.reg); copy.kind = value.kind; return copy;
    }
    bool floating = (semantic(value) & K_NUMBER) && !value.integer;
    MIR_reg_t copy = em_new_reg(&c->em, "local_read", floating ? MIR_T_D : MIR_T_I64);
    move(c, copy, reg(c, value.reg), floating); value.reg = copy; value.literal = false;
    if (value.number_present) {
        MIR_reg_t present = em_new_reg(&c->em, "local_present", MIR_T_I64);
        move(c, present, reg(c, value.number_present)); value.number_present = present;
    }
    return value;
}
static void scalar_field_write(LmdCompiler* c, LmdScalarField* field, LmdValue value) {
    if (!field->written) { field->value = is_boxed(value) ? read_local_value(c, value) : value; return; }
    bool native = field->kinds == K_NUMBER || field->kinds == K_BOOL;
    bool floating = field->kinds == K_NUMBER && !field->integer;
    if (!field->value.reg) {
        field->value = {em_new_reg(&c->em, "local_field", floating ? MIR_T_D : MIR_T_I64),
            field->kinds | (native ? 0u : (unsigned)K_BOXED), false, 0, field->integer, field->range};
        if (!native) {
            boxed(c, field->value.reg);
            field->home = em_scalar_home_new(&c->em);
        }
    }
    MIR_reg_t stored;
    if (native) stored = scalar_reg(c, value, field->kinds, field->integer);
    else if (!(semantic(value) & K_NUMBER)) stored = box(c, value).reg;
    else {
        MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, field->home));
        stored = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_F64, box(c, value).reg, address);
    }
    move(c, field->value.reg, reg(c, stored), floating);
    if (!native) em_scalar_home_bind(&c->em, field->home, field->value.reg);
}
static LmdObjectPlan* value_plan(LmdCompiler* c, AstNode* node) {
    if (node && node->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(c->program, (AstIdentNode*)node);
        for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent)
            for (int i = 0; i < frame->count; i++) if (frame->bindings[i] == b) return frame->plans[i];
    }
    return object_plan(c->program, node);
}
static MIR_reg_t planned_shape(LmdCompiler* c, LmdObjectPlan* plan) {
    return em_load_at(&c->em, constant(c, (uint64_t)plan), offsetof(LmdObjectPlan, shape), MIR_T_I64, "literal_shape");
}
static LmdValue field_read(LmdCompiler* c, LmdReference ref) {
    ShapeEntry* field = ref.field;
    TypeId type = field->type->type_id;
    MIR_reg_t data = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "field_data");
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED)
        return boxed(c, constant(c, type == LMD_TYPE_NULL ? ITEM_NULL : ITEM_JS_UNDEFINED));
    MIR_reg_t value = em_load_at(&c->em, data, field->byte_offset,
        type == LMD_TYPE_FLOAT ? MIR_T_D : type == LMD_TYPE_BOOL ? MIR_T_U8 : MIR_T_I64, "field_value");
    if (type == LMD_TYPE_INT) return {value, K_NUMBER, false, 0, true};
    if (type == LMD_TYPE_FLOAT) return {value, K_NUMBER};
    if (type == LMD_TYPE_BOOL) return {value, K_BOOL};
    if (type == LMD_TYPE_STRING || type == LMD_TYPE_FUNC)
        value = op(c, MIR_OR, reg(c, value), integer(c, (uint64_t)type << 56));
    return boxed(c, value);
}
static bool field_write(LmdCompiler* c, LmdReference ref, LmdValue value, MIR_label_t miss, bool fresh = false) {
    TypeId type = ref.field->type->type_id;
    unsigned k = semantic(value);
    bool matches = type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ? k == K_NUMBER :
        type == LMD_TYPE_BOOL ? k == K_BOOL : type == LMD_TYPE_NULL ? k == K_NULL :
        type == LMD_TYPE_UNDEFINED ? k == K_UNDEFINED : type == LMD_TYPE_STRING ? k == K_STRING :
        type == LMD_TYPE_FUNC ? k == K_FUNCTION : type == LMD_TYPE_ARRAY ? k == K_ARRAY :
        type == LMD_TYPE_MAP ? k == K_OBJECT || k == K_MAP : false;
    bool optional_object = type == LMD_TYPE_MAP && (k & K_OBJECT) && !(k & ~(K_OBJECT | K_NULL | K_UNDEFINED));
    if (!matches && !optional_object) return false;
    if (!matches) branch(c, MIR_BNE, miss, reg(c, tag(c, value)), integer(c, LMD_TYPE_MAP));
    // this guard runs after the RHS: calls and computed keys may have reshaped the owner.
    if (!fresh) em_guard_map_shape(&c->em, ref.owner.reg, planned_shape(c, ref.plan), miss,
        semantic(ref.owner) == K_OBJECT || semantic(ref.owner) == K_MAP);
    MIR_reg_t data = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_MAP_DATA, MIR_T_I64, "field_data");
    if ((type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT) && !value.integer) {
        MIR_reg_t raw = op(c, MIR_URSH, reg(c, box(c, value).reg), integer(c, 56));
        branch(c, type == LMD_TYPE_INT ? MIR_BNE : MIR_BEQ, miss, reg(c, raw), integer(c, LMD_TYPE_INT));
    } else if (type == LMD_TYPE_FLOAT && value.integer) jump(c, miss);
    MIR_reg_t stored = type == LMD_TYPE_INT ? (value.integer ? value.reg : em_unbox_finite_int_item(&c->em, box(c, value).reg)) : type == LMD_TYPE_FLOAT ? to_number(c, value) :
        type == LMD_TYPE_BOOL ? truth(c, value) : type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED ? constant(c, 0) :
        type == LMD_TYPE_STRING || type == LMD_TYPE_FUNC ? payload(c, box(c, value).reg) : box(c, value).reg;
    em_store_at(&c->em, data, ref.field->byte_offset,
        type == LMD_TYPE_FLOAT ? MIR_T_D : type == LMD_TYPE_BOOL || type == LMD_TYPE_UNDEFINED ? MIR_T_U8 : MIR_T_I64, stored);
    return true;
}
static MIR_reg_t property_access(LmdCompiler* c, int operation, MIR_reg_t owner,
        MIR_reg_t name = 0, MIR_reg_t value = 0) {
    if (c->program->receiver_abi)
        return em_call_4(&c->em, "mvp_lmd_class_property", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, name ? reg(c, name) : integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, value ? reg(c, value) : integer(c, ITEM_JS_UNDEFINED),
            MIR_T_I64, integer(c, operation), true);
    if (operation >= LMD_PROP_KEYS)
        return em_call_2(&c->em, "mvp_lmd_object_project", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, integer(c, operation - LMD_PROP_KEYS), true);
    if (operation == LMD_PROP_DELETE)
        return em_call_2(&c->em, "mvp_lmd_property_delete", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, name), true);
    const char* helper = operation == LMD_PROP_SET ? "mvp_lmd_property_set" :
        operation >= LMD_PROP_OWN ? "mvp_lmd_property_has" : "mvp_lmd_property_get";
    return em_call_3(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, name),
        MIR_T_I64, operation == LMD_PROP_SET ? reg(c, value) :
            integer(c, operation == LMD_PROP_CALLEE || operation == LMD_PROP_HAS), true);
}
static LmdValue canonical_key(LmdCompiler* c, LmdValue value) {
    LmdValue string = to_string(c, value);
    MIR_reg_t key = em_call_1(&c->em, "mvp_lmd_property_key", MIR_T_I64, MIR_T_I64, reg(c, string.reg), true);
    check_error(c, key); return boxed(c, key, K_STRING);
}
static LmdReference reference(LmdCompiler* c, AstNode* n, bool capture_name = false) {
    if (n->node_type == AST_NODE_IDENT) return {identifier_binding(c->program, (AstIdentNode*)n), {}, 0};
    AstFieldNode* field = (AstFieldNode*)n;
    LmdObjectPlan* local = value_plan(c, field->object);
    if (local && local->scalar_binding) {
        String* name = ((AstIdentNode*)field->property)->name;
        ShapeEntry* entry = typemap_hash_lookup(local->blueprint, name->chars, name->len);
        return {NULL, {}, 1, true, 0, 0, local, entry};
    }
    LmdValue owner = box(c, expression(c, field->object));
    LmdValue key = field->computed ? expression(c, field->property)
        : boxed(c, constant(c, s2it(((AstIdentNode*)field->property)->name)), K_STRING);
    LmdValue name = {};
    bool typed = semantic(owner) == K_TYPED_ARRAY;
    bool known = !field->computed || field->property->node_type == AST_NODE_LITERAL;
    int64_t index = -2;
    if (!field->computed) index = mvp_lmd_string_key(((AstIdentNode*)field->property)->name, typed);
    else if (known) {
        AstLiteralNode* literal = (AstLiteralNode*)field->property;
        if (literal->literal_type == AST_LITERAL_STRING) index = mvp_lmd_string_key(literal->value.string_value, typed);
        else if (literal->literal_type == AST_LITERAL_NUMBER) {
            double d = literal->value.number_value;
            index = d >= 0 && d <= (typed ? INT53_MAX : UINT32_MAX - 1.0) && d == trunc(d)
                ? (int64_t)d : typed ? -3 : -2;
        }
    }
    if (typed || semantic(owner) == K_ARRAY || semantic(owner) == K_STRING) {
        LmdReference ref = {NULL, owner, known ? constant(c, (uint64_t)index) : property_key(c, key, typed), known, index};
        AstNode* source = field->object;
        if (source->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(c->program, (AstIdentNode*)source);
            source = b && b->entry->is_const && b->writes == 1 ? b->initializer : NULL;
        }
        // evaluating the owner above retains TDZ and source effects before using immutable literal facts.
        if (source && source->node_type == AST_NODE_LITERAL &&
                ((AstLiteralNode*)source)->literal_type == AST_LITERAL_STRING)
            ref.string_literal = ((AstLiteralNode*)source)->value.string_value;
        ref.spelling = member_spelling(field);
        if (capture_name) ref.name = to_string(c, key).reg;
        LmdArrayFacts facts = array_facts(c->program, field->object);
        ref.lanes = facts.lanes;
        ref.elements = facts.elements;
        ref.numeric_key = semantic(key) == K_NUMBER || key.number_present;
        ref.key_present = key.number_present;
        // a present integer element retains its range even when the producer can be absent.
        LmdRange bounds = known ? finite_range(index, index) : key.integer && key.range.state == 1
            ? key.range : range(c->program, field->property, true);
        ref.in_bounds = !(facts.lanes & LMD_ARRAY_UNKNOWN) && bounds.state == 1 && facts.length.state == 1 && bounds.lower >= 0 && bounds.upper < facts.length.lower;
        return ref;
    }
    if (!field->computed) {
        String* s = name_pool_create_string(c->program->frontend->name_pool, ((AstIdentNode*)field->property)->name);
        name = boxed(c, constant(c, s2it(s)), K_STRING);
    } else name = canonical_key(c, key);
    MIR_reg_t slot = known ? constant(c, (uint64_t)index) : property_key(c, name);
    LmdObjectPlan* plan = known ? value_plan(c, field->object) : NULL;
    ShapeEntry* entry = NULL;
    String* spelling = member_spelling(field);
    if (plan && spelling) entry = typemap_hash_lookup(plan->blueprint, spelling->chars, spelling->len);
    bool guarded = false;
    if (field->object->node_type == AST_NODE_IDENT) {
        LmdBinding* b = identifier_binding(c->program, (AstIdentNode*)field->object);
        for (LmdInlineFrame* frame = c->inlining; frame; frame = frame->parent)
            for (int i = 0; i < frame->count; i++) if (frame->bindings[i] == b) guarded |= frame->guarded[i];
    }
    return {NULL, owner, slot, known, index, name.reg, plan, entry,
        spelling ? mvp_lmd_builtin_method(spelling) : 0, known ? object_shapes(c->program, field->object) : LmdShapeSet{}, spelling, guarded};
}
static void array_key_check(LmdCompiler* c, LmdReference ref, AstNode* site) {
    if (ref.key_known) { if (ref.index < -1) fail(c, LMD_MVP_CAPABILITY, site); }
    else {
        MIR_label_t valid = label(c);
        branch(c, MIR_BGE, valid, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        fail(c, LMD_MVP_CAPABILITY, site); put_label(c, valid);
    }
}
static void property_failure(LmdCompiler* c, LmdValue owner, AstNode* site) {
    MIR_label_t type = label(c);
    branch_truth(c, type, nullish(c, owner));
    fail(c, LMD_MVP_CAPABILITY, site); put_label(c, type);
    fail(c, LMD_MVP_TYPE, site);
}
static LmdValue array_read(LmdCompiler* c, LmdReference ref, bool borrow_scalar = false, bool numeric = false) {
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "array_length");
    if (ref.key_known && ref.index == -1) {
        return {size, K_NUMBER, false, 0, true, finite_range(0, UINT32_MAX)};
    }
    // numeric consumers can merge absence as NaN at the load, without a second conversion branch.
    bool integral = ref.lanes == LMD_ARRAY_ITEMS && ref.elements.state == 1 && (!numeric || ref.in_bounds);
    bool optional = ref.lanes == LMD_ARRAY_ITEMS && (!numeric || integral) && !ref.in_bounds;
    numeric = ref.lanes == LMD_ARRAY_ITEMS;
    bool floating = numeric && !integral;
    MIR_reg_t present = optional ? constant(c, 0) : 0;
    MIR_label_t length = ref.key_known || ref.numeric_key ? NULL : label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "element", floating ? MIR_T_D : MIR_T_I64);
    if (length) branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    move(c, result, floating ? MIR_new_double_op(c->em.ctx, NAN) : integer(c, integral ? 0 : ITEM_JS_UNDEFINED), floating);
    if (!ref.in_bounds) branch(c, MIR_UBGE, done, reg(c, ref.key), reg(c, size));
    MIR_reg_t element = em_load_at(&c->em,
        em_array_element_address(&c->em, ref.owner.reg, ref.key, 8), 0, MIR_T_I64, "element");
    // specialize proven arrays in hole-free units; retain the generic property fallback's CFG.
    if (!numeric && (c->program->array_holes || semantic(ref.owner) != K_ARRAY))
        branch(c, MIR_BEQ, done, reg(c, element), integer(c, ITEM_JS_DELETED_SENTINEL));
    move(c, result, reg(c, integral ? em_unbox_finite_int_item(&c->em, element) :
        numeric ? to_number(c, boxed(c, element, K_NUMBER)) :
        borrow_scalar ? element : stable_item(c, element).reg), floating);
    if (present) move(c, present, integer(c, 1));
    if (length) {
        jump(c, done); put_label(c, length);
        LmdReference length_ref = ref; length_ref.key_known = true; length_ref.index = -1;
        LmdValue value = array_read(c, length_ref);
        move(c, result, reg(c, integral ? value.reg : numeric ? to_number(c, value) : box(c, value).reg), floating);
        if (present) move(c, present, integer(c, 1));
    }
    put_label(c, done); return numeric ? LmdValue{result, present ? K_NUMBER | K_UNDEFINED : K_NUMBER,
        false, 0, integral, length ? LmdRange{} : ref.elements, present} :
        boxed(c, result, ref.lanes == LMD_ARRAY_ITEMS ? K_NUMBER | K_UNDEFINED : K_ANY);
}
static Item* literal_characters(MvpLmdProgram* p) {
    if (p->character_items) return p->character_items;
    Item* items = (Item*)pool_alloc(p->frontend->pool, 128 * sizeof(Item));
    if (!items) { diagnostic(p, NULL, "character table allocation failed"); return NULL; }
    for (int i = 0; i < 128; i++) {
        char byte = (char)i;
        String* character = get_ascii_char_string((unsigned char)i);
        if (!character) character = name_pool_create_len(p->frontend->name_pool, &byte, 1);
        if (!character) { diagnostic(p, NULL, "character literal allocation failed"); return NULL; }
        items[i].item = s2it(character);
    }
    // entries are static ASCII strings or frontend-owned names, never borrowed GC values.
    p->character_items = items;
    return items;
}
static LmdValue string_read(LmdCompiler* c, LmdReference ref) {
    bool ascii_literal = ref.string_literal && ref.string_literal->is_ascii;
    if (ref.key_known && ref.index == -1) {
        if (ascii_literal) return number(c, ref.string_literal->len);
        MIR_reg_t string = payload(c, ref.owner.reg);
        MIR_reg_t size = em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "byte_length");
        MIR_label_t done = label(c);
        MIR_reg_t flags = em_load_at(&c->em, string, offsetof(String, flags), MIR_T_U8, "string_flags");
        branch_truth(c, done, op(c, MIR_AND, reg(c, flags), integer(c, 1)));
        MIR_reg_t bytes = op(c, MIR_ADD, reg(c, string), integer(c, offsetof(String, chars)));
        move(c, size, reg(c, em_call_2(&c->em, "utf8_to_utf16_length", MIR_T_I64,
            MIR_T_P, reg(c, bytes), MIR_T_I64, reg(c, size), true)));
        put_label(c, done);
        return {size, K_NUMBER, false, 0, true, finite_range(0, UINT32_MAX)};
    }
    Item* characters = literal_characters(c->program);
    if (!characters) return boxed(c, constant(c, ITEM_JS_UNDEFINED));
    // numeric keys cannot select length; retain the character result kind through consumers.
    MIR_label_t length = ref.key_known || ref.numeric_key ? NULL : label(c), done = label(c);
    MIR_label_t slow = ascii_literal ? NULL : label(c);
    MIR_reg_t result = em_new_reg(&c->em, "string_property", MIR_T_I64);
    if (length) branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    MIR_reg_t string = ascii_literal ? constant(c, (uint64_t)ref.string_literal) : payload(c, ref.owner.reg);
    if (slow) {
        MIR_reg_t flags = em_load_at(&c->em, string, offsetof(String, flags), MIR_T_U8, "string_flags");
        branch_truth(c, slow, op(c, MIR_AND, reg(c, flags), integer(c, 1)), false);
    }
    move(c, result, integer(c, ITEM_JS_UNDEFINED));
    MIR_reg_t size = ascii_literal ? constant(c, ref.string_literal->len) :
        em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "string_bytes");
    branch(c, MIR_UBGE, done, reg(c, ref.key), reg(c, size));
    MIR_reg_t address = op(c, MIR_ADD, reg(c, string), reg(c, ref.key));
    MIR_reg_t unit = em_load_at(&c->em, address, offsetof(String, chars), MIR_T_U8, "ascii_unit");
    MIR_reg_t entry = op(c, MIR_ADD, integer(c, (uint64_t)characters),
        reg(c, op(c, MIR_LSH, reg(c, unit), integer(c, 3))));
    move(c, result, reg(c, em_load_at(&c->em, entry, 0, MIR_T_I64, "ascii_character")));
    if (slow) {
        jump(c, done); put_label(c, slow);
        MIR_reg_t at = em_call_3(&c->em, "mvp_lmd_string_at", MIR_T_I64,
            MIR_T_I64, reg(c, ref.owner.reg), MIR_T_D, reg(c, to_number(c, {ref.key, K_NUMBER, false, 0, true})),
            MIR_T_I64, integer(c, LMD_STRING_INDEX), true);
        check_error(c, at); move(c, result, reg(c, at));
    }
    if (length) {
        jump(c, done); put_label(c, length);
        LmdReference length_ref = ref; length_ref.key_known = true; length_ref.index = -1;
        move(c, result, reg(c, box(c, string_read(c, length_ref)).reg));
    }
    put_label(c, done);
    return boxed(c, result, length ? K_ANY : K_STRING | K_UNDEFINED);
}
// JS admission/coercion precedes these shared physical lane operations (D2.4.3).
static LmdValue typed_array_lanes(LmdCompiler* c, LmdValue owner, MIR_reg_t index,
        LmdValue* value = NULL, MIR_reg_t end = 0, unsigned lanes = 0, bool native = false) {
    if (!lanes || (lanes & ~7u)) lanes = 7;
    bool single = !(lanes & (lanes - 1)), integral = !(lanes & 4);
    MIR_reg_t lane = single ? 0 : em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "numeric_lane");
    MIR_label_t done = label(c);
    bool floating_result = native && !integral;
    MIR_reg_t result = value ? 0 : em_new_reg(&c->em, "typed_element", floating_result ? MIR_T_D : MIR_T_I64);
    MIR_reg_t converted = 0;
    if (value && lanes != 4) {
        MIR_label_t floating = label(c);
        if (lanes & 4) branch(c, MIR_BEQ, floating, reg(c, lane), integer(c, ELEM_FLOAT64));
        converted = int32(c, *value);
        put_label(c, floating);
    }
    for (int i = 0; i < 3; i++) {
        if (!(lanes & (1u << i))) continue;
        ArrayNumElemType element = typed_lanes[i];
        MIR_type_t storage = em_numeric_storage_type(element);
        MIR_label_t next = label(c);
        if (!single) branch(c, MIR_BNE, next, reg(c, lane), integer(c, element));
        MIR_label_t repeat = end ? label(c) : NULL;
        if (end) {
            put_label(c, repeat);
            branch(c, MIR_UBGE, done, reg(c, index), reg(c, end));
        }
        MIR_reg_t address = em_array_element_address(&c->em, owner.reg, index,
            ELEM_TYPE_SIZE[element >> 4]);
        if (value) em_store_at(&c->em, address, 0, storage,
            storage == MIR_T_D ? to_number(c, *value) : converted);
        else {
            MIR_reg_t loaded = em_load_at(&c->em, address, 0, storage, "numeric_element");
            LmdValue v = {loaded, K_NUMBER, false, 0, storage != MIR_T_D};
            move(c, result, reg(c, native ? (floating_result ? to_number(c, v) : loaded) : box(c, v).reg), floating_result);
        }
        if (end) { move(c, index, reg(c, op(c, MIR_ADD, reg(c, index), integer(c, 1)))); jump(c, repeat); }
        else jump(c, done);
        put_label(c, next);
    }
    if (!single) fail(c, LMD_MVP_CAPABILITY, NULL);
    put_label(c, done);
    return value ? owner : native ? LmdValue{result, K_NUMBER, false, 0, integral,
        integral ? finite_range(lanes & 1 ? INT32_MIN : 0, lanes & 1 ? INT32_MAX : UINT8_MAX) : LmdRange{0, 0, 2}}
        : boxed(c, result, K_NUMBER);
}
static LmdValue typed_array_reference(LmdCompiler* c, LmdReference ref, AstNode* site,
        LmdValue* value = NULL, bool callee = false, bool numeric = false) {
    if (ref.name) {
        ref.key = property_key(c, boxed(c, ref.name, K_STRING), true);
        ref.key_known = false;
    }
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "typed_length");
    if (ref.key_known) {
        if (ref.index == -1) {
            if (!value) return {size, K_NUMBER, false, 0, true, finite_range(0, INT53_MAX)};
            if (c->strict) fail(c, LMD_MVP_TYPE, site);
            return *value;
        }
        if (ref.index == -4 && callee && !value)
            return boxed(c, constant(c, mvp_lmd_method_token(9)));
        if (ref.index == -2 || ref.index == -4) {
            fail(c, LMD_MVP_CAPABILITY, site); return boxed(c, constant(c, ITEM_JS_UNDEFINED));
        }
    }
    bool native = !value && !callee;
    bool integral = native && ref.lanes && !(ref.lanes & ~3u) && (!numeric || ref.in_bounds);
    bool floating = native && !integral;
    MIR_reg_t present = native && (!numeric || integral) ? constant(c, 0) : 0;
    MIR_reg_t result = floating ? to_number(c, number(c, NAN)) : constant(c, integral ? 0 : ITEM_JS_UNDEFINED);
    MIR_label_t length = label(c), fill = label(c), unsupported = label(c), done = label(c);
    bool named_key = !ref.key_known && !ref.numeric_key;
    if (ref.key_present) {
        MIR_label_t present_key = label(c);
        branch_truth(c, present_key, ref.key_present);
        fail(c, LMD_MVP_CAPABILITY, site); put_label(c, present_key);
    }
    if (named_key) {
        branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        branch(c, MIR_BEQ, fill, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -4));
        branch(c, MIR_BEQ, unsupported, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -2));
    }
    // even an invalid canonical numeric index performs ToNumber on the RHS.
    LmdValue stored = value ? *value : LmdValue{};
    if (value && semantic(stored) != K_NUMBER && !(stored.integer && stored.number_present))
        stored = {to_number(c, stored), K_NUMBER};
    if (!ref.in_bounds) branch(c, MIR_UBGE, done, reg(c, ref.key), reg(c, size));
    LmdValue accessed = typed_array_lanes(c, ref.owner, ref.key, value ? &stored : NULL, 0, ref.lanes, native);
    if (ref.in_bounds) return value ? *value : accessed;
    if (!value) move(c, result, reg(c, floating ? to_number(c, accessed) : accessed.reg), floating);
    if (present) move(c, present, integer(c, 1));
    if (named_key) {
        jump(c, done);
        put_label(c, length);
        if (value) { if (c->strict) fail(c, LMD_MVP_TYPE, site); }
        else {
            LmdValue length_value = {size, K_NUMBER, false, 0, true};
            move(c, result, reg(c, integral ? size : native ? to_number(c, length_value) : box(c, length_value).reg), floating);
            if (present) move(c, present, integer(c, 1));
        }
        jump(c, done);
        put_label(c, fill);
        if (callee && !value) { move(c, result, integer(c, mvp_lmd_method_token(9))); jump(c, done); }
        put_label(c, unsupported); fail(c, LMD_MVP_CAPABILITY, site);
    }
    put_label(c, done); return value ? *value : native ?
        LmdValue{result, present ? K_NUMBER | K_UNDEFINED : K_NUMBER, false, 0, integral,
            named_key ? LmdRange{} : accessed.range, present} :
        boxed(c, result, callee ? K_ANY : K_NUMBER | K_UNDEFINED);
}
static LmdValue sequence_reference(LmdCompiler* c, LmdReference ref, unsigned owner_kind,
        AstNode* site, bool callee, bool borrow_scalar = false, bool numeric = false) {
    if (!callee || (ref.key_known && ref.index >= -1)) {
        array_key_check(c, ref, site);
        return owner_kind == K_ARRAY ? array_read(c, ref, borrow_scalar, numeric) : string_read(c, ref);
    }
    int method = ref.spelling ? mvp_lmd_builtin_method(ref.spelling,
        owner_kind == K_ARRAY ? LMD_TYPE_ARRAY : LMD_TYPE_STRING) : 0;
    if (method) return boxed(c, constant(c, mvp_lmd_method_token(method)));
    MIR_label_t indexed = label(c), done = label(c);
    MIR_reg_t result = em_new_reg(&c->em, "sequence_property", MIR_T_I64);
    branch(c, MIR_BGE, indexed, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
    MIR_reg_t member = property_access(c, LMD_PROP_CALLEE, ref.owner.reg, ref.name);
    check_error(c, member); move(c, result, reg(c, member)); jump(c, done);
    put_label(c, indexed);
    move(c, result, reg(c, box(c, owner_kind == K_ARRAY ? array_read(c, ref) : string_read(c, ref)).reg));
    put_label(c, done); return boxed(c, result);
}
static LmdValue read_reference(LmdCompiler* c, LmdReference ref, AstNode* site, bool callee = false,
        bool borrow_scalar = false, bool numeric = false) {
    if (!ref.key) return read_binding(c, ref.binding, site, borrow_scalar);
    if (LmdScalarField* field = scalar_reference(ref)) return read_local_value(c, field->value);
    if (ref.guarded && ref.field) return field_read(c, ref);
    unsigned k = semantic(ref.owner);
    if (k == K_TYPED_ARRAY) return typed_array_reference(c, ref, site, NULL, callee, numeric);
    if (k == K_ARRAY || k == K_STRING) {
        return sequence_reference(c, ref, k, site, callee, borrow_scalar, numeric);
    }
    MIR_reg_t result = em_new_reg(&c->em, "property", MIR_T_I64);
    MIR_label_t array = label(c), string = label(c), typed = label(c), object = label(c), done = label(c);
    if (k != K_OBJECT && k != K_MAP) {
        MIR_reg_t tid = tag(c, ref.owner);
        branch(c, MIR_BEQ, array, reg(c, tid), integer(c, LMD_TYPE_ARRAY));
        branch(c, MIR_BEQ, string, reg(c, tid), integer(c, LMD_TYPE_STRING));
        branch(c, MIR_BEQ, typed, reg(c, tid), integer(c, LMD_TYPE_ARRAY_NUM));
        branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_MAP));
        if (c->program->receiver_abi) branch(c, MIR_BEQ, object, reg(c, tid), integer(c, LMD_TYPE_FUNC));
        property_failure(c, ref.owner, site);
        put_label(c, array);
        move(c, result, reg(c, box(c, sequence_reference(c, ref, K_ARRAY, site, callee)).reg)); jump(c, done);
        put_label(c, string);
        move(c, result, reg(c, box(c, sequence_reference(c, ref, K_STRING, site, callee)).reg)); jump(c, done);
        put_label(c, typed);
        move(c, result, reg(c, box(c, typed_array_reference(c, ref, site, NULL, callee)).reg)); jump(c, done);
    }
    put_label(c, object);
    if (callee && ref.method) {
        MIR_label_t miss = label(c);
        // capture the builtin before arguments run; the empty attribute shape excludes own overrides.
        em_guard_map_shape(&c->em, ref.owner.reg, constant(c, (uint64_t)c->program->roots[1]), miss, true);
        move(c, result, integer(c, mvp_lmd_method_token(ref.method))); jump(c, done);
        put_label(c, miss);
    }
    LmdShapeSet shapes = ref.shapes.count ? ref.shapes : LmdShapeSet{ref.plan ? 1 : 0, {ref.plan}};
    for (int i = 0; i < shapes.count; i++) {
        LmdReference candidate = ref; candidate.plan = shapes.plans[i];
        if (ref.spelling) candidate.field = typemap_hash_lookup(candidate.plan->blueprint,
            ref.spelling->chars, ref.spelling->len);
        if (!candidate.field) continue;
        MIR_label_t miss = label(c);
        em_guard_map_shape(&c->em, ref.owner.reg, planned_shape(c, candidate.plan), miss,
            k == K_OBJECT || k == K_MAP);
        move(c, result, reg(c, box(c, field_read(c, candidate)).reg)); jump(c, done);
        put_label(c, miss);
    }
    MIR_reg_t value = property_access(c, callee ? LMD_PROP_CALLEE : LMD_PROP_GET, ref.owner.reg, ref.name);
    unsigned result_kind = !callee && site && (site->node_type == AST_NODE_MEMBER_EXPR || site->node_type == AST_NODE_INDEX_EXPR)
        ? kind(c->program, site, c->inlining) : K_ANY;
    check_error(c, value); move(c, result, reg(c, (result_kind & K_NUMBER) ? stable_item(c, value).reg : value));
    put_label(c, done); return boxed(c, result, result_kind);
}
static void array_index_write(LmdCompiler* c, LmdReference ref, LmdValue value) {
    LmdValue item = box(c, value);
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length");
    MIR_label_t done = label(c), slow = label(c), store = label(c);
    branch(c, MIR_UBGT, slow, reg(c, ref.key), reg(c, size));
    // only out-of-band Numbers require an owned scalar home; zero sentinels store verbatim.
    if (!value.integer && (semantic(value) & K_NUMBER)) {
        MIR_reg_t wide_tag = op(c, MIR_URSH, reg(c, item.reg), integer(c, 56));
        MIR_label_t immediate = label(c);
        branch(c, MIR_BNE, immediate, reg(c, wide_tag), integer(c, LMD_TYPE_FLOAT));
        branch(c, MIR_UBGT, slow, reg(c, item.reg), integer(c, ITEM_FLOAT_N0)); put_label(c, immediate);
    }
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
    MIR_reg_t d = to_number(c, value);
    MIR_label_t range_error = label(c), capability = label(c), done = label(c);
    MIR_reg_t i = integer_lane(c, d, 0, UINT32_MAX, range_error, true);
    MIR_reg_t size = em_load_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "old_length");
    branch(c, MIR_UBGT, capability, reg(c, i), reg(c, size));
    em_store_at(&c->em, ref.owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, i); jump(c, done);
    put_label(c, range_error); fail(c, LMD_MVP_RANGE, site);
    put_label(c, capability); fail(c, LMD_MVP_CAPABILITY, site); put_label(c, done);
}
static void write_reference(LmdCompiler* c, LmdReference ref, LmdValue value, AstNode* site) {
    if (!ref.key) { write_binding(c, ref.binding, value, site); return; }
    if (LmdScalarField* field = scalar_reference(ref)) { scalar_field_write(c, field, value); return; }
    MIR_label_t done = label(c), object = label(c);
    unsigned k = semantic(ref.owner);
    if (k == K_TYPED_ARRAY) { typed_array_reference(c, ref, site, &value); put_label(c, done); return; }
    if (k & K_TYPED_ARRAY) {
        MIR_label_t ordinary = label(c);
        branch(c, MIR_BNE, ordinary, reg(c, tag(c, ref.owner)), integer(c, LMD_TYPE_ARRAY_NUM));
        typed_array_reference(c, ref, site, &value); jump(c, done); put_label(c, ordinary);
    }
    if (k == K_OBJECT || k == K_MAP) jump(c, object);
    else if (k != K_ARRAY) branch(c, MIR_BEQ, object, reg(c, tag(c, ref.owner)), integer(c, LMD_TYPE_MAP));
    if (k != K_ARRAY) {
        MIR_label_t array = label(c);
        branch(c, MIR_BEQ, array, reg(c, tag(c, ref.owner)), integer(c, LMD_TYPE_ARRAY));
        property_failure(c, ref.owner, site); put_label(c, array);
    }
    array_key_check(c, ref, site);
    if (ref.key_known) {
        if (ref.index == -1) array_length_write(c, ref, value, site);
        else array_index_write(c, ref, value);
    } else {
        MIR_label_t length = label(c), done = label(c);
        branch(c, MIR_BEQ, length, reg(c, ref.key), MIR_new_int_op(c->em.ctx, -1));
        array_index_write(c, ref, value); jump(c, done);
        put_label(c, length); array_length_write(c, ref, value, site); put_label(c, done);
    }
    if (!ref.name) { put_label(c, done); return; }
    jump(c, done); put_label(c, object);
    if (ref.plan && ref.field) {
        MIR_label_t miss = label(c);
        if (field_write(c, ref, value, miss)) jump(c, done);
        put_label(c, miss);
    }
    MIR_reg_t stored = property_access(c, LMD_PROP_SET, ref.owner.reg, ref.name, box(c, value).reg);
    check_error(c, stored); put_label(c, done);
}

static LmdValue new_function(LmdCompiler* c, LmdFunction* f) {
    MIR_reg_t value = em_call_2(&c->em, "mvp_lmd_function_new", MIR_T_I64,
        MIR_T_I64, integer(c, f->id), MIR_T_P, reg(c, c->unit), true);
    check_error(c, value); return boxed(c, value, K_FUNCTION);
}
static LmdValue* call_arguments(LmdCompiler* c, AstCallNode* n, bool boxed_args) {
    int count = ast_linked_node_count(n->arguments), i = 0;
    LmdValue* values = (LmdValue*)mem_calloc((size_t)count, sizeof(LmdValue), MEM_CAT_TEMP);
    for (AstNode* a = n->arguments; a; a = a->next) {
        LmdValue value = expression(c, a);
        values[i++] = boxed_args ? box(c, value) : value;
    }
    return values;
}
static LmdValue array_fill_call(LmdCompiler* c, LmdValue owner, LmdValue* values, int count,
        unsigned lanes = 0, bool ordinary = false) {
    LmdValue absent = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    LmdValue value = count ? values[0] : absent;
    if (!ordinary && (semantic(value) != K_NUMBER || is_boxed(value))) value = {to_number(c, value), K_NUMBER};
    MIR_reg_t size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "fill_length");
    MIR_reg_t length = to_number(c, {size, K_NUMBER, false, 0, true});
    MIR_reg_t bounds[2];
    for (int i = 0; i < 2; i++) {
        bounds[i] = em_new_reg(&c->em, "fill_bound", MIR_T_I64);
        move(c, bounds[i], i ? reg(c, size) : integer(c, 0));
        if (count <= i + 1) continue;
        MIR_label_t done = label(c), nonnegative = label(c);
        if (i) branch(c, MIR_BEQ, done, reg(c, tag(c, values[i + 1])), integer(c, LMD_TYPE_UNDEFINED));
        MIR_reg_t d = em_call_1(&c->em, "trunc", MIR_T_D, MIR_T_D,
            reg(c, to_number(c, values[i + 1])), true);
        move(c, bounds[i], integer(c, 0));
        branch_truth(c, done, op(c, MIR_DEQ, reg(c, d), reg(c, d)), false);
        branch_truth(c, nonnegative, op(c, MIR_DLT, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
        move(c, d, reg(c, op(c, MIR_DADD, reg(c, d), reg(c, length), MIR_T_D)), true);
        put_label(c, nonnegative);
        branch_truth(c, done, op(c, MIR_DGT, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
        move(c, bounds[i], reg(c, size));
        branch_truth(c, done, op(c, MIR_DLT, reg(c, d), reg(c, length)), false);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, bounds[i]), reg(c, d)));
        put_label(c, done);
    }
    if (ordinary) {
        MIR_label_t loop = label(c), done = label(c);
        put_label(c, loop);
        branch(c, MIR_UBGE, done, reg(c, bounds[0]), reg(c, bounds[1]));
        array_index_write(c, {NULL, owner, bounds[0]}, value);
        move(c, bounds[0], reg(c, op(c, MIR_ADD, reg(c, bounds[0]), integer(c, 1)))); jump(c, loop);
        put_label(c, done); return owner;
    }
    // conversion and lane selection happen once, outside the fixed-storage fill loop.
    return typed_array_lanes(c, owner, bounds[0], &value, bounds[1], lanes);
}
static LmdValue array_construct(LmdCompiler* c, AstCallNode* n) {
    int count = ast_linked_node_count(n->arguments);
    LmdValue* values = call_arguments(c, n, true);
    MIR_reg_t result = em_new_reg(&c->em, "constructed_array", MIR_T_I64);
    MIR_label_t elements = label(c), done = label(c);
    if (count == 1) {
        branch(c, MIR_BNE, elements, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_FLOAT));
        MIR_label_t invalid = label(c), valid = label(c);
        MIR_reg_t length = integer_lane(c, to_number(c, values[0]), 0, UINT32_MAX, invalid, true);
        MIR_reg_t array = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, reg(c, length), true);
        check_error(c, array); move(c, result, reg(c, array)); jump(c, valid);
        put_label(c, invalid); fail(c, LMD_MVP_RANGE, (AstNode*)n);
        put_label(c, valid); jump(c, done);
    }
    put_label(c, elements);
    MIR_reg_t empty = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, integer(c, 0), true);
    check_error(c, empty); LmdValue owner = boxed(c, empty, K_ARRAY);
    for (int i = 0; i < count; i++) array_index_write(c, {NULL, owner, constant(c, i), true, i}, values[i]);
    move(c, result, reg(c, owner.reg)); put_label(c, done);
    mem_free(values); return boxed(c, result, K_ARRAY);
}
static LmdValue sequence_method_call(LmdCompiler* c, LmdValue owner, int method,
        LmdValue* values, int count) {
    LmdValue absent = boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
    if (method == LMD_METHOD_FILL) return array_fill_call(c, owner, values, count, 0, true);
    if (method == LMD_METHOD_CHAR_AT || method == LMD_METHOD_CODE_AT) {
        LmdValue argument = count ? values[0] : number(c, 0);
        MIR_reg_t index = to_number(c, argument);
        MIR_reg_t code = 0;
        MIR_label_t slow = label(c), done = label(c);
        if (method == LMD_METHOD_CODE_AT) {
            code = to_number(c, number(c, NAN));
            MIR_reg_t string = payload(c, owner.reg);
            MIR_reg_t flags = em_load_at(&c->em, string, offsetof(String, flags), MIR_T_U8, "string_flags");
            branch_truth(c, slow, op(c, MIR_AND, reg(c, flags), integer(c, 1)), false);
            MIR_reg_t at = argument.integer && !argument.number_present ? argument.reg :
                integer_lane(c, index, 0, UINT32_MAX, slow, false);
            MIR_reg_t bytes = em_load_at(&c->em, string, offsetof(String, len), MIR_T_U32, "string_bytes");
            branch(c, MIR_UBGE, done, reg(c, at), reg(c, bytes));
            MIR_reg_t address = op(c, MIR_ADD, reg(c, string), reg(c, at));
            MIR_reg_t unit = em_load_at(&c->em, address, offsetof(String, chars), MIR_T_U8, "code_unit");
            move(c, code, reg(c, to_number(c, {unit, K_NUMBER, false, 0, true})), true);
            jump(c, done); put_label(c, slow);
        }
        MIR_reg_t result = em_call_3(&c->em, "mvp_lmd_string_at", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_D, reg(c, index),
            MIR_T_I64, integer(c, method == LMD_METHOD_CODE_AT ? LMD_STRING_CODE : LMD_STRING_CHAR), true);
        check_error(c, result);
        if (code) {
            move(c, code, reg(c, to_number(c, boxed(c, result, K_NUMBER))), true);
            put_label(c, done); return {code, K_NUMBER};
        }
        return boxed(c, result, K_STRING);
    }
    if (method == LMD_METHOD_REPEAT) {
        MIR_reg_t d = em_call_1(&c->em, "trunc", MIR_T_D, MIR_T_D,
            reg(c, to_number(c, count ? values[0] : number(c, 0))), true);
        MIR_label_t invalid = label(c), allocate = label(c), done = label(c);
        MIR_reg_t times = constant(c, 0);
        branch_truth(c, allocate, op(c, MIR_DEQ, reg(c, d), reg(c, d)), false);
        branch_truth(c, invalid, op(c, MIR_DGE, reg(c, d), MIR_new_double_op(c->em.ctx, 0)), false);
        branch_truth(c, invalid, op(c, MIR_DLT, reg(c, d), MIR_new_double_op(c->em.ctx, INFINITY)), false);
        MIR_reg_t source = payload(c, owner.reg);
        MIR_reg_t bytes = em_load_at(&c->em, source, offsetof(String, len), MIR_T_U32, "repeat_bytes");
        branch_truth(c, allocate, bytes, false);
        // enforce the shared allocator's representable size before converting an unbounded count.
        MIR_reg_t maximum = op(c, MIR_DDIV, MIR_new_double_op(c->em.ctx, INT_MAX - sizeof(String) - 1),
            reg(c, to_number(c, {bytes, K_NUMBER, false, 0, true})), MIR_T_D);
        branch_truth(c, invalid, op(c, MIR_DLE, reg(c, d), reg(c, maximum)), false);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, times), reg(c, d)));
        put_label(c, allocate);
        MIR_reg_t result = em_call_2(&c->em, "str_repeat", MIR_T_P,
            MIR_T_P, reg(c, payload(c, owner.reg)), MIR_T_I64, reg(c, times), true);
        MIR_label_t allocated = label(c); branch_truth(c, allocated, result);
        fail(c, LMD_MVP_MEMORY, NULL); put_label(c, allocated); jump(c, done);
        put_label(c, invalid); fail(c, LMD_MVP_RANGE, NULL); put_label(c, done);
        return boxed(c, op(c, MIR_OR, reg(c, result), integer(c, (uint64_t)LMD_TYPE_STRING << 56)), K_STRING);
    }
    MIR_reg_t size = em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "sequence_length");
    if (method == LMD_METHOD_PUSH) {
        MIR_label_t valid = label(c);
        branch(c, MIR_UBLE, valid, reg(c, size), integer(c, UINT32_MAX - (uint64_t)count));
        fail(c, LMD_MVP_RANGE, NULL); put_label(c, valid);
        for (int i = 0; i < count; i++) array_index_write(c,
            {NULL, owner, op(c, MIR_ADD, reg(c, size), integer(c, i))}, values[i]);
        return {op(c, MIR_ADD, reg(c, size), integer(c, count)), K_NUMBER, false, 0, true};
    }
    if (method == LMD_METHOD_POP) {
        MIR_reg_t result = constant(c, ITEM_JS_UNDEFINED);
        MIR_label_t done = label(c); branch_truth(c, done, size, false);
        MIR_reg_t index = op(c, MIR_SUB, reg(c, size), integer(c, 1));
        // copy a tail-owned scalar before shrinking or clearing the source slot.
        move(c, result, reg(c, box(c, array_read(c, {NULL, owner, index})).reg));
        em_store_at(&c->em, em_array_element_address(&c->em, owner.reg, index, 8), 0, MIR_T_I64, absent.reg);
        em_store_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, index);
        put_label(c, done); return boxed(c, result);
    }
    // primitive conversion stays in the existing MIR coercion path; fn_join2 receives strings only.
    MIR_reg_t separator = text(c, ",").reg;
    if (count) {
        MIR_label_t default_separator = label(c);
        branch(c, MIR_BEQ, default_separator, reg(c, tag(c, values[0])), integer(c, LMD_TYPE_UNDEFINED));
        move(c, separator, reg(c, to_string(c, values[0]).reg)); put_label(c, default_separator);
    }
    MIR_reg_t joined_source = em_new_reg(&c->em, "join_source", MIR_T_I64);
    move(c, joined_source, reg(c, owner.reg));
    MIR_reg_t scan = constant(c, 0);
    MIR_label_t inspect = label(c), convert = label(c), join = label(c);
    // a successful string-only scan needs no conversion allocation or user-code invocation.
    put_label(c, inspect); branch(c, MIR_UBGE, join, reg(c, scan), reg(c, size));
    MIR_reg_t part = em_load_at(&c->em,
        em_array_element_address(&c->em, owner.reg, scan, 8), 0, MIR_T_I64, "join_part");
    branch(c, MIR_BNE, convert, reg(c, op(c, MIR_URSH, reg(c, part), integer(c, 56))), integer(c, LMD_TYPE_STRING));
    move(c, scan, reg(c, op(c, MIR_ADD, reg(c, scan), integer(c, 1)))); jump(c, inspect);
    put_label(c, convert);
    MIR_reg_t array = em_call_1(&c->em, "mvp_lmd_array_new", MIR_T_I64, MIR_T_I64, reg(c, size), true);
    check_error(c, array); LmdValue converted = boxed(c, array, K_ARRAY);
    MIR_reg_t index = constant(c, 0);
    MIR_label_t loop = label(c), done = label(c), append = label(c);
    put_label(c, loop); branch(c, MIR_UBGE, done, reg(c, index), reg(c, size));
    LmdValue value = array_read(c, {NULL, owner, index});
    MIR_reg_t string = text(c, "").reg;
    branch_truth(c, append, nullish(c, value));
    move(c, string, reg(c, to_string(c, value).reg)); put_label(c, append);
    array_index_write(c, {NULL, converted, index}, boxed(c, string, K_STRING));
    move(c, index, reg(c, op(c, MIR_ADD, reg(c, index), integer(c, 1)))); jump(c, loop);
    put_label(c, done);
    move(c, joined_source, reg(c, converted.reg)); put_label(c, join);
    MIR_reg_t result = em_call_2(&c->em, "fn_join2", MIR_T_I64,
        MIR_T_I64, reg(c, joined_source), MIR_T_I64, reg(c, separator), true);
    check_error(c, result); return boxed(c, result, K_STRING);
}
struct LmdInlineBudget { int nodes; bool allowed; MvpLmdProgram* program; LmdFunction* owner; bool statements; };
static void inline_budget(AstNode* node, void* opaque) {
    LmdInlineBudget* budget = (LmdInlineBudget*)opaque;
    if (!budget->allowed || ++budget->nodes > (budget->statements ? 64 : 24)) { budget->allowed = false; return; }
    switch (node->node_type) {
    case AST_NODE_IDENT: case AST_NODE_LITERAL: case AST_NODE_BINARY:
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR: case AST_NODE_CONDITIONAL_EXPR:
        break;
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: case AST_NODE_PROPERTY:
        if (budget->statements) budget->allowed = false;
        break;
    case AST_NODE_BLOCK: case AST_NODE_VAR_STAM: case AST_NODE_VARIABLE_DECLARATOR:
    case AST_NODE_EXPR_STMT: case AST_NODE_LOOP: case AST_NODE_IF_EXPR: case AST_NODE_NULL:
        if (!budget->statements) budget->allowed = false;
        break;
    case AST_NODE_ASSIGN: {
        AstNode* target = ((AstAssignNode*)node)->left;
        LmdBinding* b = target->node_type == AST_NODE_IDENT
            ? identifier_binding(budget->program, (AstIdentNode*)target) : NULL;
        if (!budget->statements || !b || b->owner != budget->owner) budget->allowed = false;
        break;
    }
    case AST_NODE_UNARY: {
        Operator op = ((AstUnaryNode*)node)->op;
        if (op == OPERATOR_JS_DELETE) budget->allowed = false;
        if (op == OPERATOR_JS_INCREMENT || op == OPERATOR_JS_DECREMENT) {
            AstNode* target = ((AstUnaryNode*)node)->operand;
            LmdBinding* b = target->node_type == AST_NODE_IDENT
                ? identifier_binding(budget->program, (AstIdentNode*)target) : NULL;
            if (!budget->statements || !b || b->owner != budget->owner) budget->allowed = false;
        }
        break;
    }
    default: budget->allowed = false; break;
    }
    if (budget->allowed) js_ast_visit_children(node, inline_budget, budget);
}
static AstNode* inline_body(MvpLmdProgram* p, LmdFunction* f, AstNode** prefix) {
    *prefix = NULL;
    if (!f || !f->closed_calls || ast_linked_node_count(f->ast->params) > LAMBDA_MAX_FUNCTION_ARGS) return NULL;
    // duplicate formals need the ordinary parameter installation order.
    if (js_ast_collect_parameter_facts(f->ast->params).has_duplicate_param_names) return NULL;
    AstNode* body = f->ast->body;
    LmdInlineBudget budget = {0, true, p, f, false};
    if (body->node_type == AST_NODE_BLOCK) {
        AstNode* first = ((AstBlockNode*)body)->statements;
        body = first;
        if (!body) return NULL;
        while (body->next) body = body->next;
        if (body->node_type != AST_NODE_RETURN_STAM) return NULL;
        if (first != body) {
            int locals = 0;
            for (int i = 0; i < p->bindings->length; i++) {
                LmdBinding* b = (LmdBinding*)p->bindings->data[i];
                if (b->owner != f) continue;
                if (b->external || ++locals > 32) return NULL;
            }
            budget.statements = true;
            for (AstNode* node = first; node != body; node = node->next) inline_budget(node, &budget);
            *prefix = first;
        }
        body = ((AstReturnNode*)body)->value;
    }
    if (!body) return NULL;
    inline_budget(body, &budget);
    return budget.allowed ? body : NULL;
}
struct LmdCallCapture { LmdValue callee; LmdValue* values; LmdFunction* direct = NULL;
    bool borrows_arguments = false; LmdValue receiver; LmdValue new_target; };
static LmdValue call(LmdCompiler* c, AstCallNode* n, LmdCallCapture* captured = NULL) {
    if (!captured && c->function->home && n->callee->node_type == AST_NODE_IDENT &&
            named(((AstIdentNode*)n->callee)->name, "super")) {
        MIR_reg_t base = em_call_2(&c->em, "mvp_lmd_class_super", MIR_T_I64,
            MIR_T_I64, reg(c, c->self), MIR_T_I64, integer(c, 1), true);
        check_error(c, base);
        LmdCallCapture capture = {boxed(c, base, K_FUNCTION), call_arguments(c, n, true)};
        capture.new_target = boxed(c, c->new_target);
        LmdValue result = call(c, n, &capture);
        MIR_label_t uninitialized = label(c);
        branch(c, MIR_BEQ, uninitialized, reg(c, c->receiver), integer(c, ITEM_JS_TDZ));
        fail(c, LMD_MVP_REFERENCE, (AstNode*)n); put_label(c, uninitialized);
        move(c, c->receiver, reg(c, result.reg)); return result;
    }
    LmdBinding* intrinsic = !captured && n->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(c->program, (AstIdentNode*)n->callee) : NULL;
    if (intrinsic && intrinsic->intrinsic == I_ARRAY) return array_construct(c, n);
    int math = captured ? -1 : math_operation(c->program, n);
    if (math >= 0) {
        LmdValue* values = call_arguments(c, n, false);
        int count = ast_linked_node_count(n->arguments);
        const auto& operation = math_operations[math];
        MIR_reg_t result;
        if (operation.arity == 1) result = em_call_1(&c->em, operation.native, MIR_T_D, MIR_T_D,
            reg(c, to_number(c, count ? values[0] : number(c, NAN))), true);
        else {
            result = to_number(c, count ? values[0] : number(c, !strcmp(operation.name, "min") ? INFINITY : -INFINITY));
            for (int i = 1; i < count; i++) result = em_call_2(&c->em, operation.native, MIR_T_D,
                MIR_T_D, reg(c, result), MIR_T_D, reg(c, to_number(c, values[i])), true);
        }
        mem_free(values); return {result, K_NUMBER};
    }
    bool member = !captured && ( n->callee->node_type == AST_NODE_MEMBER_EXPR || n->callee->node_type == AST_NODE_INDEX_EXPR);
    if (member) {
        AstFieldNode* field = (AstFieldNode*)n->callee;
        LmdBinding* builtin = field->object->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)field->object) : NULL;
        if (builtin && builtin->intrinsic == I_STRING && named(member_spelling(field), "fromCharCode")) {
            LmdValue* values = call_arguments(c, n, false);
            int count = ast_linked_node_count(n->arguments);
            LmdValue result = text(c, "");
            for (int i = 0; i < count; i++) {
                MIR_reg_t unit = op(c, MIR_AND, reg(c, int32(c, values[i])), integer(c, UINT16_MAX));
                MIR_reg_t character = em_call_3(&c->em, "mvp_lmd_string_at", MIR_T_I64,
                    MIR_T_I64, integer(c, ITEM_JS_UNDEFINED), MIR_T_D,
                    reg(c, to_number(c, {unit, K_NUMBER, false, 0, true})),
                    MIR_T_I64, integer(c, LMD_STRING_FROM_CODE), true);
                check_error(c, character); LmdValue part = boxed(c, character, K_STRING);
                if (!i) result = part;
                else {
                    MIR_reg_t joined = em_call_2(&c->em, "mvp_lmd_string_concat", MIR_T_I64,
                        MIR_T_I64, reg(c, result.reg), MIR_T_I64, reg(c, part.reg), true);
                    check_error(c, joined); result = boxed(c, joined, K_STRING);
                }
            }
            mem_free(values); return result;
        }
        if (builtin && builtin->intrinsic == 5 && !field->computed) {
            String* key = ((AstIdentNode*)field->property)->name;
            int op = named(key, "keys") ? 0 : named(key, "values") ? 1 : named(key, "entries") ? 2 :
                named(key, "hasOwn") ? 3 : -1;
            LmdValue* values = call_arguments(c, n, true);
            int count = ast_linked_node_count(n->arguments);
            LmdValue owner = count ? values[0] : boxed(c, constant(c, ITEM_JS_UNDEFINED));
            MIR_reg_t result;
            if (op == 3) {
                LmdValue key = count > 1 ? values[1] : boxed(c, constant(c, ITEM_JS_UNDEFINED));
                LmdValue name = canonical_key(c, key);
                result = property_access(c, LMD_PROP_OWN, owner.reg, name.reg);
            } else if (op >= 0) result = property_access(c, LMD_PROP_KEYS + op, owner.reg);
            else { fail(c, LMD_MVP_CAPABILITY, (AstNode*)n); result = constant(c, ITEM_JS_UNDEFINED); }
            mem_free(values); check_error(c, result); return boxed(c, result, op == 3 ? K_BOOL : K_ARRAY);
        }
    }
    LmdBinding* b = n->callee->node_type == AST_NODE_IDENT
        ? identifier_binding(c->program, (AstIdentNode*)n->callee) : NULL;
    LmdFunction* direct = captured ? captured->direct : direct_target(c->program, n);
    AstNode* prefix = NULL;
    AstNode* body = captured ? NULL : inline_body(c->program, direct, &prefix);
    // reserve statement-body expansion for loop call sites; a one-off call cannot amortize the extra MIR.
    if (prefix && !enclosing_loop(c->program, (AstNode*)n, c->function, true)) body = NULL;
    if (body) {
        // snapshot all arguments, including extras, before substituting immutable parameter reads.
        LmdValue* values = call_arguments(c, n, false);
        int count = ast_linked_node_count(n->arguments);
        LmdInlineFrame frame = {}; frame.parent = c->inlining; frame.function = direct;
        MIR_label_t fallback = label(c), done = label(c);
        bool guarded = false;
        AstNode* actual = n->arguments;
        for (AstNode* formal = direct->ast->params; formal; formal = formal->next) {
            int i = frame.count++;
            frame.bindings[i] = binding(c->program, js_ast_parameter_binding_identifier(formal)->entry);
            frame.slots[i].written = prefix != NULL;
            frame.slots[i].kinds = frame.bindings[i]->kinds;
            frame.slots[i].range = frame.bindings[i]->range;
            frame.slots[i].integer = frame.slots[i].kinds == K_NUMBER && frame.slots[i].range.state == 1;
            scalar_field_write(c, &frame.slots[i], i < count ? values[i] :
                boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED));
            frame.plans[i] = actual ? value_plan(c, actual) : NULL;
            if (prefix && direct->direct_args && frame.plans[i] && !frame.bindings[i]->writes &&
                    !frame.plans[i]->scalar_binding) {
                em_guard_map_shape(&c->em, box(c, values[i]).reg, planned_shape(c, frame.plans[i]), fallback,
                    semantic(values[i]) == K_OBJECT || semantic(values[i]) == K_MAP);
                guarded = frame.guarded[i] = true;
            }
            if (actual) actual = actual->next;
        }
        int parameters = frame.count;
        if (prefix) {
            for (int i = 0; i < c->program->bindings->length; i++) {
                LmdBinding* local = (LmdBinding*)c->program->bindings->data[i];
                if (local->owner != direct || local->entry->is_parameter) continue;
                int slot = frame.count++;
                frame.bindings[slot] = local; frame.slots[slot].written = true;
                frame.slots[slot].kinds = local->dominated ? local->seed_kinds : K_ANY;
            }
            // reuse the type solver inside the guard; its facts stay local to this inlined region.
            LmdTypes types = {c->program, true, NULL, &frame};
            while (types.changed) { types.changed = false; type_walk(direct->ast->body, &types); }
            for (int i = parameters; i < frame.count; i++) if (!frame.slots[i].kinds) {
                frame.slots[i].kinds = K_ANY; types.changed = true;
            }
            while (types.changed) { types.changed = false; type_walk(direct->ast->body, &types); }
            for (int i = parameters; i < frame.count; i++) {
                LmdBinding* local = frame.bindings[i];
                bool native = frame.slots[i].kinds == K_NUMBER || frame.slots[i].kinds == K_BOOL;
                // inlining retains the range proof even when the original callee used a float region.
                frame.slots[i].range = local->range;
                frame.slots[i].integer = frame.slots[i].kinds == K_NUMBER && local->range.state == 1;
                scalar_field_write(c, &frame.slots[i], native ? number(c, 0) :
                    boxed(c, constant(c, local->entry->is_lexical ? ITEM_JS_TDZ : ITEM_JS_UNDEFINED)));
            }
        }
        bool caller_strict = c->strict;
        c->strict = direct->ast->has_use_strict_directive || (direct->ast->vars && direct->ast->vars->strict);
        c->inlining = &frame;
        if (prefix) for (AstNode* node = prefix; node->next; node = node->next) statement(c, node);
        LmdValue result = expression(c, body);
        c->inlining = frame.parent; c->strict = caller_strict;
        if (guarded) {
            MIR_reg_t joined = em_new_reg(&c->em, "inline_result", MIR_T_I64);
            move(c, joined, reg(c, box(c, result).reg)); jump(c, done);
            put_label(c, fallback);
            // both branches use the original argument snapshots; the outer call owns their array.
            LmdCallCapture capture = {boxed(c, constant(c, ITEM_JS_UNDEFINED)), values, direct, true};
            move(c, joined, reg(c, box(c, call(c, n, &capture)).reg));
            put_label(c, done); result = boxed(c, joined);
        } else { put_label(c, fallback); put_label(c, done); }
        mem_free(values); return result;
    }
    bool native = direct && direct->native;
    bool direct_args = direct && direct->direct_args;
    LmdReference method = member ? reference(c, n->callee, true) : LmdReference{};
    LmdValue callee = captured ? captured->callee : member ? box(c, read_reference(c, method, (AstNode*)n, true)) : direct && !b->observed ? boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED)
        : box(c, expression(c, n->callee));
    int count = ast_linked_node_count(n->arguments);
    bool typed_fill = member && semantic(method.owner) == K_TYPED_ARRAY && method.key_known && method.index == -4;
    unsigned receiver = semantic(method.owner);
    int sequence_method = member && method.spelling && (receiver == K_ARRAY || receiver == K_STRING)
        ? mvp_lmd_builtin_method(method.spelling, receiver == K_ARRAY ? LMD_TYPE_ARRAY : LMD_TYPE_STRING) : 0;
    LmdValue* values = captured ? captured->values : call_arguments(c, n, !direct_args && !typed_fill && !sequence_method);
    if (typed_fill) {
        LmdValue result = array_fill_call(c, method.owner, values, count, method.lanes);
        mem_free(values); return result;
    }
    if (sequence_method) {
        LmdValue result = sequence_method_call(c, method.owner, sequence_method, values, count);
        mem_free(values); return result;
    }
    MIR_label_t builtin_call = member ? label(c) : NULL, call_done = member ? label(c) : NULL;
    MIR_reg_t member_result = member ? em_new_reg(&c->em, "method_result", MIR_T_I64) : 0;
    if (member) {
        MIR_label_t ordinary = label(c);
        branch(c, MIR_BEQ, ordinary, reg(c, callee.reg), integer(c, ITEM_JS_UNDEFINED));
        branch(c, MIR_BEQ, builtin_call,
            reg(c, op(c, MIR_URSH, reg(c, callee.reg), integer(c, 56))), integer(c, LMD_TYPE_UNDEFINED));
        put_label(c, ordinary);
    }
    int i = 0;
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
    if (count && !direct_args) {
        if (count > c->argument_count) c->argument_count = count;
        span = em_deferred_root_span_base(&c->em, &c->argument_span, &c->argument_fixup);
        for (i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, values[i].reg);
    }
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS] = {MIR_T_P, MIR_T_P, MIR_T_P, MIR_T_I64, MIR_T_I64};
    MIR_op_t args[LAMBDA_MAX_FUNCTION_ARGS] = {reg(c, c->em.frame.runtime), reg(c, c->unit), span ? reg(c, span) : integer(c, 0),
        integer(c, count), reg(c, callee.reg)};
    MirImportEntry* entry = direct_args ? &direct->entry : &c->program->entry;
    int nargs = direct_args ? direct->arity : c->program->receiver_abi ? 7 : 5;
    if (!direct_args && c->program->receiver_abi) {
        types[5] = types[6] = MIR_T_I64;
        args[5] = args[6] = integer(c, ITEM_JS_UNDEFINED);
    }
    AstNode* formal = direct_args ? direct->ast->params : NULL;
    if (direct_args) for (i = 2; i < nargs; i++, formal = formal->next) {
        types[i] = direct->signature[i].type;
        LmdBinding* parameter = binding(c->program,
            js_ast_parameter_binding_identifier(formal)->entry);
        LmdValue actual = i - 2 < count ? values[i - 2] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        args[i] = reg(c, parameter->native ? scalar_reg(c, actual, parameter->kinds, parameter->integer)
            : box(c, actual).reg);
    }
    MIR_type_t result_type = native && direct->returns == K_NUMBER && !direct->integer ? MIR_T_D : MIR_T_I64;
    MIR_reg_t result = em_new_reg(&c->em, "call_result", result_type);
    bool pair = native && em_returns_result_pair(direct->return_abi.companion);
    MIR_reg_t companion = pair ? em_new_reg(&c->em, "call_error", MIR_T_I64) : 0;
    if (c->program->receiver_abi && !direct) {
        bool super_member = member && ((AstFieldNode*)n->callee)->object->node_type == AST_NODE_IDENT &&
            named(((AstIdentNode*)((AstFieldNode*)n->callee)->object)->name, "super");
        MIR_op_t receiver = captured && captured->receiver.reg ? reg(c, captured->receiver.reg) :
            member ? reg(c, super_member ? c->receiver : method.owner.reg) : integer(c, ITEM_JS_UNDEFINED);
        result = em_call_5(&c->em, "mvp_lmd_class_invoke", MIR_T_I64,
            MIR_T_I64, reg(c, callee.reg), MIR_T_I64,
            receiver, MIR_T_P, span ? reg(c, span) : integer(c, 0), MIR_T_I64, integer(c, count),
            MIR_T_I64, captured && captured->new_target.reg ? reg(c, captured->new_target.reg)
                : integer(c, ITEM_JS_UNDEFINED), true);
    } else {
        em_before_resolved_call(&c->em, "mvp_lmd_entry", &entry->call, nargs, types, args);
        MIR_insn_t insn = mir_new_call_with_target(c->em.ctx, entry->proto,
            direct ? MIR_new_ref_op(c->em.ctx, direct->forward) : reg(c, target), result, nargs, args, companion);
        mir_append_emit_insn(c->em.ctx, c->em.func_item, insn);
        em_after_resolved_call(&c->em, "mvp_lmd_entry", &entry->call, insn, 0, result_type);
    }
    // consume the context companion before any safepoint or rooted store (D5.2.1v3).
    bool pending = !direct_args || fn_return_shape_may_be_pending(direct->return_abi.shape);
    if (!pair && (native || pending)) companion = em_load_at(&c->em, c->em.frame.runtime,
        offsetof(Context, mir_companion_slot), MIR_T_I64, "companion");
    if (native) {
        MIR_label_t ok = label(c);
        branch(c, MIR_BEQ, ok, reg(c, companion), integer(c, pair ? ITEM_NULL : 0));
        return_error(c, companion); put_label(c, ok);
        if (!captured || !captured->borrows_arguments) mem_free(values);
        return {result, direct->returns, false, 0, direct->integer, direct->range};
    }
    MIR_reg_t resolved = result;
    if (pending) {
        int home = em_scalar_home_new(&c->em);
        MIR_reg_t address = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home));
        resolved = em_resolve_pending_pair(&c->em, result, companion, address);
        em_scalar_home_bind(&c->em, home, resolved);
    }
    if (count && !direct_args) {
        MIR_reg_t absent = constant(c, ITEM_JS_UNDEFINED);
        for (i = 0; i < count; i++) em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, absent);
    }
    check_error(c, resolved);
    if (member) {
        move(c, member_result, reg(c, resolved)); jump(c, call_done); put_label(c, builtin_call);
        if ((semantic(method.owner) & K_TYPED_ARRAY) && (!method.spelling || named(method.spelling, "fill"))) {
            MIR_label_t map_method = label(c);
            branch(c, MIR_BNE, map_method, reg(c, callee.reg), integer(c, mvp_lmd_method_token(9)));
            branch(c, MIR_BNE, map_method, reg(c, tag(c, method.owner)), integer(c, LMD_TYPE_ARRAY_NUM));
            move(c, member_result, reg(c, array_fill_call(c, method.owner, values, count).reg)); jump(c, call_done);
            put_label(c, map_method);
        }
        for (int operation = LMD_METHOD_FILL; operation <= LMD_METHOD_REPEAT; operation++) {
            unsigned owner_kind = operation <= LMD_METHOD_JOIN ? K_ARRAY : K_STRING;
            if (!(semantic(method.owner) & owner_kind)) continue;
            if (method.spelling && mvp_lmd_builtin_method(method.spelling,
                    owner_kind == K_ARRAY ? LMD_TYPE_ARRAY : LMD_TYPE_STRING) != operation) continue;
            MIR_label_t next = label(c);
            branch(c, MIR_BNE, next, reg(c, callee.reg), integer(c, mvp_lmd_method_token(operation)));
            LmdValue owner = boxed(c, method.owner.reg, owner_kind);
            move(c, member_result, reg(c, box(c, sequence_method_call(c, owner, operation, values, count)).reg));
            jump(c, call_done); put_label(c, next);
        }
        MIR_reg_t absent = constant(c, ITEM_JS_UNDEFINED);
        MIR_reg_t result = em_call_4(&c->em, "mvp_lmd_map_call", MIR_T_I64,
            MIR_T_I64, reg(c, method.owner.reg), MIR_T_I64, reg(c, callee.reg),
            MIR_T_I64, reg(c, count ? values[0].reg : absent),
            MIR_T_I64, reg(c, count > 1 ? values[1].reg : absent), true);
        check_error(c, result); move(c, member_result, reg(c, stable_item(c, result).reg));
        put_label(c, call_done); resolved = member_result;
    }
    if (!captured || !captured->borrows_arguments) mem_free(values);
    return boxed(c, resolved, direct ? direct->returns : K_ANY);
}
static bool tail_call(LmdCompiler* c, AstNode* n) {
    if (!c->tail_entry || !n || n->node_type != AST_NODE_CALL_EXPR ||
            direct_target(c->program, (AstCallNode*)n) != c->function) return false;
    LmdValue* values = call_arguments(c, (AstCallNode*)n, false);
    int count = ast_linked_node_count(((AstCallNode*)n)->arguments), i = 0;
    // evaluate every argument before overwriting any parameter, including discarded extras.
    for (AstNode* a = c->function->ast->params; a; a = a->next, i++) {
        LmdBinding* b = binding(c->program, js_ast_parameter_binding_identifier(a)->entry);
        LmdValue actual = i < count ? values[i] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        write_binding(c, b, actual, n, true);
    }
    mem_free(values); release_iterators(c); jump(c, c->tail_entry); return true;
}
static Operator compound_operator(Operator op) {
    static const Operator operations[] = {OPERATOR_ADD, OPERATOR_SUB, OPERATOR_MUL,
        OPERATOR_DIV, OPERATOR_MOD, OPERATOR_JS_EXP, OPERATOR_JS_BIT_AND,
        OPERATOR_JS_BIT_OR, OPERATOR_JS_BIT_XOR, OPERATOR_JS_LSHIFT,
        OPERATOR_JS_RSHIFT, OPERATOR_JS_URSHIFT};
    // logical assignments have no eager arithmetic operation in the range planner.
    return op >= OPERATOR_JS_ADD_ASSIGN && op <= OPERATOR_JS_URSHIFT_ASSIGN
        ? operations[op - OPERATOR_JS_ADD_ASSIGN] : op;
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
// these operands cannot overwrite a scalar source or allocate on a successful evaluation.
static bool immediate_scalar_operand(LmdCompiler* c, AstNode* n) {
    if (n->node_type == AST_NODE_IDENT || n->node_type == AST_NODE_LITERAL) return true;
    if (n->node_type != AST_NODE_MEMBER_EXPR && n->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* field = (AstFieldNode*)n;
    if (field->object->node_type != AST_NODE_IDENT) return false;
    LmdBinding* owner = identifier_binding(c->program, (AstIdentNode*)field->object);
    if (!owner || (!owner->entry->is_parameter && !owner->dominated) ||
            kind(c->program, field->object, c->inlining) != K_ARRAY) return false;
    if (!field->computed || field->property->node_type == AST_NODE_LITERAL) return true;
    if (field->property->node_type != AST_NODE_IDENT) return false;
    unsigned key = kind(c->program, field->property, c->inlining);
    return key == K_NUMBER || key == K_STRING;
}
static bool remainder_zero(LmdCompiler* c, AstBinaryNode* test, LmdValue* result) {
    bool unequal = test->op == OPERATOR_NE || test->op == OPERATOR_JS_STRICT_NE;
    if (!unequal && test->op != OPERATOR_EQ && test->op != OPERATOR_JS_STRICT_EQ) return false;
    if (test->right->node_type != AST_NODE_LITERAL ||
            ((AstLiteralNode*)test->right)->literal_type != AST_LITERAL_NUMBER ||
            ((AstLiteralNode*)test->right)->value.number_value != 0 ||
            test->left->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* remainder = (AstBinaryNode*)test->left;
    if (remainder->op != OPERATOR_MOD || remainder->right->node_type != AST_NODE_LITERAL ||
            ((AstLiteralNode*)remainder->right)->literal_type != AST_LITERAL_NUMBER) return false;
    double divisor = ((AstLiteralNode*)remainder->right)->value.number_value;
    if (divisor < 1 || divisor > 9007199254740992.0 || divisor != trunc(divisor)) return false;
    uint64_t integer_divisor = (uint64_t)divisor;
    if (integer_divisor & (integer_divisor - 1)) return false;
    LmdValue value = expression(c, remainder->left, false, true);
    MIR_reg_t zero;
    if (value.integer && !value.number_present) {
        zero = op(c, MIR_EQ, reg(c, op(c, MIR_AND, reg(c, value.reg), integer(c, integer_divisor - 1))), integer(c, 0));
    } else {
        unsigned power = 0;
        for (uint64_t d = integer_divisor; d > 1; d >>= 1) power++;
        MIR_reg_t bits = em_emit_double_bits(&c->em, to_number(c, value));
        MIR_reg_t magnitude = op(c, MIR_AND, reg(c, bits), integer(c, INT64_MAX));
        MIR_reg_t exponent = op(c, MIR_URSH, reg(c, magnitude), integer(c, 52));
        MIR_label_t exceptional = label(c), done = label(c);
        zero = constant(c, 0);
        MIR_reg_t shift = op(c, MIR_SUB, reg(c, exponent), integer(c, 1023 + power));
        // ordinary exponents need one guard; keep zero, tiny and huge Numbers off this path.
        branch(c, MIR_UBGE, exceptional, reg(c, shift), integer(c, 52));
        MIR_reg_t fraction = op(c, MIR_AND,
            reg(c, op(c, MIR_LSH, reg(c, magnitude), reg(c, shift))), integer(c, (UINT64_C(1) << 52) - 1));
        move(c, zero, reg(c, op(c, MIR_EQ, reg(c, fraction), integer(c, 0))));
        jump(c, done); put_label(c, exceptional);
        MIR_reg_t large = op(c, MIR_ULT,
            reg(c, op(c, MIR_SUB, reg(c, shift), integer(c, 52))), integer(c, 2047 - 1023 - power - 52));
        move(c, zero, reg(c, op(c, MIR_OR, reg(c, large),
            reg(c, op(c, MIR_EQ, reg(c, magnitude), integer(c, 0)))))); put_label(c, done);
    }
    *result = {unequal ? op(c, MIR_XOR, reg(c, zero), integer(c, 1)) : zero, K_BOOL};
    return true;
}
static bool string_concat(LmdCompiler* c, AstBinaryNode* node, LmdValue* result) {
    AstNode* leaves[6];
    int count = 0;
    AstNode* left = (AstNode*)node;
    const unsigned primitives = K_STRING | K_NUMBER | K_BOOL | K_NULL | K_UNDEFINED;
    while (left->node_type == AST_NODE_BINARY && count < 5) {
        AstBinaryNode* part = (AstBinaryNode*)left;
        unsigned right_kind = kind(c->program, part->right, c->inlining);
        if (part->op != OPERATOR_ADD || kind(c->program, part->left, c->inlining) != K_STRING ||
                !right_kind || (right_kind & ~primitives)) break;
        leaves[count++] = part->right; left = part->left;
    }
    if (count < 2) return false;
    leaves[count++] = left;
    MIR_type_t types[6]; MIR_op_t args[6];
    for (int i = 0; i < count; i++) {
        // only primitive conversions can be combined; retain source evaluation and conversion order.
        LmdValue part = to_string(c, expression(c, leaves[count - i - 1]));
        types[i] = MIR_T_P; args[i] = reg(c, payload(c, part.reg));
    }
    const char* helpers[] = {"fn_strcat3", "fn_strcat4", "fn_strcat5", "fn_strcat6"};
    MIR_reg_t joined = em_call_with_args(&c->em, helpers[count - 3], MIR_T_P, count, types, args, true);
    MIR_label_t valid = label(c), invalid = label(c);
    branch_truth(c, invalid, joined, false);
    branch(c, MIR_BNE, valid, reg(c, joined), integer(c, (uint64_t)&STR_ERROR));
    put_label(c, invalid); fail(c, LMD_MVP_MEMORY, (AstNode*)node); put_label(c, valid);
    joined = em_call_1(&c->em, "fn_string_freeze", MIR_T_P, MIR_T_P, reg(c, joined), true);
    *result = boxed(c, op(c, MIR_OR, reg(c, joined), integer(c, (uint64_t)LMD_TYPE_STRING << 56)), K_STRING);
    return true;
}
static LmdValue expression(LmdCompiler* c, AstNode* n, bool borrow_scalar, bool numeric) {
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
    case JS_AST_NODE_TEMPLATE_LITERAL: {
        JsTemplateLiteralNode* literal = (JsTemplateLiteralNode*)n;
        AstNode* next = literal->expressions;
        LmdValue result = text(c, "");
        for (AstNode* quasi = literal->quasis; quasi; quasi = quasi->next) {
            String* cooked = ((JsTemplateElementNode*)quasi)->cooked;
            LmdValue part = boxed(c, constant(c, s2it(cooked)), K_STRING);
            if (quasi == literal->quasis) result = part;
            else result = binary_value(c, OPERATOR_ADD, result, part);
            if (next) {
                part = to_string(c, expression(c, next));
                result = binary_value(c, OPERATOR_ADD, result, part); next = next->next;
            }
        }
        return result;
    }
    case AST_NODE_IDENT: {
        AstIdentNode* id = (AstIdentNode*)n;
        if (c->function->home && (named(id->name, "this") || named(id->name, "new.target"))) {
            MIR_reg_t value = named(id->name, "this") ? c->receiver : c->new_target;
            MIR_label_t ready = label(c);
            branch(c, MIR_BNE, ready, reg(c, value), integer(c, ITEM_JS_TDZ));
            fail(c, LMD_MVP_REFERENCE, n); put_label(c, ready);
            // preserve the value of an earlier this read across a later super() call.
            return boxed(c, op(c, MIR_OR, reg(c, value), integer(c, 0)));
        }
        if (c->function->home && named(id->name, "super")) {
            MIR_label_t initialized = label(c);
            branch(c, MIR_BNE, initialized, reg(c, c->receiver), integer(c, ITEM_JS_TDZ));
            fail(c, LMD_MVP_REFERENCE, n); put_label(c, initialized);
            MIR_reg_t base = em_call_2(&c->em, "mvp_lmd_class_super", MIR_T_I64,
                MIR_T_I64, reg(c, c->self), MIR_T_I64, integer(c, 0), true);
            check_error(c, base); return boxed(c, base, K_OBJECT);
        }
        LmdBinding* b = identifier_binding(c->program, id);
        return read_binding(c, b, n, borrow_scalar);
    }
    case AST_NODE_FUNC_EXPR: case AST_NODE_ARROW_FUNC:
        return new_function(c, function(c->program, n));
    case AST_NODE_CALL_EXPR: return call(c, (AstCallNode*)n);
    case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR:
        return read_reference(c, reference(c, n), n, false, borrow_scalar, numeric);
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)n;
        if (a->left->node_type == AST_NODE_ARRAY || a->left->node_type == AST_NODE_ARRAY_PATTERN) {
            AstNode* parent = ast_index_parent(&c->program->frontend->ast_index, n);
            if (a->right->node_type == AST_NODE_ARRAY && parent && parent->node_type == AST_NODE_EXPR_STMT &&
                    (c->function->id || c->inlining)) {
                int count = ast_linked_node_count(((AstArrayNode*)a->right)->item), i = 0;
                LmdValue* values = (LmdValue*)mem_calloc(count, sizeof(LmdValue), MEM_CAT_TEMP);
                // snapshot every RHS before ordered writes; extra elements still run for their effects.
                for (AstNode* e = ((AstArrayNode*)a->right)->item; e; e = e->next)
                    values[i++] = expression(c, e);
                i = 0;
                for (AstNode* e = ((AstArrayNode*)a->left)->item; e; e = e->next, i++)
                    write_binding(c, identifier_binding(c->program, (AstIdentNode*)e),
                        i < count ? values[i] : boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED), e);
                mem_free(values);
                return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
            }
            LmdValue owner = box(c, expression(c, a->right));
            MIR_label_t array = label(c);
            branch(c, MIR_BEQ, array, reg(c, tag(c, owner)), integer(c, LMD_TYPE_ARRAY));
            fail(c, LMD_MVP_CAPABILITY, n); put_label(c, array);
            int i = 0;
            // evaluating the RHS array already snapshots swap operands; bindings are written in order.
            for (AstNode* e = ((AstArrayNode*)a->left)->item; e; e = e->next, i++)
                write_binding(c, identifier_binding(c->program, (AstIdentNode*)e),
                    array_read(c, {NULL, owner, constant(c, i), true, i}), e);
            return owner;
        }
        if (a->op == OPERATOR_JS_AND_ASSIGN || a->op == OPERATOR_JS_OR_ASSIGN || a->op == OPERATOR_JS_NULLISH_ASSIGN)
            return logical(c, a->op, a->left, a->right, true);
        LmdReference ref = reference(c, a->left);
        LmdValue old = {};
        bool consume = a->op != OPERATOR_ASSIGN && numeric_operands(compound_operator(a->op),
            kind(c->program, a->left, c->inlining), kind(c->program, a->right, c->inlining));
        if (a->op != OPERATOR_ASSIGN) old = read_reference(c, ref, a->left, false, false, consume);
        LmdValue rhs = expression(c, a->right, false, consume);
        if (a->op != OPERATOR_ASSIGN) rhs = binary_value(c, compound_operator(a->op), old, rhs);
        write_reference(c, ref, rhs, n); return rhs;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)n;
        if (u->op == OPERATOR_JS_DELETE) {
            LmdReference ref = reference(c, u->operand, true);
            MIR_reg_t deleted = property_access(c, LMD_PROP_DELETE, ref.owner.reg, ref.name);
            check_error(c, deleted); return boxed(c, deleted, K_BOOL);
        }
        if (u->op == OPERATOR_JS_INCREMENT || u->op == OPERATOR_JS_DECREMENT) {
            LmdReference ref = reference(c, u->operand);
            LmdValue before = read_reference(c, ref, n, false, false, true);
            if (semantic(before) != K_NUMBER) before = {to_number(c, before), K_NUMBER};
            LmdValue after = binary_value(c, u->op == OPERATOR_JS_INCREMENT ? OPERATOR_ADD : OPERATOR_SUB,
                before, number(c, 1));
            write_reference(c, ref, after, n); return u->prefix ? after : before;
        }
        if (u->op == OPERATOR_JS_TYPEOF && u->operand->node_type == AST_NODE_IDENT) {
            AstIdentNode* id = (AstIdentNode*)u->operand;
            // class receiver/meta reads must still check initialization and evaluate their value.
            bool class_value = c->function->home && (named(id->name, "this") || named(id->name, "new.target"));
            if (!class_value && !identifier_binding(c->program, id)) return text(c, "undefined");
        }
        LmdValue v = expression(c, u->operand, false,
            u->op == OPERATOR_POS || u->op == OPERATOR_NEG || u->op == OPERATOR_JS_BIT_NOT);
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
            return {bits, K_NUMBER, false, 0, true};
        }
        if (u->op == OPERATOR_POS && semantic(v) == K_NUMBER) return v;
        if (u->op == OPERATOR_NEG && v.literal) return number(c, -v.number);
        if (u->op == OPERATOR_NEG && v.integer && !v.number_present && v.range.state == 1 &&
                (v.range.lower > 0 || v.range.upper < 0)) {
            MIR_reg_t negated = em_new_reg(&c->em, "negative_integer", MIR_T_I64);
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_NEG, reg(c, negated), reg(c, v.reg)));
            return {negated, K_NUMBER, false, 0, true, finite_range(-v.range.upper, -v.range.lower)};
        }
        MIR_reg_t d = to_number(c, v);
        if (u->op == OPERATOR_POS) return {d, K_NUMBER};
        MIR_reg_t result = em_new_reg(&c->em, "negate", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_DNEG, reg(c, result), reg(c, d))); return {result, K_NUMBER};
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)n;
        LmdValue divisible;
        if (remainder_zero(c, b, &divisible)) return divisible;
        LmdValue concatenated;
        if (b->op == OPERATOR_ADD && string_concat(c, b, &concatenated)) return concatenated;
        if (b->op == OPERATOR_AND || b->op == OPERATOR_OR || b->op == OPERATOR_JS_NULLISH_COALESCE)
            return logical(c, b->op, b->left, b->right);
        bool consume = numeric_operands(b->op, kind(c->program, b->left, c->inlining),
            kind(c->program, b->right, c->inlining));
        MirNumericOpPlan plan = {};
        bool integer_consumer = (em_numeric_op_plan(b->op, &plan) && plan.is_comparison) ||
            (b->op >= OPERATOR_JS_BIT_AND && b->op <= OPERATOR_JS_URSHIFT);
        if (consume && integer_consumer && range(c->program, b->left, true, true).state == 1 &&
                range(c->program, b->right, true, true).state == 1) consume = false;
        // numeric consumers finish before a safepoint; a left borrow also needs an inert RHS.
        bool right_borrow = consume && immediate_scalar_operand(c, b->right);
        LmdValue left = expression(c, b->left, right_borrow && immediate_scalar_operand(c, b->left), consume);
        LmdValue right = expression(c, b->right, right_borrow, consume);
        if (b->op == OPERATOR_JS_INSTANCEOF) {
            MIR_reg_t result = em_call_2(&c->em, "mvp_lmd_instanceof", MIR_T_I64,
                MIR_T_I64, reg(c, box(c, left).reg), MIR_T_I64, reg(c, box(c, right).reg), true);
            check_error(c, result); return boxed(c, result, K_BOOL);
        }
        if (b->op == OPERATOR_IN) {
            LmdValue name = canonical_key(c, left);
            MIR_reg_t has = property_access(c, LMD_PROP_HAS, box(c, right).reg, name.reg);
            check_error(c, has); return boxed(c, has, K_BOOL);
        }
        return binary_value(c, b->op, left, right);
    }
    case AST_NODE_CONDITIONAL_EXPR: {
        AstIfNode* b = (AstIfNode*)n;
        bool numeric = kind(c->program, n) == K_NUMBER;
        MIR_reg_t result = em_new_reg(&c->em, "conditional", numeric ? MIR_T_D : MIR_T_I64);
        MIR_label_t alternate = label(c), done = label(c);
        branch_condition(c, b->test, alternate, false);
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
    case AST_NODE_NEW_EXPR: {
        AstCallNode* call = (AstCallNode*)n;
        LmdBinding* b = call->callee->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)call->callee) : NULL;
        if (b && b->intrinsic == I_ARRAY) return array_construct(c, call);
        if (c->program->receiver_abi && (!b || !b->intrinsic)) {
            LmdValue constructor = box(c, expression(c, call->callee));
            LmdCallCapture capture = {constructor, call_arguments(c, call, true)};
            capture.new_target = constructor;
            return ::call(c, call, &capture);
        }
        LmdValue* args = call_arguments(c, call, false);
        if (b && b->intrinsic >= I_INT32_ARRAY && b->intrinsic <= I_FLOAT64_ARRAY) {
            // ToIndex truncates fractions and maps NaN/undefined to zero before checking range.
            MIR_reg_t requested = to_number(c, call->arguments ? args[0] : number(c, 0));
            mem_free(args);
            MIR_reg_t d = em_call_1(&c->em, "trunc", MIR_T_D, MIR_T_D, reg(c, requested), true);
            MIR_reg_t length = constant(c, 0);
            MIR_label_t allocate = label(c), invalid = label(c), memory = label(c), done = label(c);
            branch_truth(c, allocate, op(c, MIR_DEQ, reg(c, d), reg(c, d)), false);
            move(c, length, reg(c, integer_lane(c, d, 0, INT53_MAX, invalid, false)));
            put_label(c, allocate);
            MIR_reg_t array = em_call_2(&c->em, "array_num_new", MIR_T_P, MIR_T_I64,
                integer(c, typed_lanes[b->intrinsic - I_INT32_ARRAY]), MIR_T_I64, reg(c, length), true);
            LmdValue owner = boxed(c, array, K_TYPED_ARRAY);
            branch_truth(c, memory, array, false);
            // shared allocation keeps a zero length header on buffer allocation failure.
            branch(c, MIR_BNE, memory, reg(c, length), reg(c, em_load_at(&c->em, array,
                LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "allocated_length")));
            jump(c, done);
            put_label(c, invalid); fail(c, LMD_MVP_RANGE, n);
            put_label(c, memory); fail(c, LMD_MVP_MEMORY, n);
            put_label(c, done); return owner;
        }
        mem_free(args);
        if (!b || b->intrinsic != 4 || call->arguments) { fail(c, LMD_MVP_CAPABILITY, n); return boxed(c, constant(c, ITEM_JS_UNDEFINED)); }
        MIR_reg_t map = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
            MIR_T_P, integer(c, (uint64_t)c->program->roots[1]), MIR_T_I64, integer(c, 1), true);
        check_error(c, map); return boxed(c, map, K_MAP);
    }
    case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL: {
        LmdObjectPlan* plan = object_plan(c->program, n);
        if (plan && plan->scalar_binding) {
            int index = 0;
            for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next) {
                LmdValue value = expression(c, ((AstPropertyNode*)e)->value);
                // each initializer still runs once, in order; retain borrowed scalar storage precisely.
                scalar_field_write(c, &plan->scalar_fields[index++], value);
            }
            return boxed(c, constant(c, ITEM_JS_UNDEFINED), K_UNDEFINED);
        }
        // a fixed-key fresh literal cannot escape through its initializers; root child snapshots first.
        int count = plan ? plan->blueprint->length : 0, index = 0;
        LmdValue* values = count ? (LmdValue*)mem_calloc(count, sizeof(LmdValue), MEM_CAT_TEMP) : NULL;
        if (values) for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next)
            values[index++] = expression(c, ((AstPropertyNode*)e)->value);
        MIR_reg_t shape = plan ? planned_shape(c, plan) : constant(c, (uint64_t)c->program->roots[0]);
        MIR_reg_t object = em_call_2(&c->em, "mvp_lmd_object_new", MIR_T_I64,
            MIR_T_P, reg(c, shape), MIR_T_I64, integer(c, 0), true);
        check_error(c, object); LmdValue owner = boxed(c, object, K_OBJECT);
        index = 0;
        for (AstNode* e = ((AstMapNode*)n)->properties; e; e = e->next) {
            AstPropertyNode* prop = (AstPropertyNode*)e;
            String* spelling = !prop->computed && prop->key->node_type == AST_NODE_IDENT
                ? name_pool_create_string(c->program->frontend->name_pool, ((AstIdentNode*)prop->key)->name) : NULL;
            LmdValue name = spelling ? boxed(c, constant(c, s2it(spelling)), K_STRING)
                : canonical_key(c, expression(c, prop->key));
            LmdValue value = values ? values[index++] : expression(c, prop->value);
            MIR_label_t miss = label(c), done = label(c);
            ShapeEntry* field = plan && spelling ? typemap_hash_lookup(plan->blueprint, spelling->chars, spelling->len) : NULL;
            // the first field cannot observe this fresh owner; later fields may follow a retype fallback.
            if (field && field_write(c, {NULL, owner, 0, true, 0, name.reg, plan, field}, value, miss,
                    e == ((AstMapNode*)n)->properties)) jump(c, done);
            put_label(c, miss);
            MIR_reg_t stored = property_access(c, LMD_PROP_SET, owner.reg, name.reg, box(c, value).reg);
            check_error(c, stored); put_label(c, done);
        }
        mem_free(values); return owner;
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
        if (!b || (b->owner != c->function && !inline_slot(c, b)) || e->is_parameter || b->native || b->intrinsic) continue;
        if (b->shape_hint && b->shape_hint->scalar_binding == b) continue;
        LmdScalarField* local = inline_slot(c, b);
        if (local && !is_boxed(local->value)) continue;
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
    if (!c->function->id && !c->inlining) move(c, c->em.frame.return_reg, integer(c, ITEM_JS_UNDEFINED));
}
struct LmdIntegerLoopPlan { LmdCompiler* compiler; LmdInlineFrame frame; bool allowed; bool floating; int nodes; };
static void integer_loop_node(AstNode* node, void* opaque) {
    if (!node) return;
    LmdIntegerLoopPlan* plan = (LmdIntegerLoopPlan*)opaque;
    if (!plan->allowed || ++plan->nodes > 96) { plan->allowed = false; return; }
    Operator operation = OPERATOR_ASSIGN;
    AstNode* target = NULL;
    AstNode* left = NULL;
    AstNode* right = NULL;
    switch (node->node_type) {
    case AST_NODE_IDENT: {
        LmdBinding* b = identifier_binding(plan->compiler->program, (AstIdentNode*)node);
        if (!b || !b->native || b->external || b->kinds != K_NUMBER || b->owner != plan->frame.function)
            { plan->allowed = false; return; }
        for (int i = 0; i < plan->frame.count; i++) if (plan->frame.bindings[i] == b) return;
        if (plan->frame.count == 32) { plan->allowed = false; return; }
        plan->frame.bindings[plan->frame.count++] = b;
        plan->floating |= !b->integer; return;
    }
    case AST_NODE_LITERAL: {
        AstLiteralNode* literal = (AstLiteralNode*)node;
        if (literal->literal_type != AST_LITERAL_NUMBER || !finite_integer(literal->value.number_value) ||
                literal->value.number_value < 0) plan->allowed = false;
        return;
    }
    case AST_NODE_ASSIGN: {
        AstAssignNode* a = (AstAssignNode*)node;
        target = left = a->left; right = a->right;
        operation = a->op == OPERATOR_ASSIGN ? OPERATOR_ASSIGN : compound_operator(a->op); break;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* u = (AstUnaryNode*)node;
        if (u->op != OPERATOR_JS_INCREMENT && u->op != OPERATOR_JS_DECREMENT)
            { plan->allowed = false; return; }
        target = u->operand; break;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* b = (AstBinaryNode*)node;
        operation = b->op; left = b->left; right = b->right; break;
    }
    case AST_NODE_BLOCK: case AST_NODE_EXPR_STMT: case AST_NODE_IF_EXPR: case AST_NODE_NULL: break;
    default: plan->allowed = false; return;
    }
    if (target) {
        LmdBinding* b = target->node_type == AST_NODE_IDENT
            ? identifier_binding(plan->compiler->program, (AstIdentNode*)target) : NULL;
        if (!b || b->entry->is_const || b->entry->is_function_name_binding)
            { plan->allowed = false; return; }
    }
    if (operation != OPERATOR_ASSIGN) {
        MirNumericOpPlan numeric = {};
        bool equality = operation == OPERATOR_EQ || operation == OPERATOR_NE ||
            operation == OPERATOR_JS_STRICT_EQ || operation == OPERATOR_JS_STRICT_NE;
        bool comparison = em_numeric_op_plan(operation, &numeric) && numeric.is_comparison;
        if (!equality && !comparison && operation != OPERATOR_ADD && operation != OPERATOR_SUB) {
            AstLiteralNode* factor = right && right->node_type == AST_NODE_LITERAL ? (AstLiteralNode*)right : NULL;
            if (operation == OPERATOR_MUL && (!factor || factor->literal_type != AST_LITERAL_NUMBER || factor->value.number_value <= 0))
                factor = left && left->node_type == AST_NODE_LITERAL ? (AstLiteralNode*)left : NULL;
            if ((operation != OPERATOR_MUL && operation != OPERATOR_DIV && operation != OPERATOR_MOD) ||
                    !factor || factor->literal_type != AST_LITERAL_NUMBER ||
                    !finite_integer(factor->value.number_value) || factor->value.number_value < 1)
                { plan->allowed = false; return; }
            uint64_t divisor = (uint64_t)factor->value.number_value;
            if (operation == OPERATOR_DIV && (divisor & (divisor - 1)))
                { plan->allowed = false; return; }
        }
    }
    js_ast_visit_children(node, integer_loop_node, opaque);
}
static void integer_loop(LmdCompiler* c, AstLoopControlNode* node, MIR_label_t done) {
    if (c->integer_bail || node->form != LOOP_FORM_WHILE || node->init || node->update || !node->cond) return;
    LmdIntegerLoopPlan plan = {}; plan.compiler = c; plan.allowed = true;
    plan.frame.parent = c->inlining; plan.frame.function = c->inlining ? c->inlining->function : c->function;
    if (!plan.frame.function->id) return;
    integer_loop_node(node->cond, &plan); integer_loop_node(node->body, &plan);
    if (!plan.allowed || !plan.floating || !plan.frame.count) return;
    MIR_label_t slow = label(c), head = label(c), finished = label(c), bail = label(c);
    MIR_reg_t snapshots[32];
    for (int i = 0; i < plan.frame.count; i++) {
        LmdValue value = read_binding(c, plan.frame.bindings[i], (AstNode*)node);
        MIR_reg_t integer = integer_lane(c, to_number(c, value), 1, INT53_MAX, slow, true);
        plan.frame.slots[i].value = {integer, K_NUMBER, false, 0, true, finite_range(1, INT53_MAX)};
        plan.frame.slots[i].integer = true; plan.frame.slots[i].kinds = K_NUMBER;
        plan.frame.slots[i].range = finite_range(1, INT53_MAX); plan.frame.slots[i].written = true;
        snapshots[i] = em_new_reg(&c->em, "iteration_snapshot", MIR_T_I64);
    }
    c->inlining = &plan.frame; c->integer_bail = bail;
    put_label(c, head);
    for (int i = 0; i < plan.frame.count; i++) move(c, snapshots[i], reg(c, plan.frame.slots[i].value.reg));
    branch_condition(c, node->cond, finished, false);
    statement(c, node->body); jump(c, head);
    c->inlining = plan.frame.parent; c->integer_bail = NULL;
    put_label(c, finished);
    for (int i = 0; i < plan.frame.count; i++) if (!plan.frame.bindings[i]->entry->is_const)
        write_binding(c, plan.frame.bindings[i], plan.frame.slots[i].value, (AstNode*)node);
    jump(c, done); put_label(c, bail);
    // only local numeric writes are admitted; undo a partial iteration before the generic replay.
    for (int i = 0; i < plan.frame.count; i++) if (!plan.frame.bindings[i]->entry->is_const)
        write_binding(c, plan.frame.bindings[i],
            {snapshots[i], K_NUMBER, false, 0, true, finite_range(1, INT53_MAX)}, (AstNode*)node);
    put_label(c, slow);
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
    integer_loop(c, n, done);
    if (n->form != LOOP_FORM_DO_WHILE) jump(c, test);
    put_label(c, start); statement(c, n->body);
    put_label(c, next); if (n->update) (void)expression(c, n->update);
    put_label(c, test);
    if (n->cond) branch_condition(c, n->cond, start);
    else jump(c, start);
    put_label(c, done); c->control = control.parent;
}
// direct collection traversal mirrors Lambda loops: owner + stable position, no iterator object.
static void for_of(LmdCompiler* c, JsForOfNode* loop, const char* name = NULL, int length = 0) {
    control_completion(c); scope_initialize(c, loop->vars);
    AstNode* iterable = loop->right;
    LmdValue owner;
    int projection = 6;
    MIR_reg_t selected_projection = 0;
    bool object_projection = false;
    if (iterable->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)iterable)->callee->node_type == AST_NODE_MEMBER_EXPR) {
        AstNode* receiver = ((AstFieldNode*)((AstCallNode*)iterable)->callee)->object;
        LmdBinding* binding = receiver->node_type == AST_NODE_IDENT
            ? identifier_binding(c->program, (AstIdentNode*)receiver) : NULL;
        object_projection = binding && binding->intrinsic == 5;
    }
    AstFieldNode* projection_field = iterable->node_type == AST_NODE_CALL_EXPR &&
        (((AstCallNode*)iterable)->callee->node_type == AST_NODE_MEMBER_EXPR ||
         ((AstCallNode*)iterable)->callee->node_type == AST_NODE_INDEX_EXPR)
        ? (AstFieldNode*)((AstCallNode*)iterable)->callee : NULL;
    String* projection_name = projection_field ? member_spelling(projection_field) : NULL;
    bool projection_call = projection_name && (named(projection_name, "entries") ||
        named(projection_name, "keys") || named(projection_name, "values"));
    if (!object_projection && projection_call && iterable->node_type == AST_NODE_CALL_EXPR &&
            (((AstCallNode*)iterable)->callee->node_type == AST_NODE_MEMBER_EXPR ||
             ((AstCallNode*)iterable)->callee->node_type == AST_NODE_INDEX_EXPR)) {
        AstCallNode* call = (AstCallNode*)iterable;
        LmdReference ref = reference(c, call->callee, true);
        LmdValue method = box(c, read_reference(c, ref, iterable, true));
        LmdValue* args = call_arguments(c, call, true);
        projection = named(projection_name, "entries") ? 6 : named(projection_name, "keys") ? 7 : 8;
        MIR_reg_t selected = em_new_reg(&c->em, "iterable_owner", MIR_T_I64);
        selected_projection = constant(c, 6);
        MIR_label_t builtin = label(c), selected_done = label(c);
        branch(c, MIR_BEQ, builtin, reg(c, method.reg), integer(c, mvp_lmd_method_token(projection)));
        LmdCallCapture capture = {method, args};
        move(c, selected, reg(c, box(c, ::call(c, call, &capture)).reg)); jump(c, selected_done);
        put_label(c, builtin); move(c, selected, reg(c, ref.owner.reg));
        move(c, selected_projection, integer(c, projection));
        put_label(c, selected_done); owner = boxed(c, selected);
    } else owner = box(c, expression(c, iterable));
    bool direct_pair = loop->left->node_type == AST_NODE_ARRAY_PATTERN && projection == 6;
    MIR_reg_t components[2] = {0, 0};
    MIR_label_t bind_pair = direct_pair ? label(c) : NULL;
    if (direct_pair) for (int i = 0; i < 2; i++) components[i] = em_new_reg(&c->em, "pair_component", MIR_T_I64);
    MIR_reg_t cursor = constant(c, 0), item = em_new_reg(&c->em, "iteration_value", MIR_T_I64);
    MIR_label_t test = label(c), array = label(c), map = label(c), body = label(c), next = label(c), done = label(c);
    MIR_reg_t active = constant(c, 0);
    MIR_label_t registered = label(c);
    branch(c, MIR_BNE, registered, reg(c, tag(c, owner)), integer(c, LMD_TYPE_MAP));
    branch(c, MIR_BNE, registered, reg(c, em_load_at(&c->em, owner.reg,
        LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "map_kind")), integer(c, MAP_KIND_ORDERED));
    MIR_reg_t count = em_load_at(&c->em, owner.reg, offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64, "active_cursors");
    em_store_at(&c->em, owner.reg, offsetof(LambdaGcOrderedMapLayout, cursors), MIR_T_I64,
        op(c, MIR_ADD, reg(c, count), integer(c, 1)));
    move(c, active, integer(c, 1)); put_label(c, registered);
    LmdControl control = {c->control, done, next, name, length, (AstNode*)loop, owner.reg, active}; c->control = &control;
    for (LmdControl* outer = control.parent; outer; outer = outer->parent)
        if (outer->loop_target == (AstNode*)loop) { outer->next = next; outer->stop = done; }
    put_label(c, test);
    branch(c, MIR_BEQ, array, reg(c, tag(c, owner)), integer(c, LMD_TYPE_ARRAY));
    branch(c, MIR_BEQ, map, reg(c, tag(c, owner)), integer(c, LMD_TYPE_MAP));
    fail(c, LMD_MVP_CAPABILITY, iterable);
    put_label(c, array);
    branch(c, MIR_UBGE, done, reg(c, cursor), reg(c, em_load_at(&c->em, owner.reg,
        LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "iteration_length")));
    move(c, item, reg(c, box(c, array_read(c, {NULL, owner, cursor})).reg)); jump(c, body);
    put_label(c, map);
    MIR_label_t ordered = label(c);
    branch(c, MIR_BEQ, ordered, reg(c, em_load_at(&c->em, owner.reg,
        LAMBDA_GC_OFF_CONTAINER_MAP_KIND, MIR_T_U8, "map_kind")), integer(c, MAP_KIND_ORDERED));
    fail(c, LMD_MVP_CAPABILITY, iterable); put_label(c, ordered);
    move(c, cursor, reg(c, em_call_2(&c->em, "mvp_lmd_map_next", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, cursor), true)));
    branch(c, MIR_BLT, done, reg(c, cursor), integer(c, 0));
    bool native_entry = direct_pair || projection == 7 || projection == 8;
    MIR_label_t ordinary_entries = selected_projection && projection != 6 ? label(c) : NULL;
    if (native_entry) {
        // an ordinary method's returned Map uses its default entry iteration.
        if (ordinary_entries) branch(c, MIR_BEQ, ordinary_entries,
            reg(c, selected_projection), integer(c, 6));
        MIR_reg_t entries = em_load_at(&c->em, owner.reg, offsetof(LambdaGcOrderedMapLayout, entries), MIR_T_I64, "ordered_entries");
        int count = direct_pair ? 2 : 1;
        for (int i = 0; i < count; i++) {
            MIR_reg_t slot = op(c, MIR_ADD, reg(c, cursor), integer(c, direct_pair ? i : projection == 8));
            LmdValue value = stable_item(c, em_load_at(&c->em,
                em_array_element_address(&c->em, entries, slot, 8), 0, MIR_T_I64, "ordered_value"));
            move(c, direct_pair ? components[i] : item, reg(c, value.reg));
            boxed(c, direct_pair ? components[i] : item);
        }
        jump(c, direct_pair ? bind_pair : body);
    }
    if (!native_entry || ordinary_entries) {
        if (ordinary_entries) put_label(c, ordinary_entries);
        MIR_reg_t entry = em_call_3(&c->em, "mvp_lmd_map_entry", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, cursor), MIR_T_I64,
            selected_projection ? reg(c, selected_projection) : integer(c, projection), true);
        check_error(c, entry); move(c, item, reg(c, stable_item(c, entry).reg));
    }
    put_label(c, body); LmdValue value = boxed(c, item);
    AstNode* head = loop->left;
    if (head->node_type == AST_NODE_ARRAY_PATTERN) {
        MIR_label_t pair = label(c);
        branch(c, MIR_BEQ, pair, reg(c, tag(c, value)), integer(c, LMD_TYPE_ARRAY));
        fail(c, LMD_MVP_CAPABILITY, head); put_label(c, pair);
        if (direct_pair) {
            for (int i = 0; i < 2; i++) move(c, components[i], reg(c, box(c,
                array_read(c, {NULL, value, constant(c, i), true, i})).reg));
            put_label(c, bind_pair);
        }
        int i = 0;
        for (AstNode* e = ((AstArrayNode*)head)->item; e; e = e->next, i++) {
            LmdValue component = direct_pair ? boxed(c, components[i]) :
                array_read(c, {NULL, value, constant(c, i), true, i});
            write_binding(c, identifier_binding(c->program, (AstIdentNode*)e), component, e, loop->declares_binding);
        }
    } else write_binding(c, identifier_binding(c->program, (AstIdentNode*)head), value, head, loop->declares_binding);
    statement(c, loop->body); put_label(c, next);
    MIR_reg_t step = constant(c, 1); MIR_label_t advance = label(c);
    branch(c, MIR_BNE, advance, reg(c, tag(c, owner)), integer(c, LMD_TYPE_MAP));
    move(c, step, integer(c, 4)); put_label(c, advance);
    move(c, cursor, reg(c, op(c, MIR_ADD, reg(c, cursor), reg(c, step)))); jump(c, test);
    put_label(c, done); release_iterators(c, control.parent); c->control = control.parent;
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
    case AST_NODE_CLASS: {
        AstClassNode* ast = (AstClassNode*)n;
        MvpLmdClass* cls = class_plan(c->program, ast);
        LmdBinding* inner = binding(c->program, ast->entry);
        if (inner) write_binding(c, inner, boxed(c, constant(c, ITEM_JS_TDZ)), n, true);
        LmdValue base = box(c, expression(c, ast->superclass));
        MIR_reg_t result = em_call_2(&c->em, "mvp_lmd_class_new", MIR_T_I64,
            MIR_T_P, integer(c, (uint64_t)cls), MIR_T_I64, reg(c, base.reg), true);
        check_error(c, result);
        if (inner) write_binding(c, inner, boxed(c, result, K_FUNCTION), n, true);
        LmdBinding* outer = binding(c->program, ast->outer_entry);
        if (outer && outer != inner) write_binding(c, outer, boxed(c, result, K_FUNCTION), n, true);
        break;
    }
    case AST_NODE_RAISE_STAM: {
        AstNode* operand = ((AstRaiseNode*)n)->value;
        // Error objects are admitted only as immediate uncaught throw operands in this phase.
        bool error_constructor = operand->node_type == AST_NODE_NEW_EXPR &&
            ((AstCallNode*)operand)->callee->node_type == AST_NODE_IDENT &&
            identifier_binding(c->program, (AstIdentNode*)((AstCallNode*)operand)->callee) == &c->program->intrinsics[I_ERROR - 1];
        LmdValue value;
        if (error_constructor) {
            AstCallNode* call = (AstCallNode*)operand;
            if (call->arguments && call->arguments->next) {
                diagnostic(c->program, n, "Error options"); break;
            }
            LmdValue* args = call_arguments(c, call, false);
            value = text(c, "");
            if (call->arguments) {
                // an explicit undefined message has the same empty message as Error().
                MIR_label_t done = label(c);
                branch(c, MIR_BEQ, done, reg(c, box(c, args[0]).reg), integer(c, ITEM_JS_UNDEFINED));
                move(c, value.reg, reg(c, to_string(c, args[0]).reg));
                put_label(c, done);
            }
            mem_free(args);
        } else value = expression(c, operand);
        MIR_reg_t error = em_call_2(&c->em, "mvp_lmd_throw", MIR_T_I64,
            MIR_T_I64, reg(c, box(c, value).reg), MIR_T_I64, integer(c, error_constructor), true);
        return_error(c, error); break;
    }
    case AST_NODE_VAR_STAM:
        for (AstNode* d = ((AstVarDeclNode*)n)->declarations; d; d = d->next) {
            AstDeclaratorNode* declaration = (AstDeclaratorNode*)d;
            LmdBinding* b = binding(c->program, ((AstIdentNode*)declaration->id)->entry);
            // an unobserved, initialized local function has only direct uses and no identity allocation.
            if (declaration->init && b->target && (AstNode*)b->target->ast == declaration->init &&
                    b->target->closed_calls && !b->observed && !b->assigned) continue;
            if (declaration->init || ((AstVarDeclNode*)n)->kind != JS_VAR_VAR) {
                LmdValue value = expression(c, declaration->init);
                if (!b->shape_hint || b->shape_hint->scalar_binding != b)
                    write_binding(c, b, value, d, true);
            }
        }
        break;
    case AST_NODE_EXPR_STMT: {
        LmdValue evaluated = expression(c, ((AstExprStmtNode*)n)->expression);
        if (c->function->id || c->inlining) break;
        LmdValue v = box(c, evaluated);
        // retain the latest expression completion; declarations do not reset it.
        move(c, c->em.frame.return_reg, reg(c, v.reg)); break;
    }
    case AST_NODE_RETURN_STAM:
        if (!tail_call(c, ((AstReturnNode*)n)->value))
            return_value(c, expression(c, ((AstReturnNode*)n)->value));
        break;
    case AST_NODE_IF_EXPR: {
        control_completion(c);
        JsIfNode* f = (JsIfNode*)n;
        MIR_label_t alternate = label(c), done = label(c);
        branch_condition(c, f->test, alternate, false);
        scope_initialize(c, f->consequent_vars); statement(c, f->then); jump(c, done);
        put_label(c, alternate); scope_initialize(c, f->alternate_vars); statement(c, f->otherwise);
        put_label(c, done); break;
    }
    case AST_NODE_LOOP: loop(c, (AstLoopControlNode*)n); break;
    case AST_NODE_FOR_OF_STAM: for_of(c, (JsForOfNode*)n); break;
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
            release_iterators(c, control);
            jump(c, next ? control->next : control->stop); break;
        }
        if (!control) diagnostic(c->program, n, "invalid control target"); break;
    }
    default:
        if (n->node_type == JS_AST_NODE_LABELED_STATEMENT) {
            JsLabeledStatementNode* l = (JsLabeledStatementNode*)n;
            if (l->body->node_type == AST_NODE_LOOP) loop(c, (AstLoopControlNode*)l->body, l->label, l->label_len);
            else if (l->body->node_type == AST_NODE_FOR_OF_STAM) for_of(c, (JsForOfNode*)l->body, l->label, l->label_len);
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
    {"fn_strcat3", (void*)fn_strcat3, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR))},
    {"fn_strcat4", (void*)fn_strcat4, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR)|ARG(3,GC_PTR))},
    {"fn_strcat5", (void*)fn_strcat5, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR)|ARG(3,GC_PTR)|ARG(4,GC_PTR))},
    {"fn_strcat6", (void*)fn_strcat6, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,GC_PTR)|ARG(2,GC_PTR)|ARG(3,GC_PTR)|ARG(4,GC_PTR)|ARG(5,GC_PTR))},
    {"fn_string_freeze", (void*)fn_string_freeze, AUDIT(JIT_EFFECT_NO_GC, GC_PTR, ARG(0,GC_PTR))},
    {"mvp_lmd_string_compare", (void*)mvp_lmd_string_compare, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_string_at", (void*)mvp_lmd_string_at, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR))},
    {"mvp_lmd_number_pow", (void*)mvp_lmd_number_pow, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"mvp_lmd_string_key", (void*)mvp_lmd_string_key, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_array_store", (void*)mvp_lmd_array_store, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,ITEM))},
    {"mvp_lmd_array_new", (void*)mvp_lmd_array_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR))},
    {"str_repeat", (void*)str_repeat, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"fn_join2", (void*)fn_join2, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_class_property", (void*)mvp_lmd_class_property, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,SCALAR))},
    {"mvp_lmd_class_new", (void*)mvp_lmd_class_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,ITEM))},
    {"mvp_lmd_class_invoke", (void*)mvp_lmd_class_invoke, {JIT_EFFECT_MAY_GC, JIT_REENTRY_YES, SCALAR,
        ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,RAW_PTR)|ARG(3,SCALAR)|ARG(4,ITEM),
        JIT_IMPORT_SCALAR_RESULT(SCALAR_RETURN_NONE)|JIT_IMPORT_NUMBER_STACK_PRESERVES|JIT_IMPORT_ARGS_BORROWED_AUDITED,
        JIT_EXCEPTION_PRESERVES, 0}},
    {"mvp_lmd_class_super", (void*)mvp_lmd_class_super, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_constructor_result", (void*)mvp_lmd_constructor_result, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_instanceof", (void*)mvp_lmd_instanceof, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_throw", (void*)mvp_lmd_throw, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_function_new", (void*)mvp_lmd_function_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,SCALAR)|ARG(1,RAW_PTR))},
    {"mvp_lmd_property_key", (void*)mvp_lmd_property_key, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM))},
    {"mvp_lmd_object_new", (void*)mvp_lmd_object_new, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,RAW_PTR)|ARG(1,SCALAR))},
    {"mvp_lmd_property_get", (void*)mvp_lmd_property_get, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_property_set", (void*)mvp_lmd_property_set, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM))},
    {"mvp_lmd_property_delete", (void*)mvp_lmd_property_delete, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM))},
    {"mvp_lmd_property_has", (void*)mvp_lmd_property_has, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,SCALAR))},
    {"mvp_lmd_object_project", (void*)mvp_lmd_object_project, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_map_call", (void*)mvp_lmd_map_call, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,ITEM)|ARG(2,ITEM)|ARG(3,ITEM))},
    {"mvp_lmd_map_next", (void*)mvp_lmd_map_next, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,ITEM)|ARG(1,SCALAR))},
    {"mvp_lmd_map_entry", (void*)mvp_lmd_map_entry, AUDIT(JIT_EFFECT_MAY_GC, ITEM, ARG(0,ITEM)|ARG(1,SCALAR)|ARG(2,SCALAR))},
    {"array", (void*)array, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, 0)},
    {"array_num_new", (void*)array_num_new, AUDIT(JIT_EFFECT_MAY_GC, GC_PTR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"array_reserve_append_slots", (void*)array_reserve_append_slots, AUDIT(JIT_EFFECT_MAY_GC, SCALAR, ARG(0,GC_PTR)|ARG(1,SCALAR))},
    {"owned_item_slot_store", (void*)owned_item_slot_store, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,SCALAR)|ARG(2,SCALAR)|ARG(3,ITEM))},
    {"lambda_item_adopt_scalar_home", (void*)lambda_item_adopt_scalar_home, AUDIT(JIT_EFFECT_NO_GC, ITEM, ARG(0,ITEM)|ARG(1,RAW_PTR))},
    {"utf8_to_utf16_length", (void*)utf8_to_utf16_length, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,RAW_PTR)|ARG(1,SCALAR))},
    {"fmod", (void*)(double (*)(double, double))fmod, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"sqrt", (void*)(double (*)(double))sqrt, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"sin", (void*)(double (*)(double))sin, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"cos", (void*)(double (*)(double))cos, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"floor", (void*)(double (*)(double))floor, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"ceil", (void*)(double (*)(double))ceil, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"trunc", (void*)(double (*)(double))trunc, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"fn_abs_f", (void*)fn_abs_f, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR))},
    {"fn_min2_u", (void*)fn_min2_u, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},
    {"fn_max2_u", (void*)fn_max2_u, AUDIT(JIT_EFFECT_NO_GC, SCALAR, ARG(0,SCALAR)|ARG(1,SCALAR))},

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
    {MIR_T_P, "arguments", 0}, {MIR_T_I64, "argc", 0}, {MIR_T_I64, "self", 0},
    {MIR_T_I64, "receiver", 0}, {MIR_T_I64, "new_target", 0}};
static void emit_return(LmdCompiler* c, bool unentered = false) {
    MirFrameState* frame = &c->em.frame;
    MIR_reg_t result, companion;
    if (unentered) {
        result = c->function->native ? (frame->return_type == MIR_T_D
            ? to_number(c, number(c, 0)) : constant(c, 0)) : constant(c, ITEM_ERROR);
        companion = constant(c, c->function->native ? ITEM_ERROR : 0);
    } else if (c->function->native) {
        result = frame->return_reg; companion = frame->error_return_reg;
    } else if (fn_return_shape_may_be_pending(frame->plan.return_shape))
        em_build_pending_pair(&c->em, frame->return_reg, &result, &companion);
    else { result = frame->return_reg; companion = constant(c, 0); }
    bool pair = em_returns_result_pair(frame->plan.companion);
    if (em_returns_companion_slot(frame->plan.companion))
        em_store_frame_top(&c->em, frame->runtime, offsetof(Context, mir_companion_slot), companion);
    if (!unentered) {
        em_store_frame_top(&c->em, frame->runtime, offsetof(Context, side_root_top), frame->root_base);
        // audited MVP imports preserve the watermark; only owned scalar storage needs an extent.
        if (frame->scalar_home_count || frame->fixed_number_slots || frame->number_frame_required)
            em_store_frame_top(&c->em, frame->runtime, offsetof(Context, side_number_top), frame->number_base);
    }
    em_emit_insn(&c->em, pair ? MIR_new_ret_insn(c->em.ctx, 2, reg(c, result), reg(c, companion))
        : MIR_new_ret_insn(c->em.ctx, 1, reg(c, result)));
}
static bool compile_function(MvpLmdProgram* p, LmdFunction* f) {
    LmdCompiler c = {}; c.program = p; c.function = f;
    c.em.ctx = p->mir; c.em.call_owner = &c; c.em.root_call_value = root_value;
    c.em.lookup_import_metadata = import_metadata; c.em.import_cache = p->import_cache;
    c.em.helper_results_skip_rehome = true;
    c.em.name_pool = p->frontend->name_pool;
    MIR_type_t result_types[] = {f->native && f->returns == K_NUMBER && !f->integer ? MIR_T_D : MIR_T_I64, MIR_T_I64};
    // the MIR API interns descriptor names; keep the reusable template immutable.
    MIR_var_t arguments[LAMBDA_MAX_FUNCTION_ARGS]; memcpy(arguments, f->signature, f->arity * sizeof(MIR_var_t));
    f->item = MIR_new_func_arr(p->mir, f->name, em_return_nres(f->return_abi.companion), result_types, f->arity, arguments);
    c.em.func_item = f->item; c.em.func = MIR_get_item_func(p->mir, f->item);
    MirFrameState* frame = &c.em.frame;
    frame->active = true; frame->number_active = true; frame->item_return = !f->native;
    frame->return_type = result_types[0]; frame->scalar_return_mode = f->return_abi.normal.scalar_class;
    frame->return_lane_kind = f->native ? RETURN_LANE_ERROR : RETURN_LANE_SCALAR;
    frame->runtime = MIR_reg(p->mir, "runtime", c.em.func);
    c.unit = MIR_reg(p->mir, "unit", c.em.func);
    if (!f->direct_args) {
        c.arguments = MIR_reg(p->mir, "arguments", c.em.func);
        c.argc = MIR_reg(p->mir, "argc", c.em.func);
        c.self = MIR_reg(p->mir, "self", c.em.func);
        if (p->receiver_abi) {
            c.receiver = MIR_reg(p->mir, "receiver", c.em.func);
            c.new_target = MIR_reg(p->mir, "new_target", c.em.func);
            root_value(&c, c.receiver, JIT_VALUE_BOXED_ITEM);
            root_value(&c, c.new_target, JIT_VALUE_BOXED_ITEM);
        }
    }
    for (int i = 0; i < f->arity; i++)
        em_function_argument_register(&c.em, MIR_reg(p->mir, f->signature[i].name, c.em.func));
    frame->root_base = em_new_reg(&c.em, "root_frame", MIR_T_I64);
    frame->root_end = em_new_reg(&c.em, "root_end", MIR_T_I64);
    frame->number_base = em_new_reg(&c.em, "number_frame", MIR_T_I64);
    frame->return_reg = em_new_reg(&c.em, "completion", frame->return_type);
    if (f->native) frame->error_return_reg = em_new_reg(&c.em, "error", MIR_T_I64);
    else root_value(&c, frame->return_reg, JIT_VALUE_BOXED_ITEM);
    frame->anchor = label(&c); frame->return_label = label(&c);
    frame->native_stack_overflow = label(&c);
    frame->plan.entry_kind = f->native ? FN_ENTRY_NATIVE_BODY : FN_ENTRY_BOXED_BODY;
    frame->plan.entry_mode = f->native ? MIR_ENTRY_BOUND_INTERNAL : MIR_ENTRY_CHECKED;
    frame->plan.debug_name = f->name;
    em_plan_bind_return(&frame->plan, &f->return_abi, !f->native);
    put_label(&c, frame->anchor);
    if (!f->native) move(&c, frame->return_reg, integer(&c, ITEM_JS_UNDEFINED));
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        b->scalar_home = 0;
        if (b->owner != f || (!f->id && !b->native)) continue;
        b->reg = em_new_reg(&c.em, "binding", b->native && (b->kinds & K_NUMBER) && !b->integer ? MIR_T_D : MIR_T_I64);
        b->number_present = b->native && b->kinds == (K_NUMBER | K_UNDEFINED)
            ? em_new_reg(&c.em, "binding_present", MIR_T_I64) : 0;
        if (!b->native) root_value(&c, b->reg, JIT_VALUE_BOXED_ITEM);
    }
    if (f->ast) {
        c.strict = f->ast->has_use_strict_directive || (f->ast->vars && f->ast->vars->strict);
        uint64_t i = 0;
        for (AstNode* a = f->ast->params; a; a = a->next, i++) {
            AstIdentNode* id = js_ast_parameter_binding_identifier(a);
            LmdBinding* b = binding(p, id->entry);
            if (f->direct_args) {
                MIR_reg_t value = MIR_reg(p->mir, f->signature[i + 2].name, c.em.func);
                write_binding(&c, b, b->native ? LmdValue{value, b->kinds, false, 0, b->integer, b->range}
                    : boxed(&c, value, b->kinds), a, true);
                continue;
            }
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
        // re-enter after parameter installation so self-tail calls reset activation locals.
        if (f->native) { c.tail_entry = label(&c); put_label(&c, c.tail_entry); }
        scope_initialize(&c, f->ast->vars);
        if (f->ast->vars && f->ast->vars->parent && f->ast->vars->parent->is_function_name_scope)
            scope_initialize(&c, f->ast->vars->parent);
        if (f->ast->body->node_type == AST_NODE_BLOCK) {
            statement(&c, f->ast->body);
            if (falls_through(f->ast->body)) return_value(&c, expression(&c, NULL));
        } else return_value(&c, expression(&c, f->ast->body));
    } else {
        c.strict = ((AstScript*)p->frontend->ast_root)->has_use_strict_directive;
        statement(&c, p->frontend->ast_root); jump(&c, frame->return_label);
    }
    put_label(&c, frame->return_label);
    emit_return(&c);
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
    emit_return(&c, true);
    em_finalize_function_metadata(&c.em);
    MIR_finish_func(p->mir);
    em_frame_dispose(&c.em);
    return !p->diagnostic[0];
}
extern "C" Item mvp_lmd_function_new(uint64_t code_id, MvpLmdProgram* p) {
    LmdFunction* f = (LmdFunction*)p->functions->data[code_id];
    Function* value = (Function*)heap_calloc(p->receiver_abi ? sizeof(MvpLmdCallable) : sizeof(Function), LMD_TYPE_FUNC);
    if (!value) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
    function_init_abi(value, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
    value->arity = (uint8_t)ast_linked_node_count(f->ast->params);
    value->ptr = (fn_ptr)f->address;
    value->runtime_context = context;
    value->def = f->ast;
    if (p->receiver_abi) {
        MvpLmdCallable* callable = (MvpLmdCallable*)value;
        callable->requires_runtime_context = true; callable->program = p; callable->home = f->home;
        if (f->home) {
            AstMethodNode* method = (AstMethodNode*)f->ast;
            callable->constructor = method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR;
            callable->static_method = method->static_method;
        }
    }
    return Item{.item = (uint64_t)value};
}
extern "C" Item mvp_lmd_class_new(MvpLmdClass* cls, Item base) {
    if (cls->ast->superclass) {
        if (base.item == ITEM_NULL) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        MvpLmdClass* parent = mvp_lmd_class_record(base);
        if (!parent || get_type_id(base) != LMD_TYPE_FUNC)
            return mvp_lmd_fail(get_type_id(base) == LMD_TYPE_FUNC || get_type_id(base) == LMD_TYPE_TYPE
                ? LMD_MVP_CAPABILITY : LMD_MVP_TYPE, 0);
        if (parent->program != cls->program) return mvp_lmd_fail(LMD_MVP_CAPABILITY, 0);
        cls->nominal.base = &parent->nominal;
        cls->prototype_nominal.base = &parent->prototype_nominal;
    }
    // top-level declarations evaluate once; the registered slots own every published class value.
    cls->values[1] = mvp_lmd_object_new(&cls->prototype_shape, 0);
    if (item_is_error(cls->values[1])) return cls->values[1];
    cls->values[2] = mvp_lmd_object_new(&cls->static_shape, 0);
    if (item_is_error(cls->values[2])) return cls->values[2];
    if (cls->constructor_id >= 0)
        cls->values[0] = mvp_lmd_function_new((uint64_t)cls->constructor_id, cls->program);
    else {
        MvpLmdCallable* fn = (MvpLmdCallable*)heap_calloc(sizeof(MvpLmdCallable), LMD_TYPE_FUNC);
        if (!fn) return mvp_lmd_fail(LMD_MVP_MEMORY, 0);
        function_init_abi(fn, LMD_TYPE_FUNC, FN_ENTRY_ABI_MVP_LMD);
        fn->requires_runtime_context = true; fn->runtime_context = context;
        fn->program = cls->program; fn->home = cls; fn->constructor = true;
        cls->values[0] = Item{.function = fn};
    }
    if (item_is_error(cls->values[0])) return cls->values[0];
    Map* prototype = cls->values[1].map;
    Map* statics = cls->values[2].map;
    // every slot is predeclared and nonenumerable; initialization does not mutate shared shapes.
    ShapeEntry* constructor = typemap_hash_lookup(&cls->prototype_shape, "constructor", 11);
    ShapeEntry* proto = typemap_hash_lookup(&cls->static_shape, "prototype", 9);
    map_field_store((char*)prototype->data + constructor->byte_offset, cls->values[0], LMD_TYPE_FUNC);
    map_field_store((char*)statics->data + proto->byte_offset, cls->values[1], LMD_TYPE_MAP);
    ShapeEntry* name = typemap_hash_lookup(&cls->static_shape, "name", 4);
    ShapeEntry* length = typemap_hash_lookup(&cls->static_shape, "length", 6);
    if (name->type->type_id == LMD_TYPE_STRING)
        map_field_store((char*)statics->data + name->byte_offset, Item{.item = s2it(cls->ast->name)}, LMD_TYPE_STRING);
    if (length->type->type_id == LMD_TYPE_INT)
        map_field_store((char*)statics->data + length->byte_offset, Item{.item = i2it(cls->values[0].function->arity)}, LMD_TYPE_INT);
    for (int i = 1; i < cls->program->functions->length; i++) {
        LmdFunction* function = (LmdFunction*)cls->program->functions->data[i];
        if (function->home != cls || function->id == cls->constructor_id) continue;
        AstMethodNode* method = (AstMethodNode*)function->ast;
        String* key = name_pool_create_string(cls->program->frontend->name_pool, ((AstIdentNode*)method->key)->name);
        Item value = mvp_lmd_function_new(function->id, cls->program);
        if (item_is_error(value)) return value;
        Map* target = method->static_method ? statics : prototype;
        ShapeEntry* field = typemap_hash_lookup((TypeMap*)target->type, key->chars, key->len);
        map_field_store((char*)target->data + field->byte_offset, value, LMD_TYPE_FUNC);
    }
    return cls->values[0];
}

static int admission_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    p->immutable_objects = true;
    p->literal_kinds = (unsigned*)pool_calloc(p->frontend->pool,
        p->frontend->ast_index.count * sizeof(unsigned));
    if (!p->literal_kinds) { diagnostic(p, NULL, "literal kind allocation"); return false; }
    p->functions = arraylist_new(8); p->bindings = arraylist_new(16); p->classes = arraylist_new(4);
    if (!p->functions || !p->bindings || !p->classes) {
        diagnostic(p, NULL, "admission allocation"); return false;
    }
    LmdFunction* main = (LmdFunction*)mem_calloc(1, sizeof(LmdFunction), MEM_CAT_TEMP);
    str_copy(main->name, sizeof(main->name), "mvp_lmd_program", 15);
    arraylist_append(p->functions, main);
    p->intrinsic_uses = (LmdBinding**)mem_calloc(p->frontend->ast_index.count,
        sizeof(LmdBinding*), MEM_CAT_TEMP);
    for (int i = 0; i < I_COUNT - 1; i++) {
        p->intrinsics[i].owner = main; p->intrinsics[i].intrinsic = (uint8_t)i + 1;
        p->intrinsics[i].kinds = i >= 3 ? K_OBJECT : i ? K_NUMBER : K_UNDEFINED;
    }
    for (int i = 0; i < 2; i++) {
        const char* name = i ? "MvpLmdMap" : "MvpLmdObject";
        p->families[i].type_name = {name, strlen(name)};
        p->families[i].struct_kind = LMD_TYPE_MAP;
        p->roots[i] = (TypeMap*)alloc_type(p->frontend->pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!p->roots[i]) return false;
        p->roots[i]->type_id = LMD_TYPE_MAP;
        p->roots[i]->nominal = &p->families[i];
        p->roots[i]->is_nominal = true;
        p->roots[i]->type_index = -1;
    }
    LmdWalk gather = {p, main, NULL, false}; walk(p->frontend->ast_root, &gather);
    gather.facts = true; walk(p->frontend->ast_root, &gather);
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i]; b->seed_kinds = b->kinds;
    }
    return !p->diagnostic[0];
}
static int representation_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->array = {}; f->array_changes = 0;
    }
    // only stable declarations used exclusively as direct callees have a closed incoming domain.
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        bool initialized_function = !b->external && b->dominated && b->writes == 1 && b->initializer &&
            (b->initializer->node_type == AST_NODE_FUNC_EXPR || b->initializer->node_type == AST_NODE_ARROW_FUNC);
        if (initialized_function) {
            AstFuncNode* initialized = (AstFuncNode*)b->initializer;
            // named expressions require the boxed entry's self identity for their name scope.
            if (initialized->vars && initialized->vars->parent && initialized->vars->parent->is_function_name_scope)
                initialized_function = false;
        }
        if (initialized_function) b->target = function(p, b->initializer);
        if (b->target && !b->assigned && !b->observed && b->entry->node &&
                (b->entry->node->node_type == AST_NODE_FUNC || initialized_function) && !b->entry->is_annex_b_companion)
            b->target->closed_calls = true;
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (b->entry->is_parameter && b->owner->closed_calls) b->kinds = 0;
        b->array = {};
        b->array_changes = 0;
        if (b->entry->is_parameter && !b->owner->closed_calls) b->array.lanes = LMD_ARRAY_UNKNOWN;
    }
    // refine kinds once with solved extents; unknown bounds keep number-or-undefined.
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            memset(p->literal_kinds, 0, p->frontend->ast_index.count * sizeof(unsigned));
            for (int i = 0; i < p->bindings->length; i++) {
                LmdBinding* b = (LmdBinding*)p->bindings->data[i];
                b->kinds = b->entry->is_parameter && b->owner->closed_calls ? 0 : b->seed_kinds;
            }
            for (int i = 0; i < p->functions->length; i++)
                ((LmdFunction*)p->functions->data[i])->returns = 0;
        }
        LmdTypes types = {p, true};
        while (types.changed) { types.changed = false; type_walk(p->frontend->ast_root, &types); }
        // unresolved recursive domains remain generic; propagate that widening before selecting ABIs.
        for (int i = 0; i < p->functions->length; i++) {
            LmdFunction* f = (LmdFunction*)p->functions->data[i];
            if (!f->returns) { f->returns = K_ANY; types.changed = true; }
            if (!f->array.lanes) { f->array.lanes = LMD_ARRAY_UNKNOWN; types.changed = true; }
        }
        for (int i = 0; i < p->bindings->length; i++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[i];
            if (!b->kinds) { b->kinds = K_ANY; types.changed = true; }
            if (!b->array.lanes) { b->array.lanes = LMD_ARRAY_UNKNOWN; types.changed = true; }
        }
        while (types.changed) { types.changed = false; type_walk(p->frontend->ast_root, &types); }
        if (pass) break;
        LmdRanges ranges = {p, NULL, true};
        while (ranges.changed) { ranges.changed = false; range_walk(p->frontend->ast_root, &ranges); }
        // unresolved cycles cannot establish integer admission; propagate widening before choosing carriers.
        for (int i = 0; i < p->bindings->length; i++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[i];
            if (!b->range.state) { b->range.state = 2; ranges.changed = true; }
            if (b->kinds == (K_NUMBER | K_UNDEFINED) && !b->present_range.state)
                { b->present_range.state = 2; ranges.changed = true; }
            if (!b->array.length.state) { b->array.length.state = 2; ranges.changed = true; }
        }
        for (int i = 0; i < p->functions->length; i++) {
            LmdFunction* f = (LmdFunction*)p->functions->data[i];
            if (!f->range.state) { f->range.state = 2; ranges.changed = true; }
            if (!f->array.length.state) { f->array.length.state = 2; ranges.changed = true; }
        }
        while (ranges.changed) { ranges.changed = false; range_walk(p->frontend->ast_root, &ranges); }
    }
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        if (!b->kinds) b->kinds = K_ANY;
        b->native = !b->external && (b->kinds == K_NUMBER || b->kinds == K_BOOL ||
            (!b->entry->is_parameter && b->kinds == (K_NUMBER | K_UNDEFINED))) &&
            (b->entry->is_parameter ? b->owner->closed_calls : b->initializer && b->dominated);
        b->integer = b->native && (b->kinds == K_NUMBER ? b->range.state == 1 :
            b->kinds == (K_NUMBER | K_UNDEFINED) && b->present_range.state == 1);
    }
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->direct_args = f->closed_calls && ast_linked_node_count(f->ast->params) + 2 <= LAMBDA_MAX_FUNCTION_ARGS;
        f->native = f->direct_args && (f->returns == K_NUMBER || f->returns == K_BOOL);
        f->integer = f->native && f->returns == K_NUMBER && f->range.state == 1;
    }
    // keep closed native numeric regions coherent instead of converting invariant operands in a float loop.
    for (int i = 1; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        if (!f->native) continue;
        bool integral = f->returns != K_NUMBER || f->integer;
        bool array_region = false;
        for (int j = 0; j < p->bindings->length; j++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[j];
            if (b->owner == f && b->native && b->kinds == K_NUMBER) integral &= b->integer;
            if (b->owner == f && (b->kinds == K_ARRAY || b->kinds == K_TYPED_ARRAY)) array_region = true;
        }
        // array indices retain their proven integer carrier beside floating accumulators.
        f->integer_region = integral || array_region;
        if (!f->integer_region) for (int j = 0; j < p->bindings->length; j++) {
            LmdBinding* b = (LmdBinding*)p->bindings->data[j];
            if (b->owner == f) b->integer = false;
        }
    }
    return true;
}
static bool plan_shape_field(MvpLmdProgram* p, TypeMap* shape, String* key, TypeId tid, uint8_t flags) {
    if (!key) return false;
    key = name_pool_create_string(p->frontend->name_pool, key);
    if (!key) return false;
    if (typemap_shape_lookup_last(shape, key->chars, key->len)) return true;
    ShapeEntry* field = (ShapeEntry*)pool_calloc(p->frontend->pool, sizeof(ShapeEntry) + sizeof(StrView));
    if (!field) return false;
    field->name = (StrView*)(field + 1); *field->name = {key->chars, key->len};
    field->key_kind = NAME_KEY_STRING; field->flags = flags;
    field->name_hash = typemap_name_hash(key->chars, key->len);
    shape_entry_set_type(field, type_info[tid].type);
    field->byte_offset = shape->byte_size; shape->byte_size += shape_entry_storage_size(field);
    if (shape->last) shape->last->chain_next = field; else shape->shape = field;
    shape->last = field; shape->length++;
    return true;
}
static void plan_objects(AstNode* node, void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    if (node->node_type == AST_NODE_MAP || node->node_type == AST_NODE_OBJECT_LITERAL) {
        Pool* pool = p->frontend->pool;
        TypeMap* shape = (TypeMap*)alloc_type(pool, LMD_TYPE_MAP, sizeof(TypeMap));
        if (!shape) { diagnostic(p, node, "literal shape allocation"); return; }
        shape->nominal = &p->families[0]; shape->is_nominal = true; shape->type_index = -1;
        bool exact = true;
        for (AstNode* n = ((AstMapNode*)node)->properties; n; n = n->next) {
            AstPropertyNode* prop = (AstPropertyNode*)n;
            if (prop->computed || prop->key->node_type != AST_NODE_IDENT) { exact = false; break; }
            String* key = name_pool_create_string(p->frontend->name_pool, ((AstIdentNode*)prop->key)->name);
            if (!key || typemap_shape_lookup_last(shape, key->chars, key->len)) { exact = false; break; }
            unsigned k = kind(p, prop->value);
            TypeId tid = k == K_UNDEFINED ? LMD_TYPE_UNDEFINED : k == K_NULL ? LMD_TYPE_NULL :
                k == K_BOOL ? LMD_TYPE_BOOL : k == K_STRING ? LMD_TYPE_STRING : k == K_ARRAY ? LMD_TYPE_ARRAY :
                k == K_OBJECT || k == K_MAP || ((k & K_OBJECT) && !(k & ~(K_OBJECT | K_NULL | K_UNDEFINED)))
                    ? LMD_TYPE_MAP : k == K_FUNCTION ? LMD_TYPE_FUNC : LMD_TYPE_ANY;
            if (k == K_NUMBER) tid = range(p, prop->value).state == 1 ? LMD_TYPE_INT : LMD_TYPE_FLOAT;
            if (tid == LMD_TYPE_ANY) { exact = false; break; }
            if (!plan_shape_field(p, shape, key, tid, 0)) { exact = false; break; }
        }
        if (exact) {
            typemap_hash_build(shape, pool);
            LmdObjectPlan* plan = (LmdObjectPlan*)pool_calloc(pool, sizeof(LmdObjectPlan));
            if (!plan) diagnostic(p, node, "literal plan allocation");
            else { *plan = {node, shape, shape}; arraylist_append(p->objects, plan); }
        }
    }
    js_ast_visit_children(node, plan_objects, opaque);
}
struct LmdShapes { MvpLmdProgram* program; LmdFunction* owner; bool changed; };
static void shape_hint(LmdShapes* state, LmdShapeSet* target, LmdObjectPlan** first, AstNode* value) {
    state->changed |= shape_union(target, object_shapes(state->program, value));
    *first = target->plans[0];
}
static void shape_walk(AstNode* node, void* opaque) {
    LmdShapes* state = (LmdShapes*)opaque;
    MvpLmdProgram* p = state->program;
    LmdFunction* f = function(p, node);
    if (f) {
        LmdShapes inner = {p, f, false};
        if (f->ast->body->node_type != AST_NODE_BLOCK) shape_hint(&inner, &f->shapes, &f->shape_hint, f->ast->body);
        js_ast_visit_children(node, shape_walk, &inner);
        state->changed |= inner.changed; return;
    }
    if (node->node_type == AST_NODE_RETURN_STAM && state->owner)
        shape_hint(state, &state->owner->shapes, &state->owner->shape_hint, ((AstReturnNode*)node)->value);
    if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        f = direct_target(p, call);
        AstNode* actual = call->arguments;
        for (AstNode* formal = f && f->closed_calls ? f->ast->params : NULL;
                formal && actual; formal = formal->next, actual = actual->next) {
            LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(formal)->entry);
            shape_hint(state, &b->shapes, &b->shape_hint, actual);
        }
    }
    if (node->node_type == AST_NODE_ASSIGN) {
        AstAssignNode* assignment = (AstAssignNode*)node;
        if (assignment->op == OPERATOR_ASSIGN && assignment->left->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)assignment->left);
            if (b) shape_hint(state, &b->shapes, &b->shape_hint, assignment->right);
        }
    }
    if (node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        AstDeclaratorNode* d = (AstDeclaratorNode*)node;
        if (d->id->node_type == AST_NODE_IDENT) {
            LmdBinding* b = identifier_binding(p, (AstIdentNode*)d->id);
            shape_hint(state, &b->shapes, &b->shape_hint, d->init);
        }
    }
    js_ast_visit_children(node, shape_walk, state);
}
static void plan_scalar_objects(MvpLmdProgram* p) {
    const AstIndex* index = &p->frontend->ast_index;
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        LmdObjectPlan* plan = b->shape_hint;
        if (!plan || plan->literal != b->initializer || b->external || !b->dominated ||
                b->writes != 1 || !plan->blueprint->length || plan->blueprint->length > 4) continue;
        bool local = true;
        for (uint32_t id = 0; local && id < index->count; id++) {
            AstNode* use = index->nodes[id];
            if (use->node_type != AST_NODE_IDENT || ((AstIdentNode*)use)->entry != b->entry) continue;
            AstNode* parent = ast_index_parent(index, use);
            if (parent && parent->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                    ((AstDeclaratorNode*)parent)->id == use) continue;
            // identity, aliases and use before initialization require a real object.
            if (!parent || parent->node_type != AST_NODE_MEMBER_EXPR ||
                    ((AstFieldNode*)parent)->object != use || ((AstFieldNode*)parent)->computed ||
                    use->source_span.start_byte < b->initializer->source_span.end_byte) { local = false; break; }
            AstFieldNode* member = (AstFieldNode*)parent;
            String* key = ((AstIdentNode*)member->property)->name;
            AstNode* consumer = ast_index_parent(index, parent);
            if (!typemap_hash_lookup(plan->blueprint, key->chars, key->len) ||
                    (consumer && ((consumer->node_type == AST_NODE_UNARY &&
                        ((AstUnaryNode*)consumer)->op == OPERATOR_JS_DELETE) ||
                    (consumer->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)consumer)->callee == parent)))) local = false;
        }
        if (local) {
            plan->scalar_binding = b;
            plan->scalar_fields = (LmdScalarField*)pool_calloc(p->frontend->pool, plan->blueprint->length * sizeof(LmdScalarField));
            if (!plan->scalar_fields) diagnostic(p, plan->literal, "scalar field allocation");
            else {
                int field_index = 0;
                for (AstNode* e = ((AstMapNode*)plan->literal)->properties; e; e = e->next) {
                    AstNode* value = ((AstPropertyNode*)e)->value;
                    plan->scalar_fields[field_index].initializer = value;
                    plan->scalar_fields[field_index++].kinds = kind(p, value);
                }
                for (uint32_t id = 0; id < index->count; id++) {
                    AstNode* use = index->nodes[id];
                    if (use->node_type != AST_NODE_MEMBER_EXPR) continue;
                    LmdScalarField* field = scalar_field(p, use);
                    if (!field) continue;
                    AstNode* consumer = ast_index_parent(index, use);
                    if (consumer && ((consumer->node_type == AST_NODE_ASSIGN && ((AstAssignNode*)consumer)->left == use) ||
                            (consumer->node_type == AST_NODE_UNARY &&
                                (((AstUnaryNode*)consumer)->op == OPERATOR_JS_INCREMENT ||
                                 ((AstUnaryNode*)consumer)->op == OPERATOR_JS_DECREMENT)))) field->written = true;
                }
            }
        }
    }
}
static int lower_pass(void* opaque) {
    MvpLmdProgram* p = (MvpLmdProgram*)opaque;
    for (int i = 0; i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        bool ok = plan_shape_field(p, &cls->prototype_shape,
            name_pool_create_len(p->frontend->name_pool, "constructor", 11), LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE);
        ok &= plan_shape_field(p, &cls->static_shape,
            name_pool_create_len(p->frontend->name_pool, "prototype", 9), LMD_TYPE_MAP, JSPD_NON_ENUMERABLE);
        for (AstNode* n = ((AstBlockNode*)cls->ast->body)->statements; n; n = n->next) {
            AstMethodNode* method = (AstMethodNode*)n;
            if (method->kind == AstMethodNode::JS_METHOD_CONSTRUCTOR) continue;
            ok &= plan_shape_field(p, method->static_method ? &cls->static_shape : &cls->prototype_shape,
                ((AstIdentNode*)method->key)->name, LMD_TYPE_FUNC, JSPD_NON_ENUMERABLE);
        }
        ok &= plan_shape_field(p, &cls->static_shape,
            name_pool_create_len(p->frontend->name_pool, "name", 4), LMD_TYPE_STRING, JSPD_NON_ENUMERABLE);
        ok &= plan_shape_field(p, &cls->static_shape,
            name_pool_create_len(p->frontend->name_pool, "length", 6), LMD_TYPE_INT, JSPD_NON_ENUMERABLE);
        if (!ok) { diagnostic(p, (AstNode*)cls->ast, "class shape allocation"); return false; }
        typemap_hash_build(&cls->prototype_shape, p->frontend->pool);
        typemap_hash_build(&cls->static_shape, p->frontend->pool);
    }
    p->objects = arraylist_new(8);
    plan_objects(p->frontend->ast_root, p);
    if (p->diagnostic[0]) return false;
    LmdShapes shapes = {p, NULL, true};
    while (shapes.changed) { shapes.changed = false; shape_walk(p->frontend->ast_root, &shapes); }
    plan_scalar_objects(p);
    // proven unobservable fields feed the existing type/range solver, including accumulator lanes.
    for (int i = 0; i < p->bindings->length; i++) {
        LmdBinding* b = (LmdBinding*)p->bindings->data[i];
        b->kinds = b->seed_kinds; b->range = {}; b->range_changes = 0;
        b->present_range = {}; b->present_range_changes = 0;
    }
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->returns = 0; f->range = {}; f->range_changes = 0;
    }
    if (!representation_pass(p) || p->diagnostic[0]) return false;
    p->import_cache = em_import_cache_new(32);
    p->mir = MIR_init(); p->module = MIR_new_module(p->mir, "js_mvp_lmd");
    MIR_type_t result_type = MIR_T_I64;
    int entry_arity = p->receiver_abi ? 7 : 5;
    MIR_var_t arguments[7]; memcpy(arguments, entry_arguments, entry_arity * sizeof(MIR_var_t));
    p->entry.proto = MIR_new_proto_arr(p->mir, "mvp_lmd_signature", 1, &result_type, entry_arity, arguments);
    p->entry.audit = (JitImportMetadata)AUDIT(JIT_EFFECT_MAY_GC, SCALAR,
        ARG(0,RAW_PTR)|ARG(1,RAW_PTR)|ARG(2,RAW_PTR)|ARG(3,SCALAR)|ARG(4,ITEM));
    if (p->receiver_abi) p->entry.audit.arg_classes |= ARG(5,ITEM)|ARG(6,ITEM);
    em_normalize_import_call(&p->entry, MIR_T_I64, entry_arity, arguments, 1);
    for (int i = 0; i < p->functions->length; i++) {
        LmdFunction* f = (LmdFunction*)p->functions->data[i];
        f->forward = MIR_new_forward(p->mir, f->name);
        // closed nonnumeric results never need a pending scalar payload or a caller number home.
        ScalarReturnClass scalar = f->native || (f->direct_args && !(f->returns & K_NUMBER))
            ? SCALAR_RETURN_NONE : SCALAR_RETURN_F64;
        f->return_abi.shape = em_return_shape(f->native, f->native, scalar);
        f->return_abi.companion = em_companion_transport(f->return_abi.shape, !f->native);
        f->return_abi.normal = {f->native ? (f->returns == K_NUMBER ? (f->integer ? LMD_TYPE_INT : LMD_TYPE_FLOAT) : LMD_TYPE_BOOL) : LMD_TYPE_ANY,
            f->native ? (f->returns == K_NUMBER ? (f->integer ? VALUE_REP_INT_LANE : VALUE_REP_F64) : VALUE_REP_I64) : VALUE_REP_ITEM,
            scalar};
        if (f->native) {
            f->return_abi.error_lane = FN_ERROR_LANE_CONTEXT_ITEM;
            f->return_abi.error = {LMD_TYPE_ERROR, VALUE_REP_ITEM, SCALAR_RETURN_NONE};
        }
        if (f->direct_args) {
            MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS] = {MIR_T_P, MIR_T_P};
            uint64_t classes = ARG(0,RAW_PTR) | ARG(1,RAW_PTR);
            f->arity = 2;
            for (AstNode* a = f->ast->params; a; a = a->next) {
                LmdBinding* b = binding(p, js_ast_parameter_binding_identifier(a)->entry);
                types[f->arity] = b->native && b->kinds == K_NUMBER && !b->integer ? MIR_T_D : MIR_T_I64;
                classes |= b->native ? ARG(f->arity, SCALAR) : ARG(f->arity, ITEM); f->arity++;
            }
            mir_prepare_call_args(f->signature, types, f->arity);
            f->signature[0].name = "runtime"; f->signature[1].name = "unit";
            MIR_type_t results[] = {f->native && f->returns == K_NUMBER && !f->integer ? MIR_T_D : MIR_T_I64, MIR_T_I64};
            char name[64]; snprintf(name, sizeof(name), "%s_signature", f->name);
            f->entry.proto = MIR_new_proto_arr(p->mir, name, em_return_nres(f->return_abi.companion), results, f->arity, f->signature);
            f->entry.audit = (JitImportMetadata)AUDIT(JIT_EFFECT_MAY_GC, SCALAR, classes);
            em_normalize_import_call(&f->entry, results[0], f->arity, f->signature, em_return_nres(f->return_abi.companion));
            f->entry.call.return_shape = f->return_abi.shape;
            f->entry.call.abi = &f->return_abi;
        } else {
            f->arity = entry_arity; memcpy(f->signature, entry_arguments, entry_arity * sizeof(MIR_var_t));
        }
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
    lambda_stack_init();
    // use the shared recoverable budget and unwind headroom for native entry guards.
    e->context.stack_limit = lambda_stack_recoverable_limit();
    if (!eval_context_init(&e->context)) return e;
    e->active = true; heap_init();
    if (!e->context.heap) return e;
    e->context.pool = e->context.heap->pool;
    for (int i = 0; e->program.objects && i < e->program.objects->length; i++) {
        LmdObjectPlan* plan = (LmdObjectPlan*)e->program.objects->data[i];
        TypeMap* shape = e->program.roots[0];
        Input* tree = runtime_shape_tree();
        FOR_EACH_MAP_FIELD(plan->blueprint, field) {
            shape = shape && tree ? type_tree_add_map_field_chars(tree, shape,
                field->name->str, field->name->length, field->type->type_id, NULL) : NULL;
        }
        plan->shape = shape ? shape : plan->blueprint;
    }
    MvpLmdProgram* p = &e->program;
    if (p->slot_count) {
        p->slots = (Item*)mem_calloc((size_t)p->slot_count * 2, sizeof(Item), MEM_CAT_EVAL);
        if (!p->slots || !heap_try_register_gc_root_range((uint64_t*)p->slots, (int)p->slot_count)) return e;
        e->registered = true;
    }
    LmdFunction* main = (LmdFunction*)p->functions->data[0];
    typedef Item (*Entry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item);
    for (int i = 0; i < p->classes->length; i++) {
        MvpLmdClass* cls = (MvpLmdClass*)p->classes->data[i];
        cls->values = p->slots + cls->slot;
    }
    Item undefined = {.item = ITEM_JS_UNDEFINED};
    typedef Item (*ReceiverEntry)(Context*, MvpLmdProgram*, Item*, uint64_t, Item, Item, Item);
    uint64_t started = execution_ms ? time_now_ns() : 0;
    Item result = p->receiver_abi
        ? ((ReceiverEntry)main->address)(&e->context, p, NULL, 0, undefined, undefined, undefined)
        : ((Entry)main->address)(&e->context, p, NULL, 0, undefined);
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
        context_capsule_destroy_all(&e->context);
        if (e->context.name_pool) name_pool_release(e->context.name_pool);
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
    if (p->objects) arraylist_free(p->objects);
    if (p->classes) arraylist_free(p->classes);
    if (p->frontend) js_transpiler_destroy(p->frontend);
    mem_free(e);
}
