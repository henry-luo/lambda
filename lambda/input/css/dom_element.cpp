// #define _POSIX_C_SOURCE 200809L
#include "dom_element.hpp"

#include "dom_lifecycle.hpp"
#include "style_epoch.hpp"
#include "css_formatter.hpp"
#include "css_style_node.hpp"
#include "css_parser.hpp"
#include "selector_matcher.hpp"
#include "css_counter_hook.h"
#include "../../../lib/hashmap.h"
#include "../../../lib/mem_factory.h"
#include "../../../lib/hashmap_typed.hpp"
#include "../../../lib/strbuf.h"
#include "../../../lib/stringbuf.h"
#include "../../../lib/string.h"
#include "../../../lib/log.h"
#include "../../../lib/strview.h"
#include "../../../lib/str.h"
#include "../../../lib/utf.h"
#include "../../../lib/arena.h"
#include "../../../lib/memtrack.h"
#include "../../../lib/mem_grow.hpp"
#include "../../lambda-data.hpp"  // For get_type_id, and proper type definitions
#include "../../core/well_known_markup_names.h"
#include "../../core/mark_reader.hpp"  // For ElementReader
#include "../../io/mark_editor.hpp"  // For MarkEditor
#include "../../io/mark_builder.hpp" // For MarkBuilder

DomNode* dom_source_parent(DomNode* node) {
    DomNode* parent = node ? node->parent : nullptr;
    // generated table boxes participate in layout, never CSS or DOM ancestry.
    while (parent && parent->is_element() && parent->as_element()->is_table_fixup()) {
        parent = parent->parent;
    }
    return parent;
}

DomElement* dom_parent_element(DomElement* element) {
    DomNode* parent_node = dom_source_parent(element);
    DomElement* parent = parent_node ? parent_node->as_element() : nullptr;
    if (parent && parent->tag_name &&
        strcmp(parent->tag_name, "#document-fragment") == 0 &&
        parent->shadow_host_element()) {
        // CSS Shadow DOM: a shadow fragment inherits the host's computed style;
        // projected light-DOM nodes must not fall back to UA defaults.
        return parent->shadow_host_element();
    }
    return parent;
}

static const char* dom_element_stored_attribute(DomElement* element, const char* key);

static const char* dom_element_namespace_binding(DomElement* element, const char* prefix, size_t length) {
    if (!element) return nullptr;
    if (length == 3 && !memcmp(prefix, "xml", 3)) return "http://www.w3.org/XML/1998/namespace";
    if (length == 5 && !memcmp(prefix, "xmlns", 5)) return "http://www.w3.org/2000/xmlns/";
    char declaration[128] = "xmlns";
    StrBuf* extended = nullptr;
    const char* key = declaration;
    if (length) {
        if (length <= sizeof(declaration) - 7) {
            declaration[5] = ':'; memcpy(declaration + 6, prefix, length); declaration[6 + length] = '\0';
        } else {
            extended = strbuf_new();
            if (!extended) return nullptr;
            strbuf_append_str(extended, "xmlns:"); strbuf_append_str_n(extended, prefix, length); key = extended->str;
        }
    }
    // preserve an explicit default-namespace reset separately from an absent declaration.
    const char* result = nullptr;
    for (DomNode* node = element; node; node = node->parent) {
        if (!node->is_element()) continue;
        result = dom_element_stored_attribute(node->as_element(), key);
        if (result) break;
    }
    if (extended) strbuf_free(extended);
    return result;
}

const char* dom_element_lookup_namespace_uri(DomElement* element, const char* prefix) {
    const char* uri = dom_element_namespace_binding(element, prefix, prefix ? strlen(prefix) : 0);
    return uri && *uri ? uri : nullptr;
}

const char* dom_element_namespace_uri(DomElement* element) {
    if (!element || !element->tag_name) return "";
    const char* uri = dom_element_stored_attribute(element, "__lambda_ns_uri");
    if (uri) return uri;
    const char* colon = strchr(element->tag_name, ':');
    bool parsed_html = element->doc && !element->doc->xml_document &&
        element->doc->page_kind == DOM_PAGE_KIND_HTML;
    if (!parsed_html) {
        if (colon && colon == element->tag_name.get()) return "";
        uri = dom_element_namespace_binding(element, element->tag_name, colon ? (size_t)(colon - element->tag_name) : 0);
        if (uri) return uri;
        if (colon || (element->doc && element->doc->xml_document)) return "";
    }
    for (DomNode* node = element; node; node = node->parent) {
        if (!node->is_element()) continue;
        DomElement* ancestor = node->as_element();
        if (!ancestor->tag_name) continue;
        // SVG HTML integration points stop inherited SVG namespace membership.
        if (node != element &&
            (str_icmp_cstr(ancestor->tag_name, "foreignObject") == 0 ||
             str_icmp_cstr(ancestor->tag_name, "desc") == 0 ||
             str_icmp_cstr(ancestor->tag_name, "title") == 0)) break;
        if (str_icmp_cstr(ancestor->tag_name, "svg") == 0)
            return "http://www.w3.org/2000/svg";
        if (str_icmp_cstr(ancestor->tag_name, "math") == 0)
            return "http://www.w3.org/1998/Math/MathML";
    }
    return "http://www.w3.org/1999/xhtml";
}

NameId dom_element_html_tag(DomElement* element) {
    if (!element || strcmp(dom_element_namespace_uri(element), "http://www.w3.org/1999/xhtml")) return 0;
    // HTML element types use the case-sensitive local name, independent of an XML prefix.
    const char* local = element->local_name();
    return well_known_name_id({local, strlen(local)});
}

DomNamespacedAttribute* dom_element_namespaced_attributes(DomElement* element) {
    return element && element->ext ? element->ext->namespaced_attributes : nullptr;
}

bool dom_element_record_namespaced_attribute(DomElement* element,
    const char* namespace_uri, const char* qualified_name, const char* value) {
    if (!element || !element->doc || !namespace_uri ||
        !qualified_name || !*qualified_name) return false;
    const char* colon = strrchr(qualified_name, ':');
    const char* local = *namespace_uri && colon
        ? colon + 1 : qualified_name;
    DomElementExt* ext = element->ensure_ext();
    if (!ext) return false;
    Pool* pool = element->storage_pool();
    for (DomNamespacedAttribute* attr = ext->namespaced_attributes;
         attr; attr = attr->next) {
        if (attr->active && strcmp(attr->namespace_uri, namespace_uri) == 0 &&
            strcmp(attr->local_name, local) == 0) {
            if (strcmp(attr->qualified_name, qualified_name) == 0 &&
                ((!attr->value && !value) || (attr->value && value && strcmp(attr->value, value) == 0))) return true;
            char* name_copy = pool_strdup(pool, qualified_name);
            char* value_copy = value ? pool_strdup(pool, value) : nullptr;
            if (!name_copy || (value && !value_copy)) return false;
            attr->qualified_name = lam::own(name_copy);
            attr->value = lam::own(value_copy);
            attr->active = true;
            return attr->active;
        }
    }
    DomNamespacedAttribute* attr = (DomNamespacedAttribute*)pool_calloc(
        pool, sizeof(DomNamespacedAttribute));
    if (!attr) return false;
    attr->namespace_uri = lam::own(pool_strdup(pool, namespace_uri));
    attr->local_name = lam::own(pool_strdup(pool, local));
    attr->qualified_name = lam::own(pool_strdup(pool, qualified_name));
    attr->value = lam::own(value ? pool_strdup(pool, value) : nullptr);
    if (!attr->namespace_uri || !attr->local_name ||
        !attr->qualified_name || (value && !attr->value)) return false;
    attr->active = true;
    lam::Own<DomNamespacedAttribute>* tail = &ext->namespaced_attributes;
    while (*tail) tail = &(*tail)->next;
    *tail = lam::own(attr);
    return true;
}

void dom_element_remove_namespaced_attribute(DomElement* element,
    const char* namespace_uri, const char* local_name) {
    if (!namespace_uri || !local_name) return;
    for (DomNamespacedAttribute* attr = dom_element_namespaced_attributes(element);
         attr; attr = attr->next) {
        if (attr->active && strcmp(attr->namespace_uri, namespace_uri) == 0 &&
            strcmp(attr->local_name, local_name) == 0) {
            attr->active = false;
            return;
        }
    }
}

const char* dom_element_get_namespaced_attribute(DomElement* element,
    const char* namespace_uri, const char* local_name) {
    if (!namespace_uri || !local_name) return nullptr;
    for (DomNamespacedAttribute* attr = dom_element_namespaced_attributes(element);
         attr; attr = attr->next) {
        if (attr->active && strcmp(attr->namespace_uri, namespace_uri) == 0 &&
            strcmp(attr->local_name, local_name) == 0) return attr->value;
    }
    return nullptr;
}

const char* dom_element_attribute_namespace_uri(DomElement* element,
    const char* qualified_name, const char** local_name) {
    if (!element || !qualified_name || !local_name) return nullptr;
    for (DomNamespacedAttribute* attr = dom_element_namespaced_attributes(element);
         attr; attr = attr->next) {
        if (attr->active && strcmp(attr->qualified_name, qualified_name) == 0) {
            *local_name = attr->local_name;
            return attr->namespace_uri;
        }
    }
    const char* colon = strchr(qualified_name, ':');
    *local_name = qualified_name;
    if (!element->doc || element->doc->xml_document || element->doc->page_kind != DOM_PAGE_KIND_HTML) {
        if (!colon) return strcmp(qualified_name, "xmlns") == 0
            ? "http://www.w3.org/2000/xmlns/" : "";
        *local_name = colon + 1;
        return dom_element_namespace_binding(element, qualified_name, (size_t)(colon - qualified_name));
    }
    // HTML parsing assigns namespaces only to the foreign-attribute adjustment table.
    const char* element_uri = dom_element_namespace_uri(element);
    if (strcmp(element_uri, "http://www.w3.org/2000/svg") == 0 ||
        strcmp(element_uri, "http://www.w3.org/1998/Math/MathML") == 0) {
        const char* uri = nullptr;
        if (strcmp(qualified_name, "xml:lang") == 0 || strcmp(qualified_name, "xml:base") == 0 ||
            strcmp(qualified_name, "xml:space") == 0) uri = "http://www.w3.org/XML/1998/namespace";
        else if (strcmp(qualified_name, "xlink:actuate") == 0 || strcmp(qualified_name, "xlink:arcrole") == 0 ||
            strcmp(qualified_name, "xlink:href") == 0 || strcmp(qualified_name, "xlink:role") == 0 ||
            strcmp(qualified_name, "xlink:show") == 0 || strcmp(qualified_name, "xlink:title") == 0 ||
            strcmp(qualified_name, "xlink:type") == 0) uri = "http://www.w3.org/1999/xlink";
        else if (strcmp(qualified_name, "xmlns") == 0 || strcmp(qualified_name, "xmlns:xlink") == 0)
            uri = "http://www.w3.org/2000/xmlns/";
        if (uri) { if (colon) *local_name = colon + 1; return uri; }
    }
    return "";
}

// DOM bridge diagnostics are emitted per node during document construction.
#define log_debug(...) log_trace(__VA_ARGS__)

void element_dom_map_remove(HashMap* map, Element* elem);
static void dom_element_clear_synthetic_attributes(DomElement* element);

bool dom_document_owns_node_storage(DomDocument* doc, const void* storage) {
    if (!doc || !storage) return false;
    return (doc->input && doc->input->arena && arena_owns(doc->input->arena, storage)) ||
        (doc->node_arena && arena_owns(doc->node_arena, storage));
}

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

DomElement* dom_shadow_root(DomNode* node) {
    if (!node) return nullptr;
    while (node->parent) node = node->parent;
    DomElement* root = node->as_element();
    return root && root->shadow_host_element() ? root : nullptr;
}

static bool dom_is_html_slot(DomElement* element) {
    return dom_element_html_tag(element) == MARKUP_NAME_SLOT;
}

bool dom_slot_assignment_matches(DomElement* slot, DomNode* child) {
    if (!dom_is_html_slot(slot) || !child) return false;
    const char* name = dom_element_attribute_value_ns(slot, "", "name");
    if (!name) name = "";
    if (child->is_text()) return !*name;
    if (!child->is_element() || child->as_element()->is_synthetic()) return false;
    const char* assigned = dom_element_attribute_value_ns(child->as_element(), "", "slot");
    return strcmp(name, assigned ? assigned : "") == 0;
}

DomElement* dom_shadow_first_matching_slot(DomNode* root, const char* name) {
    // named assignment uses source tree order, excluding inert template contents and generated boxes.
    for (DomNode* node = root; node;) {
        DomElement* element = node->as_element();
        if (dom_is_html_slot(element)) {
            const char* candidate = dom_element_attribute_value_ns(element, "", "name");
            if (strcmp(candidate ? candidate : "", name ? name : "") == 0) return element;
        }
        bool inert = element && (element->is_synthetic() ||
            dom_element_html_tag(element) == MARKUP_NAME_TEMPLATE);
        node = element && !inert && element->first_child ? element->first_child :
            dom_next_after_subtree(root, node);
    }
    return nullptr;
}

DomElement* dom_slot_assignment_host(DomElement* slot) {
    if (!dom_is_html_slot(slot)) return nullptr;
    DomElement* root = dom_shadow_root(slot);
    return root && dom_shadow_first_matching_slot(root, dom_element_attribute_value_ns(slot, "", "name")) == slot
        ? root->shadow_host_element() : nullptr;
}

DomElement* dom_flat_tree_parent(DomElement* element) {
    DomNode* source_parent = dom_source_parent(element);
    if (source_parent && source_parent->is_element() && source_parent->as_element()->shadow_host_element())
        return source_parent->as_element()->shadow_host_element();
    DomElement* parent = dom_parent_element(element);
    if (!parent) return nullptr;
    DomElement* shadow = parent->shadow_root_element();
    if (shadow) {
        const char* name = dom_element_attribute_value_ns(element, "", "slot");
        return dom_shadow_first_matching_slot(shadow, name);
    }
    if (dom_is_html_slot(parent)) {
        DomElement* host = dom_slot_assignment_host(parent);
        for (DomNode* child = host ? host->first_child.get() : nullptr; child; child = child->next_sibling)
            if (dom_slot_assignment_matches(parent, child)) return nullptr;
    }
    return parent;
}

static CssEnum dom_html_direction_state(DomElement* element) {
    if (!element || strcmp(dom_element_namespace_uri(element), "http://www.w3.org/1999/xhtml"))
        return CSS_VALUE__UNDEF;
    const char* value = dom_element_attribute_value_ns(element, "", "dir");
    if (!value) return CSS_VALUE__UNDEF;
    if (str_ieq_cstr(value, "ltr")) return CSS_VALUE_LTR;
    if (str_ieq_cstr(value, "rtl")) return CSS_VALUE_RTL;
    return str_ieq_cstr(value, "auto") ? CSS_VALUE_AUTO : CSS_VALUE__UNDEF;
}

bool dom_element_has_directionality_hint(DomElement* element) {
    if (!element || strcmp(dom_element_namespace_uri(element), "http://www.w3.org/1999/xhtml")) return false;
    return dom_html_direction_state(element) != CSS_VALUE__UNDEF ||
        dom_element_html_tag(element) == MARKUP_NAME_BDI ||
        (dom_element_html_tag(element) == MARKUP_NAME_INPUT &&
         form_input_kind(dom_element_attribute_value_ns(element, "", "type")) == FORM_INPUT_KIND_TEL);
}

