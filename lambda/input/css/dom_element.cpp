// #define _POSIX_C_SOURCE 200809L
#include "dom_element.hpp"
#include "dom_lifecycle.hpp"
#include "style_epoch.hpp"
#include "css_formatter.hpp"
#include "css_style_node.hpp"
#include "css_parser.hpp"
#include "css_counter_hook.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/hashmap_helpers.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/stringbuf.h"
#include "../../../lib/string.h"
#include "../../../lib/log.h"
#include "../../../lib/strview.h"
#include "../../../lib/str.h"
#include "../../../lib/arena.h"
#include "../../../lib/memtrack.h"
#include "../../lambda-data.hpp"  // For get_type_id, and proper type definitions
#include "../../core/mark_reader.hpp"  // For ElementReader
#include "../../io/mark_editor.hpp"  // For MarkEditor
#include "../../io/mark_builder.hpp" // For MarkBuilder
#include "../../../radiant/view.hpp"  // For HTM_TAG_* constants

void element_dom_map_remove(HashMap* map, Element* elem);

extern "C" __attribute__((weak)) void svg_unregister_image_resolvers_for_tree(Element* root) {
    (void)root;
}

bool dom_subtree_contains_node(DomNode* root, DomNode* target) {
    if (!root || !target) return false;
    if (root == target) return true;
    if (!root->is_element()) return false;

    DomElement* element = root->as_element();
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        if (dom_subtree_contains_node(child, target)) return true;
    }
    return false;
}

// Runtime-cleanup hook: the full runtime layer (lambda/runner.cpp's
// runtime_init) installs this so the input/css layer doesn't hard-depend on
// runner.cpp's runtime_cleanup. NULL in input-only unit-test builds — safe
// because those builds never create a document->lambda_runtime.
static void (*g_runtime_cleanup_hook)(Runtime*) = nullptr;
extern "C" void dom_set_runtime_cleanup_hook(void (*fn)(Runtime*)) {
    g_runtime_cleanup_hook = fn;
}

// Timing accumulators for cascade profiling
static thread_local int64_t g_apply_decl_count = 0;

void reset_dom_element_timing() {
    g_apply_decl_count = 0;
}

void log_dom_element_timing() {
    log_info("[TIMING] cascade detail: decl_count: %lld", g_apply_decl_count);
}

// Forward declaration
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* document, DomElement* parent);
DomElement* build_dom_tree_from_element_with_input(Element* elem, DomDocument* document, DomElement* parent);
const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena);
static bool dom_element_add_cached_class(DomElement* element, const char* class_name);

// helper: append a DomNode child to a parent's sibling chain
static void dom_append_to_sibling_chain(DomElement* parent, DomNode* child) {
    dom_node_cancel_detached(parent ? parent->doc : nullptr, child);
    child->parent = parent;
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
        child->prev_sibling = nullptr;
        child->next_sibling = nullptr;
    } else {
        DomNode* last = parent->last_child;
        if (!last) {
            last = parent->first_child;
            while (last->next_sibling) last = last->next_sibling;
        }
        last->next_sibling = child;
        child->prev_sibling = last;
        child->next_sibling = nullptr;
        parent->last_child = child;
    }
}

static DomText* dom_text_from_fat_string(DomDocument* doc, String* string_value) {
    if (!doc || !doc->node_arena || !string_value) return nullptr;
    if (!arena_owns(doc->node_arena, string_value)) return nullptr;

    DomText* candidate = string_to_dom_text(string_value);
    if (!arena_owns(doc->node_arena, candidate)) return nullptr;
    if (candidate->node_type != DOM_NODE_TEXT) return nullptr;
    if (candidate->native_string != string_value) return nullptr;
    return candidate;
}

static String* dom_create_mutation_string(MarkBuilder* builder, const char* content, bool use_dom_text_string) {
    if (!builder || !content) return nullptr;
    size_t len = strlen(content);
    if (use_dom_text_string) {
        return builder->createDomTextString(content, len);
    }
    if (len > 0) {
        return builder->createString(content, len);
    }

    // dom text mutations need a real empty string, while normal Lambda "" maps to null.
    String* s = (String*)arena_alloc(builder->arena(), sizeof(String) + 1);
    if (!s) return nullptr;
    s->len = 0;
    s->is_ascii = 1;
    s->chars[0] = '\0';
    return s;
}

// helper: extract a name string from a CssValue (works for counter names, attr names, etc.)
static const char* css_value_extract_name(const CssValue* value) {
    if (!value) return nullptr;
    if (value->type == CSS_VALUE_TYPE_STRING) return value->data.string;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        const CssEnumInfo* info = css_enum_info(value->data.keyword);
        return info ? info->name : nullptr;
    }
    if (value->type == CSS_VALUE_TYPE_CUSTOM) return value->data.custom_property.name;
    return nullptr;
}

// ============================================================================
// DOM Document Creation and Destruction
// ============================================================================

DomDocument* dom_document_create(Input* input) {
    // Allocate document structure
    DomDocument* document = (DomDocument*)mem_calloc(1, sizeof(DomDocument), MEM_CAT_INPUT_CSS);
    if (!document) {
        log_error("dom_document_create: failed to allocate document");
        return nullptr;
    }

    if (!document->init(input)) {
        mem_free(document);
        return nullptr;
    }

    log_debug("dom_document_create: created document with arena");
    return document;
}

bool DomDocument::init(Input* source_input) {
    if (!source_input) {
        log_error("dom_document_create: input is required");
        return false;
    }

    // Reuse the input's per-document memory sub-context so the DOM pool/arena are
    // attributed to the same document (URL) as the parsed source. The whole
    // document subtree is reclaimed together at free_document() time.
    MemContext* dctx = source_input->mem_ctx ? (MemContext*)source_input->mem_ctx : NULL;
    services.mem_ctx = dctx;

    // Create pool for arena chunks
    document_pool = mem_pool_create(dctx, MEM_ROLE_NODE, "dom.document.pool");
    if (!document_pool) {
        log_error("dom_document_create: failed to create pool");
        return false;
    }

    if (!dom_lifecycle_init(this)) {
        log_error("dom_document_create: failed to create node lifecycle registry");
        destroy();
        return false;
    }

    if (!style_epoch_manager_init(this)) {
        log_error("dom_document_create: failed to create style epoch manager");
        destroy();
        return false;
    }

    // Create arena for all DOM node allocations
    node_arena = mem_arena_create(dctx, document_pool, MEM_ROLE_NODE, "dom.node.arena");
    if (!node_arena) {
        log_error("dom_document_create: failed to create arena");
        // Factory-created DOM roots must unregister their memory-context nodes on teardown.
        destroy();
        return false;
    }

    input = source_input;
    root = nullptr;
    return true;
}

void dom_document_destroy(DomDocument* document) {
    if (!document) {
        return;
    }
    document->destroy();
    mem_free(document);
}

void DomDocument::destroy() {
    float ext_rate = services.element_count
        ? 100.0f * (float)services.ext_allocations / (float)services.element_count
        : 0.0f;
    float cache_rate = services.element_count
        ? 100.0f * (float)services.layout_cache_allocations / (float)services.element_count
        : 0.0f;
    log_debug("DOM_PROP_ALLOCATION_RATE elements=%u ext=%u ext_rate=%.1f%% layout_cache=%u cache_rate=%.1f%%",
              services.element_count, services.ext_allocations, ext_rate,
              services.layout_cache_allocations, cache_rate);
    if (html_root) {
        svg_unregister_image_resolvers_for_tree(html_root);
    }

    if (pending_navigation_url) {
        mem_free(pending_navigation_url);
        pending_navigation_url = nullptr;
    }

    // Runtime-backed extension values must release their GC roots while the
    // document's retained Lambda runtime is still alive.
    DomDocumentResource* resource = resources;
    while (resource) {
        DomDocumentResource* next = resource->next;
        if (resource->destroy) resource->destroy(resource->data);
        mem_free(resource);
        resource = next;
    }
    resources = nullptr;

    if (lambda_runtime) {
        if (g_runtime_cleanup_hook) g_runtime_cleanup_hook(lambda_runtime);
        mem_free(lambda_runtime);
        lambda_runtime = nullptr;
    }

    if (js.mutation_record_count > 0 || js.mutation_count > 0) {
        dom_js_mutation_records_reset(this);
    }

    // Canonical styles use independent pools and must disappear while the
    // document pool that owns their manager/list is still valid.
    style_epoch_manager_destroy(this);

    // The lifecycle registry owns detached-candidate metadata backed by the
    // document pool, so tear it down before its registered node arena.
    dom_lifecycle_destroy(this);

    // Note: root and all DOM nodes are allocated from arena,
    // so they will be freed when arena is destroyed
    if (node_arena) {
        // Factory-created DOM roots must unregister their memory-context nodes on teardown.
        mem_arena_destroy(node_arena);
        node_arena = nullptr;
    }

    if (document_pool) {
        mem_pool_destroy(document_pool);
        document_pool = nullptr;
    }

    // Note: Input* is not owned by document, don't free it
    log_debug("dom_document_destroy: destroyed document and arena");
}

bool dom_document_add_resource(DomDocument* document, void* data,
                               DomDocumentResourceDestroyFn destroy) {
    if (!document || !data || !destroy) return false;
    DomDocumentResource* resource = (DomDocumentResource*)mem_calloc(
        1, sizeof(DomDocumentResource), MEM_CAT_LAYOUT);
    if (!resource) return false;
    resource->data = data;
    resource->destroy = destroy;
    resource->next = document->resources;
    document->resources = resource;
    return true;
}

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

static uint32_t dom_document_alloc_node_id(DomDocument* doc) {
    if (!doc) return 0;
    uint32_t id = doc->next_node_id++;
    if (id == 0) id = doc->next_node_id++;
    return id;
}

static void dom_element_release_cached_id(DomElement* element) {
    if (!element || !element->id) return;
    if (element->doc && element->doc->document_pool) {
        pool_free(element->doc->document_pool, (void*)element->id);
    }
    dom_element_clear_id(element);
}

static void dom_element_release_cached_classes(DomElement* element) {
    if (!element || !element->class_names) return;
    if (element->doc && element->doc->document_pool) {
        for (int i = 0; i < element->class_count; i++) {
            pool_free(element->doc->document_pool, (void*)element->class_names[i]);
        }
        pool_free(element->doc->document_pool, (void*)element->class_names);
    }
    element->class_count = 0;
    dom_element_clear_class_names(element);
}

DomElement* DomElement::create_in(Arena* arena) {
    if (!arena) return nullptr;
    // Arena zeroing is the construction contract; only the discriminator is non-zero.
    DomElement* element = (DomElement*)arena_calloc(arena, sizeof(DomElement));
    if (element) element->node_type = DOM_NODE_ELEMENT;
    return element;
}

DomElement* DomElement::create_in(Pool* pool) {
    if (!pool) return nullptr;
    DomElement* element = (DomElement*)pool_calloc(pool, sizeof(DomElement));
    if (element) {
        element->node_type = DOM_NODE_ELEMENT;
        // View-pool generated boxes have no Lambda backing; zero flags would
        // otherwise make their embedded placeholder look tree-owned.
        element->set_synthetic(true);
    }
    return element;
}

DomElement* DomElement::create(DomDocument* doc, const char* tag_name,
                               Element* native_element) {
    if (!doc || !doc->node_arena || !tag_name) return nullptr;
    return create_in(create_in(doc->node_arena), doc, tag_name, native_element);
}

DomElement* DomElement::create_in(DomElement* element, DomDocument* doc,
                                  const char* tag_name, Element* native_element) {
    if (!element || !doc || !tag_name) return nullptr;

    bool reinitializing = element->doc != nullptr;
    uint32_t retained_id = reinitializing && element->doc == doc
        ? static_cast<DomNode*>(element)->id : 0;
    if (reinitializing) {
        // UI rebuilds reuse fat Lambda-element storage; stale tree links and
        // attribute caches would splice the previous DOM epoch into the new one.
        element->parent = nullptr;
        element->next_sibling = nullptr;
        element->prev_sibling = nullptr;
        element->first_child = nullptr;
        element->last_child = nullptr;
        dom_element_release_cached_id(element);
        dom_element_release_cached_classes(element);
        if (element->tag_name) pool_free(element->doc->document_pool, (void*)element->tag_name);
        element->tag_name = nullptr;
        if (element->specified_style_shared()) {
            style_epoch_unbind_element(element);
        } else if (element->specified_style_borrowed()) {
            element->specified_style = nullptr;
            element->mark_specified_style_owned();
        } else if (element->specified_style) {
            style_tree_destroy_owned(element->specified_style);
            element->specified_style = nullptr;
        }
        element->set_styles_resolved(false);
    }

    static_cast<DomNode*>(element)->id = retained_id ? retained_id : dom_document_alloc_node_id(doc);
    element->node_type = DOM_NODE_ELEMENT;
    element->doc = doc;

    // A null backing marks layout-only nodes; their embedded storage must never
    // be mistaken for a member of the Lambda tree.
    if (!native_element) {
        element->set_synthetic(true);
    }
    else {
        // Rebinding reused storage must also retire a prior synthetic identity.
        if (reinitializing) element->set_synthetic(false);
        if (native_element != &element->elmt) {
            element->elmt = *native_element;  // shallow copy (items[], type, data pointers are shared)
        }
    }

    // An unresolved display is semantically different from display:none; the
    // retired constructor used NONE and could suppress the first table resolve.
    element->display = {CSS_VALUE__UNDEF, CSS_VALUE__UNDEF};

    // Mutable DOM metadata is individually reclaimable document-pool storage;
    // keeping it in the node arena would defeat detached-node retirement.
    lam::PoolPtr<char> tag_copy = lam::promote_to_pool(doc->document_pool, tag_name);
    if (!tag_copy) {
        return nullptr;
    }
    dom_element_retain_tag_name(element, tag_copy);

    // Convert tag name to Lexbor tag ID for fast comparison
    element->tag_id = DomNode::tag_name_to_id(tag_name);

    // Create style trees (still use pool for AVL nodes)
    element->specified_style = style_tree_create(doc->document_pool);
    if (!element->specified_style) {
        return nullptr;
    }

    element->style_version = 1;
    element->set_needs_style_recompute(true);

    // Initialize cached attribute fields from native element (if exists)
    if (native_element) {
        // Cache ID attribute
        // Template-bound attributes can carry a runtime String even when their
        // shape field is not statically typed as one; the runtime lookup is the
        // authoritative source for selector caches during DOM reconstruction.
        const char* id_attr = extract_element_attribute(native_element, "id", nullptr);
        if (id_attr) {
            dom_element_retain_id(element, lam::promote_to_pool(doc->document_pool, id_attr));
        }

        // Parse class attribute into array
        const char* class_str = extract_element_attribute(native_element, "class", nullptr);
        if (class_str && class_str[0] != '\0') {
            // Count classes (space-separated)
            int count = 1;
            for (const char* p = class_str; *p; p++) {
                if (*p == ' ' || *p == '\t') count++;
            }

            const char** class_names = (const char**)pool_calloc(
                doc->document_pool, count * sizeof(const char*));
            if (class_names) {
                dom_element_retain_class_names(element, lam::PoolPtr<const char*>(class_names));
                // Parse classes - make a copy for strtok
                char* class_copy = pool_strdup(doc->document_pool, class_str);
                if (class_copy) {
                    str_copy(class_copy, strlen(class_str) + 1, class_str, strlen(class_str));

                    int index = 0;
                    char* token = strtok(class_copy, " \t\n\r");
                    while (token && index < count) {
                        // Allocate permanent copy of each class from arena
                        size_t token_len = strlen(token);
                        char* class_perm = (char*)pool_alloc(doc->document_pool, token_len + 1);
                        if (class_perm) {
                            str_copy(class_perm, token_len + 1, token, token_len);
                            class_names[index++] = class_perm;
                        }
                        token = strtok(NULL, " \t\n\r");
                    }
                    element->class_count = index;
                    pool_free(doc->document_pool, class_copy);
                }
            }
        }
    }

    if (!dom_node_registry_register(doc, element, sizeof(DomElement), true)) {
        return nullptr;
    }
    // The reverse-map key belongs to the external generation registry; keeping
    // it out of DomElement preserves the hot node-size ratchet.
    dom_node_registry_set_backing_source(doc, element, native_element);
    doc->services.element_count++;
    return element;
}

