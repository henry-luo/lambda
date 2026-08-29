#include "ast-core.hpp"

#include <stdlib.h>
#include <string.h>

// Names are stable report keys, not prose: the census baseline file and the
// AST dump are diffed across builds [Type_Infer TI3].
extern "C" const char* any_reason_name(AnyReason reason) {
    switch (reason) {
    case ANY_OPEN_PARAM:          return "open_param";
    case ANY_OPEN_MAP:            return "open_map";
    case ANY_DYNAMIC_NAME:        return "dynamic_name";
    case ANY_EXPLICIT:            return "explicit";
    case ANY_SYSFUNC_ROW:         return "sysfunc_row";
    case ANY_INDEX_ELEM:          return "index_elem";
    case ANY_MEMBER_SHAPE:        return "member_shape";
    case ANY_JOIN:                return "join";
    case ANY_LOGICAL_AND:         return "logical_and";
    case ANY_COMPARE:             return "compare";
    case ANY_LIST:                return "list";
    case ANY_UNARY:               return "unary";
    case ANY_LOOP_SRC:            return "loop_src";
    case ANY_DECOMPOSE:           return "decompose";
    case ANY_PIPE:                return "pipe";
    case ANY_JS_BINARY:           return "js_binary";
    case ANY_JS_CALL_MEMBER:      return "js_call_member";
    case ANY_JS_CALL:             return "js_call";
    case ANY_JS_MEMBER:           return "js_member";
    case ANY_ARITH_OPERAND:       return "arith_operand";
    case ANY_JOIN_OP:             return "join_op";
    case ANY_CALL_RESULT:         return "call_result";
    case ANY_WIDENED_VAR:         return "widened_var";
    case ANY_STATEMENT:           return "statement";
    case ANY_ERROR_RECOVERY:      return "error_recovery";
    case ANY_LEGACY_UNCLASSIFIED: return "legacy_unclassified";
    default:                      return "unknown";
    }
}

static unsigned long ast_ptr_hash(const AstNode* node) {
    uintptr_t value = (uintptr_t)node;
    value >>= 3;
    value ^= value >> 17;
    value *= (uintptr_t)0xed5ad4bbU;
    value ^= value >> 11;
    return (unsigned long)value;
}

static void ast_index_free_buffers(AstNode** nodes, AstNode** parents,
        AstFunctionId* owners, AstBindingId* node_bindings, NameScope** scopes,
        NameEntry** bindings, AstNode** classes, AstNodeFacts* facts) {
    free(nodes); free(parents); free(owners); free(node_bindings); free(scopes);
    free(bindings); free(classes); free(facts);
}

static bool ast_index_reserve(AstIndex* index, uint32_t needed) {
    if (needed <= index->capacity) return true;
    uint32_t capacity = index->capacity ? index->capacity : 256;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2) return false;
        capacity *= 2;
    }
    AstNode** nodes = (AstNode**)malloc(sizeof(AstNode*) * capacity);
    AstNode** parents = (AstNode**)malloc(sizeof(AstNode*) * capacity);
    AstFunctionId* owners = (AstFunctionId*)malloc(sizeof(AstFunctionId) * capacity);
    AstBindingId* node_bindings = (AstBindingId*)malloc(sizeof(AstBindingId) * capacity);
    NameScope** scopes = (NameScope**)malloc(sizeof(NameScope*) * capacity);
    NameEntry** bindings = (NameEntry**)malloc(sizeof(NameEntry*) * capacity);
    AstNode** classes = (AstNode**)malloc(sizeof(AstNode*) * capacity);
    AstNodeFacts* facts = (AstNodeFacts*)malloc(sizeof(AstNodeFacts) * capacity);
    if (!nodes || !parents || !owners || !node_bindings || !scopes ||
            !bindings || !classes || !facts) {
        ast_index_free_buffers(nodes, parents, owners, node_bindings, scopes,
            bindings, classes, facts);
        return false;
    }
    if (index->count) {
        memcpy(nodes, index->nodes, sizeof(AstNode*) * index->count);
        memcpy(parents, index->parents, sizeof(AstNode*) * index->count);
        memcpy(owners, index->owner_functions, sizeof(AstFunctionId) * index->count);
        memcpy(node_bindings, index->node_bindings,
            sizeof(AstBindingId) * index->count);
        memcpy(scopes, index->scopes, sizeof(NameScope*) * index->scope_count);
        memcpy(bindings, index->bindings, sizeof(NameEntry*) * index->binding_count);
        memcpy(classes, index->classes, sizeof(AstNode*) * index->class_count);
        memcpy(facts, index->facts, sizeof(AstNodeFacts) * index->count);
    }
    ast_index_free_buffers(index->nodes, index->parents, index->owner_functions,
        index->node_bindings, index->scopes, index->bindings, index->classes,
        index->facts);
    index->nodes = nodes;
    index->parents = parents;
    index->owner_functions = owners;
    index->node_bindings = node_bindings;
    index->scopes = scopes;
    index->bindings = bindings;
    index->classes = classes;
    index->facts = facts;
    index->capacity = capacity;
    return true;
}

