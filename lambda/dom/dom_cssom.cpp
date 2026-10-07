/**
 * JavaScript CSSOM (CSS Object Model) Bridge Implementation
 *
 * Wraps CssStylesheet, CssRule, and CssDeclaration structures for JS access.
 * Uses branded native VMaps for stylesheet/rule/declaration host objects.
 */

#include "dom_cssom.h"
#include "dom_core.h"
#include "realm/dom_realm.h"
#include "dom.h"
#include "../js/js_runtime.h"
#include "../js/js_function.hpp"
#include "../js/js_class.h"
#include "../js/js_object_meta.h"
#include "../js/js_runtime_state.hpp"
#include "../js/js_property_attrs.h"
#include "../runtime/lambda-root-frame.hpp"
#include "../runtime/gc/gc_heap.h"
#include "../runtime/heap_api.h"
#include "../runtime/context_capsule.h"
#include "../runtime/transpiler.hpp"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../jube/jube_interface.h"
#include "../jube/jube_registry.h"
#include "../../lib/log.h"
#include "../../lib/str.h"
#include "../../lib/strview.h"
#include "../../lib/strbuf.h"
#include "../../lib/mem_factory.h"
#include "../../lib/mempool.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/arraylist.h"
#include "../../lib/arraylist.hpp"
#include "../../radiant/view.hpp"
#include "../input/css/dom_element.hpp"
#include "../input/css/dom_node.hpp"
#include "../input/css/dom_lifecycle.hpp"
#include "../input/css/style_epoch.hpp"
#include "../input/css/css_engine.hpp"
#include "../input/css/css_parser.hpp"
#include "../input/css/css_style.hpp"
#include "../input/css/css_formatter.hpp"
#include "../input/css/css_paged_media.hpp"
#include "../input/css/css_declaration_attributes.h"

#include <cstring>
#include <ctype.h>
#include "../../lib/mem_grow.hpp"

extern String* heap_create_name(const char* name, size_t len);
extern "C" Item vmap_new(void);
extern "C" const void* radiant_dom_stylesheet_host_type(void);
extern "C" const void* radiant_dom_css_rule_host_type(void);
extern "C" const void* radiant_dom_rule_style_decl_host_type(void);
extern "C" const void* radiant_dom_inline_style_host_type(void);
extern "C" const void* radiant_dom_computed_style_host_type(void);
extern "C" const void* radiant_dom_style_sheet_list_host_type(void);
extern "C" const void* radiant_dom_css_rule_list_host_type(void);
extern "C" Item dom_document_proxy_for_doc_bridge(void* doc);

// Forward declaration
extern "C" void* dom_document_from_item(Item item);
static Pool* get_document_pool();
// A sheet knows its own pool, or its owner element's document; the ambient
// document (realm state, absent for a Lambda-only script) is the last resort
// rather than the first (ESO114).
static Pool* sheet_pool(CssStylesheet* sheet);
extern "C" void dom_notify_mutation(DomJsMutationKind kind, void* target, void* parent);
static void cssom_collection_detach(Item wrapper);

enum CssomCacheKind { CSSOM_CACHE_SHEET, CSSOM_CACHE_RULE,
    CSSOM_CACHE_DECLARATION, CSSOM_CACHE_RULE_LIST, CSSOM_CACHE_SHEET_LIST, CSSOM_CACHE_NODE,
    CSSOM_CACHE_INLINE_STYLE, CSSOM_CACHE_COMPUTED_STYLE };
struct CssomWrapperCache;
struct CssomWrapperEntry {
    void* native;
    CssomCacheKind kind;
    CssomWrapperCache* cache;
    DomDocument* document;
    uint64_t item; // collector-registered weak slot; the index never roots wrappers
    CssomWrapperEntry* previous;
    CssomWrapperEntry* next;
    DomNodeRef node_ref;
};
struct CssomWrapperIndex {
    void* native;
    CssomCacheKind kind;
    CssomWrapperEntry* entry;
};
struct CssomWrapperCache {
    HashMap* index;
    gc_heap* gc;
    CssomWrapperEntry* entries;
    ArrayList* documents;
    CssomWrapperEntry* swept_nodes;
};
struct CssomDocumentResource : DomDocumentResourceData {
    DomDocument* document;
    ArrayList* caches;
};
static void cssom_document_destroyed(DomDocumentResourceData* data);

static uint64_t cssom_wrapper_hash(const void* value, uint64_t seed0, uint64_t seed1) {
    const CssomWrapperIndex* key = (const CssomWrapperIndex*)value;
    return hashmap_hash_identity2(&key->native, sizeof(key->native),
        &key->kind, sizeof(key->kind), seed0, seed1);
}
static int cssom_wrapper_compare(const void* a, const void* b, void*) {
    const CssomWrapperIndex* left = (const CssomWrapperIndex*)a;
    const CssomWrapperIndex* right = (const CssomWrapperIndex*)b;
    return left->native == right->native && left->kind == right->kind ? 0 : 1;
}

static void cssom_wrapper_cache_destroy(void* data);
static void* cssom_wrapper_cache_create(EvalContext* owner) {
    if (!owner->heap || !owner->heap->gc) return nullptr;
    CssomWrapperCache* cache = (CssomWrapperCache*)mem_calloc(1, sizeof(*cache), MEM_CAT_JS_RUNTIME);
    if (cache) {
        cache->index = hashmap_new(sizeof(CssomWrapperIndex), 16, 0, 0,
            cssom_wrapper_hash, cssom_wrapper_compare, nullptr, nullptr);
        if (!cache->index) { mem_free(cache); return nullptr; }
        cache->documents = arraylist_new(4);
        if (!cache->documents) {
            hashmap_free(cache->index);
            mem_free(cache);
            return nullptr;
        }
        cache->gc = owner->heap->gc;
    }
    return cache;
}

static const ContextCapsuleOps cssom_wrapper_cache_ops = {
    "dom-wrapper-cache", CONTEXT_CAPSULE_LIFETIME_REALM, 0,
    cssom_wrapper_cache_create, nullptr, cssom_wrapper_cache_destroy
};

static CssomWrapperCache* cssom_wrapper_cache(bool create) {
    if (!context) return nullptr;
    return (CssomWrapperCache*)(create
        ? context_capsule_ensure(context, CONTEXT_CAPSULE_DOM_WRAPPER_CACHE, &cssom_wrapper_cache_ops)
        : context_capsule(context, CONTEXT_CAPSULE_DOM_WRAPPER_CACHE));
}

static CssomWrapperEntry* cssom_wrapper_entry(void* native, CssomCacheKind kind) {
    CssomWrapperCache* cache = cssom_wrapper_cache(false);
    if (!native || !cache) return nullptr;
    CssomWrapperIndex key = {native, kind, nullptr};
    const CssomWrapperIndex* found = (const CssomWrapperIndex*)hashmap_get(cache->index, &key);
    return found ? found->entry : nullptr;
}
static Item cssom_cached_wrapper(void* native, CssomCacheKind kind) {
    CssomWrapperEntry* entry = cssom_wrapper_entry(native, kind);
    return entry && entry->item ? (Item){.item = entry->item} : ItemNull;
}
static void cssom_wrapper_remove(CssomWrapperEntry* entry) {
    CssomWrapperIndex key = {entry->native, entry->kind, nullptr};
    hashmap_delete(entry->cache->index, &key);
    if (entry->previous) entry->previous->next = entry->next;
    else entry->cache->entries = entry->next;
    if (entry->next) entry->next->previous = entry->previous;
}

static void cssom_wrapper_cleared(uint64_t*, void* data) {
    CssomWrapperEntry* entry = (CssomWrapperEntry*)data;
    cssom_wrapper_remove(entry);
    if (entry->kind == CSSOM_CACHE_NODE) {
        dom_node_unpin(entry->document, entry->node_ref, DOM_NODE_PIN_WRAPPER);
        // retired nodes are swept only after every weak slot in this collection has cleared.
        entry->next = entry->cache->swept_nodes;
        entry->cache->swept_nodes = entry;
    } else {
        mem_free(entry);
    }
}

static bool cssom_cache_wrapper(void* native, CssomCacheKind kind,
                                Item wrapper, DomDocument* document) {
    CssomWrapperCache* cache = cssom_wrapper_cache(true);
    if (!cache || wrapper.item == ITEM_NULL) return false;
    CssomWrapperEntry* entry = (CssomWrapperEntry*)mem_calloc(
        1, sizeof(*entry), MEM_CAT_JS_RUNTIME);
    if (!entry) return false;
    *entry = {native, kind, cache, document, wrapper.item, nullptr, nullptr};
    if (kind == CSSOM_CACHE_NODE) {
        entry->node_ref = dom_node_ref((DomNode*)native);
        if (!dom_node_pin(document, entry->node_ref, DOM_NODE_PIN_WRAPPER)) {
            mem_free(entry);
            return false;
        }
    }
    CssomWrapperIndex key = {native, kind, entry};
    hashmap_set(cache->index, &key);
    if (hashmap_oom(cache->index)) {
        if (kind == CSSOM_CACHE_NODE)
            dom_node_unpin(document, entry->node_ref, DOM_NODE_PIN_WRAPPER);
        mem_free(entry);
        return false;
    }
    entry->next = cache->entries;
    if (entry->next) entry->next->previous = entry;
    cache->entries = entry;
    gc_register_weak(cache->gc, &entry->item, cssom_wrapper_cleared, entry);
    return true;
}

static void cssom_wrapper_detach(CssomWrapperEntry* entry) {
    if (entry->item) {
        // document teardown leaves a safe JS husk, never a freed pool payload.
        Item wrapper = {.item = entry->item};
        if (entry->kind == CSSOM_CACHE_RULE_LIST || entry->kind == CSSOM_CACHE_SHEET_LIST)
            cssom_collection_detach(wrapper);
        virtual_host_set(wrapper, virtual_host_type(wrapper), nullptr);
    }
    gc_unregister_weak(entry->cache->gc, &entry->item);
    if (entry->kind == CSSOM_CACHE_NODE)
        dom_node_unpin(entry->document, entry->node_ref, DOM_NODE_PIN_WRAPPER);
    cssom_wrapper_remove(entry);
    mem_free(entry);
}

static void cssom_cache_detach(CssomWrapperCache* cache, void* document) {
    // native destruction may run during a retirement sweep of embedded documents.
    for (CssomWrapperEntry* entry = cache->swept_nodes; entry; entry = entry->next)
        if (!document || entry->document == document) entry->document = nullptr;
    // walk retained entries so removing weak slots never depends on a scratch allocation.
    CssomWrapperEntry* entry = cache->entries;
    while (entry) {
        CssomWrapperEntry* next = entry->next;
        if (!document || entry->document == document) cssom_wrapper_detach(entry);
        entry = next;
    }
}

static int cssom_pointer_equal(void* left, void* right) { return left == right; }

static void cssom_remove_link(ArrayList* links, void* value) {
    int index = arraylist_index_of(links, cssom_pointer_equal, value);
    if (index >= 0) arraylist_remove(links, index);
}

static CssomDocumentResource* cssom_document_resource(DomDocument* document) {
    if (!document) return nullptr;
    for (DomDocumentResource* resource = document->resources; resource; resource = resource->next)
        if (resource->destroy == cssom_document_destroyed)
            return (CssomDocumentResource*)resource->data;
    return nullptr;
}

static void cssom_resource_detach(CssomDocumentResource* resource) {
    if (!resource) return;
    for (int i = 0; i < resource->caches->length; i++)
        cssom_cache_detach((CssomWrapperCache*)resource->caches->data[i], resource->document);
}

extern "C" void dom_cssom_invalidate_document(void* document) {
    cssom_resource_detach(cssom_document_resource((DomDocument*)document));
}

extern "C" void dom_cssom_release_context(void) {
    if (context) context_capsule_drop(context, CONTEXT_CAPSULE_DOM_WRAPPER_CACHE);
}

static void cssom_wrapper_cache_destroy(void* data) {
    CssomWrapperCache* cache = (CssomWrapperCache*)data;
    if (!cache) return;
    cssom_cache_detach(cache, nullptr);
    while (cache->documents->length) {
        CssomDocumentResource* resource = (CssomDocumentResource*)arraylist_pop(cache->documents);
        cssom_remove_link(resource->caches, cache);
    }
    while (cache->swept_nodes) {
        CssomWrapperEntry* entry = cache->swept_nodes;
        cache->swept_nodes = entry->next;
        mem_free(entry);
    }
    arraylist_free(cache->documents);
    hashmap_free(cache->index);
    mem_free(cache);
}

static void cssom_document_destroyed(DomDocumentResourceData* data) {
    CssomDocumentResource* resource = (CssomDocumentResource*)data;
    // earlier resource nodes may already be freed during the document's destruction walk.
    cssom_resource_detach(resource);
    for (int i = 0; i < resource->caches->length; i++) {
        CssomWrapperCache* cache = (CssomWrapperCache*)resource->caches->data[i];
        cssom_remove_link(cache->documents, resource);
    }
    arraylist_free(resource->caches);
    mem_free(resource);
}