DomElement* dom_element_create(DomDocument* doc, const char* tag_name, Element* native_element) {
    return DomElement::create(doc, tag_name, native_element);
}

void dom_element_clear(DomElement* element) {
    if (!element) {
        return;
    }

    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
        element->specified_style = style_tree_create(element->doc->document_pool);
        element->mark_specified_style_owned();
    } else if (element->specified_style_borrowed()) {
        // A generated pseudo box may discard its view of the source tree, but
        // it must never clear declarations owned by the originating element.
        element->specified_style = style_tree_create(element->doc->document_pool);
        element->mark_specified_style_owned();
    } else if (element->specified_style) {
        // Recascade runs inside the document pool's active lifetime; returning
        // its live style allocations here corrupts subsequent CSS allocations.
        style_tree_clear(element->specified_style);
    } else {
        element->specified_style = style_tree_create(element->doc->document_pool);
        element->mark_specified_style_owned();
    }
    // Reset version tracking
    element->style_version++;
    element->set_needs_style_recompute(true);

    // Note: We don't free memory here since it's pool-allocated
    // The pool will handle cleanup
}

void dom_element_clear_cascaded_styles(DomElement* element) {
    if (!element) return;

    bool changed = false;
    if (element->specified_style_shared()) {
        // Canonical trees never contain inline declarations, so detaching is
        // sufficient and avoids materializing declarations that are discarded.
        style_epoch_unbind_element(element);
        element->specified_style = style_tree_create(element->doc->document_pool);
        element->mark_specified_style_owned();
        changed = true;
    } else if (element->specified_style_borrowed()) {
        element->specified_style = style_tree_create(element->doc->document_pool);
        element->mark_specified_style_owned();
        changed = true;
    } else if (element->specified_style) {
        if (style_tree_has_inline_declarations(element->specified_style)) {
            // Inline declarations are live DOM state. Reparse-on-recascade both
            // lost CSSOM writes and grew the document pool on every hover event.
            changed = style_tree_remove_non_inline_declarations(
                element->specified_style);
        } else if (!style_tree_is_empty(element->specified_style)) {
            style_tree_clear(element->specified_style);
            changed = true;
        }
    } else {
        element->specified_style = style_tree_create(element->doc->document_pool);
        element->mark_specified_style_owned();
    }
    if (changed) {
        element->style_version++;
        element->set_needs_style_recompute(true);
    }
}

void dom_element_borrow_specified_style(DomElement* element, StyleTree* style) {
    if (!element) return;
    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
    } else if (!element->specified_style_borrowed() && element->specified_style &&
               element->specified_style != style) {
        style_tree_destroy_owned(element->specified_style);
    }
    // Generated pseudo elements are views over their source declarations; the
    // source element remains the sole owner across view retirement and rebuild.
    element->specified_style = style;
    if (style) element->mark_specified_style_borrowed();
    else element->mark_specified_style_owned();
}

void dom_element_destroy(DomElement* element) {
    if (!element) {
        return;
    }

    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
    } else if (element->specified_style_borrowed()) {
        element->specified_style = nullptr;
        element->mark_specified_style_owned();
    } else if (element->specified_style) {
        style_tree_destroy_owned(element->specified_style);
        element->specified_style = nullptr;
    }

    // Clear cached fields (but don't free - owned by pool/element)
    dom_element_clear_id(element);
    dom_element_clear_class_names(element);
    element->class_count = 0;

    // The embedded Lambda Element's storage is managed by Input/Arena.
    // Note: The element structure itself is pool-allocated,
    // so it will be freed when the pool is destroyed
}

void dom_element_release_retired_storage(DomElement* element) {
    if (!element || !element->doc) return;
    Element* backing_source = dom_node_registry_backing_source(
        element->doc, static_cast<DomNode*>(element));
    if (element->doc->element_dom_map && backing_source) {
        // Reconcile lookup values are raw DOM pointers; remove the Lambda key
        // before the arena can repurpose this node generation.
        element_dom_map_remove(element->doc->element_dom_map,
                               backing_source);
    }
    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
    } else if (element->specified_style_borrowed()) {
        element->specified_style = nullptr;
        element->mark_specified_style_owned();
    } else if (element->specified_style) {
        style_tree_destroy_owned(element->specified_style);
        element->specified_style = nullptr;
    }
    if (element->tag_name) {
        pool_free(element->doc->document_pool, (void*)element->tag_name);
        element->tag_name = nullptr;
    }
    dom_element_release_cached_id(element);
    dom_element_release_cached_classes(element);
    CssCustomProp* variable = element->css_variables;
    while (variable) {
        CssCustomProp* next = variable->next;
        pool_free(element->doc->document_pool, variable);
        variable = next;
    }
    element->css_variables = nullptr;
    if (element->ext) {
        for (int kind = 0; kind < PSEUDO_STYLE_COUNT; kind++) {
            if (element->ext->pseudo_styles[kind]) {
                style_tree_destroy_owned(element->ext->pseudo_styles[kind]);
                element->ext->pseudo_styles[kind] = nullptr;
            }
        }
        pool_free(element->doc->document_pool,
                  (void*)element->ext->attribute_names_cache);
        pool_free(element->doc->document_pool, element->ext);
        element->ext = nullptr;
    }
}

// ============================================================================
// Attribute Management
// ============================================================================

// Helper: lowercase attribute name into buffer (HTML5 spec: attribute names are case-insensitive)
static const char* lowercase_attr_name(const char* name, char* buf, size_t buf_size) {
    size_t i = 0;
    for (; name[i] && i < buf_size - 1; i++) {
        buf[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] + 0x20) : name[i];
    }
    buf[i] = '\0';
    return buf;
}

static bool dom_element_clear_inline_style_declarations(DomElement* element) {
    if (!element || !element->specified_style) {
        return false;
    }
    if (!style_epoch_ensure_owned(element)) return false;
    return style_tree_remove_inline_declarations(element->specified_style);
}

bool DomElement::set_attribute(const char* name, const char* value) {
    DomElement* element = this;
    if (!name || !value) {
        log_debug("dom_element_set_attribute: invalid parameters");
        return false;
    }

    // HTML5: attribute names are case-insensitive, store lowercased
    char lower_name[128];
    lowercase_attr_name(name, lower_name, sizeof(lower_name));

    if (!element->is_synthetic() && element->doc) {
        Element* backing = dom_element_to_element(element);
        MarkEditor editor(element->doc->input, EDIT_MODE_INLINE);

        // Create string value item
        Item value_item = editor.builder()->createStringItem(value);

        // Update attribute via MarkEditor
        Item result = editor.elmt_update_attr(
            {.element = backing},
            lower_name,
            value_item
        );

        // NOTE: a failed update returns ITEM_ERROR, whose raw bits are non-null
        // (0x19<<56), so a bare `if (result.element)` would treat the error as a
        // valid pointer and corrupt the DOM backing identity.
        // Guard on the actual runtime type, mirroring the delete path below.
        if (get_type_id(result) == LMD_TYPE_ELEMENT && result.element) {
            // Inline editing must preserve the embedded Element address; storing a
            // returned pointer would reintroduce a second, divergent identity.
            if (result.element != backing) {
                log_error("dom_element_set_attribute: inline editor changed backing identity");
                return false;
            }

            // Handle special attributes
            if (strcmp(lower_name, "id") == 0) {
                // Cache ID for fast access
                dom_element_release_cached_id(element);
                ElementReader reader(backing);
                const char* id_attr = reader.get_attr_string("id");
                if (id_attr) {
                    dom_element_retain_id(element, lam::promote_to_pool(
                        element->doc->document_pool, id_attr));
                }
            } else if (strcmp(lower_name, "class") == 0) {
                // Parse space-separated classes
                dom_element_release_cached_classes(element);

                // Parse and add each class
                if (value && strlen(value) > 0) {
                    char* class_copy = pool_strdup(element->doc->document_pool, value);
                    if (class_copy) {
                        str_copy(class_copy, strlen(value) + 1, value, strlen(value));

                        // Split by spaces and add each class
                        char* token = strtok(class_copy, " \t\n\r");
                        while (token) {
                            if (strlen(token) > 0) {
                                dom_element_add_cached_class(element, token);
                            }
                            token = strtok(nullptr, " \t\n\r");
                        }
                        pool_free(element->doc->document_pool, class_copy);
                    }
                }
            } else if (strcmp(lower_name, "style") == 0) {
                // Setting the style attribute replaces the previous inline declaration block.
                dom_element_clear_inline_style_declarations(element);
                if (value[0] != '\0') {
                    dom_element_apply_inline_style(element, value);
                }
            }

            // Invalidate style cache
            element->style_version++;
            element->set_needs_style_recompute(true);

            return true;
        }

        log_error("dom_element_set_attribute: MarkEditor failed to update attribute");
        return false;
    }

    // No native element - log warning
    log_warn("dom_element_set_attribute: element is synthetic or has no input context");
    return false;
}

const char* DomElement::get_attribute(const char* name) {
    DomElement* element = this;
    if (!name || name[0] == '\0') {
        return nullptr;
    }

    // HTML5 stores attribute names lowercased. CSS attr() may pass mixed-case.
    // Lowercase the lookup key for case-insensitive matching per CSS spec.
    char lower_name[128];
    size_t i = 0;
    for (; name[i] && i < sizeof(lower_name) - 1; i++) {
        lower_name[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] + 0x20) : name[i];
    }
    lower_name[i] = '\0';

    // Use ElementReader for read-only access
    if (!element->is_synthetic()) {
        Element* backing = dom_element_to_element(element);
        // Try shape-typed fast path first (covers fields with compile-time LMD_TYPE_STRING)
        ElementReader reader(backing);
        const char* result = reader.get_attr_string(lower_name);
        if (result) return result;

        // Fallback: check runtime Item type (handles fields where compile-time type
        // differs from runtime type, e.g. state-bound template attributes)
        ConstItem attr_value = backing->get_attr(lower_name);
        String* string_value = attr_value.string();
        if (string_value) return string_value->chars;
    }

    return nullptr;
}

bool DomElement::remove_attribute(const char* name) {
    DomElement* element = this;
    if (!name) {
        return false;
    }

    // HTML5: attribute names are case-insensitive
    char lower_name[128];
    lowercase_attr_name(name, lower_name, sizeof(lower_name));

    if (!element->is_synthetic() && element->doc) {
        Element* backing = dom_element_to_element(element);
        MarkEditor editor(element->doc->input, EDIT_MODE_INLINE);

        // Delete attribute via MarkEditor
        Item result = editor.elmt_delete_attr(
            {.element = backing},
            lower_name
        );

        if (get_type_id(result) == LMD_TYPE_ELEMENT && result.element) {
            if (result.element != backing) {
                log_error("dom_element_remove_attribute: inline editor changed backing identity");
                return false;
            }

            // Clear cached fields
            if (strcmp(lower_name, "id") == 0) {
                dom_element_release_cached_id(element);
            } else if (strcmp(lower_name, "class") == 0) {
                dom_element_release_cached_classes(element);
            } else if (strcmp(lower_name, "style") == 0) {
                dom_element_clear_inline_style_declarations(element);
            }

            // Invalidate style cache
            element->style_version++;
            element->set_needs_style_recompute(true);

            return true;
        }
    }

    return false;
}

bool DomElement::has_attribute(const char* name) {
    DomElement* element = this;
    if (!name) {
        return false;
    }

    // HTML5: attribute names are case-insensitive
    char lower_name[128];
    lowercase_attr_name(name, lower_name, sizeof(lower_name));

    if (!element->is_synthetic()) {
        ElementReader reader(dom_element_to_element(element));
        return reader.has_attr(lower_name);
    }

    return false;
}

const char** DomElement::attribute_names(int* count) {
    DomElement* element = this;
    if (!count) {
        if (count) *count = 0;
        return nullptr;
    }

    *count = 0;
    if (element->is_synthetic()) return nullptr;

    Element* backing = dom_element_to_element(element);
    ElementReader reader(backing);
    int attr_count = reader.attrCount();
    if (attr_count == 0) return nullptr;

    DomElementExt* data = element->ensure_ext();
    if (!data) return nullptr;
    if (attr_count > data->attribute_names_capacity) {
        const char** names = (const char**)pool_realloc(
            element->doc->document_pool, (void*)data->attribute_names_cache,
            attr_count * sizeof(const char*));
        if (!names) return nullptr;
        data->attribute_names_cache = names;
        data->attribute_names_capacity = attr_count;
    }
    const char** names = data->attribute_names_cache;

    // Iterate through shape to collect names
    const TypeElmt* type = (const TypeElmt*)backing->type;
    if (!type || !type->shape) {
        *count = 0;
        return nullptr;
    }

    const ShapeEntry* field = type->shape;
    int index = 0;

    while (field && index < attr_count) {
        if (field->name && field->name->str) {
            names[index++] = field->name->str;
        }
        field = field->next;
    }

    *count = index;
    return names;
}

// ============================================================================
// Class Management
// ============================================================================

