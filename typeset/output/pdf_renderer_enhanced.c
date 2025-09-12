#include "pdf_renderer_enhanced.h"
#include "../../lib/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Enhanced PDF renderer implementation with advanced typography and layout

// =============================================================================
// Enhanced Font Management
// =============================================================================

bool pdf_load_enhanced_fonts(PDFRendererEnhanced* renderer) {
    if (!renderer || !renderer->base.pdf_doc) {
        return false;
    }
    
    HPDF_Doc doc = renderer->base.pdf_doc;
    
    // Load serif font family (Times)
    renderer->fonts.serif.regular = HPDF_GetFont(doc, "Times-Roman", NULL);
    renderer->fonts.serif.bold = HPDF_GetFont(doc, "Times-Bold", NULL);
    renderer->fonts.serif.italic = HPDF_GetFont(doc, "Times-Italic", NULL);
    renderer->fonts.serif.bold_italic = HPDF_GetFont(doc, "Times-BoldItalic", NULL);
    renderer->fonts.serif.family_name = strdup("Times");
    
    // Load sans-serif font family (Helvetica)
    renderer->fonts.sans_serif.regular = HPDF_GetFont(doc, "Helvetica", NULL);
    renderer->fonts.sans_serif.bold = HPDF_GetFont(doc, "Helvetica-Bold", NULL);
    renderer->fonts.sans_serif.italic = HPDF_GetFont(doc, "Helvetica-Oblique", NULL);
    renderer->fonts.sans_serif.bold_italic = HPDF_GetFont(doc, "Helvetica-BoldOblique", NULL);
    renderer->fonts.sans_serif.family_name = strdup("Helvetica");
    
    // Load monospace font family (Courier)
    renderer->fonts.monospace.regular = HPDF_GetFont(doc, "Courier", NULL);
    renderer->fonts.monospace.bold = HPDF_GetFont(doc, "Courier-Bold", NULL);
    renderer->fonts.monospace.italic = HPDF_GetFont(doc, "Courier-Oblique", NULL);
    renderer->fonts.monospace.bold_italic = HPDF_GetFont(doc, "Courier-BoldOblique", NULL);
    renderer->fonts.monospace.family_name = strdup("Courier");
    
    // For now, use serif fonts for math (in a real implementation, we'd load proper math fonts)
    renderer->fonts.math.regular = renderer->fonts.serif.italic;
    renderer->fonts.math.bold = renderer->fonts.serif.bold_italic;
    renderer->fonts.math.italic = renderer->fonts.serif.italic;
    renderer->fonts.math.bold_italic = renderer->fonts.serif.bold_italic;
    renderer->fonts.math.family_name = strdup("Times-Math");
    
    log_info("Enhanced font families loaded successfully");
    return true;
}

HPDF_Font pdf_get_font_variant(PDFRendererEnhanced* renderer, const char* family, 
                               bool bold, bool italic) {
    if (!renderer || !family) return NULL;
    
    PDFFontFamily* font_family = NULL;
    
    // Select font family
    if (strstr(family, "serif") || strstr(family, "Times") || strstr(family, "Computer Modern")) {
        font_family = &renderer->fonts.serif;
    } else if (strstr(family, "sans") || strstr(family, "Helvetica") || strstr(family, "Arial")) {
        font_family = &renderer->fonts.sans_serif;
    } else if (strstr(family, "mono") || strstr(family, "Courier") || strstr(family, "typewriter")) {
        font_family = &renderer->fonts.monospace;
    } else if (strstr(family, "math")) {
        font_family = &renderer->fonts.math;
    } else {
        // Default to serif
        font_family = &renderer->fonts.serif;
    }
    
    // Select variant
    if (bold && italic) {
        return font_family->bold_italic;
    } else if (bold) {
        return font_family->bold;
    } else if (italic) {
        return font_family->italic;
    } else {
        return font_family->regular;
    }
}

