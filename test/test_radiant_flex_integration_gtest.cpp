#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>

// Include the radiant layout headers
#include "../radiant/layout.hpp"
#include "../radiant/view.hpp"
#include "../radiant/flex.hpp"
#include "../radiant/flex_layout_new.hpp"

// Test fixture for flex layout integration tests with HTML/CSS
class FlexIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        lycon = (LayoutContext*)calloc(1, sizeof(LayoutContext));
        lycon->width = 1200;
        lycon->height = 800;
        lycon->dpi = 96;
        init_view_pool(lycon);
    }
    
    void TearDown() override {
        cleanup_view_pool(lycon);
        free(lycon);
    }
    
    // Helper to create HTML document structure
    DomNode* createHTMLDocument(const std::string& html_content) {
        // This would normally parse HTML, for testing we create structure manually
        DomNode* doc = (DomNode*)calloc(1, sizeof(DomNode));
        doc->tag = strdup("html");
        doc->node_type = NODE_ELEMENT;
        return doc;
    }
    
    // Helper to create flex container from CSS properties
    ViewBlock* createFlexContainerFromCSS(const std::string& css_properties) {
        ViewBlock* container = alloc_view_block(lycon);
        container->width = 800;
        container->height = 400;
        container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        container->embed->flex_container = (FlexContainerLayout*)calloc(1, sizeof(FlexContainerLayout));
        
        // Parse CSS properties (simplified for testing)
        if (css_properties.find("flex-direction: column") != std::string::npos) {
            container->embed->flex_container->direction = DIR_COLUMN;
        } else {
            container->embed->flex_container->direction = DIR_ROW;
        }
        
        if (css_properties.find("flex-wrap: wrap") != std::string::npos) {
            container->embed->flex_container->wrap = WRAP_WRAP;
        } else if (css_properties.find("flex-wrap: wrap-reverse") != std::string::npos) {
            container->embed->flex_container->wrap = WRAP_WRAP_REVERSE;
        } else {
            container->embed->flex_container->wrap = WRAP_NOWRAP;
        }
        
        if (css_properties.find("justify-content: center") != std::string::npos) {
            container->embed->flex_container->justify = JUSTIFY_CENTER;
        } else if (css_properties.find("justify-content: flex-end") != std::string::npos) {
            container->embed->flex_container->justify = JUSTIFY_END;
        } else if (css_properties.find("justify-content: space-between") != std::string::npos) {
            container->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
        } else if (css_properties.find("justify-content: space-around") != std::string::npos) {
            container->embed->flex_container->justify = JUSTIFY_SPACE_AROUND;
        } else if (css_properties.find("justify-content: space-evenly") != std::string::npos) {
            container->embed->flex_container->justify = JUSTIFY_SPACE_EVENLY;
        } else {
            container->embed->flex_container->justify = JUSTIFY_START;
        }
        
        if (css_properties.find("align-items: center") != std::string::npos) {
            container->embed->flex_container->align_items = ALIGN_CENTER;
        } else if (css_properties.find("align-items: flex-end") != std::string::npos) {
            container->embed->flex_container->align_items = ALIGN_END;
        } else if (css_properties.find("align-items: stretch") != std::string::npos) {
            container->embed->flex_container->align_items = ALIGN_STRETCH;
        } else if (css_properties.find("align-items: baseline") != std::string::npos) {
            container->embed->flex_container->align_items = ALIGN_BASELINE;
        } else {
            container->embed->flex_container->align_items = ALIGN_START;
        }
        
        // Parse gap properties
        size_t gap_pos = css_properties.find("gap: ");
        if (gap_pos != std::string::npos) {
            int gap_value = std::stoi(css_properties.substr(gap_pos + 5));
            container->embed->flex_container->row_gap = gap_value;
            container->embed->flex_container->column_gap = gap_value;
        }
        
        return container;
    }
    
    // Helper to create flex item from CSS properties
    ViewBlock* createFlexItemFromCSS(ViewBlock* parent, const std::string& css_properties, 
                                   int width = 100, int height = 100) {
        ViewBlock* item = alloc_view_block(lycon);
        item->width = width;
        item->height = height;
        item->parent = parent;
        
        // Parse flex properties
        size_t flex_pos = css_properties.find("flex: ");
        if (flex_pos != std::string::npos) {
            // Parse "flex: grow shrink basis" format
            std::string flex_value = css_properties.substr(flex_pos + 6);
            size_t space1 = flex_value.find(' ');
            size_t space2 = flex_value.find(' ', space1 + 1);
            
            if (space1 != std::string::npos) {
                item->flex_grow = std::stof(flex_value.substr(0, space1));
                if (space2 != std::string::npos) {
                    item->flex_shrink = std::stof(flex_value.substr(space1 + 1, space2 - space1 - 1));
                    std::string basis_str = flex_value.substr(space2 + 1);
                    if (basis_str == "auto") {
                        item->flex_basis = -1;
                    } else {
                        item->flex_basis = std::stoi(basis_str);
                    }
                }
            }
        } else {
            // Parse individual properties
            if (css_properties.find("flex-grow: ") != std::string::npos) {
                size_t grow_pos = css_properties.find("flex-grow: ") + 11;
                item->flex_grow = std::stof(css_properties.substr(grow_pos));
            }
            if (css_properties.find("flex-shrink: ") != std::string::npos) {
                size_t shrink_pos = css_properties.find("flex-shrink: ") + 13;
                item->flex_shrink = std::stof(css_properties.substr(shrink_pos));
            }
            if (css_properties.find("flex-basis: ") != std::string::npos) {
                size_t basis_pos = css_properties.find("flex-basis: ") + 12;
                std::string basis_str = css_properties.substr(basis_pos);
                if (basis_str.find("auto") != std::string::npos) {
                    item->flex_basis = -1;
                } else {
                    item->flex_basis = std::stoi(basis_str);
                }
            }
        }
        
        // Parse align-self
        if (css_properties.find("align-self: center") != std::string::npos) {
            item->align_self = ALIGN_CENTER;
        } else if (css_properties.find("align-self: flex-end") != std::string::npos) {
            item->align_self = ALIGN_END;
        } else if (css_properties.find("align-self: stretch") != std::string::npos) {
            item->align_self = ALIGN_STRETCH;
        } else if (css_properties.find("align-self: baseline") != std::string::npos) {
            item->align_self = ALIGN_BASELINE;
        } else {
            item->align_self = ALIGN_AUTO;
        }
        
        // Parse order
        size_t order_pos = css_properties.find("order: ");
        if (order_pos != std::string::npos) {
            item->order = std::stoi(css_properties.substr(order_pos + 7));
        }
        
        // Add to parent's children
        if (parent->first_child == nullptr) {
            parent->first_child = item;
            parent->last_child = item;
        } else {
            parent->last_child->next_sibling = item;
            item->prev_sibling = parent->last_child;
            parent->last_child = item;
        }
        
        return item;
    }
    
    LayoutContext* lycon;
};

