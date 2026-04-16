#include "image.h"
#include "memtrack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <turbojpeg.h>
#include <gif_lib.h>
#include "log.h"
#include "str.h"
#include "memtrack.h"

// Helper function to determine image format from file extension
typedef enum {
    IMAGE_TYPE_UNKNOWN,
    IMAGE_TYPE_PNG,
    IMAGE_TYPE_JPEG,
    IMAGE_TYPE_GIF
} ImageType;

static ImageType get_image_type(const char* filename) {
    if (!filename) return IMAGE_TYPE_UNKNOWN;

    int len = strlen(filename);
    if (len < 4) return IMAGE_TYPE_UNKNOWN;

    const char* ext = filename + len - 4;
    if (str_ieq_const(ext, strlen(ext), ".png")) {
        return IMAGE_TYPE_PNG;
    }

    if (str_ieq_const(ext, strlen(ext), ".jpg")) {
        return IMAGE_TYPE_JPEG;
    }

    if (len >= 5) {
        ext = filename + len - 5;
        if (str_ieq_const(ext, strlen(ext), ".jpeg")) {
            return IMAGE_TYPE_JPEG;
        }
    }

    ext = filename + len - 4;
    if (str_ieq_const(ext, strlen(ext), ".gif")) {
        return IMAGE_TYPE_GIF;
    }

    return IMAGE_TYPE_UNKNOWN;
}

// Load PNG image using libpng
static unsigned char* load_png(const char* filename, int* width, int* height, int* channels, int req_channels) {
    (void)req_channels; // Mark as unused for compatibility
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        log_error("Failed to open PNG file: %s", filename);
        return NULL;
    }

    // Check PNG signature
    unsigned char header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        log_error("File is not a valid PNG: %s", filename);
        fclose(fp);
        return NULL;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        log_error("Failed to create PNG read struct");
        fclose(fp);
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        log_error("Failed to create PNG info struct");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        log_error("Error during PNG reading");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    *width = png_get_image_width(png_ptr, info_ptr);
    *height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // Convert various PNG formats to RGBA
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    png_read_update_info(png_ptr, info_ptr);

    // Allocate memory for image data
    *channels = 4; // Always return RGBA
    unsigned char* image_data = mem_alloc(*width * *height * 4, MEM_CAT_IMAGE);
    if (!image_data) {
        log_error("Failed to allocate memory for PNG image data");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    // Read the image data
    png_bytep* row_pointers = mem_alloc(sizeof(png_bytep) * *height, MEM_CAT_IMAGE);
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = image_data + y * *width * 4;
    }

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, NULL);

    // Clean up
    mem_free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);

    return image_data;
}

// Load JPEG image using TurboJPEG
static unsigned char* load_jpeg(const char* filename, int* width, int* height, int* channels, int req_channels) {
    (void)req_channels; // Mark as unused for compatibility
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        log_error("Failed to open JPEG file: %s", filename);
        return NULL;
    }

    // Read entire file into memory
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* jpeg_buffer = mem_alloc(file_size, MEM_CAT_IMAGE);
    if (!jpeg_buffer) {
        log_error("Failed to allocate memory for JPEG file buffer");
        fclose(fp);
        return NULL;
    }

    if (fread(jpeg_buffer, 1, file_size, fp) != (size_t)file_size) {
        log_error("Failed to read JPEG file: %s", filename);
        mem_free(jpeg_buffer);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    // Initialize TurboJPEG decompressor
    tjhandle tj_instance = tjInitDecompress();
    if (!tj_instance) {
        log_error("Failed to initialize TurboJPEG decompressor: %s", tjGetErrorStr());
        mem_free(jpeg_buffer);
        return NULL;
    }

    // Get JPEG header information
    int jpeg_width, jpeg_height, jpeg_subsamp, jpeg_colorspace;
    if (tjDecompressHeader3(tj_instance, jpeg_buffer, file_size, &jpeg_width, &jpeg_height, &jpeg_subsamp, &jpeg_colorspace) < 0) {
        log_error("Failed to read JPEG header: %s", tjGetErrorStr());
        tjDestroy(tj_instance);
        mem_free(jpeg_buffer);
        return NULL;
    }

    *width = jpeg_width;
    *height = jpeg_height;
    *channels = 4; // Always return RGBA

    // Allocate memory for decompressed image
    unsigned char* image_data = mem_alloc(*width * *height * 4, MEM_CAT_IMAGE);
    if (!image_data) {
        log_error("Failed to allocate memory for JPEG image data");
        tjDestroy(tj_instance);
        mem_free(jpeg_buffer);
        return NULL;
    }

    // Decompress JPEG to RGBA
    if (tjDecompress2(tj_instance, jpeg_buffer, file_size, image_data, *width, 0, *height, TJPF_RGBA, TJFLAG_FASTDCT) < 0) {
        log_error("Failed to decompress JPEG: %s", tjGetErrorStr());
        mem_free(image_data);
        tjDestroy(tj_instance);
        mem_free(jpeg_buffer);
        return NULL;
    }

    // Clean up
    tjDestroy(tj_instance);
    mem_free(jpeg_buffer);

    return image_data;
}