int dom_find_strong_direction(DomNode* node, bool skip_explicit_dir,
        bool first, bool raw_content, DomDirectionValueResolver resolve_value, void* context) {
    int last_strong = 0;
    // walk source descendants without one native stack frame per DOM level.
    for (DomNode* current = node; current;) {
        DomElement* element = current->as_element();
        bool excluded = false;
        int strong = 0;
        if (current->is_text()) {
            DomText* text = current->as_text();
            if (text->text && text->length)
                strong = utf8_bidi_strong_direction(text->text, text->length, first);
        } else if (element) {
            NameId html_tag = dom_element_html_tag(element);
            excluded = ((raw_content || skip_explicit_dir) && element->is_synthetic()) ||
                (!raw_content && ((html_tag == MARKUP_NAME_SCRIPT || html_tag == MARKUP_NAME_STYLE ||
                    html_tag == MARKUP_NAME_TEXTAREA) ||
                    (element->tag_name && strcmp(element->tag_name, "::marker") == 0) ||
                    (skip_explicit_dir && (dom_html_direction_state(element) != CSS_VALUE__UNDEF ||
                        html_tag == MARKUP_NAME_BDI))));
            if (!excluded && !raw_content && skip_explicit_dir && dom_is_html_slot(element)) {
                DomElement* root = dom_shadow_root(element);
                // HTML contained-text auto direction treats a shadow slot as its host's direction.
                if (root) {
                    strong = dom_element_directionality(root->shadow_host_element(), resolve_value, context);
                    excluded = true;
                }
            }
        }
        if (strong) {
            if (first) return strong;
            last_strong = strong;
        }
        current = element && !excluded && element->first_child ? element->first_child :
            dom_next_after_subtree(node, current);
    }
    return last_strong;
}

int dom_element_directionality(DomElement* element,
        DomDirectionValueResolver resolve_value, void* context) {
    for (DomElement* current = element; current; current = dom_parent_element(current)) {
        CssEnum state = dom_html_direction_state(current);
        if (state == CSS_VALUE_LTR) return -1;
        if (state == CSS_VALUE_RTL) return 1;
        NameId html_tag = dom_element_html_tag(current);
        bool input = html_tag == MARKUP_NAME_INPUT;
        bool textarea = html_tag == MARKUP_NAME_TEXTAREA;
        bool bdi = html_tag == MARKUP_NAME_BDI;
        if (state == CSS_VALUE_AUTO || bdi) {
            bool control = textarea || (input && html_input_has_auto_direction_value(dom_element_attribute_value_ns(current, "", "type")));
            const char* value = control && resolve_value ? resolve_value(current, context) : nullptr;
            if (!value && control && input) {
                value = dom_element_attribute_value_ns(current, "", "value");
                if (!value) value = ""; // void input descendants never contribute to its value.
            }
            if (value) return utf8_bidi_strong_direction(value, strlen(value), true) > 0 ? 1 : -1;
            DomElement* assignment_host = dom_slot_assignment_host(current);
            bool assigned = false;
            for (DomNode* child = assignment_host ? assignment_host->first_child.get() : nullptr;
                 child; child = child->next_sibling) {
                if (!dom_slot_assignment_matches(current, child)) continue;
                assigned = true;
                int strong = dom_find_strong_direction(child, true, true, false, resolve_value, context);
                if (strong) return strong;
            }
            // a nonempty neutral assignment suppresses fallback content as well.
            if (assigned) return -1;
            // raw textarea value includes every text descendant; isolated subtrees are excluded elsewhere.
            for (DomNode* child = current->first_child; child; child = child->next_sibling) {
                int strong = dom_find_strong_direction(child, !control, true, control, resolve_value, context);
                if (strong) return strong;
            }
            return -1;
        }
        // telephone controls default to LTR even when their parent is RTL.
        if (input && form_input_kind(dom_element_attribute_value_ns(current, "", "type")) == FORM_INPUT_KIND_TEL) return -1;
    }
    return -1;
}

static void dom_option_collect_normalized_text(DomNode* node, StrBuf* out,
                                                bool* previous_whitespace) {
    if (!node || !out || !previous_whitespace) return;
    if (node->is_text()) {
        DomText* text = node->as_text();
        if (!text || !text->text) return;
        *previous_whitespace = strbuf_append_collapsed_ascii_whitespace(
            out, text->text, text->length, true, *previous_whitespace);
        return;
    }
    if (!node->is_element()) return;
    DomElement* element = node->as_element();
    for (DomNode* child = element->first_child; child; child = child->next_sibling) {
        dom_option_collect_normalized_text(child, out, previous_whitespace);
    }
}

void dom_option_text_normalized(DomElement* option, StrBuf* out) {
    if (!option || option->tag() != MARKUP_NAME_OPTION || !out) return;
    bool previous_whitespace = true;
    dom_option_collect_normalized_text(option->first_child, out,
                                       &previous_whitespace);
    while (out->length > 0 && out->str[out->length - 1] == ' ') {
        out->str[--out->length] = '\0';
    }
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
#ifndef LAMBDA_NO_CONSOLE_DUMP
    log_info("[TIMING] cascade detail: decl_count: %lld", g_apply_decl_count);
#endif
}

// Forward declaration
DomElement* build_dom_tree_from_element(Element* elem, DomDocument* document, DomElement* parent);
DomElement* build_dom_tree_from_element_with_input(Element* elem, DomDocument* document, DomElement* parent);
const char* extract_element_attribute(Element* elem, const char* attr_name, Arena* arena);
static bool dom_element_add_cached_class(DomElement* element, const char* class_name);

static DomText* dom_text_from_fat_string(DomDocument* doc, String* string_value) {
    if (!doc || !string_value) return nullptr;
    if (!dom_document_owns_node_storage(doc, string_value)) return nullptr;

    DomText* candidate = string_to_dom_text(string_value);
    if (!dom_document_owns_node_storage(doc, candidate)) return nullptr;
    if (candidate->node_type != DOM_NODE_TEXT) return nullptr;
    if (candidate->native_string != string_value) return nullptr;
    // Every adopted fat string needs a lifecycle record, including strings
    // flattened from arrays and text introduced by MarkEditor mutations.
    if (!candidate->id) candidate->id = dom_document_alloc_node_id(doc);
    size_t primary_size = sizeof(DomText) + sizeof(String) + candidate->length + 1;
    if (!dom_node_registry_register(doc, candidate, primary_size, true)) return nullptr;
    return candidate;
}

DomText* dom_find_text_child(DomElement* parent, String* string_value) {
    if (!parent || !string_value) return nullptr;
    for (DomNode* child = parent->first_child; child; child = child->next_sibling) {
        if (!child->is_text()) continue;
        DomText* text = child->as_text();
        if (text && text->native_string == string_value) return text;
    }
    return nullptr;
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
    return string_from_strview_arena(strview_init("", 0), builder->arena());
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
    document->owns_input_resources = true;

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

    // Keep selectively released document records separate from the node region.
    document_pool = lam::own(mem_pool_create(dctx, MEM_ROLE_NODE, "dom.document.pool"));
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
    node_arena = lam::own(mem_arena_create(dctx, MEM_ROLE_NODE, "dom.node.arena"));
    if (!node_arena) {
        log_error("dom_document_create: failed to create arena");
        // Factory-created DOM roots must unregister their memory-context nodes on teardown.
        destroy();
        return false;
    }

    input = source_input;
    root = nullptr;
    // Factory allocation uses mem_calloc, so DomJsRuntime's constructor is bypassed.
    js.implicit_doctype = true;
    js.mutation_records = js.inline_mutation_records;
    js.mutation_record_capacity = DOM_JS_MUTATION_RECORD_CAP;
    return true;
}

void dom_document_destroy(DomDocument* document) {
    if (!document) {
        return;
    }
    document->destroy();
    mem_free(document);
}

Input* dom_document_take_owned_input_resources(DomDocument* document) {
    if (!document || !document->owns_input_resources) return nullptr;
    document->owns_input_resources = false;
    return document->input;
}

void dom_document_borrow_input_resources(DomDocument* document) {
    if (document) document->owns_input_resources = false;
}

bool dom_document_finalize_loader_pool(DomDocument* document, Pool* pool) {
    if (!document || !pool) return false;
    if (!document->owned_loader_pool) {
        document->owned_loader_pool = lam::own(pool);
        return true;
    }
    if (document->owned_loader_pool != pool) {
        // A loader such as a Lambda document can replace itself with a fresh
        // HTML document. Its original pool then has no surviving consumers.
        mem_pool_destroy(pool);
    }
    return true;
}

bool dom_document_replace_url(DomDocument* document, Url* replacement) {
    if (!document || !replacement) return false;
    if (!document->url) {
        document->url = lam::own(replacement);
        return true;
    }
    // Input and the loader can retain document->url across navigation, so
    // exchange owned fields instead of invalidating their carrier pointer.
    Url previous = *document->url;
    *document->url = *replacement;
    *replacement = previous;
    url_destroy(replacement);
    return true;
}

extern "C" __attribute__((weak)) void dom_css_document_language_changed(void* document) {
    DomDocument* doc = (DomDocument*)document;
    if (doc) { doc->mutation_epoch++; doc->style_content_epoch++; }
}

static bool dom_document_replace_language(DomDocument* document, lam::Own<char>* slot, StrView value) {
    if (!document || !slot) return false;
    if ((!*slot && !value.length) || (*slot && str_eq(*slot, strlen(*slot), value.str, value.length)))
        return true;
    char* replacement = value.length ? mem_dup_n(value.str, value.length, MEM_CAT_DOM) : nullptr;
    if (value.length && !replacement) return false;
    lam::free_owned(*slot);
    *slot = lam::own(replacement);
    dom_css_document_language_changed(document);
    return true;
}

const char* dom_document_default_language(const DomDocument* document) {
    if (!document) return nullptr;
    return document->services.pragma_language ? document->services.pragma_language :
        document->services.protocol_language;
}

bool dom_document_set_content_language(DomDocument* document, const char* header) {
    if (!document) return false;
    StrView value = {nullptr, 0};
    // HTTP lists describe an audience; only one language is a node-language fallback.
    if (header && !strchr(header, ',')) {
        const char* end = header + strlen(header);
        while (header < end && str_char_is_ascii_space(*header)) header++;
        while (end > header && str_char_is_ascii_space(end[-1])) end--;
        value = {header, (size_t)(end - header)};
    }
    return dom_document_replace_language(document, &document->services.protocol_language, value);
}

bool dom_document_process_metadata_insertion(DomDocument* document, DomNode* subtree) {
    if (!document || document->xml_document || !subtree || !subtree->is_element() ||
        subtree->as_element()->doc != document ||
        !dom_element_is_connected(subtree->as_element())) return false;
    StrView candidate = {nullptr, 0};
    for (DomNode* node = subtree; node;) {
        DomElement* element = node->as_element();
        if (dom_element_html_tag(element) == MARKUP_NAME_META) {
            const char* equiv = dom_element_attribute_value_ns(element, "", "http-equiv");
            const char* content = dom_element_attribute_value_ns(element, "", "content");
            if (equiv && str_ieq_cstr(equiv, "content-language") && content && !strchr(content, ',')) {
                while (*content && str_char_is_ascii_space(*content)) content++;
                const char* end = content;
                while (*end && !str_char_is_ascii_space(*end)) end++;
                if (end != content) candidate = {content, (size_t)(end - content)};
            }
        }
        // parser-backed template contents are inert until their detached fragment is materialized.
        bool inert_template = dom_element_html_tag(element) == MARKUP_NAME_TEMPLATE;
        node = element && !inert_template && element->first_child ? element->first_child :
            dom_next_after_subtree(subtree, node);
    }
    // an unsuccessful pragma and later removal never erase a successfully processed default.
    return candidate.length && dom_document_replace_language(document,
        &document->services.pragma_language, candidate);
}

const char* dom_element_language(DomElement* element) {
    if (!element) return nullptr;
    for (DomElement* current = element; current;) {
        const char* language = dom_element_attribute_value_ns(current,
            "http://www.w3.org/XML/1998/namespace", "lang");
        const char* namespace_uri = dom_element_namespace_uri(current);
        bool html = strcmp(namespace_uri, "http://www.w3.org/1999/xhtml") == 0;
        if (!language && (html || strcmp(namespace_uri, "http://www.w3.org/2000/svg") == 0))
            language = dom_element_attribute_value_ns(current, "", "lang");
        if (language) return language;
        current = dom_parent_element(current);
    }
    return dom_document_default_language(element->doc);
}

