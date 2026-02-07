#include "mark_editor.hpp"
#include "../lib/log.h"
#include <string.h>
#include <stdlib.h>

// Maximum number of batch updates supported
#define MAX_BATCH_UPDATES 64

// Forward declarations of helper functions from input.cpp
extern void map_put(Map* mp, String* key, Item value, Input *input);
extern void elmt_put(Element* elmt, String* key, Item value, Pool* pool);
extern TypeMap EmptyMap;
extern TypeElmt EmptyElmt;
extern TypeInfo type_info[];

//==============================================================================
// Constructor / Destructor
//==============================================================================

MarkEditor::MarkEditor(Input* input, EditMode mode)
    : input_(input)
    , pool_(input->pool)
    , arena_(input->arena)
    , name_pool_(input->name_pool)
    , shape_pool_(input->shape_pool)
    , type_list_(input->type_list)
    , mode_(mode)
    , current_version_(nullptr)
    , version_head_(nullptr)
    , next_version_num_(0)
{
    // Create builder for constructing new structures
    builder_ = new MarkBuilder(input);

    log_debug("MarkEditor created: mode=%s",
        mode == EDIT_MODE_INLINE ? "inline" : "immutable");
}

MarkEditor::~MarkEditor() {
    // Clean up version history
    if (version_head_) {
        free_version_chain(version_head_);
    }

    // Clean up builder
    if (builder_) {
        delete builder_;
        builder_ = nullptr;
    }

    log_debug("MarkEditor destroyed");
}

//==============================================================================
// Version Control Helpers
//==============================================================================

EditVersion* MarkEditor::create_version(Item root, const char* description) {
    EditVersion* version = (EditVersion*)pool_calloc(pool_, sizeof(EditVersion));
    if (!version) return nullptr;

    version->root = root;
    version->version_number = next_version_num_++;
    version->description = description ? strdup(description) : nullptr;
    version->prev = nullptr;
    version->next = nullptr;

    log_debug("Created version %d: %s", version->version_number,
        description ? description : "(no description)");

    return version;
}

void MarkEditor::free_version_chain(EditVersion* version) {
    EditVersion* current = version;
    while (current) {
        EditVersion* next = current->next;
        if (current->description) {
            free((void*)current->description);
        }
        pool_free(pool_, current);
        current = next;
    }
}

//==============================================================================
// Mode Control
//==============================================================================

void MarkEditor::set_mode(EditMode mode) {
    if (mode_ == mode) return;

    if (mode == EDIT_MODE_INLINE) {
        // Switching to inline mode - clear version history
        log_warn("Switching to inline mode, clearing version history");
        if (version_head_) {
            free_version_chain(version_head_);
            version_head_ = nullptr;
            current_version_ = nullptr;
            next_version_num_ = 0;
        }
    }

    mode_ = mode;
    log_debug("Edit mode changed to: %s", mode == EDIT_MODE_INLINE ? "inline" : "immutable");
}

//==============================================================================
// Version Control API
//==============================================================================

int MarkEditor::commit(const char* description) {
    if (mode_ != EDIT_MODE_IMMUTABLE) {
        log_warn("commit: only available in immutable mode");
        return -1;
    }

    Item current_root = input_->root;
    EditVersion* version = create_version(current_root, description);
    if (!version) {
        log_error("commit: failed to create version");
        return -1;
    }

    if (current_version_) {
        // Clear any redo history when committing new version
        if (current_version_->next) {
            free_version_chain(current_version_->next);
        }
        current_version_->next = version;
        version->prev = current_version_;
    } else {
        version_head_ = version;
    }

    current_version_ = version;

    return version->version_number;
}

bool MarkEditor::undo() {
    if (mode_ != EDIT_MODE_IMMUTABLE || !current_version_ || !current_version_->prev) {
        log_debug("undo: cannot undo (mode=%d, current=%p, prev=%p)",
            mode_, current_version_, current_version_ ? current_version_->prev : nullptr);
        return false;
    }

    current_version_ = current_version_->prev;
    input_->root = current_version_->root;

    log_debug("undo: reverted to version %d", current_version_->version_number);
    return true;
}

bool MarkEditor::redo() {
    if (mode_ != EDIT_MODE_IMMUTABLE || !current_version_ || !current_version_->next) {
        log_debug("redo: cannot redo (mode=%d, current=%p, next=%p)",
            mode_, current_version_, current_version_ ? current_version_->next : nullptr);
        return false;
    }

    current_version_ = current_version_->next;
    input_->root = current_version_->root;

    log_debug("redo: advanced to version %d", current_version_->version_number);
    return true;
}

Item MarkEditor::current() const {
    if (mode_ == EDIT_MODE_IMMUTABLE && current_version_) {
        return current_version_->root;
    }
    return input_->root;
}

Item MarkEditor::get_version(int version_num) const {
    if (mode_ != EDIT_MODE_IMMUTABLE) {
        log_warn("get_version: only available in immutable mode");
        return ItemNull;
    }

    EditVersion* v = version_head_;
    while (v) {
        if (v->version_number == version_num) {
            return v->root;
        }
        v = v->next;
    }

    log_warn("get_version: version %d not found", version_num);
    return ItemNull;
}

void MarkEditor::list_versions() const {
    if (mode_ != EDIT_MODE_IMMUTABLE) {
        printf("Version control not available in inline mode\n");
        return;
    }

    if (!version_head_) {
        printf("No versions committed yet\n");
        return;
    }

    EditVersion* v = version_head_;
    while (v) {
        printf("Version %d: %s %s\n",
               v->version_number,
               v->description ? v->description : "(no description)",
               v == current_version_ ? "<- current" : "");
        v = v->next;
    }
}

//==============================================================================
// Utility Helpers
//==============================================================================

String* MarkEditor::ensure_string_key(const char* key) {
    if (!key) return nullptr;
    // Use name_pool for keys (structural identifiers)
    return name_pool_create_len(name_pool_, key, strlen(key));
}

bool MarkEditor::find_field_in_shape(ShapeEntry* shape, const char* key,
                                     TypeId* out_type, int64_t* out_offset) {
    if (!shape || !key) return false;

    ShapeEntry* entry = shape;
    while (entry) {
        if (strcmp(entry->name->str, key) == 0) {
            if (out_type) *out_type = entry->type->type_id;
            if (out_offset) *out_offset = entry->byte_offset;
            return true;
        }
        entry = entry->next;
    }

    return false;
}

void MarkEditor::store_value_at_offset(void* field_ptr, Item value, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_NULL:
        *(void**)field_ptr = nullptr;
        break;
    case LMD_TYPE_BOOL:
        *(bool*)field_ptr = value.bool_val;
        break;
    case LMD_TYPE_INT:
        *(int64_t*)field_ptr = value.get_int56();  // write full int64 to preserve 56-bit value
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_DTIME:
        *(DateTime*)field_ptr = value.get_datetime();
        break;
    case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_BINARY: {
        String* str = value.get_string();
        *(String**)field_ptr = str;
        if (str) str->ref_cnt++;
        break;
    }
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_INT:
    case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_LIST:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: {
        Container* container = value.container;
        *(Container**)field_ptr = container;
        if (container) container->ref_cnt++;
        break;
    }
    default:
        log_error("store_value_at_offset: unsupported type %s", get_type_name(type_id));
        break;
    }
}