static bool cssom_prepare_document(DomDocument* document) {
    if (!document) return true;
    CssomWrapperCache* cache = cssom_wrapper_cache(true);
    if (!cache) return false;
    CssomDocumentResource* resource = cssom_document_resource(document);
    if (!resource) {
        resource = (CssomDocumentResource*)mem_calloc(1, sizeof(*resource), MEM_CAT_JS_RUNTIME);
        if (!resource) return false;
        resource->document = document;
        resource->caches = arraylist_new(2);
        if (!resource->caches ||
                !dom_document_add_resource(document, resource, cssom_document_destroyed)) {
            if (resource->caches) arraylist_free(resource->caches);
            mem_free(resource);
            return false;
        }
    }
    // native owner links survive quiescent worker handoff; a TLS registry cannot cover destruction.
    if (arraylist_index_of(resource->caches, cssom_pointer_equal, cache) >= 0) return true;
    if (!arraylist_append(resource->caches, cache)) return false;
    if (!arraylist_append(cache->documents, resource)) {
        arraylist_pop(resource->caches);
        return false;
    }
    return true;
}

extern "C" Item dom_cached_node_wrapper(void* node) {
    CssomWrapperEntry* entry = cssom_wrapper_entry(node, CSSOM_CACHE_NODE);
    return entry && entry->item && dom_node_ref_validate(entry->document, entry->node_ref)
        ? (Item){.item = entry->item} : ItemNull;
}

extern "C" bool dom_cache_node_wrapper(void* node, void* document, Item wrapper) {
    DomDocument* doc = (DomDocument*)document;
    return node && doc && cssom_prepare_document(doc) &&
        cssom_cache_wrapper(node, CSSOM_CACHE_NODE, wrapper, doc);
}

extern "C" void* dom_cached_node_wrapper_document(Item wrapper) {
    CssomWrapperEntry* entry = cssom_wrapper_entry(virtual_host_data(wrapper), CSSOM_CACHE_NODE);
    return entry && entry->item == wrapper.item &&
        dom_node_ref_validate(entry->document, entry->node_ref) ? entry->document : nullptr;
}

extern "C" void dom_wrapper_weak_slots_processed(void) {
    CssomWrapperCache* cache = cssom_wrapper_cache(false);
    if (!cache) return;
    for (CssomWrapperEntry* entry = cache->swept_nodes; entry; entry = entry->next) {
        bool already_swept = false;
        for (CssomWrapperEntry* prior = cache->swept_nodes; prior != entry; prior = prior->next) {
            if (prior->document == entry->document) { already_swept = true; break; }
        }
        if (!already_swept && entry->document) dom_retire_sweep(entry->document);
    }
    while (cache->swept_nodes) {
        CssomWrapperEntry* entry = cache->swept_nodes;
        cache->swept_nodes = entry->next;
        mem_free(entry);
    }
}

static DomDocument* cssom_sheet_document(CssStylesheet* sheet) {
    if (!sheet) return nullptr;
    if (sheet->owner_document) return sheet->owner_document;
    if (sheet->owner_element) sheet->owner_document = sheet->owner_element->doc;
    else if (sheet->parent_stylesheet)
        sheet->owner_document = cssom_sheet_document(sheet->parent_stylesheet);
    else {
        DomDocument* doc = (DomDocument*)dom_get_document();
        if (doc && doc->document_pool == sheet->pool) sheet->owner_document = doc;
    }
    return sheet->owner_document;
}

static Item cssom_wrap_host(void* native, const void* host_type,
        CssomCacheKind kind, Item owner, DomDocument* document) {
    Item cached = cssom_cached_wrapper(native, kind);
    if (cached.item != ITEM_NULL) return cached;
    if (!cssom_prepare_document(document)) return ItemNull;
    RootFrame roots(2);
    Rooted<Item> owner_root(roots, owner);
    Rooted<Item> wrapper_root(roots, vmap_new());
    if (get_type_id(wrapper_root.get()) != LMD_TYPE_VMAP) return ItemNull;
    virtual_host_set(wrapper_root.get(), host_type, native);
    if (owner_root.get().item != ITEM_NULL &&
        !vmap_set_owner(wrapper_root.get().vmap, owner_root.get())) return ItemNull;
    cssom_cache_wrapper(native, kind, wrapper_root.get(), document);
    return wrapper_root.get();
}

extern "C" Item dom_cssom_wrap_element_style(void* native_elem, void* payload, bool computed) {
    DomElement* elem = (DomElement*)native_elem;
    if (!elem || !payload) return ItemNull;
    // the traced node edge pins detached elements; the shared cache neuters pool payloads.
    Item owner = dom_cached_node_wrapper(elem);
    return cssom_wrap_host(payload, computed ? radiant_dom_computed_style_host_type()
        : radiant_dom_inline_style_host_type(), computed ? CSSOM_CACHE_COMPUTED_STYLE
        : CSSOM_CACHE_INLINE_STYLE, owner, elem->doc);
}

static void js_cssom_notify_stylesheet_mutation(CssStylesheet* stylesheet) {
    if (!stylesheet) return;
    css_stylesheet_mark_changed(stylesheet);
    // stylesheet edits do not touch a DOM node, but they still require post-script cascade.
    DomDocument* doc = cssom_sheet_document(stylesheet);
    style_epoch_mark_global_change(doc);
    DomElement* owner = stylesheet ? stylesheet->owner_element : nullptr;
    // linked and imported sheets can require document-wide recascade.
    if (owner) dom_notify_mutation(DOM_JS_MUTATION_STYLE, owner, owner->parent);
    else if (doc && doc->root) {
        dom_notify_mutation(DOM_JS_MUTATION_UNKNOWN, doc->root, nullptr);
    }
}

// =============================================================================
// Sentinel Markers for CSSOM Types
// =============================================================================

static const char js_stylesheet_vmap_marker = 0;
static const char js_css_rule_vmap_marker = 0;
static const char js_rule_decl_vmap_marker = 0;

// Legacy map markers are accepted by the predicates only so old callers fail
// through the same native unwrap path while CSSOM wrappers move to VMaps.
static TypeMap js_stylesheet_marker = {};
static TypeMap js_css_rule_marker = {};
static TypeMap js_rule_decl_marker = {};

// =============================================================================
// Type Checking
// =============================================================================

static bool js_cssom_is_host(Item item, const void* legacy_marker,
                             const void* vmap_marker,
                             const void* (*host_type)(void)) {
    if (get_type_id(item) == LMD_TYPE_VMAP) {
        return item.vmap &&
            (item.vmap->host_type == vmap_marker ||
             item.vmap->host_type == host_type()) &&
            item.vmap->host_data != nullptr;
    }
    if (get_type_id(item) != LMD_TYPE_MAP) return false;
    return item.map->type == legacy_marker;
}
JS_FORWARD_RETURN(bool, dom_is_stylesheet, (Item item), js_cssom_is_host, (item, &js_stylesheet_marker, &js_stylesheet_vmap_marker, radiant_dom_stylesheet_host_type))
static bool js_cssom_is_host_family(Item item, const void* legacy_marker,
        const void* vmap_marker, const void* (*host_type)(void)) {
    if (js_cssom_is_host(item, legacy_marker, vmap_marker, host_type)) return true;
    if (get_type_id(item) != LMD_TYPE_VMAP || !item.vmap || !item.vmap->host_data)
        return false;
    int family = jube_iface_type_slot((const JubeTypeDef*)host_type());
    return family >= 0 && family == jube_iface_type_slot(
        (const JubeTypeDef*)item.vmap->host_type);
}
JS_FORWARD_RETURN(bool, dom_is_css_rule, (Item item), js_cssom_is_host_family, (item, &js_css_rule_marker, &js_css_rule_vmap_marker, radiant_dom_css_rule_host_type))
extern "C" bool dom_is_rule_style_decl(Item item) {
    // inheritance shares interface methods, not the native payload representation.
    return !dom_is_inline_style_item(item) && !dom_is_computed_style_item(item) &&
        js_cssom_is_host_family(item, &js_rule_decl_marker,
            &js_rule_decl_vmap_marker, radiant_dom_rule_style_decl_host_type);
}

// =============================================================================
// Helper: camelCase to CSS hyphenated property name
// =============================================================================

// =============================================================================
// Helper: Create a string Item
// =============================================================================

// =============================================================================
// CSSStyleSheet Wrapper
// =============================================================================

extern "C" Item dom_cssom_wrap_stylesheet(void* stylesheet) {
    if (!stylesheet) return ItemNull;
    Item cached = cssom_cached_wrapper(stylesheet, CSSOM_CACHE_SHEET);
    if (cached.item != ITEM_NULL) return cached;
    DomDocument* document = cssom_sheet_document((CssStylesheet*)stylesheet);
    Item owner = document ? dom_document_proxy_for_doc_bridge(document) : ItemNull;
    return cssom_wrap_host(stylesheet, radiant_dom_stylesheet_host_type(),
        CSSOM_CACHE_SHEET, owner, document);
}

extern "C" Item dom_cssom_stylesheet_constructor(Item options) {
    (void)options;
    Pool* pool = get_document_pool();
    if (!pool) return ItemNull;
    // Constructed sheets share the active document pool so CSSOM wrappers and
    // their parsed rules cannot outlive the document realm that owns them.
    CssEngine* engine = css_engine_create(pool);
    if (!engine) return ItemNull;
    CssStylesheet* sheet = css_parse_stylesheet(engine, "", "<constructed-stylesheet>");
    if (sheet) sheet->constructed = true;
    css_engine_destroy(engine);
    return dom_cssom_wrap_stylesheet(sheet);
}

static void* js_cssom_unwrap_host(Item item, bool (*is_host)(Item)) {
    if (!is_host(item)) return nullptr;
    if (get_type_id(item) == LMD_TYPE_VMAP) return item.vmap->host_data;
    return item.map->data;
}

static CssStylesheet* unwrap_stylesheet(Item item) {
    return (CssStylesheet*)js_cssom_unwrap_host(item, dom_is_stylesheet);
}

// =============================================================================
// Font-Face Declaration Parsing (lazy on .style access)
// =============================================================================

// use the CssRule's legacy compatibility fields to cache parsed declarations
// for font-face rules. property_count stores declaration count, and the paired
// legacy pointers hold the cached shadow CssRule.
static CssRule* get_font_face_as_style_rule(CssRule* rule) {
    if (!rule || (rule->type != CSS_RULE_FONT_FACE && rule->type != CSS_RULE_PAGE)) return nullptr;

    Pool* pool = rule->pool ? rule->pool : get_document_pool();
    if (!pool) return nullptr;

    // keep lazy parsing for descriptor rules, but let the shared CSS parser
    // own declaration-list tokenization and error recovery.
    if (rule->property_names && rule->property_values) {
        return (CssRule*)rule->property_values;
    }

    const char* content = rule->data.generic_rule.content;
    if (!content) content = "";

    size_t decl_count = 0;
    CssDeclaration** decls = rule->type == CSS_RULE_PAGE && rule->page
        ? rule->page->declarations
        : css_parse_declaration_list_text(content, strlen(content), pool, &decl_count);
    if (rule->type == CSS_RULE_PAGE && rule->page) decl_count = rule->page->declaration_count;

    // create a shadow CssRule of type CSS_RULE_STYLE to hold the declarations
    CssRule* shadow = (CssRule*)pool_calloc(pool, sizeof(CssRule));
    if (!shadow) return nullptr;
    shadow->type = CSS_RULE_STYLE;
    shadow->pool = pool;
    shadow->data.style_rule.declarations = decls;
    shadow->data.style_rule.declaration_count = decl_count;
    shadow->data.style_rule.selector = nullptr;
    shadow->data.style_rule.selector_group = nullptr;
    css_rule_attach(shadow, rule, rule->stylesheet);

    // cache the shadow rule in the original font-face rule's legacy fields
    rule->property_count = decl_count;
    rule->property_values = (CssValue**)shadow;    // repurposed: stores CssRule*
    rule->property_names = (const char**)shadow;   // sentinel for "parsed"

    log_debug("get_font_face_as_style_rule: parsed %zu declarations from font-face content", decl_count);
    return shadow;
}

// =============================================================================
// CSSRule Wrapper
// =============================================================================