static bool dom_element_add_cached_class(DomElement* element, const char* class_name) {
    if (!element || !class_name) {
        return false;
    }

    // Allow empty class names to be added (permissive), but they won't match later
    // Check if class already exists
    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            return true; // Already exists
        }
    }

    // Add new class
    int new_count = element->class_count + 1;
    const char** new_classes = (const char**)pool_calloc(
        element->doc->document_pool, new_count * sizeof(char*));
    if (!new_classes) {
        return false;
    }

    // Copy existing classes
    if (element->class_count > 0) {
        memcpy(new_classes, element->class_names, element->class_count * sizeof(char*));
    }

    // Add new class
    size_t class_len = strlen(class_name);
    char* class_copy = (char*)pool_alloc(element->doc->document_pool, class_len + 1);
    if (!class_copy) {
        pool_free(element->doc->document_pool, (void*)new_classes);
        return false;
    }
    str_copy(class_copy, class_len + 1, class_name, class_len);

    new_classes[element->class_count] = class_copy;
    const char** old_classes = element->class_names;
    dom_element_retain_class_names(element, lam::PoolPtr<const char*>(new_classes));
    element->class_count = new_count;
    pool_free(element->doc->document_pool, (void*)old_classes);

    return true;
}

static bool dom_element_remove_cached_class(DomElement* element, const char* class_name) {
    if (!element || !class_name) {
        return false;
    }

    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            pool_free(element->doc->document_pool, (void*)element->class_names[i]);
            // Found the class - shift remaining classes down
            if (i < element->class_count - 1) {
                memmove((void*)&element->class_names[i],
                       (void*)&element->class_names[i + 1],
                       (element->class_count - i - 1) * sizeof(char*));
            }
            element->class_count--;
            return true;
        }
    }

    return false;
}

static bool dom_element_sync_class_attribute(DomElement* element) {
    if (!element || element->is_synthetic() || !element->doc) {
        return true;
    }

    StrBuf* serialized = strbuf_new();
    if (!serialized) {
        return false;
    }
    for (int i = 0; i < element->class_count; i++) {
        if (i > 0) {
            strbuf_append_char(serialized, ' ');
        }
        strbuf_append_str(serialized, element->class_names[i]);
    }

    // DOMTokenList mutates the content attribute; keeping only the selector cache
    // made classList and getAttribute()/native automation observe different DOMs.
    bool updated = element->set_attribute("class", serialized->str);
    strbuf_free(serialized);
    return updated;
}

bool DomElement::add_class(const char* class_name) {
    DomElement* element = this;
    if (!class_name) {
        return false;
    }
    if (has_class(class_name)) {
        return true;
    }
    if (!dom_element_add_cached_class(element, class_name)) {
        return false;
    }
    return dom_element_sync_class_attribute(element);
}

bool DomElement::remove_class(const char* class_name) {
    DomElement* element = this;
    if (!dom_element_remove_cached_class(element, class_name)) {
        return false;
    }
    return dom_element_sync_class_attribute(element);
}

bool DomElement::has_class(const char* class_name) const {
    const DomElement* element = this;
    if (!class_name || class_name[0] == '\0') {
        return false;  // Empty class names never match
    }

    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            return true;
        }
    }

    return false;
}

bool DomElement::toggle_class(const char* class_name) {
    if (!class_name) {
        return false;
    }

    if (has_class(class_name)) {
        remove_class(class_name);
        return false;
    } else {
        add_class(class_name);
        return true;
    }
}

// ============================================================================
// Inline Style Support
// ============================================================================

/**
 * Parse and apply inline style attribute to an element
 * Format: "property: value; property: value;"
 * Inline styles have specificity (1,0,0,0) - highest non-!important specificity
 */
int dom_element_apply_inline_style(DomElement* element, const char* style_text) {
    if (!element || !style_text || !element->doc) {
        return 0;
    }

    int applied_count = 0;

    // Parse the style text - split by semicolons
    // Example: "color: red; font-size: 14px; background: blue"
    size_t style_len = strlen(style_text);
    char* text_copy = (char*)pool_alloc(element->doc->document_pool, style_len + 1);
    if (!text_copy) {
        return 0;
    }

    // Copy text for in-place modification, preserving CSS comments intact.
    // Comments inside custom property values must be preserved per CSS spec.
    // We split by semicolons that are NOT inside comments.
    memcpy(text_copy, style_text, style_len);
    text_copy[style_len] = '\0';

    // Find semicolons not inside comments and replace them with NUL for splitting
    {
        size_t i = 0;
        while (i < style_len) {
            if (i + 1 < style_len && text_copy[i] == '/' && text_copy[i + 1] == '*') {
                // inside comment — skip until closing */
                i += 2;
                while (i + 1 < style_len && !(text_copy[i] == '*' && text_copy[i + 1] == '/')) {
                    i++;
                }
                if (i + 1 < style_len) i += 2; // skip */
            } else if (text_copy[i] == ';') {
                text_copy[i] = '\0';  // split point
                i++;
            } else {
                i++;
            }
        }
    }

    // Iterate over NUL-separated declarations
    size_t offset = 0;
    while (offset < style_len) {
        char* declaration_str = text_copy + offset;
        // advance offset past this segment (to next NUL or end)
        size_t seg_len = strlen(declaration_str);
        offset += seg_len + 1; // +1 for the NUL separator
        // Trim leading whitespace
        while (*declaration_str == ' ' || *declaration_str == '\t' ||
               *declaration_str == '\n' || *declaration_str == '\r') {
            declaration_str++;
        }

        // Skip empty declarations
        if (*declaration_str == '\0') {
            continue;
        }

        // Find the colon separator
        char* colon = strchr(declaration_str, ':');
        if (!colon) {
            continue;
        }

        // Split into property name and value
        *colon = '\0';
        char* prop_name = declaration_str;
        char* prop_value = colon + 1;

        // Trim property name
        char* prop_end = colon - 1;
        while (prop_end >= prop_name && (*prop_end == ' ' || *prop_end == '\t')) {
            *prop_end = '\0';
            prop_end--;
        }

        // Trim property value
        while (*prop_value == ' ' || *prop_value == '\t') {
            prop_value++;
        }
        size_t value_len = strlen(prop_value);
        while (value_len > 0 && (prop_value[value_len - 1] == ' ' ||
                                 prop_value[value_len - 1] == '\t')) {
            prop_value[value_len - 1] = '\0';
            value_len--;
        }

        // Parse the property using the proper CSS tokenizer and parser
        // Format the declaration string for parsing
        size_t decl_str_len = strlen(prop_name) + strlen(prop_value) + 3; // "name: value"
        char* decl_str = (char*)pool_alloc(element->doc->document_pool, decl_str_len);
        if (decl_str) {
            snprintf(decl_str, decl_str_len, "%s:%s", prop_name, prop_value);

            // Tokenize the declaration
            size_t token_count = 0;
            CssToken* tokens = css_tokenize(decl_str, strlen(decl_str), element->doc->document_pool, &token_count);

            if (tokens && token_count > 0) {
                int pos = 0;
                CssDeclaration* decl = css_parse_declaration_from_tokens(tokens, &pos, token_count, element->doc->document_pool);

                if (decl) {
                    // Set origin to author (inline styles are author origin)
                    decl->origin = CSS_ORIGIN_AUTHOR;

                    // Set inline style specificity (1,0,0,0)
                    decl->specificity.inline_style = 1;
                    decl->specificity.ids = 0;
                    decl->specificity.classes = 0;
                    decl->specificity.elements = 0;
                    decl->specificity.important = decl->important; // preserve !important for cascade

                    // Apply to element
                    bool applied = dom_element_apply_declaration(element, decl);
                    if (applied) {
                        applied_count++;
                    }
                }
            }
            // The tokenizer copies retained declaration data into document
            // storage; its mutable source buffer is only parse-call scratch.
            pool_free(element->doc->document_pool, decl_str);
        }
    }

    pool_free(element->doc->document_pool, text_copy);
    return applied_count;
}

/**
 * Get inline style text from an element
 * Returns the style attribute value or NULL if none
 */
const char* dom_element_get_inline_style(DomElement* element) {
    if (!element) {
        return NULL;
    }

    return element->get_attribute("style");
}

/**
 * Remove inline styles from an element
 * Removes all declarations with inline_style specificity
 */
bool dom_element_remove_inline_styles(DomElement* element) {
    if (!element) {
        return false;
    }

    bool removed_attr = element->remove_attribute("style");
    bool removed_decl = dom_element_clear_inline_style_declarations(element);

    if (removed_decl) {
        element->style_version++;
        element->set_needs_style_recompute(true);
        element->set_styles_resolved(false);
    }

    return removed_attr || removed_decl;
}

// ============================================================================
// Style Management
// ============================================================================

bool dom_element_apply_declaration(DomElement* element, CssDeclaration* declaration) {
    if (!element || !declaration) {
        return false;
    }

    if (!style_epoch_ensure_owned(element)) return false;
    g_apply_decl_count++;

    // Check if this is a custom property (CSS variable)
    if (declaration->property_name &&
        declaration->property_name[0] == '-' &&
        declaration->property_name[1] == '-') {

        // Custom property - store in linked list
        log_info("[CSS] Storing custom property: %s", declaration->property_name);

        CssCustomProp* prop = (CssCustomProp*)pool_calloc(element->doc->document_pool, sizeof(CssCustomProp));
        if (!prop) {
            log_error("[CSS] Failed to allocate CssCustomProp");
            return false;
        }

        prop->name = declaration->property_name;
        prop->value = declaration->value;
        prop->value_text = declaration->value_text;
        prop->value_text_len = declaration->value_text_len;
        prop->next = element->css_variables;
        element->css_variables = prop;

        // Increment style version to invalidate caches
        element->style_version++;
        element->set_needs_style_recompute(true);

        return true;
    }

    // Validate the property value before applying
    if (!css_property_validate_value(declaration->property_id, declaration->value)) {
        return false;
    }

    // Apply to specified style tree
    StyleNode* node = style_tree_apply_declaration(element->specified_style, declaration);
    if (!node) {
        return false;
    }

    // Increment style version to invalidate caches
    element->style_version++;
    element->set_needs_style_recompute(true);

    return true;
}

int dom_element_apply_rule(DomElement* element, CssRule* rule, CssSpecificity specificity) {
    if (!element || !rule) {
        return 0;
    }

    if (style_epoch_record_rule(element, rule, specificity)) {
        return (int)rule->data.style_rule.declaration_count;
    }

    int applied_count = 0;

    // Apply each declaration from the rule
    if (rule->type == CSS_RULE_STYLE && rule->data.style_rule.declarations) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (decl) {
                CssDeclaration* element_decl = css_declaration_clone_for_cascade(
                    decl, specificity, rule->origin, element->doc->document_pool);
                if (element_decl && dom_element_apply_declaration(element, element_decl)) {
                    applied_count++;
                }
            }
        }
    }

    return applied_count;
}

CssDeclaration* dom_element_get_specified_value(DomElement* element, CssPropertyId property_id) {
    if (!element || !element->specified_style) {
        return NULL;
    }

    return style_tree_get_declaration(element->specified_style, property_id);
}

bool dom_element_remove_property(DomElement* element, CssPropertyId property_id) {
    if (!element || !element->specified_style) {
        return false;
    }

    if (!style_epoch_ensure_owned(element)) return false;
    bool removed = style_tree_remove_property(element->specified_style, property_id);

    if (removed) {
        element->style_version++;
        element->set_needs_style_recompute(true);
    }

    return removed;
}

bool dom_element_clear_pseudo_styles(DomElement* element) {
    if (!element || !element->ext) return false;

    bool cleared = false;
    for (int kind = 0; kind < PSEUDO_STYLE_COUNT; kind++) {
        StyleTree* style = element->ext->pseudo_styles[kind];
        if (style) {
            // Pseudo rules share the base cascade epoch, so stale :hover
            // declarations must not survive into the next state.
            style_tree_clear(style);
            cleared = true;
        }
    }
    return cleared;
}

// ============================================================================
// Pseudo-Element Style Management (::before, ::after)
// ============================================================================

int dom_element_apply_pseudo_element_rule(DomElement* element, CssRule* rule,
                                          CssSpecificity specificity, int pseudo_element) {
    log_debug("[CSS-PSEUDO] Applying pseudo-element rule to <%s>, pseudo_type=%d",
              element ? element->tag_name : "NULL", pseudo_element);

    if (!element || !rule || !element->doc) {
        log_debug("[CSS-PSEUDO] Early return due to null element/rule/doc");
        return 0;
    }

    // Get the appropriate style tree for the pseudo-element
    StyleTree** target_style = nullptr;
    const char* pseudo_name = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_BEFORE);
        pseudo_name = "::before";
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_AFTER);
        pseudo_name = "::after";
    } else if (pseudo_element == 4) {  // PSEUDO_ELEMENT_FIRST_LETTER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_FIRST_LETTER);
        pseudo_name = "::first-letter";
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_MARKER);
        pseudo_name = "::marker";
    } else if (pseudo_element == 7) {  // PSEUDO_ELEMENT_PLACEHOLDER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_PLACEHOLDER);
        pseudo_name = "::placeholder";
    } else {
        log_debug("[CSS] Unknown pseudo-element type: %d", pseudo_element);
        return 0;
    }

    // Create style tree if needed
    if (!*target_style) {
        *target_style = style_tree_create(element->doc->document_pool);
        if (!*target_style) {
            log_error("[CSS] Failed to create style tree for %s", pseudo_name);
            return 0;
        }
    }

    int applied_count = 0;

    // Apply each declaration from the rule
    if (rule->type == CSS_RULE_STYLE && rule->data.style_rule.declarations) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (decl) {
                CssDeclaration* element_decl = css_declaration_clone_for_cascade(
                    decl, specificity, rule->origin, element->doc->document_pool);
                if (!element_decl) continue;

                // Apply to pseudo-element style tree
                if (style_tree_apply_declaration(*target_style, element_decl)) {
                    applied_count++;
                    log_debug("[CSS] Applied %s property %d to <%s>",
                              pseudo_name, element_decl->property_id, element->tag_name);
                }
            }
        }
    }

    if (applied_count > 0) {
        element->style_version++;
        element->set_needs_style_recompute(true);
    }

    return applied_count;
}

CssDeclaration* dom_element_get_pseudo_element_value(DomElement* element,
                                                     CssPropertyId property_id, int pseudo_element) {
    if (!element) {
        return NULL;
    }

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->pseudo_style(PSEUDO_STYLE_BEFORE);
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->pseudo_style(PSEUDO_STYLE_AFTER);
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->pseudo_style(PSEUDO_STYLE_MARKER);
    } else if (pseudo_element == 7) {  // PSEUDO_ELEMENT_PLACEHOLDER
        style = element->pseudo_style(PSEUDO_STYLE_PLACEHOLDER);
    }

    if (!style) {
        return NULL;
    }

    return style_tree_get_declaration(style, property_id);
}

bool dom_element_has_before_content(DomElement* element) {
    if (!element || !element->pseudo_style(PSEUDO_STYLE_BEFORE)) {
        return false;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(
        element->pseudo_style(PSEUDO_STYLE_BEFORE), CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        return false;
    }

    // Check if content value is not 'none' or 'normal'
    CssValue* value = content_decl->value;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_NONE ||
            value->data.keyword == CSS_VALUE_NORMAL) {
            return false;
        }
    }

    return true;
}