static AstFunctionId ast_index_add_function(AstIndex* index, AstNode* node) {
    if (!index || !node) return AST_FUNCTION_ID_INVALID;
    if (index->function_count == index->function_capacity) {
        uint32_t capacity = index->function_capacity ? index->function_capacity * 2 : 32;
        if (capacity < index->function_count + 1) capacity = index->function_count + 1;
        AstNode** functions = (AstNode**)malloc(sizeof(AstNode*) * capacity);
        if (!functions) return AST_FUNCTION_ID_INVALID;
        if (index->function_count) {
            memcpy(functions, index->functions,
                sizeof(AstNode*) * index->function_count);
        }
        free(index->functions);
        index->functions = functions;
        index->function_capacity = capacity;
    }
    AstFunctionId id = index->function_count++;
    index->functions[id] = node;
    return id;
}

static bool ast_index_rehash(AstIndex* index, uint32_t capacity) {
    AstNode** slots = (AstNode**)calloc(capacity, sizeof(AstNode*));
    AstNodeId* slot_ids = (AstNodeId*)malloc(sizeof(AstNodeId) * capacity);
    if (!slots || !slot_ids) { free(slots); free(slot_ids); return false; }
    for (uint32_t i = 0; i < capacity; i++) slot_ids[i] = AST_NODE_ID_INVALID;
    for (uint32_t i = 0; i < index->count; i++) {
        AstNode* node = index->nodes[i];
        uint32_t slot = (uint32_t)(ast_ptr_hash(node) & (capacity - 1));
        while (slots[slot]) slot = (slot + 1) & (capacity - 1);
        slots[slot] = node;
        slot_ids[slot] = i;
    }
    free(index->slots);
    free(index->slot_ids);
    index->slots = slots;
    index->slot_ids = slot_ids;
    index->slot_capacity = capacity;
    return true;
}

bool ast_index_publish_scope(AstIndex* index, NameScope* scope) {
    if (!scope) return true;
    for (uint32_t i = 0; i < index->scope_count; i++) {
        if (index->scopes[i] == scope) { scope->scope_id = i; return true; }
    }
    if (index->scope_count >= index->capacity) return false;
    scope->scope_id = index->scope_count; index->scopes[index->scope_count++] = scope;
    return true;
}

static AstBindingId ast_index_publish_binding(AstIndex* index, NameEntry* entry) {
    if (!entry || !ast_index_publish_scope(index, entry->scope)) return AST_BINDING_ID_INVALID;
    for (uint32_t i = 0; i < index->binding_count; i++) {
        if (index->bindings[i] == entry) return i;
    }
    if (index->binding_count >= index->capacity) return AST_BINDING_ID_INVALID;
    AstBindingId id = index->binding_count++;
    index->bindings[id] = entry;
    return id;
}

