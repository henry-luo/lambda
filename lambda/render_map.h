// render_map.h — Observer-based reconciliation: source→result mapping
// Maintains a bidirectional mapping from (source_item, template_ref) to the
// result nodes produced by template invocation. Used for two-phase update:
//   Phase 1: Mark affected entries dirty after state/model mutation
//   Phase 2: Top-down re-transform of dirty entries only
#pragma once

#include "lambda.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Key for the render map: (source_item, template_ref)
typedef struct RenderMapKey {
    Item source_item;          // the model/source node passed to apply()
    const char* template_ref;  // template_ref from TemplateEntry (interned pointer)
} RenderMapKey;

// Entry in the render map
typedef struct RenderMapEntry {
    RenderMapKey key;
    Item result_node;          // the result node produced by this template invocation
    Item parent_result;        // parent node in the result tree (ItemNull if root)
    int child_index;           // position within parent's children (-1 if unknown)
    bool dirty;                // needs re-transformation
} RenderMapEntry;

// Initialize the global render map (call once at startup)
void render_map_init(void);

// Destroy the global render map
void render_map_destroy(void);

// Record a source→result mapping (called during apply())
void render_map_record(Item source_item, const char* template_ref,
                       Item result_node, Item parent_result, int child_index);

// Mark an entry dirty by key (called after state/model mutation)
void render_map_mark_dirty(Item source_item, const char* template_ref);

// Check if any entries are dirty
bool render_map_has_dirty(void);

// Get the result node for a (source_item, template_ref) pair.
// Returns ItemNull if not found.
Item render_map_get_result(Item source_item, const char* template_ref);

// Re-transform all dirty entries: re-execute template bodies and replace results.
// Returns the number of entries re-transformed.
int render_map_retransform(void);

// Result of a single retransformation (for incremental DOM rebuild)
typedef struct RetransformResult {
    Item parent_result;     // Lambda parent element containing the changed child
    Item new_result;        // new Lambda result element (after re-execution)
    Item old_result;        // old Lambda result element (before re-execution)
    int child_index;        // position within parent's children
    const char* template_ref;
} RetransformResult;

// Re-transform dirty entries and return details of what changed.
// Fills out_results (up to max_results), returns actual count.
// If more dirty entries than max_results, excess is still retransformed but not reported.
int render_map_retransform_with_results(RetransformResult* out_results, int max_results);

// Clear all entries
void render_map_reset(void);

// Get the underlying hashmap pointer (for RadiantState unification)
struct hashmap* render_map_get_map(void);

// Inject an external hashmap (e.g., from RadiantState). Caller owns the map.
void render_map_set_map(struct hashmap* map);

// ============================================================================
// Reverse lookup: result_node → (source_item, template_ref)
// Used by event dispatch to find which template produced a clicked element.
// ============================================================================

// Result of a reverse lookup
typedef struct RenderMapLookup {
    Item source_item;              // the model item that was matched
    const char* template_ref;      // template reference (interned pointer)
} RenderMapLookup;

// Reverse lookup: given a result_node Item, find the source_item and template_ref.
// Returns true if found, false otherwise. Writes to *out on success.
bool render_map_reverse_lookup(Item result_node, RenderMapLookup* out);

// Set the document root element so retransform can fix parent references.
// Call this after producing the top-level element tree in load_lambda_script_doc.
void render_map_set_doc_root(Item root);
Item render_map_get_doc_root(void);

#ifdef __cplusplus
}
#endif
