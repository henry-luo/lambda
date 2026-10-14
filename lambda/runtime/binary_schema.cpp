#include "binary_schema.hpp"
#include "heap_api.h"
#include "lambda-error.h"
#include "gc/gc_heap.h"
#include "../../lib/mem.h"
#include "../../lib/log.h"

struct BinarySchemaOwner {
    size_t references;
    uint64_t generation;
    bool valid;
    Pool* bindings;
};

struct BinarySchemaView {
    const BinarySchema* schema;
    const void* record;
    BinarySchemaOwner* owner;
    uint64_t generation;
    Item binary;
    size_t offset;
    BinarySchemaEndian endian;
};

static Item binary_schema_error(const char* message) {
    log_error("BINARY_SCHEMA_ACCESS: %s", message);
    return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, message, nullptr));
}

BinarySchemaOwner* binary_schema_owner_create() {
    BinarySchemaOwner* owner = (BinarySchemaOwner*)mem_calloc(
        1, sizeof(BinarySchemaOwner), MEM_CAT_CONTAINER);
    if (owner) {
        owner->references = 1;
        owner->generation = 1;
        owner->valid = true;
    }
    return owner;
}

void binary_schema_owner_invalidate(BinarySchemaOwner* owner) {
    if (!owner || !owner->valid) return;
    owner->valid = false;
    owner->generation++;
}

void binary_schema_owner_release(BinarySchemaOwner* owner) {
    if (owner && --owner->references == 0) {
        if (owner->bindings) pool_destroy(owner->bindings);
        mem_free(owner);
    }
}

static const BinarySchema* binary_schema_bind_native_depth(BinarySchemaOwner* owner,
        const BinarySchema* declared, const BinarySchema* native, Item* error, size_t depth) {
    if (!owner || !owner->valid || !declared || !native || !declared->binding ||
            !native->binding || declared->endian != BINARY_SCHEMA_NATIVE ||
            (!declared->fields && declared->field_count) || (!native->fields && native->field_count) ||
            declared->field_count > SIZE_MAX / sizeof(BinarySchemaField) ||
            strcmp(declared->binding, native->binding) != 0 || depth >= 64) {
        *error = binary_schema_error("native binary schema binding or nesting is incompatible");
        return nullptr;
    }
    BinarySchema* bound = (BinarySchema*)pool_calloc(owner->bindings, sizeof(BinarySchema));
    BinarySchemaField* fields = (BinarySchemaField*)pool_calloc(owner->bindings,
        declared->field_count * sizeof(BinarySchemaField));
    if (!bound || (!fields && declared->field_count)) {
        *error = binary_schema_error("cannot allocate native binary schema binding");
        return nullptr;
    }
    *bound = *declared;
    bound->fields = fields;
    bound->byte_size = native->byte_size;
    bound->endian = BINARY_SCHEMA_NATIVE;
    bound->identity = declared;
    for (size_t i = 0; i < declared->field_count; i++) {
        const BinarySchemaField& wanted = declared->fields[i];
        const BinarySchemaField* found = nullptr;
        for (size_t j = 0; j < native->field_count; j++) {
            if (strcmp(wanted.name, native->fields[j].name) == 0) {
                found = &native->fields[j]; break;
            }
        }
        if (!found || !found->locate || found->kind != wanted.kind ||
                (wanted.kind != BINARY_FIELD_RECORD && found->storage_size != wanted.storage_size)) {
            *error = binary_schema_error("native binary schema field type or width is incompatible");
            return nullptr;
        }
        if (wanted.kind == BINARY_FIELD_ITEM && wanted.logical_type && wanted.logical_type != &TYPE_ANY &&
                wanted.logical_type != found->logical_type) {
            *error = binary_schema_error("native boxed field logical type is incompatible");
            return nullptr;
        }
        fields[i] = *found;
        fields[i].nullable = wanted.nullable;
        fields[i].logical_type = wanted.logical_type;
        if (wanted.target) {
            fields[i].target = binary_schema_bind_native_depth(owner, wanted.target,
                found->target, error, depth + 1);
            if (!fields[i].target) return nullptr;
        }
    }
    return bound;
}