void MarkEditor::decrement_ref_count(void* field_ptr, TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_STRING:
    case LMD_TYPE_SYMBOL:
    case LMD_TYPE_BINARY: {
        String* str = *(String**)field_ptr;
        if (str && str->ref_cnt > 0) {
            str->ref_cnt--;
        }
        break;
    }
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_INT:
    case LMD_TYPE_ARRAY_INT64:
    case LMD_TYPE_ARRAY_FLOAT:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_LIST:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: {
        Container* container = *(Container**)field_ptr;
        if (container && container->ref_cnt > 0) {
            container->ref_cnt--;
        }
        break;
    }
    default:
        // Other types don't use ref counting
        break;
    }
}

//==============================================================================
// MAP OPERATIONS
//==============================================================================

Item MarkEditor::map_update(Item map, const char* key, Item value) {
    // Check type_id first before accessing union fields
    TypeId map_type_id = get_type_id(map);
    if (map_type_id != LMD_TYPE_MAP || !map.map) {
        log_error("map_update: not a map (type=%d)", map_type_id);
        return ItemError;
    }

    String* key_str = ensure_string_key(key);
    if (!key_str) {
        log_error("map_update: invalid key");
        return ItemError;
    }

    return map_update(map, key_str, value);
}

Item MarkEditor::map_update(Item map, String* key, Item value) {
    TypeId map_type_id = get_type_id(map);
    if (map_type_id != LMD_TYPE_MAP || !map.map) {
        log_error("map_update: not a map (type=%d)", map_type_id);
        return ItemError;
    }

    if (!key) {
        log_error("map_update: null key");
        return ItemError;
    }

    // Ensure value is in target arena (deep copy if external)
    if (!builder_->is_in_arena(value)) {
        log_debug("map_update: value not in arena, deep copying");
        value = builder_->deep_copy(value);
    }

    if (mode_ == EDIT_MODE_INLINE) {
        return map_update_inline(map.map, key, value);
    } else {
        return map_update_immutable(map.map, key, value);
    }
}

Item MarkEditor::map_update_inline(Map* map, String* key, Item value) {
    TypeMap* map_type = (TypeMap*)map->type;
    TypeId value_type = get_type_id(value);

    log_debug("map_update_inline: key='%s', value_type=%d", key->chars, value_type);

    // Check if field already exists
    TypeId existing_type;
    int64_t existing_offset;
    bool field_exists = find_field_in_shape(map_type->shape, key->chars,
                                           &existing_type, &existing_offset);

    if (field_exists) {
        // Field exists - check if type matches
        if (existing_type == value_type) {
            // Same type - simple in-place update
            log_debug("map_update_inline: same type, in-place update at offset %ld", existing_offset);

            void* field_ptr = (char*)map->data + existing_offset;

            // Decrement ref count for old value
            decrement_ref_count(field_ptr, existing_type);

            // Store new value
            store_value_at_offset(field_ptr, value, value_type);

            return {.map = map};  // Return same map
        } else {
            // Type changed - need to rebuild shape
            log_debug("map_update_inline: type changed, rebuilding shape");

            ShapeBuilder builder = shape_builder_init_map(shape_pool_);
            shape_builder_import_shape(&builder, map_type->shape);

            // Update the changed field's type
            shape_builder_remove_field(&builder, key->chars);
            shape_builder_add_field(&builder, key->chars, value_type);

            Item rebuilt = map_rebuild_with_new_shape(map, &builder, true);

            // Now store the new value in the rebuilt map
            if (rebuilt.map && rebuilt.map->type_id == LMD_TYPE_MAP) {
                TypeMap* rebuilt_type = (TypeMap*)rebuilt.map->type;
                TypeId field_type;
                int64_t field_offset;
                if (find_field_in_shape(rebuilt_type->shape, key->chars, &field_type, &field_offset)) {
                    void* field_ptr = (char*)rebuilt.map->data + field_offset;
                    store_value_at_offset(field_ptr, value, value_type);
                }
            }

            return rebuilt;
        }
    } else {
        // New field - add to shape
        log_debug("map_update_inline: new field, rebuilding shape");

        ShapeBuilder builder = shape_builder_init_map(shape_pool_);
        shape_builder_import_shape(&builder, map_type->shape);
        shape_builder_add_field(&builder, key->chars, value_type);

        Item rebuilt = map_rebuild_with_new_shape(map, &builder, true);

        // Store the new field value
        if (rebuilt.map && rebuilt.map->type_id == LMD_TYPE_MAP) {
            TypeMap* rebuilt_type = (TypeMap*)rebuilt.map->type;
            TypeId field_type;
            int64_t field_offset;
            if (find_field_in_shape(rebuilt_type->shape, key->chars, &field_type, &field_offset)) {
                void* field_ptr = (char*)rebuilt.map->data + field_offset;
                store_value_at_offset(field_ptr, value, value_type);
            }
        }

        return rebuilt;
    }
}