void DomDocument::destroy() {
    dom_retire_begin_destroy(this);
    mem_free(services.preferred_languages); services.preferred_languages = nullptr;
    lam::free_owned(services.pragma_language);
    lam::free_owned(services.protocol_language);
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

    dom_document_clear_embedding(this);

    if (pending_navigation_url) {
        mem_free(pending_navigation_url);
        pending_navigation_url = nullptr;
    }
    if (behavior_init_controls) {
        arraylist_free(behavior_init_controls);
        behavior_init_controls = nullptr;
    }

    // adopted-document resources can destroy the physical backing heap.
    dom_lifecycle_release_backing_roots(this);
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

    dom_js_mutation_records_destroy(this);

    // Canonical styles use independent pools and must disappear while the
    // document pool that owns their manager/list is still valid.
    style_epoch_manager_destroy(this);

    // The lifecycle registry owns detached-candidate metadata backed by the
    // document pool, so tear it down before its registered node arena.
    dom_lifecycle_destroy(this);

    // Incremental Lambda reconciliation maps backing Elements to DOM wrappers
    // with heap storage outside the document pool.
    if (element_dom_map) {
        hashmap_free(element_dom_map);
        element_dom_map = nullptr;
    }

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

bool dom_document_add_resource(DomDocument* document, DomDocumentResourceData* data,
                               DomDocumentResourceDestroyFn destroy) {
    if (!document || !data || !destroy) return false;
    DomDocumentResource* resource = (DomDocumentResource*)mem_calloc(
        1, sizeof(DomDocumentResource), MEM_CAT_LAYOUT);
    if (!resource) return false;
    resource->data = lam::own(data);
    resource->destroy = destroy;
    resource->next = document->resources;
    document->resources = lam::own(resource);
    return true;
}

bool dom_document_release_resource(DomDocument* document, DomDocumentResourceData* data) {
    if (!document || !data) return false;
    lam::Own<DomDocumentResource>* link = &document->resources;
    while (*link) {
        DomDocumentResource* resource = *link;
        if (resource->data == data) {
            *link = resource->next;
            if (resource->destroy) resource->destroy(resource->data);
            mem_free(resource);
            return true;
        }
        link = &resource->next;
    }
    return false;
}

bool dom_document_set_embedding(DomDocument* embedded, DomDocument* parent,
                                DomElement* iframe) {
    if (!embedded || !parent || !iframe || iframe->doc != parent) return false;
    DomNodeRef ref = dom_node_ref((DomNode*)iframe);
    if (!dom_node_ref_validate(parent, ref) ||
        !dom_node_pin(parent, ref, DOM_NODE_PIN_EXTERNAL)) {
        return false;
    }
    dom_document_clear_embedding(embedded);
    embedded->embedding_document = lam::up(parent);
    embedded->embedding_element_ref = ref;
    parent->embedded_evaluator_pending = true;
    return true;
}

void dom_document_clear_embedding(DomDocument* embedded) {
    if (!embedded) return;
    if (embedded->embedding_document &&
        embedded->embedding_element_ref.address) {
        dom_node_unpin(embedded->embedding_document,
                       embedded->embedding_element_ref,
                       DOM_NODE_PIN_EXTERNAL);
    }
    embedded->embedding_document = nullptr;
    embedded->embedding_element_ref = {nullptr, 0};
}

DomElement* dom_document_embedding_element(DomDocument* embedded) {
    if (!embedded || !embedded->embedding_document) return nullptr;
    DomNode* node = dom_node_ref_validate(embedded->embedding_document,
                                          embedded->embedding_element_ref);
    return node && node->is_element() ? node->as_element() : nullptr;
}

// ============================================================================
// DOM Element Creation and Destruction
// ============================================================================

uint32_t dom_document_alloc_node_id(DomDocument* doc) {
    if (!doc) return 0;
    uint32_t id = doc->next_node_id++;
    if (id == 0) id = doc->next_node_id++;
    return id;
}

static void dom_element_release_cached_id(DomElement* element) {
    if (!element || !element->id) return;
    if (element->doc && element->storage_pool()) {
        pool_free(element->storage_pool(), (void*)element->id);
    }
    dom_element_clear_id(element);
}

static void dom_element_release_cached_classes(DomElement* element) {
    if (!element || !element->class_names) return;
    if (element->doc && element->storage_pool()) {
        for (int i = 0; i < element->class_count; i++) {
            pool_free(element->storage_pool(), (void*)element->class_names[i]);
        }
        pool_free(element->storage_pool(), (void*)element->class_names);
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
    if (reinitializing && element->doc != doc && !element->preserve_storage_owner())
        return nullptr;
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
        dom_element_clear_synthetic_attributes(element);
        if (element->tag_name) pool_free(element->storage_pool(), (void*)element->tag_name);
        element->tag_name = nullptr;
        if (element->specified_style_shared()) {
            style_epoch_unbind_element(element);
        } else if (element->specified_style_borrowed()) {
            style_tree_release_borrow(element->specified_style);
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
    element->doc = lam::up(doc);

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
    lam::PoolPtr<char> tag_copy = lam::promote_to_pool(element->storage_pool(), tag_name);
    if (!tag_copy) {
        return nullptr;
    }
    dom_element_retain_tag_name(element, tag_copy);

    // Convert tag name to Lexbor tag ID for fast comparison
    element->tag_id = DomNode::tag_name_to_id(tag_name);

    // Create style trees (still use pool for AVL nodes)
    element->specified_style = lam::shared(style_tree_create(element->storage_pool()));
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
            dom_element_retain_id(element, lam::promote_to_pool(element->storage_pool(), id_attr));
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
                element->storage_pool(), count * sizeof(const char*));
            if (class_names) {
                dom_element_retain_class_names(element, lam::PoolPtr<const char*>(class_names));
                // Parse classes - make a copy for strtok
                char* class_copy = pool_strdup(element->storage_pool(), class_str);
                if (class_copy) {
                    int index = 0;
                    char* token = strtok(class_copy, " \t\n\r");
                    while (token && index < count) {
                        // Allocate permanent copy of each class from arena
                        size_t token_len = strlen(token);
                        char* class_perm = pool_dup_n(element->storage_pool(), token, token_len);
                        if (class_perm) {
                            class_names[index++] = class_perm;
                        }
                        token = strtok(NULL, " \t\n\r");
                    }
                    element->class_count = index;
                    pool_free(element->storage_pool(), class_copy);
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
        element->specified_style = lam::shared(style_tree_create(element->storage_pool()));
        element->mark_specified_style_owned();
    } else if (element->specified_style_borrowed()) {
        // A generated pseudo box may discard its view of the source tree, but
        // it must never clear declarations owned by the originating element.
        style_tree_release_borrow(element->specified_style);
        element->specified_style = lam::shared(style_tree_create(element->storage_pool()));
        element->mark_specified_style_owned();
    } else if (element->specified_style) {
        // Recascade runs inside the document pool's active lifetime; returning
        // its live style allocations here corrupts subsequent CSS allocations.
        style_tree_clear(element->specified_style);
    } else {
        element->specified_style = lam::shared(style_tree_create(element->storage_pool()));
        element->mark_specified_style_owned();
    }
    // Reset version tracking
    element->advance_style_version();
    element->set_needs_style_recompute(true);

    // Note: We don't free memory here since it's pool-allocated
    // The pool will handle cleanup
}

enum CssCustomSelection { CSS_CUSTOM_INLINE, CSS_CUSTOM_CASCADED,
    CSS_CUSTOM_PRESENTATION, CSS_CUSTOM_ANIMATION, CSS_CUSTOM_ALL };

static bool dom_element_custom_property_selected(const CssCustomProp* prop,
    CssCustomSelection selection, const char* name = nullptr, const void* effect = nullptr) {
    const CssDeclaration* declaration = prop->declaration;
    if (name && !css_custom_property_name_matches(prop->name, name)) return false;
    if (selection == CSS_CUSTOM_ALL) return true;
    if (selection == CSS_CUSTOM_ANIMATION) return prop->animation_owner == effect;
    if (prop->animation_owner) return false;
    bool presentation = declaration && declaration->presentation_value;
    if (selection == CSS_CUSTOM_PRESENTATION) return presentation;
    bool is_inline = declaration && declaration->specificity.inline_style != 0;
    return !presentation && is_inline == (selection == CSS_CUSTOM_INLINE);
}

static bool dom_element_clear_custom_properties(DomElement* element,
    CssCustomSelection selection, const char* name = nullptr,
    const CssDeclaration* keep = nullptr, const void* effect = nullptr) {
    if (!element || !element->doc) return false;
    bool removed = false;
    lam::Own<CssCustomProp>* slot = &element->css_variables;
    while (*slot) {
        CssCustomProp* prop = *slot;
        if (prop->declaration == keep || !dom_element_custom_property_selected(prop, selection, name, effect)) {
            slot = &prop->next;
            continue;
        }
        *slot = prop->next;
        if (prop->declaration && prop->declaration->tree_owned_record)
            css_declaration_destroy_owned(prop->declaration,
                                           element->storage_pool());
        pool_free(element->storage_pool(), prop);
        removed = true;
    }
    return removed;
}

void dom_element_clear_cascaded_styles(DomElement* element) {
    if (!element) return;
    if (!element->doc) {
        log_error("drawing recascade element missing document: element=%p tag=%s parent=%p",
                  (void*)element, element->tag_name ? element->tag_name : "?",
                  (void*)element->parent);
        return;
    }

    bool changed = dom_element_clear_custom_properties(element, CSS_CUSTOM_CASCADED);
    if (element->specified_style_shared()) {
        // Canonical trees never contain inline declarations, so detaching is
        // sufficient and avoids materializing declarations that are discarded.
        style_epoch_unbind_element(element);
        element->specified_style = lam::shared(style_tree_create(element->storage_pool()));
        element->mark_specified_style_owned();
        changed = true;
    } else if (element->specified_style_borrowed()) {
        // retained pseudo boxes must keep borrowing the source pseudo-style tree;
        // detaching here leaves reused boxes with an empty tree after recascade.
        return;
    } else if (element->specified_style) {
        if (style_tree_has_local_declarations(element->specified_style)) {
            // Inline declarations are live DOM state. Reparse-on-recascade both
            // lost CSSOM writes and grew the document pool on every hover event.
            changed = style_tree_remove_non_inline_declarations(
                element->specified_style);
        } else if (!style_tree_is_empty(element->specified_style)) {
            // The principal specified tree has no registered borrowers. Replace
            // it as one exclusive owner so a full recascade cannot retain the
            // discarded declaration graph until document teardown.
            StyleTree* replacement = style_tree_create(element->storage_pool());
            if (!replacement) return;
            style_tree_destroy_owned(element->specified_style);
            element->specified_style = lam::shared(replacement);
            element->mark_specified_style_owned();
            changed = true;
        }
    } else {
        element->specified_style = lam::shared(style_tree_create(element->storage_pool()));
        element->mark_specified_style_owned();
    }
    if (changed) {
        element->advance_style_version();
        element->set_needs_style_recompute(true);
    }
}

void dom_element_borrow_specified_style(DomElement* element, StyleTree* style) {
    if (!element) return;
    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
    } else if (element->specified_style_borrowed()) {
        if (element->specified_style == style) {
            element->set_styles_resolved(false);
            element->set_needs_style_recompute(true);
            return;
        }
        style_tree_release_borrow(element->specified_style);
        element->specified_style = nullptr;
        element->mark_specified_style_owned();
    } else if (!element->specified_style_borrowed() && element->specified_style &&
               element->specified_style != style) {
        style_tree_destroy_owned(element->specified_style);
    }
    // Generated pseudo elements are views over their source declarations; the
    // source element remains the sole owner across view retirement and rebuild.
    element->specified_style = lam::shared(style);
    if (style) {
        style_tree_acquire_borrow(style);
        element->mark_specified_style_borrowed();
    }
    else element->mark_specified_style_owned();
    // Borrowing changes the cascade input. A retained generated box may have
    // just inherited its host's font, so it must reapply its own declarations.
    element->set_styles_resolved(false);
    element->set_needs_style_recompute(true);
}

void dom_element_destroy(DomElement* element) {
    if (!element) {
        return;
    }

    if (element->specified_style_shared()) {
        style_epoch_unbind_element(element);
    } else if (element->specified_style_borrowed()) {
        style_tree_release_borrow(element->specified_style);
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

struct DomInlineDeclarations : DomDocumentResourceData {
    Pool* owner_pool;
    Pool* pool;
    CssRule rule;
    bool updating;
};

static void dom_inline_declarations_reset(DomInlineDeclarations* block) {
    if (block->pool) mem_pool_destroy(block->pool);
    block->pool = nullptr;
    block->rule = {};
}

static void dom_inline_declarations_destroy(DomDocumentResourceData* resource) {
    DomInlineDeclarations* block = (DomInlineDeclarations*)resource;
    dom_inline_declarations_reset(block);
    pool_free(block->owner_pool, block);
}

static void dom_element_clear_inline_declaration_block(DomElement* element) {
    if (element->ext && element->ext->inline_declarations)
        dom_inline_declarations_reset(element->ext->inline_declarations);
}

CssRule* dom_element_inline_declaration_block(DomElement* element) {
    DomInlineDeclarations* block = element && element->ext
        ? element->ext->inline_declarations : nullptr;
    return block && block->pool ? &block->rule : nullptr;
}

bool dom_element_commit_inline_declarations(DomElement* element, CssRule* rule, const char* text) {
#ifdef LAMBDA_NO_CSS_INPUT
    return false;
#else
    if (!element || !element->doc || !rule || !text) return false;
    DomElementExt* ext = element->ensure_ext();
    if (!ext) return false;
    DomInlineDeclarations* block = ext->inline_declarations;
    if (!block) {
        block = (DomInlineDeclarations*)pool_calloc(element->storage_pool(), sizeof(DomInlineDeclarations));
        if (!block) return false;
        block->owner_pool = element->storage_pool();
        if (!dom_document_add_resource(element->storage_owner(), block, dom_inline_declarations_destroy)) {
            pool_free(element->storage_pool(), block);
            return false;
        }
        ext->inline_declarations = lam::up(block);
    }
    Pool* pool = mem_pool_create((MemContext*)element->doc->services.mem_ctx, MEM_ROLE_CSS, "css.inline.authored");
    if (!pool) return false;
    CssRule next = {};
    next.pool = pool;
    next.type = CSS_RULE_STYLE;
    size_t count = rule->data.style_rule.declaration_count;
    next.data.style_rule.declarations = count ? (CssDeclaration**)pool_calloc(pool, count * sizeof(CssDeclaration*)) : nullptr;
    if (count && !next.data.style_rule.declarations) {mem_pool_destroy(pool); return false;}
    for (size_t i = 0; i < count; i++) {
        CssDeclaration* copy = css_declaration_snapshot(rule->data.style_rule.declarations[i], pool);
        if (!copy) {mem_pool_destroy(pool); return false;}
        next.data.style_rule.declarations[next.data.style_rule.declaration_count++] = copy;
    }
    Pool* previous_pool = block->pool;
    CssRule previous_rule = block->rule;
    block->pool = pool;
    block->rule = next;
    // CSSOM's updating flag keeps attribute serialization from reparsing raw DOMString names.
    block->updating = true;
    bool changed = element->set_attribute("style", text);
    block->updating = false;
    if (!changed) {
        block->pool = previous_pool;
        block->rule = previous_rule;
        mem_pool_destroy(pool);
        return false;
    }
    if (previous_pool) mem_pool_destroy(previous_pool);
    return true;
#endif
}

void dom_element_release_retired_storage(DomElement* element) {
    if (!element || !element->doc) return;
    // no live Attr wrapper can reach an owner selected for retirement; keep detached values intact.
    while (element->ext && element->ext->attribute_nodes)
        dom_attribute_node_detach(element->ext->attribute_nodes);
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
        style_tree_release_borrow(element->specified_style);
        element->specified_style = nullptr;
        element->mark_specified_style_owned();
    } else if (element->specified_style) {
        style_tree_destroy_owned(element->specified_style);
        element->specified_style = nullptr;
    }
    if (element->tag_name) {
        pool_free(element->storage_pool(), (void*)element->tag_name);
        element->tag_name = nullptr;
    }
    dom_element_release_cached_id(element);
    dom_element_release_cached_classes(element);
    dom_element_clear_synthetic_attributes(element);
    dom_element_clear_custom_properties(element, CSS_CUSTOM_ALL);
    style_epoch_selection_clear_element(element);
    // retirement releases both the authored pool and its document resource registration (D4.5.1v4).
    if (element->ext && element->ext->inline_declarations)
        dom_document_release_resource(element->storage_owner(), element->ext->inline_declarations);
    if (element->ext) {
        // the snapshot and its dynamic tracks survive relayout, then retire together.
        CssTransitionElemState* transition = element->transition_state_prop();
        if (transition) {
            pool_free(transition->pool, transition->tracks);
            pool_free(transition->pool, transition);
        }
        element->set_transition_state_prop(nullptr);
        for (int kind = 0; kind < PSEUDO_STYLE_COUNT; kind++) {
            if (element->ext->pseudo_styles[kind]) {
                style_tree_retire_borrow_source(element->ext->pseudo_styles[kind]);
                element->ext->pseudo_styles[kind] = nullptr;
            }
        }
        pool_free(element->storage_pool(),
                  (void*)element->ext->attribute_names_cache);
        pool_free(element->storage_pool(), element->ext);
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

static void dom_element_clear_synthetic_attributes(DomElement* element) {
    if (!element || !element->ext || !element->doc ||
        !element->storage_pool()) {
        return;
    }
    DomElementExt* data = element->ext;
    for (int i = 0; i < data->synthetic_attribute_count; i++) {
        pool_free(element->storage_pool(),
                  (void*)data->synthetic_attributes[i].name);
        pool_free(element->storage_pool(),
                  (void*)data->synthetic_attributes[i].value);
    }
    pool_free(element->storage_pool(), data->synthetic_attributes);
    data->synthetic_attributes = nullptr;
    data->synthetic_attribute_count = 0;
    data->synthetic_attribute_capacity = 0;
}

static int dom_element_find_synthetic_attribute(DomElement* element,
                                                const char* lower_name) {
    if (!element || !element->ext || !lower_name) return -1;
    DomElementExt* data = element->ext;
    for (int i = 0; i < data->synthetic_attribute_count; i++) {
        const char* candidate = data->synthetic_attributes[i].name;
        if (candidate && strcmp(candidate, lower_name) == 0) return i;
    }
    return -1;
}

static bool dom_element_set_synthetic_attribute(DomElement* element,
                                                const char* lower_name,
                                                const char* value) {
    if (!element || !element->doc || !element->storage_pool() ||
        !lower_name || !value) {
        return false;
    }
    DomElementExt* data = element->ensure_ext();
    if (!data) return false;
    int index = dom_element_find_synthetic_attribute(element, lower_name);
    if (index >= 0) {
        char* next_value = pool_strdup(element->storage_pool(), value);
        if (!next_value) return false;
        pool_free(element->storage_pool(),
                  (void*)data->synthetic_attributes[index].value);
        data->synthetic_attributes[index].value = lam::own((const char*)next_value);
        return true;
    }
    if (data->synthetic_attribute_count == data->synthetic_attribute_capacity) {
        if (!lam::pool_grow_array(element->storage_pool(),
                                  &data->synthetic_attributes,
                                  &data->synthetic_attribute_capacity,
                                  data->synthetic_attribute_count + 1, 4)) return false;
    }
    char* name_copy = pool_strdup(element->storage_pool(), lower_name);
    char* value_copy = pool_strdup(element->storage_pool(), value);
    if (!name_copy || !value_copy) {
        pool_free(element->storage_pool(), name_copy);
        pool_free(element->storage_pool(), value_copy);
        return false;
    }
    data->synthetic_attributes[data->synthetic_attribute_count++] = {
        lam::own((const char*)name_copy), lam::own((const char*)value_copy)
    };
    return true;
}

static bool dom_element_remove_synthetic_attribute(DomElement* element,
                                                   const char* lower_name) {
    int index = dom_element_find_synthetic_attribute(element, lower_name);
    if (index < 0 || !element || !element->ext || !element->doc ||
        !element->storage_pool()) {
        return false;
    }
    DomElementExt* data = element->ext;
    pool_free(element->storage_pool(),
              (void*)data->synthetic_attributes[index].name);
    pool_free(element->storage_pool(),
              (void*)data->synthetic_attributes[index].value);
    for (int i = index + 1; i < data->synthetic_attribute_count; i++) {
        data->synthetic_attributes[i - 1] = data->synthetic_attributes[i];
    }
    data->synthetic_attribute_count--;
    return true;
}

static bool dom_element_clear_inline_style_declarations(DomElement* element) {
    if (!element || !element->specified_style) {
        return false;
    }
    if (!style_epoch_ensure_owned(element)) return false;
    bool removed = style_tree_remove_inline_declarations(element->specified_style);
    return dom_element_clear_custom_properties(element, CSS_CUSTOM_INLINE) || removed;
}

static void dom_element_attribute_did_set(DomElement* element,
                                          const char* lower_name,
                                          const char* value, bool same_value) {
    if (!element || !element->doc || !lower_name || !value) return;
    if (strcmp(lower_name, "id") == 0) {
        dom_element_release_cached_id(element);
        const char* id_attr = dom_element_attribute_value_ns(element, "", "id");
        if (id_attr) {
            dom_element_retain_id(element, lam::promote_to_pool(
                element->storage_pool(), id_attr));
        }
    } else if (strcmp(lower_name, "class") == 0) {
        dom_element_release_cached_classes(element);
        if (value[0] != '\0') {
            char* class_copy = pool_strdup(element->storage_pool(), value);
            if (class_copy) {
                char* token = strtok(class_copy, " \t\n\r");
                while (token) {
                    if (token[0] != '\0') {
                        dom_element_add_cached_class(element, token);
                    }
                    token = strtok(nullptr, " \t\n\r");
                }
                pool_free(element->storage_pool(), class_copy);
            }
        }
    } else if (strcmp(lower_name, "style") == 0) {
        DomInlineDeclarations* block = element->ext ? element->ext->inline_declarations : nullptr;
        if (same_value && !(block && block->updating)) return;
        dom_element_clear_inline_style_declarations(element);
        if (!(block && block->updating)) dom_element_clear_inline_declaration_block(element);
        if (value[0] != '\0') dom_element_apply_inline_style(element, value);
    }
    element->advance_style_version();
    element->set_needs_style_recompute(true);
}

static void dom_element_attribute_did_remove(DomElement* element,
                                             const char* lower_name) {
    if (!element || !lower_name) return;
    if (strcmp(lower_name, "id") == 0) {
        dom_element_release_cached_id(element);
    } else if (strcmp(lower_name, "class") == 0) {
        dom_element_release_cached_classes(element);
    } else if (strcmp(lower_name, "style") == 0) {
        dom_element_clear_inline_style_declarations(element);
        dom_element_clear_inline_declaration_block(element);
    }
    element->advance_style_version();
    element->set_needs_style_recompute(true);
}

// The stored key an attribute query names, lowercasing into `lower`. HTML
// stores attribute names lowercased, so a query matches case-insensitively;
// SVG and other foreign content keep their case (`viewBox`, `gradientUnits`,
// as the parser and Lambda templates spell them), and DOM §4.9 lowercases a
// queried name only for HTML elements, so a name stored exactly as asked wins.
static const char* dom_element_stored_attribute(DomElement* element, const char* key) {
    if (!element->is_synthetic()) {
        ElementReader reader(dom_element_to_element(element));
        const char* value = reader.get_attr_string(key);
        if (value) return value;
    }
    int index = dom_element_find_synthetic_attribute(element, key);
    return index >= 0 ? element->ext->synthetic_attributes[index].value.get() : nullptr;
}

static const char* dom_element_attr_key(DomElement* element, const char* name,
                                        char* lower, size_t lower_size) {
    lowercase_attr_name(name, lower, lower_size);
    // an unchanged name needs no namespace walk; layout queries these keys
    // repeatedly on every descendant of large HTML and foreign-content trees.
    if (strcmp(lower, name) == 0) return name;
    if ((element->doc && element->doc->xml_document) ||
        strcmp(dom_element_namespace_uri(element), "http://www.w3.org/1999/xhtml") != 0) return name;
    bool stored_exact = element->is_synthetic()
        ? dom_element_find_synthetic_attribute(element, name) >= 0
        : ElementReader(dom_element_to_element(element)).has_attr(name);
    return stored_exact ? name : lower;
}

static DomNamespacedAttribute* dom_first_recorded_attribute(DomElement* element, const char* name) {
    for (DomNamespacedAttribute* attr = dom_element_namespaced_attributes(element); attr; attr = attr->next)
        if (attr->active && strcmp(attr->qualified_name, name) == 0) return attr;
    return nullptr;
}

static bool dom_element_record_all_attributes(DomElement* element) {
    if (!element) return false;
    DomElementExt* ext = element->ensure_ext();
    if (!ext) return false;
    if (ext->attributes_are_recorded) return true;
    int count = 0;
    const char** names = element->attribute_names(&count);
    // D4.5.1v4: the ordered list owns distinct expanded names; Mark mirrors only the first QName.
    lam::Own<DomNamespacedAttribute> remainder = ext->namespaced_attributes;
    ext->namespaced_attributes = nullptr;
    lam::Own<DomNamespacedAttribute>* tail = &ext->namespaced_attributes;
    for (int index = 0; names && index < count; index++) {
        lam::Own<DomNamespacedAttribute>* link = &remainder;
        while (*link && (!(*link)->active || strcmp((*link)->qualified_name, names[index])))
            link = &(*link)->next;
        if (*link) {
            DomNamespacedAttribute* attr = link->get();
            *link = attr->next;
            attr->next = nullptr;
            *tail = lam::own(attr);
            tail = &attr->next;
        } else {
            const char* local = nullptr;
            const char* uri = dom_element_attribute_namespace_uri(element, names[index], &local);
            const char* value = dom_element_stored_attribute(element, names[index]);
            if (!dom_element_record_namespaced_attribute(element, uri ? uri : "", names[index], value)) {
                *tail = remainder;
                return false;
            }
            while (*tail) tail = &(*tail)->next;
        }
    }
    *tail = remainder;
    ext->attributes_are_recorded = true;
    return true;
}

const char* dom_element_find_qualified_attribute(DomElement* element,
        const char* namespace_uri, const char* local_name) {
    if (!element || !local_name) return nullptr;
    const char* wanted = namespace_uri ? namespace_uri : "";
    for (DomNamespacedAttribute* attr = dom_element_namespaced_attributes(element); attr; attr = attr->next)
        if (attr->active && strcmp(attr->namespace_uri, wanted) == 0 &&
            strcmp(attr->local_name, local_name) == 0) return attr->qualified_name;
    if (element->ext && element->ext->attributes_are_recorded) return nullptr;
    int count = 0;
    const char** names = element->attribute_names(&count);
    for (int index = 0; names && index < count; index++) {
        const char* local = nullptr;
        const char* uri = dom_element_attribute_namespace_uri(element, names[index], &local);
        if (uri && local && strcmp(uri, wanted) == 0 && strcmp(local, local_name) == 0) return names[index];
    }
    return nullptr;
}

const char* dom_element_attribute_value_ns(DomElement* element,
        const char* namespace_uri, const char* local_name) {
    if (!element || !local_name) return nullptr;
    const char* wanted = namespace_uri ? namespace_uri : "";
    const char* value = dom_element_get_namespaced_attribute(element, wanted, local_name);
    if (value || (element->ext && element->ext->attributes_are_recorded)) return value;
    if (!*wanted && !dom_element_namespaced_attributes(element) && !strchr(local_name, ':'))
        return dom_element_stored_attribute(element, local_name);
    const char* qualified = dom_element_find_qualified_attribute(element, wanted, local_name);
    return qualified ? element->get_attribute(qualified) : nullptr;
}

static bool dom_attribute_name_present(const char* name) {
    return name && name[0] != '\0';
}

bool dom_attribute_node_set_value(DomAttr* attribute, const char* value) {
    if (!attribute || !attribute->doc || !value) return false;
    if (attribute->value && strcmp(attribute->value, value) == 0) return true;
    Pool* pool = attribute->storage_pool;
    char* copy = pool_strdup(pool, value);
    if (!copy) return false;
    if (attribute->value) pool_free(pool, (void*)attribute->value.get());
    attribute->value = lam::own(copy);
    return true;
}

DomAttr* dom_attribute_node_create(DomDocument* document, const char* namespace_uri,
    const char* qualified_name, const char* value) {
    if (!document || !document->node_arena || !qualified_name || !value) return nullptr;
    DomAttr* attribute = (DomAttr*)arena_calloc(document->node_arena, sizeof(DomAttr));
    if (!attribute) return nullptr;
    attribute->node_type = DOM_NODE_ATTRIBUTE;
    attribute->id = dom_document_alloc_node_id(document);
    attribute->doc = lam::up(document);
    attribute->storage_document = lam::up(document);
    Pool* pool = document->document_pool;
    attribute->storage_pool = pool;
    const char* uri = namespace_uri ? namespace_uri : "";
    const char* colon = *uri ? strchr(qualified_name, ':') : nullptr;
    attribute->namespace_uri = lam::own(pool_strdup(pool, uri));
    attribute->qualified_name = lam::own(pool_strdup(pool, qualified_name));
    attribute->local_name = lam::own(pool_strdup(pool, colon ? colon + 1 : qualified_name));
    if (colon) {
        char* prefix = pool_strdup(pool, qualified_name);
        if (prefix) prefix[colon - qualified_name] = '\0';
        attribute->prefix = lam::own(prefix);
    }
    if (!attribute->namespace_uri || !attribute->qualified_name || !attribute->local_name ||
        (colon && !attribute->prefix) || !dom_attribute_node_set_value(attribute, value)) return nullptr;
    return dom_node_registry_register(document, attribute, sizeof(DomAttr), true) ? attribute : nullptr;
}

void dom_attribute_node_detach(DomAttr* attribute) {
    DomElement* owner = attribute ? attribute->owner_element.get() : nullptr;
    if (!owner) return;
    DomAttr* previous = nullptr;
    for (DomAttr* current = owner->ext ? owner->ext->attribute_nodes.get() : nullptr;
         current; current = current->attribute_next) {
        if (current == attribute) {
            if (previous) previous->attribute_next = attribute->attribute_next;
            else owner->ext->attribute_nodes = attribute->attribute_next;
            break;
        }
        previous = current;
    }
    attribute->owner_element = nullptr;
    attribute->attribute_next = nullptr;
    if (attribute->owner_changed) attribute->owner_changed(attribute);
    dom_node_schedule_detached(attribute->doc, attribute);
}

void dom_attribute_node_release_retired_storage(DomAttr* attribute) {
    // values and names belong to the physical source pool even after adoption (D4.5.1v4).
    const char* fields[] = {attribute->namespace_uri.get(), attribute->qualified_name.get(),
        attribute->local_name.get(), attribute->prefix.get(), attribute->value.get()};
    for (const char* field : fields) if (field) pool_free(attribute->storage_pool, (void*)field);
    attribute->namespace_uri = nullptr;
    attribute->qualified_name = nullptr;
    attribute->local_name = nullptr;
    attribute->prefix = nullptr;
    attribute->value = nullptr;
}

void dom_attribute_node_attach(DomElement* element, DomAttr* attribute) {
    if (!element || !attribute || attribute->owner_element == element) return;
    DomElementExt* extension = element->ensure_ext();
    if (!extension) return;
    dom_attribute_node_detach(attribute);
    attribute->owner_element = lam::up(element);
    attribute->attribute_next = extension->attribute_nodes;
    extension->attribute_nodes = lam::up(attribute);
    dom_node_cancel_detached(attribute->doc, attribute);
    if (attribute->owner_changed) attribute->owner_changed(attribute);
}

static DomAttr* dom_element_attribute_node_value(DomElement* element, const char* namespace_uri,
        const char* qualified_name, const char* local_name, const char* value) {
    const char* uri = namespace_uri ? namespace_uri : "";
    for (DomAttr* attribute = element->ext ? element->ext->attribute_nodes.get() : nullptr;
         attribute; attribute = attribute->attribute_next) {
        if (strcmp(attribute->namespace_uri, uri) == 0 && strcmp(attribute->local_name, local_name) == 0)
            return attribute;
    }
    DomAttr* attribute = dom_attribute_node_create(element->doc, uri, qualified_name, value ? value : "");
    if (attribute) dom_attribute_node_attach(element, attribute);
    return attribute;
}

DomAttr* dom_element_attribute_node(DomElement* element, const char* name) {
    if (!element || !name) return nullptr;
    char lower[128];
    const char* key = dom_element_attr_key(element, name, lower, sizeof(lower));
    const char* value = element->get_attribute(key);
    if (!value && !element->has_attribute(key)) return nullptr;
    const char* local = nullptr;
    const char* uri = dom_element_attribute_namespace_uri(element, key, &local);
    return dom_element_attribute_node_value(element, uri, key, local ? local : key, value);
}

DomAttr* dom_element_attribute_node_ns(DomElement* element,
        const char* namespace_uri, const char* local_name) {
    const char* qualified = dom_element_find_qualified_attribute(element, namespace_uri, local_name);
    return qualified ? dom_element_attribute_node_value(element, namespace_uri, qualified, local_name,
        dom_element_attribute_value_ns(element, namespace_uri, local_name)) : nullptr;
}

static void dom_attribute_nodes_did_set(DomElement* element, const char* key, const char* value,
        const char* namespace_uri = nullptr) {
    for (DomAttr* attribute = element->ext ? element->ext->attribute_nodes.get() : nullptr;
         attribute; attribute = attribute->attribute_next) {
        if (strcmp(attribute->qualified_name, key) == 0 &&
            (!namespace_uri || strcmp(attribute->namespace_uri, namespace_uri) == 0))
            dom_attribute_node_set_value(attribute, value);
    }
}

static void dom_attribute_nodes_did_remove(DomElement* element, const char* key,
        const char* namespace_uri = nullptr) {
    for (DomAttr* attribute = element->ext ? element->ext->attribute_nodes.get() : nullptr; attribute;) {
        DomAttr* next = attribute->attribute_next;
        if (strcmp(attribute->qualified_name, key) == 0 &&
            (!namespace_uri || strcmp(attribute->namespace_uri, namespace_uri) == 0))
            dom_attribute_node_detach(attribute);
        attribute = next;
    }
}

struct DomAttributeValueEntry {
    const char* chars;
    String* value;
};

using DomAttributeValues = TypedHashMap<DomAttributeValueEntry,
    HashMapCStrMemberKeyOps<DomAttributeValueEntry, &DomAttributeValueEntry::chars>>;

struct DomAttributeValueCache : DomDocumentResourceData {
    Input* input;
    DomAttributeValues values;
};

static void dom_attribute_values_destroy(DomDocumentResourceData* resource) {
    auto* cache = static_cast<DomAttributeValueCache*>(resource);
    cache->values.destroy();
    mem_free(cache);
}

static String* dom_attribute_value(DomDocument* doc, MarkBuilder* builder,
                                   const char* value) {
    DomAttributeValueCache* cache = nullptr;
    for (DomDocumentResource* resource = doc->resources; resource; resource = resource->next) {
        if (resource->destroy != dom_attribute_values_destroy) continue;
        auto* candidate = static_cast<DomAttributeValueCache*>(resource->data);
        if (candidate->input == doc->input) { cache = candidate; break; }
    }
    if (!cache) {
        cache = static_cast<DomAttributeValueCache*>(
            mem_calloc(1, sizeof(DomAttributeValueCache), MEM_CAT_LAYOUT));
        if (!cache) return nullptr;
        cache->input = doc->input;
        if (!cache->values.init(0) ||
            !dom_document_add_resource(doc, cache, dom_attribute_values_destroy)) {
            dom_attribute_values_destroy(cache);
            return nullptr;
        }
    }
    DomAttributeValueEntry key = {value, nullptr};
    if (const auto* found = cache->values.get(key)) return found->value;
    // published attribute strings keep Input ownership; equal writes reuse immutable bytes.
    String* string = builder->createString(value);
    if (!string) return nullptr;
    cache->values.set({string->chars, string});
    return cache->values.oom() ? nullptr : string;
}

static bool dom_element_store_attribute(DomElement* element, const char* key,
        const char* value, bool remove) {
    if (element->is_synthetic()) return remove
        ? dom_element_remove_synthetic_attribute(element, key)
        : dom_element_set_synthetic_attribute(element, key, value ? value : "");
    if (!element->doc) return false;
    Element* backing = dom_element_to_element(element);
    MarkEditor editor(element->doc, EDIT_MODE_INLINE);
    Item value_item = ItemNull;
    if (!remove && value) {
        String* stored = dom_attribute_value(element->doc, editor.builder(), value);
        if (!stored) return false;
        value_item.item = s2it(stored);
    }
    Item result = remove ? editor.elmt_delete_attr({.element = backing}, key)
        : editor.elmt_update_attr({.element = backing}, key, value_item);
    // inline editing preserves the embedded identity; ITEM_ERROR is not a backing pointer.
    if (get_type_id(result) != LMD_TYPE_ELEMENT || result.element != backing) {
        log_error("dom_element_store_attribute: inline edit failed or changed backing identity");
        return false;
    }
    return true;
}

static bool dom_element_sync_attribute_mirror(DomElement* element, const char* name) {
    DomNamespacedAttribute* first = dom_first_recorded_attribute(element, name);
    return dom_element_store_attribute(element, name, first ? first->value.get() : nullptr, !first);
}

bool DomElement::set_attribute(const char* name, const char* value, bool preserve_case) {
    DomElement* element = this;
    if (!dom_attribute_name_present(name) || !value) return false;
    char lower_name[128];
    const char* key = dom_element_attr_key(element, name, lower_name, sizeof(lower_name));
    if (preserve_case) key = name;
    DomNamespacedAttribute* recorded = dom_first_recorded_attribute(element, key);
    const char* uri = recorded ? recorded->namespace_uri.get() : "";
    const char* previous = element->get_attribute(key);
    bool same = previous && strcmp(previous, value) == 0;
    if (!dom_element_store_attribute(element, key, value, false)) return false;
    if ((recorded || (element->ext && element->ext->attributes_are_recorded)) &&
        !dom_element_record_namespaced_attribute(element, uri, key, value)) return false;
    dom_attribute_nodes_did_set(element, key, value, uri);
    if (!*uri) dom_element_attribute_did_set(element, key, value, same);
    else { element->advance_style_version(); element->set_needs_style_recompute(true); }
    return true;
}

bool dom_element_set_namespace_identity(DomElement* element,
        const char* namespace_uri, const char* local_name) {
    if (!element || !local_name) return false;
    // identity belongs to the element, independent of later ancestry or xmlns edits.
    return dom_element_store_attribute(element, "__lambda_ns_uri", namespace_uri ? namespace_uri : "", false) &&
        dom_element_store_attribute(element, "__lambda_ns_local_name", local_name, false);
}

DomAttributeIterator dom_element_attribute_iterator(DomElement* element) {
    DomAttributeIterator iterator = {};
    iterator.element = element;
    iterator.recorded = element && element->ext && element->ext->attributes_are_recorded;
    if (iterator.recorded) iterator.next = dom_element_namespaced_attributes(element);
    else if (element) iterator.names = element->attribute_names(&iterator.count);
    return iterator;
}

bool dom_element_next_attribute(DomAttributeIterator* iterator, DomAttributeView* attribute) {
    if (!iterator || !iterator->element || !attribute) return false;
    if (iterator->recorded) {
        while (iterator->next) {
            DomNamespacedAttribute* record = iterator->next;
            iterator->next = record->next;
            if (!record->active) continue;
            *attribute = {record->namespace_uri, record->qualified_name, record->local_name, record->value};
            return true;
        }
        return false;
    }
    while (iterator->names && iterator->index < iterator->count) {
        const char* name = iterator->names[iterator->index++];
        if (!name) continue;
        const char* local = name;
        const char* uri = dom_element_attribute_namespace_uri(iterator->element, name, &local);
        *attribute = {uri ? uri : "", name, local, dom_element_stored_attribute(iterator->element, name)};
        // partial metadata can override a legacy QName mirror before full recording.
        for (DomNamespacedAttribute* record = dom_element_namespaced_attributes(iterator->element); record; record = record->next) {
            if (record->active && !strcmp(record->qualified_name, name)) {
                attribute->value = record->value;
                break;
            }
        }
        return true;
    }
    return false;
}

bool dom_element_set_attribute_ns(DomElement* element,
        const char* namespace_uri, const char* qualified_name, const char* value, bool replace_name) {
    if (!qualified_name || !*qualified_name || !dom_element_record_all_attributes(element)) return false;
    const char* uri = namespace_uri ? namespace_uri : "";
    const char* colon = *uri ? strchr(qualified_name, ':') : nullptr;
    const char* local = colon ? colon + 1 : qualified_name;
    const char* existing = dom_element_find_qualified_attribute(element, uri, local);
    const char* name = existing && !replace_name ? existing : qualified_name;
    const char* previous = dom_element_attribute_value_ns(element, uri, local);
    bool same = previous && value && strcmp(previous, value) == 0;
    if (!dom_element_record_namespaced_attribute(element, uri, name, value)) return false;
    // a replaced Attr keeps its list position while the two QName mirrors are rebuilt independently.
    if (existing && strcmp(existing, name) && !dom_element_sync_attribute_mirror(element, existing)) return false;
    if (!dom_element_sync_attribute_mirror(element, name)) return false;
    dom_attribute_nodes_did_set(element, name, value ? value : "", uri);
    if (!*uri) dom_element_attribute_did_set(element, name, value ? value : "", same);
    else { element->advance_style_version(); element->set_needs_style_recompute(true); }
    return true;
}

bool dom_element_remove_attribute_ns(DomElement* element,
        const char* namespace_uri, const char* local_name) {
    if (!local_name || !dom_element_record_all_attributes(element)) return false;
    const char* uri = namespace_uri ? namespace_uri : "";
    const char* name = dom_element_find_qualified_attribute(element, uri, local_name);
    if (!name) return false;
    dom_element_remove_namespaced_attribute(element, uri, local_name);
    dom_attribute_nodes_did_remove(element, name, uri);
    if (!dom_element_sync_attribute_mirror(element, name)) return false;
    if (!*uri) dom_element_attribute_did_remove(element, name);
    else { element->advance_style_version(); element->set_needs_style_recompute(true); }
    return true;
}

bool DomElement::set_attribute(NameId name_id, const char* value) {
    StrView name = well_known_name_view(name_id);
    // DOM storage still accepts bytes, but generated callers must preserve the
    // NameId until this single backing-map boundary.
    return name.str ? set_attribute(name.str, value) : false;
}

const char* DomElement::local_name() const {
    if (!tag_name) return "";
    const char* stored = dom_element_stored_attribute(const_cast<DomElement*>(this), "__lambda_ns_local_name");
    if (stored) return stored;
    const char* prefix = strchr(tag_name, ':');
    // a colon in an HTML local name is literal unless a namespace factory recorded a qualified name.
    bool literal_html = doc && !doc->xml_document && doc->page_kind == DOM_PAGE_KIND_HTML &&
        !dom_element_stored_attribute(const_cast<DomElement*>(this), "__lambda_ns_uri");
    return prefix && !literal_html ? prefix + 1 : tag_name;
}

const char* DomElement::get_attribute(const char* name) {
    DomElement* element = this;
    if (!dom_attribute_name_present(name)) {
        return nullptr;
    }

    // HTML5 stores attribute names lowercased and CSS attr() may pass
    // mixed case, so the lookup is case-insensitive unless the name is
    // stored exactly as asked (foreign content keeps its case).
    char lower_name[128];
    const char* key = dom_element_attr_key(element, name, lower_name, sizeof(lower_name));

    // namespace resolution reads stored keys directly, avoiding recursive case normalization.
    return dom_element_stored_attribute(element, key);
}

const char* DomElement::get_attribute(NameId name_id) {
    StrView name = well_known_name_view(name_id);
    return name.str ? get_attribute(name.str) : nullptr;
}

bool DomElement::remove_attribute(const char* name) {
    DomElement* element = this;
    if (!dom_attribute_name_present(name)) {
        return false;
    }

    // HTML5: attribute names are case-insensitive (foreign content keeps case)
    char lower_name[128];
    const char* key = dom_element_attr_key(element, name, lower_name, sizeof(lower_name));

    if (element->ext && element->ext->attributes_are_recorded) {
        DomNamespacedAttribute* recorded = dom_first_recorded_attribute(element, key);
        if (recorded) return dom_element_remove_attribute_ns(element, recorded->namespace_uri, recorded->local_name);
    }

    if (!dom_element_store_attribute(element, key, nullptr, true)) return false;
    dom_attribute_nodes_did_remove(element, key);
    dom_element_attribute_did_remove(element, key);
    return true;
}

bool DomElement::remove_attribute(NameId name_id) {
    StrView name = well_known_name_view(name_id);
    return name.str ? remove_attribute(name.str) : false;
}

bool DomElement::has_attribute(const char* name) {
    DomElement* element = this;
    if (!dom_attribute_name_present(name)) {
        return false;
    }

    // HTML5: attribute names are case-insensitive (foreign content keeps case)
    char lower_name[128];
    const char* key = dom_element_attr_key(element, name, lower_name, sizeof(lower_name));

    if (!element->is_synthetic()) {
        ElementReader reader(dom_element_to_element(element));
        return reader.has_attr(key);
    }
    return dom_element_find_synthetic_attribute(element, key) >= 0;
}

bool DomElement::has_attribute(NameId name_id) {
    StrView name = well_known_name_view(name_id);
    return name.str && has_attribute(name.str);
}

const char** DomElement::attribute_names(int* count) {
    DomElement* element = this;
    if (!count) {
        if (count) *count = 0;
        return nullptr;
    }

    *count = 0;
    if (element->ext && element->ext->attributes_are_recorded) {
        DomElementExt* ext = element->ext;
        int size = 0;
        for (DomNamespacedAttribute* attr = ext->namespaced_attributes; attr; attr = attr->next)
            if (attr->active) size++;
        if (!size) return nullptr;
        if (!lam::pool_grow_array(element->storage_pool(), &ext->attribute_names_cache,
                &ext->attribute_names_capacity, size, 16)) return nullptr;
        for (DomNamespacedAttribute* attr = ext->namespaced_attributes; attr; attr = attr->next)
            if (attr->active) ext->attribute_names_cache[(*count)++] = attr->qualified_name;
        return ext->attribute_names_cache;
    }

    if (element->is_synthetic()) {
        DomElementExt* data = element->ext;
        int attr_count = data ? data->synthetic_attribute_count : 0;
        if (attr_count == 0) return nullptr;
        if (!lam::pool_grow_array(element->storage_pool(),
                &data->attribute_names_cache,
                &data->attribute_names_capacity, attr_count, 16)) {
            return nullptr;
        }
        for (int i = 0; i < attr_count; i++) {
            data->attribute_names_cache[i] = data->synthetic_attributes[i].name;
        }
        *count = attr_count;
        return data->attribute_names_cache;
    }

    Element* backing = dom_element_to_element(element);
    ElementReader reader(backing);
    int attr_count = reader.attrCount();
    if (attr_count == 0) return nullptr;

    DomElementExt* data = element->ensure_ext();
    if (!data) return nullptr;
    if (!lam::pool_grow_array(element->storage_pool(),
            &data->attribute_names_cache, &data->attribute_names_capacity,
            attr_count, 16)) return nullptr;
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
        field = typemap_next_field(type, field);
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
        element->storage_pool(), new_count * sizeof(char*));
    if (!new_classes) {
        return false;
    }

    // Copy existing classes
    if (element->class_count > 0) {
        memcpy(new_classes, element->class_names, element->class_count * sizeof(char*));
    }

    // Add new class
    size_t class_len = strlen(class_name);
    char* class_copy = pool_dup_n(element->storage_pool(), class_name, class_len);
    if (!class_copy) {
        pool_free(element->storage_pool(), (void*)new_classes);
        return false;
    }

    new_classes[element->class_count] = class_copy;
    const char** old_classes = element->class_names;
    dom_element_retain_class_names(element, lam::PoolPtr<const char*>(new_classes));
    element->class_count = new_count;
    pool_free(element->storage_pool(), (void*)old_classes);

    return true;
}

static bool dom_element_remove_cached_class(DomElement* element, const char* class_name) {
    if (!element || !class_name) {
        return false;
    }

    for (int i = 0; i < element->class_count; i++) {
        if (strcmp(element->class_names[i], class_name) == 0) {
            pool_free(element->storage_pool(), (void*)element->class_names[i]);
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
const char* dom_inline_style_declaration_end(const char* text) {
    if (!text) return nullptr;
    char quote = 0;
    int parens = 0;
    int brackets = 0;
    int braces = 0;
    const char* p = text;
    for (; *p; p++) {
        if (quote) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == quote) quote = 0;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (!*p) return p;
            p++;
            continue;
        }
        if (*p == '\\' && p[1]) { p++; continue; }
        if (*p == '\'' || *p == '"') { quote = *p; continue; }
        if (*p == '(') parens++;
        else if (*p == ')' && parens > 0) parens--;
        else if (*p == '[') brackets++;
        else if (*p == ']' && brackets > 0) brackets--;
        else if (*p == '{') braces++;
        else if (*p == '}' && braces > 0) braces--;
        else if (*p == ';' && parens == 0 && brackets == 0 && braces == 0) return p;
    }
    return p;
}

static bool dom_element_uses_quirks_css(const DomElement* element) {
    return element && element->doc &&
        is_quirks_mode((HtmlVersion)element->doc->html_version);
}

static bool dom_element_apply_transient_custom(DomElement* element, CssDeclaration* owned,
                                               const void* effect, bool* changed) {
    if (!dom_element_apply_declaration(element, owned)) {
        css_declaration_destroy_owned(owned, element->storage_pool());
        return false;
    }
    element->css_variables->animation_owner = effect;
    dom_element_clear_custom_properties(element, effect ? CSS_CUSTOM_ANIMATION : CSS_CUSTOM_PRESENTATION,
        owned->property_name, owned, effect);
    element->set_styles_resolved(false);
    if (changed) *changed = true;
    return true;
}

bool dom_element_set_animation_custom_property(DomElement* element,
    const CssDeclaration* sample, const void* effect, bool* changed) {
    if (changed) *changed = false;
    if (!element || !element->doc || !sample || !effect) return false;
    for (const CssCustomProp* prop = element->css_variables; prop; prop = prop->next) {
        if (dom_element_custom_property_selected(prop, CSS_CUSTOM_ANIMATION, sample->property_name, effect) &&
            prop->value_text && sample->value_text && prop->value_text_len == sample->value_text_len &&
            memcmp(prop->value_text, sample->value_text, sample->value_text_len) == 0) return true;
    }
    CssDeclaration* owned = css_declaration_clone_owned(sample, {}, CSS_ORIGIN_ANIMATION, element->storage_pool());
    if (!owned) return false;
    owned->presentation_value = true;
    return dom_element_apply_transient_custom(element, owned, effect, changed);
}

bool dom_element_clear_animation_custom_properties(DomElement* element, const void* effect, const char* name) {
    bool changed = effect && dom_element_clear_custom_properties(element, CSS_CUSTOM_ANIMATION, name, nullptr, effect);
    if (changed) {
        element->advance_style_version();
        element->set_needs_style_recompute(true);
        element->set_styles_resolved(false);
    }
    return changed;
}

bool dom_element_set_presentation_style(DomElement* element, const char* property,
                                        const char* value, bool* changed) {
    if (changed) *changed = false;
#ifdef LAMBDA_NO_CSS_INPUT
    return false;
#else
    if (!element || !element->doc || !property || !value) return false;
    CssPropertyCode code = css_property_code_from_name(property);
    bool custom = property[0] == '-' && property[1] == '-' && property[2];
    if (!custom && (code <= 0 || code >= CSS_PROPERTY_CUSTOM)) return false;
    CssDeclaration* old = nullptr;
    if (custom) {
        for (CssCustomProp* prop = element->css_variables; prop; prop = prop->next) {
            if (dom_element_custom_property_selected(prop, CSS_CUSTOM_PRESENTATION, property)) {
                old = prop->declaration;
                break;
            }
        }
    } else old = style_tree_get_presentation_declaration(element->specified_style, code);
    if (old && old->value_text && old->value_text_len == strlen(value) &&
        memcmp(old->value_text, value, old->value_text_len) == 0) return true;
    Pool* scratch = mem_pool_create((MemContext*)element->doc->services.mem_ctx,
        MEM_ROLE_CSS, "css.presentation.parse");
    if (!scratch) return false;
    CssDeclaration* parsed = css_parse_property_declaration(property, strlen(property),
        value, strlen(value), scratch);
    bool accepted = parsed && parsed->property_code == code && !parsed->important &&
        (custom || css_property_validate_value(code, parsed->value)) &&
        css_declaration_can_clone_owned(parsed);
    CssDeclaration* owned = accepted ? css_declaration_clone_owned(parsed, {},
        CSS_ORIGIN_ANIMATION, element->storage_pool()) : nullptr;
    mem_pool_destroy(scratch);
    if (!owned) return false;
    if (!style_epoch_ensure_owned(element)) {
        css_declaration_destroy_owned(owned, element->storage_pool());
        return false;
    }
    owned->presentation_value = true;
    if (custom) {
        // Custom samples share normal var() cascade/inheritance and retain
        // authored fallbacks across recascade, replacement and clear.
        return dom_element_apply_transient_custom(element, owned, nullptr, changed);
    }
    // A sample replaces its own layer without discarding authored fallbacks.
    style_tree_remove_presentation_declarations(element->specified_style, code);
    if (!style_tree_apply_declaration(element->specified_style, owned)) {
        css_declaration_destroy_owned(owned, element->storage_pool());
        return false;
    }
    element->advance_style_version();
    element->set_needs_style_recompute(true);
    element->set_styles_resolved(false);
    if (changed) *changed = true;
    return true;
#endif
}

bool dom_element_clear_presentation_style(DomElement* element) {
    if (!element || !element->specified_style || element->specified_style_shared()) return false;
    bool changed = style_tree_remove_presentation_declarations(element->specified_style,
        CSS_PROPERTY_UNKNOWN);
    changed = dom_element_clear_custom_properties(element, CSS_CUSTOM_PRESENTATION) || changed;
    if (changed) {
        element->advance_style_version();
        element->set_needs_style_recompute(true);
        element->set_styles_resolved(false);
    }
    return changed;
}

#ifndef LAMBDA_NO_CSS_INPUT
static bool dom_element_apply_inline_declaration(DomElement* element, const CssDeclaration* parsed) {
    CssDeclaration* declaration = css_declaration_snapshot(parsed, element->storage_pool());
    if (!declaration) return false;
    declaration->origin = CSS_ORIGIN_AUTHOR;
    declaration->specificity = {1, 0, 0, 0, declaration->important};
    bool applied = dom_element_apply_declaration(element, declaration);
    if (!applied && declaration->tree_owned_record)
        css_declaration_destroy_owned(declaration, element->storage_pool());
    return applied;
}
#endif

int dom_element_apply_inline_style(DomElement* element, const char* style_text) {
#ifdef LAMBDA_NO_CSS_INPUT
    // reduced profiles retain the raw HTML style attribute without CSS parsing.
    (void)element;
    (void)style_text;
    return 0;
#else
    if (!element || !style_text || !element->doc) {
        return 0;
    }

    int applied_count = 0;
    if (CssRule* authored = dom_element_inline_declaration_block(element)) {
        for (size_t i = 0; i < authored->data.style_rule.declaration_count; i++)
            applied_count += dom_element_apply_inline_declaration(element,
                authored->data.style_rule.declarations[i]);
        return applied_count;
    }
    Pool* parse_pool = mem_pool_create((MemContext*)element->doc->services.mem_ctx,
        MEM_ROLE_CSS, "css.inline.parse");
    if (!parse_pool) return 0;

    size_t count = 0;
    CssDeclaration** declarations = css_parse_declaration_list_text_mode(style_text,
        strlen(style_text), parse_pool, &count, dom_element_uses_quirks_css(element));
    for (size_t i = 0; i < count; i++)
        applied_count += dom_element_apply_inline_declaration(element, declarations[i]);

    mem_pool_destroy(parse_pool);
    return applied_count;
#endif
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
        element->advance_style_version();
        element->set_needs_style_recompute(true);
        element->set_styles_resolved(false);
    }

    return removed_attr || removed_decl;
}

static CssCustomProp* css_custom_property_winner(CssCustomProp* variables,
    const char* name, const CssDeclaration* ceiling = nullptr,
    const CssRollbackFilter* filters = nullptr, size_t name_length = (size_t)-1,
    bool exclude_animations = false) {
    CssCustomProp* winner = nullptr;
    for (CssCustomProp* variable = variables; variable; variable = variable->next) {
        if (exclude_animations && variable->animation_owner) continue;
        StrView stored = variable->declaration ? css_declaration_name(variable->declaration)
            : strview_from_cstr(variable->name);
        if (!css_custom_property_name_matches(stored.str, name, stored.length, name_length)) continue;
        if (variable->declaration &&
            !css_declaration_cascade_eligible(variable->declaration, ceiling, filters)) continue;
        if (!winner || !winner->declaration || !variable->declaration ||
            css_declaration_cascade_compare(variable->declaration, winner->declaration) > 0) {
            winner = variable;
        }
    }
    if (winner && css_declaration_is_rollback(winner->declaration)) {
        CssRollbackFilter filter = {winner->declaration, filters};
        return css_custom_property_winner(variables, name, winner->declaration, &filter, name_length, exclude_animations);
    }
    return winner;
}

// typed computation and authored serialization use the same rollback winner.
const CssCustomProp* dom_element_lookup_own_custom_property_entry(DomElement* element,
    const char* name, size_t name_length, bool exclude_animations) {
    return element ? css_custom_property_winner(element->css_variables,
        name, nullptr, nullptr, name_length, exclude_animations) : nullptr;
}

const CssValue* dom_element_lookup_own_custom_property(DomElement* element, const char* name,
    size_t name_length, StrView* token_text) {
    const CssCustomProp* winner = dom_element_lookup_own_custom_property_entry(element, name, name_length);
    if (token_text) *token_text = winner
        ? strview_init(winner->value_text, winner->value_text_len) : strview_init(nullptr, 0);
    return winner ? winner->value : nullptr;
}

// return the declaration owner so inherited references use its computed environment.
const CssValue* dom_element_lookup_custom_property(DomElement* element,
                                                  const char* var_name,
                                                  DomElement** owner) {
    if (owner) *owner = nullptr;
    if (!element || !var_name) return nullptr;
    while (element) {
        // Check if this element has CSS variables
        if (element->css_variables) {
            CssCustomProp* winner = css_custom_property_winner(element->css_variables, var_name);
            if (winner) {
                CssEnum keyword = winner->value && winner->value->type == CSS_VALUE_TYPE_KEYWORD
                    ? winner->value->data.keyword : CSS_VALUE_NONE;
                if (keyword == CSS_VALUE_INITIAL) return nullptr;
                if (keyword == CSS_VALUE_INHERIT || keyword == CSS_VALUE_UNSET) {
                    element = dom_parent_element(element);
                    continue;
                }
                if (owner) *owner = element;
                return winner->value;
            }
        }
        element = dom_parent_element(element);
    }
    return nullptr;
}

bool css_custom_property_name_matches(const char* stored_name,
    const char* lookup_name, size_t stored_length, size_t lookup_length) {
    if (!stored_name || !lookup_name) return false;
    StrView stored = strview_init(stored_name, stored_length == (size_t)-1 ? strlen(stored_name) : stored_length);
    StrView lookup = strview_init(lookup_name, lookup_length == (size_t)-1 ? strlen(lookup_name) : lookup_length);
    if (stored.length >= 2 && stored.str[0] == '-' && stored.str[1] == '-')
        stored = strview_sub(&stored, 2, stored.length);
    if (lookup.length >= 2 && lookup.str[0] == '-' && lookup.str[1] == '-')
        lookup = strview_sub(&lookup, 2, lookup.length);
    return strview_eq(&stored, &lookup);
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

        CssCustomProp* prop = (CssCustomProp*)pool_calloc(element->storage_pool(), sizeof(CssCustomProp));
        if (!prop) {
            log_error("[CSS] Failed to allocate CssCustomProp");
            return false;
        }

        prop->name = lam::up(declaration->property_name);
        prop->value = lam::up(declaration->value);
        prop->value_text = lam::up(declaration->value_text);
        prop->value_text_len = declaration->value_text_len;
        prop->declaration = lam::up(declaration);
        prop->next = element->css_variables;
        element->css_variables = lam::own(prop);

        // Increment style version to invalidate caches
        element->advance_style_version();
        element->set_needs_style_recompute(true);

        return true;
    }

    // Validate the property value before applying
    if (!css_property_validate_value_mode(declaration->property_code,
                                          declaration->value,
                                          dom_element_uses_quirks_css(element))) {
        return false;
    }

    // Apply to specified style tree
    StyleNode* node = style_tree_apply_declaration(element->specified_style, declaration);
    if (!node) {
        return false;
    }

    // Increment style version to invalidate caches
    element->advance_style_version();
    element->set_needs_style_recompute(true);

    return true;
}

int dom_element_apply_rule(DomElement* element, CssRule* rule, CssSpecificity specificity,
                            uint32_t scope_proximity) {
    if (!element || !rule) {
        return 0;
    }

    if (style_epoch_record_rule(element, rule, specificity, scope_proximity)) {
        return (int)rule->data.style_rule.declaration_count;
    }

    int applied_count = 0;

    // Apply each declaration from the rule
    if ((rule->type == CSS_RULE_STYLE ||
         rule->type == CSS_RULE_NESTED_DECLARATIONS) &&
        rule->data.style_rule.declarations) {
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            CssDeclaration* decl = rule->data.style_rule.declarations[i];
            if (decl) {
                CssDeclaration* element_decl = css_declaration_clone_for_cascade(
                    decl, specificity, rule->origin, element->storage_pool());
                if (element_decl) element_decl->scope_proximity = scope_proximity;
                if (element_decl && dom_element_apply_declaration(element, element_decl)) {
                    applied_count++;
                } else if (element_decl) {
                    // rejected cascade copies never enter an owned style tree
                    css_declaration_destroy_owned(element_decl,
                        element->storage_pool());
                }
            }
        }
    }

    return applied_count;
}

CssDeclaration* dom_element_get_specified_value(DomElement* element, CssPropertyCode property_code) {
    if (!element || !element->specified_style) {
        return NULL;
    }

    return style_tree_get_declaration(element->specified_style, property_code);
}

bool dom_element_remove_property(DomElement* element, CssPropertyCode property_code) {
    if (!element || !element->specified_style) {
        return false;
    }

    if (!style_epoch_ensure_owned(element)) return false;
    bool removed = style_tree_remove_property(element->specified_style, property_code);

    if (removed) {
        element->advance_style_version();
        element->set_needs_style_recompute(true);
    }

    return removed;
}

bool dom_element_clear_pseudo_styles(DomElement* element) {
    if (!element) return false;
    CssSelectionStyle* selection = style_epoch_selection_style(element, false);
    bool cleared = selection &&
        (selection->color.source || selection->background_color.source);
    style_epoch_selection_clear_element(element);
    if (!element->ext) return cleared;
    for (int kind = 0; kind < PSEUDO_STYLE_COUNT; kind++) {
        lam::Own<StyleTree>* slot = &element->ext->pseudo_styles[kind];
        StyleTree* style = *slot;
        if (!style) continue;
        StyleTree* replacement = style_tree_create(element->storage_pool());
        if (!replacement) {
            // Preserve the old in-place reset only when publication of a new
            // tree fails; generated boxes must still see valid declarations.
            style_tree_clear(style);
            cleared = true;
            continue;
        }
        *slot = lam::own(replacement);
        // Retained generated boxes still borrow `style`.  Its declaration graph
        // becomes reclaimable as each box rebinds or leaves the view tree.
        style_tree_retire_borrow_source(style);
        cleared = true;
    }
    return cleared;
}

// ============================================================================
// Pseudo-Element Style Management (::before, ::after)
// ============================================================================

static int dom_element_apply_selection_rule(DomElement* element, CssRule* rule,
                                            CssSpecificity specificity, uint32_t scope_proximity) {
    if (!element || !rule || rule->type != CSS_RULE_STYLE) return 0;
    int applied_count = 0;
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* declaration = rule->data.style_rule.declarations[i];
        if (!declaration || !declaration->value ||
            !css_property_validate_value(declaration->property_code,
                                         declaration->value)) continue;
        CssSelectionCascadeValue* target = nullptr;
        CssSelectionStyle* selection = nullptr;
        if (declaration->property_code == CSS_PROPERTY_COLOR) {
            selection = style_epoch_selection_style(element, true);
            target = selection ? &selection->color : nullptr;
        } else if (declaration->property_code == CSS_PROPERTY_BACKGROUND_COLOR ||
                   declaration->property_code == CSS_PROPERTY_BACKGROUND) {
            selection = style_epoch_selection_style(element, true);
            target = selection ? &selection->background_color : nullptr;
        }
        if (!target) continue;
        // The stylesheet owns the value; this element retains only the
        // cascade metadata needed to select its highlight paint colors.
        CssDeclaration candidate = *declaration;
        candidate.specificity = specificity;
        candidate.specificity.important = declaration->important;
        candidate.origin = rule->origin;
        candidate.scope_proximity = scope_proximity;
        CssDeclaration previous = {};
        if (target->source) {
            previous = *target->source;
            previous.specificity = target->specificity;
            previous.origin = target->origin;
            previous.scope_proximity = target->scope_proximity;
        }
        if (!target->source ||
            css_declaration_cascade_compare(&candidate, &previous) >= 0) {
            target->source = lam::up(declaration);
            target->specificity = candidate.specificity;
            target->origin = candidate.origin;
            target->scope_proximity = scope_proximity;
            applied_count++;
        }
    }
    if (applied_count) {
        element->advance_style_version();
        element->set_needs_style_recompute(true);
    }
    return applied_count;
}

int dom_element_apply_pseudo_element_rule(DomElement* element, CssRule* rule,
                                          CssSpecificity specificity, int pseudo_element,
                                          uint32_t scope_proximity) {
    log_debug("[CSS-PSEUDO] Applying pseudo-element rule to <%s>, pseudo_type=%d",
              element ? element->tag_name : "NULL", pseudo_element);

    if (!element || !rule || !element->doc) {
        log_debug("[CSS-PSEUDO] Early return due to null element/rule/doc");
        return 0;
    }
    if (pseudo_element == PSEUDO_ELEMENT_SELECTION) {
        return dom_element_apply_selection_rule(element, rule, specificity, scope_proximity);
    }

    // Get the appropriate style tree for the pseudo-element
    lam::Own<StyleTree>* target_style = nullptr;
    const char* pseudo_name = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_BEFORE);
        pseudo_name = "::before";
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_AFTER);
        pseudo_name = "::after";
    } else if (pseudo_element == 3) {  // PSEUDO_ELEMENT_FIRST_LINE
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_FIRST_LINE);
        pseudo_name = "::first-line";
    } else if (pseudo_element == 4) {  // PSEUDO_ELEMENT_FIRST_LETTER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_FIRST_LETTER);
        pseudo_name = "::first-letter";
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_MARKER);
        pseudo_name = "::marker";
    } else if (pseudo_element == 7) {  // PSEUDO_ELEMENT_PLACEHOLDER
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_PLACEHOLDER);
        pseudo_name = "::placeholder";
    } else if (pseudo_element == 8) {  // PSEUDO_ELEMENT_BACKDROP
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_BACKDROP);
        pseudo_name = "::backdrop";
    } else if (pseudo_element == 9) {  // PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON
        target_style = element->pseudo_style_slot(PSEUDO_STYLE_FILE_SELECTOR_BUTTON);
        pseudo_name = "::file-selector-button";
    } else {
        log_debug("[CSS] Unknown pseudo-element type: %d", pseudo_element);
        return 0;
    }

    // Create style tree if needed
    if (!*target_style) {
        *target_style = lam::own(style_tree_create(element->storage_pool()));
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
                    decl, specificity, rule->origin, element->storage_pool());
                if (!element_decl) continue;
                element_decl->scope_proximity = scope_proximity;

                // Apply to pseudo-element style tree
                if (style_tree_apply_declaration(*target_style, element_decl)) {
                    applied_count++;
                    log_debug("[CSS] Applied %s property %d to <%s>",
                              pseudo_name, element_decl->property_code, element->tag_name);
                }
            }
        }
    }

    if (applied_count > 0) {
        element->advance_style_version();
        element->set_needs_style_recompute(true);
    }

    return applied_count;
}

