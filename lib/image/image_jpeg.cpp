#include "image_internal.h"
#include <turbojpeg.h>
#include <limits.h>

static ImageStatus image_jpeg_error(ImageOperation *op, tjhandle handle) {
    image_fail(op, IMAGE_INVALID_DATA, "JPEG: %s", tjGetErrorStr2(handle));
    op->error->native_code = tjGetErrorCode(handle);
    return op->error->code;
}
ImageStatus image_jpeg_probe(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    const uint8_t *data = image_source_bytes(op, source);
    if (!data)
        return op->error->code;
    if (source->length > ULONG_MAX)
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "JPEG input exceeds codec length");
    tjhandle handle = tjInitDecompress();
    if (!handle)
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create JPEG decoder");
    int w = 0, h = 0, subsampling = 0, color = 0;
    if (tjDecompressHeader3(handle, data, (unsigned long)source->length, &w, &h, &subsampling, &color) < 0)
        image_jpeg_error(op, handle);
    else {
        info->width = (uint32_t)w;
        info->height = (uint32_t)h;
        info->bit_depth = 8;
        info->orientation = (uint8_t)image_jpeg_exif_orientation_from_memory(data, source->length);
        info->frame_count = info->page_count = 1;
        info->frames_known = info->pages_known = true;
    }
    tjDestroy(handle);
    return op->error->code;
}
ImageStatus image_jpeg_decode(ImageOperation *op, const ImageSource *source,
                              const ImageDecodeOptions *options, ImageDecoded *result) {
    const uint8_t *data = image_source_bytes(op, source);
    if (!data)
        return op->error->code;
    tjhandle handle = tjInitDecompress();
    if (!handle)
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create JPEG decoder");
    int w = 0, h = 0, subsampling = 0, color = 0;
    if (tjDecompressHeader3(handle, data, (unsigned long)source->length, &w, &h, &subsampling, &color) < 0) {
        image_jpeg_error(op, handle);
        tjDestroy(handle);
        return op->error->code;
    }
    if (color == TJCS_CMYK || color == TJCS_YCCK) {
        tjDestroy(handle);
        return image_fail(op, IMAGE_UNSUPPORTED_FEATURE, "CMYK JPEG requires a supported color transform");
    }
    uint32_t tw = result->info.width, th = result->info.height;
    if (options->mode == IMAGE_DECODE_THUMBNAIL)
        image_target_size(&result->info, options, &tw, &th);
    else if (options->mode == IMAGE_DECODE_AT_LEAST) {
        tw = options->target_width ? options->target_width : 1;
        th = options->target_height ? options->target_height : 1;
        if (options->normalize_orientation && result->info.orientation >= 5) {
            uint32_t t = tw;
            tw = th;
            th = t;
        }
    }
    int out_w = w, out_h = h, count = 0;
    uint64_t best = (uint64_t)w * h;
    if (options->mode != IMAGE_DECODE_FULL) {
        tjscalingfactor *factors = tjGetScalingFactors(&count);
        for (int i = 0; i < count; i++) {
            int cw = TJSCALED(w, factors[i]), ch = TJSCALED(h, factors[i]);
            if (cw >= 0 && ch >= 0 && (uint32_t)cw >= tw && (uint32_t)ch >= th && (uint64_t)cw * ch < best) {
                out_w = cw;
                out_h = ch;
                best = (uint64_t)cw * ch;
            }
        }
    }
    if (options->mode == IMAGE_DECODE_THUMBNAIL && !options->allow_full_decode &&
        (tw < (uint32_t)w || th < (uint32_t)h) &&
        ((out_w == w && out_h == h) || tj3Get(handle, TJPARAM_PROGRESSIVE))) {
        tjDestroy(handle);
        return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE,
                          "JPEG target or progressive coefficients require source-size storage");
    }
    // progressive scans retain full-image DCT coefficients even with scaled output.
    bool progressive = tj3Get(handle, TJPARAM_PROGRESSIVE) != 0;
    if (progressive && (uint64_t)w * h * 16 > op->context->limits.max_scratch_bytes - op->scratch_bytes) {
        tjDestroy(handle);
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "JPEG coefficients exceed scratch budget");
    }
    if (image_allocate_pixels(op, (uint32_t)out_w, (uint32_t)out_h, result)) {
        // fast DCT is a preview choice; full decoding retains accurate reconstruction.
        int flags = options->mode == IMAGE_DECODE_FULL ? TJFLAG_ACCURATEDCT : TJFLAG_FASTDCT;
        if (tjDecompress2(handle, data, (unsigned long)source->length, result->pixels, out_w,
                          (int)result->stride, out_h, TJPF_RGBA, flags) < 0)
            image_jpeg_error(op, handle);
        if (out_w != w || out_h != h)
            result->strategy = IMAGE_STRATEGY_NATIVE_SCALED;
        result->full_decode_fallback = progressive && (out_w != w || out_h != h);
    }
    tjDestroy(handle);
    return op->error->code;
}
ImageStatus image_jpeg_encode(ImageOperation *op, const ImagePixelView *pixels,
                              const ImageEncodeOptions *options, const ImageSink *sink) {
    ImagePixelView prepared;
    if (!image_prepare_pixels(op, pixels, pixels->channels == 4 ? 3 : pixels->channels,
                              options->has_background ? options->background : NULL, &prepared))
        return op->error->code;
    int format = pixels->channels == 1 ? TJPF_GRAY : TJPF_RGB;
    if (prepared.stride > INT_MAX)
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "JPEG stride exceeds codec range");
    tjhandle handle = tjInitCompress();
    if (!handle)
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create JPEG encoder");
    unsigned char *encoded = NULL;
    unsigned long length = 0;
    if (tjCompress2(handle, prepared.pixels, (int)pixels->width, (int)prepared.stride, (int)pixels->height, format, &encoded,
                    &length, pixels->channels == 1 ? TJSAMP_GRAY : TJSAMP_444, options->quality,
                    TJFLAG_ACCURATEDCT) < 0)
        image_jpeg_error(op, handle);
    else if (length > op->context->limits.max_encoded_bytes)
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "JPEG output exceeds byte limit");
    else
        image_sink_write(op, sink, encoded, length);
    if (encoded)
        tjFree(encoded);
    tjDestroy(handle);
    return op->error->code;
}