extern "C" Item dom_cssom_wrap_rule(void* rule, void* pool) {
    (void)pool;
    if (!rule) return ItemNull;
    Item cached = cssom_cached_wrapper(rule, CSSOM_CACHE_RULE);
    if (cached.item != ITEM_NULL) return cached;
    CssRule* css_rule = (CssRule*)rule;
    DomDocument* document = css_rule->owner_document;
    if (!document) document = cssom_sheet_document(css_rule->stylesheet);
    css_rule->owner_document = document;
    Item owner = document ? dom_document_proxy_for_doc_bridge(document) : ItemNull;
    const CssRuleInterface* interface = css_rule_interface(css_rule);
    // detached native rules also need the declared family before choosing a brand.
    const JubeTypeDef* base = (const JubeTypeDef*)radiant_dom_css_rule_host_type();
    if (!jube_type_has_interface(base) && !jube_find_type_by_host_type(base)) return ItemNull;
    const void* brand = interface ? jube_iface_type_by_name(interface->host_name, strlen(interface->host_name)) : nullptr;
    return cssom_wrap_host(rule, brand ? brand : radiant_dom_css_rule_host_type(),
        CSSOM_CACHE_RULE, owner, document);
}

static CssRule* unwrap_rule(Item item) {
    return (CssRule*)js_cssom_unwrap_host(item, dom_is_css_rule);
}

// =============================================================================
// CSSStyleDeclaration (rule declarations) Wrapper
// =============================================================================

static CssRule* cssom_declaration_owner(CssRule* rule) {
    // descriptor shadows expose their real CSSRule as CSSStyleDeclaration.parentRule.
    return rule && rule->type == CSS_RULE_STYLE && rule->parent &&
        (rule->parent->type == CSS_RULE_FONT_FACE || rule->parent->type == CSS_RULE_PAGE)
        ? rule->parent : rule;
}

static Item wrap_rule_decl(CssRule* rule, Pool* pool) {
    (void)pool;
    if (!rule) return ItemNull;
    Item cached = cssom_cached_wrapper(rule, CSSOM_CACHE_DECLARATION);
    if (cached.item != ITEM_NULL) return cached;
    CssRule* parent = cssom_declaration_owner(rule);
    Item owner = dom_cssom_wrap_rule(parent, parent->pool);
    CssomWrapperEntry* entry = cssom_wrapper_entry(parent, CSSOM_CACHE_RULE);
    const char* host_name = "css_style_properties";
#define CSS_DECLARATION_INTERFACE(kind, name, host, metadata) \
    if (parent->type == kind) host_name = #host;
#include "../input/css/css_declaration_interfaces.def"
#undef CSS_DECLARATION_INTERFACE
    const void* brand = jube_iface_type_by_name(host_name, strlen(host_name));
    if (!brand) return ItemNull;
    return cssom_wrap_host(rule, brand,
        CSSOM_CACHE_DECLARATION, owner, entry ? entry->document : nullptr);
}

static CssRule* unwrap_rule_decl(Item item) {
    return (CssRule*)js_cssom_unwrap_host(item, dom_is_rule_style_decl);
}

// =============================================================================
// Helper: Get the Pool from current document
// =============================================================================

static Pool* sheet_pool(CssStylesheet* sheet) {
    if (!sheet) return get_document_pool();
    if (sheet->pool) return sheet->pool;
    if (sheet->owner_element && sheet->owner_element->doc &&
        sheet->owner_element->doc->document_pool) {
        return sheet->owner_element->doc->document_pool;
    }
    return get_document_pool();
}

static Pool* get_document_pool() {
    DomDocument* doc = (DomDocument*)dom_get_document();
    return doc ? doc->document_pool : nullptr;
}

// =============================================================================
// Helper: Serialize a selector group to text
// =============================================================================

static const char* serialize_selector_text(CssRule* rule, Pool* pool) {
    if (!rule || rule->type != CSS_RULE_STYLE) return "";
    if (!pool) return "";
    if (rule->data.style_rule.authored_selector_text)
        return rule->data.style_rule.authored_selector_text;

    CssSelectorGroup* group = rule->data.style_rule.selector_group;
    CssSelector* single = rule->data.style_rule.selector;

    if (!group && !single) return "";

    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!fmt) return "";

    // if there's a selector group, use it
    if (group) {
        const char* text = css_format_selector_group(fmt, group);
        return text ? text : "";
    }

    // single selector: create a temporary group
    CssSelectorGroup temp_group;
    temp_group.selectors = &single;
    temp_group.selector_count = 1;
    const char* text = css_format_selector_group(fmt, &temp_group);
    return text ? text : "";
}

static void append_rule_declaration_text(StringBuf* buf, CssDeclaration* decl, Pool* pool) {
    if (!buf || !decl || !pool) return;

    const char* name = decl->property_name ? decl->property_name : css_property_spelling_from_code(decl->property_code);
    if (!name) return;

    stringbuf_append_all(buf, 2, name, ": ");
    stringbuf_append_str(buf, css_serialize_declaration_value(decl, pool));
    if (decl->important) {
        stringbuf_append_str(buf, " !important");
    }
}

static bool append_rule_declarations(StringBuf* buffer, CssDeclaration** declarations,
        size_t count, Pool* pool, const char* first_prefix = "") {
    bool appended = false;
    for (size_t i = 0; i < count; i++) {
        CssDeclaration* declaration = declarations[i];
        if (!declaration) continue;
        stringbuf_append_str(buffer, appended ? " " : first_prefix);
        append_rule_declaration_text(buffer, declaration, pool);
        stringbuf_append_str(buffer, ";");
        appended = true;
    }
    return appended;
}

static const char* serialize_cssom_rule_css_text(CssRule* rule, Pool* pool,
                                                 int depth) {
    if (!rule || !pool || depth > 128) return "";
    StringBuf* buf = stringbuf_new(pool);
    if (!buf) return "";
    if (rule->type == CSS_RULE_STYLE ||
        rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        bool nested_declarations = rule->type == CSS_RULE_NESTED_DECLARATIONS;
        size_t child_count = nested_declarations ? 0
            : rule->data.style_rule.nested_rule_count;
        if (!nested_declarations) {
            stringbuf_append_all(buf, 2, serialize_selector_text(rule, pool), " {");
            stringbuf_append_str(buf, child_count ? "\n" : " ");
        }
        bool appended = append_rule_declarations(buf, rule->data.style_rule.declarations,
            rule->data.style_rule.declaration_count, pool, child_count ? "  " : "");
        if (child_count && appended) stringbuf_append_str(buf, "\n");
        else if (!nested_declarations && !child_count && appended)
            stringbuf_append_str(buf, " ");
        for (size_t i = 0; i < child_count; i++) {
            CssRule* child = rule->data.style_rule.nested_rules[i];
            if (!child) continue;
            stringbuf_append_str(buf, "  ");
            stringbuf_append_str(buf,
                serialize_cssom_rule_css_text(child, pool, depth + 1));
            stringbuf_append_str(buf, "\n");
        }
        if (!nested_declarations) stringbuf_append_str(buf, "}");
    } else if (rule->type == CSS_RULE_PAGE || rule->type == CSS_RULE_FONT_FACE) {
        bool page = rule->type == CSS_RULE_PAGE;
        CssRule* declarations = get_font_face_as_style_rule(rule);
        stringbuf_append_str(buf, page ? "@page" : "@font-face");
        if (page && rule->page && rule->page->selector_text && rule->page->selector_text[0])
            stringbuf_append_all(buf, 2, " ", rule->page->selector_text);
        stringbuf_append_str(buf, " { ");
        bool appended = declarations && append_rule_declarations(buf,
            declarations->data.style_rule.declarations,
            declarations->data.style_rule.declaration_count, pool);
        if (page && rule->page) {
            for (size_t i = 0; i < rule->page->area_count; i++) {
                CssPageAreaRule* area = &rule->page->areas[i];
                const char* name = area->kind == CSS_PAGE_AREA_FOOTNOTE ? "footnote"
                    : css_page_margin_box_name(area->box);
                if (!name) continue;
                if (appended) stringbuf_append_str(buf, " ");
                stringbuf_append_all(buf, 3, "@", name, " { ");
                append_rule_declarations(buf, area->declarations, area->declaration_count, pool);
                stringbuf_append_str(buf, " }");
                appended = true;
            }
        }
        stringbuf_append_str(buf, appended ? " }" : "}");
    } else if (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
               rule->type == CSS_RULE_CONTAINER || rule->type == CSS_RULE_SCOPE || rule->type == CSS_RULE_LAYER) {
        const char* name = rule->type == CSS_RULE_MEDIA ? "media"
            : rule->type == CSS_RULE_SUPPORTS ? "supports"
            : rule->type == CSS_RULE_CONTAINER ? "container"
            : rule->type == CSS_RULE_SCOPE ? "scope" : "layer";
        stringbuf_append_all(buf, 2, "@", name);
        if (rule->data.conditional_rule.condition) {
            const char* start = rule->data.conditional_rule.condition;
            size_t length = strlen(start);
            str_trim(&start, &length);
            if (length) {
                stringbuf_append_str(buf, " ");
                stringbuf_append_str_n(buf, start, length);
            }
        }
        if (rule->type == CSS_RULE_LAYER &&
            rule->data.conditional_rule.layer_statement) {
            stringbuf_append_str(buf, ";");
            String* result = stringbuf_to_string(buf);
            return result ? result->chars : "";
        }
        stringbuf_append_str(buf, " {\n");
        for (size_t i = 0; i < rule->data.conditional_rule.rule_count; i++) {
            CssRule* child = rule->data.conditional_rule.rules[i];
            if (!child) continue;
            stringbuf_append_str(buf, "  ");
            stringbuf_append_str(buf,
                serialize_cssom_rule_css_text(child, pool, depth + 1));
            stringbuf_append_str(buf, "\n");
        }
        stringbuf_append_str(buf, "}");
    } else {
        CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        if (!fmt) return "";
        return css_format_rule(fmt, rule);
    }
    String* result = stringbuf_to_string(buf);
    return result ? result->chars : "";
}

// =============================================================================
// CSSStyleSheet Property Access
// =============================================================================

typedef enum CssomVArrayKind {
    CSSOM_VARRAY_STYLE_SHEETS,
    CSSOM_VARRAY_RULES,
} CssomVArrayKind;

typedef struct CssomVArray {
    Item owner;
    CssomVArrayKind kind;
} CssomVArray;

static void cssom_collection_detach(Item wrapper) {
    CssomVArray* collection = (CssomVArray*)virtual_host_data(wrapper);
    if (collection) collection->owner = ItemNull;
}

static int64_t cssom_rule_list_count(CssStylesheet* sheet) {
    if (!sheet) return 0;
    int64_t count = 0;
    for (size_t i = 0; i < sheet->rule_count; i++) {
        if (sheet->rules[i] && sheet->rules[i]->type != CSS_RULE_CHARSET) count++;
    }
    return count;
}

static CssRule* cssom_rule_list_at(CssStylesheet* sheet, int64_t index) {
    if (!sheet || index < 0) return nullptr;
    int64_t visible = 0;
    for (size_t i = 0; i < sheet->rule_count; i++) {
        CssRule* rule = sheet->rules[i];
        if (!rule || rule->type == CSS_RULE_CHARSET) continue;
        if (visible++ == index) return rule;
    }
    return nullptr;
}

static CssRule* cssom_owner_rule_at(Item owner, int64_t index) {
    CssStylesheet* sheet = unwrap_stylesheet(owner);
    if (sheet) return cssom_rule_list_at(sheet, index);
    CssRuleChildList children = css_rule_child_list(unwrap_rule(owner));
    return children.count && index >= 0 && (uint64_t)index < *children.count
        ? (*children.rules)[index] : nullptr;
}

static int64_t cssom_varray_count(void* data) {
    CssomVArray* collection = (CssomVArray*)data;
    if (!collection) return 0;
    if (collection->kind == CSSOM_VARRAY_STYLE_SHEETS) {
        DomDocument* doc = (DomDocument*)dom_document_from_item(collection->owner);
        return doc && doc->stylesheet_count > 0 ? doc->stylesheet_count : 0;
    }
    CssStylesheet* sheet = unwrap_stylesheet(collection->owner);
    if (sheet) return cssom_rule_list_count(sheet);
    CssRuleChildList children = css_rule_child_list(unwrap_rule(collection->owner));
    return children.count ? (int64_t)*children.count : 0;
}

static VirtualOpStatus cssom_varray_get(void* data, int64_t index, Item* out) {
    CssomVArray* collection = (CssomVArray*)data;
    if (!collection || !out || index < 0) return VIRTUAL_OP_MISSING;
    if (collection->kind == CSSOM_VARRAY_STYLE_SHEETS) {
        DomDocument* doc = (DomDocument*)dom_document_from_item(collection->owner);
        if (!doc || index >= doc->stylesheet_count) return VIRTUAL_OP_MISSING;
        *out = dom_cssom_wrap_stylesheet(doc->stylesheets[index]);
    } else {
        CssRule* rule = cssom_owner_rule_at(collection->owner, index);
        if (!rule) return VIRTUAL_OP_MISSING;
        *out = dom_cssom_wrap_rule(rule, rule->pool);
    }
    return get_type_id(*out) == LMD_TYPE_MAP || get_type_id(*out) == LMD_TYPE_VMAP
        ? VIRTUAL_OP_OK : VIRTUAL_OP_ERROR;
}

