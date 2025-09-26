#include "layout.hpp"
#include "flex_layout_new.hpp"
#include "layout_flex_content.hpp"

#include "../lib/log.h"

// Feature flag to control migration
#ifndef USE_NEW_FLEX_LAYOUT
#define USE_NEW_FLEX_LAYOUT 1
#endif

// Migration compatibility layer
void migrate_flex_container_properties(ViewBlock* container) {
    if (!container || !container->embed) return;
    
    log_debug("Migrating flex container properties\n");
    
    // Ensure flex container is properly initialized
    if (!container->embed->flex_container) {
        init_flex_container(container);
    }
    
    FlexContainerLayout* flex = container->embed->flex_container;
    
    // Set default values if not already set
    if (flex->direction == 0) {
        flex->direction = LXB_CSS_VALUE_ROW;
    }
    if (flex->wrap == 0) {
        flex->wrap = LXB_CSS_VALUE_NOWRAP;
    }
    if (flex->justify == 0) {
        flex->justify = LXB_CSS_VALUE_FLEX_START;
    }
    if (flex->align_items == 0) {
        flex->align_items = LXB_CSS_VALUE_FLEX_START;
    }
    if (flex->align_content == 0) {
        flex->align_content = LXB_CSS_VALUE_FLEX_START;
    }
    
    // Note: Legacy flex property migration would go here if needed
}

// Migrate legacy flex properties to new format
void migrate_legacy_flex_properties(ViewBlock* container) {
    if (!container) return;
    
    // Check for any legacy flex properties and convert them
    // This is a placeholder for any specific legacy property migration
    
    log_debug("Legacy flex properties migrated\n");
}

// Migrate flex item properties
void migrate_flex_item_properties(ViewBlock* item) {
    if (!item) return;
    
    log_debug("Migrating flex item properties\n");
    
    // Ensure all new properties are initialized
    if (item->aspect_ratio == 0.0f) {
        // Keep default value
    }
    
    if (item->baseline_offset == 0) {
        // Will use default calculation (3/4 of height)
    }
    
    // Initialize auto margin flags if not set
    // These should be set by CSS resolution, but ensure defaults
    if (!item->margin_left_auto && !item->margin_right_auto && 
        !item->margin_top_auto && !item->margin_bottom_auto) {
        // All false by default - correct
    }
    
    // Initialize percentage flags
    if (!item->width_is_percent && !item->height_is_percent &&
        !item->min_width_is_percent && !item->max_width_is_percent &&
        !item->min_height_is_percent && !item->max_height_is_percent) {
        // All false by default - correct
    }
    
    // Initialize constraint values
    if (item->min_width == 0 && item->max_width == 0 &&
        item->min_height == 0 && item->max_height == 0) {
        // Zero means no constraint - correct
    }
    
    // Initialize position and visibility
    if (item->position == 0) {
        item->position = POS_STATIC;
    }
    if (item->visibility == 0) {
        item->visibility = VIS_VISIBLE;
    }
}

// Check if new flex layout should be used
bool should_use_new_flex_layout(ViewBlock* container) {
    if (!container) return false;
    
    // Feature flag check
    if (!USE_NEW_FLEX_LAYOUT) {
        return false;
    }
    
    // Check if container has new flex properties that require new implementation
    if (container->aspect_ratio > 0 ||
        container->margin_left_auto || container->margin_right_auto ||
        container->margin_top_auto || container->margin_bottom_auto ||
        container->width_is_percent || container->height_is_percent ||
        container->min_width_is_percent || container->max_width_is_percent ||
        container->min_height_is_percent || container->max_height_is_percent) {
        return true;
    }
    
    // Check if any child has new properties
    View* child = container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* child_block = (ViewBlock*)child;
            if (child_block->aspect_ratio > 0 ||
                child_block->margin_left_auto || child_block->margin_right_auto ||
                child_block->margin_top_auto || child_block->margin_bottom_auto ||
                child_block->baseline_offset > 0) {
                return true;
            }
        }
        child = child->next;
    }
    
    // Default to new implementation
    return true;
}

