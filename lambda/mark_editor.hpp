#pragma once

#include "lambda-data.hpp"
#include "mark_builder.hpp"
#include "shape_builder.hpp"
#include "shape_pool.hpp"
#include "name_pool.hpp"
#include <stdarg.h>

// Forward declarations
class MarkEditor;

/**
 * Edit mode for operations
 */
enum EditMode {
    EDIT_MODE_INLINE,    // Modify in-place (mutable)
    EDIT_MODE_IMMUTABLE  // Copy-on-write (versioned)
};

/**
 * Version history entry (for immutable mode)
 */
typedef struct EditVersion {
    Item root;               // Document root at this version
    int version_number;      // Sequential version number
    const char* description; // Optional description
    struct EditVersion* prev; // Previous version
    struct EditVersion* next; // Next version (for redo)
} EditVersion;

/**
 * MarkEditor - Fluent API for editing Lambda document structures
 * 
 * MEMORY MODEL:
 * - Editor is stack-allocated (RAII)
 * - Operates on Input's arena/pool/name_pool/shape_pool
 * - Inline mode: modifies structures in-place
 * - Immutable mode: creates new versions, shares unchanged data
 * 
 * USAGE:
 *   MarkEditor editor(input, EDIT_MODE_IMMUTABLE);
 *   Item new_doc = editor.map_update(doc, "field", new_value);
 *   editor.commit("Updated field");  // Save version
 *   editor.undo();                    // Revert to previous
 */
class MarkEditor {
private:
    Input* input_;              // Input context
    Pool* pool_;                // Memory pool
    Arena* arena_;              // Arena allocator
    NamePool* name_pool_;       // String interning
    ShapePool* shape_pool_;     // Shape deduplication
    ArrayList* type_list_;      // Type registry
    MarkBuilder* builder_;      // For creating new structures
    
    EditMode mode_;             // Current edit mode
    EditVersion* current_version_; // Current version (immutable mode)
    EditVersion* version_head_;    // Head of version list
    int next_version_num_;         // Next version number

public:
    /**
     * Construct editor from Input
     */
    explicit MarkEditor(Input* input, EditMode mode = EDIT_MODE_INLINE);
    
    /**
     * Destructor
     */
    ~MarkEditor();
    
    // Non-copyable
    MarkEditor(const MarkEditor&) = delete;
    MarkEditor& operator=(const MarkEditor&) = delete;

    // ========================================================================
    // MAP OPERATIONS
    // ========================================================================
    
    /**
     * Update single field in map
     * 
     * Inline mode: Modifies map in-place, may reallocate data buffer
     * Immutable mode: Creates new Map sharing unchanged fields
     * 
     * @param map Original map Item
     * @param key Field name (string or String*)
     * @param value New value for field
     * @return Updated map Item (same as input in inline mode, new in immutable)
     */
    Item map_update(Item map, const char* key, Item value);
    Item map_update(Item map, String* key, Item value);
    
    /**
     * Update multiple fields in map (batch operation)
     * More efficient than multiple single updates (shapes rebuilt once)
     * 
     * @param map Original map Item
     * @param count Number of key-value pairs
     * @param ... Variadic args: key1, value1, key2, value2, ...
     * @return Updated map Item
     */
    Item map_update_batch(Item map, int count, ...);
    
    /**
     * Delete field from map
     * Rebuilds shape without the field, migrates data
     * 
     * @param map Original map Item
     * @param key Field name to delete
     * @return Updated map Item
     */
    Item map_delete(Item map, const char* key);
    Item map_delete(Item map, String* key);
    
    /**
     * Delete multiple fields (batch operation)
     * 
     * @param map Original map Item
     * @param count Number of keys
     * @param keys Array of key names
     * @return Updated map Item
     */
    Item map_delete_batch(Item map, int count, const char** keys);
    
    /**
     * Insert new field into map
     * Alias for map_update (works same way for new fields)
     */
    Item map_insert(Item map, const char* key, Item value) {
        return map_update(map, key, value);
    }
    
    /**
     * Rename field in map (preserves value, changes key)
     */
    Item map_rename(Item map, const char* old_key, const char* new_key);
    
    // ========================================================================
    // ELEMENT OPERATIONS
    // ========================================================================
    
    /**
     * Update single attribute in element
     * 
     * @param element Original element Item
     * @param attr_name Attribute name
     * @param value New attribute value
     * @return Updated element Item
     */
    Item elmt_update_attr(Item element, const char* attr_name, Item value);
    Item elmt_update_attr(Item element, String* attr_name, Item value);
    
    /**
     * Update multiple attributes (batch operation)
     * 
     * @param element Original element Item
     * @param count Number of attribute-value pairs
     * @param ... Variadic args: attr1, value1, attr2, value2, ...
     * @return Updated element Item
     */
    Item elmt_update_attr_batch(Item element, int count, ...);
    
