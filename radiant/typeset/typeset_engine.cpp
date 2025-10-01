#include "typeset_engine.hpp"
#include "../layout.hpp"
#include "../../lambda/input/input.h"
#include <cstring>
#include <cstdlib>

// RadiantTypesetEngine implementation
RadiantTypesetEngine::RadiantTypesetEngine(UiContext* ui_context) 
    : ui_context(ui_context), pool(nullptr), default_options(nullptr) {
    
    // Initialize memory pool
    pool = ui_context->document->pool;  // Reuse existing document pool
    
    // Create default options
    default_options = create_default_options();
    
    // Initialize statistics
    reset_stats();
}

RadiantTypesetEngine::~RadiantTypesetEngine() {
    if (default_options) {
        destroy_options(default_options);
    }
}

ViewTree* RadiantTypesetEngine::typeset_latex_document(Item latex_ast, TypesetOptions* options) {
    if (!options) {
        options = default_options;
    }
    
    // Create new ViewTree using Radiant's system
    ViewTree* tree = (ViewTree*)pool_calloc(pool, 1, sizeof(ViewTree));
    if (!tree) return nullptr;
    
    tree->pool = pool;
    
    // Process the LaTeX AST and convert to Radiant views
    ViewBlock* root_block = process_latex_document(latex_ast);
    if (!root_block) {
        return nullptr;
    }
    
    tree->root = (View*)root_block;
    
    // Setup page layout using Radiant's layout system
    setup_page_layout(tree, options);
    
    // Update statistics
    stats.documents_processed++;
    
    return tree;
}

ViewBlock* RadiantTypesetEngine::process_latex_document(Item document_node) {
    // Create document container using existing ViewBlock
    ViewBlock* doc_block = (ViewBlock*)pool_calloc(pool, 1, sizeof(ViewBlock));
    if (!doc_block) return nullptr;
    
    // Initialize ViewBlock structure
    doc_block->type = RDT_VIEW_BLOCK;
    doc_block->node = nullptr;  // No DOM node for LaTeX
    doc_block->parent = nullptr;
    doc_block->child = nullptr;
    doc_block->next = nullptr;
    
    // Set document-level styling
    apply_latex_styling(doc_block, "document");
    
    // Process document content
    if (document_node.type == ITEM_ARRAY && document_node.item.array) {
        Array* content = document_node.item.array;
        ViewBlock* prev_child = nullptr;
        
        for (int i = 0; i < content->length; i++) {
            Item child_item = array_get(content, i);
            ViewBlock* child_block = nullptr;
            
            // Process different LaTeX elements
            if (child_item.type == ITEM_ELEMENT) {
                Element* elem = child_item.item.element;
                if (elem && elem->tag) {
                    if (strcmp(elem->tag, "section") == 0) {
                        child_block = process_latex_section(child_item, 1);
                    } else if (strcmp(elem->tag, "paragraph") == 0) {
                        child_block = process_latex_paragraph(child_item);
                    } else if (strcmp(elem->tag, "math") == 0) {
                        child_block = process_latex_math_display(child_item);
                    } else if (strcmp(elem->tag, "table") == 0) {
                        ViewTable* table = process_latex_table(child_item);
                        child_block = (ViewBlock*)table;  // ViewTable extends ViewBlock
                    } else if (strcmp(elem->tag, "list") == 0) {
                        child_block = process_latex_list(child_item);
                    }
                }
            }
            
            // Add child to document
            if (child_block) {
                child_block->parent = (ViewGroup*)doc_block;
                if (prev_child) {
                    prev_child->next_sibling = (ViewBlock*)child_block;
                    child_block->prev_sibling = prev_child;
                } else {
                    doc_block->child = (View*)child_block;
                    doc_block->first_child = child_block;
                }
                prev_child = child_block;
                doc_block->last_child = child_block;
            }
        }
    }
    
    return doc_block;
}

ViewBlock* RadiantTypesetEngine::process_latex_section(Item section_node, int level) {
    // Create section block using existing ViewBlock
    ViewBlock* section_block = (ViewBlock*)pool_calloc(pool, 1, sizeof(ViewBlock));
    if (!section_block) return nullptr;
    
    // Initialize ViewBlock
    section_block->type = RDT_VIEW_BLOCK;
    section_block->node = nullptr;
    
    // Apply section styling based on level
    char section_class[32];
    snprintf(section_class, sizeof(section_class), "section-level-%d", level);
    apply_latex_styling(section_block, section_class);
    
    // Process section content (title, content, subsections)
    // TODO: Implement section content processing
    
    return section_block;
}

ViewBlock* RadiantTypesetEngine::process_latex_paragraph(Item paragraph_node) {
    // Create paragraph block using existing ViewBlock
    ViewBlock* para_block = (ViewBlock*)pool_calloc(pool, 1, sizeof(ViewBlock));
    if (!para_block) return nullptr;
    
    // Initialize ViewBlock
    para_block->type = RDT_VIEW_BLOCK;
    para_block->node = nullptr;
    
    // Apply paragraph styling
    apply_latex_styling(para_block, "paragraph");
    
    // Process paragraph content (text, inline math, formatting)
    // TODO: Implement paragraph content processing
    
    return para_block;
}