Item MarkEditor::map_rebuild_with_new_shape(Map* old_map, ShapeBuilder* builder, bool is_inline) {
    log_debug("map_rebuild_with_new_shape: field_count=%zu", builder->field_count);

    // Get new deduplicated shape
    ShapeEntry* new_shape = shape_builder_finalize(builder);
    if (!new_shape && builder->field_count > 0) {
        log_error("map_rebuild_with_new_shape: failed to finalize shape");
        return ItemError;
    }

    // Calculate new byte size
    int64_t new_byte_size = 0;
    ShapeEntry* entry = new_shape;
    while (entry) {
        new_byte_size = entry->byte_offset + type_info[entry->type->type_id].byte_size;
        entry = entry->next;
    }

    log_debug("map_rebuild_with_new_shape: new_byte_size=%ld", new_byte_size);

    // Allocate new data buffer
    void* new_data = pool_calloc(pool_, new_byte_size);
    if (!new_data && new_byte_size > 0) {
        log_error("map_rebuild_with_new_shape: allocation failed");
        return ItemError;
    }

    // Copy matching fields from old data to new data
    TypeMap* old_type = (TypeMap*)old_map->type;
    entry = new_shape;
    while (entry) {
        // Find matching field in old shape
        TypeId old_type_id;
        int64_t old_offset;
        bool found = find_field_in_shape(old_type->shape, entry->name->str,
                                        &old_type_id, &old_offset);

        if (found && old_type_id == entry->type->type_id) {
            // Copy value from old to new location
            void* old_field = (char*)old_map->data + old_offset;
            void* new_field = (char*)new_data + entry->byte_offset;
            int field_size = type_info[entry->type->type_id].byte_size;
            memcpy(new_field, old_field, field_size);

            // Update ref counts for pointer types (only in immutable mode)
            if (!is_inline) {
                if (entry->type->type_id == LMD_TYPE_STRING ||
                    entry->type->type_id == LMD_TYPE_SYMBOL ||
                    entry->type->type_id == LMD_TYPE_BINARY) {
                    String* str = *(String**)new_field;
                    if (str) str->ref_cnt++;
                }
                else if (entry->type->type_id >= LMD_TYPE_LIST &&
                         entry->type->type_id <= LMD_TYPE_ELEMENT) {
                    Container* container = *(Container**)new_field;
                    if (container) container->ref_cnt++;
                }
            }
        }
        // If field not found in old shape or type changed, leave as zero (default)

        entry = entry->next;
    }

    Map* result_map = old_map;

    if (!is_inline) {
        // Immutable mode - create new map structure
        result_map = (Map*)arena_alloc(arena_, sizeof(Map));
        if (!result_map) {
            log_error("map_rebuild_with_new_shape: failed to allocate new map");
            return ItemError;
        }
        memcpy(result_map, old_map, sizeof(Map));
        result_map->ref_cnt = 0;
    }

    // Create or update TypeMap
    TypeMap* new_type;
    if (old_type->type_index == -1 || old_type == &EmptyMap || !is_inline) {
        // Need to create new TypeMap
        new_type = (TypeMap*)alloc_type(pool_, LMD_TYPE_MAP, sizeof(TypeMap));
        new_type->shape = new_shape;
        new_type->length = builder->field_count;
        new_type->byte_size = new_byte_size;
        new_type->type_index = type_list_->length;

        // Find last entry
        new_type->last = new_shape;
        while (new_type->last && new_type->last->next) {
            new_type->last = new_type->last->next;
        }

        arraylist_append(type_list_, new_type);
        result_map->type = new_type;
    } else {
        // Inline mode - mutate existing TypeMap
        old_type->shape = new_shape;
        old_type->length = builder->field_count;
        old_type->byte_size = new_byte_size;

        // Find last entry
        old_type->last = new_shape;
        while (old_type->last && old_type->last->next) {
            old_type->last = old_type->last->next;
        }
    }

    // Free old data (inline mode only), replace with new
    if (is_inline && old_map->data) {
        pool_free(pool_, old_map->data);
    }
    result_map->data = new_data;
    result_map->data_cap = new_byte_size;

    log_debug("map_rebuild_with_new_shape: success");
    return {.map = result_map};
}

Item MarkEditor::map_update_immutable(Map* old_map, String* key, Item value) {
    log_debug("map_update_immutable: key='%s'", key->chars);

    // Determine if we need new shape
    TypeMap* old_type = (TypeMap*)old_map->type;
    TypeId value_type = get_type_id(value);
    TypeId existing_type;
    int64_t existing_offset;
    bool field_exists = find_field_in_shape(old_type->shape, key->chars,
                                           &existing_type, &existing_offset);

    if (field_exists && existing_type == value_type) {
        // Same shape - create new map with copied data
        log_debug("map_update_immutable: same shape, copying data");

        Map* new_map = (Map*)arena_alloc(arena_, sizeof(Map));
        if (!new_map) return ItemError;

        memcpy(new_map, old_map, sizeof(Map));
        new_map->ref_cnt = 0;

        new_map->data = pool_calloc(pool_, old_type->byte_size);
        if (!new_map->data && old_type->byte_size > 0) return ItemError;

        memcpy(new_map->data, old_map->data, old_type->byte_size);
        new_map->data_cap = old_type->byte_size;

        // Update the changed field
        void* field_ptr = (char*)new_map->data + existing_offset;
        store_value_at_offset(field_ptr, value, value_type);

        // new_map->type stays same (shared TypeMap)

        return {.map = new_map};

    } else {
        // Different shape - rebuild
        log_debug("map_update_immutable: different shape, rebuilding");

        ShapeBuilder builder = shape_builder_init_map(shape_pool_);
        shape_builder_import_shape(&builder, old_type->shape);

        if (field_exists) {
            // Update existing field's type
            shape_builder_remove_field(&builder, key->chars);
        }
        shape_builder_add_field(&builder, key->chars, value_type);

        // Create new map structure first
        Map* new_map = (Map*)arena_alloc(arena_, sizeof(Map));
        if (!new_map) return ItemError;

        memcpy(new_map, old_map, sizeof(Map));
        new_map->ref_cnt = 0;

        // This will create new data buffer and TypeMap
        Item rebuilt = map_rebuild_with_new_shape(new_map, &builder, false);

        // Store the new/updated field value
        if (rebuilt.map && rebuilt.map->type_id == LMD_TYPE_MAP) {
            TypeMap* rebuilt_type = (TypeMap*)rebuilt.map->type;
            TypeId field_type;
            int64_t field_offset;
            if (find_field_in_shape(rebuilt_type->shape, key->chars, &field_type, &field_offset)) {
                void* field_ptr = (char*)rebuilt.map->data + field_offset;
                store_value_at_offset(field_ptr, value, value_type);
            }
        }

        return rebuilt;
    }
}

Item MarkEditor::map_update_batch(Item map, int count, ...) {
    TypeId map_type_id = get_type_id(map);
    if (map_type_id != LMD_TYPE_MAP || !map.map) {
        log_error("map_update_batch: not a map (type=%d)", map_type_id);
        return ItemError;
    }

    if (count <= 0) {
        log_warn("map_update_batch: count <= 0");
        return map;
    }

    log_debug("map_update_batch: updating %d fields", count);

    // First, collect all key-value pairs (we need them twice: for shape and for storing)
    struct UpdateEntry {
        const char* key;
        Item value;
        TypeId value_type;
    };

    // Use stack-allocated array for batch updates
    if (count > MAX_BATCH_UPDATES) {
        log_error("map_update_batch: count %d exceeds max %d", count, MAX_BATCH_UPDATES);
        return ItemError;
    }
    UpdateEntry updates[MAX_BATCH_UPDATES];
    int update_count = 0;

    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++) {
        UpdateEntry entry;
        entry.key = va_arg(args, const char*);
        entry.value = va_arg(args, Item);

        // Ensure value is in target arena (deep copy if external)
        if (!builder_->is_in_arena(entry.value)) {
            log_debug("map_update_batch: value for key '%s' not in arena, deep copying", entry.key);
            entry.value = builder_->deep_copy(entry.value);
        }

        entry.value_type = get_type_id(entry.value);
        updates[update_count++] = entry;
    }

    va_end(args);

    // Build new shape with all updates
    Map* target_map = map.map;
    TypeMap* map_type = (TypeMap*)target_map->type;

    ShapeBuilder builder = shape_builder_init_map(shape_pool_);
    shape_builder_import_shape(&builder, map_type->shape);

    for (int i = 0; i < count; i++) {
        // Update or add field in builder
        if (shape_builder_has_field(&builder, updates[i].key)) {
            shape_builder_remove_field(&builder, updates[i].key);
        }
        shape_builder_add_field(&builder, updates[i].key, updates[i].value_type);
    }

    // Rebuild map with new shape
    Item rebuilt;
    if (mode_ == EDIT_MODE_INLINE) {
        rebuilt = map_rebuild_with_new_shape(target_map, &builder, true);
    } else {
        Map* new_map = (Map*)arena_alloc(arena_, sizeof(Map));
        if (!new_map) return ItemError;
        memcpy(new_map, target_map, sizeof(Map));
        new_map->ref_cnt = 0;
        rebuilt = map_rebuild_with_new_shape(new_map, &builder, false);
    }

    // Now store all the new values in the rebuilt map
    if (rebuilt.map && rebuilt.map->type_id == LMD_TYPE_MAP) {
        TypeMap* rebuilt_type = (TypeMap*)rebuilt.map->type;
        for (int i = 0; i < count; i++) {
            TypeId field_type;
            int64_t field_offset;
            if (find_field_in_shape(rebuilt_type->shape, updates[i].key, &field_type, &field_offset)) {
                void* field_ptr = (char*)rebuilt.map->data + field_offset;
                store_value_at_offset(field_ptr, updates[i].value, updates[i].value_type);
            }
        }
    }

    return rebuilt;
}