static VirtualOpStatus cssom_varray_readonly_set(
        void*, int64_t, Item, Item*) {
    return VIRTUAL_OP_READONLY;
}

static VirtualOpStatus cssom_varray_readonly_splice(
        void*, int64_t, int64_t, const Item*, int64_t, Item*) {
    return VIRTUAL_OP_READONLY;
}

static void cssom_varray_destroy(void* data) {
    mem_free(data);
}

static void cssom_varray_trace(void* data, gc_heap* gc) {
    CssomVArray* collection = (CssomVArray*)data;
    if (collection && gc) gc_mark_item(gc, collection->owner.item);
}

extern "C" const VArrayVtable dom_cssom_collection_varray_vtable = {
    {LAMBDA_VIRTUAL_ABI_VERSION, LMD_TYPE_VARRAY, {0, 0, 0},
     cssom_varray_destroy, cssom_varray_trace, nullptr},
    {cssom_varray_count, cssom_varray_get,
     cssom_varray_readonly_set, cssom_varray_readonly_splice}
};

static Item cssom_varray_new(Item owner, CssomVArrayKind kind,
                             const void* host_type) {
    void* native = kind == CSSOM_VARRAY_STYLE_SHEETS
        ? dom_document_from_item(owner) : virtual_host_data(owner);
    CssomCacheKind cache_kind = kind == CSSOM_VARRAY_STYLE_SHEETS
        ? CSSOM_CACHE_SHEET_LIST : CSSOM_CACHE_RULE_LIST;
    Item cached = cssom_cached_wrapper(native, cache_kind);
    if (cached.item != ITEM_NULL) return cached;
    RootFrame roots(1);
    Rooted<Item> owner_root(roots, owner);
    CssomWrapperEntry* entry = cssom_wrapper_entry(native,
        unwrap_stylesheet(owner_root.get()) ? CSSOM_CACHE_SHEET : CSSOM_CACHE_RULE);
    DomDocument* document = kind == CSSOM_VARRAY_STYLE_SHEETS ? (DomDocument*)native
        : entry ? entry->document : nullptr;
    if (!cssom_prepare_document(document)) return ItemNull;
    CssomVArray* collection = (CssomVArray*)mem_calloc(
        1, sizeof(CssomVArray), MEM_CAT_JS_RUNTIME);
    if (!collection) return ItemNull;
    collection->owner = owner_root.get();
    collection->kind = kind;
    Item result = varray_new(&dom_cssom_collection_varray_vtable, collection,
                             host_type, collection);
    if (get_type_id(result) != LMD_TYPE_VARRAY) {
        cssom_varray_destroy(collection);
        return ItemNull;
    }
    cssom_cache_wrapper(native, cache_kind, result, document);
    return result;
}

// Receiver-explicit per-property getters (DOM3 declared-interface bindings).
extern "C" Item dom_cssom_stylesheet_get_css_rules(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    return cssom_varray_new(sheet_item, CSSOM_VARRAY_RULES,
        radiant_dom_css_rule_list_host_type());
}

extern "C" Item dom_cssom_stylesheet_get_length(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    // exclude @charset rules from length count
    return (Item){.item = i2it(cssom_rule_list_count(sheet))};
}

extern "C" Item dom_cssom_stylesheet_get_disabled(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    return sheet->disabled ? (Item){.item = ITEM_TRUE} : (Item){.item = ITEM_FALSE};
}

extern "C" bool dom_cssom_stylesheet_set_disabled(Item sheet_item, bool disabled) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return false;
    if (sheet->disabled == disabled) return true;
    sheet->disabled = disabled;
    // CSSOM's disabled flag changes which rules participate in the cascade.
    js_cssom_notify_stylesheet_mutation(sheet);
    return true;
}

extern "C" Item dom_cssom_stylesheet_set_disabled_property(Item sheet, Item value) {
    dom_cssom_stylesheet_set_disabled(sheet, js_is_truthy(value));
    return value;
}

extern "C" Item dom_cssom_stylesheet_get_owner_node(Item receiver) {
    CssStylesheet* sheet = unwrap_stylesheet(receiver);
    return sheet && sheet->owner_element ? dom_wrap_element(sheet->owner_element) : ItemNull;
}

extern "C" Item dom_cssom_stylesheet_get_owner_rule(Item receiver) {
    CssStylesheet* sheet = unwrap_stylesheet(receiver);
    return sheet && sheet->owner_rule ? dom_cssom_wrap_rule(sheet->owner_rule,
        sheet->owner_rule->pool) : ItemNull;
}

extern "C" Item dom_cssom_stylesheet_get_parent_style_sheet(Item receiver) {
    CssStylesheet* sheet = unwrap_stylesheet(receiver);
    if (!sheet) return ItemNull;
    // The import rule can remain alive after deletion while its parent sheet becomes null.
    CssStylesheet* parent = sheet->owner_rule ? sheet->owner_rule->stylesheet : sheet->parent_stylesheet;
    return dom_cssom_wrap_stylesheet(parent);
}
JS_FORWARD_EXPRESSION(Item, dom_cssom_stylesheet_get_type, (Item sheet_item), (unwrap_stylesheet(sheet_item) ? make_string_item("text/css") : ItemNull))

extern "C" Item dom_cssom_stylesheet_get_href(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    return sheet && sheet->href ? make_string_item(sheet->href) : ItemNull;
}

extern "C" Item dom_cssom_stylesheet_get_title(Item sheet_item) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    return sheet ? make_string_item(sheet->title) : ItemNull;
}

extern "C" Item dom_cssom_stylesheet_index(Item sheet_item, int64_t index) {
    CssStylesheet* sheet = unwrap_stylesheet(sheet_item);
    if (!sheet) return ItemNull;
    return dom_cssom_wrap_rule(cssom_rule_list_at(sheet, index), sheet_pool(sheet));
}

// =============================================================================
// CSSStyleSheet Method Dispatch
// =============================================================================

struct CssomRuleList {
    CssRuleChildList storage;
    CssStylesheet* sheet;
    CssRule* parent;
    Pool* pool;
};

static CssomRuleList cssom_mutable_rule_list(Item owner) {
    CssStylesheet* sheet = unwrap_stylesheet(owner);
    if (sheet) return {{&sheet->rules, &sheet->rule_count}, sheet, nullptr, sheet_pool(sheet)};
    CssRule* rule = unwrap_rule(owner);
    return {css_rule_child_list(rule), rule ? rule->stylesheet : nullptr,
        rule, rule && rule->pool ? rule->pool : get_document_pool()};
}

static size_t cssom_rule_storage_index(const CssomRuleList* list, uint32_t index) {
    size_t visible = 0;
    for (size_t i = 0; i < *list->storage.count; i++) {
        CssRule* rule = (*list->storage.rules)[i];
        if (!list->parent && rule && rule->type == CSS_RULE_CHARSET) continue;
        if (visible++ == index) return i;
    }
    return *list->storage.count;
}

static CssRule* cssom_nesting_parent(CssRule* parent, bool scopes = false) {
    while (parent && parent->type != CSS_RULE_STYLE &&
        !(scopes && parent->type == CSS_RULE_SCOPE)) parent = parent->parent;
    return parent;
}

static bool cssom_namespace_list_is_mutable(const CssomRuleList* list) {
    for (size_t i = 0; i < *list->storage.count; i++) {
        CssRule* rule = (*list->storage.rules)[i];
        if (rule && rule->type != CSS_RULE_CHARSET && rule->type != CSS_RULE_IMPORT &&
            rule->type != CSS_RULE_NAMESPACE) return false;
    }
    return true;
}

static const char* cssom_rule_insertion_error(const CssomRuleList* list,
                                             CssRule* rule, size_t index) {
    if (rule->type == CSS_RULE_CHARSET) return "SyntaxError";
    if (list->parent) {
        if (rule->type == CSS_RULE_IMPORT || rule->type == CSS_RULE_NAMESPACE)
            return "HierarchyRequestError";
        if (cssom_nesting_parent(list->parent) && rule->type != CSS_RULE_STYLE &&
            rule->type != CSS_RULE_NESTED_DECLARATIONS && rule->type != CSS_RULE_MEDIA &&
            rule->type != CSS_RULE_SUPPORTS && rule->type != CSS_RULE_CONTAINER &&
            rule->type != CSS_RULE_SCOPE && rule->type != CSS_RULE_LAYER)
            return "HierarchyRequestError";
        return nullptr;
    }
    bool statement = rule->type == CSS_RULE_LAYER && rule->data.conditional_rule.layer_statement;
    for (size_t i = 0; i < *list->storage.count; i++) {
        CssRule* sibling = (*list->storage.rules)[i];
        if (!sibling || sibling->type == CSS_RULE_CHARSET) continue;
        bool sibling_statement = sibling->type == CSS_RULE_LAYER && sibling->data.conditional_rule.layer_statement;
        if (rule->type == CSS_RULE_IMPORT && i < index && sibling->type != CSS_RULE_IMPORT &&
            !sibling_statement) return "HierarchyRequestError";
        if (i >= index && !statement &&
            ((sibling->type == CSS_RULE_IMPORT && rule->type != CSS_RULE_IMPORT) ||
             (sibling->type == CSS_RULE_NAMESPACE && rule->type != CSS_RULE_IMPORT && rule->type != CSS_RULE_NAMESPACE)))
            return "HierarchyRequestError";
    }
    if (rule->type == CSS_RULE_NAMESPACE && !cssom_namespace_list_is_mutable(list))
        return "InvalidStateError";
    return nullptr;
}

extern "C" Item dom_cssom_insert_rule(Item owner, Item text_arg, Item index_arg) {
    RootFrame roots(3);
    Rooted<Item> owner_root(roots, owner), text_root(roots, text_arg), index_root(roots, index_arg);
    JS_ASSIGN_OR_RETURN(text, js_to_string(text_root.get()));
    text_root.set(text);
    JS_ASSIGN_OR_RETURN(number, js_to_number(index_root.get()));
    index_root.set(number);
    uint32_t index = (uint32_t)js_to_int32(it2d(number));
    CssomRuleList list = cssom_mutable_rule_list(owner_root.get());
    if (!list.storage.count) return dom_raise_type_error("insertRule requires a stylesheet or grouping rule");
    size_t count = *list.storage.count;
    size_t visible_count = list.parent ? count : (size_t)cssom_rule_list_count(list.sheet);
    if (list.parent && index > visible_count)
        return dom_raise_exception("IndexSizeError", "CSS rule insertion index exceeds the list length");
    if (!list.pool) return ItemError;
    String* source = it2s(text_root.get());
    CssRule* nesting = cssom_nesting_parent(list.parent, true);
    bool scoped = nesting && nesting->type == CSS_RULE_SCOPE;
    CssSelectorGroup* parent_group = nesting ? (scoped ? nesting->data.conditional_rule.scope_selector
        : nesting->data.style_rule.selector_group) : nullptr;
    CssFormatter* formatter = parent_group ? css_formatter_create(list.pool, CSS_FORMAT_COMPACT) : nullptr;
    const char* parent_text = formatter ? css_format_selector_group(formatter, parent_group) : nullptr;
    CssRule* rule = css_parse_rule_text_in_context(source->chars, source->len, list.pool, parent_text, parent_group, scoped);
    if (!rule && nesting) {
        size_t declaration_count = 0;
        CssDeclaration** declarations = css_parse_declaration_list_text(source->chars, source->len, list.pool, &declaration_count);
        if (declaration_count) {
            rule = (CssRule*)pool_calloc(list.pool, sizeof(CssRule));
            if (!rule) return ItemError;
            rule->pool = list.pool;
            rule->type = CSS_RULE_NESTED_DECLARATIONS;
            rule->data.style_rule.selector_group = parent_group;
            rule->data.style_rule.selector = parent_group && parent_group->selector_count ? parent_group->selectors[0] : nullptr;
            rule->data.style_rule.declarations = declarations;
            rule->data.style_rule.declaration_count = declaration_count;
        }
    }
    if (!rule) return dom_raise_exception("SyntaxError", "CSS text does not parse as one rule or nested declaration block");
    // stylesheet parsing and constructed-sheet restrictions precede its index check.
    if (!list.parent && list.sheet->constructed && rule->type == CSS_RULE_IMPORT)
        return dom_raise_exception("SyntaxError", "Constructed stylesheets cannot contain @import rules");
    if (index > visible_count)
        return dom_raise_exception("IndexSizeError", "CSS rule insertion index exceeds the list length");
    size_t raw_index = cssom_rule_storage_index(&list, index);
    const char* error = cssom_rule_insertion_error(&list, rule, raw_index);
    if (error) return dom_raise_exception(error, "CSS rule is not allowed at this position");
    if (list.sheet && !css_bind_rule_namespaces(rule, list.sheet))
        return dom_raise_exception("SyntaxError", "CSS selector uses an undeclared namespace");
    size_t capacity = list.parent ? count : list.sheet->rule_capacity;
    if (!lam::pool_copy_grow_array(list.pool, list.storage.rules, &capacity, count, count + 1, 8, true))
        return ItemError;
    if (!list.parent) list.sheet->rule_capacity = capacity;
    auto* namespaces = list.sheet ? list.sheet->namespaces : nullptr;
    size_t namespace_index = 0;
    if (rule->type == CSS_RULE_NAMESPACE) {
        for (size_t i = 0; i < raw_index; i++)
            if ((*list.storage.rules)[i]->type == CSS_RULE_NAMESPACE) namespace_index++;
        namespaces = (decltype(namespaces))pool_calloc(list.pool, (list.sheet->namespace_count + 1) * sizeof(*namespaces));
        if (!namespaces) return ItemError;
        for (size_t i = 0; i < list.sheet->namespace_count + 1; i++) {
            if (i == namespace_index) {
                namespaces[i].prefix = rule->data.namespace_rule.prefix;
                namespaces[i].url = rule->data.namespace_rule.namespace_url;
            } else namespaces[i] = list.sheet->namespaces[i < namespace_index ? i : i - 1];
        }
    }
    for (size_t i = count; i > raw_index; i--) (*list.storage.rules)[i] = (*list.storage.rules)[i - 1];
    (*list.storage.rules)[raw_index] = rule;
    *list.storage.count = count + 1;
    css_rule_attach(rule, list.parent, list.sheet);
    if (rule->type == CSS_RULE_NAMESPACE) {
        list.sheet->namespaces = namespaces;
        list.sheet->namespace_count++;
    }
    js_cssom_notify_stylesheet_mutation(list.sheet);
    return (Item){.item = i2it(index)};
}

