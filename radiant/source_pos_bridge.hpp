// source_pos_bridge.hpp — Radiant ↔ Lambda editor source-position bridge
//
// Phase R7 (Radiant integration step 1) of the rich-text editor work
// described in vibe/Radiant_Rich_Text_Editing.md. Pure-Lambda algorithms
// live in lambda/package/editor/mod_dom_bridge.ls; this header documents
// the C++ seam that converts (DomNode*, dom_offset) ↔ (source_path, offset)
// via render_map.
//
// Design contract
// ---------------
// * The editor's "doc tree" is a Lambda value built with mod_doc.ls
//   constructors: a node is `{kind:'node', tag, attrs, content}` and
//   a text leaf is `{kind:'text', text, marks}`.
// * Lambda map equality is structural and unreliable as identity, so the
//   bridge does NOT try to find subtrees by reference. Instead, the
//   render_map records the SOURCE PATH (a heap-owned int[] of child
//   indices) for every rendered subtree at apply() time. Reverse lookup
//   then returns a stable path.
// * UTF-16 ↔ UTF-8 conversion happens here, at the DOM-API boundary,
//   matching dom_text_utf16_to_utf8 / dom_text_utf8_to_utf16 in
//   dom_range.hpp.
//
// What is implemented in this header today: the data types and function
// declarations the renderer + event dispatch will call. The DOM-side
// glue (DomNode → recorded source path; SourcePos → DomNode) is wired
// in subsequent commits as render_map gains a path field. Until then,
// these functions are no-ops returning false.

#ifndef RADIANT_SOURCE_POS_BRIDGE_HPP
#define RADIANT_SOURCE_POS_BRIDGE_HPP

#include <stdint.h>
#include <stdbool.h>
#include "../lambda/lambda.h"

#ifdef __cplusplus
extern "C" {
#endif

struct DomNode;
struct DomBoundary;
struct DomRange;

// ---------------------------------------------------------------------------
// SourcePathC — a concrete C-side mirror of `mod_source_pos.pos.path`.
// `indices` is heap-owned; call source_path_free() to release.
// ---------------------------------------------------------------------------

typedef struct SourcePathC {
    int* indices;
    int  depth;        // 0 == doc root
} SourcePathC;

void source_path_init(SourcePathC* p);
void source_path_free(SourcePathC* p);
SourcePathC source_path_clone(const SourcePathC* p);
bool source_path_equal(const SourcePathC* a, const SourcePathC* b);

// ---------------------------------------------------------------------------
// SourcePosC — (path, offset) pair matching the Lambda `pos` constructor.
// `kind` is 'text' or 'element', mirroring mod_dom_bridge `hit_kind`.
// ---------------------------------------------------------------------------

typedef enum SourcePosKind {
    SOURCE_POS_TEXT    = 0,   // offset is UTF-8 byte offset within text
    SOURCE_POS_ELEMENT = 1,   // offset is child index within container
} SourcePosKind;

typedef struct SourcePosC {
    SourcePathC   path;
    uint32_t      offset;
    SourcePosKind kind;
} SourcePosC;

void source_pos_free(SourcePosC* p);

// ---------------------------------------------------------------------------
// DOM → source position
// ---------------------------------------------------------------------------
// Walk up DOM ancestry from `node` until a DomElement is found whose
// native_element is registered in render_map; convert UTF-16 → UTF-8 if
// the hit is a DomText. Writes into `*out` and returns true on success.
//
// Today: returns false unconditionally (path-recording in render_map is
// a follow-up commit). Caller MUST handle false.

bool source_pos_from_dom_boundary(const DomBoundary* boundary,
                                  SourcePosC* out);

// Convenience: convert both endpoints of a DomRange to source positions.
bool source_pos_from_dom_range(const DomRange* range,
                               SourcePosC* out_start,
                               SourcePosC* out_end);

// ---------------------------------------------------------------------------
// Source position → DOM
// ---------------------------------------------------------------------------
// Walk the recorded source path under `doc_root` and consult render_map
// to find the rendered DomNode; convert UTF-8 → UTF-16 for text hits.
// Writes into `*out` and returns true on success.

bool dom_boundary_from_source_pos(Item doc_root,
                                  const SourcePosC* pos,
                                  DomBoundary* out);

// ---------------------------------------------------------------------------
// Path recording (render_map extension)
// ---------------------------------------------------------------------------
// Called by the apply() pipeline after each template invocation to
// record where in the source tree the subtree lives. Stored alongside
// the existing (source_item, template_ref) → result_node entry so
// reverse lookup can return both source_item AND its path.

void render_map_record_path(Item source_item, const char* template_ref,
                            const int* path_indices, int depth);

// Reverse-lookup variant that also yields the recorded source path.
// `*out_path` (if non-null) is filled with a heap-owned copy the caller
// must release via source_path_free().
struct RenderMapLookup;
bool render_map_reverse_lookup_with_path(Item result_node,
                                         struct RenderMapLookup* out_lookup,
                                         SourcePathC* out_path);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_SOURCE_POS_BRIDGE_HPP