bool dom_element_has_after_content(DomElement* element) {
    if (!element || !element->pseudo_style(PSEUDO_STYLE_AFTER)) {
        return false;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(
        element->pseudo_style(PSEUDO_STYLE_AFTER), CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        return false;
    }

    // Check if content value is not 'none' or 'normal'
    CssValue* value = content_decl->value;
    if (value->type == CSS_VALUE_TYPE_KEYWORD) {
        if (value->data.keyword == CSS_VALUE_NONE ||
            value->data.keyword == CSS_VALUE_NORMAL) {
            return false;
        }
    }

    return true;
}

/**
 * Resolve a CSS quote character (open-quote or close-quote) for an element.
 * Walks up the DOM tree to find the 'quotes' property, parses quote pairs,
 * and returns the appropriate character at the given depth.
 * CSS 2.1 §12.3.2: Quotes are nested; depth determines which pair to use.
 *
 * @param element The element with the ::before/::after pseudo-element
 * @param is_open_quote true for open-quote, false for close-quote
 * @param depth Quote nesting depth (0 = outermost)
 * @return The quote character string, or default quotes if none specified
 */
static const char* resolve_quote_char(DomElement* element, bool is_open_quote, int depth) {
    // Walk up DOM tree to find the 'quotes' property (it's inherited)
    DomElement* cur = element;
    CssDeclaration* quotes_decl = NULL;
    while (cur) {
        quotes_decl = dom_element_get_specified_value(cur, CSS_PROPERTY_QUOTES);
        if (quotes_decl && quotes_decl->value) break;
        cur = cur->parent_element();
    }

    if (!quotes_decl || !quotes_decl->value) {
        // Default quotes per CSS 2.1: use typographic quotes
        return is_open_quote ? "\xe2\x80\x9c" : "\xe2\x80\x9d";  // U+201C / U+201D
    }

    CssValue* qval = quotes_decl->value;

    // quotes: none
    if (qval->type == CSS_VALUE_TYPE_KEYWORD && qval->data.keyword == CSS_VALUE_NONE) {
        return "";
    }

    // quotes: "open1" "close1" "open2" "close2" ...
    // Parsed as a CSS_VALUE_TYPE_LIST of strings
    if (qval->type == CSS_VALUE_TYPE_LIST && qval->data.list.count >= 2) {
        int pair_count = qval->data.list.count / 2;
        int pair_index = depth < pair_count ? depth : pair_count - 1;  // CSS 2.1: use last pair for deeper nesting
        int str_index = pair_index * 2 + (is_open_quote ? 0 : 1);
        if (str_index < qval->data.list.count) {
            CssValue* sv = qval->data.list.values[str_index];
            if (sv && sv->type == CSS_VALUE_TYPE_STRING && sv->data.string) {
                log_debug("[QUOTES] depth=%d, pair=%d, %s='%s'",
                    depth, pair_index, is_open_quote ? "open" : "close", sv->data.string);
                return sv->data.string;
            }
        }
    }

    // quotes: "open" "close" (single pair, may be stored as 2 strings in a list or other format)
    if (qval->type == CSS_VALUE_TYPE_STRING && qval->data.string) {
        // Single string — shouldn't normally happen for quotes, but handle gracefully
        return qval->data.string;
    }

    return is_open_quote ? "\xe2\x80\x9c" : "\xe2\x80\x9d";  // fallback
}

/**
 * Check if a CssValue represents an open-quote or close-quote content value.
 * Returns 1 for open-quote, 2 for close-quote, 0 for neither.
 */
static int check_quote_content(CssValue* value) {
    if (!value) return 0;
    if (value->type == CSS_VALUE_TYPE_CUSTOM && value->data.custom_property.name) {
        if (strcmp(value->data.custom_property.name, "open-quote") == 0) return 1;
        if (strcmp(value->data.custom_property.name, "close-quote") == 0) return 2;
        if (strcmp(value->data.custom_property.name, "no-open-quote") == 0) return 3;
        if (strcmp(value->data.custom_property.name, "no-close-quote") == 0) return 4;
    }
    return 0;
}

const char* dom_element_get_pseudo_element_content(DomElement* element, int pseudo_element) {
    log_info("[PSEUDO CONTENT GET ENTRY] element=%p, pseudo=%d, tag=%s",
        (void*)element, pseudo_element,
        element ? (element->tag_name ? element->tag_name : "?") : "NULL");

    if (!element) {
        log_info("[PSEUDO CONTENT GET] element is NULL, returning NULL");
        return NULL;
    }

    log_info("[PSEUDO CONTENT GET] Called for element <%s>, pseudo=%d",
        element->tag_name ? element->tag_name : "?", pseudo_element);

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->pseudo_style(PSEUDO_STYLE_BEFORE);
        log_info("[PSEUDO CONTENT GET] before_styles=%p", (void*)style);
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->pseudo_style(PSEUDO_STYLE_AFTER);
        log_info("[PSEUDO CONTENT GET] after_styles=%p", (void*)style);
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->pseudo_style(PSEUDO_STYLE_MARKER);
        log_info("[PSEUDO CONTENT GET] marker_styles=%p", (void*)style);
    }    if (!style) {
        log_info("[PSEUDO CONTENT GET] No style tree found");
        return NULL;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(style, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        log_info("[PSEUDO CONTENT GET] No content declaration found");
        return NULL;
    }

    CssValue* value = content_decl->value;
    log_info("[PSEUDO CONTENT GET] Found content value, type=%d", value->type);

    // Return the string content
    if (value->type == CSS_VALUE_TYPE_STRING) {
        const char* str = value->data.string;
        size_t len = str ? strlen(str) : 0;
        log_info("[PSEUDO CONTENT] Extracted STRING content, len=%zu, bytes=[%02x %02x %02x]",
            len, len > 0 ? (unsigned char)str[0] : 0, len > 1 ? (unsigned char)str[1] : 0, len > 2 ? (unsigned char)str[2] : 0);
        return str;
    }

    // Handle attr() function: content: attr(attribute-name)
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        CssFunction* func = value->data.function;
        if (func && func->name && strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
            // extract the attribute name from the first argument
            const char* attr_name = NULL;
            CssValue* arg0 = func->args[0];
            if (arg0) {
                if (arg0->type == CSS_VALUE_TYPE_STRING) {
                    attr_name = arg0->data.string;
                } else if (arg0->type == CSS_VALUE_TYPE_KEYWORD) {
                    // known keyword used as attribute name (e.g., "class")
                    const CssEnumInfo* info = css_enum_info(arg0->data.keyword);
                    attr_name = info ? info->name : NULL;
                } else if (arg0->type == CSS_VALUE_TYPE_CUSTOM) {
                    // unknown ident parsed as custom property (e.g., "data-val")
                    attr_name = arg0->data.custom_property.name;
                }
            }
            if (attr_name) {
                const char* attr_value = element->get_attribute(attr_name);
                log_info("[PSEUDO CONTENT] attr(%s) => '%s'", attr_name, attr_value ? attr_value : "NULL");
                return attr_value ? attr_value : "";
            }
        }
    }

    // Handle attr() via CSS_VALUE_TYPE_ATTR (parsed by CSS value parser)
    if (value->type == CSS_VALUE_TYPE_ATTR) {
        CSSAttrRef* attr_ref = value->data.attr_ref;
        if (attr_ref && attr_ref->name) {
            const char* attr_value = element->get_attribute(attr_ref->name);
            log_info("[PSEUDO CONTENT] attr(%s) => '%s'", attr_ref->name, attr_value ? attr_value : "NULL");
            return attr_value ? attr_value : "";
        }
    }

    // Handle open-quote / close-quote (CSS_VALUE_TYPE_CUSTOM with ident name)
    int quote_type = check_quote_content(value);
    if (quote_type == 1 || quote_type == 2) {
        return resolve_quote_char(element, quote_type == 1, 0);
    }
    if (quote_type == 3 || quote_type == 4) {
        return "";  // no-open-quote / no-close-quote: affect depth only, generate nothing
    }

    // Handle list of values (for content with multiple parts)
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        // For now, return the first string value
        CssValue* first = value->data.list.values[0];
        if (first && first->type == CSS_VALUE_TYPE_STRING) {
            const char* str = first->data.string;
            size_t len = str ? strlen(str) : 0;
            log_info("[PSEUDO CONTENT] Extracted LIST content, len=%zu, bytes=[%02x %02x %02x]",
                len, len > 0 ? (unsigned char)str[0] : 0, len > 1 ? (unsigned char)str[1] : 0, len > 2 ? (unsigned char)str[2] : 0);
            return str;
        }
    }

    log_info("[PSEUDO CONTENT] No string content found (type=%d)", value->type);
    return NULL;
}

/**
 * Get pseudo-element content with counter resolution
 * This version handles counter() and counters() functions
 */
const char* dom_element_get_pseudo_element_content_with_counters(
    DomElement* element, int pseudo_element, void* counter_context, Arena* arena) {

    log_info("[PSEUDO CONTENT WITH COUNTERS] Called: element=%p <%s>, pseudo=%d",
        (void*)element, element ? (element->tag_name ? element->tag_name : "?") : "NULL", pseudo_element);

    if (!element || !arena) {
        log_info("[PSEUDO CONTENT WITH COUNTERS] element or arena is NULL");
        return NULL;
    }

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->pseudo_style(PSEUDO_STYLE_BEFORE);
        log_info("[PSEUDO CONTENT WITH COUNTERS] before_styles=%p", (void*)style);
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->pseudo_style(PSEUDO_STYLE_AFTER);
        log_info("[PSEUDO CONTENT WITH COUNTERS] after_styles=%p", (void*)style);
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->pseudo_style(PSEUDO_STYLE_MARKER);
        log_info("[PSEUDO CONTENT WITH COUNTERS] marker_styles=%p", (void*)style);
    }

    if (!style) {
        log_info("[PSEUDO CONTENT WITH COUNTERS] No style tree");
        return NULL;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(style, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        log_info("[PSEUDO CONTENT WITH COUNTERS] No content declaration");
        return NULL;
    }

    CssValue* value = content_decl->value;
    log_info("[PSEUDO CONTENT WITH COUNTERS] Found content value, type=%d", value->type);

    // Return string content directly
    if (value->type == CSS_VALUE_TYPE_STRING) {
        const char* str = value->data.string;
        size_t len = str ? strlen(str) : 0;
        log_info("[PSEUDO CONTENT WITH COUNTERS] STRING content, len=%zu, bytes=[%02x %02x %02x]",
            len, len > 0 ? (unsigned char)str[0] : 0, len > 1 ? (unsigned char)str[1] : 0, len > 2 ? (unsigned char)str[2] : 0);
        return str;
    }

    // Handle attr() via CSS_VALUE_TYPE_ATTR
    if (value->type == CSS_VALUE_TYPE_ATTR) {
        CSSAttrRef* attr_ref = value->data.attr_ref;
        if (attr_ref && attr_ref->name) {
            const char* attr_value = element->get_attribute(attr_ref->name);
            log_info("[PSEUDO CONTENT WITH COUNTERS] attr(%s) => '%s'", attr_ref->name, attr_value ? attr_value : "NULL");
            return attr_value ? attr_value : "";
        }
    }

    // Handle open-quote / close-quote (CSS_VALUE_TYPE_CUSTOM with ident name)
    {
        int quote_type = check_quote_content(value);
        if (quote_type == 1 || quote_type == 2) {
            return resolve_quote_char(element, quote_type == 1, 0);
        }
        if (quote_type == 3 || quote_type == 4) {
            return "";  // no-open-quote / no-close-quote
        }
    }

    // Handle counter() or counters() function
    if (value->type == CSS_VALUE_TYPE_FUNCTION) {
        CssFunction* func = value->data.function;
        log_debug("[Counter] Found function in content: %s (arg_count=%d)",
                 func ? func->name : "NULL", func ? func->arg_count : 0);
        if (func && func->name && counter_context) {
            if (strcmp(func->name, "counter") == 0) {
                // counter(name) or counter(name, style)
                // Parse arguments: name (identifier), optional style (keyword)
                if (func->arg_count >= 1) {
                    log_debug("[Counter] counter() arg[0] type=%d", (int)func->args[0]->type);

                    const char* counter_name = css_value_extract_name(func->args[0]);

                    uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)

                    if (func->arg_count >= 2 && func->args[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                        style_type = func->args[1]->data.keyword;
                        const CssEnumInfo* style_info = css_enum_info((CssEnum)style_type);
                        log_debug("[Counter] counter style keyword: %u (%s)",
                                style_type, style_info ? style_info->name : "unknown");
                    }

                    // Format counter value
                    char* buffer = (char*)arena_alloc(arena, 64);
                    if (buffer && counter_name) {
                        css_counter_format(counter_context, counter_name,
                                     style_type, buffer, 64);
                        log_debug("[Counter] counter(%s, style=%u) = '%s'", counter_name, style_type, buffer);
                        return buffer;
                    }
                }
            } else if (strcmp(func->name, "counters") == 0) {
                // counters(name, separator) or counters(name, separator, style)
                if (func->arg_count >= 2) {
                    const char* counter_name = css_value_extract_name(func->args[0]);

                    const char* separator = func->args[1]->data.string;
                    uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)

                    if (func->arg_count >= 3 && func->args[2]->type == CSS_VALUE_TYPE_KEYWORD) {
                        style_type = func->args[2]->data.keyword;
                    }

                    // Format counters with separator
                    char* buffer = (char*)arena_alloc(arena, 128);
                    if (buffer && counter_name) {
                        css_counters_format(counter_context, counter_name,
                                      separator ? separator : ".", style_type, buffer, 128);
                        log_debug("[Counter] counters(%s, \"%s\") = %s",
                                counter_name, separator ? separator : ".", buffer);
                        return buffer;
                    }
                }
            } else if (strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
                // attr(attribute-name) in content property
                const char* attr_name = css_value_extract_name(func->args[0]);
                if (attr_name) {
                    const char* attr_value = element->get_attribute(attr_name);
                    log_info("[PSEUDO CONTENT WITH COUNTERS] attr(%s) => '%s'", attr_name, attr_value ? attr_value : "NULL");
                    return attr_value ? attr_value : "";
                }
            }
        }
    }

    // Handle list of values (for content with multiple parts, e.g., counter(c) "text")
    if (value->type == CSS_VALUE_TYPE_LIST && value->data.list.count > 0) {
        log_debug("[Counter] Processing content list with %d values", value->data.list.count);

        // Use a fixed-size buffer for concatenation
        char result_buffer[512];
        result_buffer[0] = '\0';
        int result_len = 0;

        // Concatenate all values in the list
        for (int i = 0; i < value->data.list.count; i++) {
            CssValue* item = value->data.list.values[i];
            if (!item) continue;

            if (item->type == CSS_VALUE_TYPE_STRING && item->data.string) {
                // Append string content
                int str_len = strlen(item->data.string);
                if (result_len + str_len < (int)sizeof(result_buffer) - 1) {
                    memcpy(result_buffer + result_len, item->data.string, str_len);
                    result_len += str_len;
                    result_buffer[result_len] = '\0';
                    log_debug("[Counter] Appended string: '%s'", item->data.string);
                }
            } else if (item->type == CSS_VALUE_TYPE_FUNCTION) {
                // Handle counter() or counters() in list
                CssFunction* func = item->data.function;
                log_debug("[Counter] Processing function in list: %s", func ? func->name : "NULL");
                if (func && func->name && counter_context) {
                    char temp_buffer[128];
                    temp_buffer[0] = '\0';

                    if (strcmp(func->name, "counter") == 0 && func->arg_count >= 1) {
                        const char* counter_name = css_value_extract_name(func->args[0]);

                        uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)
                        if (func->arg_count >= 2 && func->args[1]->type == CSS_VALUE_TYPE_KEYWORD) {
                            style_type = func->args[1]->data.keyword;
                            const CssEnumInfo* style_info = css_enum_info((CssEnum)style_type);
                            log_debug("[Counter] counter style keyword: %u (%s)",
                                    style_type, style_info ? style_info->name : "unknown");
                        }

                        if (counter_name) {
                            css_counter_format(counter_context, counter_name,
                                         style_type, temp_buffer, sizeof(temp_buffer));
                            int temp_len = strlen(temp_buffer);
                            if (result_len + temp_len < (int)sizeof(result_buffer) - 1) {
                                memcpy(result_buffer + result_len, temp_buffer, temp_len);
                                result_len += temp_len;
                                result_buffer[result_len] = '\0';
                            }
                            log_debug("[Counter] counter(%s, style=%u) = '%s'", counter_name, style_type, temp_buffer);
                        }
                    } else if (strcmp(func->name, "counters") == 0 && func->arg_count >= 2) {
                        const char* counter_name = css_value_extract_name(func->args[0]);

                        const char* separator = func->args[1]->data.string;
                        uint32_t style_type = 0x00AA;  // CSS_VALUE_DECIMAL (default)
                        if (func->arg_count >= 3 && func->args[2]->type == CSS_VALUE_TYPE_KEYWORD) {
                            style_type = func->args[2]->data.keyword;
                        }

                        if (counter_name) {
                            css_counters_format(counter_context, counter_name,
                                          separator ? separator : ".", style_type,
                                          temp_buffer, sizeof(temp_buffer));
                            int temp_len = strlen(temp_buffer);
                            if (result_len + temp_len < (int)sizeof(result_buffer) - 1) {
                                memcpy(result_buffer + result_len, temp_buffer, temp_len);
                                result_len += temp_len;
                                result_buffer[result_len] = '\0';
                            }
                            log_debug("[Counter] counters(%s, '%s', style=%u) = '%s'",
                                    counter_name, separator ? separator : ".", style_type, temp_buffer);
                        }
                    }
                }
                if (func && func->name && strcmp(func->name, "attr") == 0 && func->arg_count > 0) {
                    // Handle attr() function in list
                    const char* attr_name = css_value_extract_name(func->args[0]);
                    if (attr_name) {
                        const char* attr_value = element->get_attribute(attr_name);
                        if (attr_value) {
                            int attr_len = strlen(attr_value);
                            if (result_len + attr_len < (int)sizeof(result_buffer) - 1) {
                                memcpy(result_buffer + result_len, attr_value, attr_len);
                                result_len += attr_len;
                                result_buffer[result_len] = '\0';
                            }
                            log_debug("[Counter] Appended attr(%s) = '%s'", attr_name, attr_value);
                        }
                    }
                }
            } else if (item->type == CSS_VALUE_TYPE_ATTR) {
                // Handle attr() in list: content: "text" attr(class) "more text"
                CSSAttrRef* attr_ref = item->data.attr_ref;
                if (attr_ref && attr_ref->name) {
                    const char* attr_value = element->get_attribute(attr_ref->name);
                    if (attr_value) {
                        int attr_len = strlen(attr_value);
                        if (result_len + attr_len < (int)sizeof(result_buffer) - 1) {
                            memcpy(result_buffer + result_len, attr_value, attr_len);
                            result_len += attr_len;
                            result_buffer[result_len] = '\0';
                        }
                        log_debug("[Counter] Appended attr(%s) = '%s'", attr_ref->name, attr_value);
                    }
                }
            } else {
                // Handle open-quote / close-quote in list
                int qt = check_quote_content(item);
                if (qt == 1 || qt == 2) {
                    // Track depth based on position of quote in the list
                    int quote_depth = 0;
                    for (int j = 0; j < i; j++) {
                        int prev_qt = check_quote_content(value->data.list.values[j]);
                        if (prev_qt == 1 || prev_qt == 3) quote_depth++;
                    }
                    const char* qc = resolve_quote_char(element, qt == 1, quote_depth);
                    if (qc) {
                        int qlen = strlen(qc);
                        if (result_len + qlen < (int)sizeof(result_buffer) - 1) {
                            memcpy(result_buffer + result_len, qc, qlen);
                            result_len += qlen;
                            result_buffer[result_len] = '\0';
                        }
                        log_debug("[Counter] Appended %s = '%s'", qt == 1 ? "open-quote" : "close-quote", qc);
                    }
                }
                // no-open-quote / no-close-quote: generate nothing but affect depth
            }
        }

        // Copy result to arena-allocated buffer
        if (result_len > 0) {
            char* result = (char*)arena_alloc(arena, result_len + 1);
            if (result) {
                memcpy(result, result_buffer, result_len);
                result[result_len] = '\0';
                log_debug("[Counter] Final content: '%s'", result);
                return result;
            }
        }
    }

    return NULL;
}