// Load GIF image using giflib
static unsigned char* load_gif(const char* filename, int* width, int* height, int* channels, int req_channels) {
    (void)req_channels; // Mark as unused for compatibility

    int error_code;
    GifFileType* gif = DGifOpenFileName(filename, &error_code);
    if (!gif) {
        log_error("Failed to open GIF file: %s (error: %d)", filename, error_code);
        return NULL;
    }

    // Read the entire GIF file
    if (DGifSlurp(gif) == GIF_ERROR) {
        log_error("Failed to read GIF file: %s (error: %d)", filename, gif->Error);
        DGifCloseFile(gif, &error_code);
        return NULL;
    }

    *width = gif->SWidth;
    *height = gif->SHeight;
    *channels = 4; // Always return RGBA

    // Allocate memory for RGBA image
    unsigned char* image_data = mem_calloc(*width * *height * 4, 1, MEM_CAT_IMAGE);
    if (!image_data) {
        log_error("Failed to allocate memory for GIF image data");
        DGifCloseFile(gif, &error_code);
        return NULL;
    }

    // Get the first frame (for animated GIFs, we only load the first frame)
    if (gif->ImageCount > 0) {
        SavedImage* frame = &gif->SavedImages[0];
        GifImageDesc* desc = &frame->ImageDesc;

        // Get the color map
        ColorMapObject* color_map = desc->ColorMap ? desc->ColorMap : gif->SColorMap;
        if (!color_map) {
            log_error("No color map found in GIF file: %s", filename);
            mem_free(image_data);
            DGifCloseFile(gif, &error_code);
            return NULL;
        }

        // Check for transparency
        int transparent_color = -1;
        if (frame->ExtensionBlockCount > 0) {
            for (int i = 0; i < frame->ExtensionBlockCount; i++) {
                ExtensionBlock* ext = &frame->ExtensionBlocks[i];
                if (ext->Function == GRAPHICS_EXT_FUNC_CODE && ext->ByteCount >= 4) {
                    if (ext->Bytes[0] & 0x01) {
                        transparent_color = ext->Bytes[3];
                    }
                    break;
                }
            }
        }

        // Convert indexed color to RGBA
        GifByteType* raster = frame->RasterBits;
        int frame_width = desc->Width;
        int frame_height = desc->Height;
        int left = desc->Left;
        int top = desc->Top;

        for (int y = 0; y < frame_height; y++) {
            for (int x = 0; x < frame_width; x++) {
                int dst_y = top + y;
                int dst_x = left + x;

                if (dst_x >= 0 && dst_x < *width && dst_y >= 0 && dst_y < *height) {
                    int dst_idx = (dst_y * *width + dst_x) * 4;
                    int src_idx = y * frame_width + x;
                    int color_index = raster[src_idx];

                    if (color_index == transparent_color) {
                        // Transparent pixel
                        image_data[dst_idx + 0] = 0;
                        image_data[dst_idx + 1] = 0;
                        image_data[dst_idx + 2] = 0;
                        image_data[dst_idx + 3] = 0;
                    } else if (color_index < color_map->ColorCount) {
                        GifColorType* color = &color_map->Colors[color_index];
                        image_data[dst_idx + 0] = color->Red;
                        image_data[dst_idx + 1] = color->Green;
                        image_data[dst_idx + 2] = color->Blue;
                        image_data[dst_idx + 3] = 255;
                    }
                }
            }
        }
    }

    DGifCloseFile(gif, &error_code);
    return image_data;
}

