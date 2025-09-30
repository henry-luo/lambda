#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <turbojpeg.h>
#include "log.h"

// Helper function to determine image format from file extension
typedef enum {
    IMAGE_TYPE_UNKNOWN,
    IMAGE_TYPE_PNG,
    IMAGE_TYPE_JPEG
} ImageType;

static ImageType get_image_type(const char* filename) {
    if (!filename) return IMAGE_TYPE_UNKNOWN;

    int len = strlen(filename);
    if (len < 4) return IMAGE_TYPE_UNKNOWN;

    const char* ext = filename + len - 4;
    if (strcasecmp(ext, ".png") == 0) {
        return IMAGE_TYPE_PNG;
    }

    if (strcasecmp(ext, ".jpg") == 0) {
        return IMAGE_TYPE_JPEG;
    }

    if (len >= 5) {
        ext = filename + len - 5;
        if (strcasecmp(ext, ".jpeg") == 0) {
            return IMAGE_TYPE_JPEG;
        }
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