bool pdf_set_font_enhanced(PDFRendererEnhanced* renderer, const char* family, 
                          double size, bool bold, bool italic) {
    if (!renderer || !renderer->base.current_page) {
        return false;
    }
    
    HPDF_Font font = pdf_get_font_variant(renderer, family, bold, italic);
    if (!font) {
        log_warn("Failed to get font variant for %s (bold=%d, italic=%d)", 
                 family, bold, italic);
        return false;
    }
    
    HPDF_Page_SetFontAndSize(renderer->base.current_page, font, (float)size);
    renderer->base.current_font = font;
    renderer->base.line_height = size * renderer->line_spacing;
    
    return true;
}

// =============================================================================
// Enhanced Renderer Creation and Management
// =============================================================================

PDFRendererEnhanced* pdf_renderer_enhanced_create(PDFRenderOptions* options) {
    PDFRendererEnhanced* renderer = (PDFRendererEnhanced*)calloc(1, sizeof(PDFRendererEnhanced));
    if (!renderer) {
        log_error("Failed to allocate enhanced PDF renderer");
        return NULL;
    }
    
    // Initialize base renderer
    PDFRenderer* base = pdf_renderer_create(options);
    if (!base) {
        log_error("Failed to create base PDF renderer");
        free(renderer);
        return NULL;
    }
    
    // Copy base renderer data
    memcpy(&renderer->base, base, sizeof(PDFRenderer));
    
    // Free the separate base renderer allocation (we copied the data)
    free(base);
    
    // Initialize enhanced font management
    if (!pdf_load_enhanced_fonts(renderer)) {
        log_error("Failed to load enhanced fonts");
        pdf_renderer_enhanced_destroy(renderer);
        return NULL;
    }
    
    // Initialize rendering contexts
    renderer->text_ctx.x = 72.0;           // Start at 1 inch margin
    renderer->text_ctx.y = 72.0;           // Start at 1 inch margin
    renderer->text_ctx.line_height = 12.0;
    renderer->text_ctx.paragraph_indent = 0.0;
    renderer->text_ctx.left_margin = 72.0;
    renderer->text_ctx.right_margin = 72.0;
    renderer->text_ctx.available_width = 451.0; // Letter width minus margins
    renderer->text_ctx.at_paragraph_start = true;
    renderer->text_ctx.alignment = 0; // Left align
    
    renderer->list_ctx.type = 0;
    renderer->list_ctx.level = 0;
    renderer->list_ctx.item_number = 0;
    renderer->list_ctx.indent = 0.0;
    renderer->list_ctx.bullet_width = 20.0;
    renderer->list_ctx.bullet_style = strdup("•");
    
    // Initialize typography settings
    renderer->base_font_size = 10.0;
    renderer->line_spacing = 1.2;       // 20% leading
    renderer->paragraph_spacing = 6.0;  // 6 points between paragraphs
    
    // Initialize page dimensions (Letter size default)
    renderer->page_width = 612.0;
    renderer->page_height = 792.0;
    renderer->content_x = 72.0;
    renderer->content_y = 72.0;
    renderer->content_width = 468.0;    // Page width - 2 * margin
    renderer->content_height = 648.0;   // Page height - 2 * margin
    
    // Initialize math settings
    renderer->math_mode = false;
    renderer->math_axis_height = 2.5;   // Typical math axis height
    
    log_info("Enhanced PDF renderer created successfully");
    return renderer;
}

void pdf_renderer_enhanced_destroy(PDFRendererEnhanced* renderer) {
    if (!renderer) return;
    
    // Clean up font family names
    if (renderer->fonts.serif.family_name) free(renderer->fonts.serif.family_name);
    if (renderer->fonts.sans_serif.family_name) free(renderer->fonts.sans_serif.family_name);
    if (renderer->fonts.monospace.family_name) free(renderer->fonts.monospace.family_name);
    if (renderer->fonts.math.family_name) free(renderer->fonts.math.family_name);
    
    // Clean up context strings
    if (renderer->list_ctx.bullet_style) free(renderer->list_ctx.bullet_style);
    
    // Clean up table context
    if (renderer->table_ctx.col_widths) free(renderer->table_ctx.col_widths);
    if (renderer->table_ctx.row_heights) free(renderer->table_ctx.row_heights);
    
    // Clean up base renderer (this will clean up the PDF document)
    pdf_renderer_destroy(&renderer->base);
    
    free(renderer);
    log_info("Enhanced PDF renderer destroyed");
}

// =============================================================================
// Enhanced Text Rendering
// =============================================================================

