#include "mark_editor.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"
#include "../../lib/log.h"
#include "../../lib/arena.h"
#include "../../lib/hashmap.h"
#include <string.h>
#include <stdlib.h>
#include <new>
#include "../../lib/memtrack.h"

// Maximum number of batch updates supported
#define MAX_BATCH_UPDATES 64

extern TypeMap EmptyMap;
extern TypeElmt EmptyElmt;
extern TypeInfo type_info[];
DomElement* element_dom_map_lookup(HashMap* map, Element* elem);
void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem);

static bool mark_editor_should_preserve_ui_dom_child(Item child) {
    TypeId type_id = get_type_id(child);
    if (type_id == LMD_TYPE_ELEMENT && child.element) {
        DomElement* elem = element_to_dom_element(child.element);
        return elem && elem->node_type == DOM_NODE_ELEMENT &&
            !elem->is_synthetic() && dom_element_to_element(elem) == child.element;
    }
    if (type_id == LMD_TYPE_STRING) {
        String* s = child.get_safe_string();
        if (!s) return false;
        DomText* text = string_to_dom_text(s);
        return text && text->node_type == DOM_NODE_TEXT &&
            text->native_string == s;
    }
    return false;
}

static DomElement* mark_editor_lookup_ui_element_child(DomElement* parent,
                                                        Element* child_element) {
    if (!parent || !parent->doc || !child_element) return nullptr;
    DomElement* embedded = element_to_dom_element(child_element);
    if (dom_document_owns_node_storage(parent->doc, embedded) &&
        embedded->node_type == DOM_NODE_ELEMENT && embedded->doc == parent->doc &&
        dom_element_to_element(embedded) == child_element) {
        return embedded;
    }
    if (parent->doc->element_dom_map) {
        return element_dom_map_lookup(parent->doc->element_dom_map, child_element);
    }
    return nullptr;
}

static DomNode* mark_editor_take_relinked_ui_child(DomNode* old_first,
                                                    DomElement* parent,
                                                    Item child) {
    if (!parent) return nullptr;
    TypeId type_id = get_type_id(child);
    for (DomNode* candidate = old_first; candidate; candidate = candidate->next_sibling) {
        if (candidate->parent != parent) continue;
        if (type_id == LMD_TYPE_ELEMENT && child.element && candidate->is_element() &&
            candidate == static_cast<DomNode*>(
                mark_editor_lookup_ui_element_child(parent, child.element))) {
            candidate->parent = nullptr;
            return candidate;
        }
        if (type_id == LMD_TYPE_STRING && candidate->is_text()) {
            DomText* text = candidate->as_text();
            if (text && text->native_string == child.get_safe_string()) {
                candidate->parent = nullptr;
                return candidate;
            }
        }
    }
    return nullptr;
}

static bool mark_editor_synthetic_subtree_contains_item(DomNode* node, Item item) {
    if (!node) return false;
    TypeId type_id = get_type_id(item);
    if (node->is_element()) {
        DomElement* element = node->as_element();
        if (type_id == LMD_TYPE_ELEMENT && item.element && !element->is_synthetic() &&
            dom_element_to_element(element) == item.element) {
            return true;
        }
        for (DomNode* child = element->first_child; child; child = child->next_sibling) {
            if (mark_editor_synthetic_subtree_contains_item(child, item)) return true;
        }
        return false;
    }
    if (type_id != LMD_TYPE_STRING || !node->is_text()) return false;
    return node->as_text()->native_string == item.get_safe_string();
}

static DomNode* mark_editor_find_synthetic_child_proxy(DomNode* old_first,
                                                        DomElement* parent, Item item) {
    for (DomNode* candidate = old_first; candidate; candidate = candidate->next_sibling) {
        if (!candidate->is_element() || candidate->parent != parent ||
            !candidate->as_element()->is_synthetic()) {
            continue;
        }
        if (mark_editor_synthetic_subtree_contains_item(candidate, item)) return candidate;
    }
    return nullptr;
}

