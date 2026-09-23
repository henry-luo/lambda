#include "ast-core.hpp"

#include <stdlib.h>
#include <string.h>
#include "../../lib/mempool.h"
#include "../../lib/log.h"

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
    case ANY_FORCE:               return "force";
    case ANY_ADDRESS_OF:          return "address_of";
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
        AstFunctionId* owners, AstNodeId* subtree_end, AstBindingId* node_bindings, AstNodeId* first_children,
        AstNodeId* next_siblings, NameScope** scopes, NameEntry** bindings,
        AstNode** classes) {
    free(nodes); free(parents); free(owners); free(subtree_end); free(node_bindings); free(first_children);
    free(next_siblings); free(scopes);
    free(bindings); free(classes);
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
    AstNodeId* subtree_end = (AstNodeId*)malloc(sizeof(AstNodeId) * capacity);
    AstBindingId* node_bindings = (AstBindingId*)malloc(sizeof(AstBindingId) * capacity);
    AstNodeId* first_children = (AstNodeId*)malloc(sizeof(AstNodeId) * capacity);
    AstNodeId* next_siblings = (AstNodeId*)malloc(sizeof(AstNodeId) * capacity);
    NameScope** scopes = (NameScope**)malloc(sizeof(NameScope*) * capacity);
    NameEntry** bindings = (NameEntry**)malloc(sizeof(NameEntry*) * capacity);
    AstNode** classes = (AstNode**)malloc(sizeof(AstNode*) * capacity);
    if (!nodes || !parents || !owners || !subtree_end || !node_bindings || !first_children ||
            !next_siblings || !scopes ||
            !bindings || !classes) {
        ast_index_free_buffers(nodes, parents, owners, subtree_end, node_bindings, first_children,
            next_siblings, scopes, bindings, classes);
        return false;
    }
    if (index->count) {
        memcpy(nodes, index->nodes, sizeof(AstNode*) * index->count);
        memcpy(parents, index->parents, sizeof(AstNode*) * index->count);
        memcpy(owners, index->owner_functions, sizeof(AstFunctionId) * index->count);
        memcpy(subtree_end, index->subtree_end, sizeof(AstNodeId) * index->count);
        memcpy(node_bindings, index->node_bindings,
            sizeof(AstBindingId) * index->count);
        memcpy(first_children, index->first_children,
            sizeof(AstNodeId) * index->count);
        memcpy(next_siblings, index->next_siblings,
            sizeof(AstNodeId) * index->count);
        memcpy(scopes, index->scopes, sizeof(NameScope*) * index->scope_count);
        memcpy(bindings, index->bindings, sizeof(NameEntry*) * index->binding_count);
        memcpy(classes, index->classes, sizeof(AstNode*) * index->class_count);
    }
    ast_index_free_buffers(index->nodes, index->parents, index->owner_functions,
        index->subtree_end,
        index->node_bindings, index->first_children, index->next_siblings,
        index->scopes, index->bindings, index->classes);
    index->nodes = nodes;
    index->parents = parents;
    index->owner_functions = owners;
    index->subtree_end = subtree_end;
    index->node_bindings = node_bindings;
    index->first_children = first_children;
    index->next_siblings = next_siblings;
    index->scopes = scopes;
    index->bindings = bindings;
    index->classes = classes;
    index->capacity = capacity;
    return true;
}

static AstFunctionId ast_index_add_function(AstIndex* index, AstNode* node,
        AstFunctionId parent_function) {
    if (!index || !node) return AST_FUNCTION_ID_INVALID;
    if (index->function_count == index->function_capacity) {
        uint32_t capacity = index->function_capacity ? index->function_capacity * 2 : 32;
        if (capacity < index->function_count + 1) capacity = index->function_count + 1;
        AstFunctionIndexEntry* functions = (AstFunctionIndexEntry*)malloc(
            sizeof(AstFunctionIndexEntry) * capacity);
        if (!functions) return AST_FUNCTION_ID_INVALID;
        if (index->function_count) {
            memcpy(functions, index->functions,
                sizeof(AstFunctionIndexEntry) * index->function_count);
        }
        free(index->functions);
        index->functions = functions;
        index->function_capacity = capacity;
    }
    AstFunctionId id = index->function_count++;
    index->functions[id] = {node, parent_function, AST_NODE_ID_INVALID,
        AST_NODE_ID_INVALID};
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
    case AST_NODE_PARAM: case AST_NODE_KEY_EXPR: case AST_NODE_NAMED_ARG:
    case AST_NODE_FOR_INDEX: return ((AstNamedNode*)node)->entry;
    case AST_NODE_VARIABLE_DECLARATOR: return ((AstDeclaratorNode*)node)->entry;
    // A Lambda assignment carries its resolved target directly. Publishing it
    // makes a captured write a first-class index fact even without a target
    // identifier child.
    case AST_NODE_ASSIGN_STAM: return ((AstAssignStamNode*)node)->target_entry;
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
    index->subtree_end[id] = id + 1;
    index->node_bindings[id] = AST_BINDING_ID_INVALID;
    index->first_children[id] = AST_NODE_ID_INVALID;
    index->next_siblings[id] = AST_NODE_ID_INVALID;
    AstNodeId parent_id = ast_index_find(index, parent);
    if (parent_id != AST_NODE_ID_INVALID) {
        index->next_siblings[id] = index->first_children[parent_id];
        index->first_children[parent_id] = id;
    }
    if (!ast_index_publish_node(index, node, id)) return AST_NODE_ID_INVALID;
    index->slots[slot] = node;
    index->slot_ids[slot] = id;
    return id;
}

bool ast_index_note_allocation(AstIndex* index, AstNode* node) {
    // Lambda reductions construct children before parents. Reserve storage now
    // but assign the dense identity only during the preorder bind walk, when
    // structural ranges can keep their D8.2.4 invariant.
    return index && node && ast_index_reserve(index, index->count + 1);
}

