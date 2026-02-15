/**
 * PDF Viewer Command for Lambda
 *
 * Implements 'lambda view <file.pdf>' to open PDF in a window
 * Uses existing radiant window infrastructure
 */

#include "../view.hpp"
#include "../pdf/pdf_to_view.hpp"
#include "../../lambda/input/input.hpp"
#include "../../lib/log.h"
#include "../../lib/mempool.h"
#include "../../lib/memtrack.h"
#include "../../lib/font/font.h"

// FreeType for direct glyph rendering in PDF viewer (OpenGL bitmap textures)
#include <ft2build.h>
#include FT_FREETYPE_H

// External functions
void parse_pdf(Input* input, const char* pdf_data, size_t pdf_length); // From input-pdf.cpp
int ui_context_init(UiContext* uicon, bool headless); // From window.cpp
void ui_context_cleanup(UiContext* uicon); // From window.cpp
void ui_context_create_surface(UiContext* uicon, int width, int height); // From window.cpp
void render_html_doc(UiContext* uicon, ViewTree* view_tree, const char* output_file); // From window.cpp

// External declarations
extern bool do_redraw;

// Global variable for PDF page height (needed for coordinate conversion)
static float g_pdf_page_height = 0.0f;

// PDF Viewer context - holds both UI and view tree
typedef struct {
    UiContext* uicon;
    ViewTree* view_tree;
    Input* input;           // Keep input alive for memory pool
    Item pdf_root;          // Parsed PDF data for page navigation
    int current_page;       // 0-based current page index
    int total_pages;        // Total number of pages in PDF
} PdfViewerContext;

// Helper function: render text string using FreeType
static void render_text_gl(UiContext* uicon, const char* text, float x, float y, FontProp* font_prop, float r, float g, float b) {
    // Use provided font properties
    if (!font_prop) {
        font_prop = &uicon->default_font;
    }

    const char* font_family = font_prop->family ? font_prop->family : "Arial";

    // Resolve font via unified font module
    FontWeight fw = (font_prop->font_weight == CSS_VALUE_BOLD) ? FONT_WEIGHT_BOLD : FONT_WEIGHT_NORMAL;
    FontSlant fs = (font_prop->font_style == CSS_VALUE_ITALIC) ? FONT_SLANT_ITALIC : FONT_SLANT_NORMAL;
    FontStyleDesc style = {};
    style.family = font_family;
    style.size_px = font_prop->font_size;
    style.weight = fw;
    style.slant = fs;
    FontHandle* handle = font_resolve(uicon->font_ctx, &style);
    FT_Face face = handle ? (FT_Face)font_handle_get_ft_face(handle) : NULL;
    if (!face) {
        log_warn("No font face available for text rendering");
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float pen_x = x;
    float pen_y = y;

    for (const char* p = text; *p; p++) {
        // Load character glyph
        if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) {
            continue;
        }

        FT_GlyphSlot glyph = face->glyph;

        // Create texture from glyph bitmap
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_ALPHA,
            glyph->bitmap.width,
            glyph->bitmap.rows,
            0,
            GL_ALPHA,
            GL_UNSIGNED_BYTE,
            glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        float xpos = pen_x + glyph->bitmap_left;
        float ypos = pen_y - glyph->bitmap_top;
        float w = glyph->bitmap.width;
        float h = glyph->bitmap.rows;

        // Render textured quad with text color
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);

        // Set texture environment to modulate color with alpha texture
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        // Set color just before drawing (r, g, b will be multiplied with texture alpha)
        glColor4f(r, g, b, 1.0f);

        glBegin(GL_QUADS);
            glTexCoord2f(0.0f, 0.0f); glVertex2f(xpos, ypos);
            glTexCoord2f(1.0f, 0.0f); glVertex2f(xpos + w, ypos);
            glTexCoord2f(1.0f, 1.0f); glVertex2f(xpos + w, ypos + h);
            glTexCoord2f(0.0f, 1.0f); glVertex2f(xpos, ypos + h);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        glDeleteTextures(1, &texture);

        // Advance cursor
        pen_x += (glyph->advance.x >> 6);
    }

    glDisable(GL_BLEND);
}