static DomNode* mark_editor_create_relinked_ui_child(DomElement* parent, Item child) {
    if (!parent || !parent->doc) return nullptr;
    TypeId type_id = get_type_id(child);
    if (type_id == LMD_TYPE_ELEMENT && child.element) {
        DomElement* element = mark_editor_lookup_ui_element_child(parent, child.element);
        TypeElmt* type = (TypeElmt*)child.element->type;
        const char* tag_name = type ? type->name.str : nullptr;
        if (!element) {
            DomElement* storage = element_to_dom_element(child.element);
            if (dom_document_owns_node_storage(parent->doc, storage) &&
                storage->node_type == DOM_NODE_ELEMENT && !storage->doc &&
                !storage->tag_name && dom_element_to_element(storage) == child.element &&
                tag_name) {
                // UI-mode MarkBuilder reserves a DomElement prefix for every
                // new Element. A parser-created fragment has not joined a
                // document yet, so initialize that reserved storage here
                // instead of deriving a wrapper from an unrelated Element.
                element = DomElement::create_in(storage, parent->doc, tag_name,
                                                child.element);
            }
        }
        if (!element && tag_name) {
            // Fragment parsers create plain Elements even for UI documents.
            // Give those values a registered wrapper instead of treating the
            // bytes before the Element as an embedded DomElement.
            element = DomElement::create(parent->doc, tag_name, child.element);
            if (element && parent->doc->element_dom_map) {
                element_dom_map_insert(parent->doc->element_dom_map, child.element, element);
            }
        }
        if (!element) {
            log_error("mark_editor_dom_relink: child Element has no DOM wrapper");
            return nullptr;
        }
        return static_cast<DomNode*>(element);
    }
    if (type_id != LMD_TYPE_STRING) return nullptr;

    String* string_value = child.get_safe_string();
    if (!string_value) return nullptr;
    DomText* candidate = string_to_dom_text(string_value);
    // Only a fat DomText-String allocation may be recovered from the String
    // address; parser-owned and ordinary strings require a backed wrapper.
    if (dom_document_owns_node_storage(parent->doc, candidate) &&
        candidate->node_type == DOM_NODE_TEXT &&
        candidate->native_string == string_value) {
        // MarkBuilder creates this fat wrapper before it joins a DOM sibling
        // chain. Register its generation here so a later textContent replace
        // can pin the new text node through the lifecycle registry.
        if (!candidate->id) {
            candidate->id = dom_document_alloc_node_id(parent->doc);
        }
        size_t primary_size = sizeof(DomText) + sizeof(String) +
                              string_value->len + 1;
        if (!dom_node_registry_register(parent->doc, candidate,
                                        primary_size, true)) {
            return nullptr;
        }
        return static_cast<DomNode*>(candidate);
    }
    return static_cast<DomNode*>(DomText::create(string_value, parent));
}

static int mark_editor_relinked_node_index(ArrayList* nodes, DomNode* candidate) {
    if (!nodes || !candidate) return -1;
    for (int i = 0; i < arraylist_length(nodes); i++) {
        if ((DomNode*)arraylist_get(nodes, i) == candidate) return i;
    }
    return -1;
}

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
    , ui_mode_(input->ui_mode)
    , current_version_(nullptr)
    , version_head_(nullptr)
    , next_version_num_(0)
{
    // Create builder for constructing new structures
    builder_ = mark_builder_create(input);

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
        mark_builder_destroy(builder_);
        builder_ = nullptr;
    }

    log_debug("MarkEditor destroyed");
}

//------------------------------------------------------------------------------
// Heap factory (audited boundary for `new MarkEditor` / `delete editor`)
//------------------------------------------------------------------------------

MarkEditor* mark_editor_create(Input* input, EditMode mode) {
    if (!input) return nullptr;
    MarkEditor* editor = (MarkEditor*)mem_alloc(sizeof(MarkEditor), MEM_CAT_EVAL);
    if (!editor) return nullptr;
    new (editor) MarkEditor(input, mode); // NEW_DELETE_OK: single audited construction boundary for MarkEditor.
    return editor;
}

void mark_editor_destroy(MarkEditor* editor) {
    if (!editor) return;
    editor->~MarkEditor(); // NEW_DELETE_OK: paired with mark_editor_create.
    mem_free(editor);
}

//==============================================================================
// DOM Linked-List Sync (ui_mode only)
//==============================================================================