extern "C" Item dom_cssom_delete_rule(Item owner, Item index_arg) {
    RootFrame roots(2);
    Rooted<Item> owner_root(roots, owner), index_root(roots, index_arg);
    JS_ASSIGN_OR_RETURN(number, js_to_number(index_root.get()));
    index_root.set(number);
    uint32_t index = (uint32_t)js_to_int32(it2d(number));
    CssomRuleList list = cssom_mutable_rule_list(owner_root.get());
    if (!list.storage.count) return dom_raise_type_error("deleteRule requires a stylesheet or grouping rule");
    size_t count = *list.storage.count;
    size_t visible_count = list.parent ? count : (size_t)cssom_rule_list_count(list.sheet);
    if (index >= visible_count) return dom_raise_exception("IndexSizeError", "CSS rule deletion index exceeds the list length");
    size_t raw_index = cssom_rule_storage_index(&list, index);
    CssRule* removed = (*list.storage.rules)[raw_index];
    if (removed->type == CSS_RULE_NAMESPACE && list.sheet) {
        if (!cssom_namespace_list_is_mutable(&list))
            return dom_raise_exception("InvalidStateError", "CSS namespaces cannot change after other rules exist");
        size_t namespace_index = 0;
        for (size_t i = 0; i < raw_index; i++)
            if ((*list.storage.rules)[i]->type == CSS_RULE_NAMESPACE) namespace_index++;
        for (size_t i = namespace_index; i + 1 < list.sheet->namespace_count; i++)
            list.sheet->namespaces[i] = list.sheet->namespaces[i + 1];
        list.sheet->namespace_count--;
    }
    for (size_t i = raw_index; i + 1 < count; i++) (*list.storage.rules)[i] = (*list.storage.rules)[i + 1];
    *list.storage.count = count - 1;
    // wrappers can retain deleted rules; detach associations without freeing pool-owned content.
    css_rule_attach(removed, nullptr, nullptr);
    js_cssom_notify_stylesheet_mutation(list.sheet);
    return ItemNull;
}

// =============================================================================
// CSSStyleRule Property Access
// =============================================================================

// Receiver-explicit per-property getters (DOM3 declared-interface bindings).
extern "C" Item dom_cssom_rule_get_selector_text(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->type == CSS_RULE_PAGE)
        return make_string_item(rule->page && rule->page->selector_text ? rule->page->selector_text : "");
    if (rule->type != CSS_RULE_STYLE) return make_string_item("");
    const char* sel_text = serialize_selector_text(rule, pool);
    return make_string_item(sel_text);
}

extern "C" Item dom_cssom_rule_get_style(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->type == CSS_RULE_STYLE || rule->type == CSS_RULE_NESTED_DECLARATIONS) {
        return wrap_rule_decl(rule, pool);
    }
    // font-face and page rules also expose .style
    if (rule->type == CSS_RULE_FONT_FACE || rule->type == CSS_RULE_PAGE) {
        CssRule* shadow = get_font_face_as_style_rule(rule);
        if (shadow) {
            return wrap_rule_decl(shadow, pool);
        }
    }
    return ItemNull;
}

extern "C" Item dom_cssom_rule_get_css_rules(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!css_rule_child_list(rule).count) return ItemNull;
    // retain the owner, then read its current child array on each list access.
    return cssom_varray_new(rule_item, CSSOM_VARRAY_RULES, radiant_dom_css_rule_list_host_type());
}

extern "C" Item dom_cssom_rule_get_css_text(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (!pool) return make_string_item("");
    if (rule->type == CSS_RULE_STYLE ||
        rule->type == CSS_RULE_NESTED_DECLARATIONS ||
        rule->type == CSS_RULE_PAGE || rule->type == CSS_RULE_FONT_FACE ||
        rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
        rule->type == CSS_RULE_CONTAINER || rule->type == CSS_RULE_SCOPE || rule->type == CSS_RULE_LAYER) {
        return make_string_item(serialize_cssom_rule_css_text(rule, pool, 0));
    }
    CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
    if (!fmt) return make_string_item("");
    const char* text = css_format_rule(fmt, rule);
    return make_string_item(text);
}

extern "C" Item dom_cssom_rule_get_type(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    const CssRuleInterface* interface = css_rule_interface(rule);
    return (Item){.item = i2it(interface ? interface->legacy_type : 0)};
}

extern "C" Item dom_cssom_rule_get_prototype(Item rule_item) {
    const CssRuleInterface* interface = css_rule_interface(unwrap_rule(rule_item));
    return interface ? dom_realm_constructor_prototype(interface->name) : ItemNull;
}

extern "C" Item dom_cssom_rule_type_prototype(const char* name) {
    return dom_realm_constructor_prototype(name);
}

extern "C" Item dom_cssom_rule_get_condition_text(Item receiver) {
    CssRule* rule = unwrap_rule(receiver);
    const char* text = rule && (rule->type == CSS_RULE_MEDIA || rule->type == CSS_RULE_SUPPORTS ||
        rule->type == CSS_RULE_CONTAINER) && rule->data.conditional_rule.condition
        ? rule->data.conditional_rule.condition : "";
    size_t length = strlen(text);
    str_trim(&text, &length);
    return make_string_item(pool_dup_n(rule ? rule->pool : get_document_pool(), text, length));
}

extern "C" Item dom_cssom_rule_get_namespace_prefix(Item receiver) {
    CssRule* rule = unwrap_rule(receiver);
    return make_string_item(rule && rule->type == CSS_RULE_NAMESPACE && rule->data.namespace_rule.prefix
        ? rule->data.namespace_rule.prefix : "");
}

extern "C" Item dom_cssom_rule_get_namespace_uri(Item receiver) {
    CssRule* rule = unwrap_rule(receiver);
    return make_string_item(rule && rule->type == CSS_RULE_NAMESPACE && rule->data.namespace_rule.namespace_url
        ? rule->data.namespace_rule.namespace_url : "");
}

extern "C" Item dom_cssom_rule_set_css_text(Item receiver, Item value) {
    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver), value_root(roots, value);
    // CSSRule.cssText ignores the converted string, but conversion can throw/reenter.
    JS_ASSIGN_OR_RETURN(text, js_to_string(value_root.get()));
    (void)text;
    return value_root.get();
}

extern "C" Item dom_cssom_rule_set_style(Item receiver, Item value) {
    RootFrame roots(4);
    Rooted<Item> receiver_root(roots, receiver), value_root(roots, value);
    Rooted<Item> declaration_root(roots, dom_cssom_rule_get_style(receiver_root.get()));
    Rooted<Item> key_root(roots, make_string_item("cssText"));
    return dom_cssom_rule_decl_set_property(declaration_root.get(), key_root.get(), value_root.get());
}

extern "C" Item dom_cssom_rule_get_parent_rule(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule) return ItemNull;
    Pool* pool = (rule->pool) ? rule->pool : get_document_pool();
    (void)pool;
    if (rule->parent) {
        return dom_cssom_wrap_rule(rule->parent, pool);
    }
    return ItemNull;
}

extern "C" Item dom_cssom_rule_get_parent_style_sheet(Item rule_item) {
    CssRule* rule = unwrap_rule(rule_item);
    return rule ? dom_cssom_wrap_stylesheet(rule->stylesheet) : ItemNull;
}

static Item cssom_scope_boundary(Item rule_item, bool end) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule || rule->type != CSS_RULE_SCOPE) return ItemNull;
    const char* source = end ? rule->data.conditional_rule.scope_end_text
        : rule->data.conditional_rule.scope_start_text;
    if (!source) return ItemNull;
    CssSelectorGroup* group = css_parse_selector_group_text(source, strlen(source), rule->pool, true);
    CssFormatter* formatter = group ? css_formatter_create(rule->pool, CSS_FORMAT_COMPACT) : nullptr;
    return formatter ? make_string_item(css_format_selector_group(formatter, group)) : ItemNull;
}

JS_FORWARD_EXPRESSION(Item, dom_cssom_rule_get_scope_start, (Item rule), (cssom_scope_boundary(rule, false)))
JS_FORWARD_EXPRESSION(Item, dom_cssom_rule_get_scope_end, (Item rule), (cssom_scope_boundary(rule, true)))

static Item cssom_property_descriptor(Item rule_item, unsigned descriptor) {
    CssRule* rule = unwrap_rule(rule_item);
    if (!rule || rule->type != CSS_RULE_PROPERTY) return ItemNull;
    CssPropertyRegistration* property = &rule->data.property_rule;
    if (descriptor == 2) return (Item){.item = b2it(property->inherits)};
    const char* text = descriptor == 0 ? property->name : descriptor == 1 ? property->syntax
        : property->initial_text;
    return text ? make_string_item(text) : ItemNull;
}

JS_FORWARD_EXPRESSION(Item, dom_cssom_rule_get_property_name, (Item rule), (cssom_property_descriptor(rule, 0)))
JS_FORWARD_EXPRESSION(Item, dom_cssom_rule_get_property_syntax, (Item rule), (cssom_property_descriptor(rule, 1)))
JS_FORWARD_EXPRESSION(Item, dom_cssom_rule_get_property_inherits, (Item rule), (cssom_property_descriptor(rule, 2)))
JS_FORWARD_EXPRESSION(Item, dom_cssom_rule_get_property_initial_value, (Item rule), (cssom_property_descriptor(rule, 3)))

static void cssom_rebind_nested_children(CssRule* container,
                                         const char* parent_text,
                                         CssSelectorGroup* parent_group,
                                         Pool* pool, int depth) {
    if (!container || !parent_text || !parent_group || !pool || depth > 128)
        return;
    CssRuleChildList children = css_rule_child_list(container);
    if (!children.count) return;
    for (size_t i = 0; i < *children.count; i++) {
        CssRule* child = (*children.rules)[i];
        if (!child) continue;
        if (child->type == CSS_RULE_SCOPE) {
            // scope children stay relative to their root when an enclosing selector changes.
            css_scope_rebind_prelude(child, parent_text, pool);
        } else if (child->type == CSS_RULE_NESTED_DECLARATIONS) {
            child->data.style_rule.selector_group = parent_group;
            child->data.style_rule.selector = parent_group->selector_count
                ? parent_group->selectors[0] : nullptr;
        } else if (child->type == CSS_RULE_STYLE) {
            const char* authored = child->data.style_rule.authored_selector_text;
            if (!authored) continue;
            char* canonical = nullptr;
            CssSelectorGroup* group = css_parse_nested_selector_group_text(
                authored, strlen(authored), parent_text, pool, &canonical);
            if (!group) continue;
            child->data.style_rule.selector_group = group;
            child->data.style_rule.selector = group->selector_count == 1
                ? group->selectors[0] : nullptr;
            child->data.style_rule.authored_selector_text = canonical;
            CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
            const char* child_text = fmt
                ? css_format_selector_group(fmt, group) : nullptr;
            if (child_text) cssom_rebind_nested_children(
                child, child_text, group, pool, depth + 1);
        } else {
            cssom_rebind_nested_children(
                child, parent_text, parent_group, pool, depth + 1);
        }
    }
}