// Determine image type from magic bytes
static ImageType get_image_type_from_memory(const unsigned char* data, size_t length) {
    if (!data || length < 8) return IMAGE_TYPE_UNKNOWN;

    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if (length >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47 &&
        data[4] == 0x0D && data[5] == 0x0A && data[6] == 0x1A && data[7] == 0x0A) {
        return IMAGE_TYPE_PNG;
    }

    // JPEG signature: FF D8 FF
    if (length >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return IMAGE_TYPE_JPEG;
    }

    // GIF signature: GIF87a or GIF89a
    if (length >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
        data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
        return IMAGE_TYPE_GIF;
    }

    return IMAGE_TYPE_UNKNOWN;
}

// Structure for PNG memory reading
typedef struct {
    const unsigned char* data;
    size_t size;
    size_t offset;
} PngMemoryReader;

// PNG memory read callback function
static void png_read_from_memory(png_structp png_ptr, png_bytep out_data, png_size_t bytes_to_read) {
    PngMemoryReader* reader = (PngMemoryReader*)png_get_io_ptr(png_ptr);
    if (reader->offset + bytes_to_read > reader->size) {
        png_error(png_ptr, "Read past end of data");
        return;
    }
    memcpy(out_data, reader->data + reader->offset, bytes_to_read);
    reader->offset += bytes_to_read;
}

// Load PNG from memory using libpng
static unsigned char* load_png_from_memory(const unsigned char* data, size_t length, int* width, int* height, int* channels) {
    // Create PNG read struct
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        log_error("Failed to create PNG read struct");
        return NULL;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        log_error("Failed to create PNG info struct");
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        log_error("PNG decompression error");
        return NULL;
    }

    // Set up memory reading
    PngMemoryReader reader = { .data = data, .size = length, .offset = 0 };
    png_set_read_fn(png_ptr, &reader, png_read_from_memory);

    // Read PNG info
    png_read_info(png_ptr, info_ptr);

    *width = png_get_image_width(png_ptr, info_ptr);
    *height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // Transform to RGBA
    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png_ptr);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    *channels = 4;

    // Allocate memory for image
    unsigned char* image_data = (unsigned char*)mem_alloc(*width * *height * 4, MEM_CAT_IMAGE);
    if (!image_data) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        log_error("Failed to allocate memory for PNG image");
        return NULL;
    }

    // Create row pointers
    png_bytep* row_pointers = (png_bytep*)mem_alloc(sizeof(png_bytep) * *height, MEM_CAT_IMAGE);
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = image_data + y * (*width) * 4;
    }

    // Read image data
    png_read_image(png_ptr, row_pointers);

    mem_free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return image_data;
}

// Load JPEG from memory using TurboJPEG
static unsigned char* load_jpeg_from_memory(const unsigned char* data, size_t length, int* width, int* height, int* channels) {
    // Initialize TurboJPEG decompressor
    tjhandle tj_instance = tjInitDecompress();
    if (!tj_instance) {
        log_error("Failed to initialize TurboJPEG decompressor: %s", tjGetErrorStr());
        return NULL;
    }

    // Get JPEG header information
    int jpeg_width, jpeg_height, jpeg_subsamp, jpeg_colorspace;
    if (tjDecompressHeader3(tj_instance, data, length, &jpeg_width, &jpeg_height, &jpeg_subsamp, &jpeg_colorspace) < 0) {
        log_error("Failed to read JPEG header: %s", tjGetErrorStr());
        tjDestroy(tj_instance);
        return NULL;
    }

    *width = jpeg_width;
    *height = jpeg_height;
    *channels = 4; // Always return RGBA

    // Allocate memory for decompressed image
    unsigned char* image_data = (unsigned char*)mem_alloc(*width * *height * 4, MEM_CAT_IMAGE);
    if (!image_data) {
        log_error("Failed to allocate memory for JPEG image data");
        tjDestroy(tj_instance);
        return NULL;
    }

    // Decompress JPEG to RGBA
    if (tjDecompress2(tj_instance, data, length, image_data, *width, 0, *height, TJPF_RGBA, TJFLAG_FASTDCT) < 0) {
        log_error("Failed to decompress JPEG: %s", tjGetErrorStr());
        mem_free(image_data);
        tjDestroy(tj_instance);
        return NULL;
    }

    tjDestroy(tj_instance);
    return image_data;
}