/**
 * Rebuild the DOM first_child/last_child/next_sibling/prev_sibling linked list
 * from the Element's items[] array. Called after inline child mutations in ui_mode.
 *
 * In ui_mode, Element children are embedded inside DomElement. String children may
 * be embedded inside DomText, or may need a normal backed DomText wrapper when the
 * source tree was parsed before editing.
 */
void MarkEditor::dom_relink_children(Element* parent_elem) {
    DomElement* parent = element_to_dom_element(parent_elem);
    if (!parent) return;
    DomNode* old_first = parent->first_child;
    ArrayList* old_nodes = arraylist_new(8);
    ArrayList* relinked_nodes = arraylist_new((int)parent_elem->length);
    if (!old_nodes || !relinked_nodes) {
        log_error("mark_editor_dom_relink: failed to allocate child list");
        if (old_nodes) arraylist_free(old_nodes);
        if (relinked_nodes) arraylist_free(relinked_nodes);
        return;
    }

    for (DomNode* node = old_first; node; node = node->next_sibling) {
        if (!arraylist_append(old_nodes, node)) {
            log_error("mark_editor_dom_relink: failed to snapshot child list");
            arraylist_free(old_nodes);
            arraylist_free(relinked_nodes);
            return;
        }
    }

    // Select every node before rewriting links: an inline Mark mutation must
    // retain the existing DOM wrappers for unchanged backing children.  This
    // lets the DOM removal bridge unlink the one deleted wrapper afterwards.
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item child = parent_elem->items[i];
        DomNode* node = mark_editor_find_synthetic_child_proxy(old_first, parent, child);
        if (node && mark_editor_relinked_node_index(relinked_nodes, node) >= 0) {
            continue;
        }
        if (!node) node = mark_editor_take_relinked_ui_child(old_first, parent, child);
        if (!node) {
            node = mark_editor_create_relinked_ui_child(parent, child);
        }
        if (node && !arraylist_append(relinked_nodes, node)) {
            log_error("mark_editor_dom_relink: failed to record child node");
            arraylist_free(old_nodes);
            arraylist_free(relinked_nodes);
            return;
        }
    }

    // layout-only wrappers own authored descendants in the visual tree. Keeping
    // them opaque prevents a DOM mutation from duplicating generated table boxes.
    // Mark-backed edits rebuild from Element::items and would otherwise drop
    // DOM-only nodes such as createComment() results from the sibling chain.
    // Reinsert each survivor after its nearest preceding old sibling so new
    // backed children retain the DOM position established by the mutation.
    for (int i = 0; i < arraylist_length(old_nodes); i++) {
        DomNode* node = (DomNode*)arraylist_get(old_nodes, i);
        if (!node || node->parent != parent ||
            mark_editor_relinked_node_index(relinked_nodes, node) >= 0) continue;
        int insert_index = -1;
        for (int prev = i - 1; prev >= 0; prev--) {
            int previous_index = mark_editor_relinked_node_index(relinked_nodes,
                (DomNode*)arraylist_get(old_nodes, prev));
            if (previous_index >= 0) {
                insert_index = previous_index + 1;
                break;
            }
        }
        if (insert_index < 0) {
            for (int next = i + 1; next < arraylist_length(old_nodes); next++) {
                int next_index = mark_editor_relinked_node_index(relinked_nodes,
                    (DomNode*)arraylist_get(old_nodes, next));
                if (next_index >= 0) {
                    insert_index = next_index;
                    break;
                }
            }
        }
        if (insert_index < 0) insert_index = 0;
        if (!arraylist_insert(relinked_nodes, insert_index, node)) {
            log_error("mark_editor_dom_relink: failed to preserve DOM-only child");
            arraylist_free(old_nodes);
            arraylist_free(relinked_nodes);
            return;
        }
    }

    parent->first_child = nullptr;
    parent->last_child = nullptr;
    DomNode* prev = nullptr;
    for (int i = 0; i < arraylist_length(relinked_nodes); i++) {
        DomNode* node = (DomNode*)arraylist_get(relinked_nodes, i);
        node->parent = parent;
        node->prev_sibling = prev;
        node->next_sibling = nullptr;
        if (prev) {
            prev->next_sibling = node;
        } else {
            parent->first_child = node;
        }
        parent->last_child = node;
        prev = node;
    }
    arraylist_free(old_nodes);
    arraylist_free(relinked_nodes);
}