extern "C" Item dom_cssom_rule_set_selector_text(Item rule_item, Item value) {
    RootFrame roots(2);
    Rooted<Item> rule_root(roots, rule_item), text_root(roots, value);
    // resolve the native rule after script conversion, which can detach its owner.
    JS_ASSIGN_OR_RETURN(text, js_to_string(text_root.get()));
    text_root.set(text);
    CssRule* rule = unwrap_rule(rule_root.get());
    if (!rule) return ItemNull;

    if (rule->type == CSS_RULE_PAGE && rule->page) {
        String* source = it2s(text_root.get());
        CssPageRule* parsed = css_page_selectors_parse_text(source->chars, source->len, rule->pool);
        if (parsed) {
            // replacing the prelude must retain declarations, margin areas and live wrappers.
            rule->page->selectors = parsed->selectors;
            rule->page->selector_count = parsed->selector_count;
            rule->page->selector_text = parsed->selector_text;
            js_cssom_notify_stylesheet_mutation(rule->stylesheet);
        }
        return value;
    }

    if (rule->type == CSS_RULE_STYLE) {
        // Use String* to get actual length (handles embedded NULLs)
        String* val_string = it2s(text_root.get());
        const char* new_text = val_string->chars;
        size_t new_text_len = val_string ? val_string->len : (new_text ? strlen(new_text) : 0);
        if (!new_text || new_text_len == 0) return value;

        Pool* pool = (rule && rule->pool) ? rule->pool : get_document_pool();
        if (!pool) return value;

        CssRule* ancestor = rule->parent;
        while (ancestor && ancestor->type != CSS_RULE_STYLE)
            ancestor = ancestor->parent;
        char* authored = nullptr;
        CssSelectorGroup* new_group = nullptr;
        if (ancestor && ancestor->data.style_rule.selector_group) {
            CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
            const char* parent_text = fmt ? css_format_selector_group(
                fmt, ancestor->data.style_rule.selector_group) : nullptr;
            if (parent_text) new_group = css_parse_nested_selector_group_text(
                new_text, new_text_len, parent_text, pool, &authored);
        } else {
            new_group = css_parse_selector_group_text(
                new_text, new_text_len, pool);
        }
        if (!new_group || new_group->selector_count == 0) {
            log_debug("js_cssom_rule_set_property: failed to parse selectorText '%s'", new_text);
            return value;  // silently ignore
        }

        // replace the rule's selectors
        rule->data.style_rule.selector_group = new_group;
        if (new_group->selector_count == 1) {
            rule->data.style_rule.selector = new_group->selectors[0];
        } else {
            rule->data.style_rule.selector = nullptr;
        }
        rule->data.style_rule.authored_selector_text = authored;
        CssFormatter* fmt = css_formatter_create(pool, CSS_FORMAT_COMPACT);
        const char* expanded_text = fmt
            ? css_format_selector_group(fmt, new_group) : nullptr;
        if (expanded_text) cssom_rebind_nested_children(
            rule, expanded_text, new_group, pool, 0);

        log_debug("js_cssom_rule_set_property: updated selectorText to '%s'", new_text);
        js_cssom_notify_stylesheet_mutation(rule->stylesheet);
        return value;
    }

    return value;
}

// =============================================================================
// CSSStyleDeclaration (rule) Property Access
// =============================================================================

// CSSOM exposes one effective declaration per property; source duplicates remain
// intact until mutation so layout keeps the authored logical-property order.
static bool cssom_decl_matches(const CssDeclaration* declaration, const char* name) {
    if (!declaration || !name) return false;
    CssPropertyCode code = css_property_code_from_name(name);
    return code > 0 ? declaration->property_code == code
        : declaration->property_name && strcmp(declaration->property_name, name) == 0;
}

static CssDeclaration* cssom_decl_find(CssRule* rule, const char* name) {
    if (!rule || !name) return nullptr;
    CssDeclaration* selected = nullptr;
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        CssDeclaration* declaration = rule->data.style_rule.declarations[i];
        if (cssom_decl_matches(declaration, name) &&
            (!selected || !selected->important || declaration->important)) selected = declaration;
    }
    return selected;
}

static bool cssom_decl_supports(CssRule* rule, const char* name) {
    if (!name || !name[0]) return false;
    if (name[0] == '-' && name[1] == '-')
        return css_parse_custom_property_name(name, strlen(name));
    // descriptor members exist independently of whether a declaration is present.
    CssRule* owner = cssom_declaration_owner(rule);
    if (owner && (owner->type == CSS_RULE_FONT_FACE || owner->type == CSS_RULE_PAGE)) {
        static const char* font_descriptors[] = {
#define CSS_DECLARATION_NAME(field, js_name, css_name) css_name,
            CSS_FONT_FACE_DESCRIPTORS_ATTRIBUTES(CSS_DECLARATION_NAME)
        };
        static const char* page_descriptors[] = {
            CSS_PAGE_DESCRIPTORS_ATTRIBUTES(CSS_DECLARATION_NAME)
#undef CSS_DECLARATION_NAME
        };
        const char* const* names = owner->type == CSS_RULE_FONT_FACE
            ? font_descriptors : page_descriptors;
        size_t count = owner->type == CSS_RULE_FONT_FACE
            ? sizeof(font_descriptors) / sizeof(font_descriptors[0])
            : sizeof(page_descriptors) / sizeof(page_descriptors[0]);
        for (size_t i = 0; i < count; i++) if (strcmp(names[i], name) == 0) return true;
        // page IDL attributes do not enumerate its ordinary CSS properties.
        if (owner->type == CSS_RULE_FONT_FACE) return false;
    }
    CssPropertyCode code = css_property_code_from_name(name);
    return code > 0 && css_property_exists(code);
}

static bool cssom_decl_first(CssRule* rule, size_t index) {
    CssDeclaration* declaration = rule->data.style_rule.declarations[index];
    if (!declaration || !declaration->property_name ||
        !cssom_decl_supports(rule, declaration->property_name)) return false;
    for (size_t i = 0; i < index; i++) {
        if (cssom_decl_matches(rule->data.style_rule.declarations[i],
                declaration->property_name)) return false;
    }
    return true;
}

static const char* cssom_decl_name(const char* property, bool named, char* buffer, size_t size) {
    if (!property) return nullptr;
    if (property[0] == '-' && property[1] == '-') return property;
    // Reject an overflowing standard name instead of looking up a truncated key.
    if (named) return dom_style_camel_to_css_prop(property, buffer, size) ? buffer : nullptr;
    if (strlen(property) >= size) return nullptr;
    str_copy(buffer, size, property, strlen(property));
    str_lower_inplace(buffer, strlen(buffer));
    return buffer;
}

static bool cssom_decl_store(CssRule* rule, const char* name, CssDeclaration* replacement) {
    size_t count = rule->data.style_rule.declaration_count;
    CssDeclaration** declarations = rule->data.style_rule.declarations;
    bool found = false;
    size_t retained = 0;
    for (size_t i = 0; i < count; i++) {
        if (cssom_decl_matches(declarations[i], name)) {
            // A setter replaces every duplicate while retaining the first property's slot.
            if (!found && replacement) declarations[retained++] = replacement;
            found = true;
        } else declarations[retained++] = declarations[i];
    }
    if (!found && replacement) {
        CssDeclaration** grown = (CssDeclaration**)pool_calloc(rule->pool,
            (count + 1) * sizeof(CssDeclaration*));
        if (!grown) return false;
        if (count) memcpy(grown, declarations, count * sizeof(CssDeclaration*));
        grown[retained++] = replacement;
        rule->data.style_rule.declarations = grown;
    }
    rule->data.style_rule.declaration_count = retained;
    return found || replacement;
}

struct CssomDeclarationView {
    CssRule inline_rule = {};
    CssRule* rule = nullptr;
    DomElement* element = nullptr;
    Pool* scratch = nullptr;
    bool computed = false;

    explicit CssomDeclarationView(Item receiver) {
        computed = dom_is_computed_style_item(receiver);
        if (computed) return;
        if (!dom_is_inline_style_item(receiver)) {
            rule = unwrap_rule_decl(receiver);
            return;
        }
        element = (DomElement*)virtual_host_data(receiver);
        scratch = pool_create();
        if (!scratch) return;
        // authored inline declarations remain observable even when a stylesheet wins.
        const char* text = dom_element_get_inline_style(element);
        inline_rule.pool = scratch;
        inline_rule.type = CSS_RULE_STYLE;
        inline_rule.data.style_rule.declarations = css_parse_declaration_list_text(
            text ? text : "", text ? strlen(text) : 0, scratch,
            &inline_rule.data.style_rule.declaration_count);
        rule = &inline_rule;
    }
    ~CssomDeclarationView() { if (scratch) pool_destroy(scratch); }
};

static String* cssom_decl_text(CssRule* rule) {
    StringBuf* buffer = stringbuf_new(rule->pool);
    if (!buffer) return nullptr;
    size_t count = 0;
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        if (!cssom_decl_first(rule, i)) continue;
        if (count++) stringbuf_append_str(buffer, " ");
        append_rule_declaration_text(buffer, cssom_decl_find(rule,
            rule->data.style_rule.declarations[i]->property_name), rule->pool);
        stringbuf_append_str(buffer, ";");
    }
    return stringbuf_to_string(buffer);
}

static bool cssom_computed_name(const CssProperty* property, void* data) {
    return property->shorthand ||
        ((lam::ArrayList<StrView>*)data)->append(strview_from_cstr(property->name));
}

static int cssom_compare_names(const void* left, const void* right) {
    const StrView* a = (const StrView*)left;
    const StrView* b = (const StrView*)right;
    return str_cmp(a->str, a->length, b->str, b->length);
}

struct CssomComputedNames {
    DomElement* element;
    Pool* scratch;
    lam::ArrayList<StrView>* names;
};

static bool cssom_computed_custom_name(CssomComputedNames* data, StrView name) {
    for (size_t i = 0; i < data->names->size(); i++)
        if (strview_eq(&(*data->names)[i], &name)) return true;
    // empty computed token lists are valid; only the guaranteed-invalid value is omitted.
    return !css_compute_element_custom_property(data->scratch, data->element, name.str, name.length) ||
        data->names->append(name);
}

static bool cssom_computed_registration(void* data, const CssPropertyRegistration* registration, size_t name_length) {
    return cssom_computed_custom_name((CssomComputedNames*)data, strview_init(registration->name, name_length));
}

static void cssom_computed_names(Item receiver, lam::ArrayList<StrView>* names) {
    DomComputedStyleHost* host = (DomComputedStyleHost*)virtual_host_data(receiver);
    DomElement* element = host ? (DomElement*)host->elem : nullptr;
    if (!element || !dom_element_is_connected(element)) return;
    css_property_foreach(cssom_computed_name, names);
    if (names->size() > 1)
        qsort(names->data(), names->size(), sizeof(StrView), cssom_compare_names);
    Pool* scratch = pool_create();
    if (!scratch) return;
    CssomComputedNames data = {element, scratch, names};
    dom_ensure_computed(element, false);
    for (DomElement* ancestor = element; ancestor; ancestor = dom_parent_element(ancestor))
        for (CssCustomProp* variable = ancestor->css_variables; variable; variable = variable->next)
            cssom_computed_custom_name(&data, strview_from_cstr(variable->name));
    css_visit_document_property_registrations(element->doc, cssom_computed_registration, &data);
    pool_destroy(scratch);
}

extern "C" Item dom_cssom_rule_decl_get_property(Item decl_item, Item prop_name) {
    CssomDeclarationView view(decl_item);
    CssRule* rule = view.rule;
    const char* property = fn_to_cstr(prop_name);
    if (property && (view.computed || view.element)) {
        if (strcmp(property, "parentRule") == 0) return ItemNull;
        if (view.computed) {
            if (strcmp(property, "cssText") == 0) return make_string_item("");
            if (strcmp(property, "length") == 0) {
                lam::ArrayList<StrView> names;
                cssom_computed_names(decl_item, &names);
                return (Item){.item = i2it((int64_t)names.size())};
            }
            return dom_computed_style_get_property(decl_item, prop_name);
        }
    }
    if (!rule || !property) return make_string_item("");
    Pool* pool = rule->pool;
    if (!pool) return make_string_item("");
    if (strcmp(property, "parentRule") == 0) {
        CssRule* parent = cssom_declaration_owner(rule);
        return dom_cssom_wrap_rule(parent, parent->pool);
    }
    if (strcmp(property, "length") == 0 || strcmp(property, "cssText") == 0) {
        bool text = strcmp(property, "cssText") == 0;
        if (text) {
            String* result = cssom_decl_text(rule);
            return make_string_item(result ? result->chars : "");
        }
        size_t count = 0;
        for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
            if (!cssom_decl_first(rule, i)) continue;
            count++;
        }
        return (Item){.item = i2it((int64_t)count)};
    }
    char name_buffer[128];
    const char* name = cssom_decl_name(property, true, name_buffer, sizeof(name_buffer));
    CssDeclaration* declaration = cssom_decl_find(rule, name);
    return make_string_item(declaration ? css_serialize_declaration_value(declaration, pool) : "");
}