Item MarkEditor::map_delete(Item map, const char* key) {
    TypeId map_type_id = get_type_id(map);
    if (map_type_id != LMD_TYPE_MAP || !map.map) {
        log_error("map_delete: not a map (type=%d)", map_type_id);
        return ItemError;
    }

    String* key_str = ensure_string_key(key);
    if (!key_str) {
        log_error("map_delete: invalid key");
        return ItemError;
    }

    return map_delete(map, key_str);
}

Item MarkEditor::map_delete(Item map, String* key) {
    if (!map.map || map.map->type_id != LMD_TYPE_MAP) {
        log_error("map_delete: not a map");
        return ItemError;
    }

    if (!key) {
        log_error("map_delete: null key");
        return ItemError;
    }

    if (mode_ == EDIT_MODE_INLINE) {
        return map_delete_inline(map.map, key);
    } else {
        return map_delete_immutable(map.map, key);
    }
}

Item MarkEditor::map_delete_inline(Map* map, String* key) {
    TypeMap* map_type = (TypeMap*)map->type;

    log_debug("map_delete_inline: key='%s'", key->chars);

    // Check if field exists
    if (!find_field_in_shape(map_type->shape, key->chars, nullptr, nullptr)) {
        log_warn("map_delete_inline: field '%s' not found", key->chars);
        return {.map = map};  // Return unchanged map
    }

    // Build new shape without the deleted field
    ShapeBuilder builder = shape_builder_init_map(shape_pool_);
    shape_builder_import_shape(&builder, map_type->shape);
    shape_builder_remove_field(&builder, key->chars);

    return map_rebuild_with_new_shape(map, &builder, true);
}

Item MarkEditor::map_delete_immutable(Map* old_map, String* key) {
    TypeMap* old_type = (TypeMap*)old_map->type;

    log_debug("map_delete_immutable: key='%s'", key->chars);

    // Check if field exists
    if (!find_field_in_shape(old_type->shape, key->chars, nullptr, nullptr)) {
        log_warn("map_delete_immutable: field '%s' not found", key->chars);
        return {.map = old_map};  // Return unchanged map
    }

    // Create new map structure
    Map* new_map = (Map*)arena_alloc(arena_, sizeof(Map));
    if (!new_map) return ItemError;

    memcpy(new_map, old_map, sizeof(Map));
    new_map->ref_cnt = 0;

    // Build new shape without the deleted field
    ShapeBuilder builder = shape_builder_init_map(shape_pool_);
    shape_builder_import_shape(&builder, old_type->shape);
    shape_builder_remove_field(&builder, key->chars);

    return map_rebuild_with_new_shape(new_map, &builder, false);
}

Item MarkEditor::map_delete_batch(Item map, int count, const char** keys) {
    if (!map.map || map.map->type_id != LMD_TYPE_MAP) {
        log_error("map_delete_batch: not a map");
        return ItemError;
    }

    if (count <= 0 || !keys) {
        log_warn("map_delete_batch: invalid arguments");
        return map;
    }

    log_debug("map_delete_batch: deleting %d fields", count);

    Map* target_map = map.map;
    TypeMap* map_type = (TypeMap*)target_map->type;

    // Build new shape without deleted fields
    ShapeBuilder builder = shape_builder_init_map(shape_pool_);
    shape_builder_import_shape(&builder, map_type->shape);

    for (int i = 0; i < count; i++) {
        shape_builder_remove_field(&builder, keys[i]);
    }

    if (mode_ == EDIT_MODE_INLINE) {
        return map_rebuild_with_new_shape(target_map, &builder, true);
    } else {
        Map* new_map = (Map*)arena_alloc(arena_, sizeof(Map));
        if (!new_map) return ItemError;
        memcpy(new_map, target_map, sizeof(Map));
        new_map->ref_cnt = 0;
        return map_rebuild_with_new_shape(new_map, &builder, false);
    }
}

Item MarkEditor::map_rename(Item map, const char* old_key, const char* new_key) {
    if (!map.map || map.map->type_id != LMD_TYPE_MAP) {
        log_error("map_rename: not a map");
        return ItemError;
    }

    Map* target_map = map.map;
    TypeMap* map_type = (TypeMap*)target_map->type;

    // Find old field
    TypeId field_type;
    int64_t field_offset;
    if (!find_field_in_shape(map_type->shape, old_key, &field_type, &field_offset)) {
        log_error("map_rename: field '%s' not found", old_key);
        return ItemError;
    }

    // Get old value
    void* old_field_ptr = (char*)target_map->data + field_offset;
    Item old_value;
    old_value._type_id = field_type;

    // Extract value based on type
    switch (field_type) {
    case LMD_TYPE_BOOL:
        old_value.bool_val = *(bool*)old_field_ptr;
        break;
    case LMD_TYPE_INT:
        old_value = {.item = i2it(*(int64_t*)old_field_ptr)};  // read full int64 to preserve 56-bit value
        break;
    default:
        old_value.string_ptr = *(uint64_t*)old_field_ptr;
        break;
    }

    // Delete old field and add new field with same value
    Item result = map_delete(map, old_key);
    result = map_update(result, new_key, old_value);

    return result;
}

//==============================================================================
// ELEMENT OPERATIONS
//==============================================================================

Item MarkEditor::elmt_update_attr(Item element, const char* attr_name, Item value) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_update_attr: not an element (type=%d)", element._type_id);
        return ItemError;
    }

    String* attr_str = ensure_string_key(attr_name);
    if (!attr_str) {
        log_error("elmt_update_attr: invalid attribute name");
        return ItemError;
    }

    return elmt_update_attr(element, attr_str, value);
}

Item MarkEditor::elmt_update_attr(Item element, String* attr_name, Item value) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_update_attr: not an element");
        return ItemError;
    }

    if (!attr_name) {
        log_error("elmt_update_attr: null attribute name");
        return ItemError;
    }

    // Ensure value is in target arena (deep copy if external)
    if (!builder_->is_in_arena(value)) {
        log_debug("elmt_update_attr: value not in arena, deep copying");
        value = builder_->deep_copy(value);
    }

    if (mode_ == EDIT_MODE_INLINE) {
        return elmt_update_attr_inline(element.element, attr_name, value);
    } else {
        return elmt_update_attr_immutable(element.element, attr_name, value);
    }
}