// Helper function: Convert Color struct to RGB floats
static void color_to_rgb(Color color, float* r, float* g, float* b) {
    if (color.c) {  // Color is set
        *r = color.r / 255.0f;
        *g = color.g / 255.0f;
        *b = color.b / 255.0f;
    } else {
        // Default to black if color not set
        *r = 0.0f;
        *g = 0.0f;
        *b = 0.0f;
    }
}

// Helper function: Convert Color struct to RGBA floats (includes alpha)
static void color_to_rgba(Color color, float* r, float* g, float* b, float* a) {
    if (color.c) {  // Color is set
        *r = color.r / 255.0f;
        *g = color.g / 255.0f;
        *b = color.b / 255.0f;
        *a = color.a / 255.0f;
    } else {
        // Default to black if color not set
        *r = 0.0f;
        *g = 0.0f;
        *b = 0.0f;
        *a = 1.0f;  // Fully opaque by default
    }
}

// Forward declaration
static void render_view_recursive(UiContext* uicon, View* view, float offset_x, float offset_y, float scale);

// Render a ViewText node
static void render_view_text(UiContext* uicon, ViewText* text_view, float offset_x, float offset_y, float scale) {
    if (!text_view) {
        log_debug("render_view_text: null text_view or node");
        return;
    }

    // Get text content from the node
    unsigned char* text_data = text_view->text_data();
    if (!text_data || !*text_data) {
        log_debug("render_view_text: no text data");
        return;
    }

    log_info("Rendering text: '%s' at (%.1f, %.1f) scale=%.2f", text_data, offset_x, offset_y, scale);

    // Get font properties
    FontProp* font = text_view->font;
    if (!font) {
        log_warn("render_view_text: no font property");
        return;
    }

    // Calculate position with offset and scale
    // PDF coordinates: Y increases upward from bottom (0 at bottom, height at top)
    // Screen coordinates: Y increases downward from top (0 at top, height at bottom)
    // Convert: screen_y = offset_y + (page_height - pdf_y) * scale
    float x = offset_x + text_view->x * scale;
    float y = offset_y + (g_pdf_page_height - text_view->y) * scale;

    // Scale font size for rendering
    FontProp scaled_font = *font;
    scaled_font.font_size = font->font_size * scale;

    log_debug("Text position: x=%.1f, y=%.1f, font_size=%.1f", x, y, scaled_font.font_size);

    // Get text color from ViewText (default to black if not set)
    float r = 0.0f, g = 0.0f, b = 0.0f;
    log_debug("Text color check: c=0x%08X, r=%u, g=%u, b=%u, a=%u",
             text_view->color.c, text_view->color.r, text_view->color.g,
             text_view->color.b, text_view->color.a);
    if (text_view->color.c) {
        color_to_rgb(text_view->color, &r, &g, &b);
        log_debug("Applied text color: r=%.2f, g=%.2f, b=%.2f", r, g, b);
    }

    // Render the text with proper font properties
    char text_buffer[256];
    snprintf(text_buffer, sizeof(text_buffer), "%s", text_data);
    render_text_gl(uicon, text_buffer, x, y, &scaled_font, r, g, b);
}// Render a ViewBlock node
static void render_view_block(UiContext* uicon, ViewBlock* block, float offset_x, float offset_y, float scale) {
    if (!block) return;

    // PDF coordinates: Y increases upward from bottom, (x,y) is bottom-left corner
    // Screen coordinates: Y increases downward from top
    // For rectangles: PDF y is the bottom edge, so the top edge is at y + height
    // Convert top edge: screen_y = offset_y + (page_height - (pdf_y + pdf_height)) * scale
    float x = offset_x + block->x * scale;
    float y = offset_y + (g_pdf_page_height - (block->y + block->height)) * scale;
    float w = block->width * scale;
    float h = block->height * scale;

    log_debug("Rendering block at (%.1f, %.1f) size %.1fx%.1f", x, y, w, h);

    // Render background if set
    if (block->bound && block->bound->background && block->bound->background->color.c) {
        float r, g, b, a;
        color_to_rgba(block->bound->background->color, &r, &g, &b, &a);

        log_info("Block background color: (%.2f, %.2f, %.2f, %.2f)", r, g, b, a);

        // Enable alpha blending for transparent backgrounds
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glColor4f(r, g, b, a);
        glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + w, y);
            glVertex2f(x + w, y + h);
            glVertex2f(x, y + h);
        glEnd();

        glDisable(GL_BLEND);
    }

    // Render border if set
    if (block->bound && block->bound->border) {
        BorderProp* border = block->bound->border;

        // Check if any border is visible
        bool has_border = border->width.top > 0 || border->width.right > 0 ||
                         border->width.bottom > 0 || border->width.left > 0;

        if (has_border) {
            float r, g, b;

            // Helper lambda to set up line style
            auto setup_line_style = [](CssEnum style) {
                if (style == CSS_VALUE_DASHED) {
                    glEnable(GL_LINE_STIPPLE);
                    glLineStipple(3, 0x00FF);  // 8 on, 8 off pattern
                } else if (style == CSS_VALUE_DOTTED) {
                    glEnable(GL_LINE_STIPPLE);
                    glLineStipple(1, 0x0101);  // 1 on, 7 off pattern
                } else {
                    glDisable(GL_LINE_STIPPLE);
                }
            };

            // Top border
            if (border->width.top > 0 && border->top_color.c) {
                color_to_rgb(border->top_color, &r, &g, &b);
                glColor3f(r, g, b);
                glLineWidth(border->width.top * scale);
                setup_line_style(border->top_style);
                glBegin(GL_LINES);
                    glVertex2f(x, y);
                    glVertex2f(x + w, y);
                glEnd();
            }

            // Right border
            if (border->width.right > 0 && border->right_color.c) {
                color_to_rgb(border->right_color, &r, &g, &b);
                glColor3f(r, g, b);
                glLineWidth(border->width.right * scale);
                setup_line_style(border->right_style);
                glBegin(GL_LINES);
                    glVertex2f(x + w, y);
                    glVertex2f(x + w, y + h);
                glEnd();
            }

            // Bottom border
            if (border->width.bottom > 0 && border->bottom_color.c) {
                color_to_rgb(border->bottom_color, &r, &g, &b);
                glColor3f(r, g, b);
                glLineWidth(border->width.bottom * scale);
                setup_line_style(border->bottom_style);
                glBegin(GL_LINES);
                    glVertex2f(x + w, y + h);
                    glVertex2f(x, y + h);
                glEnd();
            }

            // Left border
            if (border->width.left > 0 && border->left_color.c) {
                color_to_rgb(border->left_color, &r, &g, &b);
                glColor3f(r, g, b);
                glLineWidth(border->width.left * scale);
                setup_line_style(border->left_style);
                glBegin(GL_LINES);
                    glVertex2f(x, y + h);
                    glVertex2f(x, y);
                glEnd();
            }

            // Disable stipple after rendering all borders
            glDisable(GL_LINE_STIPPLE);
        }
    }

    // Debug: check vpath pointer
    log_info("Block vpath check: vpath=%p", (void*)block->vpath);

    // Render vector path if present (for PDF curves)
    if (block->vpath && block->vpath->segments) {
        VectorPathProp* vpath = block->vpath;
        log_info("Rendering VectorPath: has_fill=%d, has_stroke=%d, stroke_width=%.1f",
                 vpath->has_fill, vpath->has_stroke, vpath->stroke_width);

        // First, render fill if present
        if (vpath->has_fill) {
            float r = vpath->fill_color.r / 255.0f;
            float g = vpath->fill_color.g / 255.0f;
            float b = vpath->fill_color.b / 255.0f;
            float a = vpath->fill_color.a / 255.0f;

            log_info("VectorPath fill color: (%.2f, %.2f, %.2f, %.2f)", r, g, b, a);

            // Enable alpha blending for transparent fills
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(r, g, b, a);

            // Render filled polygon using GL_POLYGON
            // Note: This works for convex paths; complex paths may need tessellation
            glBegin(GL_POLYGON);
            for (VectorPathSegment* seg = vpath->segments; seg; seg = seg->next) {
                // Transform PDF coordinates to screen coordinates
                float sx = offset_x + seg->x * scale;
                float sy = offset_y + (g_pdf_page_height - seg->y) * scale;

                if (seg->type == VectorPathSegment::VPATH_CURVETO) {
                    // For curves, get previous point and subdivide
                    VectorPathSegment* prev = vpath->segments;
                    while (prev && prev->next != seg) prev = prev->next;
                    float p0x = offset_x + (prev ? prev->x : seg->x) * scale;
                    float p0y = offset_y + (g_pdf_page_height - (prev ? prev->y : seg->y)) * scale;

                    float cx1 = offset_x + seg->x1 * scale;
                    float cy1 = offset_y + (g_pdf_page_height - seg->y1) * scale;
                    float cx2 = offset_x + seg->x2 * scale;
                    float cy2 = offset_y + (g_pdf_page_height - seg->y2) * scale;

                    const int steps = 20;
                    for (int i = 1; i <= steps; i++) {
                        float t = (float)i / steps;
                        float t2 = t * t;
                        float t3 = t2 * t;
                        float mt = 1 - t;
                        float mt2 = mt * mt;
                        float mt3 = mt2 * mt;

                        float bx = mt3 * p0x + 3 * mt2 * t * cx1 + 3 * mt * t2 * cx2 + t3 * sx;
                        float by = mt3 * p0y + 3 * mt2 * t * cy1 + 3 * mt * t2 * cy2 + t3 * sy;
                        glVertex2f(bx, by);
                    }
                } else if (seg->type != VectorPathSegment::VPATH_CLOSE) {
                    glVertex2f(sx, sy);
                }
            }
            glEnd();
            glDisable(GL_BLEND);
        }

        // Then render stroke if present
        if (vpath->has_stroke) {
            float r = vpath->stroke_color.r / 255.0f;
            float g = vpath->stroke_color.g / 255.0f;
            float b = vpath->stroke_color.b / 255.0f;
            glColor3f(r, g, b);
            glLineWidth(vpath->stroke_width * scale);
        }

        // Render path segments
        glBegin(GL_LINE_STRIP);
        for (VectorPathSegment* seg = vpath->segments; seg; seg = seg->next) {
            float sx = offset_x + seg->x * scale;
            float sy = offset_y + seg->y * scale;

            switch (seg->type) {
                case VectorPathSegment::VPATH_MOVETO:
                    glEnd();  // End current strip
                    glBegin(GL_LINE_STRIP);  // Start new strip
                    glVertex2f(sx, sy);
                    log_debug("  MOVETO (%.1f, %.1f)", sx, sy);
                    break;
                case VectorPathSegment::VPATH_LINETO:
                    glVertex2f(sx, sy);
                    log_debug("  LINETO (%.1f, %.1f)", sx, sy);
                    break;
                case VectorPathSegment::VPATH_CURVETO: {
                    // Approximate cubic Bezier curve with line segments
                    // Get previous point (current GL position)
                    float cx1 = offset_x + seg->x1 * scale;
                    float cy1 = offset_y + seg->y1 * scale;
                    float cx2 = offset_x + seg->x2 * scale;
                    float cy2 = offset_y + seg->y2 * scale;

                    // Subdivide curve into ~20 segments for smooth rendering
                    // We need the previous point, so we'll use the last vertex
                    // For now, just draw straight to the endpoint as approximation
                    // TODO: Implement proper Bezier curve subdivision
                    const int steps = 20;
                    float last_x = sx, last_y = sy;  // Will be updated by Bezier eval

                    // Get start point from previous segment
                    VectorPathSegment* prev = vpath->segments;
                    while (prev && prev->next != seg) prev = prev->next;
                    float p0x = offset_x + (prev ? prev->x : seg->x) * scale;
                    float p0y = offset_y + (prev ? prev->y : seg->y) * scale;

                    for (int i = 1; i <= steps; i++) {
                        float t = (float)i / steps;
                        float t2 = t * t;
                        float t3 = t2 * t;
                        float mt = 1 - t;
                        float mt2 = mt * mt;
                        float mt3 = mt2 * mt;

                        // Cubic Bezier: B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3
                        float bx = mt3 * p0x + 3 * mt2 * t * cx1 + 3 * mt * t2 * cx2 + t3 * sx;
                        float by = mt3 * p0y + 3 * mt2 * t * cy1 + 3 * mt * t2 * cy2 + t3 * sy;
                        glVertex2f(bx, by);
                    }
                    log_debug("  CURVETO (%.1f,%.1f)-(%.1f,%.1f)->(%.1f,%.1f)",
                             cx1, cy1, cx2, cy2, sx, sy);
                    break;
                }
                case VectorPathSegment::VPATH_CLOSE:
                    // Close path by connecting to start
                    if (vpath->segments) {
                        float start_x = offset_x + vpath->segments->x * scale;
                        float start_y = offset_y + vpath->segments->y * scale;
                        glVertex2f(start_x, start_y);
                    }
                    log_debug("  CLOSE");
                    break;
            }
        }
        glEnd();
    }

    // Render children recursively
    // Note: ViewBlock inherits from ViewSpan which inherits from ViewGroup
    // The children are in the 'child' field from ViewGroup, not 'first_child'
    ViewElement* group = (ViewElement*)block;
    View* child = group->first_child;
    int child_count = 0;
    while (child) {
        child_count++;
        child = child->next();
    }

    log_debug("Block has %d children", child_count);

    // Render children with original offset (children have absolute coordinates)
    child = group->first_child;
    while (child) {
        render_view_recursive(uicon, child, offset_x, offset_y, scale);
        child = child->next_sibling;
    }
}