static NameEntry* ast_index_node_entry(AstNode* node) {
    switch (node->node_type) {
    case AST_NODE_IDENT: return ((AstIdentNode*)node)->entry;
    case AST_NODE_PARAM: case AST_NODE_KEY_EXPR: case AST_NODE_NAMED_ARG: return ((AstNamedNode*)node)->entry;
    case AST_NODE_VARIABLE_DECLARATOR: return ((AstDeclaratorNode*)node)->entry;
    default:
        return NULL;
    }
}

static bool ast_index_publish_node(AstIndex* index, AstNode* node, AstNodeId id) {
    NameEntry* entry = ast_index_node_entry(node);
    if (entry) {
        AstBindingId binding_id = ast_index_publish_binding(index, entry);
        if (binding_id == AST_BINDING_ID_INVALID) return false;
        index->node_bindings[id] = binding_id;
    }
    switch (node->node_type) {
    case AST_SCRIPT:
        return ast_index_publish_scope(index, ((AstScript*)node)->global_vars);
    case AST_NODE_BLOCK:
        return ast_index_publish_scope(index, ((AstBlockNode*)node)->vars);
    case AST_NODE_LOOP:
        return ast_index_publish_scope(index, ((AstLoopControlNode*)node)->vars);
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_PROC:
    case AST_NODE_ARROW_FUNC: case AST_NODE_METHOD:
        return ast_index_publish_scope(index, ((AstFuncNode*)node)->vars);
    case AST_NODE_CLASS: case AST_NODE_CLASS_EXPR: {
        AstClassNode* cls = (AstClassNode*)node;
        for (uint32_t i = 0; i < index->class_count; i++) {
            if (index->classes[i] == node) { cls->class_id = i; return true; }
        }
        if (index->class_count >= index->capacity) return false;
        cls->class_id = index->class_count;
        index->classes[index->class_count++] = node;
        return true;
    }
    default:
        return true;
    }
}

static AstNodeId ast_index_add(AstIndex* index, AstNode* node, AstNode* parent,
        AstFunctionId owner) {
    if (!node) return AST_NODE_ID_INVALID;
    if (!index->slot_capacity && !ast_index_rehash(index, 256)) return AST_NODE_ID_INVALID;
    if (index->count * 2 >= index->slot_capacity &&
        !ast_index_rehash(index, index->slot_capacity * 2)) return AST_NODE_ID_INVALID;
    uint32_t slot = (uint32_t)(ast_ptr_hash(node) & (index->slot_capacity - 1));
    while (index->slots[slot]) {
        if (index->slots[slot] == node) {
            return ast_index_find(index, node);
        }
        slot = (slot + 1) & (index->slot_capacity - 1);
    }
    if (!ast_index_reserve(index, index->count + 1)) return AST_NODE_ID_INVALID;
    AstNodeId id = index->count++;
    index->nodes[id] = node;
    index->parents[id] = parent;
    index->owner_functions[id] = owner;
    index->node_bindings[id] = AST_BINDING_ID_INVALID;
    index->facts[id].declared_contract = node->type;
    index->facts[id].inferred_type = NULL;
    index->facts[id].representation = VALUE_REP_NONE;
    index->facts[id].flags = 0;
    index->facts[id].folded_item = ITEM_NULL;
    if (!ast_index_publish_node(index, node, id)) return AST_NODE_ID_INVALID;
    index->slots[slot] = node;
    index->slot_ids[slot] = id;
    return id;
}

typedef struct AstIndexWalk {
    AstIndex* index;
    AstFunctionId owner_function;
    const LangProfile* profile;
    bool failed;
} AstIndexWalk;

