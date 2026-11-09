#include "transpiler.hpp"
#include "ast_build.hpp"
#include "lambda-number-types.hpp"

// RC15: materializing a const value needs only a pool to build into and the
// source text for literals that still recover their value from a span. Keeping
// that explicit lets both the compiler pass and MIR lowering share one builder
// instead of the back end owning a private copy.
typedef struct ConstMaterializeCtx {
    Pool* pool;
    const char* source;
} ConstMaterializeCtx;

static bool static_const_item_from_node(const ConstMaterializeCtx* cx,
        AstNode* node, Item* out);

static bool static_store_field_value(void* field_ptr, TypeId field_type, Item value) {
    if (!field_ptr) return false;
    TypeId value_type = get_type_id(value);
    switch (field_type) {
    case LMD_TYPE_NULL:
        if (value_type == LMD_TYPE_NULL) { *(void**)field_ptr = NULL; return true; }
        *(Item*)field_ptr = value; return true;
    case LMD_TYPE_BOOL:
        if (value_type != LMD_TYPE_BOOL) return false;
        *(bool*)field_ptr = value.bool_val; return true;
    case LMD_TYPE_INT:
        if (value_type != LMD_TYPE_INT) return false;
        // Static maps must use the same i64 carrier as runtime map writes.
        // Leaving a precomputed double here makes a later lane read decode its
        // IEEE bits as an int sentinel or an unrelated integer.
        *(int64_t*)field_ptr = lambda_int_item_to_lane(value.item); return true;
    case LMD_TYPE_INT64:
        if (value_type == LMD_TYPE_INT) { *(int64_t*)field_ptr = lambda_int_item_to_i64(value); return true; }
        if (value_type == LMD_TYPE_INT64) { *(int64_t*)field_ptr = value.get_int64(); return true; }
        return false;
    case LMD_TYPE_FLOAT:
        if (value_type == LMD_TYPE_FLOAT) { *(double*)field_ptr = value.get_double(); return true; }
        // a float field is a double slot: an int value WIDENS into it
        if (value_type == LMD_TYPE_INT) { *(double*)field_ptr = lambda_int_item_value(value); return true; }
        if (value_type == LMD_TYPE_INT64) { *(double*)field_ptr = (double)value.get_int64(); return true; }
        if (value_type == LMD_TYPE_UINT64) { *(double*)field_ptr = (double)value.get_uint64(); return true; }
        return false;
    case LMD_TYPE_DTIME:
        if (value_type != LMD_TYPE_DTIME) return false;
        *(DateTime**)field_ptr = value.get_datetime_ptr(); return true;
    case LMD_TYPE_STRING:
        if (value_type != LMD_TYPE_STRING) return false;
        *(String**)field_ptr = value.get_safe_string(); return true;
    case LMD_TYPE_SYMBOL:
        if (value_type != LMD_TYPE_SYMBOL) return false;
        *(Symbol**)field_ptr = value.get_safe_symbol(); return true;
    case LMD_TYPE_BINARY:
        if (value_type != LMD_TYPE_BINARY) return false;
        *(Binary**)field_ptr = value.get_safe_binary(); return true;
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
        if (!is_container_type_id(value_type)) return false;
        *(Container**)field_ptr = value.container; return true;
    case LMD_TYPE_DECIMAL:
        if (value_type != LMD_TYPE_DECIMAL) return false;
        *(Decimal**)field_ptr = value.get_decimal();
        return true;
    case LMD_TYPE_TYPE:
        if (value_type != LMD_TYPE_TYPE) return false;
        *(Type**)field_ptr = value.type;
        return true;
    case LMD_TYPE_FUNC:
        if (value_type != LMD_TYPE_FUNC) return false;
        *(Function**)field_ptr = value.function;
        return true;
    case LMD_TYPE_PATH:
        if (value_type != LMD_TYPE_PATH) return false;
        *(Path**)field_ptr = value.path;
        return true;
    case LMD_TYPE_UINT64:
        if (field_type != value_type) return false;
        *(uint64_t*)field_ptr = value.get_uint64();
        return true;
    case LMD_TYPE_NUM_SIZED:
        if (value_type != LMD_TYPE_NUM_SIZED) return false;
        *(uint64_t*)field_ptr = value.item; return true;
    case LMD_TYPE_ANY: {
        TypedItem titem;
        memset(&titem, 0, sizeof(titem));
        titem.type_id = value_type;
        titem.item = value.item;
        switch (value_type) {
        case LMD_TYPE_NULL: case LMD_TYPE_UNDEFINED: case LMD_TYPE_ERROR: break;
        case LMD_TYPE_BOOL: titem.bool_val = value.bool_val; break;
        // C16: carry the numeric value; an int Item's payload is not its value.
        case LMD_TYPE_INT: titem.long_val = lambda_int_item_to_lane(value.item); break;
        case LMD_TYPE_INT64: titem.long_val = value.get_int64(); break;
        case LMD_TYPE_FLOAT: titem.double_val = value.get_double(); break;
        case LMD_TYPE_DTIME: titem.datetime_ptr = value.get_datetime_ptr(); break;
        case LMD_TYPE_UINT64: titem.uint64_val = value.get_uint64(); break;
        case LMD_TYPE_STRING: titem.string = value.get_safe_string(); break;
        case LMD_TYPE_SYMBOL: titem.symbol = value.get_safe_symbol(); break;
        case LMD_TYPE_BINARY: titem.binary = value.get_safe_binary(); break;
        case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_RANGE:
        case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT: 
            titem.container = value.container; break;
        case LMD_TYPE_DECIMAL: titem.decimal = value.get_decimal(); break;
        case LMD_TYPE_TYPE: titem.type = value.type; break;
        case LMD_TYPE_FUNC: titem.function = value.function; break;
        case LMD_TYPE_PATH: titem.path = value.path; break;
        default: return false;
        }
        *(TypedItem*)field_ptr = titem;
        return true;
    }
    default:
        return false;
    }
}