// Test basic flexbox layout matching CSS specification
TEST_F(FlexIntegrationTest, BasicFlexboxLayout) {
    // Simulate CSS: display: flex; justify-content: space-between;
    ViewBlock* container = createFlexContainerFromCSS("display: flex; justify-content: space-between;");
    
    // Create items: flex: 1; (equivalent to flex: 1 1 0)
    ViewBlock* item1 = createFlexItemFromCSS(container, "flex: 1 1 0", 100, 100);
    ViewBlock* item2 = createFlexItemFromCSS(container, "flex: 1 1 0", 100, 100);
    ViewBlock* item3 = createFlexItemFromCSS(container, "flex: 1 1 0", 100, 100);
    
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_BETWEEN);
    EXPECT_FLOAT_EQ(item1->flex_grow, 1.0f);
    EXPECT_FLOAT_EQ(item1->flex_shrink, 1.0f);
    EXPECT_EQ(item1->flex_basis, 0);
    
    // With flex: 1 1 0 and space-between, items should grow equally
    // Available space: 800px, 3 items with flex-basis 0
    // Each item gets: 800 / 3 ≈ 266.67px
}

// Test CSS Grid-like layout using flexbox
TEST_F(FlexIntegrationTest, GridLikeFlexLayout) {
    // Create a 3x3 grid using nested flexboxes
    ViewBlock* main_container = createFlexContainerFromCSS(
        "display: flex; flex-direction: column; height: 600px;"
    );
    
    // Create three rows
    for (int row = 0; row < 3; ++row) {
        ViewBlock* row_container = createFlexItemFromCSS(main_container, 
            "flex: 1; display: flex; flex-direction: row;", 800, 200);
        
        // Make it a flex container
        row_container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        row_container->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
        row_container->embed->flex_container->direction = DIR_ROW;
        row_container->embed->flex_container->justify = JUSTIFY_START;
        row_container->embed->flex_container->align_items = ALIGN_STRETCH;
        
        // Create three columns in each row
        for (int col = 0; col < 3; ++col) {
            ViewBlock* cell = createFlexItemFromCSS(row_container, 
                "flex: 1;", 266, 200);
        }
    }
    
    // Verify structure
    EXPECT_EQ(main_container->embed->flex_container->direction, DIR_COLUMN);
    
    // Count rows
    int row_count = 0;
    ViewBlock* row = main_container->first_child;
    while (row) {
        row_count++;
        
        // Count columns in each row
        int col_count = 0;
        ViewBlock* col = row->first_child;
        while (col) {
            col_count++;
            col = col->next_sibling;
        }
        EXPECT_EQ(col_count, 3);
        
        row = row->next_sibling;
    }
    EXPECT_EQ(row_count, 3);
}