// Recursively render any view type
static void render_view_recursive(UiContext* uicon, View* view, float offset_x, float offset_y, float scale) {
    if (!view) return;

    log_debug("render_view_recursive: type=%d at (%.1f, %.1f)", view->view_type, offset_x, offset_y);

    switch (view->view_type) {
        case RDT_VIEW_TEXT:
            render_view_text(uicon, (ViewText*)view, offset_x, offset_y, scale);
            break;

        case RDT_VIEW_BLOCK:
        case RDT_VIEW_INLINE_BLOCK:
        case RDT_VIEW_LIST_ITEM:
        case RDT_VIEW_TABLE:
        case RDT_VIEW_TABLE_ROW_GROUP:
        case RDT_VIEW_TABLE_ROW:
        case RDT_VIEW_TABLE_CELL:
            render_view_block(uicon, (ViewBlock*)view, offset_x, offset_y, scale);
            break;

        case RDT_VIEW_INLINE: {
            // Render inline spans - they may contain text or other inline elements
            ViewSpan* span = (ViewSpan*)view;
            View* child = (View*)span->first_child;
            while (child) {
                render_view_recursive(uicon, child, offset_x, offset_y, scale);
                child = child->next_sibling;
            }
            break;
        }

        default:
            // Unknown type, skip
            break;
    }
}// Forward declaration of page loading function
static void load_pdf_page(PdfViewerContext* pdf_ctx, int page_index);