const BinarySchema* binary_schema_bind_native(BinarySchemaOwner* owner,
        const BinarySchema* declared, const BinarySchema* native, Item* error) {
    if (!error) return nullptr;
    *error = ItemNull;
    if (owner && !owner->bindings) owner->bindings = pool_create();
    if (!owner || !owner->bindings) {
        *error = binary_schema_error("native binary schema owner has no binding storage");
        return nullptr;
    }
    return binary_schema_bind_native_depth(owner, declared, native, error, 0);
}

static bool binary_schema_view_valid(const BinarySchemaView* view) {
    return view && view->schema && (!view->owner ||
        (view->owner->valid && view->generation == view->owner->generation));
}

static VirtualOpStatus binary_schema_failed(Item* out, const char* message) {
    if (out) *out = binary_schema_error(message);
    return VIRTUAL_OP_ERROR;
}

static void binary_schema_destroy(void* data) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    if (!view) return;
    binary_schema_owner_release(view->owner);
    mem_free(view);
}

template<class Visitor>
static bool binary_schema_visit_items(const BinarySchema* schema, const void* record,
                                     Visitor& visit, size_t depth) {
    if (!record || !schema) return true;
    if (depth >= 64) return false;
    for (size_t i = 0; i < schema->field_count; i++) {
        const BinarySchemaField& field = schema->fields[i];
        if (!field.locate) continue;
        const void* slot = field.locate(record);
        if (field.kind == BINARY_FIELD_ITEM && field.storage_size == sizeof(Item)) {
            Item item;
            memcpy(&item, slot, sizeof(item));
            if (!visit(item)) return false;
        } else if (field.kind == BINARY_FIELD_RECORD || field.kind == BINARY_FIELD_REFERENCE) {
            const void* target = field.kind == BINARY_FIELD_RECORD ? slot :
                field.dereference ? field.dereference(slot) : nullptr;
            if (!binary_schema_visit_items(field.target, target, visit, depth + 1)) return false;
        }
    }
    return true;
}

static void binary_schema_trace(void* data, gc_heap* gc) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    if (!view) return;
    gc_mark_item(gc, view->binary.item);
    auto mark = [gc](Item item) { gc_mark_item(gc, item.item); return true; };
    if (binary_schema_view_valid(view)) binary_schema_visit_items(view->schema, view->record, mark, 0);
}

static uint64_t binary_schema_version(void* data) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    return view && view->owner ? view->owner->generation : 0;
}

static Item binary_schema_wrap(const BinarySchema* schema, const void* record,
                              BinarySchemaOwner* owner, Item binary, size_t offset,
                              BinarySchemaEndian parent_endian = BINARY_SCHEMA_NATIVE);

static bool binary_schema_scalar_width(BinaryFieldKind kind, size_t* width) {
    switch (kind) {
    case BINARY_FIELD_BOOL: case BINARY_FIELD_I8: case BINARY_FIELD_U8:
        *width = 1; return true;
    case BINARY_FIELD_I16: case BINARY_FIELD_U16:
        *width = 2; return true;
    case BINARY_FIELD_I32: case BINARY_FIELD_U32: case BINARY_FIELD_F32:
        *width = 4; return true;
    case BINARY_FIELD_I64: case BINARY_FIELD_U64: case BINARY_FIELD_F64:
        *width = 8; return true;
    default: return false;
    }
}