// Test responsive flexbox layout
TEST_F(FlexIntegrationTest, ResponsiveFlexLayout) {
    // Simulate responsive design with flex-wrap
    ViewBlock* container = createFlexContainerFromCSS(
        "display: flex; flex-wrap: wrap; justify-content: space-around;"
    );
    
    // Create items that will wrap on smaller screens
    std::vector<ViewBlock*> items;
    for (int i = 0; i < 6; ++i) {
        ViewBlock* item = createFlexItemFromCSS(container, 
            "flex: 0 0 250px;", 250, 150); // Fixed width items
        items.push_back(item);
    }
    
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_WRAP);
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_SPACE_AROUND);
    
    // With container width 800px and item width 250px:
    // Line 1: 3 items (250 * 3 = 750px < 800px)
    // Line 2: 3 items (would wrap to next line)
    
    // Verify all items are created
    int item_count = 0;
    ViewBlock* child = container->first_child;
    while (child) {
        EXPECT_EQ(child->flex_basis, 250);
        EXPECT_FLOAT_EQ(child->flex_grow, 0.0f);
        EXPECT_FLOAT_EQ(child->flex_shrink, 0.0f);
        item_count++;
        child = child->next_sibling;
    }
    EXPECT_EQ(item_count, 6);
}

// Test flexbox navigation menu
TEST_F(FlexIntegrationTest, NavigationMenuLayout) {
    // Create horizontal navigation menu
    ViewBlock* nav = createFlexContainerFromCSS(
        "display: flex; justify-content: space-between; align-items: center;"
    );
    
    // Logo (fixed size)
    ViewBlock* logo = createFlexItemFromCSS(nav, "flex: 0 0 auto;", 120, 40);
    
    // Navigation items (centered, flexible)
    ViewBlock* nav_items = createFlexItemFromCSS(nav, "flex: 1; display: flex; justify-content: center;", 400, 40);
    nav_items->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    nav_items->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    nav_items->embed->flex_container->direction = DIR_ROW;
    nav_items->embed->flex_container->justify = JUSTIFY_CENTER;
    nav_items->embed->flex_container->align_items = ALIGN_CENTER;
    nav_items->embed->flex_container->column_gap = 20;
    
    // Individual nav items
    for (int i = 0; i < 4; ++i) {
        ViewBlock* nav_item = createFlexItemFromCSS(nav_items, "flex: 0 0 auto;", 80, 30);
    }
    
    // User actions (fixed size)
    ViewBlock* user_actions = createFlexItemFromCSS(nav, "flex: 0 0 auto;", 100, 40);
    
    // Verify layout structure
    EXPECT_EQ(nav->embed->flex_container->justify, JUSTIFY_SPACE_BETWEEN);
    EXPECT_EQ(nav->embed->flex_container->align_items, ALIGN_CENTER);
    
    // Verify nav items container
    EXPECT_FLOAT_EQ(nav_items->flex_grow, 1.0f);
    EXPECT_EQ(nav_items->embed->flex_container->justify, JUSTIFY_CENTER);
    EXPECT_EQ(nav_items->embed->flex_container->column_gap, 20);
}