Item MarkEditor::elmt_update_attr_inline(Element* elmt, String* attr_name, Item value) {
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;
    TypeId value_type = get_type_id(value);

    log_debug("elmt_update_attr_inline: attr='%s', value_type=%d", attr_name->chars, value_type);

    // Check if attribute already exists
    TypeId existing_type;
    int64_t existing_offset;
    bool attr_exists = find_field_in_shape(elmt_type->shape, attr_name->chars,
                                          &existing_type, &existing_offset);

    if (attr_exists && existing_type == value_type) {
        // Same type - simple in-place update
        log_debug("elmt_update_attr_inline: same type, in-place update");

        void* attr_ptr = (char*)elmt->data + existing_offset;
        decrement_ref_count(attr_ptr, existing_type);
        store_value_at_offset(attr_ptr, value, value_type);

        return {.element = elmt};
    } else {
        // Rebuild with new shape
        log_debug("elmt_update_attr_inline: different type or new attr, rebuilding");

        ShapeBuilder builder = shape_builder_init_element(shape_pool_, elmt_type->name.str);
        shape_builder_import_shape(&builder, elmt_type->shape);

        if (attr_exists) {
            shape_builder_remove_field(&builder, attr_name->chars);
        }
        shape_builder_add_field(&builder, attr_name->chars, value_type);

        return elmt_rebuild_with_new_shape(elmt, &builder, true, attr_name, value);
    }
}

Item MarkEditor::elmt_rebuild_with_new_shape(Element* old_elmt, ShapeBuilder* builder, bool is_inline,
                                              String* new_attr_name, Item new_attr_value) {
    log_debug("elmt_rebuild_with_new_shape: field_count=%zu, new_attr=%s",
              builder->field_count, new_attr_name ? new_attr_name->chars : "NULL");

    // Get new deduplicated shape
    ShapeEntry* new_shape = shape_builder_finalize(builder);

    if (!new_shape) {
        log_error("elmt_rebuild_with_new_shape: shape_builder_finalize failed");
        return ItemError;
    }

    // Calculate new byte size
    int64_t new_byte_size = 0;
    ShapeEntry* entry = new_shape;
    while (entry) {
        new_byte_size = entry->byte_offset + type_info[entry->type->type_id].byte_size;
        entry = entry->next;
    }

    // Allocate new data buffer
    void* new_data = nullptr;
    if (new_byte_size > 0) {
        new_data = pool_calloc(pool_, new_byte_size);
        if (!new_data) {
            log_error("elmt_rebuild_with_new_shape: allocation failed");
            return ItemError;
        }
    }

    // Copy matching attributes from old data to new data
    TypeElmt* old_type = (TypeElmt*)old_elmt->type;
    TypeId new_attr_type = new_attr_name ? get_type_id(new_attr_value) : LMD_TYPE_NULL;

    entry = new_shape;
    while (entry) {
        // Check if this is the NEW attribute being added
        if (new_attr_name && strcmp(entry->name->str, new_attr_name->chars) == 0) {
            // Store the new attribute value
            void* new_field = (char*)new_data + entry->byte_offset;
            store_value_at_offset(new_field, new_attr_value, new_attr_type);
            log_debug("elmt_rebuild_with_new_shape: stored new attr '%s' at offset %lld",
                      new_attr_name->chars, entry->byte_offset);
        } else {
            // Copy from old element if it exists
            TypeId old_type_id;
            int64_t old_offset;
            bool found = find_field_in_shape(old_type->shape, entry->name->str,
                                            &old_type_id, &old_offset);

            if (found && old_type_id == entry->type->type_id) {
                void* old_field = (char*)old_elmt->data + old_offset;
                void* new_field = (char*)new_data + entry->byte_offset;
                int field_size = type_info[entry->type->type_id].byte_size;
                memcpy(new_field, old_field, field_size);

                // Update ref counts (immutable mode only)
                if (!is_inline) {
                    if (entry->type->type_id == LMD_TYPE_STRING ||
                        entry->type->type_id == LMD_TYPE_SYMBOL ||
                        entry->type->type_id == LMD_TYPE_BINARY) {
                        String* str = *(String**)new_field;
                        if (str) str->ref_cnt++;
                    }
                    else if (entry->type->type_id >= LMD_TYPE_LIST &&
                             entry->type->type_id <= LMD_TYPE_ELEMENT) {
                        Container* container = *(Container**)new_field;
                        if (container) container->ref_cnt++;
                    }
                }
            }
        }
        entry = entry->next;
    }

    Element* result_elmt = old_elmt;

    if (!is_inline) {
        // Immutable mode - create new element structure
        result_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
        if (!result_elmt) {
            log_error("elmt_rebuild_with_new_shape: failed to allocate new element");
            return ItemError;
        }
        memcpy(result_elmt, old_elmt, sizeof(Element));
        result_elmt->ref_cnt = 0;
    }

    // Create or update TypeElmt
    TypeElmt* new_type;
    if (old_type->type_index == -1 || old_type == &EmptyElmt || !is_inline) {
        // Create new TypeElmt
        new_type = (TypeElmt*)alloc_type(pool_, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
        new_type->name = old_type->name;  // Keep same element name
        new_type->shape = new_shape;
        new_type->length = builder->field_count;
        new_type->byte_size = new_byte_size;
        new_type->content_length = old_type->content_length;
        new_type->type_index = type_list_->length;

        // Find last entry
        new_type->last = new_shape;
        while (new_type->last && new_type->last->next) {
            new_type->last = new_type->last->next;
        }

        arraylist_append(type_list_, new_type);
        result_elmt->type = new_type;
    } else {
        // Inline mode - mutate existing TypeElmt
        old_type->shape = new_shape;
        old_type->length = builder->field_count;
        old_type->byte_size = new_byte_size;

        // Find last entry
        old_type->last = new_shape;
        while (old_type->last && old_type->last->next) {
            old_type->last = old_type->last->next;
        }
    }

    // Free old data (inline mode only), replace with new
    if (is_inline && old_elmt->data) {
        pool_free(pool_, old_elmt->data);
    }
    result_elmt->data = new_data;
    result_elmt->data_cap = new_byte_size;

    log_debug("elmt_rebuild_with_new_shape: success");
    return {.element = result_elmt};
}

Item MarkEditor::elmt_update_attr_immutable(Element* old_elmt, String* attr_name, Item value) {
    log_debug("elmt_update_attr_immutable: attr='%s'", attr_name->chars);

    TypeElmt* old_type = (TypeElmt*)old_elmt->type;
    TypeId value_type = get_type_id(value);
    TypeId existing_type;
    int64_t existing_offset;
    bool attr_exists = find_field_in_shape(old_type->shape, attr_name->chars,
                                          &existing_type, &existing_offset);

    if (attr_exists && existing_type == value_type) {
        // Same shape - create new element with copied data
        Element* new_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
        if (!new_elmt) return ItemError;

        memcpy(new_elmt, old_elmt, sizeof(Element));
        new_elmt->ref_cnt = 0;

        if (old_type->byte_size > 0) {
            new_elmt->data = pool_calloc(pool_, old_type->byte_size);
            if (!new_elmt->data) return ItemError;

            memcpy(new_elmt->data, old_elmt->data, old_type->byte_size);
            new_elmt->data_cap = old_type->byte_size;

            // Update the changed attribute
            void* attr_ptr = (char*)new_elmt->data + existing_offset;
            store_value_at_offset(attr_ptr, value, value_type);
        }

        return {.element = new_elmt};

    } else {
        // Different shape - rebuild
        Element* new_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
        if (!new_elmt) return ItemError;

        memcpy(new_elmt, old_elmt, sizeof(Element));
        new_elmt->ref_cnt = 0;

        ShapeBuilder builder = shape_builder_init_element(shape_pool_, old_type->name.str);
        shape_builder_import_shape(&builder, old_type->shape);

        if (attr_exists) {
            shape_builder_remove_field(&builder, attr_name->chars);
        }
        shape_builder_add_field(&builder, attr_name->chars, value_type);

        return elmt_rebuild_with_new_shape(new_elmt, &builder, false, attr_name, value);
    }
}

Item MarkEditor::elmt_update_attr_batch(Item element, int count, ...) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_update_attr_batch: not an element");
        return ItemError;
    }

    if (count <= 0) {
        log_warn("elmt_update_attr_batch: count <= 0");
        return element;
    }

    Element* target_elmt = element.element;
    TypeElmt* elmt_type = (TypeElmt*)target_elmt->type;

    ShapeBuilder builder = shape_builder_init_element(shape_pool_, elmt_type->name.str);
    shape_builder_import_shape(&builder, elmt_type->shape);

    // First pass: collect and deep copy values
    struct AttrUpdate {
        const char* attr_name;
        Item value;
        TypeId value_type;
    };

    // Use stack-allocated array for batch updates
    if (count > MAX_BATCH_UPDATES) {
        log_error("elmt_update_attr_batch: count %d exceeds max %d", count, MAX_BATCH_UPDATES);
        return ItemError;
    }
    AttrUpdate updates[MAX_BATCH_UPDATES];
    int update_count = 0;

    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++) {
        AttrUpdate update;
        update.attr_name = va_arg(args, const char*);
        update.value = va_arg(args, Item);

        // Ensure value is in target arena (deep copy if external)
        if (!builder_->is_in_arena(update.value)) {
            log_debug("elmt_update_attr_batch: value for attr '%s' not in arena, deep copying", update.attr_name);
            update.value = builder_->deep_copy(update.value);
        }

        update.value_type = get_type_id(update.value);
        updates[update_count++] = update;

        if (shape_builder_has_field(&builder, updates[update_count-1].attr_name)) {
            shape_builder_remove_field(&builder, update.attr_name);
        }
        shape_builder_add_field(&builder, update.attr_name, update.value_type);
    }

    va_end(args);

    if (mode_ == EDIT_MODE_INLINE) {
        return elmt_rebuild_with_new_shape(target_elmt, &builder, true);
    } else {
        Element* new_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
        if (!new_elmt) return ItemError;
        memcpy(new_elmt, target_elmt, sizeof(Element));
        new_elmt->ref_cnt = 0;
        return elmt_rebuild_with_new_shape(new_elmt, &builder, false);
    }
}

