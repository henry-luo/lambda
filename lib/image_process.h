#pragma once

#include <stddef.h>
#include <stdint.h>

static inline int64_t image_edge_index(int64_t index, int64_t count) {
    return index<0 ? 0 : index>=count ? count-1 : index;
}
static inline double image_bilinear_mix(double p00, double p01, double p10, double p11,
    double fx, double fy) {
    return (p00*(1-fx)+p01*fx)*(1-fy)+(p10*(1-fx)+p11*fx)*fy;
}

// row callbacks keep neighborhood filtering independent of ArrayNum and GC.
typedef bool (*ImageReadRow)(void*, int64_t, double*);
typedef bool (*ImageWriteRow)(void*, int64_t, const double*);
bool image_box_filter(int64_t width, int64_t height, int64_t channels, int64_t kernel,
    ImageReadRow read, ImageWriteRow write, void* user, const bool* cancelled = nullptr);

enum { IMAGE_TONE_CHANNELS = 4, IMAGE_TONE_POINTS = 32 };

struct ImageProcessOptions {
    int turns = 0;
    bool flip_x = false, flip_y = false, compare = false, uncropped = false;
    double angle = 0, crop[4] = {0, 0, 1, 1};
    double exposure = 0, brightness = 0, contrast = 1, saturation = 1;
    double temperature = 0, gamma = 1, black = 0, white = 1;
    double shadows = 0, highlights = 0, intensity = 1;
    // rows are [R,G,B,offset]; presets are supplied as data by the caller.
    double matrix[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
    double output_black = 0, output_white = 1;
    // channels are master RGB, red, green and blue; plain arrays survive async option copies.
    double curves[IMAGE_TONE_CHANNELS][IMAGE_TONE_POINTS][2] = {};
    uint64_t curve_counts[IMAGE_TONE_CHANNELS] = {};
    double vignette = 0, softness = 0, sharpen = 0;
    int output_width = 0, output_height = 0;
};

bool image_process_tone_valid(const ImageProcessOptions& options);

struct ImageProcessSource {
    const uint8_t* pixels = nullptr;
    int width = 0, height = 0;
    int64_t strides[3] = {};
    uint64_t revision = 0;
};

struct ImageProcessResult {
    uint8_t* pixels = nullptr;
    bool owns_pixels = true;
    int width = 0, height = 0;
    size_t scratch_bytes = 0, cache_bytes = 0;
    uint64_t geometry_hits = 0, color_hits = 0, blur_hits = 0;
};

struct ImageProcessCache;
ImageProcessCache* image_process_cache_create(size_t budget);
void image_process_cache_destroy(ImageProcessCache* cache);
bool image_process(const ImageProcessSource& source, const ImageProcessOptions& options,
    ImageProcessResult* result, ImageProcessCache* cache = nullptr, const bool* cancelled = nullptr,
    uint8_t* destination = nullptr, size_t destination_bytes = 0);
bool image_process_dimensions(const ImageProcessSource& source, const ImageProcessOptions& options,
    int* width, int* height);
void image_process_result_free(ImageProcessResult* result);