// ============================================================================
// DOM Tree Navigation
// ============================================================================

DomElement* DomElement::parent_element() const {
    return static_cast<DomElement*>(parent);
}

DomElement* DomElement::first_child_element() const {
    return static_cast<DomElement*>(first_child);
}

DomElement* DomElement::last_child_element() const {
    return static_cast<DomElement*>(last_child);
}

DomElement* DomElement::next_sibling_element() const {
    return static_cast<DomElement*>(next_sibling);
}

DomElement* DomElement::prev_sibling_element() const {
    return static_cast<DomElement*>(prev_sibling);
}

/**
 * Link child element to parent in DOM sibling chain only.
 * Use this when the child is ALREADY in the parent's Lambda tree.
 * Does NOT modify the Lambda tree - only updates DOM navigation pointers.
 *
 * @param parent Parent element
 * @param child Child element to link (must already exist in parent's Lambda tree)
 * @return true on success, false on error
 */
bool DomElement::link_child(DomElement* child) {
    DomElement* parent = this;
    if (!child) {
        log_error("dom_element_link_child: invalid arguments");
        return false;
    }

    // Add to parent's DOM sibling chain
    dom_append_to_sibling_chain(parent, child);

    log_debug("dom_element_link_child: linked child to DOM chain (Lambda tree unchanged)");
    return true;
}

/**
 * Append child element to parent, updating BOTH Lambda tree AND DOM sibling chain.
 * Use this when adding a NEW child that is NOT yet in the parent's Lambda tree.
 *
 * For children already in the Lambda tree (e.g., when building DOM wrappers from
 * existing Lambda structures), use link_child() instead.
 *
 * @param parent Parent element (must have Lambda backing)
 * @param child Child element (must have Lambda backing)
 * @return true on success, false on error
 */
bool DomElement::append_child(DomElement* child) {
    DomElement* parent = this;
    if (!child) {
        log_error("dom_element_append_child: invalid arguments");
        return false;
    }

    if (parent->is_synthetic() || child->is_synthetic()) {
        // Generated boxes have no Lambda tree to edit; method overload resolution
        // still selects this DomElement overload, so preserve the DomNode chain path.
        return DomNode::append_child(static_cast<DomNode*>(child));
    }
    if (!parent->doc || !parent->doc->input) {
        log_error("dom_element_append_child: backed parent requires an input context");
        return false;
    }

    Element* parent_backing = dom_element_to_element(parent);
    Element* child_backing = dom_element_to_element(child);
    log_debug("dom_element_append_child: appending to Lambda tree (length before=%lld)", parent_backing->length);

    // Append to Lambda tree using MarkEditor
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_append_child(
        {.element = parent_backing},
        {.element = child_backing}
    );

    if (!result.element) {
        log_error("dom_element_append_child: failed to append to Lambda tree");
        return false;
    }

    if (result.element != parent_backing) {
        log_error("dom_element_append_child: inline editor changed backing identity");
        return false;
    }
    log_debug("dom_element_append_child: Lambda tree updated (length after=%lld)", parent_backing->length);

    // Update DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already linked)
    if (!parent->doc->input->ui_mode) {
        dom_append_to_sibling_chain(parent, child);
    }

    log_debug("dom_element_append_child: appended element to parent (both Lambda tree and DOM chain updated)");

    return true;
}

bool DomElement::remove_child(DomElement* child) {
    DomElement* parent = this;
    if (!child || child->parent != parent) {
        return false;
    }

    // Update sibling links - polymorphic base class handles this
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        // Child was first child
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        // Child was last child
        parent->last_child = child->prev_sibling;
    }

    // Clear child's parent relationship
    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    dom_node_schedule_detached(parent->doc, child);

    return true;
}

bool DomElement::insert_before(DomElement* new_child, DomElement* reference_child) {
    DomElement* parent = this;
    if (!new_child) {
        return false;
    }

    // If no reference child, append at end
    if (!reference_child) {
        return append_child(new_child);
    }

    // Verify reference child is actually a child of parent
    if (reference_child->parent != parent) {
        return false;
    }

    dom_node_cancel_detached(parent->doc, new_child);

    // Set parent relationship
    new_child->parent = parent;

    // Insert before reference child
    new_child->next_sibling = reference_child;
    new_child->prev_sibling = reference_child->prev_sibling;

    if (reference_child->prev_sibling) {
        reference_child->prev_sibling->next_sibling = new_child;
    } else {
        // Reference child was first child
        parent->first_child = new_child;
    }

    reference_child->prev_sibling = new_child;

    // If inserting before first child, update last_child if needed
    if (!new_child->next_sibling) {
        parent->last_child = new_child;
    }

    // Invalidate new child's computed values
    // dom_element_invalidate_computed_values(new_child, true);

    return true;
}

bool dom_node_replace_in_parent(DomElement* parent, DomNode* old_child, DomNode* new_child) {
    if (!parent || !old_child || !new_child) return false;
    if (old_child->parent != parent) return false;

    // Reinsertion before the retirement checkpoint cancels deferred recycling.
    dom_node_cancel_detached(parent->doc, new_child);

    // splice new_child into old_child's position in the linked list
    new_child->parent = parent;
    new_child->prev_sibling = old_child->prev_sibling;
    new_child->next_sibling = old_child->next_sibling;

    if (old_child->prev_sibling) {
        old_child->prev_sibling->next_sibling = new_child;
    } else {
        parent->first_child = new_child;
    }

    if (old_child->next_sibling) {
        old_child->next_sibling->prev_sibling = new_child;
    } else {
        parent->last_child = new_child;
    }

    old_child->parent = nullptr;
    old_child->prev_sibling = nullptr;
    old_child->next_sibling = nullptr;
    dom_node_schedule_detached(parent->doc, old_child);
    return true;
}

// ============================================================================
// Structural Queries
// ============================================================================

bool DomElement::is_first_child() {
    DomElement* element = this;
    if (!element->parent) {
        return false;
    }

    // CSS 2.1 §5.11.1: :first-child matches an element that is the first
    // child ELEMENT of its parent. Text nodes are not counted.
    DomElement* parent = static_cast<DomElement*>(element->parent);
    DomNode* child = parent->first_child;
    while (child) {
        if (child->is_element()) {
            return child == (DomNode*)element;
        }
        child = child->next_sibling;
    }
    return false;
}

bool DomElement::is_last_child() {
    DomElement* element = this;
    if (!element->parent) {
        return false;
    }

    // CSS 2.1 §5.11.1: :last-child matches an element that is the last
    // child ELEMENT of its parent. Text nodes after it are not counted.
    DomNode* sibling = element->next_sibling;
    while (sibling) {
        if (sibling->is_element()) {
            return false;
        }
        sibling = sibling->next_sibling;
    }
    return true;
}

bool DomElement::is_only_child() {
    DomElement* element = this;
    if (!element->parent) {
        return false;
    }

    // CSS Selectors §6.6.1.6: :only-child matches when the element is the
    // only child ELEMENT of its parent. Equivalent to :first-child:last-child.
    return is_first_child() && is_last_child();
}

int DomElement::child_index() {
    DomElement* element = this;
    if (!element->parent) {
        return -1;
    }

    // Count only element children (not text nodes or comments)
    // According to CSS spec, :nth-child() counts only element nodes
    int index = 0;
    DomElement* parent = static_cast<DomElement*>(element->parent);
    DomNode* sibling = parent->first_child;

    while (sibling && sibling != element) {
        // Only count element nodes for nth-child
        if (sibling->is_element()) {
            index++;
        }
        sibling = sibling->next_sibling;
    }

    return (sibling == element) ? index : -1;
}

int DomElement::child_count() {
    DomElement* element = this;

    int count = 0;
    DomNode* child = element->first_child;

    while (child) {
        count++;
        child = child->next_sibling;
    }

    return count;
}

int DomElement::count_child_elements() {
    DomElement* element = this;

    int count = 0;
    DomNode* child = element->first_child;

    while (child) {
        // Only count element children (not text or comment nodes)
        if (child->is_element()) {
            count++;
        }
        child = child->next_sibling;
    }

    return count;
}

bool DomElement::matches_nth_child(int a, int b) {
    int index = child_index();
    if (index < 0) {
        return false;
    }

    // nth-child is 1-based
    int n = index + 1;

    // Check if n matches an+b for some non-negative integer
    if (a == 0) {
        return n == b;
    }

    int diff = n - b;
    if (diff < 0) {
        return false;
    }

    return (diff % a) == 0;
}

// ============================================================================
// Utility Functions
// ============================================================================

void dom_element_get_style_stats(DomElement* element,
                                 int* specified_count,
                                 int* computed_count,
                                 int* total_declarations) {
    if (!element) {
        if (specified_count) *specified_count = 0;
        if (computed_count) *computed_count = 0;
        if (total_declarations) *total_declarations = 0;
        return;
    }

    int total_nodes = 0;
    int total_decls = 0;
    double avg_weak = 0.0;

    if (element->specified_style) {
        style_tree_get_statistics(element->specified_style, &total_nodes, &total_decls, &avg_weak);
        if (specified_count) *specified_count = total_nodes;
        if (total_declarations) *total_declarations = total_decls;
    }
}

