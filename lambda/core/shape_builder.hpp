#pragma once

#include "../lambda.h"

struct Arena;

/**
 * ShapeBuilder - the field list an editor rebuild lays out
 *
 * MarkEditor describes a map's or element's new fields with it -- import the
 * old shape, then add, retype or remove -- and container_rebuild_with_new_shape
 * turns the list into transition-tree steps, or a chain the container owns
 * when the tree declines (D3.4.3v3). It makes no ShapeEntry itself.
 *
 * SCU10: drafts grow from the document. The builder embeds no field-count
 * limit; its draft arrays live in the caller's arena (Input lifetime, no
 * per-builder free), doubling as fields are added.
 *
 * USAGE:
 *   ShapeBuilder builder = shape_builder_init_map(arena);
 *   shape_builder_import_shape(&builder, type);
 *   shape_builder_add_field(&builder, "age", LMD_TYPE_INT);
 */
typedef struct ShapeFieldDraft {
    const char* name;   // must remain valid while the builder is in use
    TypeId type_id;
} ShapeFieldDraft;

typedef struct ShapeBuilder {
    struct Arena* arena;          // owns the draft array
    ShapeFieldDraft* fields;      // arena-owned drafts, capacity `capacity`
    size_t field_count;
    size_t capacity;

    // For elements
    bool is_element;
    const char* element_name;
} ShapeBuilder;

// SCU16/SCU10 size budget: no embedded document-sized tables.
#ifdef __cplusplus
static_assert(sizeof(ShapeBuilder) <= 64, "ShapeBuilder must stay a small handle over arena-owned drafts");
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ========== Initialization ==========

/**
 * Initialize builder for map shapes
 */
ShapeBuilder shape_builder_init_map(struct Arena* arena);

/**
 * Initialize builder for element shapes (attributes)
 */
ShapeBuilder shape_builder_init_element(struct Arena* arena, const char* element_name);

// ========== Field Management ==========

/**
 * Add field/attribute to builder
 *
 * @param builder Builder to add field to
 * @param name Field name (must remain valid until finalization)
 * @param type Field type ID
 * @return true on success, false on allocation failure
 */
bool shape_builder_add_field(ShapeBuilder* builder, const char* name, TypeId type);

/**
 * Remove field by name (for editing existing shapes)
 *
 * @param builder Builder to remove field from
 * @param name Field name to remove
 * @return true if found and removed, false if not found
 */
bool shape_builder_remove_field(ShapeBuilder* builder, const char* name);

/**
 * Check if builder has a field with given name
 */
bool shape_builder_has_field(ShapeBuilder* builder, const char* name);

// ========== Import/Export ==========

/**
 * Import existing shape into builder (for modification)
 * Clears current builder content and imports all fields of the type
 */
void shape_builder_import_shape(ShapeBuilder* builder, const TypeMap* shape);

// ========== Utilities ==========

/**
 * Get current field count
 */
size_t shape_builder_field_count(ShapeBuilder* builder);

#ifdef __cplusplus
}
#endif
