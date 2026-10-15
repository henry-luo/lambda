#include "image/image_internal.h"
#include <stdio.h>
#include <math.h>
#include <limits.h>

struct ImageAllocation {
    void *pointer;
    size_t size;
    bool pixels;
    ImageAllocation *next;
};
typedef ImageStatus (*Probe)(ImageOperation *, const ImageSource *, ImageInfo *);
typedef ImageStatus (*Decode)(ImageOperation *, const ImageSource *, const ImageDecodeOptions *,
                              ImageDecoded *);
typedef ImageStatus (*Encode)(ImageOperation *, const ImagePixelView *, const ImageEncodeOptions *,
                              const ImageSink *);
typedef struct ImageCodec {
    ImageCapabilities capabilities;
    Probe probe;
    Decode decode;
    Encode encode;
} ImageCodec;
static const ImageCodec image_codecs[] = {
    {{"unknown", NULL, false, false, false, false, false}, NULL, NULL, NULL},
    {{"png", "image/png", true, true, false, false, false},
     image_png_probe,
     image_png_decode,
     image_png_encode},
    {{"jpeg", "image/jpeg", true, true, false, false, true},
     image_jpeg_probe,
     image_jpeg_decode,
     image_jpeg_encode},
    {{"gif", "image/gif", true, false, true, false, false}, image_gif_probe, image_gif_decode, NULL},
    {{"webp", "image/webp", true, true, false, false, true},
     image_webp_probe,
     image_webp_decode,
     image_webp_encode},
    {{"avif", "image/avif", true, true, false, false, false},
     image_avif_probe,
     image_avif_decode,
     image_avif_encode},
    {{"bmp", "image/bmp", true, true, false, false, false},
     image_bitmap_probe,
     image_bitmap_decode,
     image_bitmap_encode},
    {{"tiff", "image/tiff", true, true, false, true, false},
     image_tiff_probe,
     image_tiff_decode,
     image_tiff_encode},
    {{"ico", "image/x-icon", true, true, false, false, false},
     image_bitmap_probe,
     image_bitmap_decode,
     image_bitmap_encode}};