bool pdf_render_text_run_enhanced(PDFRendererEnhanced* renderer, ViewTextRun* text_run) {
    if (!renderer || !renderer->base.current_page || !text_run || !text_run->text) {
        return false;
    }
    
    // Set font if specified in text run
    if (text_run->font) {
        // TODO: Extract font properties from ViewFont structure
        pdf_set_font_enhanced(renderer, "serif", text_run->font_size, false, false);
    }
    
    double x = renderer->text_ctx.x;
    double y = pdf_convert_y(&renderer->base, renderer->text_ctx.y);
    
    // Apply color if specified
    if (text_run->color.a > 0) {
        HPDF_Page_SetRGBFill(renderer->base.current_page, 
                             (float)text_run->color.r, 
                             (float)text_run->color.g, 
                             (float)text_run->color.b);
    }
    
    // Render text
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)y, text_run->text);
    HPDF_Page_EndText(renderer->base.current_page);
    
    // Update position
    double text_width = text_run->total_width > 0 ? text_run->total_width : 
                       pdf_measure_text_width_enhanced(renderer, text_run->text, "serif", 
                                                      text_run->font_size, false, false);
    renderer->text_ctx.x += text_width;
    
    log_debug("Enhanced text rendered: '%s' at (%.1f, %.1f)", text_run->text, x, y);
    return true;
}

bool pdf_render_paragraph_enhanced(PDFRendererEnhanced* renderer, ViewNode* paragraph) {
    if (!renderer || !paragraph) return false;
    
    log_debug("Rendering enhanced paragraph");
    
    // Apply paragraph indent if at start of paragraph
    if (renderer->text_ctx.at_paragraph_start && renderer->text_ctx.paragraph_indent > 0) {
        renderer->text_ctx.x += renderer->text_ctx.paragraph_indent;
    }
    
    // Render paragraph children
    ViewNode* child = paragraph->first_child;
    while (child) {
        pdf_render_view_node_enhanced(renderer, child);
        child = child->next_sibling;
    }
    
    // End paragraph - advance to next line with paragraph spacing
    pdf_new_paragraph_enhanced(renderer);
    
    return true;
}

bool pdf_render_section_heading_enhanced(PDFRendererEnhanced* renderer, ViewNode* heading, int level) {
    if (!renderer || !heading) return false;
    
    log_debug("Rendering section heading at level %d", level);
    
    // Calculate font size based on heading level
    double font_size = renderer->base_font_size;
    switch (level) {
        case 1: font_size *= 1.8; break;  // Section
        case 2: font_size *= 1.5; break;  // Subsection
        case 3: font_size *= 1.3; break;  // Subsubsection
        case 4: font_size *= 1.1; break;  // Paragraph
        default: font_size *= 1.0; break; // Subparagraph
    }
    
    // Set heading font (bold)
    pdf_set_font_enhanced(renderer, "serif", font_size, true, false);
    
    // Add some space before heading
    pdf_new_paragraph_enhanced(renderer);
    
    // Render heading content
    ViewNode* child = heading->first_child;
    while (child) {
        pdf_render_view_node_enhanced(renderer, child);
        child = child->next_sibling;
    }
    
    // Add space after heading
    pdf_new_paragraph_enhanced(renderer);
    
    // Reset to body font
    pdf_set_font_enhanced(renderer, "serif", renderer->base_font_size, false, false);
    
    return true;
}

// =============================================================================
// Enhanced List Rendering
// =============================================================================

bool pdf_render_list_enhanced(PDFRendererEnhanced* renderer, ViewNode* list) {
    if (!renderer || !list) return false;
    
    log_debug("Rendering enhanced list");
    
    // Save current list context
    PDFListContext saved_ctx = renderer->list_ctx;
    
    // Update list context
    renderer->list_ctx.level++;
    renderer->list_ctx.indent = 20.0 * renderer->list_ctx.level;
    renderer->list_ctx.item_number = 0;
    
    // Add space before list
    pdf_new_line_enhanced(renderer);
    
    // Render list items
    ViewNode* child = list->first_child;
    while (child) {
        if (strcmp(child->semantic_role ? child->semantic_role : "", "list-item") == 0) {
            pdf_render_list_item_enhanced(renderer, child, &renderer->list_ctx);
        }
        child = child->next_sibling;
    }
    
    // Add space after list
    pdf_new_line_enhanced(renderer);
    
    // Restore list context
    renderer->list_ctx = saved_ctx;
    
    return true;
}