ViewSpan* RadiantTypesetEngine::process_latex_math_inline(Item math_node) {
    // Create inline math using existing ViewSpan
    ViewSpan* math_span = (ViewSpan*)pool_calloc(pool, 1, sizeof(ViewSpan));
    if (!math_span) return nullptr;
    
    // Initialize ViewSpan
    math_span->type = RDT_VIEW_INLINE;
    math_span->node = nullptr;
    
    // Apply math styling
    apply_latex_styling((ViewBlock*)math_span, "math-inline");
    
    // TODO: Process math content using input-math.cpp
    
    return math_span;
}

ViewBlock* RadiantTypesetEngine::process_latex_math_display(Item math_node) {
    // Create display math using existing ViewBlock
    ViewBlock* math_block = (ViewBlock*)pool_calloc(pool, 1, sizeof(ViewBlock));
    if (!math_block) return nullptr;
    
    // Initialize ViewBlock
    math_block->type = RDT_VIEW_BLOCK;
    math_block->node = nullptr;
    
    // Apply display math styling
    apply_latex_styling(math_block, "math-display");
    
    // TODO: Process math content using input-math.cpp
    
    return math_block;
}

ViewTable* RadiantTypesetEngine::process_latex_table(Item table_node) {
    // Create table using existing ViewTable
    ViewTable* table = (ViewTable*)pool_calloc(pool, 1, sizeof(ViewTable));
    if (!table) return nullptr;
    
    // Initialize ViewTable (extends ViewBlock)
    table->type = RDT_VIEW_TABLE;
    table->node = nullptr;
    table->table_layout = TABLE_LAYOUT_AUTO;
    table->border_collapse = false;
    
    // Apply table styling
    apply_latex_styling((ViewBlock*)table, "table");
    
    // TODO: Process table content (rows, cells)
    
    return table;
}

ViewBlock* RadiantTypesetEngine::process_latex_list(Item list_node) {
    // Create list using existing ViewBlock
    ViewBlock* list_block = (ViewBlock*)pool_calloc(pool, 1, sizeof(ViewBlock));
    if (!list_block) return nullptr;
    
    // Initialize ViewBlock
    list_block->type = RDT_VIEW_LIST_ITEM;
    list_block->node = nullptr;
    
    // Apply list styling
    apply_latex_styling(list_block, "list");
    
    // TODO: Process list items
    
    return list_block;
}

ViewSpan* RadiantTypesetEngine::process_text_formatting(Item text_node, const char* format_type) {
    // Create formatted text using existing ViewSpan
    ViewSpan* text_span = (ViewSpan*)pool_calloc(pool, 1, sizeof(ViewSpan));
    if (!text_span) return nullptr;
    
    // Initialize ViewSpan
    text_span->type = RDT_VIEW_INLINE;
    text_span->node = nullptr;
    
    // Apply formatting styling
    apply_latex_styling((ViewBlock*)text_span, format_type);
    
    return text_span;
}

ViewSpan* RadiantTypesetEngine::apply_font_styling(ViewSpan* span, const char* font_command) {
    if (!span || !font_command) return span;
    
    // Apply font styling using existing FontProp system
    if (!span->font) {
        span->font = (FontProp*)pool_calloc(pool, 1, sizeof(FontProp));
    }
    
    // Map LaTeX font commands to Radiant font properties
    if (strcmp(font_command, "textbf") == 0) {
        span->font->font_weight = LXB_CSS_VALUE_BOLD;
    } else if (strcmp(font_command, "textit") == 0) {
        span->font->font_style = LXB_CSS_VALUE_ITALIC;
    } else if (strcmp(font_command, "texttt") == 0) {
        if (span->font->family) free(span->font->family);
        span->font->family = strdup("monospace");
    }
    
    return span;
}

void RadiantTypesetEngine::apply_latex_styling(ViewBlock* view, const char* latex_class) {
    if (!view || !latex_class) return;
    
    // Initialize boundary properties if needed
    if (!view->bound) {
        view->bound = (BoundaryProp*)pool_calloc(pool, 1, sizeof(BoundaryProp));
    }
    
    // Apply styling based on LaTeX class
    if (strcmp(latex_class, "document") == 0) {
        // Document-level styling
        view->width = (int)default_options->page_width;
        view->height = (int)default_options->page_height;
    } else if (strncmp(latex_class, "section-level-", 14) == 0) {
        // Section styling
        int level = atoi(latex_class + 14);
        view->bound->margin.top = 24 - (level * 4);  // Decreasing margins for deeper sections
        view->bound->margin.bottom = 12 - (level * 2);
    } else if (strcmp(latex_class, "paragraph") == 0) {
        // Paragraph styling
        view->bound->margin.bottom = 12;
    } else if (strcmp(latex_class, "math-inline") == 0) {
        // Inline math styling
        // Keep default styling, math-specific rendering handled elsewhere
    } else if (strcmp(latex_class, "math-display") == 0) {
        // Display math styling
        view->bound->margin.top = 12;
        view->bound->margin.bottom = 12;
        // Center alignment would be handled by layout system
    }
}

