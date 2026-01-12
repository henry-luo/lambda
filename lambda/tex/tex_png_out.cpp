// tex_png_out.cpp - PNG Output Generation Implementation
//
// Rasterizes TeX node trees to PNG format using FreeType.

#include "tex_png_out.hpp"
#include "lib/log.h"
#include <cstring>
#include <cmath>
#include <cstdio>

// Use libpng for PNG encoding
#include <png.h>

namespace tex {

// ============================================================================
// Image Buffer Management
// ============================================================================

PNGImage* png_create_image(Arena* arena, int width, int height) {
    PNGImage* img = (PNGImage*)arena_alloc(arena, sizeof(PNGImage));
    if (!img) return nullptr;

    img->width = width;
    img->height = height;
    img->stride = width * 4;  // RGBA
    img->arena = arena;

    size_t pixel_size = (size_t)img->stride * height;
    img->pixels = (uint8_t*)arena_alloc(arena, pixel_size);
    if (!img->pixels) {
        return nullptr;
    }

    memset(img->pixels, 0, pixel_size);
    return img;
}

void png_clear(PNGImage* image, uint32_t color) {
    if (!image || !image->pixels) return;

    uint8_t r = (color >> 24) & 0xFF;
    uint8_t g = (color >> 16) & 0xFF;
    uint8_t b = (color >> 8) & 0xFF;
    uint8_t a = color & 0xFF;

    for (int y = 0; y < image->height; y++) {
        uint8_t* row = image->pixels + y * image->stride;
        for (int x = 0; x < image->width; x++) {
            row[x * 4 + 0] = r;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = a;
        }
    }
}

// ============================================================================
// Pixel Operations
// ============================================================================

void png_blend_pixel(PNGImage* image, int x, int y, uint32_t color) {
    if (!image || !image->pixels) return;
    if (x < 0 || x >= image->width || y < 0 || y >= image->height) return;

    uint8_t* dst = image->pixels + y * image->stride + x * 4;

    uint8_t src_r = (color >> 24) & 0xFF;
    uint8_t src_g = (color >> 16) & 0xFF;
    uint8_t src_b = (color >> 8) & 0xFF;
    uint8_t src_a = color & 0xFF;

    if (src_a == 0) return;  // Fully transparent

    if (src_a == 255) {
        // Fully opaque - direct copy
        dst[0] = src_r;
        dst[1] = src_g;
        dst[2] = src_b;
        dst[3] = src_a;
    } else {
        // Alpha blend
        uint8_t dst_r = dst[0];
        uint8_t dst_g = dst[1];
        uint8_t dst_b = dst[2];
        uint8_t dst_a = dst[3];

        // Porter-Duff over operator
        float sa = src_a / 255.0f;
        float da = dst_a / 255.0f;
        float out_a = sa + da * (1.0f - sa);

        if (out_a > 0) {
            dst[0] = (uint8_t)((src_r * sa + dst_r * da * (1.0f - sa)) / out_a);
            dst[1] = (uint8_t)((src_g * sa + dst_g * da * (1.0f - sa)) / out_a);
            dst[2] = (uint8_t)((src_b * sa + dst_b * da * (1.0f - sa)) / out_a);
            dst[3] = (uint8_t)(out_a * 255.0f);
        }
    }
}

void png_fill_rect(PNGImage* image, int x, int y, int w, int h, uint32_t color) {
    if (!image) return;

    // Clamp to image bounds
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = (x + w) > image->width ? image->width : (x + w);
    int y2 = (y + h) > image->height ? image->height : (y + h);

    for (int py = y1; py < y2; py++) {
        for (int px = x1; px < x2; px++) {
            png_blend_pixel(image, px, py, color);
        }
    }
}

// ============================================================================
// FreeType Bitmap Rendering
// ============================================================================

void png_render_bitmap(PNGWriter& writer, FT_Bitmap* bitmap, int x, int y, uint32_t color) {
    if (!bitmap || !writer.image) return;

    uint8_t base_r = (color >> 24) & 0xFF;
    uint8_t base_g = (color >> 16) & 0xFF;
    uint8_t base_b = (color >> 8) & 0xFF;

    for (unsigned int row = 0; row < bitmap->rows; row++) {
        int dst_y = y + row;
        if (dst_y < 0 || dst_y >= writer.image->height) continue;

        for (unsigned int col = 0; col < bitmap->width; col++) {
            int dst_x = x + col;
            if (dst_x < 0 || dst_x >= writer.image->width) continue;

            // Get grayscale value from FreeType bitmap
            uint8_t gray = bitmap->buffer[row * bitmap->pitch + col];
            if (gray == 0) continue;  // Skip fully transparent

            // Apply text color with glyph coverage as alpha
            uint32_t pixel = ((uint32_t)base_r << 24) |
                            ((uint32_t)base_g << 16) |
                            ((uint32_t)base_b << 8) |
                            gray;

            png_blend_pixel(writer.image, dst_x, dst_y, pixel);
        }
    }
}

// ============================================================================
// Writer Initialization
// ============================================================================

bool png_init(PNGWriter& writer, Arena* arena, FT_Library ft_lib, const PNGParams& params) {
    writer.arena = arena;
    writer.params = params;
    writer.ft_lib = ft_lib;
    writer.font_provider = nullptr;  // Set up on first use
    writer.image = nullptr;
    writer.scale = params.dpi / 96.0f;  // CSS pixels are 96 DPI
    writer.current_color = params.text_color;

    // Initialize FreeType if not provided
    if (!writer.ft_lib) {
        FT_Error err = FT_Init_FreeType(&writer.ft_lib);
        if (err) {
            log_error("tex_png_out: failed to initialize FreeType");
            return false;
        }
    }

    return true;
}

// ============================================================================
// Node Rendering
// ============================================================================

void png_render_char(PNGWriter& writer, TexNode* node, float x, float y) {
    if (node->node_class != NodeClass::Char && node->node_class != NodeClass::MathChar) {
        return;
    }

    // Get character info
    int32_t codepoint;
    const char* font_name;
    float font_size;

    if (node->node_class == NodeClass::Char) {
        codepoint = node->content.ch.codepoint;
        font_name = node->content.ch.font.name;
        font_size = node->content.ch.font.size_pt;
    } else {
        codepoint = node->content.math_char.codepoint;
        font_name = node->content.math_char.font.name;
        font_size = node->content.math_char.font.size_pt;
    }

    // Get FreeType face
    FT_Face face = nullptr;

    // First try the face from the node
    if (node->node_class == NodeClass::Char && node->content.ch.font.face) {
        face = node->content.ch.font.face;
    } else if (node->node_class == NodeClass::MathChar && node->content.math_char.font.face) {
        face = node->content.math_char.font.face;
    }

    if (!face) {
        // No face available - skip rendering
        log_debug("tex_png_out: no FT_Face for char U+%04X", codepoint);
        return;
    }

    // Map CM character to Unicode if needed
    int32_t unicode = CMToUnicodeMap::map(codepoint, font_name);

    // Set font size
    FT_UInt pixel_size = (FT_UInt)(font_size * writer.scale * 96.0f / 72.0f);
    FT_Set_Pixel_Sizes(face, 0, pixel_size);

    // Load glyph
    FT_UInt glyph_idx = FT_Get_Char_Index(face, unicode);
    if (glyph_idx == 0) {
        log_debug("tex_png_out: missing glyph for U+%04X in %s", unicode, font_name ? font_name : "unknown");
        return;
    }

    FT_Error err = FT_Load_Glyph(face, glyph_idx, FT_LOAD_RENDER);
    if (err) {
        log_debug("tex_png_out: failed to load glyph U+%04X", unicode);
        return;
    }

    FT_GlyphSlot slot = face->glyph;

    // Calculate render position (scale CSS px to output pixels)
    int render_x = (int)(x * writer.scale) + slot->bitmap_left;
    int render_y = (int)(y * writer.scale) - slot->bitmap_top;

    // Render glyph bitmap
    png_render_bitmap(writer, &slot->bitmap, render_x, render_y, writer.current_color);
}

void png_render_rule(PNGWriter& writer, TexNode* node, float x, float y) {
    if (node->node_class != NodeClass::Rule) return;

    float width = node->width;
    float height = node->height + node->depth;
    float top = y - node->height;

    // Scale to output pixels
    int px = (int)(x * writer.scale);
    int py = (int)(top * writer.scale);
    int pw = (int)(width * writer.scale + 0.5f);
    int ph = (int)(height * writer.scale + 0.5f);

    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;

    png_fill_rect(writer.image, px, py, pw, ph, writer.current_color);
}

void png_render_hlist(PNGWriter& writer, TexNode* node, float x, float y) {
    for (TexNode* child = node->first_child; child; child = child->next_sibling) {
        float child_x = x + child->x;
        float child_y = y + child->y;
        png_render_node(writer, child, child_x, child_y);
    }
}

void png_render_vlist(PNGWriter& writer, TexNode* node, float x, float y) {
    for (TexNode* child = node->first_child; child; child = child->next_sibling) {
        float child_x = x + child->x;
        float child_y = y + child->y;
        png_render_node(writer, child, child_x, child_y);
    }
}

void png_render_node(PNGWriter& writer, TexNode* node, float x, float y) {
    if (!node) return;

    switch (node->node_class) {
        case NodeClass::Char:
        case NodeClass::MathChar:
            png_render_char(writer, node, x, y);
            break;

        case NodeClass::Rule:
            png_render_rule(writer, node, x, y);
            break;

        case NodeClass::HList:
        case NodeClass::HBox:
            png_render_hlist(writer, node, x, y);
            break;

        case NodeClass::VList:
        case NodeClass::VBox:
        case NodeClass::VTop:
        case NodeClass::Page:
        case NodeClass::Paragraph:
            png_render_vlist(writer, node, x, y);
            break;

        case NodeClass::MathList:
            png_render_hlist(writer, node, x, y);
            break;

        case NodeClass::Fraction:
            if (node->content.frac.numerator) {
                png_render_node(writer, node->content.frac.numerator,
                    x + node->content.frac.numerator->x,
                    y + node->content.frac.numerator->y);
            }
            if (node->content.frac.denominator) {
                png_render_node(writer, node->content.frac.denominator,
                    x + node->content.frac.denominator->x,
                    y + node->content.frac.denominator->y);
            }
            png_render_hlist(writer, node, x, y);
            break;

        case NodeClass::Radical:
            if (node->content.radical.radicand) {
                png_render_node(writer, node->content.radical.radicand,
                    x + node->content.radical.radicand->x,
                    y + node->content.radical.radicand->y);
            }
            if (node->content.radical.degree) {
                png_render_node(writer, node->content.radical.degree,
                    x + node->content.radical.degree->x,
                    y + node->content.radical.degree->y);
            }
            png_render_hlist(writer, node, x, y);
            break;

        case NodeClass::Scripts:
            if (node->content.scripts.nucleus) {
                png_render_node(writer, node->content.scripts.nucleus,
                    x + node->content.scripts.nucleus->x,
                    y + node->content.scripts.nucleus->y);
            }
            if (node->content.scripts.subscript) {
                png_render_node(writer, node->content.scripts.subscript,
                    x + node->content.scripts.subscript->x,
                    y + node->content.scripts.subscript->y);
            }
            if (node->content.scripts.superscript) {
                png_render_node(writer, node->content.scripts.superscript,
                    x + node->content.scripts.superscript->x,
                    y + node->content.scripts.superscript->y);
            }
            break;

        case NodeClass::Glue:
        case NodeClass::Kern:
        case NodeClass::Penalty:
            // Invisible nodes
            break;

        case NodeClass::Ligature:
            png_render_char(writer, node, x, y);
            break;

        default:
            png_render_hlist(writer, node, x, y);
            break;
    }
}

// ============================================================================
// Document Rendering
// ============================================================================

PNGImage* png_render(PNGWriter& writer, TexNode* root) {
    if (!root) {
        log_error("tex_png_out: null root node");
        return nullptr;
    }

    // Calculate image dimensions
    float margin = writer.params.margin_px;
    float content_width = root->width > 0 ? root->width : 100;
    float content_height = (root->height + root->depth) > 0 ? (root->height + root->depth) : 100;

    int img_width = (int)((content_width + margin * 2) * writer.scale + 0.5f);
    int img_height = (int)((content_height + margin * 2) * writer.scale + 0.5f);

    // Create image
    writer.image = png_create_image(writer.arena, img_width, img_height);
    if (!writer.image) {
        log_error("tex_png_out: failed to create image %dx%d", img_width, img_height);
        return nullptr;
    }

    // Clear with background
    png_clear(writer.image, writer.params.background);

    // Render content
    float offset_x = margin;
    float offset_y = margin + root->height;  // Baseline offset

    png_render_node(writer, root, offset_x, offset_y);

    log_debug("tex_png_out: rendered %dx%d image at %.0f DPI", img_width, img_height, writer.params.dpi);
    return writer.image;
}

// ============================================================================
// File Output (using libpng)
// ============================================================================

bool png_write_to_file(PNGImage* image, const char* filename) {
    if (!image || !image->pixels || !filename) return false;

    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_error("tex_png_out: failed to open %s for writing", filename);
        return false;
    }

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        fclose(fp);
        log_error("tex_png_out: failed to create PNG write struct");
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        fclose(fp);
        log_error("tex_png_out: failed to create PNG info struct");
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        log_error("tex_png_out: PNG write error");
        return false;
    }

    png_init_io(png_ptr, fp);

    // Set image attributes
    png_set_IHDR(png_ptr, info_ptr,
        image->width, image->height,
        8,                              // Bit depth
        PNG_COLOR_TYPE_RGBA,            // Color type
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_write_info(png_ptr, info_ptr);

    // Write rows
    png_bytep* row_pointers = (png_bytep*)malloc(image->height * sizeof(png_bytep));
    for (int y = 0; y < image->height; y++) {
        row_pointers[y] = image->pixels + y * image->stride;
    }

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, nullptr);

    free(row_pointers);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    log_info("tex_png_out: wrote %dx%d PNG to %s", image->width, image->height, filename);
    return true;
}

