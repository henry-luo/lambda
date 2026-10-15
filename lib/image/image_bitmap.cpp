#include "image_internal.h"
extern "C" {
#include <libnsbmp.h>
}

static void *image_bitmap_create(void *user, int width, int height, unsigned) {
    ImageOperation *op = (ImageOperation *)user;
    ImageDecoded image = {};
    if (width <= 0 || height <= 0 || !image_check_dimensions(op, (uint32_t)width, (uint32_t)height) ||
        !image_allocate_pixels(op, (uint32_t)width, (uint32_t)height, &image))
        return NULL;
    memset(image.pixels, 0, image.size);
    return image.pixels;
}
static void image_bitmap_destroy(void *user, void *bitmap) {
    image_op_free((ImageOperation *)user, bitmap);
}
static unsigned char *image_bitmap_buffer(void *bitmap) {
    return (unsigned char *)bitmap;
}
static bmp_bitmap_callback_vt image_bitmap_callbacks(ImageOperation *op) {
    bmp_bitmap_callback_vt callbacks = {op, image_bitmap_create, image_bitmap_destroy, image_bitmap_buffer};
    return callbacks;
}
static ImageStatus image_bitmap_error(ImageOperation *op, bmp_result status) {
    image_fail(op,
               status == BMP_INSUFFICIENT_MEMORY ? IMAGE_OUT_OF_MEMORY
               : status == BMP_INSUFFICIENT_DATA ? IMAGE_TRUNCATED_INPUT
                                                 : IMAGE_INVALID_DATA,
               "BMP/ICO codec status %d", status);
    op->error->native_code = status;
    return op->error->code;
}
static bool image_ico_select(ImageOperation *op, const ImageSource *source, uint32_t tw, uint32_t th,
                             uint32_t source_width, uint32_t source_height,
                             size_t *offset, size_t *length, uint32_t *width, uint32_t *height,
                             uint32_t *index) {
    uint8_t header[6];
    if (!image_source_read(op, source, 0, header, 6))
        return false;
    uint32_t count = read_le16(header + 4);
    if (!count) {
        image_fail(op, IMAGE_INVALID_DATA, "ICO directory is empty");
        return false;
    }
    if (count > op->context->limits.max_frames) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "ICO entry count exceeds limit");
        return false;
    }
    size_t directory_end = 6 + (size_t)count * 16;
    if (directory_end > source->length) {
        image_fail(op, IMAGE_TRUNCATED_INPUT, "incomplete ICO directory");
        return false;
    }
    bool chosen = false, sufficient = false;
    uint64_t area = 0;
    unsigned depth = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t entry[16];
        if (!image_source_read(op, source, 6 + (size_t)i * 16, entry, 16))
            return false;
        uint32_t w = entry[0] ? entry[0] : 256, h = entry[1] ? entry[1] : 256;
        size_t off = read_le32(entry + 12), size = read_le32(entry + 8);
        if (off < directory_end || off > source->length || !size || size > source->length - off) {
            image_fail(op, IMAGE_INVALID_DATA, "ICO payload lies outside container");
            return false;
        }
        // alternative icon artwork must retain the chosen primary image's aspect ratio.
        if (tw && th && source_width && source_height &&
            (uint64_t)w * source_height != (uint64_t)h * source_width)
            continue;
        bool fits = tw && th && w >= tw && h >= th;
        uint64_t candidate = (uint64_t)w * h;
        unsigned bits = read_le16(entry + 6);
        bool prefer = !chosen || (fits && !sufficient) ||
                      (fits == sufficient && ((fits && candidate < area) || (!fits && candidate > area) ||
                                              (candidate == area && bits > depth)));
        if (prefer) {
            chosen = true;
            sufficient = fits;
            area = candidate;
            depth = bits;
            *offset = off;
            *length = size;
            *width = w;
            *height = h;
            *index = i;
        }
    }
    return chosen;
}
ImageStatus image_bitmap_probe(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    info->orientation = 1;
    info->bit_depth = 8;
    info->frame_count = info->page_count = 1;
    info->frames_known = info->pages_known = true;
    if (op->format == IMAGE_CODEC_ICO) {
        size_t offset = 0, length = 0;
        uint32_t index = 0;
        image_ico_select(op, source, 0, 0, 0, 0, &offset, &length, &info->width, &info->height, &index);
        info->has_alpha = true;
    } else {
        const uint8_t *bytes = image_source_bytes(op, source);
        if (!bytes)
            return op->error->code;
        bmp_bitmap_callback_vt callbacks = image_bitmap_callbacks(op);
        bmp_image bitmap;
        bmp_create(&bitmap, &callbacks);
        bitmap.metadata_only = true;
        bmp_result status = bmp_analyse(&bitmap, source->length, (uint8_t *)bytes);
        if (status != BMP_OK)
            image_bitmap_error(op, status);
        else {
            info->width = bitmap.width;
            info->height = bitmap.height;
            info->has_alpha = !bitmap.opaque;
        }
        bmp_finalise(&bitmap);
    }
    return op->error->code;
}
ImageStatus image_bitmap_decode(ImageOperation *op, const ImageSource *source,
                                const ImageDecodeOptions *options, ImageDecoded *result) {
    uint32_t tw, th;
    image_target_size(&result->info, options, &tw, &th);
    const uint8_t *bytes = image_source_bytes(op, source);
    if (!bytes)
        return op->error->code;
    bmp_bitmap_callback_vt callbacks = image_bitmap_callbacks(op);
    bmp_image bitmap;
    ico_collection icons;
    bool icon = op->format == IMAGE_CODEC_ICO;
    bmp_image *decoded = &bitmap;
    bmp_result status;
    if (icon) {
        size_t offset = 0, length = 0;
        uint32_t w = 0, h = 0, index = 0;
        if (!image_ico_select(op, source, options->mode == IMAGE_DECODE_THUMBNAIL ? tw : 0,
                              options->mode == IMAGE_DECODE_THUMBNAIL ? th : 0, result->info.width,
                              result->info.height, &offset, &length, &w, &h,
                              &index))
            return op->error->code;
        result->representation_index = index;
        ImageSource payload = image_source_memory(bytes + offset, length);
        if (image_detect(payload.data, payload.length) == IMAGE_CODEC_PNG) {
            ImageInfo selected = {};
            selected.format = IMAGE_CODEC_PNG;
            selected.orientation = 1;
            image_png_probe(op, &payload, &selected);
            if (op->error->code != IMAGE_OK)
                return op->error->code;
            if (selected.width != w || selected.height != h)
                return image_fail(op, IMAGE_INVALID_DATA, "ICO directory disagrees with PNG payload");
            ImageInfo original = result->info;
            result->info = selected;
            ImageStatus decoded_status = image_png_decode(op, &payload, options, result);
            result->info = original;
            if (decoded_status == IMAGE_OK && (w < original.width || h < original.height))
                result->strategy = IMAGE_STRATEGY_SELECTED_RESOLUTION;
            return decoded_status;
        }
        if (!options->allow_full_decode && options->mode == IMAGE_DECODE_THUMBNAIL &&
            w == result->info.width && h == result->info.height && (tw < w || th < h))
            return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE, "ICO DIB requires full representation pixels");
        // analyse only the selected DIB, using the existing upstream ICO parser.
        size_t size;
        if (!math_checked_add(22, length, &size))
            return image_fail(op, IMAGE_LIMIT_EXCEEDED, "ICO payload size overflow");
        uint8_t *single = (uint8_t *)image_op_alloc(op, size, false);
        if (!single)
            return op->error->code;
        memset(single, 0, 22);
        write_le16(single + 2, 1);
        write_le16(single + 4, 1);
        memcpy(single + 6, bytes + 6 + (size_t)index * 16, 16);
        write_le32(single + 18, 22);
        memcpy(single + 22, bytes + offset, length);
        ico_collection_create(&icons, &callbacks);
        status = ico_analyse(&icons, size, single);
        decoded = icons.first ? &icons.first->bmp : NULL;
        if (status == BMP_OK && (!decoded || decoded->width != w || decoded->height != h))
            status = BMP_DATA_ERROR;
        if (w < result->info.width || h < result->info.height)
            result->strategy = IMAGE_STRATEGY_SELECTED_RESOLUTION;
    } else {
        if (options->mode == IMAGE_DECODE_THUMBNAIL && !options->allow_full_decode &&
            (tw < result->info.width || th < result->info.height))
            return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE, "BMP decoder requires full bitmap pixels");
        bmp_create(&bitmap, &callbacks);
        status = bmp_analyse(&bitmap, source->length, (uint8_t *)bytes);
    }
    if (status == BMP_OK)
        status = bmp_decode(decoded);
    if (status != BMP_OK)
        image_bitmap_error(op, status);
    else if (decoded) {
        result->pixels = (uint8_t *)decoded->bitmap;
        decoded->bitmap = NULL;
        result->width = decoded->width;
        result->height = decoded->height;
        result->stride = (size_t)decoded->width * 4;
        result->size = result->stride * decoded->height;
    }
    if (icon)
        ico_finalise(&icons);
    else
        bmp_finalise(&bitmap);
    return op->error->code;
}

