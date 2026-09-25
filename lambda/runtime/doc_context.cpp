// The per-evaluation document context and node table. See doc_context.hpp for
// what it is for; this file is the storage.

#include "doc_context.hpp"

#include "../lambda-data.hpp"
#include "heap_api.h"
#include "../../lib/hashmap.h"
#include "../../lib/hashmap_helpers.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/memtrack.h"

#include <string.h>

// Keyed by canonical location spelling.
typedef struct DocLocationEntry {
    const char* location;
    size_t length;
    Document* doc;
} DocLocationEntry;

static uint64_t doc_location_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const DocLocationEntry* e = (const DocLocationEntry*)item;
    return hashmap_hash_bytes(e->location, e->length, seed0, seed1);
}
static int doc_location_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const DocLocationEntry* ea = (const DocLocationEntry*)a;
    const DocLocationEntry* eb = (const DocLocationEntry*)b;
    if (ea->length != eb->length) return ea->length < eb->length ? -1 : 1;
    return memcmp(ea->location, eb->location, ea->length);
}

static uint64_t doc_node_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    uintptr_t ptr = (uintptr_t)((const DocNodeEntry*)item)->container;
    return hashmap_hash_bytes(&ptr, sizeof(ptr), seed0, seed1);
}
static int doc_node_cmp(const void* a, const void* b, void* udata) {
    (void)udata;
    const void* pa = ((const DocNodeEntry*)a)->container;
    const void* pb = ((const DocNodeEntry*)b)->container;
    if (pa == pb) return 0;
    return pa < pb ? -1 : 1;
}

// One node table for every loaded document rather than one per document.
// Appendix A proposed an arena-range test to find a pointer's owning document;
// a single table answers the same question in one probe and keeps `&` O(depth)
// instead of O(documents).
static HashMap* g_doc_locations = NULL;
static HashMap* g_doc_nodes = NULL;

static void doc_context_ensure(void) {
    if (!g_doc_locations) {
        g_doc_locations = hashmap_new(sizeof(DocLocationEntry), 8, 0, 0,
            doc_location_hash, doc_location_cmp, NULL, NULL);
    }
    if (!g_doc_nodes) {
        g_doc_nodes = hashmap_new(sizeof(DocNodeEntry), 256, 0, 0,
            doc_node_hash, doc_node_cmp, NULL, NULL);
    }
}

Document* doc_context_lookup(const char* location, size_t length) {
    if (!g_doc_locations || !location) return NULL;
    DocLocationEntry probe = {location, length, NULL};
    const void* found = hashmap_get(g_doc_locations, &probe);
    return found ? ((const DocLocationEntry*)found)->doc : NULL;
}

void doc_context_register(Document* doc, const void* container,
        const void* parent, const char* key_name, size_t key_name_length,
        int64_t key_index, uint32_t generation) {
    if (!doc || !container) return;
    doc_context_ensure();
    DocNodeEntry entry = {container, parent, key_name, key_name_length,
        key_index, doc, generation};
    hashmap_set(g_doc_nodes, &entry);
}

const DocNodeEntry* doc_context_find_node(const void* container) {
    if (!g_doc_nodes || !container) return NULL;
    DocNodeEntry probe = {};
    probe.container = container;
    return (const DocNodeEntry*)hashmap_get(g_doc_nodes, &probe);
}

// PTH41: only CONTAINERS inside a document have identity; a scalar read yields
// an Item and an identity on it would have to be manufactured per read. So the
// walk registers container children and steps over scalar ones.
static const void* doc_container_pointer(Item item) {
    TypeId tid = get_type_id(item);
    switch (tid) {
    // A numeric array is a document node like any other sequence; its elements
    // are scalars, so the walk below registers the container and stops.
    case LMD_TYPE_ARRAY: case LMD_TYPE_ARRAY_NUM:
    case LMD_TYPE_MAP: case LMD_TYPE_ELEMENT:
        return (const void*)(uintptr_t)item.item;
    default:
        return NULL;
    }
}

// Guards against a document that is cyclic by construction and against runaway
// depth on a pathological input; a document tree is a tree, so hitting either
// only costs the identity of the nodes below the cut.
enum { DOC_REGISTER_MAX_DEPTH = 256 };

static void doc_register_child(Document* doc, Item child, const void* parent,
    const char* key_name, size_t key_name_length, int64_t key_index,
    uint32_t generation, int depth);

static void doc_register_members(Document* doc, Item item, const void* self,
        uint32_t generation, int depth) {
    TypeId tid = get_type_id(item);
    // Only the three container kinds a parsed document is built from are walked
    // (MarkBuilder produces plain Array/Map/Element). A virtual container is a
    // computed view, not a document node, and has no place in the node table.
    if (tid == LMD_TYPE_MAP || tid == LMD_TYPE_ELEMENT) {
        // Attributes — map fields and element attributes — carry NameKeys.
        TypeMap* shape = lambda_attr_shape(tid, (const void*)(uintptr_t)item.item);
        void* data = lambda_attr_data(tid, (const void*)(uintptr_t)item.item);
        if (shape && data) {
            FOR_EACH_MAP_FIELD(shape, field) {
                if (field->byte_offset < 0 || !field->name) continue;
                Item value = _map_read_field(field, data);
                doc_register_child(doc, value, self, field->name->str,
                    field->name->length, 0, generation, depth + 1);
            }
        }
    }
    // Sequence children — array items and element content — carry IntKeys.
    int64_t count = tid == LMD_TYPE_ARRAY ? item.array->length
        : tid == LMD_TYPE_ELEMENT ? (int64_t)item.element->length : 0;
    for (int64_t i = 0; i < count; i++) {
        Item child = tid == LMD_TYPE_ARRAY ? array_get(item.array, i)
                                           : item.element->items[i];
        doc_register_child(doc, child, self, NULL, 0, i, generation, depth + 1);
    }
}