static bool static_const_array_from_node(const ConstMaterializeCtx* cx, AstArrayNode* arr_node, Item* out) {
    if (!cx || !arr_node || !out || !cx->pool) return false;
    TypeArray* arr_type = (TypeArray*)arr_node->type;
    if (arr_type && arr_type->nested && arr_type->nested->type_id == LMD_TYPE_BOOL) return false;
    int64_t count = 0;
    for (AstNode* item = arr_node->item; item; item = item->next) {
        if (item->node_type == AST_NODE_FOR_EXPR || item->node_type == AST_NODE_SPREAD ||
            item->node_type == AST_NODE_PIPE || item->node_type == AST_NODE_VARIABLE_DECLARATOR) return false;
        AstNode* value = item;
        if (value->node_type == AST_NODE_PRIMARY) {
            AstPrimaryNode* pri = (AstPrimaryNode*)value;
            if (pri->expr) value = pri->expr;
        }
        if (value->node_type == AST_NODE_ARRAY) return false;
        count++;
    }
    Array* arr = (Array*)pool_calloc(cx->pool, sizeof(Array));
    if (!arr) return false;
    arr->type_id = LMD_TYPE_ARRAY;
    arr->is_static = 1;
    arr->is_immortal = 1;
    arr->length = count;
    arr->capacity = count;
    if (count > 0) {
        arr->items = (Item*)pool_calloc(cx->pool, sizeof(Item) * count);
        if (!arr->items) return false;
    }
    int64_t index = 0;
    for (AstNode* item = arr_node->item; item; item = item->next) {
        Item value = ItemNull;
        if (!static_const_item_from_node(cx, item, &value)) return false;
        arr->items[index++] = value;
    }
    out->item = (uint64_t)(uintptr_t)arr;
    return true;
}

