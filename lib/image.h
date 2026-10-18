#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "raster.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ImageCodecFormat {
    IMAGE_CODEC_UNKNOWN,
    IMAGE_CODEC_PNG,
    IMAGE_CODEC_JPEG,
    IMAGE_CODEC_GIF,
    IMAGE_CODEC_WEBP,
    IMAGE_CODEC_AVIF,
    IMAGE_CODEC_BMP,
    IMAGE_CODEC_TIFF,
    IMAGE_CODEC_ICO
} ImageCodecFormat;

typedef enum ImageStatus {
    IMAGE_OK,
    IMAGE_INVALID_ARGUMENT,
    IMAGE_UNKNOWN_FORMAT,
    IMAGE_CODEC_UNAVAILABLE,
    IMAGE_UNSUPPORTED_FEATURE,
    IMAGE_TRUNCATED_INPUT,
    IMAGE_INVALID_DATA,
    IMAGE_IO_ERROR,
    IMAGE_LIMIT_EXCEEDED,
    IMAGE_FAST_PATH_UNAVAILABLE,
    IMAGE_CANCELLED,
    IMAGE_OUT_OF_MEMORY,
    IMAGE_INTERNAL_ERROR
} ImageStatus;

typedef enum ImageDecodeMode {
    IMAGE_DECODE_FULL,
    IMAGE_DECODE_THUMBNAIL,
    IMAGE_DECODE_AT_LEAST
} ImageDecodeMode;
typedef enum ImageDecodeStrategy {
    IMAGE_STRATEGY_FULL,
    IMAGE_STRATEGY_NATIVE_SCALED,
    IMAGE_STRATEGY_SELECTED_RESOLUTION,
    IMAGE_STRATEGY_STREAMED_REDUCE,
    IMAGE_STRATEGY_FULL_RESIZE
} ImageDecodeStrategy;

typedef struct ImageError {
    ImageStatus code;
    ImageCodecFormat format;
    char operation[24];
    char message[256];
    int native_code;
    int os_error;
    uint64_t byte_offset;
    bool has_byte_offset;
} ImageError;

typedef struct ImageLimits {
    size_t max_encoded_bytes;
    size_t max_decoded_bytes;
    size_t max_scratch_bytes;
    uint64_t max_source_pixels;
    uint32_t max_dimension;
    uint32_t max_frames;
    uint32_t max_pages;
    size_t max_metadata_bytes;
    uint32_t max_threads;
} ImageLimits;

typedef struct ImageContext {
    ImageLimits limits;
    int (*cancelled)(void *user);
    void (*warning)(void *user, const ImageError *warning);
    void *user;
} ImageContext;

// readers return a short count only at EOF; the core supplies exact-read errors.
typedef size_t (*ImageReadAt)(void *user, uint64_t offset, uint8_t *data, size_t size);
typedef struct ImageSource {
    const uint8_t *data;
    size_t length;
    ImageReadAt read_at;
    void *user;
} ImageSource;

typedef size_t (*ImageWrite)(void *user, const uint8_t *data, size_t size);
typedef struct ImageSink {
    ImageWrite write;
    void *user;
} ImageSink;
typedef struct ImageEncoded {
    uint8_t *data;
    size_t length;
} ImageEncoded;

typedef struct ImageInfo {
    ImageCodecFormat format;
    uint32_t width, height;
    uint32_t display_width, display_height;
    uint32_t frame_count, page_count;
    uint16_t bit_depth;
    uint8_t orientation;
    bool has_alpha, frames_known, pages_known, has_icc;
    uint16_t color_primaries, transfer_characteristics, matrix_coefficients;
} ImageInfo;

typedef struct ImageDecodeOptions {
    ImageDecodeMode mode;
    uint32_t target_width, target_height;
    uint32_t page_index, frame_index;
    bool normalize_orientation;
    bool allow_full_decode;
} ImageDecodeOptions;

typedef struct ImageDecoded {
    uint8_t *pixels;
    uint32_t width, height;
    size_t stride, size;
    ImageInfo info;
    ImageDecodeStrategy strategy;
    uint32_t representation_index;
    uint32_t delay_ms, loop_count;
    bool full_decode_fallback;
    uint32_t source_delay_ms;
    bool has_loop_extension;
} ImageDecoded;
typedef struct ImageDecoder ImageDecoder;

typedef struct ImagePixelView {
    const uint8_t *pixels;
    size_t capacity, stride;
    uint32_t width, height;
    uint8_t channels; // 1=gray, 3=RGB, 4=straight RGBA
    bool premultiplied;
} ImagePixelView;

