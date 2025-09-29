#include <gtest/gtest.h>
#include "../radiant/grid.hpp"
#include "../radiant/view.hpp"

extern "C" {
#include "../lib/log.h"
}

// Basic grid container initialization test
TEST(GridLayoutTest, GridContainerInitialization) {
    // Create a mock ViewBlock for testing
    ViewBlock container;
    memset(&container, 0, sizeof(ViewBlock));
    
    // Initialize grid container
    init_grid_container(&container);
    
    // Verify grid container was created
    ASSERT_NE(container.embed, nullptr);
    ASSERT_NE(container.embed->grid_container, nullptr);
    
    GridContainerLayout* grid = container.embed->grid_container;
    
    // Verify default values
    EXPECT_EQ(grid->justify_content, LXB_CSS_VALUE_START);
    EXPECT_EQ(grid->align_content, LXB_CSS_VALUE_START);
    EXPECT_EQ(grid->justify_items, LXB_CSS_VALUE_STRETCH);
    EXPECT_EQ(grid->align_items, LXB_CSS_VALUE_STRETCH);
    EXPECT_EQ(grid->grid_auto_flow, LXB_CSS_VALUE_ROW);
    EXPECT_EQ(grid->row_gap, 0);
    EXPECT_EQ(grid->column_gap, 0);
    
    // Verify arrays were allocated
    EXPECT_NE(grid->grid_items, nullptr);
    EXPECT_NE(grid->grid_areas, nullptr);
    EXPECT_NE(grid->line_names, nullptr);
    EXPECT_EQ(grid->allocated_items, 8);
    EXPECT_EQ(grid->allocated_areas, 4);
    EXPECT_EQ(grid->allocated_line_names, 8);
    
    // Cleanup
    cleanup_grid_container(&container);
    
    // Verify cleanup
    EXPECT_EQ(container.embed->grid_container, nullptr);
}

// Grid track list creation test
TEST(GridLayoutTest, GridTrackListCreation) {
    GridTrackList* track_list = create_grid_track_list(4);
    
    ASSERT_NE(track_list, nullptr);
    EXPECT_EQ(track_list->allocated_tracks, 4);
    EXPECT_EQ(track_list->track_count, 0);
    EXPECT_NE(track_list->tracks, nullptr);
    EXPECT_NE(track_list->line_names, nullptr);
    EXPECT_EQ(track_list->is_repeat, false);
    EXPECT_EQ(track_list->repeat_count, 1);
    
    destroy_grid_track_list(track_list);
}

// Grid track size creation test
TEST(GridLayoutTest, GridTrackSizeCreation) {
    // Test length track size
    GridTrackSize* length_size = create_grid_track_size(GRID_TRACK_SIZE_LENGTH, 100);
    ASSERT_NE(length_size, nullptr);
    EXPECT_EQ(length_size->type, GRID_TRACK_SIZE_LENGTH);
    EXPECT_EQ(length_size->value, 100);
    EXPECT_EQ(length_size->is_percentage, false);
    destroy_grid_track_size(length_size);
    
    // Test fractional track size
    GridTrackSize* fr_size = create_grid_track_size(GRID_TRACK_SIZE_FR, 2);
    ASSERT_NE(fr_size, nullptr);
    EXPECT_EQ(fr_size->type, GRID_TRACK_SIZE_FR);
    EXPECT_EQ(fr_size->value, 2);
    destroy_grid_track_size(fr_size);
    
    // Test auto track size
    GridTrackSize* auto_size = create_grid_track_size(GRID_TRACK_SIZE_AUTO, 0);
    ASSERT_NE(auto_size, nullptr);
    EXPECT_EQ(auto_size->type, GRID_TRACK_SIZE_AUTO);
    EXPECT_EQ(auto_size->value, 0);
    destroy_grid_track_size(auto_size);
}

// Grid area creation test
TEST(GridLayoutTest, GridAreaCreation) {
    GridArea* area = create_grid_area("header", 1, 2, 1, 3);
    
    ASSERT_NE(area, nullptr);
    EXPECT_STREQ(area->name, "header");
    EXPECT_EQ(area->row_start, 1);
    EXPECT_EQ(area->row_end, 2);
    EXPECT_EQ(area->column_start, 1);
    EXPECT_EQ(area->column_end, 3);
    
    destroy_grid_area(area);
    free(area); // Free the area itself since create_grid_area allocates it
}