bool ast_index_prepare_binding(AstIndex* index) {
    if (!index) return false;
    // A rebind receives the AST in structural preorder, unlike the reduction
    // allocator. Discard any prior dense ordering before publishing the new
    // graph so `[id, subtree_end)` remains a true preorder range.
    index->count = 0;
    index->scope_count = 0;
    index->binding_count = 0;
    index->class_count = 0;
    index->function_count = 0;
    index->const_folded_count = 0;
    if (index->slots) memset(index->slots, 0,
        sizeof(AstNode*) * index->slot_capacity);
    if (index->slot_ids) {
        for (uint32_t i = 0; i < index->slot_capacity; i++) {
            index->slot_ids[i] = AST_NODE_ID_INVALID;
        }
    }
    free(index->binding_use_offsets); index->binding_use_offsets = NULL;
    free(index->binding_uses); index->binding_uses = NULL;
    free(index->callee_call_offsets); index->callee_call_offsets = NULL;
    free(index->callee_calls); index->callee_calls = NULL;
    free(index->object_member_offsets); index->object_member_offsets = NULL;
    free(index->object_member_uses); index->object_member_uses = NULL;
    free(index->function_child_offsets); index->function_child_offsets = NULL;
    free(index->function_children); index->function_children = NULL;
    free(index->function_reference_offsets); index->function_reference_offsets = NULL;
    free(index->function_references); index->function_references = NULL;
    free(index->function_overlay_offsets); index->function_overlay_offsets = NULL;
    free(index->function_overlay_nodes); index->function_overlay_nodes = NULL;
    free(index->profile_support); index->profile_support = NULL;
    index->profile_support_visitor = NULL;
    index->profile_support_scanned = false;
    index->profile_supported = false;
    index->graph_published = false;
    return true;
}

bool ast_index_bind_node(AstIndex* index, AstNode* node, AstNode* parent,
        AstFunctionId current_owner, AstFunctionId* node_owner) {
    if (node_owner) *node_owner = current_owner;
    if (!index || !node) return false;
    AstNodeId id = ast_index_find(index, node);
    if (id == AST_NODE_ID_INVALID) {
        id = ast_index_add(index, node, NULL, AST_FUNCTION_ID_INVALID);
        if (id == AST_NODE_ID_INVALID) return false;
    }
    // `next` is intrusive rather than structural. The direct binder visits it
    // through the preceding sibling, so restore the shared structural parent.
    if (parent && parent->next == node) {
        AstNodeId preceding_id = ast_index_find(index, parent);
        parent = preceding_id != AST_NODE_ID_INVALID ? index->parents[preceding_id] : NULL;
        AstNodeId structural_parent_id = ast_index_find(index, parent);
        current_owner = structural_parent_id != AST_NODE_ID_INVALID
            ? index->owner_functions[structural_parent_id]
            : AST_FUNCTION_ID_INVALID;
    }
    // A sibling follows the preceding node through its intrusive `next` link.
    // Return the recovered lexical owner so its descendants do not inherit a
    // preceding function's index context.
    if (node_owner) *node_owner = current_owner;
    index->parents[id] = parent;
    index->owner_functions[id] = current_owner;
    AstNodeId parent_id = ast_index_find(index, parent);
    if (parent_id != AST_NODE_ID_INVALID) {
        index->next_siblings[id] = index->first_children[parent_id];
        index->first_children[parent_id] = id;
    }
    if (!ast_index_publish_node(index, node, id)) return false;
    if (!ast_index_node_is_function(node)) return true;
    AstFunctionId function_id = ast_index_add_function(index, node,
        current_owner);
    if (function_id == AST_FUNCTION_ID_INVALID) return false;
    index->owner_functions[id] = function_id;
    if (node_owner) *node_owner = function_id;
    return true;
}

static void ast_index_free_queries(AstIndex* index) {
    if (!index) return;
    free(index->binding_use_offsets); index->binding_use_offsets = NULL;
    free(index->binding_uses); index->binding_uses = NULL;
    free(index->callee_call_offsets); index->callee_call_offsets = NULL;
    free(index->callee_calls); index->callee_calls = NULL;
    free(index->object_member_offsets); index->object_member_offsets = NULL;
    free(index->object_member_uses); index->object_member_uses = NULL;
    free(index->function_child_offsets); index->function_child_offsets = NULL;
    free(index->function_children); index->function_children = NULL;
    free(index->function_reference_offsets); index->function_reference_offsets = NULL;
    free(index->function_references); index->function_references = NULL;
    free(index->function_overlay_offsets); index->function_overlay_offsets = NULL;
    free(index->function_overlay_nodes); index->function_overlay_nodes = NULL;
    free(index->profile_support); index->profile_support = NULL;
    index->profile_support_visitor = NULL;
    index->profile_support_scanned = false;
    index->profile_supported = false;
}