//==============================================================================
// Version Control Helpers
//==============================================================================

EditVersion* MarkEditor::create_version(Item root, const char* description) {
    EditVersion* version = (EditVersion*)pool_calloc(pool_, sizeof(EditVersion));
    if (!version) return nullptr;

    version->root = root;
    version->version_number = next_version_num_++;
    version->description = description ? mem_strdup(description, MEM_CAT_SYSTEM) : nullptr;
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
            mem_free((void*)current->description);
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
    // version tracking works in both inline and immutable modes
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
        printf("Version control not available in inline mode\n"); // PRINTF_OK: user-facing CLI output.
        return;
    }

    if (!version_head_) {
        printf("No versions committed yet\n"); // PRINTF_OK: user-facing CLI output.
        return;
    }

    EditVersion* v = version_head_;
    while (v) {
        printf("Version %d: %s %s\n", // PRINTF_OK: user-facing version listing.
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
        // Packed map fields carry the int lane, not an IEEE carrier.
        *(int64_t*)field_ptr = lambda_int_item_to_lane(value.item);
        break;
    case LMD_TYPE_INT64:
        *(int64_t*)field_ptr = value.get_int64();
        break;
    case LMD_TYPE_UINT64:
        *(uint64_t*)field_ptr = value.get_uint64();
        break;
    case LMD_TYPE_FLOAT:
        *(double*)field_ptr = value.get_double();
        break;
    case LMD_TYPE_DTIME:
        *(DateTime**)field_ptr = value.get_datetime_ptr();
        break;
    case LMD_TYPE_STRING: {
        *(String**)field_ptr = value.get_safe_string();
        break;
    }
    case LMD_TYPE_SYMBOL: {
        *(Symbol**)field_ptr = value.get_safe_symbol();
        break;
    }
    case LMD_TYPE_BINARY: {
        *(Binary**)field_ptr = value.get_safe_binary();
        break;
    }
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_RANGE:
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: {
        Container* container = value.container;
        *(Container**)field_ptr = container;
        break;
    }
    default:
        log_error("store_value_at_offset: unsupported type %s", get_type_name(type_id));
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
    return container_update_attr(map, key, value);
}
// ============================================================================
// ATTRIBUTE OPERATIONS — one path for every attribute-bearing container
// ============================================================================
// D2.6.6v2: `Element` extends `Map`, so a map and an element carry their
// attribute face — shape pointer, packed buffer, capacity — at the same
// offsets. These helpers therefore take the shared `Map` base and work for
// both; elements upcast at the call site. Only three things stay kind-aware,
// and each is isolated in one of the three helpers directly below.

// A container Item is a bare pointer whose kind is read back off the header, so
// one constructor serves maps and elements alike.
static inline Item container_item(Map* container) { return {.map = container}; }

// An immutable edit clones the container header. An element's header is longer
// than a map's (it carries the content list), so cloning `sizeof(Map)` would
// drop its children.
static size_t container_header_size(const Map* container) {
    return container->type_id == LMD_TYPE_ELEMENT ? sizeof(Element) : sizeof(Map);
}

// Element shapes are interned per tag name, map shapes by their fields alone,
// so the builder has to be seeded from the right side of the shape pool.
ShapeBuilder MarkEditor::container_shape_builder(const Map* container) {
    if (container->type_id == LMD_TYPE_ELEMENT) {
        return shape_builder_init_element(shape_pool_,
            ((TypeElmt*)container->type)->name.str);
    }
    return shape_builder_init_map(shape_pool_);
}

Map* MarkEditor::container_clone_header(const Map* container) {
    size_t size = container_header_size(container);
    Map* copy = (Map*)arena_alloc(arena_, size);
    if (!copy) {
        log_error("container_clone_header: failed to allocate container");
        return nullptr;
    }
    memcpy(copy, container, size);
    return copy;
}

// Rebuild a container's attribute buffer against a new shape. The changed
// field is NOT written here: it is left zeroed and the caller stores it once
// the new offsets are known, which is what lets one rebuild serve add, retype
// and delete alike.
Item MarkEditor::container_rebuild_with_new_shape(Map* old_container,
        ShapeBuilder* builder, bool is_inline) {
    bool is_element = old_container->type_id == LMD_TYPE_ELEMENT;
    log_debug("container_rebuild_with_new_shape: field_count=%zu, element=%d",
        builder->field_count, (int)is_element);

    // A NULL shape is the legitimate result of deleting the last field; only a
    // NULL with fields still pending is a real failure.
    ShapeEntry* new_shape = shape_builder_finalize(builder);
    if (!new_shape && builder->field_count > 0) {
        log_error("container_rebuild_with_new_shape: failed to finalize shape");
        return ItemError;
    }

    int64_t new_byte_size = 0;
    ShapeEntry* entry = new_shape;
    while (entry) {
        new_byte_size = entry->byte_offset + type_info[entry->type->type_id].byte_size;
        entry = entry->next;
    }

    // pool_calloc(0) returns NULL, so an emptied container legitimately ends
    // with a null buffer.
    void* new_data = pool_calloc(pool_, new_byte_size);
    if (!new_data && new_byte_size > 0) {
        log_error("container_rebuild_with_new_shape: allocation failed");
        return ItemError;
    }

    // Carry across every field the new shape shares with the old one at the
    // same type; anything added or retyped stays zero for the caller to fill.
    TypeMap* old_type = (TypeMap*)old_container->type;
    entry = new_shape;
    while (entry) {
        TypeId old_type_id;
        int64_t old_offset;
        if (find_field_in_shape(old_type->shape, entry->name->str,
                                &old_type_id, &old_offset) &&
                old_type_id == entry->type->type_id) {
            void* old_field = (char*)old_container->data + old_offset;
            void* new_field = (char*)new_data + entry->byte_offset;
            memcpy(new_field, old_field, type_info[entry->type->type_id].byte_size);
        }
        entry = entry->next;
    }

    Map* result = old_container;
    if (!is_inline) {
        result = container_clone_header(old_container);
        if (!result) return ItemError;
    }

    // An inline edit on a MAP mutates the descriptor in place: allocating and
    // interning a fresh shape on every edit is a measurable cost on this path.
    // Three cases opt out. `type_index == -1` is a shape that was never
    // registered and `EmptyMap` is the shared empty singleton, so neither may
    // be written. And an ELEMENT always takes a fresh TypeElmt, as it always
    // has: its descriptor is shared across the DOM by tag name, so mutating it
    // leaves sibling elements reading their buffers through a layout they were
    // never packed to. Letting elements share the map's in-place path was
    // measured and breaks 8 DOM editing fixtures (todo_toggle, todo_delete,
    // todo_text_input, todo_two_delete_clear, todo_perf_timing).
    if (old_type->type_index == -1 || old_type == &EmptyMap || !is_inline || is_element) {
        TypeMap* new_type;
        if (is_element) {
            TypeElmt* elmt_type = (TypeElmt*)alloc_type(pool_, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
            elmt_type->name = ((TypeElmt*)old_type)->name;
            elmt_type->content_length = ((TypeElmt*)old_type)->content_length;
            new_type = (TypeMap*)elmt_type;
        } else {
            new_type = (TypeMap*)alloc_type(pool_, LMD_TYPE_MAP, sizeof(TypeMap));
        }
        new_type->shape = new_shape;
        new_type->length = builder->field_count;
        new_type->byte_size = new_byte_size;
        new_type->type_index = type_list_->length;

        new_type->last = new_shape;
        while (new_type->last && new_type->last->next) {
            new_type->last = new_type->last->next;
        }
        typemap_hash_build(new_type, pool_);

        arraylist_append(type_list_, new_type);
        result->type = new_type;
    } else {
        // In-place: an element's `name` and `content_length` are not touched by
        // a shape change, so its descriptor carries them across unchanged.
        old_type->shape = new_shape;
        old_type->length = builder->field_count;
        old_type->byte_size = new_byte_size;

        old_type->last = new_shape;
        while (old_type->last && old_type->last->next) {
            old_type->last = old_type->last->next;
        }
        typemap_hash_build(old_type, pool_);
    }

    // Free old data (inline mode only), replace with new
    if (is_inline && old_container->data) {
        // In ui_mode, old data was arena-allocated during JIT execution
        // (via context->arena = result_arena). The MarkEditor's pool_ is a
        // different owner; calling pool_free here would release unrelated data.
        if (!ui_mode_) {
            pool_free(pool_, old_container->data);
        }
    }
    result->data = new_data;
    result->data_cap = new_byte_size;

    log_debug("container_rebuild_with_new_shape: success");
    return container_item(result);
}

// Store one value into a rebuilt container, now that its offsets are known.
void MarkEditor::container_store_field(Item rebuilt, const char* key, Item value,
        TypeId value_type) {
    Map* container = rebuilt.map;
    if (!container || !container->type || !container->data) return;
    TypeId field_type;
    int64_t field_offset;
    if (find_field_in_shape(((TypeMap*)container->type)->shape, key,
                            &field_type, &field_offset)) {
        store_value_at_offset((char*)container->data + field_offset, value, value_type);
    }
}

Item MarkEditor::container_update_attr_inline(Map* container, String* key, Item value) {
    TypeMap* type = (TypeMap*)container->type;
    TypeId value_type = get_type_id(value);

    log_debug("container_update_attr_inline: key='%s', value_type=%d", key->chars, value_type);

    TypeId existing_type;
    int64_t existing_offset;
    bool exists = find_field_in_shape(type->shape, key->chars,
                                      &existing_type, &existing_offset);

    if (exists && existing_type == value_type) {
        // same type — the slot is already the right width, so write in place
        store_value_at_offset((char*)container->data + existing_offset, value, value_type);
        return container_item(container);
    }

    ShapeBuilder builder = container_shape_builder(container);
    shape_builder_import_shape(&builder, type->shape);
    if (exists) shape_builder_remove_field(&builder, key->chars);
    shape_builder_add_field(&builder, key->chars, value_type);

    Item rebuilt = container_rebuild_with_new_shape(container, &builder, true);
    container_store_field(rebuilt, key->chars, value, value_type);
    return rebuilt;
}

Item MarkEditor::container_update_attr_immutable(Map* old_container, String* key, Item value) {
    TypeMap* old_type = (TypeMap*)old_container->type;
    TypeId value_type = get_type_id(value);

    log_debug("container_update_attr_immutable: key='%s'", key->chars);

    TypeId existing_type;
    int64_t existing_offset;
    bool exists = find_field_in_shape(old_type->shape, key->chars,
                                      &existing_type, &existing_offset);

    Map* new_container = container_clone_header(old_container);
    if (!new_container) return ItemError;

    if (exists && existing_type == value_type) {
        // Same shape — copy the buffer and overwrite the one slot. The shape
        // is unchanged, so the shared descriptor is kept as is.
        if (old_type->byte_size > 0) {
            new_container->data = pool_calloc(pool_, old_type->byte_size);
            if (!new_container->data) return ItemError;
            memcpy(new_container->data, old_container->data, old_type->byte_size);
            new_container->data_cap = old_type->byte_size;
            store_value_at_offset((char*)new_container->data + existing_offset,
                value, value_type);
        }
        return container_item(new_container);
    }

    ShapeBuilder builder = container_shape_builder(old_container);
    shape_builder_import_shape(&builder, old_type->shape);
    if (exists) shape_builder_remove_field(&builder, key->chars);
    shape_builder_add_field(&builder, key->chars, value_type);

    Item rebuilt = container_rebuild_with_new_shape(new_container, &builder, false);
    container_store_field(rebuilt, key->chars, value, value_type);
    return rebuilt;
}

Item MarkEditor::container_update_attr(Item container, String* key, Item value) {
    // Ensure value is in target arena (deep copy if external)
    if (!builder_->is_in_arena(value)) {
        log_debug("container_update_attr: value not in arena, deep copying");
        value = builder_->deep_copy(value);
    }
    return mode_ == EDIT_MODE_INLINE
        ? container_update_attr_inline(container.map, key, value)
        : container_update_attr_immutable(container.map, key, value);
}

Item MarkEditor::container_delete_attr(Item container_item_in, String* key) {
    Map* container = container_item_in.map;
    TypeMap* type = (TypeMap*)container->type;

    log_debug("container_delete_attr: key='%s'", key->chars);

    if (!find_field_in_shape(type->shape, key->chars, nullptr, nullptr)) {
        log_warn("container_delete_attr: field '%s' not found", key->chars);
        return container_item_in;  // unchanged
    }

    bool is_inline = mode_ == EDIT_MODE_INLINE;
    Map* target = container;
    if (!is_inline) {
        target = container_clone_header(container);
        if (!target) return ItemError;
    }

    ShapeBuilder builder = container_shape_builder(container);
    shape_builder_import_shape(&builder, type->shape);
    shape_builder_remove_field(&builder, key->chars);

    return container_rebuild_with_new_shape(target, &builder, is_inline);
}

// Batched attribute writes: one shape rebuild for the whole set, then one
// store pass over the new offsets.
Item MarkEditor::container_update_attr_batch(Item container_item_in, int count, va_list args) {
    struct AttrUpdate { const char* key; Item value; TypeId value_type; };

    if (count > MAX_BATCH_UPDATES) {
        log_error("container_update_attr_batch: count %d exceeds max %d", count, MAX_BATCH_UPDATES);
        return ItemError;
    }
    AttrUpdate updates[MAX_BATCH_UPDATES];

    Map* container = container_item_in.map;
    ShapeBuilder builder = container_shape_builder(container);
    shape_builder_import_shape(&builder, ((TypeMap*)container->type)->shape);

    for (int i = 0; i < count; i++) {
        AttrUpdate entry;
        entry.key = va_arg(args, const char*);
        entry.value = va_arg(args, Item);

        // Ensure value is in target arena (deep copy if external)
        if (!builder_->is_in_arena(entry.value)) {
            log_debug("container_update_attr_batch: value for '%s' not in arena, deep copying",
                entry.key);
            entry.value = builder_->deep_copy(entry.value);
        }
        entry.value_type = get_type_id(entry.value);
        updates[i] = entry;

        if (shape_builder_has_field(&builder, entry.key)) {
            shape_builder_remove_field(&builder, entry.key);
        }
        shape_builder_add_field(&builder, entry.key, entry.value_type);
    }

    bool is_inline = mode_ == EDIT_MODE_INLINE;
    Map* target = container;
    if (!is_inline) {
        target = container_clone_header(container);
        if (!target) return ItemError;
    }

    Item rebuilt = container_rebuild_with_new_shape(target, &builder, is_inline);
    for (int i = 0; i < count; i++) {
        container_store_field(rebuilt, updates[i].key, updates[i].value, updates[i].value_type);
    }
    return rebuilt;
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
    va_list args;
    va_start(args, count);
    Item result = container_update_attr_batch(map, count, args);
    va_end(args);
    return result;
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
    return container_delete_attr(map, key);
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
    ShapeBuilder builder = container_shape_builder(target_map);
    shape_builder_import_shape(&builder, map_type->shape);

    for (int i = 0; i < count; i++) {
        shape_builder_remove_field(&builder, keys[i]);
    }

    bool is_inline = mode_ == EDIT_MODE_INLINE;
    Map* target = target_map;
    if (!is_inline) {
        target = container_clone_header(target_map);
        if (!target) return ItemError;
    }
    return container_rebuild_with_new_shape(target, &builder, is_inline);
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
    return container_update_attr(element, attr_name, value);
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
    va_list args;
    va_start(args, count);
    Item result = container_update_attr_batch(element, count, args);
    va_end(args);
    return result;
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
    return container_delete_attr(element, attr_name);
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
    if (!builder_->is_in_arena(child) &&
        !(ui_mode_ && mark_editor_should_preserve_ui_dom_child(child))) {
        log_debug("elmt_insert_child: child not in arena, deep copying");
        child = builder_->deep_copy(child);
    }

    if (mode_ == EDIT_MODE_INLINE) {
        // Inline mode - resize and insert in-place
        int64_t new_length = elmt->length + 1;

        if (new_length > elmt->capacity) {
            // Resize children array
            int64_t new_capacity = elmt->capacity ? elmt->capacity * 2 : 8;
            bool use_arena = (arena_ != nullptr && (elmt->items == nullptr || arena_owns(arena_, elmt->items)));
            if (use_arena) {
                // Always fresh alloc — do NOT arena_realloc (frees old buffer
                // to arena free-list, which can be recycled by new DomElement
                // allocations, overwriting still-referenced items buffers).
                Item* new_items = (Item*)arena_alloc(arena_, new_capacity * sizeof(Item));
                if (new_items && elmt->items) {
                    memcpy(new_items, elmt->items, elmt->capacity * sizeof(Item));
                }
                elmt->items = new_items;
            } else {
                Item* new_items = (Item*)raw_realloc(elmt->items, new_capacity * sizeof(Item));  // RAWALLOC_OK: Container items — heap-allocated, freed by free_container
                if (!new_items) {
                    log_error("elmt_insert_child: realloc failed");
                    return ItemError;
                }
                elmt->items = new_items;
            }
            if (!elmt->items) return ItemError;
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

        // Sync DOM linked list if ui_mode
        if (ui_mode_) dom_relink_children(elmt);

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
            bool use_arena = (arena_ != nullptr && (elmt->items == nullptr || arena_owns(arena_, elmt->items)));
            if (use_arena) {
                Item* new_items = (Item*)arena_alloc(arena_, new_capacity * sizeof(Item));
                if (new_items && elmt->items) {
                    memcpy(new_items, elmt->items, elmt->capacity * sizeof(Item));
                }
                elmt->items = new_items;
            } else {
                Item* new_items = (Item*)raw_realloc(elmt->items, new_capacity * sizeof(Item));  // RAWALLOC_OK: Container items
                if (!new_items) return ItemError;
                elmt->items = new_items;
            }
            if (!elmt->items) return ItemError;
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

        // Sync DOM linked list if ui_mode
        if (ui_mode_) dom_relink_children(elmt);

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

        // Sync DOM linked list if ui_mode
        if (ui_mode_) dom_relink_children(elmt);

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

        // Sync DOM linked list if ui_mode
        if (ui_mode_) dom_relink_children(elmt);

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
        // Sync DOM linked list if ui_mode
        if (ui_mode_) dom_relink_children(elmt);
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
    typemap_hash_build((TypeMap*)new_type, pool_);
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

    // Build new shape with new element name. Note the rebuilt TypeElmt keeps
    // the OLD name — as it always has; only the shape's pool bucket moves.
    ShapeBuilder builder = shape_builder_init_element(shape_pool_, new_tag_name);
    shape_builder_import_shape(&builder, old_type->shape);

    bool is_inline = mode_ == EDIT_MODE_INLINE;
    Map* target = (Map*)old_elmt;
    if (!is_inline) {
        target = container_clone_header((Map*)old_elmt);
        if (!target) return ItemError;
    }
    return container_rebuild_with_new_shape(target, &builder, is_inline);
}

//==============================================================================
// ARRAY OPERATIONS
//==============================================================================

Item MarkEditor::array_set(Item array, int64_t index, Item value) {
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

Item MarkEditor::array_insert(Item array, int64_t index, Item value) {
    TypeId array_type = get_type_id(array);

    if (array_type == LMD_TYPE_ARRAY || array_type == LMD_TYPE_ELEMENT) {
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
                // Check if items are arena-allocated to avoid realloc on arena pointers
                bool use_arena = (arena_ != nullptr && (arr->items == nullptr || arena_owns(arena_, arr->items)));
                if (use_arena) {
                    Item* new_items = (Item*)arena_alloc(arena_, new_capacity * sizeof(Item));
                    if (new_items && arr->items) {
                        memcpy(new_items, arr->items, arr->capacity * sizeof(Item));
                    }
                    arr->items = new_items;
                } else {
                    Item* new_items = (Item*)raw_realloc(arr->items, new_capacity * sizeof(Item));  // RAWALLOC_OK: Container items
                    if (!new_items) return ItemError;
                    arr->items = new_items;
                }
                if (!arr->items) return ItemError;
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

Item MarkEditor::array_delete(Item array, int64_t index) {
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