Item MarkEditor::elmt_delete_attr(Item element, const char* attr_name) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_delete_attr: not an element");
        return ItemError;
    }

    String* attr_str = ensure_string_key(attr_name);
    if (!attr_str) {
        log_error("elmt_delete_attr: invalid attribute name");
        return ItemError;
    }

    return elmt_delete_attr(element, attr_str);
}

Item MarkEditor::elmt_delete_attr(Item element, String* attr_name) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_delete_attr: not an element");
        return ItemError;
    }

    if (!attr_name) {
        log_error("elmt_delete_attr: null attribute name");
        return ItemError;
    }

    if (mode_ == EDIT_MODE_INLINE) {
        return elmt_delete_attr_inline(element.element, attr_name);
    } else {
        return elmt_delete_attr_immutable(element.element, attr_name);
    }
}

Item MarkEditor::elmt_delete_attr_inline(Element* elmt, String* attr_name) {
    TypeElmt* elmt_type = (TypeElmt*)elmt->type;

    if (!find_field_in_shape(elmt_type->shape, attr_name->chars, nullptr, nullptr)) {
        log_warn("elmt_delete_attr_inline: attribute '%s' not found", attr_name->chars);
        return {.element = elmt};
    }

    ShapeBuilder builder = shape_builder_init_element(shape_pool_, elmt_type->name.str);
    shape_builder_import_shape(&builder, elmt_type->shape);
    shape_builder_remove_field(&builder, attr_name->chars);

    return elmt_rebuild_with_new_shape(elmt, &builder, true);
}

Item MarkEditor::elmt_delete_attr_immutable(Element* old_elmt, String* attr_name) {
    TypeElmt* old_type = (TypeElmt*)old_elmt->type;

    if (!find_field_in_shape(old_type->shape, attr_name->chars, nullptr, nullptr)) {
        log_warn("elmt_delete_attr_immutable: attribute '%s' not found", attr_name->chars);
        return {.element = old_elmt};
    }

    Element* new_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
    if (!new_elmt) return ItemError;

    memcpy(new_elmt, old_elmt, sizeof(Element));
    new_elmt->ref_cnt = 0;

    ShapeBuilder builder = shape_builder_init_element(shape_pool_, old_type->name.str);
    shape_builder_import_shape(&builder, old_type->shape);
    shape_builder_remove_field(&builder, attr_name->chars);

    return elmt_rebuild_with_new_shape(new_elmt, &builder, false);
}

Item MarkEditor::elmt_insert_child(Item element, int index, Item child) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_insert_child: not an element");
        return ItemError;
    }

    Element* elmt = element.element;

    // Normalize index (-1 means append)
    if (index < 0) {
        index = elmt->length;
    }
    if (index > elmt->length) {
        log_error("elmt_insert_child: index out of bounds");
        return ItemError;
    }

    // Ensure child is in target arena (deep copy if external)
    if (!builder_->is_in_arena(child)) {
        log_debug("elmt_insert_child: child not in arena, deep copying");
        child = builder_->deep_copy(child);
    }

    if (mode_ == EDIT_MODE_INLINE) {
        // Inline mode - resize and insert in-place
        int64_t new_length = elmt->length + 1;

        if (new_length > elmt->capacity) {
            // Resize children array
            int64_t new_capacity = elmt->capacity ? elmt->capacity * 2 : 8;
            Item* new_items = (Item*)realloc(elmt->items, new_capacity * sizeof(Item));
            if (!new_items) {
                log_error("elmt_insert_child: realloc failed");
                return ItemError;
            }
            elmt->items = new_items;
            elmt->capacity = new_capacity;
        }

        // Shift children to make space
        for (int64_t i = elmt->length; i > index; i--) {
            elmt->items[i] = elmt->items[i - 1];
        }

        // Insert new child
        elmt->items[index] = child;
        elmt->length = new_length;

        // Update TypeElmt content_length
        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        elmt_type->content_length = new_length;

        return {.element = elmt};

    } else {
        // Immutable mode - create new element with new children array
        int64_t new_length = elmt->length + 1;
        Item* new_items = (Item*)arena_alloc(arena_, new_length * sizeof(Item));
        if (!new_items) return ItemError;

        // Copy children before insertion point
        for (int64_t i = 0; i < index; i++) {
            new_items[i] = elmt->items[i];
        }

        // Insert new child
        new_items[index] = child;

        // Copy children after insertion point
        for (int64_t i = index; i < elmt->length; i++) {
            new_items[i + 1] = elmt->items[i];
        }

        return elmt_copy_with_new_children(elmt, new_items, new_length);
    }
}