static VirtualOpStatus binary_schema_field(BinarySchemaView* view, size_t index,
                                           Item* out) {
    if (!out || !binary_schema_view_valid(view))
        return binary_schema_failed(out, "binary schema owner is no longer valid");
    if (index >= view->schema->field_count) return VIRTUAL_OP_MISSING;
    const BinarySchemaField* field = &view->schema->fields[index];
    const void* slot = nullptr;
    bool buffered = get_type_id(view->binary) == LMD_TYPE_BINARY;
    if (buffered) {
        Binary* binary = view->binary.get_binary();
        size_t length = binary_length(binary);
        if (field->locate || field->kind == BINARY_FIELD_REFERENCE ||
                field->offset > view->schema->byte_size ||
                field->storage_size > view->schema->byte_size - field->offset ||
                view->offset > length || field->offset > length - view->offset ||
                field->storage_size > length - view->offset - field->offset)
            return binary_schema_failed(out, "binary schema field exceeds its byte source");
        slot = binary_data(binary) + view->offset + field->offset;
    } else {
        if (!view->record || !field->locate)
            return binary_schema_failed(out, "native schema field has no compiled binding");
        slot = field->locate(view->record);
    }
    if (!slot) return binary_schema_failed(out, "binary schema field has no storage");
    if (field->kind == BINARY_FIELD_ITEM) {
        if (buffered || field->storage_size != sizeof(Item))
            return binary_schema_failed(out, "boxed native fields cannot be decoded from bytes");
        memcpy(out, slot, sizeof(Item));
        if (get_type_id(*out) == LMD_TYPE_NULL) {
            if (!field->nullable && field->logical_type && field->logical_type != &TYPE_ANY)
                return binary_schema_failed(out, "nonnull native boxed field is null");
        } else if (field->logical_type && field->logical_type != &TYPE_ANY &&
                item_semantic_type_id(get_type_id(*out)) != field->logical_type->type_id)
            return binary_schema_failed(out, "native boxed value violates its schema field type");
        return VIRTUAL_OP_OK;
    }
    if (field->kind == BINARY_FIELD_RECORD || field->kind == BINARY_FIELD_REFERENCE) {
        if (!field->target || (field->kind == BINARY_FIELD_RECORD &&
                field->storage_size < field->target->byte_size))
            return binary_schema_failed(out, "binary schema record binding is incompatible");
        const void* target = slot;
        if (field->kind == BINARY_FIELD_REFERENCE) {
            if (!field->dereference)
                return binary_schema_failed(out, "native schema reference has no typed binding");
            target = field->dereference(slot);
            if (!target) {
                if (!field->nullable)
                    return binary_schema_failed(out, "nonnull native schema reference is null");
                *out = ItemNull;
                return VIRTUAL_OP_OK;
            }
        }
        // The child retains the same owner; a live GC wrapper alone cannot pin a native arena.
        *out = binary_schema_wrap(field->target, buffered ? nullptr : target,
            view->owner, view->binary, view->offset + field->offset, view->endian);
        return get_type_id(*out) == LMD_TYPE_ERROR ? VIRTUAL_OP_ERROR : VIRTUAL_OP_OK;
    }
    size_t width = 0;
    if (!binary_schema_scalar_width(field->kind, &width) || width != field->storage_size)
        return binary_schema_failed(out, "binary schema scalar width disagrees with its binding");
    uint64_t bits = 0;
    const uint16_t endian_probe = 1;
    bool host_little = *(const uint8_t*)&endian_probe != 0;
    bool little = view->endian == BINARY_SCHEMA_NATIVE ? host_little :
        view->endian == BINARY_SCHEMA_LITTLE;
    const uint8_t* bytes = (const uint8_t*)slot;
    for (size_t i = 0; i < width; i++)
        bits |= (uint64_t)bytes[little ? i : width - i - 1] << (8 * i);
    switch (field->kind) {
    case BINARY_FIELD_BOOL:
        if (bits > 1) return binary_schema_failed(out, "binary schema boolean is not zero or one");
        *out = {.item = b2it(bits)}; break;
    case BINARY_FIELD_I8: *out = {.item = NUM_SIZED_PACK(NUM_INT8, bits)}; break;
    case BINARY_FIELD_U8: *out = {.item = NUM_SIZED_PACK(NUM_UINT8, bits)}; break;
    case BINARY_FIELD_I16: *out = {.item = NUM_SIZED_PACK(NUM_INT16, bits)}; break;
    case BINARY_FIELD_U16: *out = {.item = NUM_SIZED_PACK(NUM_UINT16, bits)}; break;
    case BINARY_FIELD_I32: *out = {.item = NUM_SIZED_PACK(NUM_INT32, bits)}; break;
    case BINARY_FIELD_U32: *out = {.item = NUM_SIZED_PACK(NUM_UINT32, bits)}; break;
    case BINARY_FIELD_I64: *out = box_int64_value((int64_t)bits); break;
    case BINARY_FIELD_U64: *out = box_uint64_value(bits); break;
    case BINARY_FIELD_F32: *out = {.item = NUM_SIZED_PACK(NUM_FLOAT32, bits)}; break;
    case BINARY_FIELD_F64: {
        double value;
        memcpy(&value, &bits, sizeof(value));
        *out = push_d(value); break;
    }
    default: return binary_schema_failed(out, "unsupported binary schema scalar");
    }
    return get_type_id(*out) == LMD_TYPE_ERROR ? VIRTUAL_OP_ERROR : VIRTUAL_OP_OK;
}