// Load image from memory buffer
unsigned char* image_load_from_memory(const unsigned char* data, size_t length, int* width, int* height, int* channels) {
    if (!data || length == 0 || !width || !height || !channels) {
        log_error("Invalid parameters passed to image_load_from_memory");
        return NULL;
    }

    ImageType type = get_image_type_from_memory(data, length);

    switch (type) {
        case IMAGE_TYPE_PNG:
            return load_png_from_memory(data, length, width, height, channels);

        case IMAGE_TYPE_JPEG:
            return load_jpeg_from_memory(data, length, width, height, channels);

        case IMAGE_TYPE_GIF:
            // GIF from memory not yet implemented - fall through
            log_warn("GIF loading from memory not yet implemented");
            return NULL;

        default:
            log_error("Unsupported or unrecognized image format in memory buffer");
            return NULL;
    }
}

// Main image loading function compatible with stbi_load
unsigned char* image_load(const char* filename, int* width, int* height, int* channels, int req_channels) {
    if (!filename || !width || !height || !channels) {
        log_error("Invalid parameters passed to image_load");
        return NULL;
    }

    ImageType type = get_image_type(filename);

    switch (type) {
        case IMAGE_TYPE_PNG:
            return load_png(filename, width, height, channels, req_channels);

        case IMAGE_TYPE_JPEG:
            return load_jpeg(filename, width, height, channels, req_channels);

        case IMAGE_TYPE_GIF:
            return load_gif(filename, width, height, channels, req_channels);

        default:
            log_error("Unsupported image format: %s", filename);
            return NULL;
    }
}

// Free image data returned by image_load
void image_free(unsigned char* data) {
    if (data) {
        mem_free(data);
    }
}

// ============================================================================
// Header-only dimension reading (no pixel decode)
// ============================================================================

// Get PNG dimensions from file (reads only IHDR chunk header)
static int get_png_dimensions(const char* filename, int* width, int* height) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return 0;

    // PNG header: 8-byte signature + 4-byte chunk length + 4-byte "IHDR" + 4-byte width + 4-byte height = 24 bytes
    unsigned char header[24];
    if (fread(header, 1, 24, fp) != 24) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    // Verify PNG signature
    if (png_sig_cmp(header, 0, 8)) return 0;

    // Width and height are big-endian uint32 at offsets 16 and 20
    *width  = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];
    *height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23];
    return (*width > 0 && *height > 0) ? 1 : 0;
}

// Get JPEG dimensions from file (reads only header via TurboJPEG)
static int get_jpeg_dimensions(const char* filename, int* width, int* height) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return 0;

    // Read enough for JPEG header (typically < 1KB)
    // We read a small buffer; tjDecompressHeader3 only needs the header
    unsigned char buf[4096];
    size_t nread = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (nread < 3) return 0;

    tjhandle tj = tjInitDecompress();
    if (!tj) return 0;

    int w, h, subsamp, colorspace;
    int ok = (tjDecompressHeader3(tj, buf, nread, &w, &h, &subsamp, &colorspace) == 0);
    tjDestroy(tj);

    if (ok) {
        *width = w;
        *height = h;
    }
    return ok;
}

