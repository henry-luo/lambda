#ifndef PAGE_H
#define PAGE_H

#include "../typeset.h"

// Page settings structure
struct PageSettings {
    float width;
    float height;
    float margin_top;
    float margin_bottom;
    float margin_left;
    float margin_right;
    char* paper_size;           // "A4", "Letter", "Legal", etc.
    
    // Page numbering
    bool show_page_numbers;
    char* page_number_format;   // "%d", "Page %d", etc.
    float page_number_margin;
    
    // Headers and footers
    char* header_text;
    char* footer_text;
    float header_margin;
    float footer_margin;
    
    // Page orientation
    bool landscape;
    
    // Content area (calculated from margins)
    float content_width;
    float content_height;
    float content_x;
    float content_y;
};

// Individual page structure
typedef struct Page {
    int page_number;
    float content_width;
    float content_height;
    DocNode* content_root;
    struct Page* next;
    struct Page* prev;
    
    // Page-specific overrides
    PageSettings* custom_settings;
    bool has_custom_settings;
    
    // Layout state
    float current_y;            // Current vertical position for content
    float remaining_height;     // Remaining space on page
    bool is_full;              // Page is full and cannot accept more content
    
    // Page elements
    DocNode* header;
    DocNode* footer;
    DocNode* page_number_node;
} Page;

// Page collection for document
typedef struct PageCollection {
    Page* first_page;
    Page* last_page;
    int total_pages;
    PageSettings* default_settings;
} PageCollection;

// Page settings creation and management
PageSettings* page_settings_create(void);
void page_settings_destroy(PageSettings* settings);
PageSettings* page_settings_copy(PageSettings* settings);
void page_settings_set_paper_size(PageSettings* settings, const char* paper_size);
void page_settings_set_margins(PageSettings* settings, float top, float bottom, float left, float right);
void page_settings_calculate_content_area(PageSettings* settings);

// Predefined paper sizes
void page_settings_set_a4(PageSettings* settings);
void page_settings_set_letter(PageSettings* settings);
void page_settings_set_legal(PageSettings* settings);
void page_settings_set_custom_size(PageSettings* settings, float width, float height);

// Page creation and management
Page* page_create(int page_number, PageSettings* settings);
void page_destroy(Page* page);
PageCollection* page_collection_create(PageSettings* default_settings);
void page_collection_destroy(PageCollection* collection);

// Page manipulation
Page* page_collection_add_page(PageCollection* collection);
void page_collection_remove_page(PageCollection* collection, Page* page);
Page* page_collection_get_page(PageCollection* collection, int page_number);

// Content addition to pages
bool page_can_fit_content(Page* page, float content_height);
bool page_add_content(Page* page, DocNode* content, float content_height);
void page_add_header(Page* page, DocNode* header);
void page_add_footer(Page* page, DocNode* footer);
void page_add_page_number(Page* page);

// Page layout calculations
float page_get_available_height(Page* page);
float page_get_available_width(Page* page);
float page_get_content_start_y(Page* page);
float page_get_content_end_y(Page* page);

// Page breaking logic
typedef enum {
    PAGE_BREAK_AUTO,
    PAGE_BREAK_ALWAYS,
    PAGE_BREAK_AVOID,
    PAGE_BREAK_LEFT,     // Break to left page (for book layouts)
    PAGE_BREAK_RIGHT     // Break to right page (for book layouts)
} PageBreakType;

bool should_break_page(Page* page, DocNode* content, float content_height);
Page* break_to_new_page(PageCollection* collection, DocNode* content);
void apply_page_break_rules(PageCollection* collection, DocNode* node);

// Page utilities
void page_clear_content(Page* page);
bool page_is_empty(Page* page);
float page_calculate_content_height(Page* page);
void page_optimize_layout(Page* page);

// Page numbering
void page_collection_update_page_numbers(PageCollection* collection);
char* format_page_number(Page* page, const char* format);

// Header/footer utilities
DocNode* create_default_header(const char* text);
DocNode* create_default_footer(const char* text);
DocNode* create_page_number_node(Page* page);

#endif // PAGE_H
