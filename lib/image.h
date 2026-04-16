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

// Get image dimensions without decoding pixel data (header-only read)
// Returns 1 on success, 0 on failure
int image_get_dimensions(const char* filename, int* width, int* height);

// Get image dimensions from memory buffer without decoding pixel data
// Returns 1 on success, 0 on failure
int image_get_dimensions_from_memory(const unsigned char* data, size_t length, int* width, int* height);

// ============================================================================
// Multi-frame GIF support
// ============================================================================

typedef struct GifFrameData {
    uint32_t* pixels;       // RGBA 32bpp (width * height pixels, caller frees via image_gif_free)
    int delay_ms;           // display duration in milliseconds (from GIF metadata)
    int disposal;           // disposal method: 0=unspecified, 1=none, 2=background, 3=previous
} GifFrameData;

typedef struct GifFrames {
    GifFrameData* frames;
    int frame_count;
    int width, height;
    int loop_count;         // 0 = infinite loop (NETSCAPE extension default)
} GifFrames;

// Load all frames from a GIF file. Returns NULL on failure or if only 1 frame.
// Caller must free with image_gif_free().
GifFrames* image_gif_load(const char* filename);

// Load all frames from a GIF in memory. Returns NULL on failure or if only 1 frame.
// Caller must free with image_gif_free().
GifFrames* image_gif_load_from_memory(const unsigned char* data, size_t length);

// Free a GifFrames struct and all frame pixel buffers.
void image_gif_free(GifFrames* gif);

// Get number of frames in a GIF file without decoding. Returns 0 on error, 1+ on success.
int image_gif_frame_count(const char* filename);

// Get number of frames in a GIF from memory. Returns 0 on error, 1+ on success.
int image_gif_frame_count_from_memory(const unsigned char* data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_H