// Callback implementations for PDF viewer
static void key_callback_pdf(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    PdfViewerContext* pdf_ctx = (PdfViewerContext*)glfwGetWindowUserPointer(window);
    if (!pdf_ctx) return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            // Close window
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;

        case GLFW_KEY_PAGE_DOWN:
        case GLFW_KEY_RIGHT:
        case GLFW_KEY_DOWN:
            // Next page
            if (pdf_ctx->current_page < pdf_ctx->total_pages - 1) {
                load_pdf_page(pdf_ctx, pdf_ctx->current_page + 1);
            }
            break;

        case GLFW_KEY_PAGE_UP:
        case GLFW_KEY_LEFT:
        case GLFW_KEY_UP:
            // Previous page
            if (pdf_ctx->current_page > 0) {
                load_pdf_page(pdf_ctx, pdf_ctx->current_page - 1);
            }
            break;

        case GLFW_KEY_HOME:
            // First page
            if (pdf_ctx->current_page != 0) {
                load_pdf_page(pdf_ctx, 0);
            }
            break;

        case GLFW_KEY_END:
            // Last page
            if (pdf_ctx->current_page != pdf_ctx->total_pages - 1) {
                load_pdf_page(pdf_ctx, pdf_ctx->total_pages - 1);
            }
            break;
    }
}