bool pdf_render_list_item_enhanced(PDFRendererEnhanced* renderer, ViewNode* item, PDFListContext* ctx) {
    if (!renderer || !item || !ctx) return false;
    
    log_debug("Rendering list item at level %d", ctx->level);
    
    // Save current position
    double saved_x = renderer->text_ctx.x;
    
    // Move to list indent
    renderer->text_ctx.x = renderer->text_ctx.left_margin + ctx->indent;
    
    // Draw bullet/number
    ctx->item_number++;
    pdf_draw_list_bullet(renderer, ctx, renderer->text_ctx.x, renderer->text_ctx.y);
    
    // Move past bullet area
    renderer->text_ctx.x += ctx->bullet_width;
    
    // Render item content
    ViewNode* child = item->first_child;
    while (child) {
        pdf_render_view_node_enhanced(renderer, child);
        child = child->next_sibling;
    }
    
    // New line for next item
    pdf_new_line_enhanced(renderer);
    
    // Restore left margin
    renderer->text_ctx.x = saved_x;
    
    return true;
}

bool pdf_draw_list_bullet(PDFRendererEnhanced* renderer, PDFListContext* ctx, double x, double y) {
    if (!renderer || !ctx || !renderer->base.current_page) return false;
    
    char bullet_text[10];
    
    // Generate bullet text based on list type
    switch (ctx->type) {
        case 0: // ITEMIZE
            strcpy(bullet_text, ctx->bullet_style ? ctx->bullet_style : "•");
            break;
        case 1: // ENUMERATE
            snprintf(bullet_text, sizeof(bullet_text), "%d.", ctx->item_number);
            break;
        case 2: // DESCRIPTION
            strcpy(bullet_text, "→");
            break;
        default:
            strcpy(bullet_text, "•");
    }
    
    double pdf_y = pdf_convert_y(&renderer->base, y);
    
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)pdf_y, bullet_text);
    HPDF_Page_EndText(renderer->base.current_page);
    
    log_debug("Drew list bullet: '%s' at (%.1f, %.1f)", bullet_text, x, y);
    return true;
}

// =============================================================================
// Enhanced Math Rendering
// =============================================================================

bool pdf_render_math_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* math) {
    if (!renderer || !math) return false;
    
    log_debug("Rendering enhanced math element of type %d", math->type);
    
    // Set math font
    pdf_set_font_enhanced(renderer, "serif", renderer->base_font_size, false, true);
    
    // Save math mode
    bool saved_math_mode = renderer->math_mode;
    renderer->math_mode = true;
    
    switch (math->type) {
        case VIEW_MATH_ATOM:
            if (math->content.atom.symbol) {
                double x = renderer->text_ctx.x;
                double y = pdf_convert_y(&renderer->base, renderer->text_ctx.y);
                
                HPDF_Page_BeginText(renderer->base.current_page);
                HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)y, 
                                 math->content.atom.symbol);
                HPDF_Page_EndText(renderer->base.current_page);
                
                // Estimate width (in real implementation, would measure properly)
                renderer->text_ctx.x += strlen(math->content.atom.symbol) * renderer->base_font_size * 0.6;
            }
            break;
            
        case VIEW_MATH_FRACTION:
            pdf_render_math_fraction_enhanced(renderer, math);
            break;
            
        case VIEW_MATH_SUPERSCRIPT:
            pdf_render_math_superscript_enhanced(renderer, math);
            break;
            
        case VIEW_MATH_SUBSCRIPT:
            pdf_render_math_subscript_enhanced(renderer, math);
            break;
            
        default:
            // Render as placeholder
            double x = renderer->text_ctx.x;
            double y = pdf_convert_y(&renderer->base, renderer->text_ctx.y);
            
            HPDF_Page_BeginText(renderer->base.current_page);
            HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)y, "⟨math⟩");
            HPDF_Page_EndText(renderer->base.current_page);
            
            renderer->text_ctx.x += 50.0; // Placeholder width
    }
    
    // Restore math mode
    renderer->math_mode = saved_math_mode;
    
    return true;
}

