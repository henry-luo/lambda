// source_pos_bridge.cpp — minimal C-side scaffolding for the editor
// source-position bridge. The pure-Lambda half lives in
// lambda/package/editor/mod_dom_bridge.ls; this file provides the
// C struct lifecycle helpers and stubs the DOM glue functions that
// require the upcoming render_map path-recording extension.

// Bring in the full Item definition before including the bridge header,
// since Item is passed by value across the C ABI.
#include "../lambda/lambda-data.hpp"

#include "source_pos_bridge.hpp"

#include <stdlib.h>
#include <string.h>

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
// DOM ↔ source-pos — stubbed pending render_map path-recording extension.
// Returning false here is intentional: callers MUST treat absence of a
// translation as a soft failure (e.g., fall back to dom_range coordinates)
// until the bridge is fully wired.
// ---------------------------------------------------------------------------

bool source_pos_from_dom_boundary(const DomBoundary* /*boundary*/,
                                  SourcePosC* out) {
    if (out) source_path_init(&out->path);
    return false;
}

bool source_pos_from_dom_range(const DomRange* /*range*/,
                               SourcePosC* /*out_start*/,
                               SourcePosC* /*out_end*/) {
    return false;
}

bool dom_boundary_from_source_pos(Item /*doc_root*/,
                                  const SourcePosC* /*pos*/,
                                  DomBoundary* /*out*/) {
    return false;
}

void render_map_record_path(Item /*source_item*/, const char* /*template_ref*/,
                            const int* /*path_indices*/, int /*depth*/) {
    // No-op until render_map is extended with a path field.
}

bool render_map_reverse_lookup_with_path(Item /*result_node*/,
                                         struct RenderMapLookup* /*out_lookup*/,
                                         SourcePathC* out_path) {
    if (out_path) source_path_init(out_path);
    return false;
}

} // extern "C"