CssDeclaration* dom_element_get_pseudo_element_value(DomElement* element,
                                                     CssPropertyCode property_code, int pseudo_element) {
    if (!element) {
        return NULL;
    }
    if (pseudo_element == PSEUDO_ELEMENT_SELECTION) {
        CssSelectionStyle* selection = style_epoch_selection_style(element, false);
        if (!selection) return nullptr;
        return property_code == CSS_PROPERTY_COLOR
            ? selection->color.source
            : property_code == CSS_PROPERTY_BACKGROUND_COLOR
                ? selection->background_color.source : nullptr;
    }

    StyleTree* style = nullptr;

    if (pseudo_element == 1) {  // PSEUDO_ELEMENT_BEFORE
        style = element->pseudo_style(PSEUDO_STYLE_BEFORE);
    } else if (pseudo_element == 2) {  // PSEUDO_ELEMENT_AFTER
        style = element->pseudo_style(PSEUDO_STYLE_AFTER);
    } else if (pseudo_element == 3) {  // PSEUDO_ELEMENT_FIRST_LINE
        style = element->pseudo_style(PSEUDO_STYLE_FIRST_LINE);
    } else if (pseudo_element == 4) {  // PSEUDO_ELEMENT_FIRST_LETTER
        style = element->pseudo_style(PSEUDO_STYLE_FIRST_LETTER);
    } else if (pseudo_element == 6) {  // PSEUDO_ELEMENT_MARKER
        style = element->pseudo_style(PSEUDO_STYLE_MARKER);
    } else if (pseudo_element == 7) {  // PSEUDO_ELEMENT_PLACEHOLDER
        style = element->pseudo_style(PSEUDO_STYLE_PLACEHOLDER);
    } else if (pseudo_element == 8) {  // PSEUDO_ELEMENT_BACKDROP
        style = element->pseudo_style(PSEUDO_STYLE_BACKDROP);
    } else if (pseudo_element == 9) {  // PSEUDO_ELEMENT_FILE_SELECTOR_BUTTON
        style = element->pseudo_style(PSEUDO_STYLE_FILE_SELECTOR_BUTTON);
    }

    if (!style) {
        return NULL;
    }

    return style_tree_get_declaration(style, property_code);
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

    return css_content_quote_char(quotes_decl ? quotes_decl->value : nullptr, is_open_quote, depth);
}