void image_context_init(ImageContext *c) {
    if (!c)
        return;
    memset(c, 0, sizeof(*c));
    c->limits = (ImageLimits){128u * 1024 * 1024,
                              512u * 1024 * 1024,
                              512u * 1024 * 1024,
                              128u * 1024 * 1024,
                              131072,
                              4096,
                              4096,
                              4u * 1024 * 1024,
                              1};
}
void image_decode_options_init(ImageDecodeOptions *o) {
    if (o) {
        memset(o, 0, sizeof(*o));
        o->allow_full_decode = true;
        o->normalize_orientation = true;
    }
}
void image_encode_options_init(ImageEncodeOptions *o, ImageCodecFormat format) {
    if (o) {
        memset(o, 0, sizeof(*o));
        o->format = format;
        o->quality = 85;
    }
}
ImageSource image_source_memory(const void *data, size_t size) {
    return (ImageSource){(const uint8_t *)data, size, NULL, NULL};
}
const ImageCapabilities *image_codec_capabilities(ImageCodecFormat format) {
    return &image_codecs[format >= IMAGE_CODEC_UNKNOWN && format <= IMAGE_CODEC_ICO ? format
                                                                                    : IMAGE_CODEC_UNKNOWN]
                .capabilities;
}
const char *image_status_name(ImageStatus status) {
    static const char *names[] = {"ok",
                                  "invalid_argument",
                                  "unknown_format",
                                  "codec_unavailable",
                                  "unsupported_feature",
                                  "truncated_input",
                                  "invalid_data",
                                  "io_error",
                                  "limit_exceeded",
                                  "fast_path_unavailable",
                                  "cancelled",
                                  "out_of_memory",
                                  "internal_error"};
    return names[status >= IMAGE_OK && status <= IMAGE_INTERNAL_ERROR ? status : IMAGE_INTERNAL_ERROR];
}
ImageCodecFormat image_detect(const uint8_t *d, size_t n) {
    if (!d)
        return IMAGE_CODEC_UNKNOWN;
    if (n >= 8 && !memcmp(d, "\211PNG\r\n\032\n", 8))
        return IMAGE_CODEC_PNG;
    if (n >= 3 && d[0] == 255 && d[1] == 216 && d[2] == 255)
        return IMAGE_CODEC_JPEG;
    if (n >= 6 && (!memcmp(d, "GIF87a", 6) || !memcmp(d, "GIF89a", 6)))
        return IMAGE_CODEC_GIF;
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4))
        return IMAGE_CODEC_WEBP;
    if (n >= 2 && !memcmp(d, "BM", 2))
        return IMAGE_CODEC_BMP;
    if (n >= 4 && (!memcmp(d, "II\052\0", 4) || !memcmp(d, "MM\0\052", 4) || !memcmp(d, "II\053\0", 4) ||
                   !memcmp(d, "MM\0\053", 4)))
        return IMAGE_CODEC_TIFF;
    if (n >= 4 && !read_le16(d) && read_le16(d + 2) == 1)
        return IMAGE_CODEC_ICO;
    // inspect major/compatible brands, excluding ftyp's minor-version word.
    for (size_t p = 0; p + 8 <= n;) {
        uint64_t length = read_be32(d + p);
        size_t header = 8;
        if (length == 1) {
            if (n - p < 16)
                break;
            length = read_be64(d + p + 8);
            header = 16;
        }
        if (length < header || length > SIZE_MAX)
            break;
        if (!memcmp(d + p + 4, "ftyp", 4) && length >= header + 8) {
            size_t end = length < n - p ? p + (size_t)length : n;
            for (size_t b = p + header; b + 4 <= end; b += 4)
                if (b != p + header + 4 && (!memcmp(d + b, "avif", 4) || !memcmp(d + b, "avis", 4)))
                    return IMAGE_CODEC_AVIF;
            break;
        }
        if (length > n - p)
            break;
        p += (size_t)length;
    }
    return IMAGE_CODEC_UNKNOWN;
}
void image_op_init(ImageOperation *op, ImageContext *context, ImageError *error, const char *name) {
    memset(op, 0, sizeof(*op));
    image_context_init(&op->defaults);
    op->context = context ? context : &op->defaults;
    op->error = error ? error : &op->local_error;
    memset(op->error, 0, sizeof(*op->error));
    op->name = name;
}
ImageStatus image_fail(ImageOperation *op, ImageStatus code, const char *message, ...) {
    if (op->error->code != IMAGE_OK)
        return op->error->code;
    op->error->code = code;
    op->error->format = op->format;
    snprintf(op->error->operation, sizeof(op->error->operation), "%s", op->name);
    va_list args;
    va_start(args, message);
    vsnprintf(op->error->message, sizeof(op->error->message), message, args);
    va_end(args);
    return code;
}
bool image_op_cancelled(ImageOperation *op) {
    if (!op->context->cancelled || !op->context->cancelled(op->context->user))
        return false;
    image_fail(op, IMAGE_CANCELLED, "image operation cancelled");
    return true;
}
void *image_op_alloc(ImageOperation *op, size_t bytes, bool pixels) {
    size_t *used = pixels ? &op->decoded_bytes : &op->scratch_bytes;
    size_t limit = pixels ? op->context->limits.max_decoded_bytes : op->context->limits.max_scratch_bytes;
    if (!bytes || bytes > limit || *used > limit - bytes) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "image allocation exceeds %s budget",
                   pixels ? "pixel" : "scratch");
        return NULL;
    }
    size_t admitted;
    if (!math_checked_add(bytes, sizeof(ImageAllocation), &admitted) ||
        !memtrack_allow_allocation(NULL, admitted)) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "image allocation exceeds process memory budget");
        return NULL;
    }
    ImageAllocation *a = (ImageAllocation *)mem_alloc(sizeof(*a), MEM_CAT_IMAGE);
    void *p = a ? mem_alloc(bytes, MEM_CAT_IMAGE) : NULL;
    if (!p) {
        if (a)
            mem_free(a);
        image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot allocate %zu image bytes", bytes);
        return NULL;
    }
    *a = (ImageAllocation){p, bytes, pixels, op->allocations};
    op->allocations = a;
    *used += bytes;
    return p;
}
static void image_op_release(ImageOperation *op, void *p, bool detach) {
    for (ImageAllocation **link = &op->allocations; *link; link = &(*link)->next) {
        ImageAllocation *a = *link;
        if (a->pointer != p)
            continue;
        *link = a->next;
        *(a->pixels ? &op->decoded_bytes : &op->scratch_bytes) -= a->size;
        if (!detach)
            mem_free(p);
        mem_free(a);
        return;
    }
}
void image_op_free(ImageOperation *op, void *p) {
    image_op_release(op, p, false);
}
void image_op_detach(ImageOperation *op, void *p) {
    image_op_release(op, p, true);
}
void image_op_cleanup(ImageOperation *op) {
    while (op->allocations)
        image_op_free(op, op->allocations->pointer);
}
bool image_check_dimensions(ImageOperation *op, uint32_t w, uint32_t h) {
    if (!w || !h) {
        image_fail(op, IMAGE_INVALID_DATA, "zero image dimensions");
        return false;
    }
    const ImageLimits *l = &op->context->limits;
    if (w > l->max_dimension || h > l->max_dimension || w > INT_MAX / 4 ||
        (uint64_t)w * h > l->max_source_pixels) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "source dimensions %ux%u exceed limits", w, h);
        return false;
    }
    return true;
}
bool image_allocate_pixels(ImageOperation *op, uint32_t w, uint32_t h, ImageDecoded *image) {
    size_t stride, bytes;
    if (!w || !h || !math_checked_mul(w, 4, &stride) || !math_checked_mul(stride, h, &bytes)) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "pixel layout overflow");
        return false;
    }
    image->pixels = (uint8_t *)image_op_alloc(op, bytes, true);
    if (!image->pixels)
        return false;
    image->width = w;
    image->height = h;
    image->stride = stride;
    image->size = bytes;
    return true;
}
bool image_source_read(ImageOperation *op, const ImageSource *s, size_t offset, void *data, size_t size) {
    if (!s || offset > s->length || size > s->length - offset) {
        image_fail(op, IMAGE_TRUNCATED_INPUT, "read exceeds image input");
        return false;
    }
    if (image_op_cancelled(op))
        return false;
    if (s->data) {
        memcpy(data, s->data + offset, size);
        return true;
    }
    if (!s->read_at) {
        image_fail(op, IMAGE_INVALID_ARGUMENT, "source has no reader");
        return false;
    }
    for (size_t done = 0; done < size;) {
        if (image_op_cancelled(op))
            return false;
        size_t n = s->read_at(s->user, offset + done, (uint8_t *)data + done, size - done);
        if (!n || n > size - done) {
            image_fail(op, IMAGE_TRUNCATED_INPUT, "short image read");
            op->error->has_byte_offset = true;
            op->error->byte_offset = offset + done;
            return false;
        }
        done += n;
    }
    return true;
}
const uint8_t *image_source_bytes(ImageOperation *op, const ImageSource *s) {
    if (s->data)
        return s->data;
    if (op->cached_source == s)
        return op->cached_bytes;
    uint8_t *bytes = (uint8_t *)image_op_alloc(op, s->length, false);
    if (!bytes || !image_source_read(op, s, 0, bytes, s->length))
        return NULL;
    op->cached_source = s;
    op->cached_bytes = bytes;
    return bytes;
}
bool image_sink_write(ImageOperation *op, const ImageSink *sink, const void *data, size_t size) {
    if (image_op_cancelled(op))
        return false;
    if (!sink || !sink->write) {
        image_fail(op, IMAGE_INVALID_ARGUMENT, "sink has no writer");
        return false;
    }
    if (op->tracked_sink != sink) {
        op->tracked_sink = sink;
        op->sink_bytes = 0;
    }
    if (size > op->context->limits.max_encoded_bytes ||
        op->sink_bytes > op->context->limits.max_encoded_bytes - size) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "encoded output exceeds byte limit");
        return false;
    }
    for (size_t done = 0; done < size;) {
        size_t n = sink->write(sink->user, (const uint8_t *)data + done, size - done);
        if (!n || n > size - done) {
            image_fail(op, IMAGE_IO_ERROR, "short image write");
            return false;
        }
        done += n;
    }
    op->sink_bytes += size;
    return true;
}
bool image_buffer_write_at(ImageByteBuffer *b, size_t offset, const void *data, size_t size) {
    size_t end;
    if (!math_checked_add(offset, size, &end) || end > b->op->context->limits.max_encoded_bytes) {
        image_fail(b->op, IMAGE_LIMIT_EXCEEDED, "encoded buffer exceeds byte limit");
        return false;
    }
    if (end > b->capacity) {
        size_t cap = b->capacity ? b->capacity : 1024;
        while (cap < end) {
            if (cap > SIZE_MAX / 2) {
                cap = end;
                break;
            }
            cap *= 2;
        }
        if (cap > b->op->context->limits.max_encoded_bytes)
            cap = end;
        uint8_t *next = (uint8_t *)image_op_alloc(b->op, cap, false);
        if (!next)
            return false;
        if (b->length)
            memcpy(next, b->data, b->length);
        if (b->data)
            image_op_free(b->op, b->data);
        b->data = next;
        b->capacity = cap;
    }
    if (offset > b->length)
        memset(b->data + b->length, 0, offset - b->length);
    if (size)
        memcpy(b->data + offset, data, size);
    if (end > b->length)
        b->length = end;
    return true;
}
size_t image_buffer_write(void *user, const uint8_t *data, size_t size) {
    ImageByteBuffer *b = (ImageByteBuffer *)user;
    return image_buffer_write_at(b, b->length, data, size) ? size : 0;
}
void image_target_size(const ImageInfo *info, const ImageDecodeOptions *o, uint32_t *width,
                       uint32_t *height) {
    uint32_t w = info->width, h = info->height, tw = o->target_width, th = o->target_height;
    if (o->normalize_orientation && info->orientation >= 5) {
        uint32_t t = tw;
        tw = th;
        th = t;
    }
    if (o->mode == IMAGE_DECODE_AT_LEAST) {
        // retain the legacy integer-step PNG preview, which stays at or above both targets.
        uint32_t step_w = tw ? w / tw : 1, step_h = th ? h / th : 1;
        uint32_t step = step_w < step_h ? step_w : step_h;
        if (!step)
            step = 1;
        *width = w / step;
        *height = h / step;
        return;
    }
    if (o->mode != IMAGE_DECODE_THUMBNAIL) {
        *width = w;
        *height = h;
        return;
    }
    // use a rational scale so a thumbnail never rounds outside its bounding box.
    uint64_t n = 1, d = 1;
    if (tw && tw < w) {
        n = tw;
        d = w;
    }
    if (th && (uint64_t)th * d < (uint64_t)h * n) {
        n = th;
        d = h;
    }
    *width = (uint32_t)((uint64_t)w * n / d);
    *height = (uint32_t)((uint64_t)h * n / d);
    if (!*width)
        *width = 1;
    if (!*height)
        *height = 1;
}
bool image_reducer_init(ImageReducer *r, ImageOperation *op, uint32_t sw, uint32_t sh, uint32_t w, uint32_t h,
                        ImageDecoded *output) {
    memset(r, 0, sizeof(*r));
    r->op = op;
    r->output = output;
    r->source_width = sw;
    r->source_height = sh;
    if (!image_allocate_pixels(op, w, h, output))
        return false;
    r->sums = (double *)image_op_alloc(op, (size_t)w * 4 * sizeof(double), false);
    if (!r->sums)
        return false;
    memset(r->sums, 0, (size_t)w * 4 * sizeof(double));
    return true;
}
bool image_reducer_row(ImageReducer *r, const uint8_t *rgba, uint32_t y) {
    if (image_op_cancelled(r->op))
        return false;
    ImageDecoded *out = r->output;
    double sx = (double)r->source_width / out->width, sy = (double)r->source_height / out->height;
    // distribute a source row across intersecting output bins using premultiplied samples.
    for (double top = y; top < y + 1.0 && r->next_row < out->height;) {
        double bottom = fmin(y + 1.0, (r->next_row + 1) * sy), wy = bottom - top;
        for (uint32_t x = 0; x < out->width; x++) {
            double left = x * sx, right = (x + 1) * sx;
            for (uint32_t ix = (uint32_t)left; ix < r->source_width && ix < right; ix++) {
                double weight = (fmin(right, ix + 1.0) - fmax(left, (double)ix)) * wy;
                const uint8_t *p = rgba + (size_t)ix * 4;
                for (unsigned c = 0; c < 3; c++)
                    r->sums[x * 4 + c] += p[c] * p[3] * weight;
                r->sums[x * 4 + 3] += p[3] * weight;
            }
        }
        top = bottom;
        if (bottom >= (r->next_row + 1) * sy || y + 1 == r->source_height) {
            uint8_t *row = out->pixels + (size_t)r->next_row * out->stride;
            for (uint32_t x = 0; x < out->width; x++) {
                double alpha = r->sums[x * 4 + 3];
                for (unsigned c = 0; c < 3; c++)
                    row[x * 4 + c] =
                        alpha > 0 ? (uint8_t)fmin(255.0, floor(r->sums[x * 4 + c] / alpha + 0.5)) : 0;
                row[x * 4 + 3] = (uint8_t)fmin(255.0, floor(alpha / (sx * sy) + 0.5));
            }
            memset(r->sums, 0, (size_t)out->width * 4 * sizeof(double));
            r->next_row++;
        }
    }
    return true;
}
void image_reducer_finish(ImageReducer *r) {
    image_op_free(r->op, r->sums);
    r->sums = NULL;
}
bool image_normalize_pixels(ImageOperation *op, ImageDecoded *image) {
    unsigned o = image->info.orientation;
    if (o <= 1 || o > 8)
        return true;
    ImageDecoded next = {};
    if (!image_allocate_pixels(op, o >= 5 ? image->height : image->width,
                               o >= 5 ? image->width : image->height, &next))
        return false;
    for (uint32_t y = 0; y < image->height; y++) {
        if (image_op_cancelled(op))
            return false;
        for (uint32_t x = 0; x < image->width; x++) {
            uint32_t dx = x, dy = y;
            switch (o) {
            case 2:
                dx = image->width - 1 - x;
                break;
            case 3:
                dx = image->width - 1 - x;
                dy = image->height - 1 - y;
                break;
            case 4:
                dy = image->height - 1 - y;
                break;
            case 5:
                dx = y;
                dy = x;
                break;
            case 6:
                dx = image->height - 1 - y;
                dy = x;
                break;
            case 7:
                dx = image->height - 1 - y;
                dy = image->width - 1 - x;
                break;
            case 8:
                dx = y;
                dy = image->width - 1 - x;
                break;
            }
            memcpy(next.pixels + (size_t)dy * next.stride + dx * 4,
                   image->pixels + (size_t)y * image->stride + x * 4, 4);
        }
    }
    image_op_free(op, image->pixels);
    image->pixels = next.pixels;
    image->width = next.width;
    image->height = next.height;
    image->stride = next.stride;
    image->size = next.size;
    image->info.orientation = 1;
    return true;
}
static ImageCodecFormat image_detect_bmff_source(ImageOperation *op, const ImageSource *source) {
    size_t metadata = 0;
    // skip padding boxes by offset; only box headers and ftyp brands consume the scan budget.
    for (size_t offset = 0; source->length - offset >= 8;) {
        uint8_t header[16];
        if (!image_source_read(op, source, offset, header, 8))
            return IMAGE_CODEC_UNKNOWN;
        uint64_t length = read_be32(header);
        size_t prefix = 8;
        bool brands = !memcmp(header + 4, "ftyp", 4);
        if (!brands && memcmp(header + 4, "free", 4) && memcmp(header + 4, "skip", 4) &&
            memcmp(header + 4, "wide", 4))
            return IMAGE_CODEC_UNKNOWN;
        if (length == 1) {
            if (source->length - offset < 16 || !image_source_read(op, source, offset + 8, header + 8, 8))
                return IMAGE_CODEC_UNKNOWN;
            length = read_be64(header + 8);
            prefix = 16;
        }
        if (length < prefix || length > source->length - offset)
            return IMAGE_CODEC_UNKNOWN;
        size_t examined = brands ? (size_t)length : prefix;
        if (metadata > op->context->limits.max_metadata_bytes ||
            examined > op->context->limits.max_metadata_bytes - metadata) {
            image_fail(op, IMAGE_LIMIT_EXCEEDED, "image identification exceeds metadata budget");
            return IMAGE_CODEC_UNKNOWN;
        }
        metadata += examined;
        if (brands) {
            uint8_t brand[4];
            if (length < prefix + 8)
                return IMAGE_CODEC_UNKNOWN;
            for (size_t b = prefix; b + 4 <= length; b += 4) {
                if (b == prefix + 4)
                    continue;
                if (!image_source_read(op, source, offset + b, brand, 4))
                    return IMAGE_CODEC_UNKNOWN;
                if (!memcmp(brand, "avif", 4) || !memcmp(brand, "avis", 4))
                    return IMAGE_CODEC_AVIF;
            }
            return IMAGE_CODEC_UNKNOWN;
        }
        offset += (size_t)length;
    }
    return IMAGE_CODEC_UNKNOWN;
}
static bool image_prepare_source(ImageOperation *op, const ImageSource *s) {
    if (!s || (!s->data && !s->read_at)) {
        image_fail(op, IMAGE_INVALID_ARGUMENT, "missing image source");
        return false;
    }
    if (!s->length) {
        image_fail(op, IMAGE_TRUNCATED_INPUT, "empty image input");
        return false;
    }
    if (s->length > op->context->limits.max_encoded_bytes) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "encoded image exceeds byte limit");
        return false;
    }
    uint8_t header[4096];
    size_t n = s->length < sizeof(header) ? s->length : sizeof(header);
    if (!image_source_read(op, s, 0, header, n))
        return false;
    op->format = image_detect(header, n);
    if (op->format == IMAGE_CODEC_UNKNOWN)
        op->format = image_detect_bmff_source(op, s);
    if (op->format == IMAGE_CODEC_UNKNOWN) {
        image_fail(op, IMAGE_UNKNOWN_FORMAT, "unrecognized image signature");
        return false;
    }
    return true;
}
static bool image_read_info(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    info->format = op->format;
    info->orientation = 1;
    image_codecs[op->format].probe(op, source, info);
    if (op->error->code != IMAGE_OK || !image_check_dimensions(op, info->width, info->height))
        return false;
    info->display_width = info->orientation >= 5 ? info->height : info->width;
    info->display_height = info->orientation >= 5 ? info->width : info->height;
    return true;
}
ImageStatus image_probe(ImageContext *context, const ImageSource *source, ImageInfo *info,
                        ImageError *error) {
    ImageOperation op;
    image_op_init(&op, context, error, "probe");
    if (info)
        memset(info, 0, sizeof(*info));
    if (!info)
        image_fail(&op, IMAGE_INVALID_ARGUMENT, "missing metadata output");
    else if (image_prepare_source(&op, source))
        image_read_info(&op, source, info);
    if (op.error->code == IMAGE_OK)
        image_op_cancelled(&op);
    ImageStatus status = op.error->code;
    image_op_cleanup(&op);
    if (status != IMAGE_OK && info)
        memset(info, 0, sizeof(*info));
    return status;
}
ImageStatus image_decode(ImageContext *context, const ImageSource *source, const ImageDecodeOptions *options,
                         ImageDecoded *result, ImageError *error) {
    ImageOperation op;
    image_op_init(&op, context, error, "decode");
    ImageDecodeOptions defaults;
    image_decode_options_init(&defaults);
    if (!options)
        options = &defaults;
    if (result)
        memset(result, 0, sizeof(*result));
    if (!result || options->mode < IMAGE_DECODE_FULL || options->mode > IMAGE_DECODE_AT_LEAST ||
        (options->mode == IMAGE_DECODE_THUMBNAIL && (!options->target_width || !options->target_height)))
        image_fail(&op, IMAGE_INVALID_ARGUMENT, "invalid decode options");
    else if (image_prepare_source(&op, source) && image_read_info(&op, source, &result->info)) {
        if (options->page_index && op.format != IMAGE_CODEC_TIFF)
            image_fail(&op, IMAGE_INVALID_ARGUMENT, "format has no pages");
        else if (options->frame_index && op.format != IMAGE_CODEC_GIF)
            image_fail(&op, IMAGE_UNSUPPORTED_FEATURE, "frame selection unavailable");
        else
            image_codecs[op.format].decode(&op, source, options, result);
        if (op.error->code == IMAGE_OK && result->pixels) {
            // page selection can replace the metadata returned by the initial probe.
            result->info.display_width =
                result->info.orientation >= 5 ? result->info.height : result->info.width;
            result->info.display_height =
                result->info.orientation >= 5 ? result->info.width : result->info.height;
            uint32_t tw, th;
            image_target_size(&result->info, options, &tw, &th);
            if (options->mode == IMAGE_DECODE_THUMBNAIL && (result->width != tw || result->height != th)) {
                ImageDecoded resized = {};
                ImageReducer reducer;
                if (!options->allow_full_decode && result->strategy == IMAGE_STRATEGY_FULL)
                    image_fail(&op, IMAGE_FAST_PATH_UNAVAILABLE, "codec requires full-resolution pixels");
                else if (image_reducer_init(&reducer, &op, result->width, result->height, tw, th, &resized)) {
                    for (uint32_t y = 0; y < result->height && op.error->code == IMAGE_OK; y++)
                        image_reducer_row(&reducer, result->pixels + (size_t)y * result->stride, y);
                    image_reducer_finish(&reducer);
                    image_op_free(&op, result->pixels);
                    result->pixels = resized.pixels;
                    result->width = tw;
                    result->height = th;
                    result->stride = resized.stride;
                    result->size = resized.size;
                    if (result->strategy == IMAGE_STRATEGY_FULL) {
                        result->strategy = IMAGE_STRATEGY_FULL_RESIZE;
                        result->full_decode_fallback = true;
                    }
                }
            }
            if (options->normalize_orientation && op.error->code == IMAGE_OK)
                image_normalize_pixels(&op, result);
        } else if (op.error->code == IMAGE_OK)
            image_fail(&op, IMAGE_INTERNAL_ERROR, "decoder returned no pixels");
    }
    // opaque codec calls may only observe cancellation once they return.
    if (op.error->code == IMAGE_OK)
        image_op_cancelled(&op);
    ImageStatus status = op.error->code;
    if (status == IMAGE_OK)
        image_op_detach(&op, result->pixels);
    image_op_cleanup(&op);
    if (status != IMAGE_OK && result)
        memset(result, 0, sizeof(*result));
    return status;
}
static ImageStatus image_encode_run(ImageOperation *operation, const ImagePixelView *pixels,
                                    const ImageEncodeOptions *options, const ImageSink *sink) {
    ImageOperation &op = *operation;
    size_t row = 0, span = 0;
    if (!pixels || !pixels->pixels || !options || !sink || !sink->write || !pixels->width ||
        !pixels->height || (pixels->channels != 1 && pixels->channels != 3 && pixels->channels != 4) ||
        (pixels->premultiplied && pixels->channels != 4) ||
        !math_checked_mul(pixels->width, pixels->channels, &row) || pixels->stride < row ||
        !math_checked_mul_add(pixels->height - 1, pixels->stride, row, &span) || span > pixels->capacity ||
        options->quality < 0 || options->quality > 100)
        image_fail(&op, IMAGE_INVALID_ARGUMENT, "invalid pixel view or encoding options");
    else if (options->format <= IMAGE_CODEC_UNKNOWN || options->format > IMAGE_CODEC_ICO)
        image_fail(&op, IMAGE_UNKNOWN_FORMAT, "unknown output format");
    else {
        op.format = options->format;
        // admit source dimensions before allocating a canonical pixel copy.
        if (!image_check_dimensions(&op, pixels->width, pixels->height))
            return op.error->code;
        ImagePixelView straight = *pixels;
        if (pixels->premultiplied && op.format != IMAGE_CODEC_PNG && op.format != IMAGE_CODEC_JPEG) {
            size_t bytes;
            if (!math_checked_mul(row, pixels->height, &bytes))
                return image_fail(&op, IMAGE_LIMIT_EXCEEDED, "straight pixel layout overflow");
            uint8_t *buffer = (uint8_t *)image_op_alloc(&op, bytes, false);
            if (!buffer)
                return op.error->code;
            for (uint32_t y = 0; y < pixels->height; y++)
                image_unpremultiply_rgba_row(buffer + (size_t)y * row,
                                             pixels->pixels + (size_t)y * pixels->stride, pixels->width);
            straight.pixels = buffer;
            straight.capacity = bytes;
            straight.stride = row;
            straight.premultiplied = false;
            pixels = &straight;
        }
        if (!image_codecs[op.format].encode)
            image_fail(&op, IMAGE_CODEC_UNAVAILABLE, "format is read-only");
        else if (!image_op_cancelled(&op))
            image_codecs[op.format].encode(&op, pixels, options, sink);
    }
    return op.error->code;
}
uint8_t image_unpremultiply_channel(uint8_t channel, uint8_t alpha) {
    if (!alpha)
        return 0;
    if (alpha == 255)
        return channel;
    uint32_t value = ((uint32_t)channel * 255u + alpha / 2u) / alpha;
    return (uint8_t)(value > 255u ? 255u : value);
}
void image_unpremultiply_rgba_row(uint8_t *destination, const uint8_t *source, size_t count) {
    for (size_t x = 0; x < count; x++) {
        uint8_t alpha = source[x * 4 + 3];
        for (unsigned c = 0; c < 3; c++)
            destination[x * 4 + c] = image_unpremultiply_channel(source[x * 4 + c], alpha);
        destination[x * 4 + 3] = alpha;
    }
}
ImageStatus image_encode(ImageContext *context, const ImagePixelView *pixels,
                         const ImageEncodeOptions *options, const ImageSink *sink, ImageError *error) {
    ImageOperation op;
    image_op_init(&op, context, error, "encode");
    ImageStatus status = image_encode_run(&op, pixels, options, sink);
    image_op_cleanup(&op);
    return status;
}
ImageStatus image_encode_to_memory(ImageContext *context, const ImagePixelView *pixels,
                                   const ImageEncodeOptions *options, ImageEncoded *result,
                                   ImageError *error) {
    ImageOperation op;
    image_op_init(&op, context, error, "encode");
    if (result)
        memset(result, 0, sizeof(*result));
    if (!result)
        image_fail(&op, IMAGE_INVALID_ARGUMENT, "missing encoded output");
    else {
        ImageByteBuffer buffer = {&op, NULL, 0, 0};
        ImageSink sink = {image_buffer_write, &buffer};
        if (image_encode_run(&op, pixels, options, &sink) == IMAGE_OK) {
            result->data = buffer.data;
            result->length = buffer.length;
            image_op_detach(&op, buffer.data);
        }
    }
    ImageStatus status = op.error->code;
    image_op_cleanup(&op);
    return status;
}
ImageStatus image_encode_rows_to_memory(ImageContext *context, const ImageRowSource *rows,
                                        const ImageEncodeOptions *options, ImageEncoded *result,
                                        ImageError *error) {
    ImageOperation op;
    image_op_init(&op, context, error, "encode_rows");
    if (result)
        memset(result, 0, sizeof(*result));
    if (!result || !rows || !rows->read_row || !options ||
        (rows->channels != 1 && rows->channels != 3 && rows->channels != 4) ||
        (rows->premultiplied && rows->channels != 4))
        image_fail(&op, IMAGE_INVALID_ARGUMENT, "invalid row source or output");
    else if (options->format != IMAGE_CODEC_PNG)
        image_fail(&op, IMAGE_UNSUPPORTED_FEATURE, "row encoding supports PNG only");
    else {
        op.format = options->format;
        ImageByteBuffer buffer = {&op, NULL, 0, 0};
        ImageSink sink = {image_buffer_write, &buffer};
        if (image_check_dimensions(&op, rows->width, rows->height) &&
            image_png_encode_rows(&op, rows, &sink) == IMAGE_OK) {
            result->data = buffer.data;
            result->length = buffer.length;
            image_op_detach(&op, buffer.data);
        }
    }
    ImageStatus status = op.error->code;
    image_op_cleanup(&op);
    return status;
}
void image_decoded_dispose(ImageDecoded *image) {
    if (image) {
        if (image->pixels)
            mem_free(image->pixels);
        memset(image, 0, sizeof(*image));
    }
}