DomElement* dom_element_clone(DomElement* source, Pool* pool) {
    if (!source || !pool) {
        return NULL;
    }

    // All DomElements must have backing Lambda element
    if (source->is_synthetic() || !source->doc) {
        log_error("dom_element_clone: source element must have Lambda backing and doc");
        return NULL;
    }

    // Use MarkBuilder to deep copy the backing Lambda element
    MarkBuilder builder(source->doc->input);
    Item cloned_elem = builder.deep_copy({.element = dom_element_to_element(source)});

    if (!cloned_elem.element) {
        log_error("dom_element_clone: MarkBuilder deep_copy failed");
        return NULL;
    }

    // Create a new document for the clone (using the same input)
    DomDocument* clone_doc = dom_document_create(source->doc->input);
    if (!clone_doc) {
        log_error("dom_element_clone: failed to create document for clone");
        return NULL;
    }

    // Build DomElement wrapper from the cloned Lambda element
    DomElement* clone = build_dom_tree_from_element(cloned_elem.element, clone_doc, nullptr);
    if (!clone) {
        log_error("dom_element_clone: build_dom_tree_from_element failed");
        dom_document_destroy(clone_doc);
        return NULL;
    }

    // Copy classes (if not already copied by build_dom_tree_from_element)
    for (int i = 0; i < source->class_count; i++) {
        if (!clone->has_class(source->class_names[i])) {
            clone->add_class(source->class_names[i]);
        }
    }

    // Copy style trees. NOTE: style_tree_clone is a SHALLOW clone — the cloned
    // tree shares (refcounts) the source's CssDeclaration/CssValue objects, which
    // remain owned by the SOURCE document's pool (see style_tree_clone contract).
    // The source document must outlive this clone. A true deep copy independent of
    // the source pool is not yet available.
    if (source->specified_style) {
        clone->specified_style = style_tree_clone(source->specified_style, pool);
    }

    // Note: Children are not cloned - caller should handle that if needed

    return clone;
}// ============================================================================
// DOM Text Node Implementation
// ============================================================================

DomText* DomText::create_in(Arena* arena) {
    if (!arena) return nullptr;
    // Arena zeroing is the construction contract; only the discriminator is non-zero.
    DomText* text_node = (DomText*)arena_calloc(arena, sizeof(DomText));
    if (text_node) text_node->node_type = DOM_NODE_TEXT;
    return text_node;
}

DomText* DomText::create_in(Pool* pool) {
    if (!pool) return nullptr;
    DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
    if (text_node) text_node->node_type = DOM_NODE_TEXT;
    return text_node;
}

DomText* DomText::create(String* native_string, DomElement* parent_element) {
    if (!native_string || !parent_element) {
        log_error("DomText::create: native_string and parent_element required");
        return nullptr;
    }

    if (!parent_element->doc) {
        log_error("DomText::create: parent_element has no document");
        return nullptr;
    }

    DomText* text_node = create_detached(native_string, parent_element->doc);
    if (!text_node) return nullptr;
    text_node->parent = parent_element;

    log_debug("DomText::create: created backed text node, text='%s'", native_string->chars);
    return text_node;
}

DomText* DomText::create_copy(const char* text, size_t len,
                              DomElement* parent_element) {
    if (!parent_element || !parent_element->doc || (!text && len)) return nullptr;
    DomText* text_node = create_detached_copy(parent_element->doc, text, len);
    if (text_node) text_node->parent = parent_element;
    return text_node;
}

DomText* DomText::create_detached(String* native_string, DomDocument* doc) {
    if (!native_string) {
        log_error("DomText::create_detached: native_string required");
        return nullptr;
    }
    if (!doc || !doc->node_arena) {
        log_error("DomText::create_detached: doc with arena required");
        return nullptr;
    }

    // Arena zeroing supplies every null/zero default omitted below.
    DomText* text_node = create_in(doc->node_arena);
    if (!text_node) {
        log_error("DomText::create_detached: arena_calloc failed");
        return nullptr;
    }

    text_node->id = dom_document_alloc_node_id(doc);
    text_node->native_string = native_string;
    text_node->text = native_string->chars;
    text_node->length = native_string->len;

    if (!dom_node_registry_register(doc, text_node, sizeof(DomText), true)) {
        return nullptr;
    }

    return text_node;
}

String* dom_document_create_string(DomDocument* doc, const char* text, size_t len) {
    if (!doc || !doc->document_pool || (!text && len > 0)) return nullptr;
    String* string = (String*)pool_alloc(doc->document_pool, sizeof(String) + len + 1);
    if (!string) return nullptr;
    string->len = (uint32_t)len;
    string->is_ascii = str_is_ascii(text ? text : "", len) ? 1 : 0;
    if (len > 0) memcpy(string->chars, text, len);
    string->chars[len] = '\0';
    return string;
}

bool dom_text_adopt_document_string(DomText* text_node, DomDocument* doc,
                                    String* string) {
    if (!text_node || !doc || !doc->document_pool || !string) return false;
    if (text_node->owns_native_string() && text_node->native_string != string) {
        // Generated and mutation strings have single-node ownership; replacing
        // one must reclaim it immediately instead of waiting for document exit.
        pool_free(doc->document_pool, text_node->native_string);
    }
    text_node->native_string = string;
    text_node->text = string->chars;
    text_node->length = string->len;
    text_node->set_owns_native_string(true);
    return true;
}

void dom_text_release_retired_storage(DomDocument* doc, DomText* text_node) {
    if (!doc || !doc->document_pool || !text_node ||
        !text_node->owns_native_string()) return;
    pool_free(doc->document_pool, text_node->native_string);
    text_node->native_string = nullptr;
    text_node->text = nullptr;
    text_node->length = 0;
    text_node->set_owns_native_string(false);
}

DomText* DomText::create_detached_copy(DomDocument* doc,
                                       const char* text, size_t len) {
    if (!doc || !doc->node_arena || (!text && len)) return nullptr;
    DomText* text_node = create_in(doc->node_arena, len);
    if (!text_node) return nullptr;
    String* string = dom_text_to_string(text_node);
    string->is_ascii = str_is_ascii(text ? text : "", len) ? 1 : 0;
    if (len) memcpy(string->chars, text, len);
    string->chars[len] = '\0';
    text_node->id = dom_document_alloc_node_id(doc);
    size_t primary_size = sizeof(DomText) + sizeof(String) + len + 1;
    if (!dom_node_registry_register(doc, text_node, primary_size, true)) return nullptr;
    return text_node;
}

DomText* DomText::create_symbol(const char* name, size_t len,
                                DomElement* parent_element) {
    if (!name || len == 0 || !parent_element) {
        log_error("DomText::create_symbol: name and parent_element required");
        return nullptr;
    }

    if (!parent_element->doc) {
        log_error("DomText::create_symbol: parent_element has no document");
        return nullptr;
    }

    // Arena zeroing supplies every null/zero default omitted below.
    DomText* text_node = create_in(parent_element->doc->node_arena);
    if (!text_node) {
        log_error("DomText::create_symbol: arena_calloc failed");
        return nullptr;
    }

    text_node->id = dom_document_alloc_node_id(parent_element->doc);
    text_node->parent = parent_element;
    text_node->text = name;
    text_node->length = len;
    text_node->set_symbol(true);

    if (!dom_node_registry_register(parent_element->doc, text_node,
                                    sizeof(DomText), true)) {
        return nullptr;
    }

    log_debug("DomText::create_symbol: created symbol node, name='%.*s'", (int)len, name);
    return text_node;
}

DomText* DomText::create_in(Arena* arena, size_t inline_string_length) {
    if (!arena) return nullptr;
    size_t total = sizeof(DomText) + sizeof(String) + inline_string_length + 1;
    // The inline Lambda String shares the same zeroed arena allocation as its node.
    DomText* text_node = (DomText*)arena_calloc(arena, total);
    if (!text_node) return nullptr;
    text_node->node_type = DOM_NODE_TEXT;
    String* string = dom_text_to_string(text_node);
    string->len = (uint32_t)inline_string_length;
    text_node->native_string = string;
    text_node->text = string->chars;
    text_node->length = inline_string_length;
    return text_node;
}

DomText* dom_text_create(String* native_string, DomElement* parent_element) {
    return DomText::create(native_string, parent_element);
}

DomText* dom_text_create_detached(String* native_string, DomDocument* doc) {
    return DomText::create_detached(native_string, doc);
}

void dom_text_destroy(DomText* text_node) {
    if (!text_node) {
        return;
    }
    // Note: Memory is pool-allocated, so it will be freed when pool is destroyed
}

const char* dom_text_get_content(DomText* text_node) {
    return text_node ? text_node->text : NULL;
}

bool dom_text_set_content(DomText* text_node, const char* new_content) {
    if (!text_node || !new_content) {
        log_error("dom_text_set_content: invalid parameters");
        return false;
    }

    if (!text_node->native_string || !text_node->parent) {
        log_error("dom_text_set_content: text node not backed by Lambda");
        return false;
    }

    DomElement* parent = (DomElement*)text_node->parent;
    if (!parent->doc) {
        log_error("dom_text_set_content: parent element has no document");
        return false;
    }

    // Get current child index
    int64_t child_idx = dom_text_get_child_index(text_node);
    if (child_idx < 0) {
        log_error("dom_text_set_content: failed to get child index");
        return false;
    }
    DomNode* saved_prev = text_node->prev_sibling;
    DomNode* saved_next = text_node->next_sibling;

    // Create new String via MarkBuilder
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    String* new_s = dom_create_mutation_string(
        editor.builder(), new_content, parent->doc->input->ui_mode);
    if (!new_s) {
        log_error("dom_text_set_content: failed to create string");
        return false;
    }
    Item new_string_item = (Item){.item = s2it(new_s)};

    // Replace child in parent Element's items array
    Item result = editor.elmt_replace_child(
        {.element = dom_element_to_element(parent)},
        child_idx,
        new_string_item
    );

    if (!result.element) {
        log_error("dom_text_set_content: failed to replace child");
        return false;
    }

    if (parent->doc->input->ui_mode) {
        // Copy DOM properties from old text_node to new embedded DomText
        String* new_string = new_string_item.get_string();
        DomText* new_dt = dom_text_from_fat_string(parent->doc, new_string);
        if (new_dt) {
            new_dt->set_symbol(text_node->is_symbol());
            new_dt->rect = text_node->rect;
            new_dt->font = text_node->font;
            new_dt->view_type = text_node->view_type;
            // Replacing the fat String transfers the retained layout handles to
            // its embedded DomText; leaving them on the retired node double-recycled
            // the TextRect chain at the next asynchronous layout checkpoint.
            text_node->rect = nullptr;
            text_node->font = nullptr;
            text_node->view_type = RDT_VIEW_NONE;
            new_dt->parent = parent;
            new_dt->prev_sibling = saved_prev;
            new_dt->next_sibling = saved_next;
            if (new_dt->prev_sibling) {
                new_dt->prev_sibling->next_sibling = new_dt;
            } else {
                parent->first_child = new_dt;
            }
            if (new_dt->next_sibling) {
                new_dt->next_sibling->prev_sibling = new_dt;
            } else {
                parent->last_child = new_dt;
            }
        }
    }

    // Update text_node fields to point to new String (backward compat for callers)
    text_node->native_string = new_string_item.get_string();
    if (!text_node->native_string) {
        log_error("dom_text_set_content: replacement string disappeared");
        return false;
    }
    text_node->text = text_node->native_string->chars;
    text_node->length = text_node->native_string->len;

    if (result.element != dom_element_to_element(parent)) {
        log_error("dom_text_set_content: inline editor changed backing identity");
        return false;
    }
    log_debug("dom_text_set_content: updated text at index %lld to '%s'", child_idx, new_content);
    return true;
}

bool dom_text_is_backed(DomText* text_node) {
    return text_node && text_node->native_string && text_node->parent;
}

int64_t dom_text_get_child_index(DomText* text_node) {
    if (!text_node || !text_node->parent || !text_node->native_string) {
        log_error("dom_text_get_child_index: text node not backed");
        return -1;
    }

    Element* parent_elem = dom_element_backing((DomElement*)text_node->parent);
    if (!parent_elem) {
        log_error("dom_text_get_child_index: parent has no native_element");
        return -1;
    }

    // Scan parent's children to find matching native_string
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING && item.get_string() == text_node->native_string) {
            log_debug("dom_text_get_child_index: found at index %lld", i);
            return i;
        }
    }

    log_error("dom_text_get_child_index: native_string not found in parent (may have been removed)");
    return -1;
}

bool dom_text_remove(DomText* text_node) {
    if (!text_node) {
        log_error("dom_text_remove: null text node");
        return false;
    }

    if (!dom_text_is_backed(text_node)) {
        log_error("dom_text_remove: text node not backed");
        return false;
    }

    DomElement* parent = (DomElement*)text_node->parent;
    if (!parent->doc) {
        log_error("dom_text_remove: parent element has no document");
        return false;
    }

    // Get current child index
    int64_t child_idx = dom_text_get_child_index(text_node);
    if (child_idx < 0) {
        log_error("dom_text_remove: failed to get child index");
        return false;
    }

    // Remove from Lambda parent Element's children array
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = dom_element_to_element(parent)},
        child_idx
    );

    if (!result.element) {
        log_error("dom_text_remove: failed to delete child");
        return false;
    }

    if (result.element != dom_element_to_element(parent)) {
        log_error("dom_text_remove: inline editor changed backing identity");
        return false;
    }

    // Remove from DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already rebuilt)
    if (!parent->doc->input->ui_mode) {
        if (text_node->prev_sibling) {
            text_node->prev_sibling->next_sibling = text_node->next_sibling;
        } else if (text_node->parent && text_node->parent->is_element()) {
            DomElement* parent_elem = static_cast<DomElement*>(text_node->parent);
            parent_elem->first_child = text_node->next_sibling;
        }

        if (text_node->next_sibling) {
            text_node->next_sibling->prev_sibling = text_node->prev_sibling;
        } else if (text_node->parent && text_node->parent->is_element()) {
            // Text node was last child
            DomElement* parent_elem = static_cast<DomElement*>(text_node->parent);
            parent_elem->last_child = text_node->prev_sibling;
        }
    }

    // Clear references
    text_node->parent = nullptr;
    text_node->prev_sibling = nullptr;
    text_node->next_sibling = nullptr;
    text_node->native_string = nullptr;
    log_debug("dom_text_remove: removed text node at index %lld", child_idx);
    return true;
}