// Get GIF dimensions from file (reads only header)
static int get_gif_dimensions(const char* filename, int* width, int* height) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return 0;

    // GIF header: 6-byte signature + 2-byte width + 2-byte height (little-endian)
    unsigned char header[10];
    if (fread(header, 1, 10, fp) != 10) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    // Verify GIF signature
    if (header[0] != 'G' || header[1] != 'I' || header[2] != 'F' ||
        header[3] != '8' || (header[4] != '7' && header[4] != '9') || header[5] != 'a') {
        return 0;
    }

    *width  = header[6] | (header[7] << 8);
    *height = header[8] | (header[9] << 8);
    return (*width > 0 && *height > 0) ? 1 : 0;
}

int image_get_dimensions(const char* filename, int* width, int* height) {
    if (!filename || !width || !height) return 0;

    ImageType type = get_image_type(filename);
    switch (type) {
        case IMAGE_TYPE_PNG:  return get_png_dimensions(filename, width, height);
        case IMAGE_TYPE_JPEG: return get_jpeg_dimensions(filename, width, height);
        case IMAGE_TYPE_GIF:  return get_gif_dimensions(filename, width, height);
        default: return 0;
    }
}

// Get PNG dimensions from memory
static int get_png_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height) {
    if (length < 24) return 0;
    if (png_sig_cmp((png_const_bytep)data, 0, 8)) return 0;
    *width  = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    *height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    return (*width > 0 && *height > 0) ? 1 : 0;
}

// Get JPEG dimensions from memory
static int get_jpeg_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height) {
    if (length < 3) return 0;
    tjhandle tj = tjInitDecompress();
    if (!tj) return 0;
    int w, h, subsamp, colorspace;
    int ok = (tjDecompressHeader3(tj, data, length, &w, &h, &subsamp, &colorspace) == 0);
    tjDestroy(tj);
    if (ok) { *width = w; *height = h; }
    return ok;
}

// Get GIF dimensions from memory
static int get_gif_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height) {
    if (length < 10) return 0;
    if (data[0] != 'G' || data[1] != 'I' || data[2] != 'F' ||
        data[3] != '8' || (data[4] != '7' && data[4] != '9') || data[5] != 'a') {
        return 0;
    }
    *width  = data[6] | (data[7] << 8);
    *height = data[8] | (data[9] << 8);
    return (*width > 0 && *height > 0) ? 1 : 0;
}

int image_get_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height) {
    if (!data || length == 0 || !width || !height) return 0;

    ImageType type = get_image_type_from_memory(data, length);
    switch (type) {
        case IMAGE_TYPE_PNG:  return get_png_dimensions_from_memory(data, length, width, height);
        case IMAGE_TYPE_JPEG: return get_jpeg_dimensions_from_memory(data, length, width, height);
        case IMAGE_TYPE_GIF:  return get_gif_dimensions_from_memory(data, length, width, height);
        default: return 0;
    }
}

// ============================================================================
// Multi-frame GIF support (giflib-based)
// ============================================================================

// Composite a single GIF frame onto a canvas, respecting frame position and transparency.
// canvas must be width*height*4 bytes (RGBA).
static void gif_composite_frame(uint32_t* canvas, int canvas_w, int canvas_h,
                                 GifFileType* gif, int frame_idx) {
    SavedImage* frame = &gif->SavedImages[frame_idx];
    GifImageDesc* desc = &frame->ImageDesc;
    ColorMapObject* cmap = desc->ColorMap ? desc->ColorMap : gif->SColorMap;
    if (!cmap) return;

    // Extract transparency from Graphics Control Block
    GraphicsControlBlock gcb;
    int transparent_color = -1;
    if (DGifSavedExtensionToGCB(gif, frame_idx, &gcb) == GIF_OK) {
        transparent_color = gcb.TransparentColor;
    }

    GifByteType* raster = frame->RasterBits;
    int fw = desc->Width, fh = desc->Height;
    int left = desc->Left, top = desc->Top;

    for (int y = 0; y < fh; y++) {
        int dy = top + y;
        if (dy < 0 || dy >= canvas_h) continue;
        for (int x = 0; x < fw; x++) {
            int dx = left + x;
            if (dx < 0 || dx >= canvas_w) continue;
            int ci = raster[y * fw + x];
            if (ci == transparent_color) continue;
            if (ci >= cmap->ColorCount) continue;
            GifColorType* c = &cmap->Colors[ci];
            uint32_t pixel = ((uint32_t)c->Red) | ((uint32_t)c->Green << 8) |
                             ((uint32_t)c->Blue << 16) | (0xFFu << 24);
            canvas[dy * canvas_w + dx] = pixel;
        }
    }
}