void RadiantTypesetEngine::setup_page_layout(ViewTree* tree, TypesetOptions* options) {
    if (!tree || !tree->root) return;
    
    // Setup page dimensions and margins using Radiant's layout system
    ViewBlock* root = (ViewBlock*)tree->root;
    
    // Set page size
    root->width = (int)options->page_width;
    root->height = (int)options->page_height;
    
    // Setup margins
    if (!root->bound) {
        root->bound = (BoundaryProp*)pool_calloc(pool, 1, sizeof(BoundaryProp));
    }
    
    root->bound->margin.left = (int)options->margin_left;
    root->bound->margin.right = (int)options->margin_right;
    root->bound->margin.top = (int)options->margin_top;
    root->bound->margin.bottom = (int)options->margin_bottom;
    
    // Calculate content area
    root->content_width = root->width - root->bound->margin.left - root->bound->margin.right;
    root->content_height = root->height - root->bound->margin.top - root->bound->margin.bottom;
}

TypesetOptions* RadiantTypesetEngine::create_default_options() {
    TypesetOptions* options = (TypesetOptions*)pool_calloc(pool, 1, sizeof(TypesetOptions));
    if (!options) return nullptr;
    
    // Page settings
    options->page_width = TYPESET_DEFAULT_PAGE_WIDTH;
    options->page_height = TYPESET_DEFAULT_PAGE_HEIGHT;
    options->margin_left = TYPESET_DEFAULT_MARGIN;
    options->margin_right = TYPESET_DEFAULT_MARGIN;
    options->margin_top = TYPESET_DEFAULT_MARGIN;
    options->margin_bottom = TYPESET_DEFAULT_MARGIN;
    
    // Typography
    options->default_font_family = strdup("Times New Roman");
    options->default_font_size = TYPESET_DEFAULT_FONT_SIZE;
    options->line_height = TYPESET_DEFAULT_LINE_HEIGHT;
    options->paragraph_spacing = 12.0;
    
    // Math settings
    options->math_font_family = strdup("Latin Modern Math");
    options->math_font_scale = 1.0;
    options->use_display_math = true;
    
    // Quality settings
    options->optimize_layout = true;
    options->show_debug_info = false;
    
    return options;
}

void RadiantTypesetEngine::destroy_options(TypesetOptions* options) {
    if (!options) return;
    
    if (options->default_font_family) {
        free(options->default_font_family);
    }
    if (options->math_font_family) {
        free(options->math_font_family);
    }
    
    // Note: Don't free options itself if allocated from pool
}

void RadiantTypesetEngine::reset_stats() {
    stats.documents_processed = 0;
    stats.pages_generated = 0;
    stats.total_layout_time = 0.0;
    stats.memory_usage = 0;
}

void RadiantTypesetEngine::print_stats() const {
    printf("Radiant Typeset Engine Statistics:\n");
    printf("  Documents processed: %d\n", stats.documents_processed);
    printf("  Pages generated: %d\n", stats.pages_generated);
    printf("  Total layout time: %.2f ms\n", stats.total_layout_time);
    printf("  Memory usage: %zu bytes\n", stats.memory_usage);
}

// C interface functions
extern "C" {

ViewTree* radiant_typeset_latex(UiContext* ui_context, Item latex_ast, TypesetOptions* options) {
    RadiantTypesetEngine engine(ui_context);
    return engine.typeset_latex_document(latex_ast, options);
}

TypesetOptions* typeset_options_create_default(void) {
    TypesetOptions* options = (TypesetOptions*)malloc(sizeof(TypesetOptions));
    if (!options) return nullptr;
    
    // Page settings
    options->page_width = TYPESET_DEFAULT_PAGE_WIDTH;
    options->page_height = TYPESET_DEFAULT_PAGE_HEIGHT;
    options->margin_left = TYPESET_DEFAULT_MARGIN;
    options->margin_right = TYPESET_DEFAULT_MARGIN;
    options->margin_top = TYPESET_DEFAULT_MARGIN;
    options->margin_bottom = TYPESET_DEFAULT_MARGIN;
    
    // Typography
    options->default_font_family = strdup("Times New Roman");
    options->default_font_size = TYPESET_DEFAULT_FONT_SIZE;
    options->line_height = TYPESET_DEFAULT_LINE_HEIGHT;
    options->paragraph_spacing = 12.0;
    
    // Math settings
    options->math_font_family = strdup("Latin Modern Math");
    options->math_font_scale = 1.0;
    options->use_display_math = true;
    
    // Quality settings
    options->optimize_layout = true;
    options->show_debug_info = false;
    
    return options;
}

void typeset_options_destroy(TypesetOptions* options) {
    if (!options) return;
    
    if (options->default_font_family) {
        free(options->default_font_family);
    }
    if (options->math_font_family) {
        free(options->math_font_family);
    }
    
    free(options);
}

} // extern "C"