/**
 * Check if a CssValue represents an open-quote or close-quote content value.
 * Returns 1 for open-quote, 2 for close-quote, 0 for neither.
 */
const char* dom_element_get_pseudo_element_content(DomElement* element, int pseudo_element) {
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
    }
    if (!style) {
        return NULL;
    }

    CssDeclaration* content_decl = style_tree_get_declaration(style, CSS_PROPERTY_CONTENT);

    if (!content_decl || !content_decl->value) {
        return NULL;
    }

    CssValue* value = content_decl->value;

    // Return the string content
    if (value->type == CSS_VALUE_TYPE_STRING) {
        const char* str = value->data.string;
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
                return attr_value ? attr_value : "";
            }
        }
    }

    // Handle attr() via CSS_VALUE_TYPE_ATTR (parsed by CSS value parser)
    if (value->type == CSS_VALUE_TYPE_ATTR) {
        CSSAttrRef* attr_ref = value->data.attr_ref;
        if (attr_ref && attr_ref->name) {
            const char* attr_value = element->get_attribute(attr_ref->name);
            return attr_value ? attr_value : "";
        }
    }

    // Handle open-quote / close-quote (CSS_VALUE_TYPE_CUSTOM with ident name)
    int quote_type = css_content_quote_type(value);
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
            return str;
        }
    }

    return NULL;
}

