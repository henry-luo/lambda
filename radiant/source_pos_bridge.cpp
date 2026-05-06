// source_pos_bridge.cpp — C-side scaffolding for the editor source-position
// bridge (Phase R7, Radiant integration step 1). The pure-Lambda half lives
// in lambda/package/editor/mod_dom_bridge.ls.
//
// What this file provides:
//   * SourcePathC / SourcePosC value lifecycle (init/free/clone/equal).
//   * A side-table from `(Item source_item, const char* template_ref)` to
//     a heap-owned SourcePathC. Populated by `render_map_record_path` from
//     the apply() pipeline; consulted by `render_map_reverse_lookup_with_path`.
//     Kept separate from the main render_map so we don't widen the
//     RenderMapEntry struct or churn its hashing.
//   * `source_pos_from_dom_boundary`: walks DOM ancestors from a boundary
//     point, finds the first DomElement whose native_element is registered
//     in render_map, retrieves its recorded source path, and (for text
//     hits) converts the boundary's UTF-16 code-unit offset to a UTF-8
//     byte offset matching the editor's internal storage.
//   * `dom_boundary_from_source_pos` is left as a stub for now — turning a
//     SourcePos into a DomBoundary additionally needs an Item→DomElement
//     reverse map, which is the next sub-step.

// Bring in the full Item definition before including the bridge header,
// since Item is passed by value across the C ABI.
#include "../lambda/lambda-data.hpp"

#include "source_pos_bridge.hpp"

#include <stdlib.h>
#include <string.h>

#include "../lib/hashmap.h"
#include "../lambda/render_map.h"
#include "../lambda/input/css/dom_node.hpp"
#include "../lambda/input/css/dom_element.hpp"
#include "dom_range.hpp"