static bool ast_index_build_query_slice(uint32_t key_count,
        const uint32_t* counts, uint32_t** offsets_out, AstNodeId** ids_out,
        const AstIndex* index, int kind) {
    uint32_t* offsets = (uint32_t*)calloc((size_t)key_count + 1,
        sizeof(uint32_t));
    if (!offsets) return false;
    for (uint32_t i = 0; i < key_count; i++) offsets[i + 1] = counts[i];
    for (uint32_t i = 1; i <= key_count; i++) offsets[i] += offsets[i - 1];
    AstNodeId* ids = offsets[key_count]
        ? (AstNodeId*)malloc(sizeof(AstNodeId) * offsets[key_count]) : NULL;
    uint32_t* write = key_count ? (uint32_t*)malloc(sizeof(uint32_t) * key_count) : NULL;
    if ((offsets[key_count] && !ids) || (key_count && !write)) {
        free(offsets); free(ids); free(write); return false;
    }
    if (key_count) memcpy(write, offsets, sizeof(uint32_t) * key_count);
    for (AstNodeId node_id = 0; node_id < index->count; node_id++) {
        AstNode* node = index->nodes[node_id];
        uint32_t key = UINT32_MAX;
        if (kind == 0) {
            key = index->node_bindings[node_id];
        } else if (kind == 1 && node &&
                (node->node_type == AST_NODE_CALL_EXPR ||
                 node->node_type == AST_NODE_NEW_EXPR)) {
            AstNode* callee = ast_unwrap_primary(((AstCallNode*)node)->function);
            if (callee && callee->node_type == AST_NODE_IDENT) {
                AstNodeId callee_id = ast_index_find(index, callee);
                if (callee_id != AST_NODE_ID_INVALID) key = index->node_bindings[callee_id];
            }
        } else if (kind == 2 && node && node->node_type == AST_NODE_MEMBER_EXPR) {
            key = ast_index_find(index, ((AstFieldNode*)node)->object);
        }
        if (key < key_count) ids[write[key]++] = node_id;
    }
    free(write);
    *offsets_out = offsets;
    *ids_out = ids;
    return true;
}

static bool ast_index_function_reference_is_free(const AstIndex* index,
        AstFunctionId function_id, NameEntry* entry) {
    if (!index || !entry || function_id >= index->function_count) return false;
    AstNode* function = index->functions[function_id].node;
    NameScope* function_scope = function ? ((AstFuncNode*)function)->vars : NULL;
    if (!function_scope || !entry->scope) return false;
    for (NameScope* scope = entry->scope; scope; scope = scope->parent) {
        if (scope == function_scope) return false;
    }
    return true;
}

static bool ast_index_function_reference_for_node(const AstIndex* index,
        AstNodeId node_id, bool write, AstFunctionReference* reference) {
    if (!index || !reference || node_id >= index->count) return false;
    AstNode* node = index->nodes[node_id];
    AstFunctionId owner = index->owner_functions[node_id];
    if (!node || owner >= index->function_count) return false;
    AstBindingId binding_id = AST_BINDING_ID_INVALID;
    uint8_t flags = 0;
    if (!write && node->node_type == AST_NODE_IDENT) {
        binding_id = index->node_bindings[node_id];
        flags = AST_FUNCTION_REF_READ;
    } else if (write && node->node_type == AST_NODE_ASSIGN_STAM) {
        binding_id = index->node_bindings[node_id];
        flags = AST_FUNCTION_REF_WRITE;
    } else if (write && node->node_type == AST_NODE_ASSIGN) {
        // JavaScript keeps the assignment target as its left child rather
        // than duplicating Lambda's target_entry field on the shared shape.
        AstNode* target = ast_unwrap_primary(((AstAssignNode*)node)->left);
        AstNodeId target_id = ast_index_find(index, target);
        binding_id = target_id < index->count ? index->node_bindings[target_id] :
            AST_BINDING_ID_INVALID;
        flags = AST_FUNCTION_REF_WRITE;
    } else if (write && node->node_type == AST_NODE_UNARY &&
            (((AstUnaryNode*)node)->op == OPERATOR_JS_INCREMENT ||
             ((AstUnaryNode*)node)->op == OPERATOR_JS_DECREMENT)) {
        AstNode* target = ast_unwrap_primary(((AstUnaryNode*)node)->operand);
        AstNodeId target_id = ast_index_find(index, target);
        binding_id = target_id < index->count ? index->node_bindings[target_id] :
            AST_BINDING_ID_INVALID;
        flags = AST_FUNCTION_REF_WRITE;
    } else if (write && (node->node_type == AST_NODE_INDEX_ASSIGN_STAM ||
            node->node_type == AST_NODE_MEMBER_ASSIGN_STAM)) {
        AstIdentNode* root = ast_compound_root_ident(
            ((AstCompoundAssignNode*)node)->object);
        AstNodeId root_id = ast_index_find(index, (AstNode*)root);
        binding_id = root_id < index->count ? index->node_bindings[root_id] :
            AST_BINDING_ID_INVALID;
        flags = AST_FUNCTION_REF_WRITE;
    }
    if (binding_id >= index->binding_count) return false;
    NameEntry* entry = index->bindings[binding_id];
    if (!entry) return false;
    if (ast_index_function_reference_is_free(index, owner, entry)) {
        flags |= AST_FUNCTION_REF_FREE;
    }
    reference->node_id = node_id;
    reference->binding_id = binding_id;
    reference->flags = flags;
    return true;
}

static bool ast_index_build_function_references(AstIndex* index,
        uint32_t* counts) {
    // A script without functions owns an empty CSR slice. Its count array is
    // intentionally null, just like the other zero-key reverse tables.
    if (!index || (index->function_count && !counts)) return false;
    index->function_reference_offsets = (uint32_t*)calloc(
        (size_t)index->function_count + 1, sizeof(uint32_t));
    if (!index->function_reference_offsets) return false;
    for (AstFunctionId i = 0; i < index->function_count; i++) {
        index->function_reference_offsets[i + 1] = counts[i];
    }
    for (AstFunctionId i = 1; i <= index->function_count; i++) {
        index->function_reference_offsets[i] +=
            index->function_reference_offsets[i - 1];
    }
    uint32_t total = index->function_reference_offsets[index->function_count];
    index->function_references = total ? (AstFunctionReference*)malloc(
        sizeof(AstFunctionReference) * total) : NULL;
    uint32_t* write = index->function_count ? (uint32_t*)malloc(
        sizeof(uint32_t) * index->function_count) : NULL;
    if ((total && !index->function_references) ||
            (index->function_count && !write)) {
        free(write);
        return false;
    }
    if (index->function_count) memcpy(write, index->function_reference_offsets,
        sizeof(uint32_t) * index->function_count);
    for (AstNodeId node_id = 0; node_id < index->count; node_id++) {
        AstFunctionReference reference = {};
        if (ast_index_function_reference_for_node(index, node_id, false, &reference)) {
            AstFunctionId owner = index->owner_functions[node_id];
            index->function_references[write[owner]++] = reference;
        }
        if (ast_index_function_reference_for_node(index, node_id, true, &reference)) {
            AstFunctionId owner = index->owner_functions[node_id];
            index->function_references[write[owner]++] = reference;
        }
    }
    free(write);
    return true;
}