// Test card layout with flexbox
TEST_F(FlexIntegrationTest, CardLayoutSystem) {
    // Create card container
    ViewBlock* container = createFlexContainerFromCSS(
        "display: flex; flex-wrap: wrap; gap: 20px; justify-content: flex-start;"
    );
    
    // Create cards with different content sizes
    std::vector<std::pair<int, int>> card_sizes = {
        {300, 200}, {300, 250}, {300, 180}, {300, 220}, {300, 190}
    };
    
    for (auto size : card_sizes) {
        ViewBlock* card = createFlexItemFromCSS(container, 
            "flex: 0 0 300px;", size.first, size.second);
        
        // Create card content structure
        card->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        card->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
        card->embed->flex_container->direction = DIR_COLUMN;
        card->embed->flex_container->justify = JUSTIFY_START;
        card->embed->flex_container->align_items = ALIGN_STRETCH;
        
        // Card header
        ViewBlock* header = createFlexItemFromCSS(card, "flex: 0 0 auto;", 300, 60);
        
        // Card content (flexible)
        ViewBlock* content = createFlexItemFromCSS(card, "flex: 1;", 300, size.second - 120);
        
        // Card footer
        ViewBlock* footer = createFlexItemFromCSS(card, "flex: 0 0 auto;", 300, 60);
    }
    
    EXPECT_EQ(container->embed->flex_container->wrap, WRAP_WRAP);
    EXPECT_EQ(container->embed->flex_container->row_gap, 20);
    EXPECT_EQ(container->embed->flex_container->column_gap, 20);
}

// Test form layout with flexbox
TEST_F(FlexIntegrationTest, FormLayoutSystem) {
    // Create form container
    ViewBlock* form = createFlexContainerFromCSS(
        "display: flex; flex-direction: column; gap: 15px;"
    );
    
    // Create form rows
    for (int i = 0; i < 5; ++i) {
        ViewBlock* row = createFlexItemFromCSS(form, 
            "flex: 0 0 auto; display: flex; align-items: center;", 800, 50);
        
        row->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        row->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
        row->embed->flex_container->direction = DIR_ROW;
        row->embed->flex_container->justify = JUSTIFY_START;
        row->embed->flex_container->align_items = ALIGN_CENTER;
        row->embed->flex_container->column_gap = 10;
        
        // Label (fixed width)
        ViewBlock* label = createFlexItemFromCSS(row, "flex: 0 0 150px;", 150, 30);
        
        // Input (flexible)
        ViewBlock* input = createFlexItemFromCSS(row, "flex: 1;", 500, 30);
        
        // Optional help text (fixed width)
        if (i % 2 == 0) {
            ViewBlock* help = createFlexItemFromCSS(row, "flex: 0 0 100px;", 100, 20);
        }
    }
    
    EXPECT_EQ(form->embed->flex_container->direction, DIR_COLUMN);
    EXPECT_EQ(form->embed->flex_container->row_gap, 15);
}