ImageStatus image_bitmap_encode(ImageOperation *op, const ImagePixelView *pixels,
                                const ImageEncodeOptions *options, const ImageSink *sink) {
    if (options->format == IMAGE_CODEC_ICO) {
        if (pixels->width > 256 || pixels->height > 256)
            return image_fail(op, IMAGE_INVALID_ARGUMENT, "ICO dimensions exceed 256 pixels");
        ImageByteBuffer buffer = {op, NULL, 0, 0};
        ImageSink memory = {image_buffer_write, &buffer};
        image_png_encode(op, pixels, options, &memory);
        if (op->error->code == IMAGE_OK) {
            uint8_t directory[22] = {};
            write_le16(directory + 2, 1);
            write_le16(directory + 4, 1);
            directory[6] = (uint8_t)pixels->width;
            directory[7] = (uint8_t)pixels->height;
            write_le16(directory + 10, 1);
            write_le16(directory + 12, 32);
            if (buffer.length > UINT32_MAX)
                image_fail(op, IMAGE_LIMIT_EXCEEDED, "ICO PNG payload exceeds 32-bit length");
            else {
                write_le32(directory + 14, (uint32_t)buffer.length);
                write_le32(directory + 18, 22);
                if (image_sink_write(op, sink, directory, 22))
                    image_sink_write(op, sink, buffer.data, buffer.length);
            }
        }
        return op->error->code;
    }
    /* adapted from libjpeg-turbo/src/wrbmp.c (IJG/Linaro/D. R. Commander);
     * retain its BMP header/BGR row layout, replacing stdio with the common sink.
     * distribution terms remain in libjpeg-turbo/README.ijg and LICENSE.md. */
    size_t stride = ((size_t)pixels->width * 3 + 3) & ~(size_t)3;
    uint64_t total = 54 + (uint64_t)stride * pixels->height;
    if (total > UINT32_MAX || total > op->context->limits.max_encoded_bytes)
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "BMP output exceeds length limit");
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    write_le32(header + 2, (uint32_t)total);
    write_le32(header + 10, 54);
    write_le32(header + 14, 40);
    write_le32(header + 18, pixels->width);
    write_le32(header + 22, pixels->height);
    write_le16(header + 26, 1);
    write_le16(header + 28, 24);
    write_le32(header + 34, (uint32_t)(total - 54));
    if (!image_sink_write(op, sink, header, sizeof(header)))
        return op->error->code;
    uint8_t *row = (uint8_t *)image_op_alloc(op, stride, false);
    if (!row)
        return op->error->code;
    memset(row, 0, stride);
    for (uint32_t y = pixels->height; y > 0; y--) {
        if (!image_rgb_row(op, pixels, y - 1, row,
                           options->has_background ? options->background : NULL, true) ||
            !image_sink_write(op, sink, row, stride))
            break;
    }
    return op->error->code;
}