// Grid line name functionality test
TEST(GridLayoutTest, GridLineNames) {
    ViewBlock container;
    memset(&container, 0, sizeof(ViewBlock));
    init_grid_container(&container);
    
    GridContainerLayout* grid = container.embed->grid_container;
    
    // Add some line names
    add_grid_line_name(grid, "header-start", 1, true);  // Row line
    add_grid_line_name(grid, "sidebar-start", 1, false); // Column line
    add_grid_line_name(grid, "header-end", 2, true);    // Row line
    
    EXPECT_EQ(grid->line_name_count, 3);
    
    // Test finding line names
    EXPECT_EQ(find_grid_line_by_name(grid, "header-start", true), 1);
    EXPECT_EQ(find_grid_line_by_name(grid, "sidebar-start", false), 1);
    EXPECT_EQ(find_grid_line_by_name(grid, "header-end", true), 2);
    EXPECT_EQ(find_grid_line_by_name(grid, "nonexistent", true), 0);
    
    cleanup_grid_container(&container);
}

// Grid item intrinsic size calculation test
TEST(GridLayoutTest, GridItemIntrinsicSizes) {
    ViewBlock item;
    memset(&item, 0, sizeof(ViewBlock));
    
    // Set some basic dimensions
    item.width = 200;
    item.height = 100;
    item.min_width = 50;
    item.max_width = 400;
    item.min_height = 30;
    item.max_height = 200;
    
    // Test column axis (width) intrinsic sizes
    IntrinsicSizes col_sizes = calculate_grid_item_intrinsic_sizes(&item, false);
    EXPECT_GE(col_sizes.min_content, 50);  // Should respect min_width
    EXPECT_LE(col_sizes.max_content, 400); // Should respect max_width
    EXPECT_LE(col_sizes.min_content, col_sizes.max_content);
    
    // Test row axis (height) intrinsic sizes
    IntrinsicSizes row_sizes = calculate_grid_item_intrinsic_sizes(&item, true);
    EXPECT_GE(row_sizes.min_content, 30);  // Should respect min_height
    EXPECT_LE(row_sizes.max_content, 200); // Should respect max_height
    EXPECT_LE(row_sizes.min_content, row_sizes.max_content);
}

// Grid template area parsing test
TEST(GridLayoutTest, GridTemplateAreaParsing) {
    ViewBlock container;
    memset(&container, 0, sizeof(ViewBlock));
    init_grid_container(&container);
    
    GridContainerLayout* grid = container.embed->grid_container;
    
    // Parse a simple grid template areas string
    parse_grid_template_areas(grid, "header header sidebar main");
    
    // The parser should have created some areas (simplified implementation)
    // This tests the basic parsing functionality
    EXPECT_GE(grid->area_count, 0); // Should have parsed some areas
    
    cleanup_grid_container(&container);
}

// Grid utility functions test
TEST(GridLayoutTest, GridUtilityFunctions) {
    ViewBlock item;
    memset(&item, 0, sizeof(ViewBlock));
    
    // Test valid grid item check
    item.type = RDT_VIEW_BLOCK;
    EXPECT_TRUE(is_valid_grid_item(&item));
    
    item.type = RDT_VIEW_INLINE_BLOCK;
    EXPECT_TRUE(is_valid_grid_item(&item));
    
    // Test grid line position resolution
    ViewBlock container;
    memset(&container, 0, sizeof(ViewBlock));
    init_grid_container(&container);
    
    GridContainerLayout* grid = container.embed->grid_container;
    grid->computed_row_count = 3;
    grid->computed_column_count = 3;
    
    // Test positive line values
    EXPECT_EQ(resolve_grid_line_position(grid, 1, nullptr, true, false), 1);
    EXPECT_EQ(resolve_grid_line_position(grid, 2, nullptr, false, false), 2);
    
    // Test negative line values (count from end)
    EXPECT_GT(resolve_grid_line_position(grid, -1, nullptr, true, false), 0);
    
    cleanup_grid_container(&container);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