typedef struct ImageEncodeOptions {
    ImageCodecFormat format;
    int quality;
    bool lossless;
    bool has_background;
    uint8_t background[3];
} ImageEncodeOptions;

// callbacks fill one top-down row; the encoder owns and reuses the row buffer.
typedef ImageStatus (*ImageReadPixelRow)(void *user, uint32_t y, uint8_t *row, size_t size);
typedef struct ImageRowSource {
    ImageReadPixelRow read_row;
    void *user;
    uint32_t width, height;
    uint8_t channels;
    bool premultiplied;
} ImageRowSource;

typedef struct ImageCapabilities {
    const char *name;
    const char *mime;
    bool read, write, animation, pages, native_scaling;
} ImageCapabilities;

void image_context_init(ImageContext *context);
void image_decode_options_init(ImageDecodeOptions *options);
void image_encode_options_init(ImageEncodeOptions *options, ImageCodecFormat format);
const char *image_status_name(ImageStatus status);
const ImageCapabilities *image_codec_capabilities(ImageCodecFormat format);
ImageCodecFormat image_detect(const uint8_t *data, size_t size);
ImageStatus image_probe(ImageContext *context, const ImageSource *source, ImageInfo *info, ImageError *error);
ImageStatus image_decode(ImageContext *context, const ImageSource *source, const ImageDecodeOptions *options,
                         ImageDecoded *result, ImageError *error);
ImageStatus image_encode(ImageContext *context, const ImagePixelView *pixels,
                         const ImageEncodeOptions *options, const ImageSink *sink, ImageError *error);
// encoded memory is caller-owned and released with image_free().
ImageStatus image_encode_to_memory(ImageContext *context, const ImagePixelView *pixels,
                                   const ImageEncodeOptions *options, ImageEncoded *result,
                                   ImageError *error);
// streaming row encoding currently supports PNG; other formats report unsupported_feature.
ImageStatus image_encode_rows_to_memory(ImageContext *context, const ImageRowSource *rows,
                                        const ImageEncodeOptions *options, ImageEncoded *result,
                                        ImageError *error);
// the session borrows the source and context; returned frames own their pixels.
ImageStatus image_decoder_open(ImageContext *context, const ImageSource *source,
                               const ImageDecodeOptions *options, ImageDecoder **decoder, ImageError *error);
ImageStatus image_decoder_read_frame(ImageDecoder *decoder, uint32_t frame_index, ImageDecoded *result,
                                     ImageError *error);
void image_decoder_close(ImageDecoder *decoder);
void image_decoded_dispose(ImageDecoded *image);
uint8_t image_unpremultiply_channel(uint8_t channel, uint8_t alpha);
void image_unpremultiply_rgba_row(uint8_t *destination, const uint8_t *source, size_t count);

// convenience sources are borrowed for the duration of each synchronous call.
ImageSource image_source_memory(const void *data, size_t length);

// compatibility byte APIs preserve RGBA8 storage and raw orientation.
unsigned char *image_load_from_memory(const unsigned char *data, size_t length, int *width, int *height,
                                      int *channels);
unsigned char *image_load_from_memory_scaled(const unsigned char *data, size_t length, int target_w,
                                             int target_h, int *width, int *height, int *channels);
void image_free(unsigned char *data);
int image_get_dimensions_from_memory(const unsigned char *data, size_t length, int *width, int *height);
int image_jpeg_exif_orientation_from_memory(const unsigned char *data, size_t length);
const char *image_mime_type_from_memory(const unsigned char *data, size_t length);

// composited GIF frames retain the existing disposal/timing/loop contract.
typedef struct GifFrameData {
    uint32_t *pixels;
    int delay_ms;
    int disposal;
    int source_delay_ms; // exact encoded delay; playback may apply its own minimum.
} GifFrameData;
typedef struct GifFrames {
    GifFrameData *frames;
    int frame_count;
    int width, height;
    int loop_count;
    bool has_loop_extension;
} GifFrames;
GifFrames *image_gif_load_from_memory(const unsigned char *data, size_t length);
void image_gif_free(GifFrames *gif);
int image_gif_frame_count_from_memory(const unsigned char *data, size_t length);
ImageStatus image_encode_gif_frames(ImageContext* context, const RasterBuffer* const* frames,
    const uint16_t* delays, uint32_t count, uint32_t plays, ImageEncoded* encoded, ImageError* error);

// file/resource entry points are declared by lambda/io/image_io.h (D7.1.3).
#ifdef __cplusplus
}
#endif

#endif // IMAGE_H
