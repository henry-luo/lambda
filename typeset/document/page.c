#include "page.h"
#include <stdlib.h>
#include <string.h>

// Page settings creation and management
PageSettings* page_settings_create(void) {
    PageSettings* settings = malloc(sizeof(PageSettings));
    if (!settings) return NULL;
    
    memset(settings, 0, sizeof(PageSettings));
    
    // Set default values
    settings->width = TYPESET_DEFAULT_PAGE_WIDTH;
    settings->height = TYPESET_DEFAULT_PAGE_HEIGHT;
    settings->margin_top = TYPESET_DEFAULT_MARGIN;
    settings->margin_bottom = TYPESET_DEFAULT_MARGIN;
    settings->margin_left = TYPESET_DEFAULT_MARGIN;
    settings->margin_right = TYPESET_DEFAULT_MARGIN;
    settings->show_page_numbers = true;
    settings->page_number_margin = 36.0f;  // 0.5 inch
    settings->header_margin = 36.0f;
    settings->footer_margin = 36.0f;
    settings->landscape = false;
    
    // Set default paper size
    settings->paper_size = malloc(strlen("A4") + 1);
    if (settings->paper_size) {
        strcpy(settings->paper_size, "A4");
    }
    
    // Calculate content area
    page_settings_calculate_content_area(settings);
    
    return settings;
}

void page_settings_destroy(PageSettings* settings) {
    if (!settings) return;
    
    free(settings->paper_size);
    free(settings->page_number_format);
    free(settings->header_text);
    free(settings->footer_text);
    
    free(settings);
}

PageSettings* page_settings_copy(PageSettings* settings) {
    if (!settings) return NULL;
    
    PageSettings* copy = malloc(sizeof(PageSettings));
    if (!copy) return NULL;
    
    memcpy(copy, settings, sizeof(PageSettings));
    
    // Copy strings
    if (settings->paper_size) {
        copy->paper_size = malloc(strlen(settings->paper_size) + 1);
        if (copy->paper_size) {
            strcpy(copy->paper_size, settings->paper_size);
        }
    }
    
    if (settings->page_number_format) {
        copy->page_number_format = malloc(strlen(settings->page_number_format) + 1);
        if (copy->page_number_format) {
            strcpy(copy->page_number_format, settings->page_number_format);
        }
    }
    
    if (settings->header_text) {
        copy->header_text = malloc(strlen(settings->header_text) + 1);
        if (copy->header_text) {
            strcpy(copy->header_text, settings->header_text);
        }
    }
    
    if (settings->footer_text) {
        copy->footer_text = malloc(strlen(settings->footer_text) + 1);
        if (copy->footer_text) {
            strcpy(copy->footer_text, settings->footer_text);
        }
    }
    
    return copy;
}

void page_settings_set_paper_size(PageSettings* settings, const char* paper_size) {
    if (!settings || !paper_size) return;
    
    free(settings->paper_size);
    settings->paper_size = malloc(strlen(paper_size) + 1);
    if (settings->paper_size) {
        strcpy(settings->paper_size, paper_size);
    }
    
    // Set dimensions based on paper size
    if (strcmp(paper_size, "A4") == 0) {
        page_settings_set_a4(settings);
    } else if (strcmp(paper_size, "Letter") == 0) {
        page_settings_set_letter(settings);
    } else if (strcmp(paper_size, "Legal") == 0) {
        page_settings_set_legal(settings);
    }
}

void page_settings_set_margins(PageSettings* settings, float top, float bottom, float left, float right) {
    if (!settings) return;
    
    settings->margin_top = top;
    settings->margin_bottom = bottom;
    settings->margin_left = left;
    settings->margin_right = right;
    
    page_settings_calculate_content_area(settings);
}

void page_settings_calculate_content_area(PageSettings* settings) {
    if (!settings) return;
    
    settings->content_width = settings->width - settings->margin_left - settings->margin_right;
    settings->content_height = settings->height - settings->margin_top - settings->margin_bottom;
    settings->content_x = settings->margin_left;
    settings->content_y = settings->margin_top;
}

// Predefined paper sizes
void page_settings_set_a4(PageSettings* settings) {
    if (!settings) return;
    
    settings->width = PAPER_A4_WIDTH;
    settings->height = PAPER_A4_HEIGHT;
    page_settings_calculate_content_area(settings);
}

void page_settings_set_letter(PageSettings* settings) {
    if (!settings) return;
    
    settings->width = PAPER_LETTER_WIDTH;
    settings->height = PAPER_LETTER_HEIGHT;
    page_settings_calculate_content_area(settings);
}

void page_settings_set_legal(PageSettings* settings) {
    if (!settings) return;
    
    settings->width = PAPER_LEGAL_WIDTH;
    settings->height = PAPER_LEGAL_HEIGHT;
    page_settings_calculate_content_area(settings);
}

void page_settings_set_custom_size(PageSettings* settings, float width, float height) {
    if (!settings) return;
    
    settings->width = width;
    settings->height = height;
    page_settings_calculate_content_area(settings);
}