static void cursor_position_callback_pdf(GLFWwindow* window, double xpos, double ypos) {
    // Handle mouse movement (for future panning/zoom)
    // Currently a no-op
}

static void character_callback_pdf(GLFWwindow* window, unsigned int codepoint) {
    // Handle character input (for future search/navigation)
    // Currently a no-op
}

static void mouse_button_callback_pdf(GLFWwindow* window, int button, int action, int mods) {
    // Handle mouse clicks (for future link navigation)
    // Currently a no-op
}

static void scroll_callback_pdf(GLFWwindow* window, double xoffset, double yoffset) {
    // Handle scrolling (for future zoom/pan)
    // Currently a no-op
}

static void framebuffer_size_callback_pdf(GLFWwindow* window, int width, int height) {
    // Update viewport when window is resized
    glViewport(0, 0, width, height);
    do_redraw = true;
}

/**
 * Load a specific page and regenerate the view tree
 */
static void load_pdf_page(PdfViewerContext* pdf_ctx, int page_index) {
    if (!pdf_ctx || !pdf_ctx->input) {
        log_error("Invalid context for page loading");
        return;
    }

    if (page_index < 0 || page_index >= pdf_ctx->total_pages) {
        log_warn("Page index %d out of range (0-%d)", page_index, pdf_ctx->total_pages - 1);
        return;
    }

    log_info("Loading page %d/%d", page_index + 1, pdf_ctx->total_pages);

    // Generate view tree for the specific page
    // Pass pixel_ratio for high-DPI display scaling
    float pixel_ratio = pdf_ctx->uicon ? pdf_ctx->uicon->pixel_ratio : 1.0f;
    ViewTree* new_view_tree = pdf_page_to_view_tree(pdf_ctx->input, pdf_ctx->pdf_root, page_index, pixel_ratio);

    if (!new_view_tree || !new_view_tree->root) {
        log_error("Failed to generate view tree for page %d", page_index + 1);
        return;
    }

    // Note: Don't free old view_tree - it's in the same pool as Input
    // The pool will be cleaned up when the viewer closes

    // Update context with new view tree
    pdf_ctx->view_tree = new_view_tree;
    pdf_ctx->current_page = page_index;

    // Update page height for coordinate conversion
    g_pdf_page_height = new_view_tree->root->height;

    // Update window title
    if (pdf_ctx->uicon && pdf_ctx->uicon->window) {
        char title[512];
        snprintf(title, sizeof(title), "Lambda PDF Viewer - Page %d/%d",
                 page_index + 1, pdf_ctx->total_pages);
        glfwSetWindowTitle(pdf_ctx->uicon->window, title);
    }

    // Trigger redraw
    do_redraw = true;

    log_info("Successfully loaded page %d/%d", page_index + 1, pdf_ctx->total_pages);
}