// Internal: decode all frames from an already-slurped GifFileType.
// Returns NULL if < 2 frames. Caller must free with image_gif_free().
static GifFrames* gif_decode_all_frames(GifFileType* gif) {
    if (!gif || gif->ImageCount < 2) return NULL;

    int w = gif->SWidth;
    int h = gif->SHeight;
    int n = gif->ImageCount;

    GifFrames* result = (GifFrames*)mem_calloc(1, sizeof(GifFrames), MEM_CAT_IMAGE);
    result->width = w;
    result->height = h;
    result->frame_count = n;
    result->loop_count = 0;  // default: infinite

    // Check for NETSCAPE 2.0 loop extension
    for (int i = 0; i < gif->ExtensionBlockCount; i++) {
        ExtensionBlock* ext = &gif->ExtensionBlocks[i];
        if (ext->Function == APPLICATION_EXT_FUNC_CODE && ext->ByteCount >= 11) {
            if (memcmp(ext->Bytes, "NETSCAPE2.0", 11) == 0 && (i + 1) < gif->ExtensionBlockCount) {
                ExtensionBlock* sub = &gif->ExtensionBlocks[i + 1];
                if (sub->ByteCount >= 3 && sub->Bytes[0] == 1) {
                    result->loop_count = sub->Bytes[1] | (sub->Bytes[2] << 8);
                }
            }
        }
    }

    result->frames = (GifFrameData*)mem_calloc(n, sizeof(GifFrameData), MEM_CAT_IMAGE);

    // Background color for disposal
    uint32_t bg_pixel = 0;  // transparent black
    if (gif->SColorMap && gif->SBackGroundColor < gif->SColorMap->ColorCount) {
        GifColorType* bgc = &gif->SColorMap->Colors[gif->SBackGroundColor];
        bg_pixel = ((uint32_t)bgc->Red) | ((uint32_t)bgc->Green << 8) |
                   ((uint32_t)bgc->Blue << 16) | (0xFFu << 24);
    }

    // Canvas for progressive compositing
    size_t canvas_bytes = (size_t)w * h * sizeof(uint32_t);
    uint32_t* canvas = (uint32_t*)mem_calloc(w * h, sizeof(uint32_t), MEM_CAT_IMAGE);
    uint32_t* prev_canvas = NULL;  // for disposal method 3 (restore previous)

    for (int i = 0; i < n; i++) {
        // Get graphics control block for this frame
        GraphicsControlBlock gcb;
        int disposal = DISPOSAL_UNSPECIFIED;
        int delay_ms = 100;  // default 100ms (GIF spec: 10cs = 100ms)
        if (DGifSavedExtensionToGCB(gif, i, &gcb) == GIF_OK) {
            disposal = gcb.DisposalMode;
            delay_ms = gcb.DelayTime * 10;  // convert centiseconds to ms
            if (delay_ms <= 0) delay_ms = 100;  // browsers use 100ms for 0-delay
        }

        // Save canvas before compositing for disposal method 3
        if (disposal == DISPOSE_PREVIOUS) {
            if (!prev_canvas) {
                prev_canvas = (uint32_t*)mem_calloc(w * h, sizeof(uint32_t), MEM_CAT_IMAGE);
            }
            memcpy(prev_canvas, canvas, canvas_bytes);
        }

        // Composite this frame onto canvas
        gif_composite_frame(canvas, w, h, gif, i);

        // Snapshot the composited canvas as this frame's pixels
        result->frames[i].pixels = (uint32_t*)mem_calloc(w * h, sizeof(uint32_t), MEM_CAT_IMAGE);
        memcpy(result->frames[i].pixels, canvas, canvas_bytes);
        result->frames[i].delay_ms = delay_ms;
        result->frames[i].disposal = disposal;

        // Apply disposal for next frame
        switch (disposal) {
            case DISPOSE_BACKGROUND: {
                // Clear the frame area to background
                SavedImage* frame = &gif->SavedImages[i];
                GifImageDesc* desc = &frame->ImageDesc;
                for (int y = desc->Top; y < desc->Top + desc->Height && y < h; y++) {
                    for (int x = desc->Left; x < desc->Left + desc->Width && x < w; x++) {
                        canvas[y * w + x] = bg_pixel;
                    }
                }
                break;
            }
            case DISPOSE_PREVIOUS:
                // Restore canvas to state before this frame
                if (prev_canvas) {
                    memcpy(canvas, prev_canvas, canvas_bytes);
                }
                break;
            default:
                // DISPOSAL_UNSPECIFIED / DISPOSE_DO_NOT: leave canvas as-is
                break;
        }
    }

    mem_free(canvas);
    if (prev_canvas) mem_free(prev_canvas);

    return result;
}