static void ast_index_visit(AstNode* child, AstNode* parent, void* opaque) {
    AstIndexWalk* walk = (AstIndexWalk*)opaque;
    if (!child || walk->failed) return;
    // `next` is an intrusive sibling link, not a lexical parent edge. The
    // child-table visitor presents it as a child of the previous sibling; use
    // that sibling's published parent so indexed scope projection remains
    // faithful to the AST ownership contract.
    AstFunctionId owner = walk->owner_function;
    if (parent && parent->next == child) {
        AstNodeId parent_id = ast_index_find(walk->index, parent);
        if (parent_id != AST_NODE_ID_INVALID) {
            parent = walk->index->parents[parent_id];
        }
    }
    // Shared AST fragments can be retained by a synthetic callable (for
    // example a class-field initializer). Revisit their descendants under the
    // new owner so identifier facts are projected into that callable as well.
    AstNodeId existing_id = ast_index_find(walk->index, child);
    if (existing_id != AST_NODE_ID_INVALID) {
        bool is_function = child->node_type == AST_NODE_FUNC ||
            child->node_type == AST_NODE_FUNC_EXPR || child->node_type == AST_NODE_PROC ||
            child->node_type == AST_NODE_ARROW_FUNC || child->node_type == AST_NODE_METHOD;
        AstFunctionId child_owner = is_function
            ? walk->index->owner_functions[existing_id] : walk->owner_function;
        if ((!is_function && walk->index->owner_functions[existing_id] == child_owner) ||
                (is_function && child_owner == walk->owner_function)) return;
        AstFunctionId previous_owner = walk->owner_function;
        if (!is_function) walk->index->owner_functions[existing_id] = child_owner;
        walk->owner_function = child_owner;
        ast_visit_core_children(child, ast_index_visit, walk);
        if (walk->profile && walk->profile->visit_ext_children) {
            walk->profile->visit_ext_children(child, ast_index_visit, walk);
        }
        walk->owner_function = previous_owner;
        return;
    }
    // Source spans recover the innermost function when a malformed or shared
    // list edge leaves the traversal owner stale. The structural owner remains
    // the fallback for synthetic nodes without a meaningful span.
    uint32_t best_span = UINT32_MAX;
    if (child->source_span.end_byte > child->source_span.start_byte) {
        for (AstFunctionId i = 0; i < walk->index->function_count; i++) {
            AstNode* function = walk->index->functions[i];
            if (!function || function->source_span.start_byte > child->source_span.start_byte ||
                    function->source_span.end_byte < child->source_span.end_byte) continue;
            uint32_t span = function->source_span.end_byte - function->source_span.start_byte;
            if (span < best_span) {
                best_span = span;
                owner = i;
            }
        }
    }
    AstNodeId id = ast_index_add(walk->index, child, parent, owner);
    if (id == AST_NODE_ID_INVALID) {
        walk->failed = true;
        return;
    }
    if (walk->profile && walk->profile->publish_ext_facts &&
            !walk->profile->publish_ext_facts(child, walk->index)) {
        walk->failed = true;
        return;
    }
    bool is_function = child->node_type == AST_NODE_FUNC ||
        child->node_type == AST_NODE_FUNC_EXPR ||
        child->node_type == AST_NODE_PROC ||
        child->node_type == AST_NODE_ARROW_FUNC ||
        child->node_type == AST_NODE_METHOD;
    AstFunctionId previous = walk->owner_function;
    if (is_function) {
        owner = ast_index_add_function(walk->index, child);
        if (owner == AST_FUNCTION_ID_INVALID) {
            walk->failed = true;
            return;
        }
        walk->index->owner_functions[id] = owner;
        walk->owner_function = owner;
    }
    ast_visit_core_children(child, ast_index_visit, walk);
    if (walk->profile && walk->profile->visit_ext_children) {
        walk->profile->visit_ext_children(child, ast_index_visit, walk);
    }
    walk->owner_function = previous;
}