struct ImageDecoder {
    ImageContext defaults;
    ImageContext *context;
    ImageSource source;
    ImageDecodeOptions options;
};
ImageStatus image_decoder_open(ImageContext *context, const ImageSource *source,
                               const ImageDecodeOptions *options, ImageDecoder **decoder, ImageError *error) {
    ImageOperation op;
    image_op_init(&op, context, error, "open");
    if (!decoder)
        return image_fail(&op, IMAGE_INVALID_ARGUMENT, "missing decoder output");
    *decoder = NULL;
    ImageInfo info = {};
    ImageStatus status = image_probe(context, source, &info, error);
    if (status != IMAGE_OK)
        return status;
    ImageDecoder *session = (ImageDecoder *)image_op_alloc(&op, sizeof(*session), false);
    if (!session)
        return op.error->code;
    image_context_init(&session->defaults);
    session->context = context ? context : &session->defaults;
    session->source = *source;
    if (options)
        session->options = *options;
    else
        image_decode_options_init(&session->options);
    image_op_detach(&op, session);
    image_op_cleanup(&op);
    *decoder = session;
    return IMAGE_OK;
}
ImageStatus image_decoder_read_frame(ImageDecoder *decoder, uint32_t frame_index, ImageDecoded *result,
                                     ImageError *error) {
    if (!decoder) {
        ImageOperation op;
        image_op_init(&op, NULL, error, "decode");
        if (result)
            memset(result, 0, sizeof(*result));
        return image_fail(&op, IMAGE_INVALID_ARGUMENT, "missing decoder session");
    }
    ImageDecodeOptions options = decoder->options;
    options.frame_index = frame_index;
    return image_decode(decoder->context, &decoder->source, &options, result, error);
}
void image_decoder_close(ImageDecoder *decoder) {
    if (decoder)
        mem_free(decoder);
}