// Test sidebar layout with flexbox
TEST_F(FlexIntegrationTest, SidebarLayoutSystem) {
    // Create main layout container
    ViewBlock* layout = createFlexContainerFromCSS(
        "display: flex; flex-direction: row; height: 600px;"
    );
    
    // Sidebar (fixed width)
    ViewBlock* sidebar = createFlexItemFromCSS(layout, 
        "flex: 0 0 250px; display: flex; flex-direction: column;", 250, 600);
    
    sidebar->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    sidebar->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    sidebar->embed->flex_container->direction = DIR_COLUMN;
    sidebar->embed->flex_container->justify = JUSTIFY_START;
    sidebar->embed->flex_container->align_items = ALIGN_STRETCH;
    
    // Sidebar header
    ViewBlock* sidebar_header = createFlexItemFromCSS(sidebar, "flex: 0 0 auto;", 250, 80);
    
    // Sidebar content (scrollable)
    ViewBlock* sidebar_content = createFlexItemFromCSS(sidebar, "flex: 1;", 250, 520);
    
    // Main content area (flexible)
    ViewBlock* main_content = createFlexItemFromCSS(layout, 
        "flex: 1; display: flex; flex-direction: column;", 950, 600);
    
    main_content->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    main_content->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    main_content->embed->flex_container->direction = DIR_COLUMN;
    main_content->embed->flex_container->justify = JUSTIFY_START;
    main_content->embed->flex_container->align_items = ALIGN_STRETCH;
    
    // Main header
    ViewBlock* main_header = createFlexItemFromCSS(main_content, "flex: 0 0 auto;", 950, 80);
    
    // Main body (flexible)
    ViewBlock* main_body = createFlexItemFromCSS(main_content, "flex: 1;", 950, 520);
    
    // Verify layout structure
    EXPECT_EQ(layout->embed->flex_container->direction, DIR_ROW);
    EXPECT_EQ(sidebar->flex_basis, 250);
    EXPECT_FLOAT_EQ(sidebar->flex_grow, 0.0f);
    EXPECT_FLOAT_EQ(main_content->flex_grow, 1.0f);
}

// Test complex nested flexbox layout
TEST_F(FlexIntegrationTest, ComplexNestedLayout) {
    // Create dashboard-like layout
    ViewBlock* dashboard = createFlexContainerFromCSS(
        "display: flex; flex-direction: column; height: 800px;"
    );
    
    // Top bar
    ViewBlock* top_bar = createFlexItemFromCSS(dashboard, 
        "flex: 0 0 60px; display: flex; justify-content: space-between; align-items: center;", 
        1200, 60);
    
    top_bar->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    top_bar->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    top_bar->embed->flex_container->direction = DIR_ROW;
    top_bar->embed->flex_container->justify = JUSTIFY_SPACE_BETWEEN;
    top_bar->embed->flex_container->align_items = ALIGN_CENTER;
    
    // Main content area
    ViewBlock* main_area = createFlexItemFromCSS(dashboard, 
        "flex: 1; display: flex; flex-direction: row;", 1200, 740);
    
    main_area->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    main_area->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    main_area->embed->flex_container->direction = DIR_ROW;
    main_area->embed->flex_container->justify = JUSTIFY_START;
    main_area->embed->flex_container->align_items = ALIGN_STRETCH;
    
    // Left sidebar
    ViewBlock* left_sidebar = createFlexItemFromCSS(main_area, 
        "flex: 0 0 200px;", 200, 740);
    
    // Content grid
    ViewBlock* content_grid = createFlexItemFromCSS(main_area, 
        "flex: 1; display: flex; flex-direction: column; gap: 20px;", 800, 740);
    
    content_grid->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
    content_grid->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
    content_grid->embed->flex_container->direction = DIR_COLUMN;
    content_grid->embed->flex_container->justify = JUSTIFY_START;
    content_grid->embed->flex_container->align_items = ALIGN_STRETCH;
    content_grid->embed->flex_container->row_gap = 20;
    
    // Right sidebar
    ViewBlock* right_sidebar = createFlexItemFromCSS(main_area, 
        "flex: 0 0 200px;", 200, 740);
    
    // Add content rows to the grid
    for (int i = 0; i < 3; ++i) {
        ViewBlock* row = createFlexItemFromCSS(content_grid, 
            "flex: 1; display: flex; gap: 20px;", 800, 240);
        
        row->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        row->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
        row->embed->flex_container->direction = DIR_ROW;
        row->embed->flex_container->justify = JUSTIFY_START;
        row->embed->flex_container->align_items = ALIGN_STRETCH;
        row->embed->flex_container->column_gap = 20;
        
        // Add cards to each row
        for (int j = 0; j < 2; ++j) {
            ViewBlock* card = createFlexItemFromCSS(row, "flex: 1;", 390, 240);
        }
    }
    
    // Verify complex structure
    EXPECT_EQ(dashboard->embed->flex_container->direction, DIR_COLUMN);
    EXPECT_EQ(main_area->embed->flex_container->direction, DIR_ROW);
    EXPECT_EQ(content_grid->embed->flex_container->direction, DIR_COLUMN);
    EXPECT_EQ(content_grid->embed->flex_container->row_gap, 20);
}

