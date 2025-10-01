#include "latex_bridge.hpp"
#include "../layout.hpp"
#include "../../lambda/input/input-common.h"
#include <cstring>
#include <cstdlib>

// LaTeXRadiantBridge implementation
LaTeXRadiantBridge::LaTeXRadiantBridge(RadiantTypesetEngine* engine) 
    : engine(engine), pool(nullptr), ui_context(nullptr), math_input(nullptr), lambda_context(nullptr) {
    
    if (!engine) {
        log_conversion_error("Invalid engine provided to LaTeXRadiantBridge", {});
        return;
    }
    
    ui_context = engine->get_ui_context();
    pool = engine->get_memory_pool();
    
    // Initialize math integration with existing input-math.cpp
    initialize_math_integration();
}

LaTeXRadiantBridge::~LaTeXRadiantBridge() {
    cleanup_math_integration();
}

ViewTree* LaTeXRadiantBridge::convert_latex_ast_to_radiant(Item latex_ast) {
    if (latex_ast.type == ITEM_NULL) {
        log_conversion_error("Null LaTeX AST provided", latex_ast);
        return nullptr;
    }
    
    // Validate AST structure
    if (!validate_element_structure(latex_ast)) {
        log_conversion_error("Invalid LaTeX AST structure", latex_ast);
        return nullptr;
    }
    
    // Create ViewTree using Radiant's system
    ViewTree* tree = (ViewTree*)pool_calloc(pool, 1, sizeof(ViewTree));
    if (!tree) {
        log_conversion_error("Failed to allocate ViewTree", latex_ast);
        return nullptr;
    }
    
    tree->pool = pool;
    
    // Convert root document element
    ViewBlock* root_block = convert_document_element(latex_ast);
    if (!root_block) {
        log_conversion_error("Failed to convert document element", latex_ast);
        return nullptr;
    }
    
    tree->root = (View*)root_block;
    
    // Setup document metadata
    setup_document_metadata(tree, latex_ast);
    
    return tree;
}