static VirtualOpStatus binary_schema_get(void* data, Item key, Item* out) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    if (!binary_schema_view_valid(view))
        return binary_schema_failed(out, "binary schema owner is no longer valid");
    if (!is_text_type_id(get_type_id(key))) return VIRTUAL_OP_MISSING;
    for (size_t i = 0; i < view->schema->field_count; i++) {
        const char* name = view->schema->fields[i].name;
        if (name && strlen(name) == key.get_len() &&
                memcmp(name, key.get_chars(), key.get_len()) == 0)
            return binary_schema_field(view, i, out);
    }
    return VIRTUAL_OP_MISSING;
}

static VirtualOpStatus binary_schema_tag(void* data, Item* out) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    if (!binary_schema_view_valid(view))
        return binary_schema_failed(out, "binary schema owner is no longer valid");
    *out = {.item = s2it(heap_create_name(view->schema->tag, strlen(view->schema->tag)))};
    return VIRTUAL_OP_OK;
}

static int64_t binary_schema_count(void* data) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    return binary_schema_view_valid(view) ? view->schema->field_count : 0;
}

static VirtualOpStatus binary_schema_key_at(void* data, int64_t index, Item* out) {
    BinarySchemaView* view = (BinarySchemaView*)data;
    if (!binary_schema_view_valid(view))
        return binary_schema_failed(out, "binary schema owner is no longer valid");
    if (index < 0 || (uint64_t)index >= view->schema->field_count) return VIRTUAL_OP_MISSING;
    const char* name = view->schema->fields[index].name;
    *out = {.item = s2it(heap_create_name(name, strlen(name)))};
    return VIRTUAL_OP_OK;
}

static VirtualOpStatus binary_schema_value_at(void* data, int64_t index, Item* out) {
    if (index < 0) return VIRTUAL_OP_MISSING;
    return binary_schema_field((BinarySchemaView*)data, (size_t)index, out);
}

static VirtualOpStatus binary_schema_prepare(void* data, Item* error) {
    return binary_schema_view_valid((BinarySchemaView*)data) ? VIRTUAL_OP_OK :
        binary_schema_failed(error, "binary schema owner is no longer valid");
}

static int64_t binary_schema_no_children(void*) { return 0; }
static VirtualOpStatus binary_schema_no_child(void*, int64_t, Item*) { return VIRTUAL_OP_MISSING; }

static const VelmtVtable binary_schema_vtable = {
    {LAMBDA_VIRTUAL_ABI_VERSION, LMD_TYPE_VELMT, {0, 0, 0},
        binary_schema_destroy, binary_schema_trace, binary_schema_version},
    {binary_schema_tag, nullptr,
        {binary_schema_get, nullptr, nullptr, nullptr, binary_schema_count,
         binary_schema_key_at, binary_schema_value_at},
        {binary_schema_no_children, binary_schema_no_child, nullptr, nullptr,
         binary_schema_prepare}}
};