static Item cssom_decl_method_get(Item receiver, Item property, bool priority) {
    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver), property_root(roots, property);
    JS_ASSIGN_OR_RETURN(string, js_to_string(property_root.get()));
    property_root.set(string);
    CssomDeclarationView view(receiver_root.get());
    CssRule* rule = view.rule;
    char name_buffer[128];
    const char* name = cssom_decl_name(fn_to_cstr(property_root.get()), false, name_buffer, sizeof(name_buffer));
    if (view.computed) {
        if (priority || !cssom_decl_supports(nullptr, name)) return make_string_item("");
        // custom names are full DOMStrings; rebuilding from C text truncates embedded NUL.
        if (!(name[0] == '-' && name[1] == '-')) property_root.set(make_string_item(name));
        return dom_computed_style_get_property(receiver_root.get(), property_root.get());
    }
    if (!rule) return make_string_item("");
    CssDeclaration* declaration = cssom_decl_supports(rule, name) ? cssom_decl_find(rule, name) : nullptr;
    const char* value = priority ? (declaration && declaration->important ? "important" : "")
        : declaration ? css_serialize_declaration_value(declaration, rule->pool) : "";
    return make_string_item(value);
}

extern "C" Item dom_cssom_rule_decl_get_value(Item receiver, Item property) {
    return cssom_decl_method_get(receiver, property, false);
}

extern "C" Item dom_cssom_rule_decl_get_priority(Item receiver, Item property) {
    return cssom_decl_method_get(receiver, property, true);
}

static void cssom_decl_notify_mutation(CssRule* rule) {
    CssRule* owner = cssom_declaration_owner(rule);
    // descriptor shadows must commit back to the rule consumed by layout/font loading.
    if (owner->type == CSS_RULE_PAGE && owner->page) {
        owner->page->declarations = rule->data.style_rule.declarations;
        owner->page->declaration_count = rule->data.style_rule.declaration_count;
    } else if (owner->type == CSS_RULE_FONT_FACE) {
        StringBuf* buffer = stringbuf_new(rule->pool);
        stringbuf_append_str(buffer, "{ ");
        append_rule_declarations(buffer, rule->data.style_rule.declarations,
            rule->data.style_rule.declaration_count, rule->pool);
        stringbuf_append_str(buffer, " }");
        String* content = stringbuf_to_string(buffer);
        if (content) owner->data.generic_rule.content = content->chars;
    }
    js_cssom_notify_stylesheet_mutation(owner->stylesheet);
}


static void cssom_decl_commit(CssomDeclarationView* view, const char* name) {
    if (!view->element) {
        cssom_decl_notify_mutation(view->rule);
        return;
    }
    String* text = cssom_decl_text(view->rule);
    if (!text) return;
    CssPropertyCode property = name ? css_property_code_from_name(name) : CSS_PROPERTY_UNKNOWN;
    css_transition_capture_before_change(view->element, property);
    if (view->element->set_attribute("style", text->chars))
        dom_notify_mutation(dom_style_mutation_kind(property), view->element, view->element->parent);
}

static Item cssom_decl_set(Item receiver, Item property, Item value, Item priority, bool named) {
    // D5.3.3: conversions can allocate or call author code before native storage is unwrapped.
    RootFrame roots(4);
    Rooted<Item> receiver_root(roots, receiver), property_root(roots, property),
        value_root(roots, value), priority_root(roots, priority);
    JS_ASSIGN_OR_RETURN(property_string, js_to_string(property_root.get()));
    property_root.set(property_string);
    JS_ASSIGN_OR_RETURN(value_string, js_to_string(value_root.get().item == ITEM_NULL
        ? make_string_item("") : value_root.get()));
    value_root.set(value_string);
    JS_ASSIGN_OR_RETURN(priority_string, js_to_string(priority_root.get().item == ITEM_NULL ||
        priority_root.get().item == ITEM_JS_UNDEFINED ? make_string_item("") : priority_root.get()));
    priority_root.set(priority_string);
    CssomDeclarationView view(receiver_root.get());
    if (view.computed)
        return dom_raise_exception("NoModificationAllowedError", "Computed declarations are readonly");
    CssRule* rule = view.rule;
    if (!rule || !rule->pool) return make_js_undefined();
    String* source = it2s(value_root.get());
    const char* property_text = fn_to_cstr(property_root.get());
    if (named && strcmp(property_text, "cssText") == 0) {
        size_t count = 0;
        CssDeclaration** parsed = css_parse_declaration_list_text(source->chars,
            source->len, rule->pool, &count);
        rule->data.style_rule.declarations = parsed;
        rule->data.style_rule.declaration_count = count;
        cssom_decl_commit(&view, nullptr);
        return make_js_undefined();
    }
    char name_buffer[128];
    const char* name = cssom_decl_name(property_text, named, name_buffer, sizeof(name_buffer));
    if (!cssom_decl_supports(rule, name)) return make_js_undefined();
    // CSSOM checks empty values before priority, including an invalid priority string.
    if (!source->len) {
        if (cssom_decl_store(rule, name, nullptr)) cssom_decl_commit(&view, name);
        return make_js_undefined();
    }
    const char* requested_priority = fn_to_cstr(priority_root.get());
    bool important = requested_priority[0] && str_icmp_cstr(requested_priority, "important") == 0;
    if (requested_priority[0] && !important) return make_js_undefined();
    CssDeclaration* declaration = css_parse_property_value_declaration(name, strlen(name),
        source->chars, source->len, rule->pool);
    if (!declaration) return make_js_undefined();
    if (strcmp(name, "unicode-range") == 0) {
        const char* canonical = css_parse_unicode_range_canonical(source->chars, source->len, rule->pool);
        if (!canonical) return make_js_undefined();
        declaration->value = nullptr;
        declaration->value_text = canonical;
        declaration->value_text_len = strlen(canonical);
    }
    declaration->important = important;
    if (cssom_decl_store(rule, name, declaration)) cssom_decl_commit(&view, name);
    return make_js_undefined();
}

extern "C" Item dom_cssom_rule_decl_set_value(Item receiver, Item property, Item value, Item priority) {
    return cssom_decl_set(receiver, property, value, priority, false);
}

extern "C" Item dom_cssom_rule_decl_set_property(Item receiver, Item property, Item value) {
    RootFrame roots(1);
    Rooted<Item> value_root(roots, value);
    Item result = cssom_decl_set(receiver, property, value_root.get(), ItemNull, true);
    return item_is_error(result) ? result : value_root.get();
}

extern "C" Item dom_cssom_rule_decl_remove_property(Item receiver, Item property) {
    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver), property_root(roots, property);
    JS_ASSIGN_OR_RETURN(string, js_to_string(property_root.get()));
    property_root.set(string);
    CssomDeclarationView view(receiver_root.get());
    if (view.computed)
        return dom_raise_exception("NoModificationAllowedError", "Computed declarations are readonly");
    CssRule* rule = view.rule;
    if (!rule) return make_string_item("");
    char name_buffer[128];
    const char* name = cssom_decl_name(fn_to_cstr(property_root.get()), false, name_buffer, sizeof(name_buffer));
    CssDeclaration* declaration = cssom_decl_supports(rule, name) ? cssom_decl_find(rule, name) : nullptr;
    const char* old_value = declaration ? css_serialize_declaration_value(declaration, rule->pool) : "";
    if (declaration && cssom_decl_store(rule, name, nullptr)) cssom_decl_commit(&view, name);
    return make_string_item(old_value);
}

extern "C" Item dom_cssom_rule_decl_item(Item receiver, Item index_item) {
    RootFrame roots(2);
    Rooted<Item> receiver_root(roots, receiver), index_root(roots, index_item);
    JS_ASSIGN_OR_RETURN(number, js_to_number(index_root.get()));
    index_root.set(number);
    uint32_t index = (uint32_t)js_to_int32(it2d(number));
    CssomDeclarationView view(receiver_root.get());
    if (view.computed) {
        lam::ArrayList<StrView> names;
        cssom_computed_names(receiver_root.get(), &names);
        if (index >= names.size()) return make_string_item("");
        StrView name = names[index];
        return (Item){.item = s2it(heap_strcpy(name.str, name.length))};
    }
    CssRule* rule = view.rule;
    if (!rule) return make_string_item("");
    for (size_t i = 0; i < rule->data.style_rule.declaration_count; i++) {
        if (cssom_decl_first(rule, i) && index-- == 0)
            return make_string_item(rule->data.style_rule.declarations[i]->property_name);
    }
    return make_string_item("");
}

extern "C" Item dom_cssom_decl_css_has(Item receiver, Item property) {
    CssomDeclarationView view(receiver);
    char name_buffer[128];
    const char* name = cssom_decl_name(fn_to_cstr(property), true, name_buffer, sizeof(name_buffer));
    // native named adapters share the same context-specific supported-name list.
    return (Item){.item = b2it((view.rule || view.computed) && cssom_decl_supports(view.rule, name))};
}


// =============================================================================
// document.styleSheets
// =============================================================================

static Item stylesheets_varray_for(DomDocument* doc, Item owner) {
    if (!doc) return ItemNull;
    RootFrame roots(1);
    Rooted<Item> owner_root(roots, owner);
    if (get_type_id(owner_root.get()) != LMD_TYPE_VELMT) {
        owner_root.set(dom_document_proxy_for_doc_bridge(doc));
    }
    return cssom_varray_new(owner_root.get(), CSSOM_VARRAY_STYLE_SHEETS,
        radiant_dom_style_sheet_list_host_type());
}

// JS's door: the ambient document, which is realm state.
extern "C" Item dom_cssom_get_document_stylesheets(void) {
    DomDocument* doc = (DomDocument*)dom_get_document();
    return stylesheets_varray_for(doc, ItemNull);
}

// Lambda's door: the script names its document (ESO113 pattern, ESO114).
extern "C" Item dom_cssom_get_document_stylesheets_for(Item doc_item) {
    return stylesheets_varray_for(
        (DomDocument*)dom_document_from_item(doc_item), doc_item);
}

// Item-uniform twin of stylesheet_index for the Lambda face; the native
// (Item, int64_t) door stays for the module.
extern "C" Item dom_cssom_stylesheet_rule_at(Item sheet_item, Item index_item) {
    if (get_type_id(index_item) != LMD_TYPE_INT) return ItemNull;
    return dom_cssom_stylesheet_index(sheet_item, it2i(index_item));
}

// =============================================================================
// HTMLStyleElement .sheet
// =============================================================================

static CssStylesheet* js_cssom_parse_inline_stylesheet(DomElement* elem) {
    if (!elem || !elem->doc || !elem->doc->document_pool) return nullptr;

    Pool* pool = elem->doc->document_pool;
    StrBuf* css_text = strbuf_new_cap(32);
    if (!css_text) return nullptr;
    for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
        if (!child->is_text()) continue;
        DomText* text = child->as_text();
        if (text && text->text && text->length > 0) {
            strbuf_append_str_n(css_text, text->text, text->length);
        }
    }

    // CSSOM exposes the sheet immediately after a style node is connected;
    // the post-script stylesheet rescan happens too late for libraries that
    // insert rules during the same script turn.
    CssEngine* engine = css_engine_create(pool);
    CssStylesheet* sheet = engine
        ? css_parse_stylesheet(engine, css_text->str ? css_text->str : "", "<inline-style>")
        : nullptr;
    if (engine) css_engine_destroy(engine);
    strbuf_free(css_text);
    if (!sheet) return nullptr;

    sheet->owner_element = elem;
    return sheet;
}

static bool dom_cssom_node_precedes(DomNode* first, DomNode* second) {
    if (!first || !second || first == second) return false;
    int first_depth = 0;
    int second_depth = 0;
    for (DomNode* node = first; node; node = node->parent) first_depth++;
    for (DomNode* node = second; node; node = node->parent) second_depth++;
    while (first_depth > second_depth) {
        first = first->parent;
        first_depth--;
    }
    if (first == second) return false;
    while (second_depth > first_depth) {
        second = second->parent;
        second_depth--;
    }
    if (first == second) return true;
    while (first && second && first->parent != second->parent) {
        first = first->parent;
        second = second->parent;
    }
    if (!first || !second || first->parent != second->parent) return false;
    for (DomNode* node = first; node; node = node->next_sibling) {
        if (node == second) return true;
    }
    return false;
}

