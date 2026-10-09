#include "mark_editor.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_element.hpp"
#include "../input/input.hpp"
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

// The fat DOM node a UI child would be embedded in. Pointer arithmetic only:
// the node header in front of the child may be read only once the storage is
// known to hold a node.
static const void* mark_editor_ui_node_storage(Item child) {
    TypeId type_id = get_type_id(child);
    if (type_id == LMD_TYPE_ELEMENT && child.element) {
        return element_to_dom_element(child.element);
    }
    if (type_id == LMD_TYPE_STRING) {
        String* s = child.get_safe_string();
        return s ? string_to_dom_text(s) : nullptr;
    }
    return nullptr;
}

// Reads the node header in front of `child`; the caller must know the child is
// node-backed (see mark_editor_ui_node_storage).
static bool mark_editor_is_ui_dom_node(Item child) {
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
    // adopted nodes retain source arenas; their registry identity still belongs to this document.
    if (dom_node_registry_owns(parent->doc, embedded) &&
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
        // comments use Element backing but keep a distinct, stable DOM wrapper.
        if (type_id == LMD_TYPE_ELEMENT && candidate->is_comment() &&
            candidate->as_comment()->native_element == child.element) {
            candidate->parent = nullptr;
            return candidate;
        }
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
        TypeElmt* type = (TypeElmt*)child.element->type;
        const char* tag_name = type ? type->name.str : nullptr;
        // creating an element wrapper for a comment duplicates its backing identity.
        if (dom_is_comment_tag(tag_name)) {
            return DomComment::create_detached(child.element, parent->doc);
        }
        DomElement* element = mark_editor_lookup_ui_element_child(parent, child.element);
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
    , draft_arena_(nullptr)
    , name_pool_(input->name_pool)
    , type_list_(input->type_list)
    , mode_(mode)
    , ui_mode_(input->ui_mode)
    , ui_node_arena_(nullptr)
    , ui_document_(nullptr)
    , current_version_(nullptr)
    , version_head_(nullptr)
    , next_version_num_(0)
{
    // Create builder for constructing new structures
    builder_ = mark_builder_create(input);

    log_debug("MarkEditor created: mode=%s",
        mode == EDIT_MODE_INLINE ? "inline" : "immutable");
}

MarkEditor::MarkEditor(DomDocument* document, EditMode mode)
    : MarkEditor(document->input, mode) {
    set_ui_node_arena(document->node_arena);
    ui_document_ = document;
}

MarkEditor::~MarkEditor() {
    if (draft_arena_) arena_destroy(draft_arena_);
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
        node->parent = lam::up(parent);
        node->prev_sibling = lam::up(prev);
        node->next_sibling = nullptr;
        if (prev) {
            prev->next_sibling = lam::own(node);
        } else {
            parent->first_child = lam::own(node);
        }
        parent->last_child = lam::up(node);
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
#ifndef LAMBDA_NO_CONSOLE_DUMP
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
#endif
}

//==============================================================================
// Utility Helpers
//==============================================================================

String* MarkEditor::ensure_string_key(const char* key) {
    if (!key) return nullptr;
    // Use name_pool for keys (structural identifiers)
    return name_pool_create_len(name_pool_, key, strlen(key));
}

bool MarkEditor::find_field_in_shape(const TypeMap* shape, const char* key,
                                     TypeId* out_type, int64_t* out_offset) {
    if (!shape || !key) return false;

    FOR_EACH_MAP_FIELD(shape, entry) {
        if (strcmp(entry->name->str, key) == 0) {
            if (out_type) *out_type = entry->type->type_id;
            if (out_offset) *out_offset = entry->byte_offset;
            return true;
        }
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

Arena* MarkEditor::shape_draft_arena() {
    if (!draft_arena_) draft_arena_ = arena_create_default();
    return draft_arena_;
}

// element shapes start on their tag's transition root; map shapes start unnamed.
ShapeBuilder MarkEditor::container_shape_builder(const Map* container) {
    if (container->type_id == LMD_TYPE_ELEMENT) {
        return shape_builder_init_element(shape_draft_arena(),
            ((TypeElmt*)container->type)->name.str);
    }
    return shape_builder_init_map(shape_draft_arena());
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

// The field list a rebuild lays out, as tree steps: a field the old type has
// keeps that field's identity (D3.4.4v2); a new one joins under a pooled key,
// as ElementBuilder::attr pools its keys.
static TypeTreeStep* rebuild_steps(MarkBuilder* builder_,
        TypeMap* old_type, ShapeBuilder* builder) {
    int count = (int)builder->field_count;
    if (count == 0) return NULL;
    // steps are copied into persistent types; only their pooled keys escape.
    TypeTreeStep* steps = (TypeTreeStep*)arena_calloc(builder->arena,
        sizeof(TypeTreeStep) * (size_t)count);
    if (!steps) return NULL;
    for (int i = 0; i < count; i++) {
        const char* name = builder->fields[i].name;
        steps[i].type_id = builder->fields[i].type_id;
        steps[i].like = typemap_hash_lookup(old_type, name, (int)strlen(name));
        if (!steps[i].like) steps[i].key = builder_->createName(name);
        if (!steps[i].like && !steps[i].key) return NULL;
    }
    return steps;
}

// A chain this container alone will own: the same steps, laid out by storage
// size as alloc_shape_entry lays a chain, keeping a replayed field's flags and
// default. Never pooled, so an in-place edit can never reach another type.
static ShapeEntry* rebuild_private_chain(Pool* pool, const TypeTreeStep* steps,
        int count, ShapeEntry** out_last) {
    ShapeEntry* first = NULL;
    ShapeEntry* prev = NULL;
    for (int i = 0; i < count; i++) {
        const TypeTreeStep* step = &steps[i];
        ShapeEntry* entry = step->like
            ? shape_entry_copy_as(type_alloc_of_pool(pool), step->like, step->type_id, prev)
            : alloc_shape_entry(pool, step->key, step->type_id, prev);
        if (!entry) return NULL;
        if (step->like) {
            entry->flags = step->like->flags;
            entry->default_value = step->like->default_value;
        }
        entry->byte_offset = prev ? prev->byte_offset + shape_entry_storage_size(prev) : 0;
        if (!first) first = entry;
        prev = entry;
    }
    *out_last = prev;
    return first;
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

    TypeMap* old_type = (TypeMap*)old_container->type;
    int count = (int)builder->field_count;
    TypeTreeStep* steps = rebuild_steps(builder_, old_type, builder);
    if (count > 0 && !steps) {
        log_error("container_rebuild_with_new_shape: failed to describe the new fields");
        return ItemError;
    }

    // D3.4.3v3: the rebuilt type comes from the Input's transition tree when
    // the container may use it, so an edited container joins the type its new
    // field sequence already has, allocating nothing on a hit. A declined tree
    // leaves a private type with a chain of its own.
    TypeMap* tree_type = NULL;
    if (TypeMap* root = type_tree_root_like(input_, old_container)) {
        tree_type = type_tree_follow(input_, root, steps, count);
    }
    ShapeEntry* new_shape;
    ShapeEntry* new_last = NULL;
    if (tree_type) {
        new_shape = tree_type->shape;
        new_last = tree_type->last;
    } else {
        // A NULL shape is the legitimate result of deleting the last field;
        // only a NULL with fields still pending is a real failure.
        new_shape = rebuild_private_chain(pool_, steps, count, &new_last);
        if (!new_shape && count > 0) {
            log_error("container_rebuild_with_new_shape: failed to build the new shape");
            return ItemError;
        }
    }

    int64_t new_byte_size = new_last
        ? new_last->byte_offset + shape_entry_storage_size(new_last) : 0;

    // pool_calloc(0) returns NULL, so an emptied container legitimately ends
    // with a null buffer.
    void* new_data = pool_calloc(pool_, new_byte_size);
    if (!new_data && new_byte_size > 0) {
        log_error("container_rebuild_with_new_shape: allocation failed");
        return ItemError;
    }

    // Carry across every field the new shape shares with the old one at the
    // same type; anything added or retyped stays zero for the caller to fill.
    // a tree type's chain may run on past new_last (D3.4.3v3)
    for (ShapeEntry* entry = new_shape; entry;
            entry = shape_chain_next_until(entry, new_last)) {
        TypeId old_type_id;
        int64_t old_offset;
        if (find_field_in_shape(old_type, entry->name->str,
                                &old_type_id, &old_offset) &&
                old_type_id == entry->type->type_id) {
            void* old_field = (char*)old_container->data + old_offset;
            void* new_field = (char*)new_data + entry->byte_offset;
            memcpy(new_field, old_field, shape_entry_storage_size(entry));
        }
    }

    Map* result = old_container;
    if (!is_inline) {
        result = container_clone_header(old_container);
        if (!result) return ItemError;
    }

    if (tree_type) {
        result->type = tree_type;
    } else if (is_inline && old_type->is_private_clone &&
            !typemap_is_shared_shape(old_type) && old_type != &EmptyMap) {
        // In place, and only here: this container owns its type. Every other
        // type may be read by another container -- a transition-tree node, a
        // literal's compile-time type, a pooled chain -- and rewriting one
        // re-laid the fields under every sibling built the same way. An
        // element's name, id and content pattern are untouched by a shape change.
        old_type->shape = new_shape;
        old_type->last = new_last;
        old_type->length = count;
        old_type->byte_size = new_byte_size;
        typemap_hash_build(old_type, pool_);
    } else {
        // A fresh type this container owns, so its later inline edits can
        // take the in-place path above.
        TypeMap* new_type;
        if (is_element) {
            TypeElmt* old_elmt_type = (TypeElmt*)old_type;
            TypeElmt* elmt_type = (TypeElmt*)alloc_type(pool_, LMD_TYPE_ELEMENT, sizeof(TypeElmt));
            elmt_type->name = old_elmt_type->name;
            elmt_type->name_id = old_elmt_type->name_id;  // the HTML5 parser compares tags by it
            elmt_type->ns = old_elmt_type->ns;
            elmt_type->content_list = old_elmt_type->content_list;
            new_type = (TypeMap*)elmt_type;
        } else {
            new_type = (TypeMap*)alloc_type(pool_, LMD_TYPE_MAP, sizeof(TypeMap));
        }
        new_type->js_meta = old_type->js_meta;  // D3.4.7: the class stays with the value
        new_type->shape = new_shape;
        new_type->last = new_last;
        new_type->length = count;
        new_type->byte_size = new_byte_size;
        new_type->is_private_clone = true;
        new_type->type_index = type_list_->length;
        typemap_hash_build(new_type, pool_);
        arraylist_append(type_list_, new_type);
        result->type = new_type;
    }

    // Free the old data (inline mode only) when this editor's pool owns it. A
    // JIT result's buffer belongs to its result arena (ui_mode) or the GC heap,
    // which reclaim it themselves. Keying this on ui_mode_ trusted every path
    // to set that flag right, where a wrong one freed a foreign buffer (LR11-3).
    if (is_inline && pool_owns(pool_, old_container->data)) {
        pool_free(pool_, old_container->data);
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
    if (find_field_in_shape((TypeMap*)container->type, key,
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
    bool exists = find_field_in_shape(type, key->chars,
                                      &existing_type, &existing_offset);

    if (exists && existing_type == value_type) {
        // same type — the slot is already the right width, so write in place
        store_value_at_offset((char*)container->data + existing_offset, value, value_type);
        return container_item(container);
    }

    ShapeBuilder builder = container_shape_builder(container);
    // a draft that failed to grow holds only some fields; rebuilding from it
    // would silently drop the rest (LR11-4)
    if (!shape_builder_import_shape(&builder, type)) return ItemError;
    if (exists) shape_builder_remove_field(&builder, key->chars);
    if (!shape_builder_add_field(&builder, key->chars, value_type)) return ItemError;

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
    bool exists = find_field_in_shape(old_type, key->chars,
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
    if (!shape_builder_import_shape(&builder, old_type)) return ItemError;
    if (exists) shape_builder_remove_field(&builder, key->chars);
    if (!shape_builder_add_field(&builder, key->chars, value_type)) return ItemError;

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

    if (!find_field_in_shape(type, key->chars, nullptr, nullptr)) {
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
    if (!shape_builder_import_shape(&builder, type)) return ItemError;
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
    if (!shape_builder_import_shape(&builder, (TypeMap*)container->type)) return ItemError;

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
        if (!shape_builder_add_field(&builder, entry.key, entry.value_type)) {
            return ItemError;
        }
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
    if (!shape_builder_import_shape(&builder, map_type)) return ItemError;

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
    if (!find_field_in_shape(map_type, old_key, &field_type, &field_offset)) {
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

// LR11-3: a child buffer may belong to this Input's arena, a JIT result arena or
// the GC heap -- never to malloc -- so the old one stays with its owner and the
// grown one comes from this editor. raw_realloc of a buffer the editor's arena
// had not allocated corrupted the heap, and the copy loops it replaced left the
// owned wide-scalar tail behind the dense items that point into it.
bool MarkEditor::reserve_children(List* list, int64_t dense_length) {
    int64_t needed = dense_length + list->extra;
    if (needed <= list->capacity) return true;
    return list_grow_io(list, needed, pool_, arena_);
}

bool MarkEditor::owns_ui_node_storage(const void* storage) const {
    return storage && (arena_owns(arena_, storage) ||
        (ui_node_arena_ && arena_owns(ui_node_arena_, storage)));
}

Item MarkEditor::import_child(Item child, bool preserve_ui_nodes) {
    if (builder_->is_in_arena(child)) return child;
    // keep a live UI node by identity only when its storage is one this editor
    // vouches for; reading a node header in front of a GC object would read
    // garbage and could leave a GC pointer in the document (D4.5.2)
    const void* storage = mark_editor_ui_node_storage(child);
    bool registered = ui_document_ && dom_node_registry_owns(ui_document_, (DomNode*)storage);
    // adoption retains the source document; copying its registered node would duplicate DOM identity.
    if ((ui_mode_ || preserve_ui_nodes) &&
        (registered || owns_ui_node_storage(storage)) &&
        mark_editor_is_ui_dom_node(child)) return child;
    return builder_->deep_copy(child);
}

bool MarkEditor::prepare_child_edit(Item element, int64_t index, int64_t delete_count,
                                    int64_t count, Item* children, Array* edited) {
    if (get_type_id(element) != LMD_TYPE_ELEMENT || !element.element) {
        log_error("mark_editor_child_edit: not an element");
        return false;
    }
    Element* elmt = element.element;
    if (index < 0 && delete_count == 0) index = elmt->length;
    if (index < 0 || index > elmt->length || delete_count < 0 ||
        delete_count > elmt->length - index || count < 0 || (count > 0 && !children)) {
        log_error("mark_editor_child_edit: invalid child range");
        return false;
    }

    // stage positional slots before normalizing; array_append owns wide-scalar tails.
    edited->type_id = LMD_TYPE_ARRAY;
    auto append = [&](Item child) {
        int64_t previous_length = edited->length;
        ::array_append(edited, child, pool_, arena_);
        return edited->length == previous_length + 1;
    };
    for (int64_t i = 0; i < index; i++) {
        if (!append(elmt->items[i])) return false;
    }
    for (int64_t i = 0; i < count; i++) {
        if (!append(children[i])) return false;
    }
    for (int64_t i = index + delete_count; i < elmt->length; i++) {
        if (!append(elmt->items[i])) return false;
    }
    return true;
}

Item MarkEditor::publish_child_edit(Element* old_elmt, const Array* edited) {
    Element* target = old_elmt;
    if (mode_ == EDIT_MODE_IMMUTABLE) {
        target = (Element*)arena_alloc(arena_, sizeof(Element));
        if (!target) return ItemError;
        memcpy(target, old_elmt, sizeof(Element));

        // content edits retain the shared type and own the copied attribute buffer.
        TypeElmt* type = (TypeElmt*)old_elmt->type;
        if (type->byte_size > 0) {
            target->data = pool_calloc(pool_, type->byte_size);
            if (!target->data) return ItemError;
            memcpy(target->data, old_elmt->data, type->byte_size);
            target->data_cap = type->byte_size;
        }
    }
    target->items = edited->items;
    target->length = edited->length;
    target->capacity = edited->capacity;
    target->extra = edited->extra;
    if (mode_ == EDIT_MODE_INLINE && ui_mode_) dom_relink_children(target);
    return {.element = target};
}

Item MarkEditor::elmt_edit_children(Item element, int64_t index, int64_t delete_count,
                                    int64_t count, Item* children) {
    Array imported = {};
    imported.type_id = LMD_TYPE_ARRAY;
    for (int64_t i = 0; i < count; i++) {
        ::array_append(&imported, import_child(children[i]), pool_, arena_);
        if (imported.length != i + 1) return ItemError;
    }
    Array edited = {};
    if (!prepare_child_edit(element, index, delete_count, count, imported.items, &edited)) {
        return ItemError;
    }

    // S2.6.5: every edit rebuilds content, including strings joined by a deletion.
    List normalized = {};
    normalized.type_id = LMD_TYPE_ELEMENT;
    for (int64_t i = 0; i < edited.length; i++) {
        list_push_with_owner(&normalized, edited.items[i], pool_, arena_,
            ui_mode_ ? input_ : nullptr);
    }
    return publish_child_edit(element.element, &normalized);
}

Item MarkEditor::elmt_insert_child(Item element, int index, Item child) {
    return elmt_edit_children(element, index, 0, 1, &child);
}

Item MarkEditor::elmt_insert_children(Item element, int index, int count, Item* children) {
    if (count <= 0 || !children) return element;
    return elmt_edit_children(element, index, 0, count, children);
}

Item MarkEditor::elmt_delete_child(Item element, int index) {
    return elmt_edit_children(element, index, 1, 0, nullptr);
}

Item MarkEditor::elmt_delete_children(Item element, int start, int end) {
    if (start >= end) return ItemError;
    return elmt_edit_children(element, start, (int64_t)end - start, 0, nullptr);
}

Item MarkEditor::elmt_replace_child(Item element, int index, Item child) {
    return elmt_edit_children(element, index, 1, 1, &child);
}

Item MarkEditor::dom_edit_child(Item element, int64_t index, int64_t delete_count, Item* child) {
    Item imported;
    if (child) {
        // DOM string arguments may be plain GC strings; prove node storage ownership
        // before reading a prefix, while preserving detached document node identity.
        imported = import_child(*child, true);
    }
    Array edited = {};
    if (!prepare_child_edit(element, index, delete_count, child ? 1 : 0,
                            child ? &imported : nullptr, &edited)) return ItemError;
    return publish_child_edit(element.element, &edited);
}

Item MarkEditor::dom_insert_child(Item element, int index, Item child) {
    return dom_edit_child(element, index, 0, &child);
}

Item MarkEditor::dom_delete_child(Item element, int index) {
    return dom_edit_child(element, index, 1, nullptr);
}

Item MarkEditor::dom_replace_child(Item element, int index, Item child) {
    return dom_edit_child(element, index, 1, &child);
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
    ShapeBuilder builder = shape_builder_init_element(shape_draft_arena(), new_tag_name);
    if (!shape_builder_import_shape(&builder, old_type)) return ItemError;

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

    if (array_type == LMD_TYPE_ELEMENT) {
        return elmt_edit_children(array, index, 0, 1, &value);
    }
    if (array_type == LMD_TYPE_ARRAY) {
        Array* arr = array.array;

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

            if (!reserve_children((List*)arr, new_length)) {
                log_error("array_insert: items growth failed");
                return ItemError;
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
