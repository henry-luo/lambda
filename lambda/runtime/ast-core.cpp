#include "ast-core.hpp"

#include <stdlib.h>
#include <string.h>

static unsigned long ast_ptr_hash(const AstNode* node) {
    uintptr_t value = (uintptr_t)node;
    value >>= 3;
    value ^= value >> 17;
    value *= (uintptr_t)0xed5ad4bbU;
    value ^= value >> 11;
    return (unsigned long)value;
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
    AstNodeFacts* facts = (AstNodeFacts*)malloc(sizeof(AstNodeFacts) * capacity);
    if (!nodes || !parents || !owners || !facts) {
        free(nodes);
        free(parents);
        free(owners);
        free(facts);
        return false;
    }
    if (index->count) {
        memcpy(nodes, index->nodes, sizeof(AstNode*) * index->count);
        memcpy(parents, index->parents, sizeof(AstNode*) * index->count);
        memcpy(owners, index->owner_functions, sizeof(AstFunctionId) * index->count);
        memcpy(facts, index->facts, sizeof(AstNodeFacts) * index->count);
    }
    free(index->nodes);
    free(index->parents);
    free(index->owner_functions);
    free(index->facts);
    index->nodes = nodes;
    index->parents = parents;
    index->owner_functions = owners;
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
    index->facts[id].declared_contract = node->type;
    index->facts[id].inferred_type = NULL;
    index->facts[id].representation = VALUE_REP_NONE;
    index->facts[id].flags = 0;
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
    // AST list links and shared declaration nodes can be reached through more
    // than one field; do not descend through an already indexed node.
    if (ast_index_find(walk->index, child) != AST_NODE_ID_INVALID) return;
    AstFunctionId owner = walk->owner_function;
    AstNodeId id = ast_index_add(walk->index, child, parent, owner);
    if (id == AST_NODE_ID_INVALID) {
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

void ast_visit_core_children(AstNode* node, AstChildVisitor visitor, void* ctx) {
    if (!node || !visitor) return;
#define AST_VISIT(field) do { if ((field)) visitor((AstNode*)(field), node, ctx); } while (0)
    switch (node->node_type) {
        case AST_SCRIPT: AST_VISIT(((AstScript*)node)->body); break;
        case AST_NODE_PRIMARY: AST_VISIT(((AstPrimaryNode*)node)->expr); break;
        case AST_NODE_UNARY: AST_VISIT(((AstUnaryNode*)node)->operand); break;
        case AST_NODE_SPREAD: AST_VISIT(((AstSpreadNode*)node)->argument); break;
        case AST_NODE_BINARY: case AST_NODE_PIPE:
            AST_VISIT(((AstBinaryNode*)node)->left);
            AST_VISIT(((AstBinaryNode*)node)->right); break;
        case AST_NODE_ASSIGN:
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
        case AST_NODE_ARRAY: case AST_NODE_LIST: case AST_NODE_SEQ:
            AST_VISIT(((AstArrayNode*)node)->item); break;
        case AST_NODE_MAP: case AST_NODE_OBJECT_LITERAL:
            AST_VISIT(((AstMapNode*)node)->item); break;
        case AST_NODE_PROPERTY:
            AST_VISIT(((AstPropertyNode*)node)->key);
            AST_VISIT(((AstPropertyNode*)node)->value); break;
        case AST_NODE_KEY_EXPR: case AST_NODE_PARAM:
            AST_VISIT(((AstNamedNode*)node)->as); break;
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
        case AST_NODE_FOR_STAM:
            AST_VISIT(((AstForStmtNode*)node)->init); AST_VISIT(((AstForStmtNode*)node)->test);
            AST_VISIT(((AstForStmtNode*)node)->update); AST_VISIT(((AstForStmtNode*)node)->body); break;
        case AST_NODE_WHILE_STAM: case AST_NODE_DO_WHILE_STAM:
            AST_VISIT(((AstWhileNode*)node)->cond); AST_VISIT(((AstWhileNode*)node)->body); break;
        case AST_NODE_RETURN_STAM: case AST_NODE_RAISE_STAM: case AST_NODE_RAISE_EXPR:
            AST_VISIT(((AstReturnNode*)node)->value); break;
        case AST_NODE_VAR_STAM: AST_VISIT(((AstVarDeclNode*)node)->declarations); break;
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
    if (!root) return true;
    AstIndexWalk walk = {index, AST_FUNCTION_ID_INVALID, profile, false};
    AstNodeId id = ast_index_add(index, root, NULL, walk.owner_function);
    if (id == AST_NODE_ID_INVALID) return false;
    if (root->node_type == AST_NODE_FUNC || root->node_type == AST_NODE_FUNC_EXPR ||
        root->node_type == AST_NODE_PROC || root->node_type == AST_NODE_ARROW_FUNC ||
        root->node_type == AST_NODE_METHOD) {
        AstFunctionId function_id = ast_index_add_function(index, root);
        if (function_id == AST_FUNCTION_ID_INVALID) return false;
        index->owner_functions[id] = function_id;
        walk.owner_function = index->owner_functions[id];
    }
    ast_visit_core_children(root, ast_index_visit, &walk);
    if (profile && profile->visit_ext_children) {
        profile->visit_ext_children(root, ast_index_visit, &walk);
    }
    return !walk.failed;
}

bool ast_index_build(AstIndex* index, AstNode* root) {
    return ast_index_build_profile(index, root, NULL);
}

void ast_index_destroy(AstIndex* index) {
    if (!index) return;
    free(index->nodes); free(index->parents); free(index->owner_functions);
    free(index->functions); free(index->facts);
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