static Item binary_schema_wrap(const BinarySchema* schema, const void* record,
        BinarySchemaOwner* owner, Item binary, size_t offset, BinarySchemaEndian parent_endian) {
    if (!schema || !schema->tag || (!schema->fields && schema->field_count))
        return binary_schema_error("invalid binary schema descriptor");
    RootFrame roots(1);
    Rooted<Item> source(roots, binary);
    size_t item_count = 0;
    auto count = [&item_count](Item) {
        if (item_count == SIZE_MAX / sizeof(Item)) return false;
        item_count++;
        return true;
    };
    if (!binary_schema_visit_items(schema, record, count, 0))
        return binary_schema_error("native binary schema graph exceeds supported nesting or root extent");
    // The carrier allocation can collect before its payload becomes visible to GC.
    RootSpan native_items(item_count);
    size_t item_index = 0;
    auto retain = [&native_items, &item_index](Item item) {
        native_items.words()[item_index++] = item.item;
        return true;
    };
    if (item_count && native_items.valid()) binary_schema_visit_items(schema, record, retain, 0);
    BinarySchemaView* view = (BinarySchemaView*)mem_calloc(1,
        sizeof(BinarySchemaView), MEM_CAT_CONTAINER);
    if (!view) return binary_schema_error("cannot allocate binary schema view");
    *view = {schema, record, owner, owner ? owner->generation : 0, source.get(), offset,
        schema->endian == BINARY_SCHEMA_INHERIT ? parent_endian : schema->endian};
    if (owner) owner->references++;
    Item result = velmt_new(&binary_schema_vtable, view, nullptr, nullptr);
    if (get_type_id(result) != LMD_TYPE_VELMT) {
        binary_schema_destroy(view);
        return binary_schema_error("cannot allocate binary schema carrier");
    }
    return result;
}

Item binary_schema_native_view(const BinarySchema* schema, const void* record,
                              BinarySchemaOwner* owner) {
    if (!record || !owner || !owner->valid)
        return binary_schema_error("native schema view needs a live owner and record");
    return binary_schema_wrap(schema, record, owner, ItemNull, 0);
}

Item binary_schema_decode(const BinarySchema* schema, Item binary) {
    if (!schema || (!schema->fields && schema->field_count) || get_type_id(binary) != LMD_TYPE_BINARY ||
            schema->byte_size > binary_length(binary.get_binary()))
        return binary_schema_error("binary schema source is truncated or not binary");
    if (schema->binding) return binary_schema_error("native schema references cannot be decoded from bytes");
    for (size_t i = 0; i < schema->field_count; i++) {
        if (schema->fields[i].locate || schema->fields[i].kind == BINARY_FIELD_REFERENCE ||
                schema->fields[i].kind == BINARY_FIELD_ITEM)
            return binary_schema_error("native schema references cannot be decoded from bytes");
    }
    return binary_schema_wrap(schema, nullptr, nullptr, binary, 0);
}

const BinarySchema* binary_schema_from_type(Type* type) {
    type = type_field_unwrap_simple_decl(type);
    return type && type->type_id == LMD_TYPE_TYPE && type->kind == TYPE_KIND_BINARY_SCHEMA
        ? &((TypeBinarySchema*)type)->layout : nullptr;
}

bool binary_schema_matches(Item value, const BinarySchema* schema) {
    if (get_type_id(value) != LMD_TYPE_VELMT || !value.velmt ||
            value.velmt->vtable != &binary_schema_vtable) return false;
    BinarySchemaView* view = (BinarySchemaView*)value.velmt->data;
    return binary_schema_view_valid(view) &&
        (view->schema == schema || view->schema->identity == schema);
}

const void* binary_schema_native_record(Item value, const char* binding) {
    if (get_type_id(value) != LMD_TYPE_VELMT || !value.velmt ||
            value.velmt->vtable != &binary_schema_vtable) return nullptr;
    BinarySchemaView* view = (BinarySchemaView*)value.velmt->data;
    return binary_schema_view_valid(view) && view->record && view->schema->binding &&
        binding && strcmp(view->schema->binding, binding) == 0 ? view->record : nullptr;
}

extern "C" Item fn_binary_decode(Item binary, Item type) {
    const BinarySchema* schema = get_type_id(type) == LMD_TYPE_TYPE
        ? binary_schema_from_type(type.type) : nullptr;
    if (!schema || schema->binding)
        return binary_schema_error("binary_decode requires a buffer schema, not a native binding");
    return binary_schema_decode(schema, binary);
}