static void doc_register_child(Document* doc, Item child, const void* parent,
        const char* key_name, size_t key_name_length, int64_t key_index,
        uint32_t generation, int depth) {
    if (depth > DOC_REGISTER_MAX_DEPTH) return;
    const void* self = doc_container_pointer(child);
    if (!self) return;
    // A container reached twice keeps the identity of its FIRST registration:
    // identity is the node's path within the document (PTH42), and a shared
    // sub-tree has to answer with one of them.
    if (doc_context_find_node(self)) return;
    doc_context_register(doc, self, parent, key_name, key_name_length,
        key_index, generation);
    doc_register_members(doc, child, self, generation, depth);
}

void doc_context_register_tree(Document* doc, Item root, const void* parent,
        const char* key_name, size_t key_name_length, int64_t key_index,
        uint32_t generation) {
    if (!doc) return;
    doc_register_child(doc, root, parent, key_name, key_name_length, key_index,
        generation, 0);
}

Document* doc_context_install(Path* path, const char* location, size_t length,
        Item head, uint32_t generation) {
    if (!location) return NULL;
    doc_context_ensure();
    Document* doc = doc_context_lookup(location, length);
    if (!doc) {
        doc = (Document*)mem_calloc(1, sizeof(Document), MEM_CAT_EVAL);
        if (!doc) return NULL;
        char* owned = (char*)mem_calloc(1, length + 1, MEM_CAT_EVAL);
        if (!owned) { mem_free(doc); return NULL; }
        memcpy(owned, location, length);
        doc->location = NULL;
        DocLocationEntry entry = {owned, length, doc};
        hashmap_set(g_doc_locations, &entry);
        // The head is the context's own reference to the document. A `temp.`
        // head is GC-heap data with no other owner, so without this root a
        // collection between two forces of one location would free it under
        // the table. Registered once per document, for the document's life.
        heap_register_gc_root(&doc->head.item);
    }
    doc->path = path;
    doc->head = head;
    doc->generation = generation;
    doc_context_register_tree(doc, head, NULL, NULL, 0, 0, generation);
    return doc;
}

// Retained versions live in chunks, each registered as one GC root range when
// it is allocated. A chunk never moves or grows, so retaining a version never
// re-registers a live root (a reallocated array would have to hand every slot
// over); each new chunk doubles the last, keeping the number of ranges
// logarithmic in the number of commits. Unfilled slots are zero, which the
// collector skips.
struct DocRetainChunk {
    DocRetainChunk* next;   // the older chunk
    uint32_t count;
    uint32_t capacity;
    Item slots[];
};

// Keep `value` alive for the rest of the evaluation.
static void doc_retain_version(Document* doc, Item value) {
    if (!doc || value.item == ItemNull.item) return;
    DocRetainChunk* chunk = doc->retained;
    if (!chunk || chunk->count == chunk->capacity) {
        uint32_t capacity = chunk ? chunk->capacity * 2 : 4;
        DocRetainChunk* fresh = (DocRetainChunk*)mem_calloc(1,
            sizeof(DocRetainChunk) + capacity * sizeof(Item), MEM_CAT_EVAL);
        if (!fresh) return;
        fresh->next = chunk;
        fresh->capacity = capacity;
        heap_register_gc_root_range(&fresh->slots[0].item, (int)capacity);
        doc->retained = chunk = fresh;
    }
    chunk->slots[chunk->count++] = value;
}

void doc_context_commit_head(Document* doc, Item head) {
    if (!doc) return;
    // The outgoing version's nodes are still in the table, and a binding or
    // cursor may still hold them.
    doc_retain_version(doc, doc->head);
    doc->head = head;
    doc->generation++;
    // Only the nodes the write set CREATED are new; `doc_register_child` stops
    // at a container that is already in the table, so a shared subtree keeps
    // its old stamp and its old identity (PTH43v2).
    doc_context_register_tree(doc, head, NULL, NULL, 0, 0, doc->generation);
}

void doc_context_reset(void) {
    if (g_doc_locations) {
        size_t i = 0;  void* item = NULL;
        while (hashmap_iter(g_doc_locations, &i, &item)) {
            DocLocationEntry* entry = (DocLocationEntry*)item;
            if (entry->doc) {
                heap_unregister_gc_root(&entry->doc->head.item);
                DocRetainChunk* chunk = entry->doc->retained;
                while (chunk) {
                    DocRetainChunk* older = chunk->next;
                    heap_unregister_gc_root_range(&chunk->slots[0].item);
                    mem_free(chunk);
                    chunk = older;
                }
            }
            mem_free((void*)entry->location);
            mem_free(entry->doc);
        }
        hashmap_free(g_doc_locations);
        g_doc_locations = NULL;
    }
    if (g_doc_nodes) {
        hashmap_free(g_doc_nodes);
        g_doc_nodes = NULL;
    }
}