/**
 * Get pseudo-element content with counter resolution
 * This version handles counter() and counters() functions
 */
struct DomContentBindings { DomElement* element; void* counters; };

static const char* dom_content_attribute(void* context, const char* name) {
    return ((DomContentBindings*)context)->element->get_attribute(name);
}
static const char* dom_content_quote(void* context, bool open, int depth) {
    return resolve_quote_char(((DomContentBindings*)context)->element, open, depth);
}
static bool dom_content_function(void* context, const CssFunction* function, StrBuf* text) {
    DomContentBindings* bindings = (DomContentBindings*)context;
    bool multiple = css_function_name_is(function, "counters");
    if (!multiple && !css_function_name_is(function, "counter")) return true;
    if (!bindings->counters || function->arg_count < (multiple ? 2 : 1)) return true;
    const char* name = css_value_identifier_name(function->args[0]);
    if (!name) return false;
    const char* separator = multiple ? css_value_identifier_name(function->args[1]) : nullptr;
    int style_index = multiple ? 2 : 1;
    uint32_t style = function->arg_count > style_index && function->args[style_index]->type == CSS_VALUE_TYPE_KEYWORD
        ? function->args[style_index]->data.keyword : CSS_VALUE_DECIMAL;
    char buffer[256] = {};
    int length = multiple ? css_counters_format(bindings->counters, name, separator ? separator : ".", style, buffer, sizeof(buffer))
        : css_counter_format(bindings->counters, name, style, buffer, sizeof(buffer));
    if (length < 0 || (size_t)length >= sizeof(buffer)) return false;
    strbuf_append_str_n(text, buffer, (size_t)length);
    return true;
}