Item MarkEditor::elmt_insert_children(Item element, int index, int count, Item* children) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_insert_children: not an element");
        return ItemError;
    }

    if (count <= 0 || !children) {
        log_warn("elmt_insert_children: invalid arguments");
        return element;
    }

    Element* elmt = element.element;

    // Normalize index
    if (index < 0) {
        index = elmt->length;
    }
    if (index > elmt->length) {
        log_error("elmt_insert_children: index out of bounds");
        return ItemError;
    }

    // Deep copy external children
    Item* copied_children = (Item*)arena_alloc(arena_, count * sizeof(Item));
    if (!copied_children) return ItemError;

    for (int i = 0; i < count; i++) {
        if (!builder_->is_in_arena(children[i])) {
            log_debug("elmt_insert_children: child %d not in arena, deep copying", i);
            copied_children[i] = builder_->deep_copy(children[i]);
        } else {
            copied_children[i] = children[i];
        }
    }

    if (mode_ == EDIT_MODE_INLINE) {
        int64_t new_length = elmt->length + count;

        if (new_length > elmt->capacity) {
            int64_t new_capacity = (elmt->capacity ? elmt->capacity : 8);
            while (new_capacity < new_length) {
                new_capacity *= 2;
            }
            Item* new_items = (Item*)realloc(elmt->items, new_capacity * sizeof(Item));
            if (!new_items) return ItemError;
            elmt->items = new_items;
            elmt->capacity = new_capacity;
        }

        // Shift existing children
        for (int64_t i = elmt->length - 1; i >= index; i--) {
            elmt->items[i + count] = elmt->items[i];
        }

        // Insert new children (use copied versions)
        for (int i = 0; i < count; i++) {
            elmt->items[index + i] = copied_children[i];
        }

        elmt->length = new_length;

        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        elmt_type->content_length = new_length;

        return {.element = elmt};

    } else {
        int64_t new_length = elmt->length + count;
        Item* new_items = (Item*)arena_alloc(arena_, new_length * sizeof(Item));
        if (!new_items) return ItemError;

        // Copy before
        for (int64_t i = 0; i < index; i++) {
            new_items[i] = elmt->items[i];
        }

        // Insert new (use copied versions)
        for (int i = 0; i < count; i++) {
            new_items[index + i] = copied_children[i];
        }

        // Copy after
        for (int64_t i = index; i < elmt->length; i++) {
            new_items[i + count] = elmt->items[i];
        }

        return elmt_copy_with_new_children(elmt, new_items, new_length);
    }
}

Item MarkEditor::elmt_delete_child(Item element, int index) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_delete_child: not an element");
        return ItemError;
    }

    Element* elmt = element.element;

    if (index < 0 || index >= elmt->length) {
        log_error("elmt_delete_child: index out of bounds");
        return ItemError;
    }

    if (mode_ == EDIT_MODE_INLINE) {
        // Shift children down
        for (int64_t i = index; i < elmt->length - 1; i++) {
            elmt->items[i] = elmt->items[i + 1];
        }

        elmt->length--;

        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        elmt_type->content_length = elmt->length;

        return {.element = elmt};

    } else {
        int64_t new_length = elmt->length - 1;
        Item* new_items = nullptr;

        if (new_length > 0) {
            new_items = (Item*)arena_alloc(arena_, new_length * sizeof(Item));
            if (!new_items) return ItemError;

            // Copy before
            for (int64_t i = 0; i < index; i++) {
                new_items[i] = elmt->items[i];
            }

            // Copy after
            for (int64_t i = index + 1; i < elmt->length; i++) {
                new_items[i - 1] = elmt->items[i];
            }
        }

        return elmt_copy_with_new_children(elmt, new_items, new_length);
    }
}

Item MarkEditor::elmt_delete_children(Item element, int start, int end) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_delete_children: not an element");
        return ItemError;
    }

    Element* elmt = element.element;

    if (start < 0 || end > elmt->length || start >= end) {
        log_error("elmt_delete_children: invalid range");
        return ItemError;
    }

    int delete_count = end - start;
    int64_t new_length = elmt->length - delete_count;

    if (mode_ == EDIT_MODE_INLINE) {
        // Shift children down
        for (int64_t i = start; i < elmt->length - delete_count; i++) {
            elmt->items[i] = elmt->items[i + delete_count];
        }

        elmt->length = new_length;

        TypeElmt* elmt_type = (TypeElmt*)elmt->type;
        elmt_type->content_length = new_length;

        return {.element = elmt};

    } else {
        Item* new_items = nullptr;

        if (new_length > 0) {
            new_items = (Item*)arena_alloc(arena_, new_length * sizeof(Item));
            if (!new_items) return ItemError;

            // Copy before
            for (int64_t i = 0; i < start; i++) {
                new_items[i] = elmt->items[i];
            }

            // Copy after
            for (int64_t i = end; i < elmt->length; i++) {
                new_items[i - delete_count] = elmt->items[i];
            }
        }

        return elmt_copy_with_new_children(elmt, new_items, new_length);
    }
}

Item MarkEditor::elmt_replace_child(Item element, int index, Item new_child) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_replace_child: not an element");
        return ItemError;
    }

    Element* elmt = element.element;

    if (index < 0 || index >= elmt->length) {
        log_error("elmt_replace_child: index out of bounds");
        return ItemError;
    }

    if (mode_ == EDIT_MODE_INLINE) {
        elmt->items[index] = new_child;
        return {.element = elmt};
    } else {
        Item* new_items = (Item*)arena_alloc(arena_, elmt->length * sizeof(Item));
        if (!new_items) return ItemError;

        memcpy(new_items, elmt->items, elmt->length * sizeof(Item));
        new_items[index] = new_child;

        return elmt_copy_with_new_children(elmt, new_items, elmt->length);
    }
}