static void dom_cssom_reorder_owned_document_stylesheets(DomDocument* doc) {
    if (!doc || !doc->stylesheets) return;
    for (int i = 1; i < doc->stylesheet_count; i++) {
        for (int j = i; j > 0; j--) {
            CssStylesheet* previous = doc->stylesheets[j - 1];
            CssStylesheet* current = doc->stylesheets[j];
            if (!previous || !current || !previous->owner_element ||
                !current->owner_element ||
                !dom_cssom_node_precedes(current->owner_element,
                                         previous->owner_element)) {
                break;
            }
            doc->stylesheets[j - 1] = current;
            doc->stylesheets[j] = previous;
        }
    }
}

static bool dom_cssom_append_document_stylesheet(DomDocument* doc,
                                                  CssStylesheet* sheet) {
    if (!doc || !doc->document_pool || !sheet) return false;
    if (doc->stylesheet_count >= doc->stylesheet_capacity) {
        // Loader-owned sheet arrays can outlive a document but are not owned by
        // its pool, so append by copying instead of reallocating foreign storage.
        if (!lam::pool_copy_grow_array(doc->document_pool, &doc->stylesheets,
                                       &doc->stylesheet_capacity, doc->stylesheet_count,
                                       doc->stylesheet_count + 1, 4, false)) return false;
    }
    doc->stylesheets[doc->stylesheet_count++] = sheet;
    dom_cssom_reorder_owned_document_stylesheets(doc);
    return true;
}

static bool dom_cssom_remove_inline_stylesheet(DomDocument* doc,
                                                DomElement* elem) {
    if (!doc || !elem) return false;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* sheet = doc->stylesheets[i];
        if (!sheet || sheet->owner_element != elem) continue;
        memmove(&doc->stylesheets[i], &doc->stylesheets[i + 1],
                (size_t)(doc->stylesheet_count - i - 1) * sizeof(CssStylesheet*));
        doc->stylesheet_count--;
        // A detached style must stop contributing rules and keyframes immediately.
        doc->services.keyframe_registry = nullptr;
        doc->font_faces_processed = false;
        return true;
    }
    return false;
}

extern "C" bool dom_cssom_sync_inline_style_element(void* dom_elem) {
    DomElement* elem = (DomElement*)dom_elem;
    if (!elem || !elem->doc || !elem->tag_name ||
        str_icmp_cstr(elem->tag_name, "style") != 0) {
        return false;
    }

    DomDocument* doc = elem->doc;
    bool connected = false;
    for (DomNode* node = static_cast<DomNode*>(elem); node; node = node->parent) {
        if (node == static_cast<DomNode*>(doc->root)) {
            connected = true;
            break;
        }
    }
    if (!connected) return dom_cssom_remove_inline_stylesheet(doc, elem);

    CssStylesheet* sheet = js_cssom_parse_inline_stylesheet(elem);
    if (!sheet) return false;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        if (doc->stylesheets[i] && doc->stylesheets[i]->owner_element == elem) {
            doc->stylesheets[i] = sheet;
            dom_cssom_reorder_owned_document_stylesheets(doc);
            // Keyframes are parsed from stylesheet text, so their cache tracks
            // the same text-tree update as the style sheet.
            doc->services.keyframe_registry = nullptr;
            doc->font_faces_processed = false;
            return true;
        }
    }

    if (!dom_cssom_append_document_stylesheet(doc, sheet)) {
        return false;
    }
    doc->services.keyframe_registry = nullptr;
    doc->font_faces_processed = false;
    return true;
}

extern "C" void dom_cssom_sync_mutated_inline_stylesheets(void* dom_doc) {
    DomDocument* doc = (DomDocument*)dom_doc;
    if (!doc) return;
    for (int i = 0; i < doc->js.inline_stylesheet_mutation_count; i++) {
        dom_cssom_sync_inline_style_element(doc->js.inline_stylesheet_mutations[i]);
    }
    // CSS Cascade source order follows the current document tree, including
    // an inserted style that precedes a previously parsed link or style sheet.
    dom_cssom_reorder_owned_document_stylesheets(doc);
}

extern "C" Item dom_cssom_get_element_sheet(Item elem_item) {
    DomElement* elem = (DomElement*)dom_unwrap_element(elem_item);
    if (!elem) return ItemNull;

    bool is_inline_style = elem->tag_name && str_icmp_cstr(elem->tag_name, "style") == 0;
    bool is_stylesheet_link = elem->tag_name && str_icmp_cstr(elem->tag_name, "link") == 0;
    if (!is_inline_style && !is_stylesheet_link) {
        return ItemNull;
    }

    DomDocument* doc = elem->doc;
    if (!doc) return ItemNull;

    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* sheet = doc->stylesheets[i];
        if (sheet && sheet->owner_element == elem) {
            return dom_cssom_wrap_stylesheet(sheet);
        }
    }

    // Linked sheets are parsed before script execution; only a <style> can
    // need a lazy synchronization after its text-tree mutation.
    if (!is_inline_style || !dom_cssom_sync_inline_style_element(elem)) return ItemNull;
    for (int i = 0; i < doc->stylesheet_count; i++) {
        CssStylesheet* sheet = doc->stylesheets[i];
        if (sheet && sheet->owner_element == elem) {
            return dom_cssom_wrap_stylesheet(sheet);
        }
    }
    return ItemNull;
}

// =============================================================================
// CSS Namespace Object (CSS.supports, CSS.escape)
// =============================================================================
JS_FORWARD_RETURN(bool, dom_is_css_namespace, (Item item), js_object_has_class, (item, JS_CLASS_CSS_NAMESPACE))

/**
 * CSS.supports(property, value) — two-argument form.
 * Returns true if the property is known and the value parses successfully.
 *
 * CSS.supports(conditionText) — single-argument form.
 * Parses "(property: value)" condition text.
 */
static Item js_css_supports(Item* args, int argc) {
    if (argc < 1) return (Item){.item = b2it(false)};

    Pool* pool = get_document_pool();
    if (!pool) pool = mem_pool_create(NULL, MEM_ROLE_CSS, "js.cssom");
    bool free_pool = (pool != get_document_pool());

    // ensure CSS property system is initialized so property lookups work
    css_property_system_init(pool);

    bool result = false;

    if (argc >= 2) {
        // two-argument form: CSS.supports(property, value)
        String* prop_s = it2s(args[0]);
        String* val_s = it2s(args[1]);
        if (!prop_s || !val_s) {
            if (free_pool) mem_pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        // check if property is known (custom properties always pass). Keep the
        // complete JS string; CSS parsing is length-aware and must not truncate.
        size_t prop_len = prop_s->len;
        char* prop_buf = pool_dup_n(pool, prop_s->chars, prop_len);
        if (!prop_buf) {
            if (free_pool) mem_pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        bool is_custom = (prop_len >= 2 && prop_buf[0] == '-' && prop_buf[1] == '-');
        if (!is_custom) {
            CssPropertyCode pid = css_property_code_from_name(prop_buf);
            if (pid == CSS_PROPERTY_UNKNOWN || pid == 0) {
                if (free_pool) mem_pool_destroy(pool);
                return (Item){.item = b2it(false)};
            }
        }

        CssDeclaration* decl = css_parse_property_declaration(
            prop_buf, prop_len, val_s->chars, val_s->len, pool);
        result = decl != NULL;
    } else {
        // single-argument form: CSS.supports("(property: value)")
        // or CSS.supports("property: value")
        String* cond_s = it2s(args[0]);
        if (!cond_s) {
            if (free_pool) mem_pool_destroy(pool);
            return (Item){.item = b2it(false)};
        }

        const char* text = cond_s->chars;
        size_t len = cond_s->len;

        // strip outer parens if present: "(property: value)" → "property: value"
        if (len >= 2 && text[0] == '(') {
            // find matching closing paren
            if (text[len - 1] == ')') {
                text++;
                len -= 2;
            }
        }

        // skip leading whitespace
        while (len > 0 && (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')) {
            text++;
            len--;
        }

        CssDeclaration* decl = css_parse_declaration_text(text, len, pool);
        result = css_declaration_is_supported(decl);
    }

    if (free_pool) mem_pool_destroy(pool);
    return (Item){.item = b2it(result)};
}
JS_FORWARD_ITEM(dom_css_supports_operation, (Item* args, int argc), js_css_supports, (args, argc))

extern "C" Item dom_css_register_property_operation(Item definition) {
    if (!js_is_object_value(definition)) return js_throw_type_error("CSS.registerProperty requires a property definition");
    RootFrame roots(5);
    Rooted<Item> definition_root(roots, definition), value_root(roots, ItemNull);
    Rooted<Item> name_root(roots, ItemNull), syntax_root(roots, ItemNull), initial_root(roots, ItemNull);
    // Web IDL dictionary members convert in name order; accessors may allocate or reenter registration (D5.3.3).
    JS_ASSIGN_OR_RETURN(inherits_value, js_get_name_key(definition_root.get(), "inherits"));
    value_root.set(inherits_value);
    if (get_type_id(value_root.get()) == LMD_TYPE_UNDEFINED)
        return js_throw_type_error("CSS.registerProperty requires inherits");
    bool inherits = it2b(js_to_boolean(value_root.get()));
    JS_ASSIGN_OR_RETURN(initial_value, js_get_name_key(definition_root.get(), "initialValue"));
    value_root.set(initial_value);
    bool has_initial = get_type_id(value_root.get()) != LMD_TYPE_UNDEFINED;
    if (has_initial) {
        JS_ASSIGN_OR_RETURN(initial_text, js_to_string(value_root.get()));
        initial_root.set(initial_text);
    }
    JS_ASSIGN_OR_RETURN(name_value, js_get_name_key(definition_root.get(), "name"));
    value_root.set(name_value);
    if (get_type_id(value_root.get()) == LMD_TYPE_UNDEFINED)
        return js_throw_type_error("CSS.registerProperty requires name");
    JS_ASSIGN_OR_RETURN(name_text, js_to_string(value_root.get()));
    name_root.set(name_text);
    JS_ASSIGN_OR_RETURN(syntax_value, js_get_name_key(definition_root.get(), "syntax"));
    value_root.set(syntax_value);
    if (get_type_id(value_root.get()) == LMD_TYPE_UNDEFINED) syntax_root.set(js_name_item("*"));
    else {
        JS_ASSIGN_OR_RETURN(syntax_text, js_to_string(value_root.get()));
        syntax_root.set(syntax_text);
    }
    String* name = it2s(name_root.get());
    String* syntax = it2s(syntax_root.get());
    if (!name || name->len <= 2 || name->chars[0] != '-' || name->chars[1] != '-')
        return dom_raise_exception("SyntaxError", "Registered property names must start with -- and contain a name");
    DomDocument* doc = (DomDocument*)dom_get_document();
    if (!doc) return dom_raise_exception("InvalidStateError", "CSS.registerProperty requires an associated document");
    if (css_find_script_property_registration(doc, name->chars, name->len))
        return dom_raise_exception("InvalidModificationError", "The custom property is already registered by script");
    Pool* scratch = pool_create();
    if (!scratch) return ItemError;
    auto register_definition = [&]() -> Item {
        CssPropertyRegistration registration = {};
        if (!syntax || memchr(syntax->chars, '\0', syntax->len) ||
            !css_parse_property_syntax(syntax->chars, scratch, &registration))
            return dom_raise_exception("SyntaxError", "The registered property syntax is invalid");
        registration.name = name->chars;
        registration.inherits = inherits;
        if (has_initial) {
            String* initial = it2s(initial_root.get());
            if (!initial || !css_parse_property_initial_value(&registration, initial->chars, initial->len, scratch))
                return dom_raise_exception("SyntaxError", "The registered initial value is invalid");
        }
        if (!css_property_registration_is_valid(&registration))
            return dom_raise_exception("SyntaxError", "The initial value must match the syntax and be computationally independent");
        if (!css_register_document_property(doc, &registration, name->len)) return ItemError;
        style_epoch_mark_global_change(doc);
        if (doc->root) dom_notify_mutation(DOM_JS_MUTATION_UNKNOWN, doc->root, nullptr);
        return make_js_undefined();
    };
    Item result = register_definition();
    pool_destroy(scratch);
    return result;
}

extern "C" Item dom_css_escape_operation(Item* args, int argc) {
    if (argc < 1) return dom_raise_type_error("CSS.escape requires an argument");
    RootFrame roots(1);
    Rooted<Item> value_root(roots, args[0]);
    JS_ASSIGN_OR_RETURN(text, js_to_string(value_root.get()));
    value_root.set(text);
    String* ident = it2s(value_root.get());
    Pool* pool = get_document_pool();
    if (!pool && js_input) pool = js_input->pool;
    StringBuf* buffer = stringbuf_new(pool);
    if (!buffer) return ItemError;
    css_append_identifier(buffer, ident->chars, ident->len);
    String* result = stringbuf_to_string(buffer);
    return result ? make_string_item(result->chars) : ItemError;
}

// CSS namespace object is managed in js_runtime.cpp (needs access to builtin enum)