static bool static_const_map_from_node(const ConstMaterializeCtx* cx, AstMapNode* map_node, Item* out) {
    if (!cx || !map_node || map_node->has_computed_key || !map_node->type || !out ||
            !cx->pool) return false;
    TypeMap* map_type = (TypeMap*)map_node->type;
    Map* map = (Map*)pool_calloc(cx->pool, sizeof(Map));
    if (!map) return false;
    map->type_id = LMD_TYPE_MAP;
    map->is_static = 1;
    map->is_immortal = 1;
    map->map_kind = MAP_KIND_PLAIN;
    map->type = map_type;
    map->data_cap = (int)map_type->byte_size;
    if (map_type->byte_size > 0) {
        map->data = pool_calloc(cx->pool, (size_t)map_type->byte_size);
        if (!map->data) return false;
    }
    AstNode* item = map_node->item;
    ShapeEntry* field = map_type->shape;
    while (item && field) {
        if (item->node_type != AST_NODE_KEY_EXPR || !field->name) return false;
        AstNamedNode* key_expr = (AstNamedNode*)item;
        Item value = ItemNull;
        if (key_expr->as && !static_const_item_from_node(cx, key_expr->as, &value)) return false;
        void* field_ptr = (char*)map->data + field->byte_offset;
        if (!map_shape_field_store_native_lane(field_ptr, field, value) &&
                !static_store_field_value(field_ptr, shape_entry_storage_type_id(field), value)) {
            return false;
        }
        item = item->next;
        field = typemap_next_field(map_type, field);
    }
    if (item || field) return false;
    out->item = (uint64_t)(uintptr_t)map;
    return true;
}

static bool static_const_item_from_node(const ConstMaterializeCtx* cx, AstNode* node, Item* out) {
    if (!cx || !node || !out) return false;
    if (node->node_type == AST_NODE_PRIMARY) {
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        if (pri->expr) return static_const_item_from_node(cx, pri->expr, out);
        if (!node->type || !node->type->is_literal) return false;
        if (static_literal_item_from_type(node->type, out)) return true;
        switch (node->type->type_id) {
        case LMD_TYPE_BOOL: out->item = b2it(parse_bool_literal_span(cx->source,
            node->source_span)); return true;
        case LMD_TYPE_INT: out->item = i2it(parse_int_literal_span(cx->source,
            node->source_span)); return true;
        case LMD_TYPE_DTIME: return false;
        // A complex literal must be heap-allocated and therefore cannot be embedded
        // in an Input-backed static collection constant.
        case LMD_TYPE_COMPLEX: return false;
        default: return false;
        }
    }
    if (node->node_type == AST_NODE_ARRAY) return static_const_array_from_node(cx, (AstArrayNode*)node, out);
    if (node->node_type == AST_NODE_MAP) return static_const_map_from_node(cx, (AstMapNode*)node, out);
    return false;
}

// RC15/RC-P5a: a const container is a value the source already determines, so
// it is *materialized* into the unit's pool -- never evaluated. This runs as its
// own pass, before const-fold, so the folder's input stays expressions only and
// both tiers see one container instead of each building its own.
bool lambda_const_materialize_script(Transpiler* tp) {
    if (!tp || !tp->ast_index.nodes || !tp->pool || !tp->const_list) {
        return true;
    }
    ConstMaterializeCtx cx = {tp->pool, tp->source};
    for (uint32_t id = 0; id < tp->ast_index.count; id++) {
        AstNode* node = tp->ast_index.nodes[id];
        if (!node) continue;
        if (node->node_type != AST_NODE_ARRAY && node->node_type != AST_NODE_MAP) {
            continue;
        }
        if (node->const_kind != AST_CONST_NONE) continue;
        Item value = ItemNull;
        if (!static_const_item_from_node(&cx, node, &value)) continue;
        TypeId tid = get_type_id(value);
        if (tid != LMD_TYPE_ARRAY && tid != LMD_TYPE_MAP) continue;
        if (!arraylist_append(tp->const_list, value.container)) continue;
        node->const_index = (uint32_t)(tp->const_list->length - 1);
        node->const_kind = AST_CONST_POOLED;
    }
    return true;
}

