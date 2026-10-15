#ifndef LAMBDA_IMAGE_INTERNAL_H
#define LAMBDA_IMAGE_INTERNAL_H
#include "../image.h"
#include "../memtrack.h"
#include "../endian.h"
#include "../math_checked.hpp"
#include <string.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ImageAllocation ImageAllocation;
typedef struct ImageOperation {
    ImageContext defaults;
    ImageContext *context;
    ImageError local_error;
    ImageError *error;
    ImageCodecFormat format;
    const char *name;
    size_t scratch_bytes, decoded_bytes;
    ImageAllocation *allocations;
    const ImageSource *cached_source;
    const uint8_t *cached_bytes;
    const ImageSink *tracked_sink;
    size_t sink_bytes;
} ImageOperation;

typedef struct ImageByteBuffer {
    ImageOperation *op;
    uint8_t *data;
    size_t length, capacity;
} ImageByteBuffer;

typedef struct ImageReducer {
    ImageOperation *op;
    ImageDecoded *output;
    double *sums;
    uint32_t source_width, source_height, next_row;
} ImageReducer;

void image_op_init(ImageOperation *op, ImageContext *context, ImageError *error, const char *name);
void image_op_cleanup(ImageOperation *op);
void *image_op_alloc(ImageOperation *op, size_t bytes, bool pixels);
void image_op_free(ImageOperation *op, void *pointer);
void image_op_detach(ImageOperation *op, void *pointer);
ImageStatus image_fail(ImageOperation *op, ImageStatus code, const char *message, ...);
bool image_op_cancelled(ImageOperation *op);
bool image_check_dimensions(ImageOperation *op, uint32_t width, uint32_t height);
bool image_allocate_pixels(ImageOperation *op, uint32_t width, uint32_t height, ImageDecoded *image);
bool image_source_read(ImageOperation *op, const ImageSource *source, size_t offset, void *data, size_t size);
const uint8_t *image_source_bytes(ImageOperation *op, const ImageSource *source);
bool image_sink_write(ImageOperation *op, const ImageSink *sink, const void *data, size_t size);
bool image_buffer_write_at(ImageByteBuffer *buffer, size_t offset, const void *data, size_t size);
size_t image_buffer_write(void *user, const uint8_t *data, size_t size);
void image_target_size(const ImageInfo *info, const ImageDecodeOptions *options, uint32_t *width,
                       uint32_t *height);
bool image_reducer_init(ImageReducer *reducer, ImageOperation *op, uint32_t source_width,
                        uint32_t source_height, uint32_t width, uint32_t height, ImageDecoded *output);
bool image_reducer_row(ImageReducer *reducer, const uint8_t *rgba, uint32_t y);
void image_reducer_finish(ImageReducer *reducer);
bool image_normalize_pixels(ImageOperation *op, ImageDecoded *image);

ImageStatus image_png_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_png_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                             ImageDecoded *);
ImageStatus image_png_encode(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                             const ImageSink *);
ImageStatus image_png_encode_rows(ImageOperation *, const ImageRowSource *, const ImageSink *);
ImageStatus image_jpeg_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_jpeg_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                              ImageDecoded *);
ImageStatus image_jpeg_encode(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                              const ImageSink *);
ImageStatus image_webp_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_webp_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                              ImageDecoded *);
ImageStatus image_webp_encode(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                              const ImageSink *);
ImageStatus image_bitmap_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_bitmap_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                                ImageDecoded *);
ImageStatus image_bitmap_encode(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                                const ImageSink *);
ImageStatus image_tiff_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_tiff_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                              ImageDecoded *);
ImageStatus image_tiff_encode(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                              const ImageSink *);
ImageStatus image_avif_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_avif_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                              ImageDecoded *);
ImageStatus image_avif_encode(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                              const ImageSink *);
ImageStatus image_gif_probe(ImageOperation *, const ImageSource *, ImageInfo *);
ImageStatus image_gif_decode(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                             ImageDecoded *);
#ifdef __cplusplus
}
#endif
#endif