// Memory read context for giflib InputFunc callback
typedef struct GifMemoryReader {
    const unsigned char* data;
    size_t size;
    size_t pos;
} GifMemoryReader;

static int gif_memory_read_func(GifFileType* gif, GifByteType* buf, int size) {
    GifMemoryReader* reader = (GifMemoryReader*)gif->UserData;
    size_t avail = reader->size - reader->pos;
    size_t to_read = (size_t)size;
    if (to_read > avail) to_read = avail;
    if (to_read > 0) {
        memcpy(buf, reader->data + reader->pos, to_read);
        reader->pos += to_read;
    }
    return (int)to_read;
}

GifFrames* image_gif_load(const char* filename) {
    int error_code;
    GifFileType* gif = DGifOpenFileName(filename, &error_code);
    if (!gif) {
        log_error("gif multi-frame: failed to open %s (error %d)", filename, error_code);
        return NULL;
    }
    if (DGifSlurp(gif) == GIF_ERROR) {
        log_error("gif multi-frame: failed to slurp %s (error %d)", filename, gif->Error);
        DGifCloseFile(gif, &error_code);
        return NULL;
    }
    GifFrames* result = gif_decode_all_frames(gif);
    DGifCloseFile(gif, &error_code);
    return result;
}

GifFrames* image_gif_load_from_memory(const unsigned char* data, size_t length) {
    if (!data || length == 0) return NULL;

    GifMemoryReader reader = { data, length, 0 };
    int error_code;
    GifFileType* gif = DGifOpen(&reader, gif_memory_read_func, &error_code);
    if (!gif) {
        log_error("gif multi-frame: failed to open from memory (error %d)", error_code);
        return NULL;
    }
    if (DGifSlurp(gif) == GIF_ERROR) {
        log_error("gif multi-frame: failed to slurp from memory (error %d)", gif->Error);
        DGifCloseFile(gif, &error_code);
        return NULL;
    }
    GifFrames* result = gif_decode_all_frames(gif);
    DGifCloseFile(gif, &error_code);
    return result;
}

void image_gif_free(GifFrames* gif) {
    if (!gif) return;
    for (int i = 0; i < gif->frame_count; i++) {
        if (gif->frames[i].pixels) mem_free(gif->frames[i].pixels);
    }
    mem_free(gif->frames);
    mem_free(gif);
}

int image_gif_frame_count(const char* filename) {
    int error_code;
    GifFileType* gif = DGifOpenFileName(filename, &error_code);
    if (!gif) return 0;
    if (DGifSlurp(gif) == GIF_ERROR) {
        DGifCloseFile(gif, &error_code);
        return 0;
    }
    int count = gif->ImageCount;
    DGifCloseFile(gif, &error_code);
    return count;
}

int image_gif_frame_count_from_memory(const unsigned char* data, size_t length) {
    if (!data || length == 0) return 0;
    GifMemoryReader reader = { data, length, 0 };
    int error_code;
    GifFileType* gif = DGifOpen(&reader, gif_memory_read_func, &error_code);
    if (!gif) return 0;
    if (DGifSlurp(gif) == GIF_ERROR) {
        DGifCloseFile(gif, &error_code);
        return 0;
    }
    int count = gif->ImageCount;
    DGifCloseFile(gif, &error_code);
    return count;
}
