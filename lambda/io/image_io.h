#ifndef LAMBDA_IMAGE_IO_H
#define LAMBDA_IMAGE_IO_H
#include "../../lib/image.h"
#ifdef __cplusplus
extern "C" {
#endif

// resource ownership stays in lambda-io (D7.1.2v2); byte codecs remain in lib.
ImageStatus image_io_probe_path(ImageContext *context, const char *path, ImageInfo *info, ImageError *error);
ImageStatus image_io_decode_path(ImageContext *context, const char *path, const ImageDecodeOptions *options,
                                 ImageDecoded *result, ImageError *error);
ImageStatus image_io_save_path(ImageContext *context, const char *path, const ImagePixelView *pixels,
                               const ImageEncodeOptions *options, ImageError *error);
ImageStatus image_io_save_rows_path(ImageContext *context, const char *path, const ImageRowSource *rows,
                                    const ImageEncodeOptions *options, ImageError *error);

unsigned char *image_load(const char *filename, int *width, int *height, int *channels, int req_channels);
unsigned char *image_load_scaled(const char *filename, int target_w, int target_h, int *width, int *height,
                                 int *channels);
int image_get_dimensions(const char *filename, int *width, int *height);
int image_save_png(const char *filename, const unsigned char *data, int width, int height, int channels);
// a supplied created flag requests exclusive creation for worker-owned cleanup.
int image_save_png_cancelable(const char *filename, const unsigned char *data, int width, int height,
                              int channels, int (*cancelled)(void *), void *user, int *created);
GifFrames *image_gif_load(const char *filename);
int image_gif_frame_count(const char *filename);

#ifdef __cplusplus
}
#endif
#endif