static bool ast_index_walk_root(AstIndex* index, AstNode* root, AstNode* parent,
        const LangProfile* profile) {
    if (!root) return true;
    AstIndexWalk walk = {index, AST_FUNCTION_ID_INVALID, profile, false};
    AstNodeId id = ast_index_add(index, root, parent, walk.owner_function);
    if (id == AST_NODE_ID_INVALID) return false;
    if (profile && profile->publish_ext_facts &&
            !profile->publish_ext_facts(root, index)) return false;
    bool is_function = root->node_type == AST_NODE_FUNC ||
        root->node_type == AST_NODE_FUNC_EXPR || root->node_type == AST_NODE_PROC ||
        root->node_type == AST_NODE_ARROW_FUNC || root->node_type == AST_NODE_METHOD;
    if (is_function) {
        AstFunctionId function_id = ast_index_add_function(index, root);
        if (function_id == AST_FUNCTION_ID_INVALID) return false;
        index->owner_functions[id] = function_id;
        walk.owner_function = function_id;
    }
    ast_visit_core_children(root, ast_index_visit, &walk);
    if (profile && profile->visit_ext_children) {
        profile->visit_ext_children(root, ast_index_visit, &walk);
    }
    return !walk.failed;
}

void ast_visit_core_children(AstNode* node, AstChildVisitor visitor, void* ctx) {
    if (!node || !visitor) return;
#define AST_VISIT(field) do { if ((field)) visitor((AstNode*)(field), node, ctx); } while (0)
    switch (node->node_type) {
        case AST_SCRIPT: AST_VISIT(((AstScript*)node)->body); break;
        case AST_NODE_PRIMARY: AST_VISIT(((AstPrimaryNode*)node)->expr); break;
        case AST_NODE_UNARY: AST_VISIT(((AstUnaryNode*)node)->operand); break;
        case AST_NODE_SPREAD: AST_VISIT(((AstSpreadNode*)node)->argument); break;
        case AST_NODE_YIELD: case AST_NODE_AWAIT:
            AST_VISIT(((AstYieldNode*)node)->argument); break;
        case AST_NODE_BINARY: case AST_NODE_PIPE:
            AST_VISIT(((AstBinaryNode*)node)->left);
            AST_VISIT(((AstBinaryNode*)node)->right); break;
        case AST_NODE_BINARY_TYPE:
            AST_VISIT(((AstBinaryNode*)node)->left);
            AST_VISIT(((AstBinaryNode*)node)->right); break;
        case AST_NODE_UNARY_TYPE: AST_VISIT(((AstUnaryNode*)node)->operand); break;
        case AST_NODE_ASSIGN:
        case AST_NODE_ASSIGN_STAM:
        case AST_NODE_INDEX_ASSIGN_STAM:
        case AST_NODE_MEMBER_ASSIGN_STAM:
        case AST_NODE_ASSIGN_PATTERN:
            AST_VISIT(((AstAssignNode*)node)->left);
            AST_VISIT(((AstAssignNode*)node)->right); break;
        case AST_NODE_CALL_EXPR: case AST_NODE_NEW_EXPR:
            AST_VISIT(((AstCallNode*)node)->function);
            AST_VISIT(((AstCallNode*)node)->argument); break;
        case AST_NODE_MEMBER_EXPR: case AST_NODE_INDEX_EXPR:
            AST_VISIT(((AstFieldNode*)node)->object);
            AST_VISIT(((AstFieldNode*)node)->field); break;
        case AST_NODE_IF_EXPR: case AST_NODE_CONDITIONAL_EXPR:
            AST_VISIT(((AstIfNode*)node)->cond);
            AST_VISIT(((AstIfNode*)node)->then);
            AST_VISIT(((AstIfNode*)node)->otherwise); break;
        case AST_NODE_ARRAY: case AST_NODE_SEQ: case AST_NODE_CONTENT: case AST_NODE_CONTENT_TYPE:
        case AST_NODE_ARRAY_PATTERN:
            AST_VISIT(((AstArrayNode*)node)->item); break;
        case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL:
        case AST_NODE_MAP_PATTERN:
            AST_VISIT(((AstMapNode*)node)->item); break;
        case AST_NODE_REST_ELEMENT: case AST_NODE_REST_PROPERTY:
            AST_VISIT(((AstSpreadNode*)node)->argument); break;
        case AST_NODE_LIST_TYPE: case AST_NODE_ARRAY_TYPE:
            AST_VISIT(((AstArrayNode*)node)->item); break;
        case AST_NODE_MAP_TYPE: case AST_NODE_ELMT_TYPE:
            AST_VISIT(((AstMapNode*)node)->item); break;
        case AST_NODE_PROPERTY:
            AST_VISIT(((AstPropertyNode*)node)->key);
            AST_VISIT(((AstPropertyNode*)node)->value); break;
        case AST_NODE_KEY_EXPR: case AST_NODE_PARAM: case AST_NODE_NAMED_ARG:
        case AST_NODE_STRING_PATTERN: case AST_NODE_SYMBOL_PATTERN:
            AST_VISIT(((AstNamedNode*)node)->as); break;
        case AST_NODE_DECOMPOSE: AST_VISIT(((AstDecomposeNode*)node)->as); break;
        case AST_NODE_MATCH_EXPR:
            AST_VISIT(((AstMatchNode*)node)->scrutinee);
            AST_VISIT(((AstMatchNode*)node)->first_arm); break;
        case AST_NODE_MATCH_ARM:
            AST_VISIT(((AstMatchArm*)node)->pattern);
            AST_VISIT(((AstMatchArm*)node)->body); break;
        case AST_NODE_BLOCK: case AST_NODE_EXPR_STMT:
            if (node->node_type == AST_NODE_BLOCK) AST_VISIT(((AstBlockNode*)node)->statements);
            else AST_VISIT(((AstExprStmtNode*)node)->expression);
            break;
        case AST_NODE_LOOP: {
            AstLoopControlNode* loop = (AstLoopControlNode*)node;
            if (loop->form == LOOP_FORM_DO_WHILE) {
                AST_VISIT(loop->body); AST_VISIT(loop->cond);
            } else {
                AST_VISIT(loop->init); AST_VISIT(loop->test);
                AST_VISIT(loop->update); AST_VISIT(loop->body);
            }
            break;
        }
        case AST_NODE_RETURN_STAM: case AST_NODE_RAISE_STAM: case AST_NODE_RAISE_EXPR:
            AST_VISIT(((AstReturnNode*)node)->value); break;
        case AST_NODE_VAR_STAM: case AST_NODE_LET_STAM:
        case AST_NODE_PUB_STAM: case AST_NODE_TYPE_STAM:
            AST_VISIT(((AstVarDeclNode*)node)->declarations); break;
        case AST_NODE_VARIABLE_DECLARATOR:
            AST_VISIT(((AstDeclaratorNode*)node)->id); AST_VISIT(((AstDeclaratorNode*)node)->init); break;
        case AST_NODE_FOR_OF_STAM: case AST_NODE_FOR_IN_STAM:
            AST_VISIT(((AstForOfNode*)node)->left); AST_VISIT(((AstForOfNode*)node)->init);
            AST_VISIT(((AstForOfNode*)node)->right); AST_VISIT(((AstForOfNode*)node)->body); break;
        case AST_NODE_TRY_STAM:
            AST_VISIT(((AstTryNode*)node)->block); AST_VISIT(((AstTryNode*)node)->handler);
            AST_VISIT(((AstTryNode*)node)->finalizer); break;
        case AST_NODE_CATCH_CLAUSE:
            AST_VISIT(((AstCatchNode*)node)->param); AST_VISIT(((AstCatchNode*)node)->body); break;
        case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_PROC: case AST_NODE_ARROW_FUNC:
            AST_VISIT(((AstFuncNode*)node)->param); AST_VISIT(((AstFuncNode*)node)->body); break;
        case AST_NODE_HANDLER_EXPR: case AST_NODE_HANDLER_STAM:
            AST_VISIT(((AstHandlerNode*)node)->operand); AST_VISIT(((AstHandlerNode*)node)->body);
            AST_VISIT(((AstHandlerNode*)node)->value_body); break;
        case AST_NODE_START: AST_VISIT(((AstStartNode*)node)->call); break;
        case AST_NODE_METHOD:
            AST_VISIT(((AstMethodNode*)node)->key); AST_VISIT(((AstMethodNode*)node)->param);
            AST_VISIT(((AstMethodNode*)node)->body); break;
        case AST_NODE_CLASS: case AST_NODE_CLASS_EXPR:
            AST_VISIT(((AstClassNode*)node)->superclass); AST_VISIT(((AstClassNode*)node)->body); break;
        case AST_NODE_FIELD:
            AST_VISIT(((AstClassFieldNode*)node)->key); AST_VISIT(((AstClassFieldNode*)node)->value); break;
        case AST_NODE_IMPORT:
            AST_VISIT(((AstImportNode*)node)->specifiers); break;
        case AST_NODE_EXPORT:
            AST_VISIT(((AstExportDeclNode*)node)->declaration);
            AST_VISIT(((AstExportDeclNode*)node)->specifiers); break;
        case AST_NODE_IMPORT_SPECIFIER: case AST_NODE_EXPORT_SPECIFIER: case AST_NODE_IDENT:
        case AST_NODE_LITERAL: case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM:
        default: break;
    }
    AST_VISIT(node->next);
#undef AST_VISIT
}