// Compatibility wrapper for old flex layout calls
void layout_flex_container_compat(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    
    if (should_use_new_flex_layout(container)) {
        log_debug("Using new flex layout implementation\n");
        
        // Migrate properties
        migrate_flex_container_properties(container);
        
        // Migrate child properties
        View* child = container->child;
        while (child) {
            if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
                migrate_flex_item_properties((ViewBlock*)child);
            }
            child = child->next;
        }
        
        // Use new implementation
        layout_flex_container_new(lycon, container);
    } else {
        log_debug("Using legacy flex layout implementation\n");
        
        // Use old implementation (if still available)
        // layout_flex_nodes(lycon, (lxb_dom_node_t*)container->child);
        
        // For now, fallback to new implementation with warning
        log_warn("Legacy flex layout not available, using new implementation\n");
        migrate_flex_container_properties(container);
        layout_flex_container_new(lycon, container);
    }
}

// Validate migration results
bool validate_flex_migration(ViewBlock* container) {
    if (!container) return false;
    
    // Check that flex container is properly initialized
    if (!container->embed || !container->embed->flex_container) {
        log_error("Flex container not properly initialized after migration\n");
        return false;
    }
    
    FlexContainerLayout* flex = container->embed->flex_container;
    
    // Validate flex container properties
    if (flex->direction == 0 || flex->wrap == 0 || flex->justify == 0 ||
        flex->align_items == 0 || flex->align_content == 0) {
        log_error("Flex container properties not properly set after migration\n");
        return false;
    }
    
    // Validate flex items
    View* child = container->child;
    while (child) {
        if (child->type == RDT_VIEW_BLOCK || child->type == RDT_VIEW_INLINE_BLOCK) {
            ViewBlock* item = (ViewBlock*)child;
            
            // Check that position and visibility are set
            if (item->position == 0 || item->visibility == 0) {
                log_error("Flex item properties not properly initialized\n");
                return false;
            }
        }
        child = child->next;
    }
    
    log_debug("Flex migration validation passed\n");
    return true;
}

// Performance comparison between old and new implementations
void benchmark_flex_implementations(LayoutContext* lycon, ViewBlock* container) {
    if (!container) return;
    
    // This would be used for performance testing during migration
    log_debug("Benchmarking flex implementations for container %p\n", container);
    
    // Record start time
    // auto start_time = std::chrono::high_resolution_clock::now();
    
    // Run new implementation
    layout_flex_container_new(lycon, container);
    
    // Record end time
    // auto end_time = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // log_debug("New flex implementation took %ld microseconds\n", duration.count());
}

// Clean up old flex implementation remnants
void cleanup_legacy_flex_data(ViewBlock* container) {
    if (!container) return;
    
    // Remove any old flex-specific data structures that are no longer needed
    // This is a placeholder for cleaning up legacy data
    
    log_debug("Legacy flex data cleaned up\n");
}

// Migration status tracking
typedef struct {
    int containers_migrated;
    int items_migrated;
    int errors_encountered;
    bool migration_complete;
} MigrationStatus;

static MigrationStatus migration_status = {0, 0, 0, false};

// Get migration status
MigrationStatus* get_migration_status() {
    return &migration_status;
}

// Update migration statistics
void update_migration_stats(bool is_container, bool success) {
    if (is_container) {
        migration_status.containers_migrated++;
    } else {
        migration_status.items_migrated++;
    }
    
    if (!success) {
        migration_status.errors_encountered++;
    }
}

// Complete migration process
void complete_flex_migration() {
    migration_status.migration_complete = true;
    
    log_info("Flex migration completed: %d containers, %d items, %d errors\n",
             migration_status.containers_migrated,
             migration_status.items_migrated,
             migration_status.errors_encountered);
}

// Reset migration status
void reset_migration_status() {
    migration_status.containers_migrated = 0;
    migration_status.items_migrated = 0;
    migration_status.errors_encountered = 0;
    migration_status.migration_complete = false;
}
