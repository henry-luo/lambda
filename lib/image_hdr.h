#pragma once
// Radiance RGBE (.hdr) images: the scene-referred light that HDR environments carry.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Linear RGB floats, three per pixel, rows top to bottom.
typedef struct HdrImage {
    float* pixels;
    uint32_t width, height;
} HdrImage;

// Decode a `-Y height +X width` image with flat or adaptive run-length scanlines, converting
// each RGBE texel as Three.js r186's RGBELoader does (mantissa / 255 * 2^(exponent - 128)), so
// both renderers see the same values. Either dimension above `max_dimension` is rejected. On
// failure `*error` names the defect and `image` stays empty.
bool hdr_read(const uint8_t* bytes, size_t length, uint32_t max_dimension, HdrImage* image, const char** error);
void hdr_image_release(HdrImage* image);

#ifdef __cplusplus
}
#endif
