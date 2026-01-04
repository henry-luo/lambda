#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Image loading function compatible with stbi_load
// Returns RGBA data with 4 bytes per pixel
// Call image_free() to free the returned data
unsigned char* image_load(const char* filename, int* width, int* height, int* channels, int req_channels);

// Load image from memory buffer
// Returns RGBA data with 4 bytes per pixel
// Call image_free() to free the returned data
unsigned char* image_load_from_memory(const unsigned char* data, size_t length, int* width, int* height, int* channels);

// Free image data returned by image_load or image_load_from_memory
void image_free(unsigned char* data);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_H