bool ast_index_rebuild_queries(AstIndex* index) {
    if (!index) return false;
    ast_index_free_queries(index);
    for (AstNodeId i = 0; i < index->count; i++) index->subtree_end[i] = i + 1;
    // Parent IDs precede children. Folding the exclusive end upward in reverse
    // therefore derives every structural range without a second AST walk.
    for (AstNodeId i = index->count; i-- > 0;) {
        AstNodeId parent_id = ast_index_parent_id(index, i);
        if (parent_id != AST_NODE_ID_INVALID &&
                index->subtree_end[parent_id] < index->subtree_end[i]) {
            index->subtree_end[parent_id] = index->subtree_end[i];
        }
    }
    for (AstFunctionId i = 0; i < index->function_count; i++) {
        AstNodeId node_id = ast_index_find(index, index->functions[i].node);
        index->functions[i].first_node = node_id;
        index->functions[i].end_node = node_id < index->count
            ? index->subtree_end[node_id] : AST_NODE_ID_INVALID;
    }

    uint32_t* binding_counts = index->binding_count
        ? (uint32_t*)calloc(index->binding_count, sizeof(uint32_t)) : NULL;
    uint32_t* call_counts = index->binding_count
        ? (uint32_t*)calloc(index->binding_count, sizeof(uint32_t)) : NULL;
    uint32_t* member_counts = index->count
        ? (uint32_t*)calloc(index->count, sizeof(uint32_t)) : NULL;
    uint32_t* child_counts = (uint32_t*)calloc((size_t)index->function_count + 1,
        sizeof(uint32_t));
    uint32_t* overlay_counts = index->function_count
        ? (uint32_t*)calloc(index->function_count, sizeof(uint32_t)) : NULL;
    uint32_t* reference_counts = index->function_count
        ? (uint32_t*)calloc(index->function_count, sizeof(uint32_t)) : NULL;
    if ((index->binding_count && (!binding_counts || !call_counts)) ||
            (index->count && !member_counts) || !child_counts ||
            (index->function_count && (!overlay_counts || !reference_counts))) {
        free(binding_counts); free(call_counts); free(member_counts); free(child_counts);
        free(overlay_counts); free(reference_counts);
        return false;
    }
    for (AstNodeId i = 0; i < index->count; i++) {
        AstBindingId binding_id = index->node_bindings[i];
        if (binding_id < index->binding_count) binding_counts[binding_id]++;
        AstNode* node = index->nodes[i];
        if (node && (node->node_type == AST_NODE_CALL_EXPR ||
                node->node_type == AST_NODE_NEW_EXPR)) {
            AstNode* callee = ast_unwrap_primary(((AstCallNode*)node)->function);
            AstNodeId callee_id = callee ? ast_index_find(index, callee) : AST_NODE_ID_INVALID;
            AstBindingId callee_binding = callee_id < index->count
                ? index->node_bindings[callee_id] : AST_BINDING_ID_INVALID;
            if (callee_binding < index->binding_count) call_counts[callee_binding]++;
        }
        if (node && node->node_type == AST_NODE_MEMBER_EXPR) {
            AstNodeId object_id = ast_index_find(index, ((AstFieldNode*)node)->object);
            if (object_id < index->count) member_counts[object_id]++;
        }
        AstFunctionId owner = index->owner_functions[i];
        if (owner < index->function_count &&
                !ast_index_node_descends(index, i,
                    index->functions[owner].first_node)) {
            overlay_counts[owner]++;
        }
        AstFunctionReference reference = {};
        if (ast_index_function_reference_for_node(index, i, false, &reference)) {
            reference_counts[owner]++;
        }
        if (ast_index_function_reference_for_node(index, i, true, &reference)) {
            reference_counts[owner]++;
        }
    }
    for (AstFunctionId i = 0; i < index->function_count; i++) {
        AstFunctionId parent = index->functions[i].parent;
        uint32_t bucket = parent < index->function_count ? parent : index->function_count;
        child_counts[bucket]++;
    }
    bool ok = ast_index_build_query_slice(index->binding_count, binding_counts,
            &index->binding_use_offsets, &index->binding_uses, index, 0) &&
        ast_index_build_query_slice(index->binding_count, call_counts,
            &index->callee_call_offsets, &index->callee_calls, index, 1) &&
        ast_index_build_query_slice(index->count, member_counts,
            &index->object_member_offsets, &index->object_member_uses, index, 2);
    if (ok) {
        index->function_child_offsets = (uint32_t*)calloc(
            (size_t)index->function_count + 2, sizeof(uint32_t));
        if (!index->function_child_offsets) ok = false;
    }
    if (ok) {
        for (AstFunctionId i = 0; i <= index->function_count; i++)
            index->function_child_offsets[i + 1] = child_counts[i];
        for (AstFunctionId i = 1; i <= index->function_count + 1; i++)
            index->function_child_offsets[i] += index->function_child_offsets[i - 1];
        uint32_t total = index->function_child_offsets[index->function_count + 1];
        index->function_children = total ? (AstFunctionId*)malloc(
            sizeof(AstFunctionId) * total) : NULL;
        uint32_t* write = (uint32_t*)malloc(
            sizeof(uint32_t) * ((size_t)index->function_count + 1));
        if ((total && !index->function_children) || !write) {
            free(write); ok = false;
        } else {
            memcpy(write, index->function_child_offsets,
                sizeof(uint32_t) * ((size_t)index->function_count + 1));
            for (AstFunctionId i = 0; i < index->function_count; i++) {
                AstFunctionId parent = index->functions[i].parent;
                uint32_t bucket = parent < index->function_count
                    ? parent : index->function_count;
                index->function_children[write[bucket]++] = i;
            }
            free(write);
        }
    }
    if (ok) {
        ok = ast_index_build_function_references(index, reference_counts);
    }
    if (ok) {
        index->function_overlay_offsets = (uint32_t*)calloc(
            (size_t)index->function_count + 1, sizeof(uint32_t));
        if (!index->function_overlay_offsets) ok = false;
    }
    if (ok) {
        for (AstFunctionId i = 0; i < index->function_count; i++)
            index->function_overlay_offsets[i + 1] = overlay_counts[i];
        for (AstFunctionId i = 1; i <= index->function_count; i++)
            index->function_overlay_offsets[i] += index->function_overlay_offsets[i - 1];
        uint32_t total = index->function_overlay_offsets[index->function_count];
        index->function_overlay_nodes = total ? (AstNodeId*)malloc(
            sizeof(AstNodeId) * total) : NULL;
        uint32_t* write = index->function_count ? (uint32_t*)malloc(
            sizeof(uint32_t) * index->function_count) : NULL;
        if ((total && !index->function_overlay_nodes) ||
                (index->function_count && !write)) {
            free(write); ok = false;
        } else {
            if (index->function_count) memcpy(write, index->function_overlay_offsets,
                sizeof(uint32_t) * index->function_count);
            for (AstNodeId i = 0; i < index->count; i++) {
                AstFunctionId owner = index->owner_functions[i];
                if (owner < index->function_count &&
                        !ast_index_node_descends(index, i,
                            index->functions[owner].first_node)) {
                    index->function_overlay_nodes[write[owner]++] = i;
                }
            }
            free(write);
        }
    }
    free(binding_counts); free(call_counts); free(member_counts); free(child_counts);
    free(overlay_counts); free(reference_counts);
    if (!ok) ast_index_free_queries(index);
    return ok;
}