extern "C" {

// ---------------------------------------------------------------------------
// SourcePathC
// ---------------------------------------------------------------------------

void source_path_init(SourcePathC* p) {
    if (!p) return;
    p->indices = NULL;
    p->depth = 0;
}

void source_path_free(SourcePathC* p) {
    if (!p) return;
    free(p->indices);
    p->indices = NULL;
    p->depth = 0;
}

SourcePathC source_path_clone(const SourcePathC* p) {
    SourcePathC out = { NULL, 0 };
    if (!p || p->depth == 0 || !p->indices) return out;
    out.indices = (int*)malloc(sizeof(int) * (size_t)p->depth);
    if (!out.indices) return out;
    memcpy(out.indices, p->indices, sizeof(int) * (size_t)p->depth);
    out.depth = p->depth;
    return out;
}

bool source_path_equal(const SourcePathC* a, const SourcePathC* b) {
    if (!a || !b) return false;
    if (a->depth != b->depth) return false;
    if (a->depth == 0) return true;
    return memcmp(a->indices, b->indices, sizeof(int) * (size_t)a->depth) == 0;
}

// ---------------------------------------------------------------------------
// SourcePosC
// ---------------------------------------------------------------------------

void source_pos_free(SourcePosC* p) {
    if (!p) return;
    source_path_free(&p->path);
    p->offset = 0;
    p->kind = SOURCE_POS_TEXT;
}

// ---------------------------------------------------------------------------
// Path side-table: keyed on (source_item.item, template_ref) → SourcePathC.
// Heap-owned indices; freed when the entry is overwritten or the table is
// reset.
// ---------------------------------------------------------------------------

typedef struct PathTableEntry {
    uint64_t    source_item_bits;
    const char* template_ref;
    SourcePathC path;            // owned
} PathTableEntry;

static HashMap* s_path_table = NULL;

static uint64_t path_hash(const void* item, uint64_t s0, uint64_t s1) {
    const PathTableEntry* e = (const PathTableEntry*)item;
    uint64_t h1 = hashmap_murmur(&e->source_item_bits, sizeof(uint64_t), s0, s1);
    uint64_t h2 = hashmap_murmur(&e->template_ref, sizeof(void*), s0, s1);
    return h1 ^ (h2 * 0x9e3779b97f4a7c15ULL);
}

static int path_compare(const void* a, const void* b, void* /*udata*/) {
    const PathTableEntry* ea = (const PathTableEntry*)a;
    const PathTableEntry* eb = (const PathTableEntry*)b;
    if (ea->source_item_bits != eb->source_item_bits) {
        return ea->source_item_bits < eb->source_item_bits ? -1 : 1;
    }
    if (ea->template_ref != eb->template_ref) {
        return ea->template_ref < eb->template_ref ? -1 : 1;
    }
    return 0;
}

// Called by hashmap when an entry is replaced or removed: free the owned
// indices buffer so we don't leak.
static void path_entry_free(void* item) {
    PathTableEntry* e = (PathTableEntry*)item;
    if (e) source_path_free(&e->path);
}

static HashMap* ensure_path_table(void) {
    if (!s_path_table) {
        s_path_table = hashmap_new(
            sizeof(PathTableEntry), 64,
            0xBEEF1234u, 0x5678CAFEu,
            path_hash, path_compare,
            path_entry_free, NULL);
    }
    return s_path_table;
}

void source_pos_bridge_reset(void) {
    if (s_path_table) {
        // hashmap_free invokes path_entry_free on each entry.
        hashmap_free(s_path_table);
        s_path_table = NULL;
    }
}

void render_map_record_path(Item source_item, const char* template_ref,
                            const int* path_indices, int depth) {
    HashMap* m = ensure_path_table();
    PathTableEntry e;
    memset(&e, 0, sizeof(e));
    e.source_item_bits = source_item.item;
    e.template_ref = template_ref;
    if (depth > 0 && path_indices) {
        e.path.indices = (int*)malloc(sizeof(int) * (size_t)depth);
        if (e.path.indices) {
            memcpy(e.path.indices, path_indices, sizeof(int) * (size_t)depth);
            e.path.depth = depth;
        }
    }
    // hashmap_set returns the prior entry's storage; the registered
    // free-callback releases its `path.indices` automatically.
    hashmap_set(m, &e);
}

// Internal helper: lookup path by (source_item, template_ref). Returns a
// pointer into the table on hit; caller must NOT free.
static const SourcePathC* path_table_get(Item source_item,
                                         const char* template_ref) {
    if (!s_path_table) return NULL;
    PathTableEntry q;
    memset(&q, 0, sizeof(q));
    q.source_item_bits = source_item.item;
    q.template_ref = template_ref;
    const PathTableEntry* hit = (const PathTableEntry*)hashmap_get(s_path_table, &q);
    return hit ? &hit->path : NULL;
}

static bool is_generated_marker_node(DomNode* node) {
    return node && node->node_type == DOM_NODE_ELEMENT && node->view_type == RDT_VIEW_MARKER;
}

static int source_child_index(DomNode* node) {
    if (!node) return -1;
    int index = 0;
    for (DomNode* sib = node->prev_sibling; sib; sib = sib->prev_sibling) {
        if (!is_generated_marker_node(sib)) index++;
    }
    return index;
}

static DomNode* source_child_at(DomElement* de, int target_idx) {
    if (!de || target_idx < 0) return NULL;
    int idx = 0;
    for (DomNode* c = de->first_child; c; c = c->next_sibling) {
        if (is_generated_marker_node(c)) continue;
        if (idx == target_idx) return c;
        idx++;
    }
    return NULL;
}

bool render_map_reverse_lookup_with_path(Item result_node,
                                         RenderMapLookup* out_lookup,
                                         SourcePathC* out_path) {
    if (out_path) source_path_init(out_path);
    RenderMapLookup local;
    RenderMapLookup* lookup = out_lookup ? out_lookup : &local;
    if (!render_map_reverse_lookup(result_node, lookup)) return false;
    const SourcePathC* p = path_table_get(lookup->source_item, lookup->template_ref);
    if (out_path && p) *out_path = source_path_clone(p);
    // Reverse lookup hit even without a recorded path is still success:
    // the caller may want the (item, template_ref) for handler dispatch.
    return true;
}

// ---------------------------------------------------------------------------
// DOM → source position
// ---------------------------------------------------------------------------
// Walk up DOM ancestry from `boundary->node` until a DomElement is found
// whose native_element is registered in render_map. The recorded path
// becomes the SourcePos path; the offset is:
//   * for a text-node boundary: the UTF-8 byte offset within that text
//     leaf, with the path extended by the text node's child index in its
//     parent;
//   * for an element-node boundary: the boundary's offset (already a
//     child index) extended onto the path.

bool source_pos_from_dom_boundary(const DomBoundary* boundary,
                                  SourcePosC* out) {
    if (!out) return false;
    source_path_init(&out->path);
    out->offset = 0;
    out->kind = SOURCE_POS_TEXT;
    if (!boundary || !boundary->node) return false;

    DomNode* node = boundary->node;
    int leaf_child_index = -1;     // index of `node` within first registered ancestor
    bool is_text_leaf = (node->node_type == DOM_NODE_TEXT);
    uint32_t leaf_u8_offset = 0;

    if (is_text_leaf) {
        DomText* tn = (DomText*)node;
        leaf_u8_offset = dom_text_utf16_to_utf8(tn, boundary->offset);
        leaf_child_index = source_child_index(node);
        node = node->parent;
        if (!node) return false;
    }

    // Walk upward until we find a DomElement with native_element.
    Element* native = NULL;
    while (node) {
        if (node->node_type == DOM_NODE_ELEMENT) {
            DomElement* de = (DomElement*)node;
            if (de->native_element) { native = de->native_element; break; }
        }
        node = node->parent;
    }
    if (!native) return false;

    Item result_item;
    result_item.element = native;
    RenderMapLookup lookup;
    SourcePathC base;
    if (!render_map_reverse_lookup_with_path(result_item, &lookup, &base)) {
        return false;
    }
    // base is a clone of the recorded path; extend it.
    int extra = 0;
    int extra_indices[2];
    if (is_text_leaf) {
        if (leaf_child_index < 0) {
            source_path_free(&base);
            return false;
        }
        extra_indices[extra++] = leaf_child_index;
    }

    int total = base.depth + extra;
    int* combined = (int*)malloc(sizeof(int) * (size_t)(total > 0 ? total : 1));
    if (!combined) { source_path_free(&base); return false; }
    if (base.depth) memcpy(combined, base.indices, sizeof(int) * (size_t)base.depth);
    for (int i = 0; i < extra; i++) combined[base.depth + i] = extra_indices[i];
    source_path_free(&base);

    out->path.indices = combined;
    out->path.depth = total;
    if (is_text_leaf) {
        out->offset = leaf_u8_offset;
        out->kind = SOURCE_POS_TEXT;
    } else {
        out->offset = boundary->offset;  // child index — same in source tree
        out->kind = SOURCE_POS_ELEMENT;
    }
    return true;
}

bool source_pos_from_dom_range(const DomRange* range,
                               SourcePosC* out_start,
                               SourcePosC* out_end) {
    if (!range || !out_start || !out_end) return false;
    DomBoundary s = range->start;
    DomBoundary e = range->end;
    bool ok1 = source_pos_from_dom_boundary(&s, out_start);
    bool ok2 = source_pos_from_dom_boundary(&e, out_end);
    if (!ok1 || !ok2) {
        source_pos_free(out_start);
        source_pos_free(out_end);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Source position → DOM
// ---------------------------------------------------------------------------
// Walk the DOM subtree rooted at `dom_root`, looking for the DomElement
// whose recorded source path (stored at apply() time via
// render_map_record_path) matches the desired source path:
//   * SOURCE_POS_ELEMENT: recorded path == pos->path; boundary points at
//     that element with offset = pos->offset (a child index, identical
//     between source and DOM child lists by editor convention).
//   * SOURCE_POS_TEXT: recorded path == pos->path with the last index
//     dropped; the dropped index is the text node's position among that
//     element's children; offset is converted UTF-8 → UTF-16.

static bool path_equals_prefix(const SourcePathC* path, int prefix_len,
                               const SourcePathC* other) {
    if (!path || !other) return false;
    if (prefix_len < 0 || prefix_len > path->depth) return false;
    if (other->depth != prefix_len) return false;
    if (prefix_len == 0) return true;
    return memcmp(path->indices, other->indices,
                  sizeof(int) * (size_t)prefix_len) == 0;
}

static DomText* first_text_descendant(DomNode* node) {
    if (!node) return NULL;
    if (node->node_type == DOM_NODE_TEXT) return (DomText*)node;
    if (node->node_type != DOM_NODE_ELEMENT) return NULL;
    DomElement* el = (DomElement*)node;
    for (DomNode* c = el->first_child; c; c = c->next_sibling) {
        DomText* hit = first_text_descendant(c);
        if (hit) return hit;
    }
    return NULL;
}

// Resolve a SourcePos against a single DomElement candidate. Reads its
// recorded source path; on a match, fills `*out` and returns true.
static bool try_resolve_at_element(DomElement* de, const SourcePosC* pos,
                                   DomBoundary* out) {
    if (!de || !de->native_element) return false;
    Item result_item;
    result_item.element = de->native_element;
    RenderMapLookup lookup;
    SourcePathC recorded;
    if (!render_map_reverse_lookup_with_path(result_item, &lookup, &recorded)) {
        return false;
    }
    bool matched = false;
    if (pos->kind == SOURCE_POS_ELEMENT) {
        if (path_equals_prefix(&pos->path, pos->path.depth, &recorded)) {
            out->node = (DomNode*)de;
            out->offset = pos->offset;  // child index in source == DOM child index
            matched = true;
        }
    } else { // SOURCE_POS_TEXT
        if (source_path_equal(&pos->path, &recorded)) {
            DomText* tn = first_text_descendant((DomNode*)de);
            if (tn) {
                out->node = (DomNode*)tn;
                out->offset = dom_text_utf8_to_utf16(tn, pos->offset);
                matched = true;
            }
        } else if (pos->path.depth > 0 &&
            path_equals_prefix(&pos->path, pos->path.depth - 1, &recorded)) {
            // Locate the text child at the recorded index.
            int target_idx = pos->path.indices[pos->path.depth - 1];
            DomNode* child = source_child_at(de, target_idx);
            if (child) {
                DomText* tn = first_text_descendant(child);
                if (tn) {
                    out->node = (DomNode*)tn;
                    out->offset = dom_text_utf8_to_utf16(tn, pos->offset);
                    matched = true;
                }
            }
        }
    }
    source_path_free(&recorded);
    return matched;
}

// Recursive DFS through the DOM subtree.
static bool resolve_walk(DomNode* node, const SourcePosC* pos,
                         DomBoundary* out) {
    if (!node) return false;
    if (node->node_type == DOM_NODE_ELEMENT) {
        DomElement* de = (DomElement*)node;
        if (try_resolve_at_element(de, pos, out)) return true;
        for (DomNode* c = de->first_child; c; c = c->next_sibling) {
            if (resolve_walk(c, pos, out)) return true;
        }
    }
    return false;
}

bool dom_boundary_from_source_pos(DomNode* dom_root,
                                  const SourcePosC* pos,
                                  DomBoundary* out) {
    if (!dom_root || !pos || !out) return false;
    out->node = NULL;
    out->offset = 0;
    return resolve_walk(dom_root, pos, out);
}

} // extern "C"

// ---------------------------------------------------------------------------
// MarkBuilder helpers (C++ only). Build the Lambda `pos` and `selection`
// shapes used by lambda/package/editor/mod_source_pos.ls:
//   pos       = { path: [int, ...], offset: int }
//   selection = { kind: 'text', anchor: pos, head: pos }   (text)
//             | { kind: 'node', path: [int, ...] }         (node)
// ---------------------------------------------------------------------------

#include "../lambda/mark_builder.hpp"

static Item path_to_item(MarkBuilder& mb, const SourcePathC* p) {
    ArrayBuilder arr = mb.array();
    if (p && p->indices) {
        for (int i = 0; i < p->depth; i++) {
            arr.append((int64_t)p->indices[i]);
        }
    }
    return arr.final();
}

Item source_pos_to_item(MarkBuilder& mb, const SourcePosC* pos) {
    MapBuilder m = mb.map();
    m.put("path", path_to_item(mb, pos ? &pos->path : nullptr));
    m.put("offset", (int64_t)(pos ? pos->offset : 0));
    return m.final();
}

Item source_text_selection_to_item(MarkBuilder& mb,
                                   const SourcePosC* anchor,
                                   const SourcePosC* head) {
    MapBuilder m = mb.map();
    m.put("kind",   mb.createSymbolItem("text"));
    m.put("anchor", source_pos_to_item(mb, anchor));
    m.put("head",   source_pos_to_item(mb, head));
    return m.final();
}

Item source_node_selection_to_item(MarkBuilder& mb, const SourcePathC* path) {
    MapBuilder m = mb.map();
    m.put("kind", mb.createSymbolItem("node"));
    m.put("path", path_to_item(mb, path));
    return m.final();
}

// ---------------------------------------------------------------------------
// Phase R4 §7.4 — Source → DOM selection sync.
//
// Parse a Lambda `selection` Item and apply it to the given DomSelection by
// resolving each endpoint through `dom_boundary_from_source_pos`.
// ---------------------------------------------------------------------------

#include "../lambda/mark_reader.hpp"

namespace {

// Pull `{ path: [int...], offset: int }` out of an Item; treat it as a
// SOURCE_POS_TEXT position (the only kind editor selections produce).
// Caller must source_pos_free() the result.
bool source_pos_from_item(Item pos_item, SourcePosC* out) {
    if (!out) return false;
    out->kind = SOURCE_POS_TEXT;
    out->offset = 0;
    source_path_init(&out->path);

    MapReader m = MapReader::fromItem(pos_item);
    if (!m.isValid()) return false;

    ItemReader off = m.get("offset");
    if (off.isInt()) out->offset = (uint32_t)off.asInt();

    ItemReader path = m.get("path");
    if (!path.isArray()) return true;  // empty path — root
    ArrayReader arr = path.asArray();
    int depth = (int)arr.length();
    if (depth <= 0) return true;
    out->path.indices = (int*)malloc(sizeof(int) * (size_t)depth);
    if (!out->path.indices) return false;
    out->path.depth = depth;
    for (int i = 0; i < depth; i++) {
        ItemReader idx = arr.get(i);
        out->path.indices[i] = idx.isInt() ? (int)idx.asInt() : 0;
    }
    return true;
}

bool source_path_from_item(Item path_item, SourcePathC* out) {
    if (!out) return false;
    source_path_init(out);
    ArrayReader arr = ArrayReader::fromItem(path_item);
    if (!arr.isValid()) return false;
    int depth = (int)arr.length();
    if (depth <= 0) return true;
    out->indices = (int*)malloc(sizeof(int) * (size_t)depth);
    if (!out->indices) return false;
    out->depth = depth;
    for (int i = 0; i < depth; i++) {
        ItemReader idx = arr.get(i);
        out->indices[i] = idx.isInt() ? (int)idx.asInt() : 0;
    }
    return true;
}

// Resolve a SOURCE_POS_ELEMENT path to the DomElement it refers to, by
// walking the DOM (via dom_boundary_from_source_pos with offset 0 and
// kind=ELEMENT). Returns NULL on failure.
DomNode* dom_node_from_source_path(DomNode* dom_root, const SourcePathC* path) {
    if (!dom_root || !path) return NULL;
    SourcePosC p;
    p.path = *path;          // borrow indices — caller still owns
    p.offset = 0;
    p.kind = SOURCE_POS_ELEMENT;
    DomBoundary b = {0};
    if (!dom_boundary_from_source_pos(dom_root, &p, &b)) return NULL;
    return b.node;
}

} // namespace

extern "C" bool dom_selection_apply_source_selection(DomSelection* ds,
                                                     DomNode* dom_root,
                                                     Item selection) {
    if (!ds || !dom_root) return false;

    MapReader m = MapReader::fromItem(selection);
    if (!m.isValid()) return false;

    ItemReader kind = m.get("kind");
    if (!kind.isSymbol()) return false;
    const char* kind_str = kind.asSymbol() ? kind.asSymbol()->chars : NULL;
    if (!kind_str) return false;

    const char* exc = NULL;

    if (strcmp(kind_str, "text") == 0) {
        SourcePosC anchor_pos;
        SourcePosC head_pos;
        if (!source_pos_from_item(m.get("anchor").item(), &anchor_pos)) return false;
        if (!source_pos_from_item(m.get("head").item(),   &head_pos)) {
            source_pos_free(&anchor_pos);
            return false;
        }
        DomBoundary anchor_b = {0};
        DomBoundary head_b = {0};
        bool ok = dom_boundary_from_source_pos(dom_root, &anchor_pos, &anchor_b)
               && dom_boundary_from_source_pos(dom_root, &head_pos,   &head_b);
        source_pos_free(&anchor_pos);
        source_pos_free(&head_pos);
        if (!ok || !anchor_b.node || !head_b.node) return false;
        return dom_selection_set_base_and_extent(ds,
            anchor_b.node, anchor_b.offset,
            head_b.node,   head_b.offset, &exc);
    }

    if (strcmp(kind_str, "node") == 0) {
        SourcePathC path;
        if (!source_path_from_item(m.get("path").item(), &path)) return false;
        DomNode* node = dom_node_from_source_path(dom_root, &path);
        source_path_free(&path);
        if (!node) return false;
        return dom_selection_select_all_children(ds, node, &exc);
    }

    if (strcmp(kind_str, "all") == 0) {
        return dom_selection_select_all_children(ds, dom_root, &exc);
    }

    return false;
}