DomText* DomElement::append_text(const char* text_content) {
    DomElement* parent = this;
    if (!text_content) {
        log_error("dom_element_append_text: invalid parameters");
        return nullptr;
    }

    if (parent->is_synthetic() || !parent->doc) {
        log_error("dom_element_append_text: parent element must be backed");
        return nullptr;
    }

    // Create String item via MarkBuilder
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    String* s = dom_create_mutation_string(
        editor.builder(), text_content, parent->doc->input->ui_mode);
    if (!s) {
        log_error("dom_element_append_text: failed to create string");
        return nullptr;
    }
    Item string_item = (Item){.item = s2it(s)};

    // Append to parent Element's children via MarkEditor
    Item result = editor.elmt_append_child(
        {.element = dom_element_to_element(parent)},
        string_item
    );

    if (!result.element) {
        log_error("dom_element_append_text: failed to append child");
        return nullptr;
    }

    DomText* text_node;
    String* string_value = string_item.get_string();
    if (!string_value) {
        log_error("dom_element_append_text: failed to read string item");
        return nullptr;
    }
    if (parent->doc->input->ui_mode) {
        // Check if DomText is embedded before the String (arena-allocated)
        DomText* candidate = dom_text_from_fat_string(parent->doc, string_value);
        if (candidate) {
            text_node = candidate;
        } else {
            text_node = nullptr;
            DomNode* relinked = parent->last_child;
            if (relinked && relinked->is_text()) {
                DomText* relinked_text = relinked->as_text();
                if (relinked_text && relinked_text->native_string == string_value) {
                    text_node = relinked_text;
                }
            }
            if (!text_node) {
                text_node = DomText::create(string_value, parent);
                if (text_node) dom_append_to_sibling_chain(parent, text_node);
            }
        }
    } else {
        // Create separate DomText wrapper with Lambda backing
        text_node = DomText::create(string_value, parent);
        if (!text_node) {
            log_error("dom_element_append_text: failed to create DomText");
            return nullptr;
        }
        dom_append_to_sibling_chain(parent, text_node);
    }

    if (result.element != dom_element_to_element(parent)) {
        log_error("dom_element_append_text: inline editor changed backing identity");
        return nullptr;
    }

    log_debug("dom_element_append_text: appended text '%s'", text_content);

    return text_node;
}

// ============================================================================
// DOM Comment/DOCTYPE Node Implementation
// ============================================================================

DomComment* DomComment::create(Element* native_element, DomElement* parent_element) {
    if (!native_element || !parent_element) {
        log_error("DomComment::create: native_element and parent_element required");
        return nullptr;
    }

    if (!parent_element->doc) {
        log_error("DomComment::create: parent_element has no document");
        return nullptr;
    }

    DomComment* comment_node = create_detached(native_element, parent_element->doc);
    if (!comment_node) return nullptr;
    comment_node->parent = parent_element;
    log_debug("DomComment::create: attached comment (tag=%s, content='%s')",
              comment_node->tag_name, comment_node->content);
    return comment_node;
}

DomComment* DomComment::create_detached(Element* native_element, DomDocument* doc) {
    if (!native_element) {
        log_error("DomComment::create_detached: native_element required");
        return nullptr;
    }
    if (!doc || !doc->node_arena) {
        log_error("DomComment::create_detached: doc with arena required");
        return nullptr;
    }

    TypeElmt* type = (TypeElmt*)native_element->type;
    const char* tag_name = type ? type->name.str : nullptr;
    if (!tag_name) {
        log_error("DomComment::create_detached: no tag name");
        return nullptr;
    }

    DomNodeType node_type;
    if (str_ieq_const(tag_name, strlen(tag_name), "!DOCTYPE")) {
        node_type = DOM_NODE_DOCTYPE;
    } else if (strcmp(tag_name, "!--") == 0 || strcmp(tag_name, "#comment") == 0) {
        node_type = DOM_NODE_COMMENT;
    } else {
        log_error("DomComment::create_detached: not a comment or DOCTYPE: %s", tag_name);
        return nullptr;
    }

    // Arena zeroing supplies every null/zero default omitted below.
    DomComment* comment_node = (DomComment*)arena_calloc(doc->node_arena, sizeof(DomComment));
    if (!comment_node) {
        log_error("DomComment::create_detached: arena_calloc failed");
        return nullptr;
    }

    comment_node->id = dom_document_alloc_node_id(doc);
    comment_node->node_type = node_type;
    comment_node->native_element = native_element;
    comment_node->tag_name = tag_name;  // RETAINED_FIELD_OK: interned reference type name, no allocation retained

    if (native_element->length > 0) {
        Item first_item = native_element->items[0];
        if (get_type_id(first_item) == LMD_TYPE_STRING) {
            String* content_str = first_item.get_string();
            if (content_str) {
                comment_node->content = content_str->chars;
                comment_node->length = content_str->len;
            }
        }
    }
    if (!comment_node->content) {
        ElementReader reader(native_element);
        const char* data_attr = reader.get_attr_string("data");
        if (!data_attr) {
            ConstItem attr_value = native_element->get_attr("data");
            String* data_string = attr_value.string();
            if (data_string) data_attr = data_string->chars;
        }
        if (data_attr) {
            comment_node->content = data_attr;
            comment_node->length = strlen(data_attr);
        }
    }

    if (!comment_node->content) {
        comment_node->content = "";
    }

    if (!dom_node_registry_register(doc, comment_node, sizeof(DomComment), true)) {
        return nullptr;
    }

    return comment_node;
}

DomComment* dom_comment_create_detached(Element* native_element, DomDocument* doc) {
    return DomComment::create_detached(native_element, doc);
}

void dom_comment_destroy(DomComment* comment_node) {
    if (!comment_node) {
        return;
    }
    // Note: Memory is pool-allocated, so it will be freed when pool is destroyed
}

// ============================================================================
// Backed DomComment Operations (Lambda Integration)
// ============================================================================

bool dom_comment_is_backed(DomComment* comment_node) {
    return comment_node && comment_node->native_element && comment_node->parent;
}

int64_t dom_comment_get_child_index(DomComment* comment_node) {
    if (!comment_node || !comment_node->parent || !comment_node->native_element) {
        log_error("dom_comment_get_child_index: comment node not backed");
        return -1;
    }

    Element* parent_elem = dom_element_backing((DomElement*)comment_node->parent);
    if (!parent_elem) {
        log_error("dom_comment_get_child_index: parent has no native_element");
        return -1;
    }

    // Scan parent's children to find matching native_element
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_ELEMENT && item.element == comment_node->native_element) {
            log_debug("dom_comment_get_child_index: found at index %lld", i);
            return i;
        }
    }

    log_error("dom_comment_get_child_index: native_element not found in parent (may have been removed)");
    return -1;
}

bool dom_comment_set_content(DomComment* comment_node, const char* new_content) {
    if (!comment_node || !new_content) {
        log_error("dom_comment_set_content: invalid arguments");
        return false;
    }

    if (!comment_node->native_element || !comment_node->parent) {
        log_error("dom_comment_set_content: comment not backed by Lambda");
        return false;
    }

    DomElement* parent = (DomElement*)comment_node->parent;
    if (!parent->doc) {
        log_error("dom_comment_set_content: parent element has no document");
        return false;
    }

    // Create new String via MarkBuilder
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    String* new_s = dom_create_mutation_string(
        editor.builder(), new_content, parent->doc->input->ui_mode);
    if (!new_s) {
        log_error("dom_comment_set_content: failed to create string");
        return false;
    }
    Item new_string_item = (Item){.item = s2it(new_s)};

    // Replace or append String child in comment Element
    Item result;
    if (comment_node->native_element->length > 0) {
        // Replace existing content (child at index 0)
        result = editor.elmt_replace_child(
            {.element = comment_node->native_element},
            0,  // Content is always first child
            new_string_item
        );
    } else {
        // Append content (comment was empty)
        result = editor.elmt_append_child(
            {.element = comment_node->native_element},
            new_string_item
        );
    }

    if (!result.element) {
        log_error("dom_comment_set_content: failed to update content");
        return false;
    }

    // Update DomComment to point to new String
    comment_node->native_element = result.element;
    String* new_string = new_string_item.get_string();
    if (!new_string) {
        log_error("dom_comment_set_content: replacement string disappeared");
        return false;
    }
    comment_node->content = new_string->chars;
    comment_node->length = new_string->len;
    log_debug("dom_comment_set_content: updated content to '%s'", new_content);
    return true;
}

DomComment* DomElement::append_comment(const char* comment_content) {
    DomElement* parent = this;
    if (!comment_content) {
        log_error("dom_element_append_comment: invalid arguments");
        return nullptr;
    }

    if (parent->is_synthetic() || !parent->doc) {
        log_error("dom_element_append_comment: parent not backed");
        return nullptr;
    }

    // Create Lambda comment Element with tag "!--"
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    ElementBuilder comment_elem = editor.builder()->element("!--");

    // Add content as String child
    if (strlen(comment_content) > 0) {
        Item content_item = editor.builder()->createStringItem(comment_content);
        comment_elem.child(content_item);
    }

    Item comment_item = comment_elem.final();
    if (!comment_item.element) {
        log_error("dom_element_append_comment: failed to create comment element");
        return nullptr;
    }

    // Append to parent Element's children
    Item result = editor.elmt_append_child(
        {.element = dom_element_to_element(parent)},
        comment_item
    );

    if (!result.element) {
        log_error("dom_element_append_comment: failed to append");
        return nullptr;
    }

    if (result.element != dom_element_to_element(parent)) {
        log_error("dom_element_append_comment: inline editor changed backing identity");
        return nullptr;
    }

    // Create DomComment wrapper
    DomComment* comment_node = DomComment::create(
        comment_item.element,
        parent
    );

    if (!comment_node) {
        log_error("dom_element_append_comment: failed to create DomComment");
        return nullptr;
    }

    // Add to DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already linked)
    if (!parent->doc->input->ui_mode) {
        dom_append_to_sibling_chain(parent, comment_node);
    }

    log_debug("dom_element_append_comment: appended comment '%s'", comment_content);

    return comment_node;
}

bool dom_comment_remove(DomComment* comment_node) {
    if (!comment_node) {
        log_error("dom_comment_remove: null comment");
        return false;
    }

    DomElement* parent = (DomElement*)comment_node->parent;
    if (!comment_node->native_element || !parent || !parent->doc) {
        log_error("dom_comment_remove: comment not backed");
        return false;
    }

    // Get current child index
    int64_t child_idx = dom_comment_get_child_index(comment_node);
    if (child_idx < 0) {
        log_error("dom_comment_remove: failed to get child index");
        return false;
    }

    // Remove from Lambda parent Element's children array
    MarkEditor editor(parent->doc->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = dom_element_to_element(parent)},
        child_idx
    );

    if (!result.element) {
        log_error("dom_comment_remove: failed to delete child");
        return false;
    }

    if (result.element != dom_element_to_element(parent)) {
        log_error("dom_comment_remove: inline editor changed backing identity");
        return false;
    }

    // Remove from DOM sibling chain (skip in ui_mode: MarkEditor's dom_relink_children already rebuilt)
    if (!parent->doc->input->ui_mode) {
        if (comment_node->prev_sibling) {
            comment_node->prev_sibling->next_sibling = comment_node->next_sibling;
        } else if (comment_node->parent) {
            DomElement* elem_parent = static_cast<DomElement*>(comment_node->parent);
            elem_parent->first_child = comment_node->next_sibling;
        }

        if (comment_node->next_sibling) {
            comment_node->next_sibling->prev_sibling = comment_node->prev_sibling;
        } else if (comment_node->parent) {
            // Comment node was last child
            DomElement* elem_parent = static_cast<DomElement*>(comment_node->parent);
            elem_parent->last_child = comment_node->prev_sibling;
        }
    }

    // Clear references
    comment_node->parent = nullptr;
    comment_node->prev_sibling = nullptr;
    comment_node->next_sibling = nullptr;
    comment_node->native_element = nullptr;
    log_debug("dom_comment_remove: removed comment at index %lld", child_idx);
    return true;
}

const char* dom_comment_get_content(DomComment* comment_node) {
    return comment_node ? comment_node->content : NULL;
}

// ============================================================================
// Helper Functions for DOM Tree Building
// ============================================================================

/**
 * Extract string attribute from Lambda Element
 * Returns attribute value or nullptr if not found
 */
const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena) {
    (void)arena;
    if (!elem || !attr_name) return nullptr;
    ConstItem attr_value = elem->get_attr(attr_name);
    String* string_value = attr_value.string();
    return string_value ? string_value->chars : nullptr;
}

// ============================================================================
// Element-to-DOM map: Lambda Element* → DomElement*
// Used for incremental DOM rebuild (Phase 12)
// ============================================================================

typedef struct ElementDomMapEntry {
    Element* element;       // key: Lambda Element pointer
    DomElement* dom_elem;   // value: corresponding DomElement
} ElementDomMapEntry;
HASHMAP_DEFINE_PTRKEY(element_dom_map, ElementDomMapEntry, element)

HashMap* element_dom_map_create(void) {
    return element_dom_map_new(64);
}

void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem) {
    if (!map || !elem || !dom_elem) return;
    ElementDomMapEntry entry;
    entry.element = elem;
    entry.dom_elem = dom_elem;
    hashmap_set(map, &entry);
}

DomElement* element_dom_map_lookup(HashMap* map, Element* elem) {
    if (!map || !elem) return nullptr;
    ElementDomMapEntry key;
    key.element = elem;
    key.dom_elem = nullptr;
    const ElementDomMapEntry* found = (const ElementDomMapEntry*)hashmap_get(map, &key);
    return found ? found->dom_elem : nullptr;
}

void element_dom_map_remove(HashMap* map, Element* elem) {
    if (!map || !elem) return;
    ElementDomMapEntry key = {.element = elem, .dom_elem = nullptr};
    hashmap_delete(map, &key);
}

static const int MAX_DOM_BUILD_DEPTH = 512;
static thread_local int g_dom_build_depth = 0;

struct DomBuildDepthGuard {
    DomBuildDepthGuard() { g_dom_build_depth++; }
    ~DomBuildDepthGuard() { g_dom_build_depth--; }
};

