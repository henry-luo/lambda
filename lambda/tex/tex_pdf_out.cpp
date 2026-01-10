// tex_pdf_out.cpp - PDF Output Generation Implementation
//
// Converts TeX node trees to PDF using the Lambda pdf_writer library.

#include "tex_pdf_out.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cmath>

namespace tex {

// ============================================================================
// File Management
// ============================================================================

bool pdf_open(PDFWriter& writer, const char* filename, const PDFParams& params) {
    writer.params = params;
    writer.page_count = 0;
    writer.x = writer.y = 0;
    writer.current_font = nullptr;
    writer.current_size = 0;

    // Allocate font table
    writer.font_capacity = 16;
    writer.fonts = (PDFFontEntry*)arena_alloc(writer.arena,
        writer.font_capacity * sizeof(PDFFontEntry));
    writer.font_count = 0;

    // Create PDF document
    writer.doc = HPDF_New(nullptr, nullptr);
    if (!writer.doc) {
        log_error("tex_pdf_out: failed to create PDF document");
        return false;
    }

    // Set compression
    if (params.use_compression) {
        HPDF_SetCompressionMode(writer.doc, HPDF_COMP_ALL);
    }

    // Set metadata
    if (params.title) {
        HPDF_SetInfoAttr(writer.doc, HPDF_INFO_TITLE, params.title);
    }
    if (params.author) {
        HPDF_SetInfoAttr(writer.doc, HPDF_INFO_AUTHOR, params.author);
    }
    if (params.subject) {
        HPDF_SetInfoAttr(writer.doc, HPDF_INFO_SUBJECT, params.subject);
    }
    if (params.creator) {
        HPDF_SetInfoAttr(writer.doc, HPDF_INFO_CREATOR, params.creator);
    }
    HPDF_SetInfoAttr(writer.doc, HPDF_INFO_PRODUCER, "Lambda Script TeX Engine");

    // Store filename for saving later
    writer.params = params;

    return true;
}

bool pdf_close(PDFWriter& writer) {
    if (!writer.doc) return false;

    // Note: We don't save here - caller should have saved with pdf_save_to_file
    HPDF_Free(writer.doc);
    writer.doc = nullptr;
    writer.page = nullptr;

    log_debug("tex_pdf_out: closed document with %d pages", writer.page_count);
    return true;
}

// Save to file (must be called before close)
static bool pdf_save_to_file(PDFWriter& writer, const char* filename) {
    if (!writer.doc) return false;

    HPDF_STATUS status = HPDF_SaveToFile(writer.doc, filename);
    if (status != HPDF_OK) {
        log_error("tex_pdf_out: failed to save PDF to %s (status=%lu)", filename, status);
        return false;
    }

    log_debug("tex_pdf_out: saved to %s", filename);
    return true;
}

// ============================================================================
// Page Commands
// ============================================================================

void pdf_begin_page(PDFWriter& writer) {
    writer.page = HPDF_AddPage(writer.doc);
    if (!writer.page) {
        log_error("tex_pdf_out: failed to add page");
        return;
    }

    // Set page size
    HPDF_Page_SetWidth(writer.page, writer.params.page_width);
    HPDF_Page_SetHeight(writer.page, writer.params.page_height);

    // Initialize position at top-left margin
    writer.x = writer.params.margin_left;
    writer.y = writer.params.margin_top;

    // Reset font state for new page
    writer.current_font = nullptr;
    writer.current_size = 0;

    writer.page_count++;
}

void pdf_end_page(PDFWriter& writer) {
    // Page is automatically finalized in PDF
    writer.page = nullptr;
}

// ============================================================================
// Font Commands
// ============================================================================

HPDF_Font pdf_get_font(PDFWriter& writer, const char* tex_name, float size_pt) {
    // Check existing fonts
    for (int i = 0; i < writer.font_count; ++i) {
        if (strcmp(writer.fonts[i].tex_name, tex_name) == 0 &&
            fabs(writer.fonts[i].size_pt - size_pt) < 0.01f) {
            return writer.fonts[i].handle;
        }
    }

    // Map TeX font to PDF font
    const char* pdf_name = map_tex_font_to_pdf(tex_name);

    // Get font handle from PDF library
    HPDF_Font handle = HPDF_GetFont(writer.doc, pdf_name, nullptr);
    if (!handle) {
        // Fallback to default font
        handle = HPDF_GetFont(writer.doc, writer.params.default_font, nullptr);
        if (!handle) {
            log_error("tex_pdf_out: failed to get font %s", pdf_name);
            return nullptr;
        }
    }

    // Grow array if needed
    if (writer.font_count >= writer.font_capacity) {
        int new_cap = writer.font_capacity * 2;
        PDFFontEntry* new_fonts = (PDFFontEntry*)arena_alloc(writer.arena,
            new_cap * sizeof(PDFFontEntry));
        memcpy(new_fonts, writer.fonts, writer.font_count * sizeof(PDFFontEntry));
        writer.fonts = new_fonts;
        writer.font_capacity = new_cap;
    }

    // Add entry
    PDFFontEntry& f = writer.fonts[writer.font_count++];
    f.tex_name = tex_name;
    f.pdf_name = pdf_name;
    f.size_pt = size_pt;
    f.handle = handle;

    return handle;
}

void pdf_select_font(PDFWriter& writer, const char* tex_name, float size_pt) {
    if (!writer.page) return;

    HPDF_Font font = pdf_get_font(writer, tex_name, size_pt);
    if (font) {
        HPDF_Page_SetFontAndSize(writer.page, font, size_pt);
        writer.current_size = size_pt;
    }
}

// ============================================================================
// Drawing Commands
// ============================================================================

void pdf_set_position(PDFWriter& writer, float x, float y) {
    writer.x = x + writer.params.margin_left;
    writer.y = y + writer.params.margin_top;
}

void pdf_draw_char(PDFWriter& writer, int32_t codepoint) {
    if (!writer.page) return;

    // Convert single character to string
    char buf[8] = {0};
    if (codepoint < 128) {
        buf[0] = (char)codepoint;
    } else {
        // For now, handle ASCII only
        // TODO: UTF-8 encoding for higher codepoints
        buf[0] = '?';
    }

    // Convert TeX coordinates to PDF coordinates
    float pdf_y = tex_y_to_pdf(writer.y, writer.params.page_height);

    HPDF_Page_BeginText(writer.page);
    HPDF_Page_TextOut(writer.page, writer.x, pdf_y, buf);
    HPDF_Page_EndText(writer.page);
}

void pdf_draw_text(PDFWriter& writer, const char* text) {
    if (!writer.page || !text) return;

    float pdf_y = tex_y_to_pdf(writer.y, writer.params.page_height);

    HPDF_Page_BeginText(writer.page);
    HPDF_Page_TextOut(writer.page, writer.x, pdf_y, text);
    HPDF_Page_EndText(writer.page);
}

void pdf_draw_rule(PDFWriter& writer, float x, float y, float width, float height) {
    if (!writer.page) return;

    // Convert TeX coordinates (y from top, rule extends down)
    float pdf_x = x + writer.params.margin_left;
    float pdf_y = tex_y_to_pdf(y + height, writer.params.page_height);

    HPDF_Page_Rectangle(writer.page, pdf_x, pdf_y, width, height);
    HPDF_Page_Fill(writer.page);
}

void pdf_move_right(PDFWriter& writer, float amount) {
    writer.x += amount;
}

void pdf_move_down(PDFWriter& writer, float amount) {
    writer.y += amount;
}

// ============================================================================
// Graphics State
// ============================================================================

void pdf_gsave(PDFWriter& writer) {
    if (writer.page) {
        HPDF_Page_GSave(writer.page);
    }
}

void pdf_grestore(PDFWriter& writer) {
    if (writer.page) {
        HPDF_Page_GRestore(writer.page);
    }
}

void pdf_set_fill_color(PDFWriter& writer, float r, float g, float b) {
    if (writer.page) {
        HPDF_Page_SetRGBFill(writer.page, r, g, b);
    }
}

void pdf_set_stroke_color(PDFWriter& writer, float r, float g, float b) {
    if (writer.page) {
        HPDF_Page_SetRGBStroke(writer.page, r, g, b);
    }
}

// ============================================================================
// Node Tree Traversal
// ============================================================================

void pdf_output_node(PDFWriter& writer, TexNode* node, TFMFontManager* fonts) {
    if (!node) return;

    switch (node->node_class) {
        case NodeClass::Char: {
            // Select font
            const char* font_name = node->content.ch.font.name;
            float font_size = node->content.ch.font.size_pt;
            if (font_name) {
                pdf_select_font(writer, font_name, font_size);
            }

            pdf_draw_char(writer, node->content.ch.codepoint);

            // Advance by character width
            writer.x += node->width;
            break;
        }

        case NodeClass::Ligature: {
            const char* font_name = node->content.lig.font.name;
            float font_size = node->content.lig.font.size_pt;
            if (font_name) {
                pdf_select_font(writer, font_name, font_size);
            }

            pdf_draw_char(writer, node->content.lig.codepoint);
            writer.x += node->width;
            break;
        }

        case NodeClass::Glue: {
            // Glue becomes fixed space after layout
            writer.x += node->width;
            break;
        }

        case NodeClass::Kern: {
            writer.x += node->content.kern.amount;
            break;
        }

        case NodeClass::Rule: {
            pdf_draw_rule(writer, writer.x - writer.params.margin_left,
                         writer.y - writer.params.margin_top,
                         node->width, node->height + node->depth);
            writer.x += node->width;
            break;
        }

        case NodeClass::HList:
        case NodeClass::HBox: {
            pdf_output_hlist(writer, node, fonts);
            break;
        }

        case NodeClass::VList:
        case NodeClass::VBox: {
            pdf_output_vlist(writer, node, fonts);
            break;
        }

        case NodeClass::Penalty:
            // Invisible
            break;

        default:
            break;
    }
}

void pdf_output_hlist(PDFWriter& writer, TexNode* hlist, TFMFontManager* fonts) {
    if (!hlist) return;

    // Save position
    float save_x = writer.x;
    float save_y = writer.y;

    // Process children left to right
    for (TexNode* child = hlist->first_child; child; child = child->next_sibling) {
        pdf_output_node(writer, child, fonts);
    }

    // Restore Y, advance X by box width
    writer.y = save_y;
    writer.x = save_x + hlist->width;
}

void pdf_output_vlist(PDFWriter& writer, TexNode* vlist, TFMFontManager* fonts) {
    if (!vlist) return;

    // Save position
    float save_x = writer.x;
    float save_y = writer.y;

    // Process children top to bottom
    for (TexNode* child = vlist->first_child; child; child = child->next_sibling) {
        // Move down by child's height (to baseline)
        writer.y += child->height;

        if (child->node_class == NodeClass::Glue) {
            // Vertical glue
            writer.y += child->content.glue.spec.space;
        } else if (child->node_class == NodeClass::Kern) {
            writer.y += child->content.kern.amount;
        } else if (child->node_class == NodeClass::HBox || child->node_class == NodeClass::HList) {
            // Output horizontal content
            float hlist_x = writer.x;
            for (TexNode* item = child->first_child; item; item = item->next_sibling) {
                pdf_output_node(writer, item, fonts);
            }
            writer.x = hlist_x;  // Reset X

            // Move past depth
            writer.y += child->depth;
        } else if (child->node_class == NodeClass::Rule) {
            pdf_draw_rule(writer,
                         writer.x - writer.params.margin_left,
                         writer.y - child->height - writer.params.margin_top,
                         child->width, child->height + child->depth);
            writer.y += child->depth;
        } else {
            pdf_output_node(writer, child, fonts);
            writer.y += child->depth;
        }
    }

    // Restore X position
    writer.x = save_x;
}

// ============================================================================
// High-Level API
// ============================================================================

bool pdf_write_page(
    PDFWriter& writer,
    TexNode* page_vlist,
    int page_number,
    TFMFontManager* fonts
) {
    if (!page_vlist) return false;

    pdf_begin_page(writer);

    // Start at top-left margin (already set by begin_page)
    pdf_output_vlist(writer, page_vlist, fonts);

    pdf_end_page(writer);

    return true;
}

bool pdf_write_document(
    PDFWriter& writer,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts
) {
    for (int i = 0; i < page_count; ++i) {
        if (!pdf_write_page(writer, pages[i].vlist, i + 1, fonts)) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Convenience Functions
// ============================================================================

bool write_pdf_file(
    const char* filename,
    PageContent* pages,
    int page_count,
    TFMFontManager* fonts,
    Arena* arena,
    const PDFParams& params
) {
    PDFWriter writer(arena);

    if (!pdf_open(writer, filename, params)) {
        return false;
    }

    bool success = pdf_write_document(writer, pages, page_count, fonts);

    if (success) {
        success = pdf_save_to_file(writer, filename);
    }

    pdf_close(writer);

    return success;
}

bool write_pdf_page(
    const char* filename,
    TexNode* vlist,
    TFMFontManager* fonts,
    Arena* arena,
    const PDFParams& params
) {
    PDFWriter writer(arena);

    if (!pdf_open(writer, filename, params)) {
        return false;
    }

    bool success = pdf_write_page(writer, vlist, 1, fonts);

    if (success) {
        success = pdf_save_to_file(writer, filename);
    }

    pdf_close(writer);

    return success;
}

// ============================================================================
// Debugging
// ============================================================================

void dump_pdf_writer_state(const PDFWriter& writer) {
    log_debug("PDF Writer State:");
    log_debug("  Position: x=%.2f y=%.2f", writer.x, writer.y);
    log_debug("  Current font size: %.2f", writer.current_size);
    log_debug("  Page count: %d", writer.page_count);
    log_debug("  Fonts defined: %d", writer.font_count);
    log_debug("  Page size: %.2f x %.2f", writer.params.page_width, writer.params.page_height);
    log_debug("  Margins: L=%.2f R=%.2f T=%.2f B=%.2f",
        writer.params.margin_left, writer.params.margin_right,
        writer.params.margin_top, writer.params.margin_bottom);
}

} // namespace tex