bool dom_element_append_content(DomElement* element, const CssValue* value,
        void* counters, int* quote_depth, StrBuf* text) {
    if (!element) return false;
    DomContentBindings data = {element, counters};
    CssContentBindings bindings = {&data, dom_content_attribute, dom_content_quote, dom_content_function};
    return css_content_append(value, &bindings, quote_depth, text);
}

const char* dom_element_get_pseudo_element_content_with_counters(
        DomElement* element, int pseudo_element, void* counter_context, Arena* arena,
        int* quote_depth) {
    if (!element || !arena) return nullptr;
    PseudoStyleKind kind = pseudo_element == 1 ? PSEUDO_STYLE_BEFORE : pseudo_element == 2 ? PSEUDO_STYLE_AFTER : PSEUDO_STYLE_MARKER;
    if (pseudo_element != 1 && pseudo_element != 2 && pseudo_element != 6) return nullptr;
    StyleTree* style = element->pseudo_style(kind);
    const CssDeclaration* declaration = style ? style_tree_get_declaration(style, CSS_PROPERTY_CONTENT) : nullptr;
    if (!declaration || !declaration->value) return nullptr;
    StrBuf* text = strbuf_new();
    if (!text) return nullptr;
    int local_quote_depth = 0;
    bool ok = dom_element_append_content(element, declaration->value, counter_context,
        quote_depth ? quote_depth : &local_quote_depth, text);
    const char* result = ok ? arena_dup_n(arena, text->str ? text->str : "", text->length) : nullptr;
    strbuf_free(text);
    return result;
}

// ============================================================================
// DOM Tree Navigation
// ============================================================================

DomElement* DomElement::parent_element() const {
    return parent && parent->is_element() ? parent->as_element() : nullptr;
}

static DomElement* dom_element_in_sibling_direction(DomNode* node, bool forward) {
    // text and comment nodes share these chains; never reinterpret them as elements.
    while (node && !node->is_element()) node = forward ? node->next_sibling.get() : node->prev_sibling.get();
    return node ? node->as_element() : nullptr;
}

DomElement* DomElement::first_child_element() const {
    return dom_element_in_sibling_direction(first_child, true);
}

DomElement* DomElement::last_child_element() const {
    return dom_element_in_sibling_direction(last_child, false);
}

DomElement* DomElement::next_sibling_element() const {
    return dom_element_in_sibling_direction(next_sibling, true);
}

