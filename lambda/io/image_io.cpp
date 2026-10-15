#include "image_io.h"
#include "../../lib/file.h"
#include "../../lib/memtrack.h"
#include "../../lib/math_checked.hpp"
#include "../../lib/log.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#define image_file_seek _fseeki64
#define image_file_tell _ftelli64
#else
#define image_file_seek fseeko
#define image_file_tell ftello
#endif

struct ImageFileSource {
    FILE *file;
    int read_error;
};

static ImageStatus image_io_error(ImageError *error, ImageStatus code, const char *operation,
                                  const char *message, int os_error) {
    if (error) {
        memset(error, 0, sizeof(*error));
        error->code = code;
        error->os_error = os_error;
        snprintf(error->operation, sizeof(error->operation), "%s", operation);
        snprintf(error->message, sizeof(error->message), "%s%s%s", message, os_error ? ": " : "",
                 os_error ? strerror(os_error) : "");
    }
    return code;
}
static size_t image_file_read(void *user, uint64_t offset, uint8_t *data, size_t size) {
    ImageFileSource *source = (ImageFileSource *)user;
    if (offset > INT64_MAX || image_file_seek(source->file, (int64_t)offset, SEEK_SET) != 0) {
        source->read_error = errno ? errno : EIO;
        return 0;
    }
    size_t count = fread(data, 1, size, source->file);
    if (ferror(source->file))
        source->read_error = errno ? errno : EIO;
    return count;
}
static ImageStatus image_file_open(ImageContext *context, const char *path, ImageFileSource *file,
                                   ImageSource *source, ImageError *error) {
    if (!path || !*path)
        return image_io_error(error, IMAGE_INVALID_ARGUMENT, "open", "empty image path", 0);
    file->file = fopen(path, "rb");
    file->read_error = 0;
    if (!file->file)
        return image_io_error(error, IMAGE_IO_ERROR, "open", "cannot open image", errno);
    if (image_file_seek(file->file, 0, SEEK_END) != 0) {
        int failure = errno;
        fclose(file->file);
        file->file = NULL;
        return image_io_error(error, IMAGE_IO_ERROR, "seek", "cannot seek image", failure);
    }
    int64_t length = image_file_tell(file->file);
    ImageContext defaults;
    if (!context) {
        image_context_init(&defaults);
        context = &defaults;
    }
    if (length < 0 || (uint64_t)length > SIZE_MAX || (uint64_t)length > context->limits.max_encoded_bytes) {
        int failure = length < 0 ? errno : 0;
        fclose(file->file);
        file->file = NULL;
        return image_io_error(error, length < 0 ? IMAGE_IO_ERROR : IMAGE_LIMIT_EXCEEDED, "open",
                              "invalid or excessive image file length", failure);
    }
    *source = {NULL, (size_t)length, image_file_read, file};
    return IMAGE_OK;
}
static ImageStatus image_file_finish(ImageFileSource *file, ImageStatus status, ImageError *error) {
    int read_error = file->read_error;
    int closed = fclose(file->file);
    file->file = NULL;
    if (read_error && (status == IMAGE_OK || status == IMAGE_TRUNCATED_INPUT))
        return image_io_error(error, IMAGE_IO_ERROR, "read", "image read failed", read_error);
    if (closed && status == IMAGE_OK)
        return image_io_error(error, IMAGE_IO_ERROR, "close", "image close failed", errno);
    return status;
}
ImageStatus image_io_probe_path(ImageContext *context, const char *path, ImageInfo *info, ImageError *error) {
    if (info)
        memset(info, 0, sizeof(*info));
    ImageFileSource file = {};
    ImageSource source = {};
    ImageStatus status = image_file_open(context, path, &file, &source, error);
    if (status != IMAGE_OK)
        return status;
    status = image_probe(context, &source, info, error);
    status = image_file_finish(&file, status, error);
    if (status != IMAGE_OK && info)
        memset(info, 0, sizeof(*info));
    return status;
}
ImageStatus image_io_decode_path(ImageContext *context, const char *path, const ImageDecodeOptions *options,
                                 ImageDecoded *result, ImageError *error) {
    if (result)
        memset(result, 0, sizeof(*result));
    ImageFileSource file = {};
    ImageSource source = {};
    ImageStatus status = image_file_open(context, path, &file, &source, error);
    if (status != IMAGE_OK)
        return status;
    status = image_decode(context, &source, options, result, error);
    status = image_file_finish(&file, status, error);
    if (status != IMAGE_OK && result)
        image_decoded_dispose(result);
    return status;
}
static ImageStatus image_io_publish_exclusive(const char *path, const ImageEncoded *encoded,
                                              ImageError *error) {
    // the legacy worker owns only newly created destinations; O_EXCL avoids a check/open race.
    FILE *file = file_open_regular_write(path, false);
    if (!file)
        return image_io_error(error, IMAGE_IO_ERROR, "publish", "cannot create image destination", errno);
    ImageStatus status = IMAGE_OK;
    if (fwrite(encoded->data, 1, encoded->length, file) != encoded->length || fflush(file) != 0)
        status = image_io_error(error, IMAGE_IO_ERROR, "write", "cannot write encoded image", errno);
    if (fclose(file) != 0 && status == IMAGE_OK)
        status = image_io_error(error, IMAGE_IO_ERROR, "close", "cannot close encoded image", errno);
    if (status != IMAGE_OK)
        file_delete(path);
    return status;
}
static ImageStatus image_io_publish(ImageContext *context, const char *path, ImageEncoded *encoded,
                                    ImageError *error, bool exclusive) {
    ImageStatus status = IMAGE_OK;
    // publish only a finished image, preserving an existing destination on encode/cancel failure.
    if (context && context->cancelled && context->cancelled(context->user))
        status = image_io_error(error, IMAGE_CANCELLED, "save", "image save cancelled", 0);
    else if (exclusive)
        status = image_io_publish_exclusive(path, encoded, error);
    else if (write_binary_file_atomic(path, encoded->data, encoded->length) != 0)
        status = image_io_error(error, IMAGE_IO_ERROR, "publish", "cannot publish encoded image", errno);
    image_free(encoded->data);
    return status;
}
static ImageStatus image_io_save_path_mode(ImageContext *context, const char *path,
                                           const ImagePixelView *pixels, const ImageEncodeOptions *options,
                                           ImageError *error, bool exclusive) {
    if (!path || !*path)
        return image_io_error(error, IMAGE_INVALID_ARGUMENT, "save", "empty image path", 0);
    ImageEncoded encoded = {};
    ImageStatus status = image_encode_to_memory(context, pixels, options, &encoded, error);
    return status == IMAGE_OK ? image_io_publish(context, path, &encoded, error, exclusive) : status;
}
ImageStatus image_io_save_path(ImageContext *context, const char *path, const ImagePixelView *pixels,
                               const ImageEncodeOptions *options, ImageError *error) {
    return image_io_save_path_mode(context, path, pixels, options, error, false);
}
ImageStatus image_io_save_rows_path(ImageContext *context, const char *path, const ImageRowSource *rows,
                                    const ImageEncodeOptions *options, ImageError *error) {
    if (!path || !*path)
        return image_io_error(error, IMAGE_INVALID_ARGUMENT, "save", "empty image path", 0);
    ImageEncoded encoded = {};
    ImageStatus status = image_encode_rows_to_memory(context, rows, options, &encoded, error);
    return status == IMAGE_OK ? image_io_publish(context, path, &encoded, error, false) : status;
}
unsigned char *image_load_scaled(const char *path, int tw, int th, int *width, int *height, int *channels) {
    if (!width || !height || !channels || tw < 0 || th < 0)
        return NULL;
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.normalize_orientation = false;
    options.mode = tw || th ? IMAGE_DECODE_AT_LEAST : IMAGE_DECODE_FULL;
    options.target_width = (uint32_t)tw;
    options.target_height = (uint32_t)th;
    ImageDecoded image = {};
    ImageError error = {};
    if (image_io_decode_path(NULL, path, &options, &image, &error) != IMAGE_OK) {
        log_error("IMAGE_IO: load %s: %s", path ? path : "(null)", error.message);
        return NULL;
    }
    *width = (int)image.width;
    *height = (int)image.height;
    *channels = 4;
    return image.pixels;
}
unsigned char *image_load(const char *path, int *width, int *height, int *channels, int) {
    return image_load_scaled(path, 0, 0, width, height, channels);
}
int image_get_dimensions(const char *path, int *width, int *height) {
    if (!width || !height)
        return 0;
    ImageInfo info = {};
    if (image_io_probe_path(NULL, path, &info, NULL) != IMAGE_OK)
        return 0;
    *width = (int)info.width;
    *height = (int)info.height;
    return 1;
}
int image_save_png_cancelable(const char *path, const unsigned char *data, int width, int height,
                              int channels, int (*cancelled)(void *), void *user, int *created) {
    if (created)
        *created = 0;
    size_t stride = 0, bytes = 0;
    if (width <= 0 || height <= 0 || channels < 1 ||
        !math_checked_mul((size_t)width, (size_t)channels, &stride) ||
        !math_checked_mul(stride, (size_t)height, &bytes))
        return 0;
    ImagePixelView pixels = {data, bytes, stride, (uint32_t)width, (uint32_t)height, (uint8_t)channels,
                             false};
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageContext context;
    image_context_init(&context);
    context.cancelled = cancelled;
    context.user = user;
    ImageError error = {};
    if (image_io_save_path_mode(&context, path, &pixels, &options, &error, created != NULL) != IMAGE_OK) {
        log_error("IMAGE_IO: PNG save %s: %s", path ? path : "(null)", error.message);
        return 0;
    }
    if (created)
        *created = 1;
    return 1;
}
int image_save_png(const char *path, const unsigned char *data, int width, int height, int channels) {
    return image_save_png_cancelable(path, data, width, height, channels, NULL, NULL, NULL);
}
static unsigned char *image_gif_read_file(const char *path, size_t *size) {
    char *bytes = NULL;
    if (!file_read_all_limit(path, MEM_CAT_IMAGE, 128u * 1024 * 1024, &bytes, size))
        return NULL;
    return (unsigned char *)bytes;
}
GifFrames *image_gif_load(const char *path) {
    size_t length = 0;
    unsigned char *bytes = image_gif_read_file(path, &length);
    if (!bytes)
        return NULL;
    GifFrames *frames = image_gif_load_from_memory(bytes, length);
    mem_free(bytes);
    return frames;
}
int image_gif_frame_count(const char *path) {
    size_t length = 0;
    unsigned char *bytes = image_gif_read_file(path, &length);
    if (!bytes)
        return 0;
    int count = image_gif_frame_count_from_memory(bytes, length);
    mem_free(bytes);
    return count;
}