void ast_index_mark_published(AstIndex* index) {
    if (!index) return;
    if (!ast_index_rebuild_queries(index)) {
        log_error("ast-index: failed to build immutable query tables");
        return;
    }
    index->graph_published = true;
}

bool ast_index_node_is_function(const AstNode* node) {
    return node && (node->node_type == AST_NODE_FUNC ||
        node->node_type == AST_NODE_FUNC_EXPR || node->node_type == AST_NODE_PROC ||
        node->node_type == AST_NODE_ARROW_FUNC || node->node_type == AST_NODE_METHOD);
}

static AstFunctionId ast_index_parent_function(const AstIndex* index,
        const AstNode* parent) {
    // A node's ownership label may come from source-span recovery, which is
    // not a lexical edge for sibling class members. Publish the nearest
    // structural function ancestor instead.
    for (const AstNode* current = parent; current;) {
        AstNodeId current_id = ast_index_find(index, current);
        if (current_id == AST_NODE_ID_INVALID) return AST_FUNCTION_ID_INVALID;
        if (ast_index_node_is_function(current)) {
            return index->owner_functions[current_id];
        }
        current = index->parents[current_id];
    }
    return AST_FUNCTION_ID_INVALID;
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
    bool sibling_edge = false;
    if (parent && parent->next == child) {
        sibling_edge = true;
        AstNodeId parent_id = ast_index_find(walk->index, parent);
        if (parent_id != AST_NODE_ID_INVALID) {
            AstFunctionId sibling_owner = walk->index->owner_functions[parent_id];
            parent = walk->index->parents[parent_id];
            // A sibling inherits the structural parent's function owner, not
            // the preceding sibling's function. A shared fragment can be
            // projected into a synthetic callable, however; retain that
            // projection after its first sibling has adopted the walk owner.
            owner = ast_index_parent_function(walk->index, parent);
            AstNode* sibling_function = sibling_owner < walk->index->function_count
                ? walk->index->functions[sibling_owner].node : NULL;
            bool shared_function_fragment = sibling_function &&
                child->source_span.start_byte >= sibling_function->source_span.start_byte &&
                child->source_span.end_byte <= sibling_function->source_span.end_byte;
            if (sibling_owner == walk->owner_function && shared_function_fragment) {
                owner = sibling_owner;
            }
        }
    }
    // Shared AST fragments can be retained by a synthetic callable (for
    // example a class-field initializer). Revisit their descendants under the
    // new owner so identifier facts are projected into that callable as well.
    AstNodeId existing_id = ast_index_find(walk->index, child);
    if (existing_id != AST_NODE_ID_INVALID) {
        bool is_function = ast_index_node_is_function(child);
        AstFunctionId child_owner = is_function
            ? walk->index->owner_functions[existing_id]
            : (sibling_edge ? owner : walk->owner_function);
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
    AstNodeId structural_parent_id = ast_index_find(walk->index, parent);
    uint32_t best_span = UINT32_MAX;
    // Normal core/profile edges have an exact structural owner. Only an
    // orphaned malformed/shared edge needs source-span recovery (LC4.11).
    if (structural_parent_id == AST_NODE_ID_INVALID &&
            child->source_span.end_byte > child->source_span.start_byte) {
        for (AstFunctionId i = 0; i < walk->index->function_count; i++) {
            AstNode* function = walk->index->functions[i].node;
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
    bool is_function = ast_index_node_is_function(child);
    AstFunctionId previous = walk->owner_function;
    if (is_function) {
        owner = ast_index_add_function(walk->index, child,
            ast_index_parent_function(walk->index, parent));
        if (owner == AST_FUNCTION_ID_INVALID) {
            walk->failed = true;
            return;
        }
        walk->index->owner_functions[id] = owner;
        walk->owner_function = owner;
    } else if (sibling_edge) {
        // Keep a sibling's descendants out of the preceding function's walk.
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
    AstFunctionId parent_function = ast_index_parent_function(index, parent);
    AstIndexWalk walk = {index, parent_function, profile, false};
    AstNodeId id = ast_index_add(index, root, parent, walk.owner_function);
    if (id == AST_NODE_ID_INVALID) return false;
    if (profile && profile->publish_ext_facts &&
            !profile->publish_ext_facts(root, index)) return false;
    bool is_function = ast_index_node_is_function(root);
    if (is_function) {
        AstFunctionId function_id = ast_index_add_function(index, root,
            parent_function);
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
        // Tier 3 (PTH60v3). Every child is an ordinary expression evaluated at
        // the statement, so the generic walk must reach them or the frame
        // planner never allocates storage for the bindings they read.
        case AST_NODE_CRUD_STAM:
            // For CRUD_OP_SEQUENCE `value` is the clause chain, which the
            // generic walk follows through `next` like any other child list.
            AST_VISIT(((AstCrudNode*)node)->object);
            AST_VISIT(((AstCrudNode*)node)->key);
            AST_VISIT(((AstCrudNode*)node)->value); break;
        case AST_NODE_OPEN_STAM:
            AST_VISIT(((AstOpenNode*)node)->target);
            AST_VISIT(((AstOpenNode*)node)->alias_decl);
            AST_VISIT(((AstOpenNode*)node)->body); break;
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
        case AST_NODE_KEY_EXPR: {
            AstNamedNode* named = (AstNamedNode*)node;
            AST_VISIT(named->key);
            AST_VISIT(named->as); break;
        }
        case AST_NODE_PARAM: case AST_NODE_NAMED_ARG: case AST_NODE_FOR_INDEX:
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

typedef bool (*AstBindingChildAction)(AstNode* child, void* context);

static bool ast_apply_binding_pattern_children(AstNode* node,
        AstBindingChildAction action, void* context) {
    if (!node || !action) return false;
    AstNode* child = NULL;
    switch (node->node_type) {
    case AST_NODE_ARRAY_PATTERN:
    case AST_NODE_ARRAY:
        child = ((AstArrayNode*)node)->item;
        break;
    case AST_NODE_MAP_PATTERN:
    case AST_NODE_MAP:
        child = ((AstMapNode*)node)->item;
        break;
    case AST_NODE_PROPERTY:
        child = ((AstPropertyNode*)node)->value;
        break;
    case AST_NODE_ASSIGN_PATTERN:
        child = ((AstAssignNode*)node)->left;
        break;
    case AST_NODE_REST_ELEMENT:
    case AST_NODE_REST_PROPERTY:
    case AST_NODE_SPREAD:
        child = ((AstSpreadNode*)node)->argument;
        break;
    default:
        return false;
    }
    for (; child; child = child->next) {
        if (action(child, context)) return true;
    }
    return false;
}

struct AstBindingChildVisit {
    AstChildVisitor visitor;
    AstNode* parent;
    void* context;
};

static bool ast_visit_binding_pattern_child(AstNode* child, void* opaque) {
    AstBindingChildVisit* visit = (AstBindingChildVisit*)opaque;
    visit->visitor(child, visit->parent, visit->context);
    return false;
}

void ast_visit_binding_pattern_children(AstNode* node, AstChildVisitor visitor,
        void* ctx) {
    if (!visitor) return;
    AstBindingChildVisit visit = {visitor, node, ctx};
    ast_apply_binding_pattern_children(node, ast_visit_binding_pattern_child,
        &visit);
}

struct AstBindingChildSearch {
    AstBindingChildPredicate predicate;
    void* context;
    bool found;
};

static bool ast_find_binding_pattern_child(AstNode* child, void* opaque) {
    AstBindingChildSearch* search = (AstBindingChildSearch*)opaque;
    search->found = search->predicate(child, search->context);
    return search->found;
}

bool ast_any_binding_pattern_child(AstNode* node,
        AstBindingChildPredicate predicate, void* ctx) {
    if (!predicate) return false;
    AstBindingChildSearch search = {predicate, ctx, false};
    ast_apply_binding_pattern_children(node, ast_find_binding_pattern_child,
        &search);
    return search.found;
}

bool ast_index_build_profile(AstIndex* index, AstNode* root, const LangProfile* profile) {
    if (!index) return false;
    ast_index_destroy(index);
    bool built = ast_index_walk_root(index, root, NULL, profile);
    if (built) ast_index_mark_published(index);
    // publication also constructs the query tables required by later passes.
    return built && index->graph_published;
}

bool ast_index_clone(AstIndex* destination, const AstIndex* source) {
    if (!destination || !source) return false;
    ast_index_destroy(destination);
    if (source->count && !ast_index_reserve(destination, source->count)) return false;
    destination->count = source->count;
    destination->scope_count = source->scope_count;
    destination->binding_count = source->binding_count;
    destination->class_count = source->class_count;
    destination->graph_published = source->graph_published;
    destination->const_folded_count = source->const_folded_count;
    if (source->count) {
        memcpy(destination->nodes, source->nodes, source->count * sizeof(AstNode*));
        memcpy(destination->parents, source->parents, source->count * sizeof(AstNode*));
        memcpy(destination->owner_functions, source->owner_functions,
            source->count * sizeof(AstFunctionId));
        memcpy(destination->subtree_end, source->subtree_end,
            source->count * sizeof(AstNodeId));
        memcpy(destination->node_bindings, source->node_bindings,
            source->count * sizeof(AstBindingId));
        memcpy(destination->first_children, source->first_children,
            source->count * sizeof(AstNodeId));
        memcpy(destination->next_siblings, source->next_siblings,
            source->count * sizeof(AstNodeId));
    }
    if (source->scope_count) memcpy(destination->scopes, source->scopes,
        source->scope_count * sizeof(NameScope*));
    if (source->binding_count) memcpy(destination->bindings, source->bindings,
        source->binding_count * sizeof(NameEntry*));
    if (source->class_count) memcpy(destination->classes, source->classes,
        source->class_count * sizeof(AstNode*));
    if (source->function_count) {
        destination->functions = (AstFunctionIndexEntry*)malloc(
            source->function_count * sizeof(AstFunctionIndexEntry));
        if (!destination->functions) {
            ast_index_destroy(destination);
            return false;
        }
        memcpy(destination->functions, source->functions,
            source->function_count * sizeof(AstFunctionIndexEntry));
        destination->function_count = source->function_count;
        destination->function_capacity = source->function_count;
    }
    if (destination->count && !ast_index_rehash(destination,
            source->slot_capacity ? source->slot_capacity : 256)) {
        ast_index_destroy(destination);
        return false;
    }
    if (!ast_index_rebuild_queries(destination)) {
        ast_index_destroy(destination);
        return false;
    }
    return true;
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
    bool appended = ast_index_walk_root(index, root, parent, profile);
    if (appended) ast_index_mark_published(index);
    return appended && index->graph_published;
}

void ast_index_destroy(AstIndex* index) {
    if (!index) return;
    ast_index_free_buffers(index->nodes, index->parents, index->owner_functions,
        index->subtree_end, index->node_bindings, index->first_children, index->next_siblings,
        index->scopes, index->bindings, index->classes);
    free(index->functions);
    ast_index_free_queries(index);
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

AstNodeId ast_index_parent_id(const AstIndex* index, AstNodeId node_id) {
    if (!index || node_id >= index->count) return AST_NODE_ID_INVALID;
    AstNode* parent = index->parents[node_id];
    return parent ? ast_index_find(index, parent) : AST_NODE_ID_INVALID;
}

bool ast_index_visit_subtree(const AstIndex* index, AstNodeId root_id,
        AstIndexSubtreeVisitor visitor, void* context) {
    if (!index || !visitor || root_id == AST_NODE_ID_INVALID ||
            root_id >= index->count) return false;
    AstNodeId end = ast_index_subtree_end(index, root_id);
    if (end == AST_NODE_ID_INVALID) return false;
    for (AstNodeId node_id = root_id; node_id < end; node_id++) {
        if (!visitor(index, node_id, context)) return false;
    }
    return true;
}

AstNodeId ast_index_subtree_end(const AstIndex* index, AstNodeId root_id) {
    if (!index || root_id == AST_NODE_ID_INVALID || root_id >= index->count ||
            !index->subtree_end) return AST_NODE_ID_INVALID;
    AstNodeId end = index->subtree_end[root_id];
    return end > root_id && end <= index->count ? end : AST_NODE_ID_INVALID;
}

bool ast_index_node_descends(const AstIndex* index, AstNodeId node_id,
        AstNodeId ancestor_id) {
    AstNodeId end = ast_index_subtree_end(index, ancestor_id);
    return end != AST_NODE_ID_INVALID && node_id >= ancestor_id && node_id < end;
}

static const AstNodeId* ast_index_query_slice(const uint32_t* offsets,
        const AstNodeId* ids, uint32_t key_count, uint32_t key,
        uint32_t* count) {
    if (count) *count = 0;
    if (!offsets || key >= key_count) return NULL;
    uint32_t first = offsets[key];
    uint32_t end = offsets[key + 1];
    if (count) *count = end - first;
    return ids ? ids + first : NULL;
}

const AstNodeId* ast_index_binding_uses(const AstIndex* index,
        AstBindingId binding_id, uint32_t* count) {
    return index ? ast_index_query_slice(index->binding_use_offsets,
        index->binding_uses, index->binding_count, binding_id, count) : NULL;
}

const AstNodeId* ast_index_callee_calls(const AstIndex* index,
        AstBindingId binding_id, uint32_t* count) {
    return index ? ast_index_query_slice(index->callee_call_offsets,
        index->callee_calls, index->binding_count, binding_id, count) : NULL;
}

const AstNodeId* ast_index_object_member_uses(const AstIndex* index,
        AstNodeId object_id, uint32_t* count) {
    return index ? ast_index_query_slice(index->object_member_offsets,
        index->object_member_uses, index->count, object_id, count) : NULL;
}

const AstFunctionId* ast_index_function_children(const AstIndex* index,
        AstFunctionId function_id, uint32_t* count) {
    if (count) *count = 0;
    if (!index || !index->function_child_offsets) return NULL;
    uint32_t bucket = function_id < index->function_count ? function_id :
        function_id == AST_FUNCTION_ID_INVALID ? index->function_count : UINT32_MAX;
    if (bucket == UINT32_MAX) return NULL;
    uint32_t first = index->function_child_offsets[bucket];
    uint32_t end = index->function_child_offsets[bucket + 1];
    if (count) *count = end - first;
    return index->function_children ? index->function_children + first : NULL;
}

const AstFunctionReference* ast_index_function_references(const AstIndex* index,
        AstFunctionId function_id, uint32_t* count) {
    if (count) *count = 0;
    if (!index || function_id >= index->function_count ||
            !index->function_reference_offsets) return NULL;
    uint32_t first = index->function_reference_offsets[function_id];
    uint32_t end = index->function_reference_offsets[function_id + 1];
    if (count) *count = end - first;
    return index->function_references ? index->function_references + first : NULL;
}

bool ast_index_scan_profile_support(AstIndex* index,
        AstIndexProfileVisitor visitor, void* context) {
    if (!index || !visitor || !index->graph_published) return false;
    if (index->profile_support_scanned &&
            index->profile_support_visitor == visitor) {
        return index->profile_supported;
    }
    free(index->profile_support);
    index->profile_support = index->count ? (uint8_t*)calloc(index->count,
        sizeof(uint8_t)) : NULL;
    if (index->count && !index->profile_support) return false;
    index->profile_support_visitor = visitor;
    index->profile_support_scanned = true;
    index->profile_supported = true;
    for (AstNodeId node_id = 0; node_id < index->count;) {
        AstIndexProfileSupport support = visitor(index, node_id, context);
        index->profile_support[node_id] = (uint8_t)support;
        if (support == AST_INDEX_PROFILE_REJECT) {
            index->profile_supported = false;
            return false;
        }
        if (support == AST_INDEX_PROFILE_SKIP_SUBTREE) {
            AstNodeId end = ast_index_subtree_end(index, node_id);
            if (end > node_id) {
                node_id = end;
                continue;
            }
        }
        node_id++;
    }
    return true;
}

const AstNodeId* ast_index_function_overlay_nodes(const AstIndex* index,
        AstFunctionId function_id, uint32_t* count) {
    if (count) *count = 0;
    if (!index || function_id >= index->function_count ||
            !index->function_overlay_offsets) return NULL;
    uint32_t first = index->function_overlay_offsets[function_id];
    uint32_t end = index->function_overlay_offsets[function_id + 1];
    if (count) *count = end - first;
    return index->function_overlay_nodes ? index->function_overlay_nodes + first : NULL;
}

static uint32_t name_scope_pointer_hash(const String* name) {
    uintptr_t value = (uintptr_t)name;
    value ^= value >> 33;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 33;
    return (uint32_t)value;
}

NameEntry* name_scope_lookup_name(const NameScope* scope, const String* name) {
    if (!scope || !name) return NULL;
    if (scope->name_index && scope->name_index_capacity) {
        uint32_t slot = name_scope_pointer_hash(name) &
            (scope->name_index_capacity - 1);
        for (;;) {
            NameEntry* entry = scope->name_index[slot];
            if (!entry) return NULL;
            if (entry->name == name) return entry;
            slot = (slot + 1) & (scope->name_index_capacity - 1);
        }
    }
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (entry->name == name) return entry;
    }
    return NULL;
}

static bool name_scope_index_insert(NameScope* scope, NameEntry* entry) {
    if (!scope || !entry || !entry->name || !scope->name_index ||
            !scope->name_index_capacity) return true;
    uint32_t slot = name_scope_pointer_hash(entry->name) &
        (scope->name_index_capacity - 1);
    for (;;) {
        NameEntry* current = scope->name_index[slot];
        if (!current) {
            scope->name_index[slot] = entry;
            scope->name_index_count++;
            return true;
        }
        // Recovery retains duplicate declarations in the ordered list, while
        // normal lookup continues to select the first binding.
        if (current->name == entry->name) return true;
        slot = (slot + 1) & (scope->name_index_capacity - 1);
    }
}

static uint32_t name_scope_index_capacity_for_entries(uint32_t entry_count) {
    uint32_t capacity = 16;
    while (entry_count * 4 >= capacity * 3) capacity *= 2;
    return capacity;
}

static bool name_scope_index_rebuild(Pool* pool, NameScope* scope,
        uint32_t capacity) {
    if (!pool || !scope || capacity < 16 || (capacity & (capacity - 1))) return false;
    NameEntry** entries = (NameEntry**)pool_calloc(pool,
        (size_t)capacity * sizeof(NameEntry*));
    if (!entries) return false;
    scope->name_index = entries;
    scope->name_index_capacity = capacity;
    scope->name_index_count = 0;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (!name_scope_index_insert(scope, entry)) return false;
    }
    return true;
}

bool name_scope_index_add(Pool* pool, NameScope* scope, NameEntry* entry) {
    if (!scope || !entry) return false;
    scope->entry_count++;
    if (!scope->name_index && scope->entry_count > 8) {
        if (!name_scope_index_rebuild(pool, scope,
                name_scope_index_capacity_for_entries(scope->entry_count))) return false;
    }
    if (scope->name_index && (scope->name_index_count + 1) * 4 >=
            scope->name_index_capacity * 3) {
        if (!name_scope_index_rebuild(pool, scope,
                scope->name_index_capacity * 2)) return false;
    }
    return name_scope_index_insert(scope, entry);
}

bool name_scope_plan_binding_slots(NameScope* scope) {
    if (!scope) return true;
    if (scope->binding_slots_planned) return true;
    uint32_t count = 0;
    for (NameEntry* entry = scope->first; entry; entry = entry->next) {
        if (count >= (uint32_t)INT32_MAX) return false;
        entry->slot = (int32_t)count++;
        entry->storage_assigned = true;
    }
    scope->binding_slot_count = count;
    scope->binding_slots_planned = true;
    return true;
}

AstClassId ast_index_nearest_class(const AstIndex* index, AstNodeId node_id,
        bool include_node) {
    if (!include_node) node_id = ast_index_parent_id(index, node_id);
    while (index && node_id < index->count) {
        AstNode* node = index->nodes[node_id];
        if (node && (node->node_type == AST_NODE_CLASS ||
                node->node_type == AST_NODE_CLASS_EXPR)) {
            AstClassId class_id = ((AstClassNode*)node)->class_id;
            return class_id < index->class_count ? class_id : AST_CLASS_ID_INVALID;
        }
        node_id = ast_index_parent_id(index, node_id);
    }
    return AST_CLASS_ID_INVALID;
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
