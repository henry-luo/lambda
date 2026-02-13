#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <turbojpeg.h>
#include <gif_lib.h>
#include "log.h"
#include "str.h"

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
    unsigned char* image_data = malloc(*width * *height * 4);
    if (!image_data) {
        log_error("Failed to allocate memory for PNG image data");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(fp);
        return NULL;
    }

    // Read the image data
    png_bytep* row_pointers = malloc(sizeof(png_bytep) * *height);
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = image_data + y * *width * 4;
    }

    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, NULL);

    // Clean up
    free(row_pointers);
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

    unsigned char* jpeg_buffer = malloc(file_size);
    if (!jpeg_buffer) {
        log_error("Failed to allocate memory for JPEG file buffer");
        fclose(fp);
        return NULL;
    }

    if (fread(jpeg_buffer, 1, file_size, fp) != (size_t)file_size) {
        log_error("Failed to read JPEG file: %s", filename);
        free(jpeg_buffer);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    // Initialize TurboJPEG decompressor
    tjhandle tj_instance = tjInitDecompress();
    if (!tj_instance) {
        log_error("Failed to initialize TurboJPEG decompressor: %s", tjGetErrorStr());
        free(jpeg_buffer);
        return NULL;
    }

    // Get JPEG header information
    int jpeg_width, jpeg_height, jpeg_subsamp, jpeg_colorspace;
    if (tjDecompressHeader3(tj_instance, jpeg_buffer, file_size, &jpeg_width, &jpeg_height, &jpeg_subsamp, &jpeg_colorspace) < 0) {
        log_error("Failed to read JPEG header: %s", tjGetErrorStr());
        tjDestroy(tj_instance);
        free(jpeg_buffer);
        return NULL;
    }

    *width = jpeg_width;
    *height = jpeg_height;
    *channels = 4; // Always return RGBA

    // Allocate memory for decompressed image
    unsigned char* image_data = malloc(*width * *height * 4);
    if (!image_data) {
        log_error("Failed to allocate memory for JPEG image data");
        tjDestroy(tj_instance);
        free(jpeg_buffer);
        return NULL;
    }

    // Decompress JPEG to RGBA
    if (tjDecompress2(tj_instance, jpeg_buffer, file_size, image_data, *width, 0, *height, TJPF_RGBA, TJFLAG_FASTDCT) < 0) {
        log_error("Failed to decompress JPEG: %s", tjGetErrorStr());
        free(image_data);
        tjDestroy(tj_instance);
        free(jpeg_buffer);
        return NULL;
    }

    // Clean up
    tjDestroy(tj_instance);
    free(jpeg_buffer);

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
    unsigned char* image_data = calloc(*width * *height * 4, 1);
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
            free(image_data);
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
    unsigned char* image_data = (unsigned char*)malloc(*width * *height * 4);
    if (!image_data) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        log_error("Failed to allocate memory for PNG image");
        return NULL;
    }

    // Create row pointers
    png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * *height);
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = image_data + y * (*width) * 4;
    }

    // Read image data
    png_read_image(png_ptr, row_pointers);

    free(row_pointers);
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
    unsigned char* image_data = (unsigned char*)malloc(*width * *height * 4);
    if (!image_data) {
        log_error("Failed to allocate memory for JPEG image data");
        tjDestroy(tj_instance);
        return NULL;
    }

    // Decompress JPEG to RGBA
    if (tjDecompress2(tj_instance, data, length, image_data, *width, 0, *height, TJPF_RGBA, TJFLAG_FASTDCT) < 0) {
        log_error("Failed to decompress JPEG: %s", tjGetErrorStr());
        free(image_data);
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
        free(data);
    }
}