DomElement* DomElement::prev_sibling_element() const {
    return dom_element_in_sibling_direction(prev_sibling, false);
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

    // Preserve this live wrapper even when its detached document is non-UI.
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
    Item result = editor.dom_append_child(
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
    bool is_shadow_root = parent->tag_name &&
        strcmp(parent->tag_name, "#document-fragment") == 0 &&
        parent->shadow_host_element() != nullptr;
    bool child_is_linked = false;
    for (DomNode* current = parent->first_child; current; current = current->next_sibling) {
        if (current == (DomNode*)child) {
            child_is_linked = true;
            break;
        }
    }
    if (!parent->doc->input->ui_mode || (is_shadow_root && !child_is_linked)) {
        // ShadowRoot's backing relinker omits detached fragment children in UI mode;
        // preserve the DOM chain so flattened-tree layout can discover the template.
        dom_append_to_sibling_chain(parent, child);
    }
    dom_move_generated_after_to_end(parent);
    dom_document_process_metadata_insertion(parent->doc, child);
    log_debug("dom_element_append_child: appended element to parent (both Lambda tree and DOM chain updated)");

    return true;
}

bool DomElement::remove_child(DomElement* child) {
    return child && DomNode::remove_child(static_cast<DomNode*>(child));
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
    new_child->parent = lam::up(parent);

    // Insert before reference child
    new_child->next_sibling = lam::own(reference_child);
    new_child->prev_sibling = reference_child->prev_sibling;

    if (reference_child->prev_sibling) {
        reference_child->prev_sibling->next_sibling = lam::own(new_child);
    } else {
        // Reference child was first child
        parent->first_child = lam::own(new_child);
    }

    reference_child->prev_sibling = lam::up(new_child);

    // If inserting before first child, update last_child if needed
    if (!new_child->next_sibling) {
        parent->last_child = lam::up(new_child);
    }

    dom_document_process_metadata_insertion(parent->doc, new_child);

    return true;
}

bool dom_node_replace_in_parent(DomElement* parent, DomNode* old_child, DomNode* new_child) {
    if (!parent || !old_child || !new_child) return false;
    if (old_child->parent != parent) return false;

    // the documentElement omits its reverse doctype link; retain that forward sibling.
    DomNode* previous = old_child->prev_sibling;
    if (!previous && parent->first_child != old_child) {
        for (DomNode* sibling = parent->first_child; sibling; sibling = sibling->next_sibling) {
            if (sibling->next_sibling == old_child) {
                previous = sibling;
                break;
            }
        }
    }

    // Reinsertion before the retirement checkpoint cancels deferred recycling.
    dom_node_cancel_detached(parent->doc, new_child);

    // splice new_child into old_child's position in the linked list
    new_child->parent = lam::up(parent);
    new_child->prev_sibling = old_child->prev_sibling;
    new_child->next_sibling = old_child->next_sibling;

    if (previous) {
        previous->next_sibling = lam::own(new_child);
    } else {
        parent->first_child = lam::own(new_child);
    }

    if (old_child->next_sibling) {
        old_child->next_sibling->prev_sibling = lam::up(new_child);
    } else {
        parent->last_child = lam::up(new_child);
    }

    old_child->parent = nullptr;
    old_child->prev_sibling = nullptr;
    old_child->next_sibling = nullptr;
    dom_node_schedule_detached(parent->doc, old_child);
    dom_document_process_metadata_insertion(parent->doc, new_child);
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
        if (dom_is_css_element_child(child)) {
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
        if (dom_is_css_element_child(sibling)) {
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
        if (dom_is_css_element_child(sibling)) {
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
        // Count source element children; generated layout nodes are not DOM children.
        if (dom_is_css_element_child(child)) {
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
    // The clone's backing tree remains allocated in the source Input.
    dom_document_borrow_input_resources(clone_doc);

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
        clone->specified_style = lam::shared(style_tree_clone(source->specified_style, pool));
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
    text_node->parent = lam::up(parent_element);

    log_debug("DomText::create: created backed text node, text='%s'", native_string->chars);
    return text_node;
}

DomText* DomText::create_copy(const char* text, size_t len,
                              DomElement* parent_element) {
    if (!parent_element || !parent_element->doc || (!text && len)) return nullptr;
    DomText* text_node = create_detached_copy(parent_element->doc, text, len);
    if (text_node) text_node->parent = lam::up(parent_element);
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
    text_node->native_string = lam::up(native_string);
    text_node->text = lam::up(native_string->chars);
    text_node->length = native_string->len;

    if (!dom_node_registry_register(doc, text_node, sizeof(DomText), true)) {
        return nullptr;
    }
    dom_node_registry_set_backing_value(doc, text_node, Item{.item = s2it(native_string)});

    return text_node;
}

String* dom_document_create_string(DomDocument* doc, const char* text, size_t len) {
    if (!doc || !doc->document_pool || (!text && len > 0)) return nullptr;
    return string_from_strview(strview_init(text ? text : "", len), doc->document_pool);
}

bool dom_text_adopt_document_string(DomText* text_node, DomDocument* doc,
                                    String* string) {
    if (!text_node || !doc || !doc->document_pool || !string) return false;
    if (text_node->owns_native_string() && text_node->native_string != string) {
        // Generated and mutation strings have single-node ownership; replacing
        // one must reclaim it immediately instead of waiting for document exit.
        pool_free(dom_node_registry_owned_string_pool(doc, text_node), text_node->native_string);
    }
    if (!text_node->owns_native_string() || text_node->native_string != string)
        dom_node_registry_set_owned_string_pool(doc, text_node, doc->document_pool);
    text_node->native_string = lam::up(string);
    text_node->text = lam::up(string->chars);
    text_node->length = string->len;
    text_node->set_owns_native_string(true);
    dom_node_registry_set_backing_value(doc, text_node, Item{.item = s2it(string)});
    return true;
}

void dom_text_release_retired_storage(DomDocument* doc, DomText* text_node) {
    if (!doc || !doc->document_pool || !text_node ||
        !text_node->owns_native_string()) return;
    pool_free(dom_node_registry_owned_string_pool(doc, text_node), text_node->native_string);
    dom_node_registry_set_owned_string_pool(doc, text_node, nullptr);
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
    text_node->node_flags |= DOM_NODE_FLAG_TEXT_REINSERTABLE;
    string->flags = 0;
    string->is_ascii = str_is_ascii(text ? text : "", len) ? 1 : 0;
    str_copy(string->chars, len + 1, text, len);
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
    text_node->parent = lam::up(parent_element);
    text_node->text = lam::up(name);
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
    text_node->native_string = lam::up(string);
    text_node->text = lam::up(string->chars);
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
    // Create new String via MarkBuilder
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
    String* new_s = dom_create_mutation_string(
        editor.builder(), new_content, parent->doc->input->ui_mode);
    if (!new_s) {
        log_error("dom_text_set_content: failed to create string");
        return false;
    }
    Item new_string_item = (Item){.item = s2it(new_s)};

    // Replace child in parent Element's items array
    Item result = editor.dom_replace_child(
        {.element = dom_element_to_element(parent)},
        child_idx,
        new_string_item
    );

    if (!result.element) {
        log_error("dom_text_set_content: failed to replace child");
        return false;
    }

    if (parent->doc->input->ui_mode) {
        String* new_string = new_string_item.get_string();
        DomText* replacement = dom_text_from_fat_string(parent->doc, new_string);
        if (!replacement || replacement->parent != parent) {
            replacement = dom_find_text_child(parent, new_string);
        }
        if (replacement && replacement != text_node) {
            DomNode* prev = replacement->prev_sibling;
            DomNode* next = replacement->next_sibling;
            // Inline Mark replacement creates a wrapper for the new String, but
            // a Text data mutation must retain its existing DOM node identity.
            // Replace that transient wrapper in the live chain before exposing
            // the new backing string, so later removal cannot use stale links.
            text_node->parent = lam::up(parent);
            text_node->prev_sibling = lam::up(prev);
            text_node->next_sibling = lam::own(next);
            if (prev) {
                prev->next_sibling = lam::own(text_node);
            } else {
                parent->first_child = lam::own(text_node);
            }
            if (next) {
                next->prev_sibling = lam::up(text_node);
            } else {
                parent->last_child = lam::up(text_node);
            }
            replacement->parent = nullptr;
            replacement->prev_sibling = nullptr;
            replacement->next_sibling = nullptr;
        }
    }

    // Update text_node fields to point to new String (backward compat for callers)
    text_node->native_string = lam::up(new_string_item.get_string());
    if (!text_node->native_string) {
        log_error("dom_text_set_content: replacement string disappeared");
        return false;
    }
    text_node->text = lam::up(text_node->native_string->chars);
    text_node->length = text_node->native_string->len;
    dom_node_registry_set_backing_value(parent->doc, text_node, new_string_item);

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

static int64_t dom_text_find_backed_child_index(DomText* text_node) {
    if (!text_node || !text_node->parent || !text_node->native_string) return -1;

    Element* parent_elem = dom_element_backing((DomElement*)text_node->parent);
    if (!parent_elem) return -1;

    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING &&
            item.get_string() == text_node->native_string) {
            return i;
        }
    }
    return -1;
}

int64_t dom_text_get_child_index(DomText* text_node) {
    if (!text_node || !text_node->parent || !text_node->native_string) {
        log_error("dom_text_get_child_index: text node not backed");
        return -1;
    }

    if (!dom_element_backing((DomElement*)text_node->parent)) {
        log_error("dom_text_get_child_index: parent has no native_element");
        return -1;
    }

    int64_t child_idx = dom_text_find_backed_child_index(text_node);
    if (child_idx >= 0) {
        log_debug("dom_text_get_child_index: found at index %lld", child_idx);
        return child_idx;
    }

    log_error("dom_text_get_child_index: native_string not found in parent (may have been removed)");
    return -1;
}

bool dom_text_replace_backed_string(DomText* text_node, String* replacement) {
    if (!text_node || !replacement || !dom_text_is_backed(text_node)) return false;
    DomElement* parent = (DomElement*)text_node->parent;
    if (!parent || !parent->doc || !parent->doc->document_pool) return false;

    int64_t child_idx = dom_text_find_backed_child_index(text_node);
    Element* parent_elem = dom_element_to_element(parent);
    if (child_idx < 0 || !parent_elem) return false;

    // retain the live wrapper while replacing its Mark item; rebuilding the
    // sibling chain here invalidates ranges and editor selections mid-edit.
    parent_elem->items[child_idx] = (Item){.item = s2it(replacement)};
    return dom_text_adopt_document_string(text_node, parent->doc, replacement);
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
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
    Item result = editor.dom_delete_child(
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

    // DomNode::remove_child handles both the current chain and wrappers whose
    // links were superseded by MarkEditor relinking.
    if (!((DomNode*)parent)->remove_child(text_node)) {
        log_error("dom_text_remove: failed to unlink DOM text node");
        return false;
    }

    if (!(text_node->node_flags & DOM_NODE_FLAG_TEXT_REINSERTABLE)) {
        // static Mark text keeps the old invalidation behavior so relinking a
        // removed source node cannot recover a stale wrapper.
        text_node->native_string = nullptr;
    }
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
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
    String* s = dom_create_mutation_string(
        editor.builder(), text_content, parent->doc->input->ui_mode);
    if (!s) {
        log_error("dom_element_append_text: failed to create string");
        return nullptr;
    }
    Item string_item = (Item){.item = s2it(s)};

    // Append to parent Element's children via MarkEditor
    Item result = editor.dom_append_child(
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

    if (text_node) text_node->node_flags |= DOM_NODE_FLAG_TEXT_REINSERTABLE;
    log_debug("dom_element_append_text: appended text '%s'", text_content);

    return text_node;
}

// ============================================================================
// DOM Comment/DOCTYPE Node Implementation
// ============================================================================

bool dom_is_comment_tag(const char* tag_name) {
    return tag_name && (strcmp(tag_name, "!--") == 0 ||
        strcmp(tag_name, "#comment") == 0 || str_ieq_cstr(tag_name, "!DOCTYPE"));
}

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
    comment_node->parent = lam::up(parent_element);
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
    if (str_ieq_cstr(tag_name, "!DOCTYPE")) {
        node_type = DOM_NODE_DOCTYPE;
    } else if (dom_is_comment_tag(tag_name)) {
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
    comment_node->native_element = lam::up(native_element);
    comment_node->tag_name = lam::up(tag_name);  // RETAINED_FIELD_OK: interned reference type name, no allocation retained

    if (native_element->length > 0) {
        Item first_item = native_element->items[0];
        if (get_type_id(first_item) == LMD_TYPE_STRING) {
            String* content_str = first_item.get_string();
            if (content_str) {
                comment_node->content = lam::up(content_str->chars);
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
            comment_node->content = lam::up(data_attr);
            comment_node->length = strlen(data_attr);
        }
    }

    if (!comment_node->content) {
        comment_node->content = lam::up("");
    }

    if (!dom_node_registry_register(doc, comment_node, sizeof(DomComment), true)) {
        return nullptr;
    }
    dom_node_registry_set_backing_source(doc, comment_node, native_element);

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
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
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
        result = editor.dom_replace_child(
            {.element = comment_node->native_element},
            0,  // Content is always first child
            new_string_item
        );
    } else {
        // Append content (comment was empty)
        result = editor.dom_append_child(
            {.element = comment_node->native_element},
            new_string_item
        );
    }

    if (!result.element) {
        log_error("dom_comment_set_content: failed to update content");
        return false;
    }

    // Update DomComment to point to new String
    comment_node->native_element = lam::up(result.element);
    dom_node_registry_set_backing_source(parent->doc, comment_node, result.element);
    String* new_string = new_string_item.get_string();
    if (!new_string) {
        log_error("dom_comment_set_content: replacement string disappeared");
        return false;
    }
    comment_node->content = lam::up(new_string->chars);
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
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
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
    Item result = editor.dom_append_child(
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

    // UI relinking already created the comment wrapper; keep that identity.
    DomComment* comment_node = parent->doc->input->ui_mode
        ? (parent->last_child ? parent->last_child->as_comment() : nullptr)
        : DomComment::create(comment_item.element, parent);

    if (!comment_node || comment_node->native_element != comment_item.element) {
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
    MarkEditor editor(parent->doc, EDIT_MODE_INLINE);
    Item result = editor.dom_delete_child(
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

    // relinking preserves DOM-only survivors; explicitly unlink the deleted wrapper.
    if (!parent->remove_child(comment_node)) return false;
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
typedef TypedHashMap<ElementDomMapEntry,
    HashMapPointerMemberKeyOps<ElementDomMapEntry, &ElementDomMapEntry::element>>
    ElementDomMap;

HashMap* element_dom_map_create(void) {
    return ElementDomMap::create(64);
}

static bool dom_element_has_embedded_ui_storage(DomDocument* doc, Element* elem) {
    if (!doc || !elem) return false;
    DomElement* storage = element_to_dom_element(elem);
    return dom_document_owns_node_storage(doc, storage) &&
        storage->node_type == DOM_NODE_ELEMENT;
}

void element_dom_map_insert(HashMap* map, Element* elem, DomElement* dom_elem) {
    if (!map || !elem || !dom_elem) return;
    ElementDomMapEntry entry;
    entry.element = elem;
    entry.dom_elem = dom_elem;
    ElementDomMap::set(map, entry);
}

DomElement* element_dom_map_lookup(HashMap* map, Element* elem) {
    if (!map || !elem) return nullptr;
    ElementDomMapEntry key;
    key.element = elem;
    key.dom_elem = nullptr;
    const ElementDomMapEntry* found = ElementDomMap::get(map, key);
    return found ? found->dom_elem : nullptr;
}

void element_dom_map_remove(HashMap* map, Element* elem) {
    if (!map || !elem) return;
    ElementDomMapEntry key = {.element = elem, .dom_elem = nullptr};
    ElementDomMap::erase(map, key);
}

static const int MAX_DOM_BUILD_DEPTH = 512;
static thread_local int g_dom_build_depth = 0;

struct DomBuildDepthGuard {
    DomBuildDepthGuard() { g_dom_build_depth++; }
    ~DomBuildDepthGuard() { g_dom_build_depth--; }
};

static void dom_detach_rebuilt_child(DomNode* node, DomElement* parent) {
    // retained fat nodes move to the new tree; the old parent must not retire them.
    if (node && parent && node->parent && node->parent != parent) {
        node->parent->remove_child(node);
    }
}

static DomText* dom_rebuild_text_node(DomDocument* doc, DomElement* parent,
                                    String* text, bool ui_mode) {
    // only arena strings with a proven DOM prefix can retain their text view.
    DomText* retained = ui_mode ? dom_text_from_fat_string(doc, text) : nullptr;
    if (!retained) return DomText::create(text, parent);
    dom_detach_rebuilt_child(retained, parent);
    retained->parent = lam::up(parent);
    return retained;
}

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
    if (dom_is_comment_tag(tag_name)) {
        return nullptr;  // Not a layout element, processed as child below
    }

    // Skip XML declarations
    if (strncmp(tag_name, "?", 1) == 0) {
        return nullptr;  // Skip XML declarations
    }

    // Script elements stay in the DOM even though the UA display default is
    // `none`; page libraries query them for their loading URL and metadata.

    // UI-mode MarkBuilder values embed a DomElement, but HTML/XML fragment
    // parsers still produce plain Elements. Only reuse storage after proving it
    // is an embedded DOM allocation; reverse-casting a parsed Element corrupts
    // the preceding allocation and loses its future DOM wrapper.
    bool ui_mode = doc->input && doc->input->ui_mode;
    DomElement* retained = ui_mode && dom_element_has_embedded_ui_storage(doc, elem)
        ? element_to_dom_element(elem) : nullptr;
    dom_detach_rebuilt_child(retained, parent);
    DomElement* dom_elem = retained
        ? DomElement::create_in(retained, doc, tag_name, elem)
        : DomElement::create(doc, tag_name, elem);
    if (!dom_elem) return nullptr;

    // The HTML tree builder creates a direct form child while in table mode
    // solely to retain the form-owner pointer. Chromium keeps that node out of
    // the rendering tree unless an author display rule makes it renderable.
    if (doc->page_kind == DOM_PAGE_KIND_HTML && parent &&
            str_ieq_cstr(tag_name, "form") && parent->tag() == MARKUP_NAME_TABLE) {
        dom_elem->set_parser_inserted_table_form(true);
    }

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
    if (str_ieq_cstr(tag_name, "td") || str_ieq_cstr(tag_name, "th")) {
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
    if (str_ieq_cstr(tag_name, "a") || str_ieq_cstr(tag_name, "area")) {
        const char* href_value = extract_element_attribute(elem, "href", nullptr);
        if (href_value && strlen(href_value) > 0) {
            dom_elem->set_attribute("href", href_value);
        }
    }

    // Store form attributes; selector matching derives static pseudo-class
    // defaults from attributes before StateStore-backed view state exists.
    if (str_ieq_cstr(tag_name, "input")) {
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
    else if (str_ieq_cstr(tag_name, "select") ||
             str_ieq_cstr(tag_name, "textarea") ||
             str_ieq_cstr(tag_name, "button") ||
             str_ieq_cstr(tag_name, "optgroup") ||
             str_ieq_cstr(tag_name, "option") ||
             str_ieq_cstr(tag_name, "fieldset")) {
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
    if (doc->xml_document && !dom_element_stored_attribute(dom_elem, "__lambda_ns_uri")) {
        // capture XML expanded names while the parser's declaration ancestry is intact.
        dom_element_set_namespace_identity(dom_elem, dom_element_namespace_uri(dom_elem), dom_elem->local_name());
        dom_element_record_all_attributes(dom_elem);
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
        // D7.4.5v2 makes Error the terminal valid TypeId; representation checks
        // below still admit only the materialized child forms supported here.
        if (child_type == 0 || child_type > LMD_TYPE_ERROR) {
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
            if (dom_is_comment_tag(child_tag_name)) {
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

            // skip if nullptr (e.g., XML declarations)
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
                DomText* text_node = dom_rebuild_text_node(doc, dom_elem, text_str, ui_mode);
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
                            DomText* text_node = dom_rebuild_text_node(doc, dom_elem, text_str, ui_mode);
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
                                        DomText* tn = dom_rebuild_text_node(doc, dom_elem, s, ui_mode);
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