static void window_refresh_callback_pdf(GLFWwindow* window) {
    // Get PDF viewer context from window user pointer
    PdfViewerContext* pdf_ctx = (PdfViewerContext*)glfwGetWindowUserPointer(window);
    if (!pdf_ctx || !pdf_ctx->uicon) {
        log_warn("window_refresh_callback_pdf: missing context");
        return;
    }

    UiContext* uicon = pdf_ctx->uicon;
    ViewTree* view_tree = pdf_ctx->view_tree;

    log_warn("window_refresh_callback_pdf called");

    // Get window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    // For now, just render a simple colored screen
    // TODO: Enable full PDF rendering once parse_pdf is fixed

    // Clear with light blue background to show something is working
    glClearColor(0.85f, 0.90f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Set up orthographic projection
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Draw a white "page" rectangle to show PDF viewer UI
    float page_width = 600;
    float page_height = 800;
    float x = (width - page_width) / 2;
    float y = (height - page_height) / 2;

    // Draw white page background
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + page_width, y);
        glVertex2f(x + page_width, y + page_height);
        glVertex2f(x, y + page_height);
    glEnd();

    // Draw a border around the page
    glColor3f(0.3f, 0.3f, 0.3f);
    glLineWidth(3.0f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x, y);
        glVertex2f(x + page_width, y);
        glVertex2f(x + page_width, y + page_height);
        glVertex2f(x, y + page_height);
    glEnd();

    // Draw some test content to show rendering is working

    // Title bar (blue)
    glColor3f(0.2f, 0.4f, 0.8f);
    glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + page_width, y);
        glVertex2f(x + page_width, y + 60);
        glVertex2f(x, y + 60);
    glEnd();

        // Draw title
    FontProp title_font = uicon->default_font;
    title_font.font_size = 20;
    render_text_gl(uicon, "Lambda PDF Viewer - Parsed Content", x + 20, y + 40, &title_font, 1.0f, 1.0f, 1.0f);

    // Render actual PDF content from view tree
    if (view_tree && view_tree->root) {
        // Debug: log view tree info
        log_info("View tree root: type=%d, size=%.0fx%.0f",
                 view_tree->root->view_type, view_tree->root->width, view_tree->root->height);

        // Calculate scale to fit PDF into page area
        float content_x = x + 20;  // Add some margin
        float content_y = y + 80;  // Below title bar
        float content_area_width = page_width - 40;  // Margins on both sides
        float content_area_height = page_height - 120;  // Title bar + margins

        float scale_x = content_area_width / view_tree->root->width;
        float scale_y = content_area_height / view_tree->root->height;
        float scale = (scale_x < scale_y) ? scale_x : scale_y;  // Use smaller scale to fit

        // Clamp scale to reasonable bounds
        if (scale > 2.0f) scale = 2.0f;  // Don't upscale too much
        if (scale < 0.1f) scale = 0.1f;  // Don't downscale too much

        log_info("Rendering with scale=%.2f at offset=(%.1f, %.1f)", scale, content_x, content_y);

        // Set global page height for coordinate conversion
        g_pdf_page_height = view_tree->root->height;

        // Center the content if it's smaller than the available area
        float scaled_width = view_tree->root->width * scale;
        float scaled_height = view_tree->root->height * scale;
        float center_offset_x = (content_area_width - scaled_width) / 2;
        float center_offset_y = (content_area_height - scaled_height) / 2;

        // Render the entire view tree
        // Pass the TOP of the content area as offset_y
        render_view_recursive(uicon, view_tree->root,
                            content_x + center_offset_x,
                            content_y + center_offset_y,
                            scale);
    } else {
        log_warn("No view tree available for rendering");
        FontProp error_font = uicon->default_font;
        error_font.font_size = 16;
        render_text_gl(uicon, "No view tree available", x + 50, y + 100, &error_font, 0.8f, 0.2f, 0.2f);
    }

    // Bottom status bar (light gray)
    glColor3f(0.8f, 0.8f, 0.8f);
    glBegin(GL_QUADS);
        glVertex2f(x, y + page_height - 40);
        glVertex2f(x + page_width, y + page_height - 40);
        glVertex2f(x + page_width, y + page_height);
        glVertex2f(x, y + page_height);
    glEnd();

    FontProp status_font = uicon->default_font;
    status_font.font_size = 14;
    render_text_gl(uicon, "PDF Parsed - Press ESC to exit", x + 20, y + page_height - 15, &status_font, 0.3f, 0.3f, 0.3f);

    // Swap buffers
    glfwSwapBuffers(window);

    do_redraw = false;
}