bool png_render_to_file(
    TexNode* root,
    const char* filename,
    const PNGParams* params,
    Arena* arena,
    FT_Library ft_lib
) {
    PNGParams p = params ? *params : PNGParams::defaults();

    PNGWriter writer;
    if (!png_init(writer, arena, ft_lib, p)) {
        return false;
    }

    PNGImage* image = png_render(writer, root);
    if (!image) {
        return false;
    }

    return png_write_to_file(image, filename);
}

// ============================================================================
// Memory Encoding (using libpng)
// ============================================================================

struct PNGMemoryWrite {
    uint8_t* buffer;
    size_t size;
    size_t capacity;
    Arena* arena;
};

static void png_memory_write_callback(png_structp png_ptr, png_bytep data, png_size_t length) {
    PNGMemoryWrite* mem = (PNGMemoryWrite*)png_get_io_ptr(png_ptr);

    // Grow buffer if needed
    if (mem->size + length > mem->capacity) {
        size_t new_capacity = mem->capacity * 2;
        if (new_capacity < mem->size + length) {
            new_capacity = mem->size + length + 4096;
        }

        uint8_t* new_buffer = (uint8_t*)arena_alloc(mem->arena, new_capacity);
        if (new_buffer && mem->buffer) {
            memcpy(new_buffer, mem->buffer, mem->size);
        }
        mem->buffer = new_buffer;
        mem->capacity = new_capacity;
    }

    if (mem->buffer) {
        memcpy(mem->buffer + mem->size, data, length);
        mem->size += length;
    }
}

static void png_memory_flush_callback(png_structp png_ptr) {
    (void)png_ptr;  // No-op for memory writes
}

uint8_t* png_encode(PNGImage* image, size_t* out_size, Arena* arena) {
    if (!image || !image->pixels || !out_size) return nullptr;

    PNGMemoryWrite mem;
    mem.buffer = (uint8_t*)arena_alloc(arena, 4096);
    mem.size = 0;
    mem.capacity = 4096;
    mem.arena = arena;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        return nullptr;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        return nullptr;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return nullptr;
    }

    // Set custom write function
    png_set_write_fn(png_ptr, &mem, png_memory_write_callback, png_memory_flush_callback);

    // Set image attributes
    png_set_IHDR(png_ptr, info_ptr,
        image->width, image->height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_write_info(png_ptr, info_ptr);

    // Write rows
    for (int y = 0; y < image->height; y++) {
        png_bytep row = image->pixels + y * image->stride;
        png_write_row(png_ptr, row);
    }

    png_write_end(png_ptr, nullptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    *out_size = mem.size;
    return mem.buffer;
}

} // namespace tex