ViewBlock* LaTeXRadiantBridge::convert_document_element(Item element) {
    // Create document container using existing ViewBlock
    ViewBlock* doc_block = create_block_container("latex-document");
    if (!doc_block) return nullptr;
    
    // Apply document-level attributes
    apply_latex_attributes((View*)doc_block, element);
    
    // Process document children
    Array* children = get_element_children(element);
    if (children) {
        ViewBlock* prev_child = nullptr;
        
        for (int i = 0; i < children->length; i++) {
            Item child_item = array_get(children, i);
            ViewBlock* child_block = nullptr;
            
            const char* tag = get_element_tag(child_item);
            if (!tag) continue;
            
            // Route to appropriate converter based on element type
            if (strcmp(tag, "chapter") == 0) {
                child_block = convert_chapter_element(child_item);
            } else if (strcmp(tag, "section") == 0) {
                child_block = convert_section_element(child_item, 1);
            } else if (strcmp(tag, "subsection") == 0) {
                child_block = convert_subsection_element(child_item, 2);
            } else if (strcmp(tag, "paragraph") == 0) {
                child_block = convert_paragraph_element(child_item);
            } else if (strcmp(tag, "abstract") == 0) {
                child_block = convert_abstract_element(child_item);
            } else if (strcmp(tag, "title") == 0) {
                child_block = convert_title_element(child_item);
            } else if (strcmp(tag, "math") == 0) {
                if (is_display_math(child_item)) {
                    child_block = convert_math_display(child_item);
                } else {
                    // Inline math should be handled at paragraph level
                    continue;
                }
            } else if (strcmp(tag, "table") == 0 || strcmp(tag, "tabular") == 0) {
                ViewTable* table = convert_table_element(child_item);
                child_block = (ViewBlock*)table;  // ViewTable extends ViewBlock
            } else if (strcmp(tag, "itemize") == 0 || strcmp(tag, "enumerate") == 0 || strcmp(tag, "description") == 0) {
                child_block = convert_list_element(child_item);
            }
            
            // Add child to document hierarchy
            if (child_block) {
                child_block->parent = (ViewGroup*)doc_block;
                if (prev_child) {
                    prev_child->next_sibling = child_block;
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

ViewBlock* LaTeXRadiantBridge::convert_section_element(Item element, int level) {
    ViewBlock* section_block = create_block_container("latex-section");
    if (!section_block) return nullptr;
    
    // Apply section-specific styling
    char section_class[64];
    snprintf(section_class, sizeof(section_class), "section-level-%d", level);
    apply_default_styling((View*)section_block, section_class);
    
    // Process section title and content
    Array* children = get_element_children(element);
    if (children) {
        for (int i = 0; i < children->length; i++) {
            Item child_item = array_get(children, i);
            const char* tag = get_element_tag(child_item);
            
            if (tag && strcmp(tag, "title") == 0) {
                // Create section title
                ViewBlock* title_block = convert_title_element(child_item);
                if (title_block) {
                    title_block->parent = (ViewGroup*)section_block;
                    if (!section_block->child) {
                        section_block->child = (View*)title_block;
                        section_block->first_child = title_block;
                    }
                    section_block->last_child = title_block;
                }
            } else {
                // Process other section content recursively
                ViewBlock* content_block = nullptr;
                if (tag && strcmp(tag, "paragraph") == 0) {
                    content_block = convert_paragraph_element(child_item);
                } else if (tag && strcmp(tag, "subsection") == 0) {
                    content_block = convert_subsection_element(child_item, level + 1);
                }
                
                if (content_block) {
                    content_block->parent = (ViewGroup*)section_block;
                    if (section_block->last_child) {
                        section_block->last_child->next_sibling = content_block;
                        content_block->prev_sibling = section_block->last_child;
                    }
                    section_block->last_child = content_block;
                }
            }
        }
    }
    
    return section_block;
}

ViewBlock* LaTeXRadiantBridge::convert_paragraph_element(Item element) {
    ViewBlock* para_block = create_block_container("latex-paragraph");
    if (!para_block) return nullptr;
    
    // Process paragraph content (text, inline math, formatting)
    Array* children = get_element_children(element);
    if (children) {
        ViewSpan* prev_span = nullptr;
        
        for (int i = 0; i < children->length; i++) {
            Item child_item = array_get(children, i);
            ViewSpan* child_span = nullptr;
            
            const char* tag = get_element_tag(child_item);
            if (tag) {
                if (strcmp(tag, "text") == 0) {
                    child_span = convert_text_element(child_item);
                } else if (strcmp(tag, "math") == 0 && !is_display_math(child_item)) {
                    child_span = convert_math_inline(child_item);
                } else if (strcmp(tag, "textbf") == 0 || strcmp(tag, "textit") == 0 || strcmp(tag, "texttt") == 0) {
                    child_span = convert_formatted_text(child_item, tag);
                }
            } else if (child_item.type == ITEM_STRING) {
                // Direct text content
                const char* text_content = child_item.item.string->data;
                child_span = create_text_span(text_content);
            }
            
            // Add span to paragraph
            if (child_span) {
                child_span->parent = (ViewGroup*)para_block;
                if (prev_span) {
                    prev_span->next = (View*)child_span;
                } else {
                    para_block->child = (View*)child_span;
                }
                prev_span = child_span;
            }
        }
    }
    
    return para_block;
}

ViewSpan* LaTeXRadiantBridge::convert_math_inline(Item math_element) {
    const char* math_content = get_element_content(math_element);
    if (!math_content) {
        log_conversion_error("No math content found", math_element);
        return nullptr;
    }
    
    // Use existing input-math.cpp for parsing
    return process_math_expression(math_content, false);
}

ViewBlock* LaTeXRadiantBridge::convert_math_display(Item math_element) {
    const char* math_content = get_element_content(math_element);
    if (!math_content) {
        log_conversion_error("No math content found", math_element);
        return nullptr;
    }
    
    // Create display math block
    ViewBlock* math_block = create_block_container("math-display");
    if (!math_block) return nullptr;
    
    // Process math content using existing input-math.cpp
    ViewSpan* math_span = process_math_expression(math_content, true);
    if (math_span) {
        math_span->parent = (ViewGroup*)math_block;
        math_block->child = (View*)math_span;
        math_block->first_child = (ViewBlock*)math_span;
        math_block->last_child = (ViewBlock*)math_span;
    }
    
    return math_block;
}

ViewSpan* LaTeXRadiantBridge::process_math_expression(const char* math_content, bool is_display) {
    if (!math_content || !math_input) {
        return nullptr;
    }
    
    // Determine math flavor (LaTeX vs ASCII)
    const char* flavor = "latex";  // Default to LaTeX
    if (strstr(math_content, "asciimath::") || strstr(math_content, "AM::")) {
        flavor = "ascii";
    }
    
    // Parse math using existing input-math.cpp
    Item math_ast = parse_math_content(math_content, flavor);
    if (math_ast.type == ITEM_NULL) {
        log_conversion_error("Failed to parse math content", {});
        return create_text_span(math_content);  // Fallback to plain text
    }
    
    // Convert math AST to ViewSpan
    return integrate_with_input_math(math_ast);
}

ViewSpan* LaTeXRadiantBridge::integrate_with_input_math(Item math_ast) {
    // Create math span using existing ViewSpan
    ViewSpan* math_span = (ViewSpan*)pool_calloc(pool, 1, sizeof(ViewSpan));
    if (!math_span) return nullptr;
    
    // Initialize ViewSpan for math
    math_span->type = RDT_VIEW_INLINE;
    math_span->node = nullptr;
    
    // Apply math-specific styling
    apply_default_styling((View*)math_span, "math-content");
    
    // Process math AST elements
    if (math_ast.type == ITEM_ELEMENT) {
        Element* math_elem = math_ast.item.element;
        if (math_elem && math_elem->tag) {
            // Handle different math element types
            if (strcmp(math_elem->tag, "fraction") == 0) {
                // TODO: Implement fraction layout using nested ViewSpans
            } else if (strcmp(math_elem->tag, "superscript") == 0) {
                // TODO: Implement superscript positioning
            } else if (strcmp(math_elem->tag, "subscript") == 0) {
                // TODO: Implement subscript positioning
            } else if (strcmp(math_elem->tag, "symbol") == 0) {
                // Create math symbol using ViewText
                const char* symbol_content = get_element_content(math_ast);
                if (symbol_content) {
                    ViewText* symbol_text = create_text_node(symbol_content);
                    if (symbol_text) {
                        math_span->child = (View*)symbol_text;
                    }
                }
            }
        }
    }
    
    return math_span;
}

Item LaTeXRadiantBridge::parse_math_content(const char* math_string, const char* flavor) {
    if (!math_input || !math_string) {
        return (Item){.type = ITEM_NULL};
    }
    
    // Use existing input-math.cpp parser
    try {
        // Reset input state
        input_reset(math_input);
        
        // Parse math content
        Item result = input_math(math_input, math_string);
        return result;
    } catch (...) {
        log_conversion_error("Exception during math parsing", {});
        return (Item){.type = ITEM_NULL};
    }
}

ViewTable* LaTeXRadiantBridge::convert_table_element(Item element) {
    // Create table using existing ViewTable system
    ViewTable* table = (ViewTable*)pool_calloc(pool, 1, sizeof(ViewTable));
    if (!table) return nullptr;
    
    // Initialize ViewTable (extends ViewBlock)
    table->type = RDT_VIEW_TABLE;
    table->node = nullptr;
    table->table_layout = TABLE_LAYOUT_AUTO;
    table->border_collapse = false;
    
    // Apply table styling
    apply_default_styling((View*)table, "latex-table");
    
    // Process table rows
    Array* children = get_element_children(element);
    if (children) {
        ViewTableRow* prev_row = nullptr;
        
        for (int i = 0; i < children->length; i++) {
            Item child_item = array_get(children, i);
            const char* tag = get_element_tag(child_item);
            
            if (tag && strcmp(tag, "row") == 0) {
                ViewTableRow* row = convert_table_row(child_item);
                if (row) {
                    row->parent = (ViewGroup*)table;
                    if (prev_row) {
                        prev_row->next_sibling = (ViewBlock*)row;
                        row->prev_sibling = (ViewBlock*)prev_row;
                    } else {
                        table->child = (View*)row;
                        table->first_child = (ViewBlock*)row;
                    }
                    prev_row = row;
                    table->last_child = (ViewBlock*)row;
                }
            }
        }
    }
    
    return table;
}

ViewTableRow* LaTeXRadiantBridge::convert_table_row(Item row_element) {
    ViewTableRow* row = (ViewTableRow*)pool_calloc(pool, 1, sizeof(ViewTableRow));
    if (!row) return nullptr;
    
    // Initialize ViewTableRow (extends ViewBlock)
    row->type = RDT_VIEW_TABLE_ROW;
    row->node = nullptr;
    
    // Process table cells
    Array* children = get_element_children(row_element);
    if (children) {
        ViewTableCell* prev_cell = nullptr;
        
        for (int i = 0; i < children->length; i++) {
            Item child_item = array_get(children, i);
            const char* tag = get_element_tag(child_item);
            
            if (tag && strcmp(tag, "cell") == 0) {
                ViewTableCell* cell = convert_table_cell(child_item);
                if (cell) {
                    cell->parent = (ViewGroup*)row;
                    if (prev_cell) {
                        prev_cell->next_sibling = (ViewBlock*)cell;
                        cell->prev_sibling = (ViewBlock*)prev_cell;
                    } else {
                        row->child = (View*)cell;
                        row->first_child = (ViewBlock*)cell;
                    }
                    prev_cell = cell;
                    row->last_child = (ViewBlock*)cell;
                }
            }
        }
    }
    
    return row;
}

ViewTableCell* LaTeXRadiantBridge::convert_table_cell(Item cell_element) {
    ViewTableCell* cell = (ViewTableCell*)pool_calloc(pool, 1, sizeof(ViewTableCell));
    if (!cell) return nullptr;
    
    // Initialize ViewTableCell (extends ViewBlock)
    cell->type = RDT_VIEW_TABLE_CELL;
    cell->node = nullptr;
    cell->col_span = 1;
    cell->row_span = 1;
    cell->vertical_align = CELL_VALIGN_TOP;
    
    // Process cell content (similar to paragraph)
    Array* children = get_element_children(cell_element);
    if (children) {
        for (int i = 0; i < children->length; i++) {
            Item child_item = array_get(children, i);
            ViewBlock* content_block = nullptr;
            
            const char* tag = get_element_tag(child_item);
            if (tag && strcmp(tag, "paragraph") == 0) {
                content_block = convert_paragraph_element(child_item);
            } else if (child_item.type == ITEM_STRING) {
                // Direct text content - wrap in paragraph
                ViewBlock* para_block = create_block_container("cell-paragraph");
                if (para_block) {
                    ViewSpan* text_span = create_text_span(child_item.item.string->data);
                    if (text_span) {
                        text_span->parent = (ViewGroup*)para_block;
                        para_block->child = (View*)text_span;
                    }
                    content_block = para_block;
                }
            }
            
            if (content_block) {
                content_block->parent = (ViewGroup*)cell;
                if (!cell->child) {
                    cell->child = (View*)content_block;
                    cell->first_child = content_block;
                }
                cell->last_child = content_block;
                break;  // Only one content block per cell for now
            }
        }
    }
    
    return cell;
}

// Utility function implementations
void LaTeXRadiantBridge::initialize_math_integration() {
    // Create Lambda context for math parsing
    lambda_context = context_create();
    if (!lambda_context) {
        log_conversion_error("Failed to create Lambda context", {});
        return;
    }
    
    // Create Input structure for math parsing
    math_input = input_create(lambda_context);
    if (!math_input) {
        log_conversion_error("Failed to create math input", {});
        return;
    }
}

void LaTeXRadiantBridge::cleanup_math_integration() {
    if (math_input) {
        input_destroy(math_input);
        math_input = nullptr;
    }
    
    if (lambda_context) {
        context_destroy(lambda_context);
        lambda_context = nullptr;
    }
}

ViewSpan* LaTeXRadiantBridge::create_text_span(const char* text, FontProp* font) {
    if (!text) return nullptr;
    
    ViewSpan* span = (ViewSpan*)pool_calloc(pool, 1, sizeof(ViewSpan));
    if (!span) return nullptr;
    
    // Initialize ViewSpan
    span->type = RDT_VIEW_INLINE;
    span->node = nullptr;
    
    // Create text node
    ViewText* text_node = create_text_node(text);
    if (text_node) {
        span->child = (View*)text_node;
    }
    
    // Apply font if provided
    if (font) {
        span->font = font;
    }
    
    return span;
}

ViewBlock* LaTeXRadiantBridge::create_block_container(const char* css_class) {
    ViewBlock* block = (ViewBlock*)pool_calloc(pool, 1, sizeof(ViewBlock));
    if (!block) return nullptr;
    
    // Initialize ViewBlock
    block->type = RDT_VIEW_BLOCK;
    block->node = nullptr;
    block->parent = nullptr;
    block->child = nullptr;
    block->next = nullptr;
    
    // Apply default styling
    if (css_class) {
        apply_default_styling((View*)block, css_class);
    }
    
    return block;
}

ViewText* LaTeXRadiantBridge::create_text_node(const char* content) {
    if (!content) return nullptr;
    
    ViewText* text = (ViewText*)pool_calloc(pool, 1, sizeof(ViewText));
    if (!text) return nullptr;
    
    // Initialize ViewText
    text->type = RDT_VIEW_TEXT;
    text->node = nullptr;
    text->start_index = 0;
    text->length = strlen(content);
    
    // Store text content (would need proper text storage system)
    // For now, this is a placeholder
    
    return text;
}

void LaTeXRadiantBridge::apply_default_styling(View* view, const char* element_type) {
    if (!view || !element_type) return;
    
    // Apply styling through the engine
    if (view->type == RDT_VIEW_BLOCK) {
        engine->apply_latex_styling((ViewBlock*)view, element_type);
    }
}

// Helper functions
const char* LaTeXRadiantBridge::get_element_tag(Item element) {
    if (element.type == ITEM_ELEMENT && element.item.element) {
        return element.item.element->tag;
    }
    return nullptr;
}

const char* LaTeXRadiantBridge::get_element_content(Item element) {
    if (element.type == ITEM_ELEMENT && element.item.element && element.item.element->content) {
        if (element.item.element->content->type == ITEM_STRING) {
            return element.item.element->content->item.string->data;
        }
    } else if (element.type == ITEM_STRING) {
        return element.item.string->data;
    }
    return nullptr;
}

Array* LaTeXRadiantBridge::get_element_children(Item element) {
    if (element.type == ITEM_ELEMENT && element.item.element && element.item.element->children) {
        return element.item.element->children;
    } else if (element.type == ITEM_ARRAY) {
        return element.item.array;
    }
    return nullptr;
}

bool LaTeXRadiantBridge::is_math_element(Item element) {
    const char* tag = get_element_tag(element);
    return tag && strcmp(tag, "math") == 0;
}

bool LaTeXRadiantBridge::is_display_math(Item element) {
    if (!is_math_element(element)) return false;
    
    // Check for display math attributes or content
    // This would need to be implemented based on LaTeX AST structure
    return false;  // Placeholder
}

void LaTeXRadiantBridge::log_conversion_error(const char* message, Item element) {
    if (message) {
        printf("LaTeX Bridge Error: %s\n", message);
    }
}

bool LaTeXRadiantBridge::validate_element_structure(Item element) {
    // Basic validation - could be expanded
    return element.type != ITEM_NULL;
}

// Placeholder implementations for remaining functions
ViewBlock* LaTeXRadiantBridge::convert_chapter_element(Item element) {
    return convert_section_element(element, 0);  // Chapter is level 0
}

ViewBlock* LaTeXRadiantBridge::convert_subsection_element(Item element, int level) {
    return convert_section_element(element, level);
}

ViewBlock* LaTeXRadiantBridge::convert_abstract_element(Item element) {
    return create_block_container("latex-abstract");
}

ViewBlock* LaTeXRadiantBridge::convert_title_element(Item element) {
    return create_block_container("latex-title");
}

ViewBlock* LaTeXRadiantBridge::convert_list_element(Item element) {
    return create_block_container("latex-list");
}

ViewSpan* LaTeXRadiantBridge::convert_text_element(Item element) {
    const char* content = get_element_content(element);
    return create_text_span(content);
}

ViewSpan* LaTeXRadiantBridge::convert_formatted_text(Item text_element, const char* format_type) {
    ViewSpan* span = convert_text_element(text_element);
    if (span && format_type) {
        apply_text_formatting(span, format_type);
    }
    return span;
}

ViewSpan* LaTeXRadiantBridge::apply_text_formatting(ViewSpan* span, const char* latex_command) {
    return engine->apply_font_styling(span, latex_command);
}

void LaTeXRadiantBridge::apply_latex_attributes(View* view, Item element) {
    // Placeholder for attribute processing
}

void LaTeXRadiantBridge::setup_document_metadata(ViewTree* tree, Item document) {
    // Placeholder for metadata setup
}

void LaTeXRadiantBridge::process_latex_preamble(Item preamble, TypesetOptions* options) {
    // Placeholder for preamble processing
}

// C interface functions
extern "C" {

ViewTree* latex_bridge_convert_ast(RadiantTypesetEngine* engine, Item latex_ast) {
    if (!engine) return nullptr;
    
    LaTeXRadiantBridge bridge(engine);
    return bridge.convert_latex_ast_to_radiant(latex_ast);
}

ViewSpan* latex_bridge_process_math(RadiantTypesetEngine* engine, const char* math_content, bool is_display) {
    if (!engine || !math_content) return nullptr;
    
    LaTeXRadiantBridge bridge(engine);
    return bridge.process_math_expression(math_content, is_display);
}

bool latex_bridge_is_math_element(Item element) {
    LaTeXRadiantBridge bridge(nullptr);  // Static method, no engine needed
    return bridge.is_math_element(element);
}

bool latex_bridge_is_section_element(Item element) {
    const char* tag = nullptr;
    if (element.type == ITEM_ELEMENT && element.item.element) {
        tag = element.item.element->tag;
    }
    
    return tag && (strcmp(tag, "section") == 0 || 
                   strcmp(tag, "subsection") == 0 || 
                   strcmp(tag, "chapter") == 0);
}

bool latex_bridge_is_table_element(Item element) {
    const char* tag = nullptr;
    if (element.type == ITEM_ELEMENT && element.item.element) {
        tag = element.item.element->tag;
    }
    
    return tag && (strcmp(tag, "table") == 0 || strcmp(tag, "tabular") == 0);
}

} // extern "C"