bool pdf_render_math_fraction_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* fraction) {
    if (!renderer || !fraction) return false;
    
    log_debug("Rendering math fraction");
    
    // For now, render as "num/denom" (simplified)
    double x = renderer->text_ctx.x;
    double y = pdf_convert_y(&renderer->base, renderer->text_ctx.y);
    
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)y, "a/b");
    HPDF_Page_EndText(renderer->base.current_page);
    
    renderer->text_ctx.x += 30.0; // Estimate width
    
    return true;
}

bool pdf_render_math_superscript_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* superscript) {
    if (!renderer || !superscript) return false;
    
    log_debug("Rendering math superscript");
    
    // Render base with smaller superscript (simplified)
    double x = renderer->text_ctx.x;
    double y = pdf_convert_y(&renderer->base, renderer->text_ctx.y);
    
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)y, "x");
    HPDF_Page_EndText(renderer->base.current_page);
    
    // Render superscript
    pdf_set_font_enhanced(renderer, "serif", renderer->base_font_size * 0.7, false, true);
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)(x + 10), (float)(y + 5), "2");
    HPDF_Page_EndText(renderer->base.current_page);
    
    renderer->text_ctx.x += 20.0; // Estimate width
    
    return true;
}

bool pdf_render_math_subscript_enhanced(PDFRendererEnhanced* renderer, ViewMathElement* subscript) {
    if (!renderer || !subscript) return false;
    
    log_debug("Rendering math subscript");
    
    // Similar to superscript but positioned lower
    double x = renderer->text_ctx.x;
    double y = pdf_convert_y(&renderer->base, renderer->text_ctx.y);
    
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)x, (float)y, "x");
    HPDF_Page_EndText(renderer->base.current_page);
    
    // Render subscript
    pdf_set_font_enhanced(renderer, "serif", renderer->base_font_size * 0.7, false, true);
    HPDF_Page_BeginText(renderer->base.current_page);
    HPDF_Page_TextOut(renderer->base.current_page, (float)(x + 10), (float)(y - 3), "i");
    HPDF_Page_EndText(renderer->base.current_page);
    
    renderer->text_ctx.x += 20.0; // Estimate width
    
    return true;
}

// =============================================================================
// Enhanced Layout and Positioning
// =============================================================================

void pdf_advance_position_enhanced(PDFRendererEnhanced* renderer, double dx, double dy) {
    if (!renderer) return;
    
    renderer->text_ctx.x += dx;
    renderer->text_ctx.y += dy;
}

void pdf_new_line_enhanced(PDFRendererEnhanced* renderer) {
    if (!renderer) return;
    
    renderer->text_ctx.x = renderer->text_ctx.left_margin;
    renderer->text_ctx.y += renderer->text_ctx.line_height;
    renderer->text_ctx.at_paragraph_start = false;
}

void pdf_new_paragraph_enhanced(PDFRendererEnhanced* renderer) {
    if (!renderer) return;
    
    renderer->text_ctx.x = renderer->text_ctx.left_margin;
    renderer->text_ctx.y += renderer->text_ctx.line_height + renderer->paragraph_spacing;
    renderer->text_ctx.at_paragraph_start = true;
}

bool pdf_check_page_break_enhanced(PDFRendererEnhanced* renderer, double needed_height) {
    if (!renderer) return false;
    
    double remaining_height = renderer->page_height - renderer->text_ctx.y - 72.0; // Bottom margin
    
    if (remaining_height < needed_height) {
        log_debug("Page break needed: remaining=%.1f, needed=%.1f", remaining_height, needed_height);
        return true;
    }
    
    return false;
}

// =============================================================================
// Enhanced View Tree Rendering
// =============================================================================

bool pdf_render_view_tree_enhanced(PDFRendererEnhanced* renderer, ViewTree* tree) {
    if (!renderer || !tree) {
        log_error("Invalid renderer or tree for enhanced rendering");
        return false;
    }
    
    log_info("Starting enhanced PDF rendering of %d pages", tree->page_count);
    
    // Render each page with enhanced processing
    for (int i = 0; i < tree->page_count; i++) {
        if (!pdf_render_page_enhanced(renderer, tree->pages[i])) {
            log_error("Failed to render enhanced page %d", i + 1);
            return false;
        }
    }
    
    log_info("Enhanced PDF rendering completed successfully");
    return true;
}

