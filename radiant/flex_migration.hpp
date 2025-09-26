#pragma once

#include "layout.hpp"

// Migration status structure
typedef struct {
    int containers_migrated;
    int items_migrated;
    int errors_encountered;
    bool migration_complete;
} MigrationStatus;

// Core migration functions
void migrate_flex_container_properties(ViewBlock* container);
void migrate_legacy_flex_properties(ViewBlock* container);
void migrate_flex_item_properties(ViewBlock* item);

// Migration decision and compatibility
bool should_use_new_flex_layout(ViewBlock* container);
void layout_flex_container_compat(LayoutContext* lycon, ViewBlock* container);

// Validation and testing
bool validate_flex_migration(ViewBlock* container);
void benchmark_flex_implementations(LayoutContext* lycon, ViewBlock* container);

// Cleanup and maintenance
void cleanup_legacy_flex_data(ViewBlock* container);

// Migration status tracking
MigrationStatus* get_migration_status();
void update_migration_stats(bool is_container, bool success);
void complete_flex_migration();
void reset_migration_status();