DomElement* build_dom_tree_from_element(Element* elem, DomDocument* doc, DomElement* parent) {
    if (!elem || !doc) {
        log_debug("build_dom_tree_from_element: Invalid arguments\n");
        return nullptr;
    }
    if (g_dom_build_depth > MAX_DOM_BUILD_DEPTH) {
        return nullptr;
    }
    DomBuildDepthGuard depth_guard;

    // Get element type and tag name
    TypeElmt* type = (TypeElmt*)elem->type;
    if (!type) return nullptr;

    const char* tag_name = type->name.str;
    log_debug("build element: <%s> (parent: %s), elem->length=%lld", tag_name,
              parent ? parent->tag_name : "none", (long long)elem->length);

    // Skip comments and DOCTYPE - they will be created as DomComment nodes below
    // HTML5 parser uses "#comment", CSS/older parsers use "!--"
    if (strcmp(tag_name, "!--") == 0 || strcmp(tag_name, "#comment") == 0 || str_ieq_const(tag_name, strlen(tag_name), "!DOCTYPE")) {
        return nullptr;  // Not a layout element, processed as child below
    }

    // Skip XML declarations
    if (strncmp(tag_name, "?", 1) == 0) {
        return nullptr;  // Skip XML declarations
    }

    // Skip <script> elements during DOM tree building. Per HTML spec the UA
    // stylesheet sets `script { display: none }`, so they don't participate
    // in layout. Keeping them out of the DOM also prevents script_runner from
    // re-executing inline scripts during pure layout passes.
    //
    // Exception: when the author explicitly overrides display via inline
    // style="display: ..." (block, inline, etc.), the script's text content
    // becomes part of the rendered/selectable text per WPT
    // selection/script-and-style-elements.html. In that case we keep the
    // element so DomRange.toString() can include its text.
    if (str_ieq_const(tag_name, strlen(tag_name), "script")) {
        const char* style_attr = extract_element_attribute(elem, "style", nullptr);
        bool has_display_override = false;
        if (style_attr) {
            // Crude check: the inline style attribute mentions "display:" with
            // a non-`none` value. css_parse will refine this further when the
            // element is later checked for visibility.
            const char* d = strstr(style_attr, "display");
            if (d) {
                const char* colon = strchr(d, ':');
                if (colon) {
                    const char* v = colon + 1;
                    while (*v == ' ' || *v == '\t') v++;
                    if (strncmp(v, "none", 4) != 0) has_display_override = true;
                }
            }
        }
        if (!has_display_override) {
            return nullptr;  // Skip script elements during DOM tree building
        }
    }

    // UI-mode Lambda elements already occupy zeroed DomElement storage.
    bool ui_mode = doc->input && doc->input->ui_mode;
    DomElement* dom_elem = ui_mode
        ? DomElement::create_in(element_to_dom_element(elem), doc, tag_name, elem)
        : DomElement::create(doc, tag_name, elem);
    if (!dom_elem) return nullptr;

    // populate element-to-DOM map if available (for incremental rebuild)
    if (doc->element_dom_map) {
        element_dom_map_insert(doc->element_dom_map, elem, dom_elem);
    }

    // Extract source line number if tracked during HTML5 parsing
    ConstItem sl_attr = elem->get_attr("__source_line");
    if (((Item*)&sl_attr)->_type_id == LMD_TYPE_INT) {
        dom_elem->source_line = (int)((Item*)&sl_attr)->int_val;
    }

    // DomElement::create_in snapshots id/class exactly once. Repeating that
    // work here used to overwrite the first id allocation and duplicate class
    // payloads for every parsed element.

    // Parse and apply inline style attribute
    const char* style_value = extract_element_attribute(elem, "style", nullptr);
    if (style_value) {
        dom_element_apply_inline_style(dom_elem, style_value);
    }

    // extract rowspan and colspan attributes for table cells (td, th)
    if (str_ieq_const(tag_name, strlen(tag_name), "td") || str_ieq_const(tag_name, strlen(tag_name), "th")) {
        const char* rowspan_value = extract_element_attribute(elem, "rowspan", nullptr);
        if (rowspan_value) {
            dom_elem->set_attribute("rowspan", rowspan_value);
        }

        const char* colspan_value = extract_element_attribute(elem, "colspan", nullptr);
        if (colspan_value) {
            dom_elem->set_attribute("colspan", colspan_value);
        }
    }

    // Store href for anchor and area elements; selector matching derives :link
    // from attributes when no StateStore resolver is installed.
    if (str_ieq_const(tag_name, strlen(tag_name), "a") || str_ieq_const(tag_name, strlen(tag_name), "area")) {
        const char* href_value = extract_element_attribute(elem, "href", nullptr);
        if (href_value && strlen(href_value) > 0) {
            dom_elem->set_attribute("href", href_value);
        }
    }

    // Store form attributes; selector matching derives static pseudo-class
    // defaults from attributes before StateStore-backed view state exists.
    if (str_ieq_const(tag_name, strlen(tag_name), "input")) {
        const char* type_value = extract_element_attribute(elem, "type", nullptr);
        const char* name_value = extract_element_attribute(elem, "name", nullptr);
        const char* ph_value = extract_element_attribute(elem, "placeholder", nullptr);
        const char* val_attr = extract_element_attribute(elem, "value", nullptr);
        // Store the type attribute for later use
        if (type_value) {
            dom_elem->set_attribute("type", type_value);
        }
        // Store the name attribute for radio button grouping
        if (name_value) {
            dom_elem->set_attribute("name", name_value);
        }
        if (elem->has_attr("checked")) {
            dom_elem->set_attribute("checked", "checked");
        }
        if (elem->has_attr("disabled")) {
            dom_elem->set_attribute("disabled", "disabled");
        }
        if (elem->has_attr("required")) {
            dom_elem->set_attribute("required", "required");
        }
        if (elem->has_attr("readonly")) {
            dom_elem->set_attribute("readonly", "readonly");
        }
        if (ph_value) {
            dom_elem->set_attribute("placeholder", ph_value);
        }
        if (val_attr) {
            dom_elem->set_attribute("value", val_attr);
        }
    }
    // :disabled also applies to <select>, <textarea>, <button>, <optgroup>, <option>,
    // <fieldset> per HTML spec: https://html.spec.whatwg.org/#selector-disabled
    else if (str_ieq_const(tag_name, strlen(tag_name), "select") ||
             str_ieq_const(tag_name, strlen(tag_name), "textarea") ||
             str_ieq_const(tag_name, strlen(tag_name), "button") ||
             str_ieq_const(tag_name, strlen(tag_name), "optgroup") ||
             str_ieq_const(tag_name, strlen(tag_name), "option") ||
             str_ieq_const(tag_name, strlen(tag_name), "fieldset")) {
        const char* ph_value = extract_element_attribute(elem, "placeholder", nullptr);
        const char* val_attr = extract_element_attribute(elem, "value", nullptr);
        if (elem->has_attr("disabled")) {
            dom_elem->set_attribute("disabled", "disabled");
        }
        if (elem->has_attr("selected")) {
            dom_elem->set_attribute("selected", "selected");
        }
        if (elem->has_attr("required")) {
            dom_elem->set_attribute("required", "required");
        }
        if (elem->has_attr("readonly")) {
            dom_elem->set_attribute("readonly", "readonly");
        }
        if (ph_value) {
            dom_elem->set_attribute("placeholder", ph_value);
        }
        if (val_attr) {
            dom_elem->set_attribute("value", val_attr);
        }
    }

    // set parent relationship if provided
    // Use link_child since the Lambda tree already contains this element
    // (we're building DOM wrappers from existing Lambda structure)
    if (parent) {
        parent->link_child(dom_elem);
    }

    // Process all children - including text nodes, comments, and elements
    // Elements are Lists, so iterate through items

    if (elem->length > 0 && !elem->items) {
        log_error("build_dom_tree: <%s> has length=%lld but items=NULL", tag_name, (long long)elem->length);
        return dom_elem;
    }
    // Sanity check: reject absurdly large length values
    if (elem->length > 100000) {
        log_error("build_dom_tree: <%s> has suspicious length=%lld, skipping", tag_name, (long long)elem->length);
        return dom_elem;
    }
    for (int64_t i = 0; i < elem->length; i++) {
        Item child_item = elem->items[i];
        TypeId child_type = get_type_id(child_item);
        // Guard: skip items with invalid type IDs (corrupted memory)
        if (child_type == 0 || child_type > LMD_TYPE_OBJECT) {
            log_error("build_dom_tree: <%s> child %lld has invalid type=%d (raw=0x%llx), skipping",
                      tag_name, (long long)i, child_type, (unsigned long long)child_item.item);
            continue;
        }
        if (child_type == LMD_TYPE_ELEMENT) {
            // element node - recursively build
            Element* child_elem = child_item.element;
            if (!child_elem || (uintptr_t)child_elem < 0x1000) {
                log_error("build_dom_tree: <%s> child %lld has invalid element pointer %p", tag_name, (long long)i, (void*)child_elem);
                continue;
            }
            TypeElmt* child_elem_type = (TypeElmt*)child_elem->type;
            const char* child_tag_name = child_elem_type ? child_elem_type->name.str : "unknown";

            // Check if this is a comment or DOCTYPE
            // HTML5 parser uses "#comment", CSS/older parsers use "!--"
            if (strcmp(child_tag_name, "!--") == 0 || strcmp(child_tag_name, "#comment") == 0 || str_ieq_const(child_tag_name, strlen(child_tag_name), "!DOCTYPE")) {
                // Create DomComment node backed by Lambda Element
                DomComment* comment_node = DomComment::create(child_elem, dom_elem);
                if (comment_node) {
                    // Add to DOM sibling chain
                    dom_append_to_sibling_chain(dom_elem, comment_node);

                    log_debug("  Created comment node at index %lld: '%s'",
                              i, comment_node->content);
                }
                continue;  // Don't try to build as DomElement
            }

            log_debug("  Building child element: <%s> for parent <%s> (parent_dom=%p)", child_tag_name, tag_name, (void*)dom_elem);
            DomElement* child_dom = build_dom_tree_from_element(child_elem, doc, dom_elem);

            // skip if nullptr (e.g., script, XML declarations)
            if (!child_dom) {
                log_debug("  Skipped child element: <%s>", child_tag_name);
                continue;
            }

            log_debug("  Successfully built child <%s> with parent <%s>. child_dom=%p, child_dom->parent=%p",
                     child_tag_name, tag_name, (void*)child_dom, (void*)child_dom->parent);

            // append_child() already linked the node during the recursive call,
            // so the parent-child and sibling relationships are already established correctly.
            // No manual linking needed!

        } else if (child_type == LMD_TYPE_STRING) {
            // Text node - create DomText that references Lambda String
            String* text_str = child_item.get_string();
            if (text_str && text_str->len > 0) {
                DomText* text_node;
                if (ui_mode) {
                    // UI mode: check if String was allocated with DomText prefix on arena
                    // (done by ui_copy_string_to_arena / MarkBuilder). Non-arena strings
                    // (GC heap, const pool) do NOT have this prefix — using string_to_dom_text
                    // on them would produce a bogus pointer that corrupts adjacent memory.
                    DomText* candidate = dom_text_from_fat_string(doc, text_str);
                    if (candidate) {
                        text_node = candidate;
                        text_node->parent = dom_elem;
                        if (!text_node->id) {
                            text_node->id = dom_document_alloc_node_id(doc);
                        }
                        size_t primary_size = sizeof(DomText) + sizeof(String) +
                                              text_node->length + 1;
                        if (!dom_node_registry_register(doc, text_node,
                                                       primary_size, true)) {
                            text_node = nullptr;
                        }
                    } else {
                        text_node = DomText::create(text_str, dom_elem);
                    }
                } else {
                    // Create text node (preserves Lambda String reference)
                    text_node = DomText::create(text_str, dom_elem);
                }
                if (text_node) {
                    // Add text node to DOM sibling chain
                    dom_append_to_sibling_chain(dom_elem, text_node);

                    log_debug("  Created text node at index %lld: '%s' (len=%zu)",
                              i, text_str->chars, text_str->len);
                }
            }
        } else if (child_type == LMD_TYPE_SYMBOL) {
            // Symbol node (HTML entity or emoji) - create DomText with symbol type
            Symbol* sym = child_item.get_symbol();
            if (sym && sym->len > 0) {
                // Create symbol text node (will be resolved at render time)
                DomText* text_node = DomText::create_symbol(sym->chars, sym->len, dom_elem);
                if (text_node) {
                    // Add symbol node to DOM sibling chain
                    dom_append_to_sibling_chain(dom_elem, text_node);

                    log_debug("  Created symbol node at index %lld: '%.*s' (len=%u)",
                              i, (int)sym->len, sym->chars, sym->len);
                }
            }
        } else if (child_type == LMD_TYPE_ARRAY) {
            // Array child - flatten into parent (Lambda scripts may produce arrays of elements)
            Array* arr = child_item.array;
            if (arr) {
                log_debug("  Flattening array child at index %lld with %lld items", i, (long long)arr->length);
                for (int64_t j = 0; j < arr->length; j++) {
                    Item arr_item = arr->items[j];
                    TypeId arr_item_type = get_type_id(arr_item);
                    if (arr_item_type == LMD_TYPE_ELEMENT) {
                        Element* child_elem = arr_item.element;
                        build_dom_tree_from_element(child_elem, doc, dom_elem);
                    } else if (arr_item_type == LMD_TYPE_STRING) {
                        String* text_str = arr_item.get_string();
                        if (text_str && text_str->len > 0) {
                            DomText* text_node;
                            if (ui_mode) {
                                DomText* candidate = dom_text_from_fat_string(doc, text_str);
                                if (candidate) {
                                    text_node = candidate;
                                    text_node->parent = dom_elem;
                                } else {
                                    text_node = DomText::create(text_str, dom_elem);
                                }
                            } else {
                                text_node = DomText::create(text_str, dom_elem);
                            }
                            if (text_node) {
                                dom_append_to_sibling_chain(dom_elem, text_node);
                            }
                        }
                    } else if (arr_item_type == LMD_TYPE_SYMBOL) {
                        Symbol* sym = arr_item.get_symbol();
                        if (sym && sym->len > 0) {
                            DomText* text_node = DomText::create_symbol(sym->chars, sym->len, dom_elem);
                            if (text_node) {
                                dom_append_to_sibling_chain(dom_elem, text_node);
                            }
                        }
                    } else if (arr_item_type == LMD_TYPE_ARRAY) {
                        // nested array - flatten recursively by wrapping in a temporary element iteration
                        Array* nested = arr_item.array;
                        if (nested) {
                            for (int64_t k = 0; k < nested->length; k++) {
                                Item nested_item = nested->items[k];
                                TypeId nested_type = get_type_id(nested_item);
                                if (nested_type == LMD_TYPE_ELEMENT) {
                                    build_dom_tree_from_element(nested_item.element, doc, dom_elem);
                                } else if (nested_type == LMD_TYPE_STRING) {
                                    String* s = nested_item.get_string();
                                    if (s && s->len > 0) {
                                        DomText* tn;
                                        if (ui_mode) {
                                            DomText* candidate = dom_text_from_fat_string(doc, s);
                                            if (candidate) {
                                                tn = candidate;
                                                tn->parent = dom_elem;
                                            } else {
                                                tn = DomText::create(s, dom_elem);
                                            }
                                        } else {
                                            tn = DomText::create(s, dom_elem);
                                        }
                                        if (tn) dom_append_to_sibling_chain(dom_elem, tn);
                                    }
                                } else if (nested_type == LMD_TYPE_SYMBOL) {
                                    Symbol* sym = nested_item.get_symbol();
                                    if (sym && sym->len > 0) {
                                        DomText* tn = DomText::create_symbol(sym->chars, sym->len, dom_elem);
                                        if (tn) dom_append_to_sibling_chain(dom_elem, tn);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return dom_elem;
}
