#include "image_internal.h"
#include <webp/decode.h>
#include <webp/encode.h>
#include <limits.h>

static ImageStatus image_webp_error(ImageOperation *op, VP8StatusCode status) {
    ImageStatus code = status == VP8_STATUS_OUT_OF_MEMORY         ? IMAGE_OUT_OF_MEMORY
                       : status == VP8_STATUS_NOT_ENOUGH_DATA     ? IMAGE_TRUNCATED_INPUT
                       : status == VP8_STATUS_UNSUPPORTED_FEATURE ? IMAGE_UNSUPPORTED_FEATURE
                                                                  : IMAGE_INVALID_DATA;
    image_fail(op, code, "WebP codec status %d", status);
    op->error->native_code = status;
    return op->error->code;
}
ImageStatus image_webp_probe(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    const uint8_t *data = image_source_bytes(op, source);
    if (!data)
        return op->error->code;
    WebPBitstreamFeatures features;
    VP8StatusCode status = WebPGetFeatures(data, source->length, &features);
    if (status != VP8_STATUS_OK)
        return image_webp_error(op, status);
    info->width = (uint32_t)features.width;
    info->height = (uint32_t)features.height;
    info->has_alpha = features.has_alpha != 0;
    info->bit_depth = 8;
    info->orientation = 1;
    info->page_count = 1;
    info->pages_known = true;
    info->frames_known = !features.has_animation;
    info->frame_count = features.has_animation ? 0 : 1;
    return IMAGE_OK;
}
ImageStatus image_webp_decode(ImageOperation *op, const ImageSource *source,
                              const ImageDecodeOptions *options, ImageDecoded *result) {
    const uint8_t *data = image_source_bytes(op, source);
    if (!data)
        return op->error->code;
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config))
        return image_fail(op, IMAGE_INTERNAL_ERROR, "WebP ABI mismatch");
    VP8StatusCode status = WebPGetFeatures(data, source->length, &config.input);
    if (status != VP8_STATUS_OK)
        return image_webp_error(op, status);
    if (config.input.has_animation)
        return image_fail(op, IMAGE_UNSUPPORTED_FEATURE, "animated WebP decoding is not enabled");
    uint32_t w = result->info.width, h = result->info.height;
    if (options->mode == IMAGE_DECODE_THUMBNAIL)
        image_target_size(&result->info, options, &w, &h);
    bool full_planes = config.input.format == 2 || config.input.has_alpha;
    bool shrinking = w < result->info.width || h < result->info.height;
    // VP8L history and the VP8 alpha plane retain source-size storage despite output scaling.
    if (full_planes && shrinking && !options->allow_full_decode)
        return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE, "WebP lossless/alpha needs source-size planes");
    uint64_t working_bytes = full_planes ? (uint64_t)result->info.width * result->info.height * 8 : 0;
    if (working_bytes > op->context->limits.max_scratch_bytes - op->scratch_bytes)
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "WebP working memory estimate exceeds scratch budget");
    if (!image_allocate_pixels(op, w, h, result))
        return op->error->code;
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = result->pixels;
    config.output.u.RGBA.stride = (int)result->stride;
    config.output.u.RGBA.size = result->size;
    if (w != result->info.width || h != result->info.height) {
        config.options.use_scaling = 1;
        config.options.scaled_width = (int)w;
        config.options.scaled_height = (int)h;
        result->strategy = IMAGE_STRATEGY_NATIVE_SCALED;
        result->full_decode_fallback = full_planes;
    }
    config.options.use_threads = op->context->limits.max_threads > 1;
    status = WebPDecode(data, source->length, &config);
    WebPFreeDecBuffer(&config.output);
    return status == VP8_STATUS_OK ? IMAGE_OK : image_webp_error(op, status);
}
typedef struct ImageWebpWriter {
    ImageOperation *op;
    const ImageSink *sink;
} ImageWebpWriter;
static int image_webp_write(const uint8_t *data, size_t size, const WebPPicture *picture) {
    ImageWebpWriter *writer = (ImageWebpWriter *)picture->custom_ptr;
    return image_sink_write(writer->op, writer->sink, data, size);
}
static int image_webp_progress(int, const WebPPicture *picture) {
    return !image_op_cancelled(((ImageWebpWriter *)picture->custom_ptr)->op);
}
ImageStatus image_webp_encode(ImageOperation *op, const ImagePixelView *pixels,
                              const ImageEncodeOptions *options, const ImageSink *sink) {
    if (pixels->stride > INT_MAX)
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "WebP stride exceeds codec range");
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture))
        return image_fail(op, IMAGE_INTERNAL_ERROR, "WebP ABI mismatch");
    config.quality = (float)options->quality;
    config.lossless = options->lossless;
    config.method = 4;
    config.thread_level = op->context->limits.max_threads > 1;
    picture.width = (int)pixels->width;
    picture.height = (int)pixels->height;
    picture.use_argb = 1;
    const uint8_t *data = pixels->pixels;
    size_t stride = pixels->stride;
    if (pixels->channels == 1) {
        uint8_t *rgb = (uint8_t *)image_op_alloc(op, (size_t)pixels->width * pixels->height * 3, false);
        if (!rgb)
            return op->error->code;
        for (uint32_t y = 0; y < pixels->height; y++)
            for (uint32_t x = 0; x < pixels->width; x++)
                memset(rgb + ((size_t)y * pixels->width + x) * 3, data[(size_t)y * stride + x], 3);
        data = rgb;
        stride = (size_t)pixels->width * 3;
    }
    int imported = pixels->channels == 4 ? WebPPictureImportRGBA(&picture, data, (int)stride)
                                         : WebPPictureImportRGB(&picture, data, (int)stride);
    if (!imported) {
        WebPPictureFree(&picture);
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot import WebP pixels");
    }
    ImageWebpWriter writer = {op, sink};
    picture.custom_ptr = &writer;
    picture.writer = image_webp_write;
    picture.progress_hook = image_webp_progress;
    if (!WebPEncode(&config, &picture))
        image_fail(
            op, picture.error_code == VP8_ENC_ERROR_OUT_OF_MEMORY ? IMAGE_OUT_OF_MEMORY : IMAGE_INVALID_DATA,
            "WebP encoding failed (%d)", picture.error_code);
    WebPPictureFree(&picture);
    return op->error->code;
}