bool ast_index_build_profile(AstIndex* index, AstNode* root, const LangProfile* profile) {
    if (!index) return false;
    ast_index_destroy(index);
    return ast_index_walk_root(index, root, NULL, profile);
}

extern "C" int ast_index_compiler_pass(void* opaque) {
    AstIndexPassContext* pass = (AstIndexPassContext*)opaque;
    return pass && pass->index && pass->root && ast_index_build_profile(
        pass->index, pass->root, pass->profile);
}

bool ast_index_append_profile(AstIndex* index, AstNode* root, AstNode* parent,
        const LangProfile* profile) {
    if (!index || !root) return false;
    // A fragment can share a declaration node with an earlier AST edge. Do
    // not rebuild the table: its IDs/facts are the identity held by promoted
    // definitions and by the P3 const pass.
    if (ast_index_find(index, root) != AST_NODE_ID_INVALID) return true;
    return ast_index_walk_root(index, root, parent, profile);
}

void ast_index_destroy(AstIndex* index) {
    if (!index) return;
    ast_index_free_buffers(index->nodes, index->parents, index->owner_functions,
        index->node_bindings, index->scopes, index->bindings, index->classes,
        index->facts);
    free(index->functions);
    free(index->slots); free(index->slot_ids);
    memset(index, 0, sizeof(*index));
}

AstNodeId ast_index_find(const AstIndex* index, const AstNode* node) {
    if (!index || !node || !index->slot_capacity) return AST_NODE_ID_INVALID;
    uint32_t slot = (uint32_t)(ast_ptr_hash(node) & (index->slot_capacity - 1));
    while (index->slots[slot]) {
        if (index->slots[slot] == node) {
            return index->slot_ids[slot];
        }
        slot = (slot + 1) & (index->slot_capacity - 1);
    }
    return AST_NODE_ID_INVALID;
}

AstBindingId ast_index_binding_id(const AstIndex* index, const AstNode* node) {
    AstNodeId node_id = ast_index_find(index, node);
    return index && node_id < index->count ? index->node_bindings[node_id] : AST_BINDING_ID_INVALID;
}

NameEntry* ast_index_binding(const AstIndex* index, AstBindingId id) { return index && id < index->binding_count ? index->bindings[id] : NULL; }

AstNode* ast_index_binding_definition(const AstIndex* index, AstBindingId id) {
    if (!index || id >= index->binding_count) return NULL;
    return ast_index_find(index, index->bindings[id]->node) == AST_NODE_ID_INVALID ? NULL : index->bindings[id]->node;
}