Item MarkEditor::elmt_copy_with_new_children(Element* old_elmt, Item* new_children, int64_t new_length) {
    // Create new element structure
    Element* new_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
    if (!new_elmt) return ItemError;

    memcpy(new_elmt, old_elmt, sizeof(Element));
    new_elmt->ref_cnt = 0;

    // Set new children
    new_elmt->items = new_children;
    new_elmt->length = new_length;
    new_elmt->capacity = new_length;

    // Need new TypeElmt with updated content_length
    TypeElmt* old_type = (TypeElmt*)old_elmt->type;
    TypeElmt* new_type = (TypeElmt*)alloc_type(pool_, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
    if (!new_type) return ItemError;

    memcpy(new_type, old_type, sizeof(TypeElmt));
    new_type->content_length = new_length;
    new_type->type_index = type_list_->length;
    arraylist_append(type_list_, new_type);

    // Copy attribute data (if any)
    if (old_type->byte_size > 0) {
        new_elmt->data = pool_calloc(pool_, old_type->byte_size);
        if (!new_elmt->data) return ItemError;
        memcpy(new_elmt->data, old_elmt->data, old_type->byte_size);
        new_elmt->data_cap = old_type->byte_size;
    }

    new_elmt->type = new_type;

    return {.element = new_elmt};
}

Item MarkEditor::elmt_rename(Item element, const char* new_tag_name) {
    if (!element.element || element.element->type_id != LMD_TYPE_ELEMENT) {
        log_error("elmt_rename: not an element");
        return ItemError;
    }

    Element* old_elmt = element.element;
    TypeElmt* old_type = (TypeElmt*)old_elmt->type;

    // Build new shape with new element name
    ShapeBuilder builder = shape_builder_init_element(shape_pool_, new_tag_name);
    shape_builder_import_shape(&builder, old_type->shape);

    if (mode_ == EDIT_MODE_INLINE) {
        return elmt_rebuild_with_new_shape(old_elmt, &builder, true);
    } else {
        Element* new_elmt = (Element*)arena_alloc(arena_, sizeof(Element));
        if (!new_elmt) return ItemError;
        memcpy(new_elmt, old_elmt, sizeof(Element));
        new_elmt->ref_cnt = 0;
        return elmt_rebuild_with_new_shape(new_elmt, &builder, false);
    }
}

//==============================================================================
// ARRAY OPERATIONS
//==============================================================================

Item MarkEditor::array_set(Item array, int index, Item value) {
    TypeId array_type = get_type_id(array);

    if (array_type == LMD_TYPE_ARRAY) {
        Array* arr = array.array;

        if (index < 0 || index >= arr->length) {
            log_error("array_set: index out of bounds");
            return ItemError;
        }

        // Ensure value is in target arena (deep copy if external)
        if (!builder_->is_in_arena(value)) {
            log_debug("array_set: value not in arena, deep copying");
            value = builder_->deep_copy(value);
        }

        if (mode_ == EDIT_MODE_INLINE) {
            arr->items[index] = value;
            return {.array = arr};
        } else {
            // Create new array
            Array* new_arr = (Array*)arena_alloc(arena_, sizeof(Array));
            if (!new_arr) return ItemError;

            memcpy(new_arr, arr, sizeof(Array));
            new_arr->ref_cnt = 0;

            // Copy items
            new_arr->items = (Item*)arena_alloc(arena_, arr->length * sizeof(Item));
            if (!new_arr->items) return ItemError;

            memcpy(new_arr->items, arr->items, arr->length * sizeof(Item));
            new_arr->items[index] = value;
            new_arr->capacity = arr->length;

            return {.array = new_arr};
        }
    }

    log_error("array_set: unsupported array type %s", get_type_name(array_type));
    return ItemError;
}

Item MarkEditor::array_insert(Item array, int index, Item value) {
    TypeId array_type = get_type_id(array);

    if (array_type == LMD_TYPE_ARRAY || array_type == LMD_TYPE_ELEMENT || array_type == LMD_TYPE_LIST) {
        // All these types share the same memory layout for items/length/capacity
        Array* arr = array.array;  // Works for List and Element too since they share layout

        if (index < 0) index = arr->length;
        if (index > arr->length) {
            log_error("array_insert: index out of bounds");
            return ItemError;
        }

        // Ensure value is in target arena (deep copy if external)
        if (!builder_->is_in_arena(value)) {
            log_debug("array_insert: value not in arena, deep copying");
            value = builder_->deep_copy(value);
        }

        if (mode_ == EDIT_MODE_INLINE) {
            int64_t new_length = arr->length + 1;

            if (new_length > arr->capacity) {
                int64_t new_capacity = arr->capacity ? arr->capacity * 2 : 8;
                Item* new_items = (Item*)realloc(arr->items, new_capacity * sizeof(Item));
                if (!new_items) return ItemError;
                arr->items = new_items;
                arr->capacity = new_capacity;
            }

            // Shift
            for (int64_t i = arr->length; i > index; i--) {
                arr->items[i] = arr->items[i - 1];
            }

            arr->items[index] = value;
            arr->length = new_length;

            return array;  // return original with modifications

        } else {
            // COW mode - need to create a new array
            int64_t new_length = arr->length + 1;

            Array* new_arr = (Array*)arena_alloc(arena_, sizeof(Array));
            if (!new_arr) return ItemError;

            memcpy(new_arr, arr, sizeof(Array));
            new_arr->ref_cnt = 0;
            new_arr->length = new_length;
            new_arr->capacity = new_length;

            new_arr->items = (Item*)arena_alloc(arena_, new_length * sizeof(Item));
            if (!new_arr->items) return ItemError;

            // Copy before
            for (int64_t i = 0; i < index; i++) {
                new_arr->items[i] = arr->items[i];
            }

            new_arr->items[index] = value;

            // Copy after
            for (int64_t i = index; i < arr->length; i++) {
                new_arr->items[i + 1] = arr->items[i];
            }

            return {.array = new_arr};
        }
    }

    log_error("array_insert: unsupported array type %s", get_type_name(array_type));
    return ItemError;
}

Item MarkEditor::array_delete(Item array, int index) {
    TypeId array_type = get_type_id(array);

    if (array_type == LMD_TYPE_ARRAY) {
        Array* arr = array.array;

        if (index < 0 || index >= arr->length) {
            log_error("array_delete: index out of bounds");
            return ItemError;
        }

        if (mode_ == EDIT_MODE_INLINE) {
            // Shift down
            for (int64_t i = index; i < arr->length - 1; i++) {
                arr->items[i] = arr->items[i + 1];
            }

            arr->length--;
            return {.array = arr};

        } else {
            int64_t new_length = arr->length - 1;

            Array* new_arr = (Array*)arena_alloc(arena_, sizeof(Array));
            if (!new_arr) return ItemError;

            memcpy(new_arr, arr, sizeof(Array));
            new_arr->ref_cnt = 0;
            new_arr->length = new_length;
            new_arr->capacity = new_length;

            if (new_length > 0) {
                new_arr->items = (Item*)arena_alloc(arena_, new_length * sizeof(Item));
                if (!new_arr->items) return ItemError;

                // Copy before
                for (int64_t i = 0; i < index; i++) {
                    new_arr->items[i] = arr->items[i];
                }

                // Copy after
                for (int64_t i = index + 1; i < arr->length; i++) {
                    new_arr->items[i - 1] = arr->items[i];
                }
            } else {
                new_arr->items = nullptr;
            }

            return {.array = new_arr};
        }
    }

    log_error("array_delete: unsupported array type %s", get_type_name(array_type));
    return ItemError;
}

Item MarkEditor::array_append(Item array, Item value) {
    return array_insert(array, -1, value);
}