// Page creation and management
Page* page_create(int page_number, PageSettings* settings) {
    Page* page = malloc(sizeof(Page));
    if (!page) return NULL;
    
    memset(page, 0, sizeof(Page));
    
    page->page_number = page_number;
    page->content_width = settings ? settings->content_width : TYPESET_DEFAULT_PAGE_WIDTH - 2 * TYPESET_DEFAULT_MARGIN;
    page->content_height = settings ? settings->content_height : TYPESET_DEFAULT_PAGE_HEIGHT - 2 * TYPESET_DEFAULT_MARGIN;
    page->current_y = 0.0f;
    page->remaining_height = page->content_height;
    page->is_full = false;
    
    return page;
}

void page_destroy(Page* page) {
    if (!page) return;
    
    if (page->custom_settings) {
        page_settings_destroy(page->custom_settings);
    }
    
    // Note: We don't destroy content_root, header, footer, page_number_node
    // as they are owned by the document tree
    
    free(page);
}

PageCollection* page_collection_create(PageSettings* default_settings) {
    PageCollection* collection = malloc(sizeof(PageCollection));
    if (!collection) return NULL;
    
    memset(collection, 0, sizeof(PageCollection));
    
    collection->default_settings = page_settings_copy(default_settings);
    
    return collection;
}

void page_collection_destroy(PageCollection* collection) {
    if (!collection) return;
    
    // Destroy all pages
    Page* page = collection->first_page;
    while (page) {
        Page* next = page->next;
        page_destroy(page);
        page = next;
    }
    
    if (collection->default_settings) {
        page_settings_destroy(collection->default_settings);
    }
    
    free(collection);
}

// Page manipulation
Page* page_collection_add_page(PageCollection* collection) {
    if (!collection) return NULL;
    
    int page_number = collection->total_pages + 1;
    Page* page = page_create(page_number, collection->default_settings);
    if (!page) return NULL;
    
    if (!collection->first_page) {
        collection->first_page = page;
        collection->last_page = page;
    } else {
        page->prev = collection->last_page;
        collection->last_page->next = page;
        collection->last_page = page;
    }
    
    collection->total_pages++;
    
    return page;
}

Page* page_collection_get_page(PageCollection* collection, int page_number) {
    if (!collection || page_number < 1) return NULL;
    
    Page* page = collection->first_page;
    while (page) {
        if (page->page_number == page_number) {
            return page;
        }
        page = page->next;
    }
    
    return NULL;
}

// Content addition to pages
bool page_can_fit_content(Page* page, float content_height) {
    if (!page) return false;
    
    return page->remaining_height >= content_height && !page->is_full;
}

bool page_add_content(Page* page, DocNode* content, float content_height) {
    if (!page || !content) return false;
    
    if (!page_can_fit_content(page, content_height)) {
        return false;
    }
    
    // Add content to page (simplified - in reality this would be more complex)
    if (!page->content_root) {
        page->content_root = content;
    }
    
    page->current_y += content_height;
    page->remaining_height -= content_height;
    
    if (page->remaining_height <= 0) {
        page->is_full = true;
    }
    
    return true;
}

// Page layout calculations
float page_get_available_height(Page* page) {
    return page ? page->remaining_height : 0.0f;
}

float page_get_available_width(Page* page) {
    return page ? page->content_width : 0.0f;
}

float page_get_content_start_y(Page* page) {
    return page ? 0.0f : 0.0f; // Relative to content area
}

float page_get_content_end_y(Page* page) {
    return page ? page->content_height : 0.0f;
}

// Page breaking logic
bool should_break_page(Page* page, DocNode* content, float content_height) {
    if (!page || !content) return false;
    
    // Simple logic - break if content doesn't fit
    return !page_can_fit_content(page, content_height);
}

Page* break_to_new_page(PageCollection* collection, DocNode* content) {
    if (!collection) return NULL;
    
    Page* new_page = page_collection_add_page(collection);
    return new_page;
}

// Utility functions
bool page_is_empty(Page* page) {
    if (!page) return true;
    
    return page->current_y == 0.0f;
}

float page_calculate_content_height(Page* page) {
    if (!page) return 0.0f;
    
    return page->current_y;
}

// Header/footer utilities
DocNode* create_default_header(const char* text) {
    if (!text) return NULL;
    
    DocNode* header = docnode_create(DOC_NODE_TEXT);
    docnode_set_text_content(header, text);
    
    return header;
}

DocNode* create_default_footer(const char* text) {
    if (!text) return NULL;
    
    DocNode* footer = docnode_create(DOC_NODE_TEXT);
    docnode_set_text_content(footer, text);
    
    return footer;
}

DocNode* create_page_number_node(Page* page) {
    if (!page) return NULL;
    
    // Create a simple page number text node
    char page_num_text[32];
    snprintf(page_num_text, sizeof(page_num_text), "%d", page->page_number);
    
    DocNode* page_num_node = docnode_create(DOC_NODE_TEXT);
    docnode_set_text_content(page_num_node, page_num_text);
    
    return page_num_node;
}