    /**
     * Delete attribute from element
     * 
     * @param element Original element Item
     * @param attr_name Attribute name to delete
     * @return Updated element Item
     */
    Item elmt_delete_attr(Item element, const char* attr_name);
    Item elmt_delete_attr(Item element, String* attr_name);
    
    /**
     * Delete child at index
     * 
     * @param element Original element Item
     * @param index Child index to delete (0-based)
     * @return Updated element Item
     */
    Item elmt_delete_child(Item element, int index);
    
    /**
     * Delete children in range [start, end)
     * 
     * @param element Original element Item
     * @param start Start index (inclusive)
     * @param end End index (exclusive)
     * @return Updated element Item
     */
    Item elmt_delete_children(Item element, int start, int end);
    
    /**
     * Insert child at index
     * 
     * @param element Original element Item
     * @param index Insert position (0 = prepend, -1 = append)
     * @param child Child Item to insert
     * @return Updated element Item
     */
    Item elmt_insert_child(Item element, int index, Item child);
    
    /**
     * Insert multiple children at index (batch operation)
     * 
     * @param element Original element Item
     * @param index Insert position
     * @param count Number of children
     * @param children Array of child Items
     * @return Updated element Item
     */
    Item elmt_insert_children(Item element, int index, int count, Item* children);
    
    /**
     * Replace child at index
     * 
     * @param element Original element Item
     * @param index Child index to replace
     * @param new_child New child Item
     * @return Updated element Item
     */
    Item elmt_replace_child(Item element, int index, Item new_child);
    
    /**
     * Append child to end
     * Convenience wrapper for elmt_insert_child(element, -1, child)
     */
    Item elmt_append_child(Item element, Item child) {
        return elmt_insert_child(element, -1, child);
    }
    
    /**
     * Update element tag name
     * Creates new element with same attrs/children but different tag
     */
    Item elmt_rename(Item element, const char* new_tag_name);
    
    // ========================================================================
    // ARRAY OPERATIONS
    // ========================================================================
    
    /**
     * Update array element at index
     */
    Item array_set(Item array, int index, Item value);
    
    /**
     * Insert element at index
     */
    Item array_insert(Item array, int index, Item value);
    
    /**
     * Delete element at index
     */
    Item array_delete(Item array, int index);
    
    /**
     * Append element to end
     */
    Item array_append(Item array, Item value);
    
    // ========================================================================
    // VERSION CONTROL (Immutable Mode Only)
    // ========================================================================
    
    /**
     * Commit current state as new version
     * Only meaningful in immutable mode
     * 
     * @param description Optional description for this version
     * @return Version number
     */
    int commit(const char* description = nullptr);
    
    /**
     * Undo to previous version
     * Returns false if no previous version exists
     */
    bool undo();
    
    /**
     * Redo to next version (after undo)
     * Returns false if no next version exists
     */
    bool redo();
    
    /**
     * Get current document root
     */
    Item current() const;
    
    /**
     * Get version at specific number
     */
    Item get_version(int version_num) const;
    
    /**
     * List all versions
     */
    void list_versions() const;
    
    // ========================================================================
    // MODE CONTROL
    // ========================================================================
    
    /**
     * Switch edit mode
     * Warning: Switching to inline mode clears version history
     */
    void set_mode(EditMode mode);
    
    /**
     * Get current mode
     */
    EditMode mode() const { return mode_; }
    
    /**
     * Get builder for creating new structures
     */
    MarkBuilder* builder() { return builder_; }
    
    // ========================================================================
    // INTERNAL HELPERS
    // ========================================================================
    
private:
    // Map helpers
    Item map_update_inline(Map* map, String* key, Item value);
    Item map_update_immutable(Map* map, String* key, Item value);
    Item map_rebuild_with_new_shape(Map* old_map, ShapeBuilder* builder, bool is_inline);
    Item map_delete_inline(Map* map, String* key);
    Item map_delete_immutable(Map* map, String* key);
    
    // Element helpers
    Item elmt_update_attr_inline(Element* elmt, String* attr_name, Item value);
    Item elmt_update_attr_immutable(Element* elmt, String* attr_name, Item value);
    Item elmt_rebuild_with_new_shape(Element* old_elmt, ShapeBuilder* builder, bool is_inline, 
                                     String* new_attr_name = nullptr, Item new_attr_value = ItemNull);
    Item elmt_copy_with_new_children(Element* old_elmt, Item* new_children, int64_t new_length);
    Item elmt_delete_attr_inline(Element* elmt, String* attr_name);
    Item elmt_delete_attr_immutable(Element* elmt, String* attr_name);
    
    // Version helpers
    EditVersion* create_version(Item root, const char* description);
    void free_version_chain(EditVersion* version);
    
    // Utility
    String* ensure_string_key(const char* key);
    bool find_field_in_shape(ShapeEntry* shape, const char* key, TypeId* out_type, int64_t* out_offset);
    void store_value_at_offset(void* field_ptr, Item value, TypeId type_id);
    void decrement_ref_count(void* field_ptr, TypeId type_id);
};