// Test flexbox with CSS transforms and positioning
TEST_F(FlexIntegrationTest, FlexboxWithTransforms) {
    ViewBlock* container = createFlexContainerFromCSS(
        "display: flex; justify-content: center; align-items: center;"
    );
    
    // Create items that might have transforms applied
    ViewBlock* item1 = createFlexItemFromCSS(container, "flex: 0 0 auto;", 100, 100);
    ViewBlock* item2 = createFlexItemFromCSS(container, "flex: 0 0 auto;", 100, 100);
    ViewBlock* item3 = createFlexItemFromCSS(container, "flex: 0 0 auto;", 100, 100);
    
    // In a real implementation, transforms would be handled separately
    // but flexbox layout should account for transformed elements
    
    EXPECT_EQ(container->embed->flex_container->justify, JUSTIFY_CENTER);
    EXPECT_EQ(container->embed->flex_container->align_items, ALIGN_CENTER);
}

// Test memory management in complex layouts
TEST_F(FlexIntegrationTest, MemoryManagementTest) {
    // Create a complex layout that exercises memory allocation
    ViewBlock* root = createFlexContainerFromCSS("display: flex; flex-direction: column;");
    
    // Create multiple levels of nesting
    for (int level = 0; level < 5; ++level) {
        ViewBlock* level_container = createFlexItemFromCSS(root, 
            "flex: 1; display: flex;", 1000, 160);
        
        level_container->embed = (EmbedProp*)calloc(1, sizeof(EmbedProp));
        level_container->embed->flex_container = (FlexContainerProp*)calloc(1, sizeof(FlexContainerProp));
        level_container->embed->flex_container->direction = DIR_ROW;
        level_container->embed->flex_container->justify = JUSTIFY_SPACE_AROUND;
        level_container->embed->flex_container->align_items = ALIGN_CENTER;
        
        // Add items to each level
        for (int item = 0; item < 10; ++item) {
            ViewBlock* leaf_item = createFlexItemFromCSS(level_container, 
                "flex: 1;", 100, 100);
        }
    }
    
    // Verify structure was created successfully
    int level_count = 0;
    ViewBlock* level = root->first_child;
    while (level) {
        level_count++;
        
        int item_count = 0;
        ViewBlock* item = level->first_child;
        while (item) {
            item_count++;
            item = item->next_sibling;
        }
        EXPECT_EQ(item_count, 10);
        
        level = level->next_sibling;
    }
    EXPECT_EQ(level_count, 5);
    
    // Memory cleanup is handled by TearDown()
}