/**
 * Read file contents to string
 */
static char* read_pdf_file(const char* filename, size_t* out_size) {
    if (out_size) *out_size = 0;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        log_error("Failed to open file: %s", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)mem_alloc(size + 1, MEM_CAT_INPUT_CSS);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, size, file);
    content[bytes_read] = '\0';
    fclose(file);

    if (out_size) *out_size = bytes_read;
    return content;
}

/**
 * View PDF file in window
 * Main entry point for 'lambda view <file.pdf>' command
 */
int view_pdf_in_window(const char* pdf_file) {
    log_info("Opening PDF file in viewer: %s", pdf_file);

    // Read PDF file content with explicit size for binary-safe parsing
    size_t pdf_size = 0;
    char* pdf_content = read_pdf_file(pdf_file, &pdf_size);
    if (!pdf_content) {
        log_error("Failed to read PDF file: %s", pdf_file);
        return 1;
    }
    log_info("Read PDF file: %zu bytes", pdf_size);

    // Create Input structure properly using InputManager
    Input* input = InputManager::create_input(nullptr); // URL not needed for direct parsing
    if (!input) {
        log_error("Failed to create Input structure");
        free(pdf_content);
        return 1;
    }

    // Parse PDF content with explicit size
    log_info("Parsing PDF content...");
    parse_pdf(input, pdf_content, pdf_size);
    free(pdf_content); // Done with raw content

    // Check if parsing succeeded
    if (input->root.item == ITEM_ERROR || input->root.item == ITEM_NULL) {
        log_error("Failed to parse PDF file");
        // Note: Input is managed by InputManager, don't destroy pool or free input
        return 1;
    }

    log_info("PDF parsed successfully");

    // Get total page count
    int total_pages = pdf_get_page_count(input->root);
    if (total_pages <= 0) {
        log_error("PDF has no pages or page count failed");
        // Note: Input is managed by InputManager, don't destroy pool or free input
        return 1;
    }

    log_info("PDF has %d page(s)", total_pages);

    // Convert first page to view tree (page 0)
    // Note: pixel_ratio not available yet (uicon not initialized), pass 1.0
    // Scaling will happen when page is re-rendered via load_pdf_page after uicon is ready
    ViewTree* view_tree = pdf_page_to_view_tree(input, input->root, 0, 1.0f);
    if (!view_tree || !view_tree->root) {
        log_error("Failed to convert first page to view tree");
        // Note: Input is managed by InputManager, don't destroy pool or free input
        return 1;
    }

    log_info("View tree created successfully for page 1/%d", total_pages);

    // Initialize UI context
    UiContext uicon;
    memset(&uicon, 0, sizeof(UiContext));

    if (ui_context_init(&uicon, false) != 0) {
        log_error("Failed to initialize UI context");
        // Note: Input is managed by InputManager, don't destroy pool or free input
        return 1;
    }

    GLFWwindow* window = uicon.window;
    if (!window) {
        log_error("Failed to create window");
        ui_context_cleanup(&uicon);
        // Note: Input is managed by InputManager, don't destroy pool or free input
        return 1;
    }    // Set up OpenGL context and callbacks (like window_main does)
    log_info("Setting up OpenGL context...");
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // enable vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction

    // Create PDF viewer context to pass to callbacks
    PdfViewerContext pdf_ctx;
    pdf_ctx.uicon = &uicon;
    pdf_ctx.view_tree = view_tree;
    pdf_ctx.input = input;
    pdf_ctx.pdf_root = input->root;
    pdf_ctx.current_page = 0;
    pdf_ctx.total_pages = total_pages;

    // Set window user pointer so callbacks can access context
    glfwSetWindowUserPointer(window, &pdf_ctx);

    // Set up event callbacks
    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetKeyCallback(window, key_callback_pdf);
    glfwSetCharCallback(window, character_callback_pdf);
    glfwSetCursorPosCallback(window, cursor_position_callback_pdf);
    glfwSetMouseButtonCallback(window, mouse_button_callback_pdf);
    glfwSetScrollCallback(window, scroll_callback_pdf);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback_pdf);
    glfwSetWindowRefreshCallback(window, window_refresh_callback_pdf);

    // Set clear color
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f); // Light grey background

    // Initialize framebuffer
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    framebuffer_size_callback_pdf(window, width, height);

    log_info("OpenGL context initialized");

    // Set window title with page information
    char title[512];
    snprintf(title, sizeof(title), "Lambda PDF Viewer - Page 1/%d - %s",
             total_pages, pdf_file);
    glfwSetWindowTitle(window, title);

    log_info("PDF viewer ready. Use PgUp/PgDn or Arrow keys to navigate. Press ESC to exit.");

    // Trigger initial draw
    do_redraw = true;    // Trigger initial draw
    do_redraw = true;

    // Main event loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (do_redraw) {
            window_refresh_callback_pdf(window);
        }

        // Limit FPS
        glfwWaitEventsTimeout(1.0 / 60.0);
    }

    // Cleanup
    log_info("Closing PDF viewer");
    ui_context_cleanup(&uicon);

    // Note: Input was created via InputManager::create_input() which uses the global
    // pool. The InputManager owns and manages the pool and all inputs created from it.
    // Do NOT call pool_destroy or free on the input - InputManager handles cleanup.

    return 0;
}