bool pdf_render_view_node_enhanced(PDFRendererEnhanced* renderer, ViewNode* node) {
    if (!renderer || !node) return false;
    
    log_debug("Rendering enhanced view node type: %d", node->type);
    
    // Route to specific enhanced rendering functions based on node type and semantic role
    if (node->semantic_role) {
        if (strcmp(node->semantic_role, "section") == 0) {
            return pdf_render_section_heading_enhanced(renderer, node, 1);
        } else if (strcmp(node->semantic_role, "subsection") == 0) {
            return pdf_render_section_heading_enhanced(renderer, node, 2);
        } else if (strcmp(node->semantic_role, "paragraph") == 0) {
            return pdf_render_paragraph_enhanced(renderer, node);
        } else if (strcmp(node->semantic_role, "list") == 0) {
            return pdf_render_list_enhanced(renderer, node);
        }
    }
    
    // Fallback to node type-based rendering
    switch (node->type) {
        case VIEW_NODE_TEXT_RUN:
            if (node->content.text_run) {
                return pdf_render_text_run_enhanced(renderer, node->content.text_run);
            }
            break;
            
        case VIEW_NODE_MATH_ELEMENT:
            if (node->content.math_elem) {
                return pdf_render_math_enhanced(renderer, node->content.math_elem);
            }
            break;
            
        case VIEW_NODE_BLOCK:
        case VIEW_NODE_INLINE:
        case VIEW_NODE_GROUP:
        case VIEW_NODE_DOCUMENT:
            // Render children
            {
                ViewNode* child = node->first_child;
                while (child) {
                    pdf_render_view_node_enhanced(renderer, child);
                    child = child->next_sibling;
                }
            }
            return true;
            
        default:
            log_debug("Enhanced rendering for node type %d not implemented", node->type);
            return true;
    }
    
    return false;
}

bool pdf_render_page_enhanced(PDFRendererEnhanced* renderer, ViewPage* page) {
    if (!renderer || !page) return false;
    
    log_info("Rendering enhanced page %d (%.1f x %.1f)", 
             page->page_number, page->page_size.width, page->page_size.height);
    
    // Start new PDF page with enhanced setup
    if (!pdf_start_page(&renderer->base, page->page_size.width, page->page_size.height)) {
        return false;
    }
    
    // Reset text context for new page
    renderer->text_ctx.x = page->content_area.origin.x;
    renderer->text_ctx.y = page->content_area.origin.y;
    renderer->text_ctx.available_width = page->content_area.size.width;
    renderer->text_ctx.at_paragraph_start = true;
    
    // Set default enhanced font
    pdf_set_font_enhanced(renderer, "serif", renderer->base_font_size, false, false);
    
    // Render page content with enhanced processing
    if (page->page_node) {
        if (!pdf_render_view_node_enhanced(renderer, page->page_node)) {
            log_warn("Failed to render enhanced page node for page %d", page->page_number);
        }
    }
    
    // End page
    pdf_end_page(&renderer->base);
    
    return true;
}

// =============================================================================
// Utility Functions
// =============================================================================

double pdf_measure_text_width_enhanced(PDFRendererEnhanced* renderer, const char* text, 
                                      const char* font_family, double font_size, 
                                      bool bold, bool italic) {
    if (!renderer || !text) return 0.0;
    
    HPDF_Font font = pdf_get_font_variant(renderer, font_family, bold, italic);
    if (!font) return 0.0;
    
    // Simplified width calculation (in real implementation would use proper text metrics)
    return strlen(text) * font_size * 0.6;
}

double pdf_get_font_line_height_enhanced(PDFRendererEnhanced* renderer, double font_size) {
    if (!renderer) return font_size;
    
    return font_size * renderer->line_spacing;
}

void pdf_set_text_alignment_enhanced(PDFRendererEnhanced* renderer, int alignment) {
    if (!renderer) return;
    
    renderer->text_ctx.alignment = alignment;
}
