#include "image_internal.h"
#include <png.h>
#include <stdio.h>

typedef struct ImagePngState {
    ImageOperation *op;
    const ImageSource *source;
    const ImageSink *sink;
    size_t offset;
    ImageReducer reducer;
} ImagePngState;

static void image_png_error(png_structp png, const char *message) {
    ImagePngState *state = (ImagePngState *)png_get_error_ptr(png);
    image_fail(state->op, IMAGE_INVALID_DATA, "PNG: %s", message);
    png_longjmp(png, 1);
}
static void image_png_warning(png_structp png, const char *message) {
    ImagePngState *state = (ImagePngState *)png_get_error_ptr(png);
    image_warn(state->op, IMAGE_CODEC_PNG, "PNG: %s", message);
}
static png_voidp image_png_alloc(png_structp png, png_alloc_size_t size) {
    return image_op_alloc((ImageOperation *)png_get_mem_ptr(png), size, false);
}
static void image_png_free(png_structp png, png_voidp pointer) {
    image_op_free((ImageOperation *)png_get_mem_ptr(png), pointer);
}
static void image_png_read(png_structp png, png_bytep data, png_size_t size) {
    ImagePngState *state = (ImagePngState *)png_get_io_ptr(png);
    if (!image_source_read(state->op, state->source, state->offset, data, size))
        png_error(png, "incomplete source");
    state->offset += size;
}
static void image_png_write(png_structp png, png_bytep data, png_size_t size) {
    ImagePngState *state = (ImagePngState *)png_get_io_ptr(png);
    if (!image_sink_write(state->op, state->sink, data, size))
        png_error(png, "output sink failed");
}
static ImageStatus image_png_read_common(ImageOperation *op, const ImageSource *source,
                                         const ImageDecodeOptions *options, ImageDecoded *result,
                                         ImageInfo *info) {
    ImagePngState *state = (ImagePngState *)image_op_alloc(op, sizeof(*state), false);
    if (!state)
        return op->error->code;
    memset(state, 0, sizeof(*state));
    state->op = op;
    state->source = source;
    png_structp png = png_create_read_struct_2(PNG_LIBPNG_VER_STRING, state, image_png_error,
                                               image_png_warning, op, image_png_alloc, image_png_free);
    if (!png)
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create PNG decoder");
    png_infop metadata = png_create_info_struct(png);
    if (!metadata) {
        png_destroy_read_struct(&png, NULL, NULL);
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create PNG metadata");
    }
    // all state modified after setjmp is heap-owned; longjmp never crosses C++ owners.
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &metadata, NULL);
        return op->error->code;
    }
    png_set_read_fn(png, state, image_png_read);
    png_set_user_limits(png, op->context->limits.max_dimension, op->context->limits.max_dimension);
    png_set_chunk_malloc_max(png, op->context->limits.max_metadata_bytes);
    png_set_chunk_cache_max(png, 128);
    png_set_crc_action(png, PNG_CRC_QUIET_USE, PNG_CRC_QUIET_USE);
#ifdef PNG_IGNORE_ADLER32
    // retain the existing browser-compatible checksum policy for both source types.
    png_set_option(png, PNG_IGNORE_ADLER32, PNG_OPTION_ON);
#endif
    png_read_info(png, metadata);
    uint32_t width = png_get_image_width(png, metadata), height = png_get_image_height(png, metadata);
    int color = png_get_color_type(png, metadata), depth = png_get_bit_depth(png, metadata);
    bool transparency = png_get_valid(png, metadata, PNG_INFO_tRNS) != 0;
    info->width = width;
    info->height = height;
    info->bit_depth = (uint16_t)depth;
    info->has_alpha = (color & PNG_COLOR_MASK_ALPHA) || transparency;
    info->has_icc = png_get_valid(png, metadata, PNG_INFO_iCCP) != 0;
    info->orientation = 1;
    info->frame_count = info->page_count = 1;
    info->frames_known = info->pages_known = true;
    if (!options || !image_check_dimensions(op, width, height)) {
        png_destroy_read_struct(&png, &metadata, NULL);
        return op->error->code;
    }
    uint32_t tw, th;
    image_target_size(info, options, &tw, &th);
    bool shrinking = tw < width || th < height;
    bool interlaced = png_get_interlace_type(png, metadata) != PNG_INTERLACE_NONE;
    if (shrinking && interlaced && !options->allow_full_decode) {
        png_destroy_read_struct(&png, &metadata, NULL);
        return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE, "interlaced PNG needs full pixels");
    }
    if (depth == 16)
        png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (transparency)
        png_set_tRNS_to_alpha(png);
    if (!(color & PNG_COLOR_MASK_ALPHA) && !transparency)
        png_set_filler(png, 255, PNG_FILLER_AFTER);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    int passes = png_set_interlace_handling(png);
    png_read_update_info(png, metadata);
    if (png_get_rowbytes(png, metadata) != (size_t)width * 4)
        png_error(png, "unexpected pixel stride");
    if (shrinking && !interlaced) {
        if (!image_reducer_init(&state->reducer, op, width, height, tw, th, result))
            png_error(png, "thumbnail allocation failed");
        uint8_t *row = (uint8_t *)image_op_alloc(op, (size_t)width * 4, false);
        if (!row)
            png_error(png, "row allocation failed");
        for (uint32_t y = 0; y < height; y++) {
            png_read_row(png, row, NULL);
            if (!image_reducer_row(&state->reducer, row, y))
                png_error(png, "thumbnail cancelled");
        }
        image_reducer_finish(&state->reducer);
        image_op_free(op, row);
        result->strategy = IMAGE_STRATEGY_STREAMED_REDUCE;
    } else {
        if (!image_allocate_pixels(op, width, height, result))
            png_error(png, "pixel allocation failed");
        memset(result->pixels, 0, result->size);
        for (int pass = 0; pass < passes; pass++)
            for (uint32_t y = 0; y < height; y++) {
                if (image_op_cancelled(op))
                    png_error(png, "decode cancelled");
                png_read_row(png, result->pixels + (size_t)y * result->stride, NULL);
            }
    }
    png_read_end(png, NULL);
    png_destroy_read_struct(&png, &metadata, NULL);
    return op->error->code;
}
ImageStatus image_png_probe(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    return image_png_read_common(op, source, NULL, NULL, info);
}
ImageStatus image_png_decode(ImageOperation *op, const ImageSource *source, const ImageDecodeOptions *options,
                             ImageDecoded *result) {
    return image_png_read_common(op, source, options, result, &result->info);
}
ImageStatus image_png_encode_rows(ImageOperation *op, const ImageRowSource *rows, const ImageSink *sink) {
    ImagePngState *state = (ImagePngState *)image_op_alloc(op, sizeof(*state), false);
    if (!state)
        return op->error->code;
    memset(state, 0, sizeof(*state));
    state->op = op;
    state->sink = sink;
    png_structp png = png_create_write_struct_2(PNG_LIBPNG_VER_STRING, state, image_png_error,
                                                image_png_warning, op, image_png_alloc, image_png_free);
    if (!png)
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create PNG encoder");
    png_infop metadata = png_create_info_struct(png);
    if (!metadata) {
        png_destroy_write_struct(&png, NULL);
        return image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create PNG metadata");
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &metadata);
        return op->error->code;
    }
    png_set_write_fn(png, state, image_png_write, NULL);
    int color = rows->channels == 1   ? PNG_COLOR_TYPE_GRAY
                : rows->channels == 3 ? PNG_COLOR_TYPE_RGB
                                      : PNG_COLOR_TYPE_RGBA;
    png_set_IHDR(png, metadata, rows->width, rows->height, 8, color, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, metadata);
    size_t row_bytes = (size_t)rows->width * rows->channels;
    uint8_t *row = (uint8_t *)image_op_alloc(op, row_bytes, false);
    if (!row)
        png_error(png, "row allocation failed");
    for (uint32_t y = 0; y < rows->height; y++) {
        if (image_op_cancelled(op))
            png_error(png, "encode cancelled");
        ImageStatus status = rows->read_row(rows->user, y, row, row_bytes);
        if (status != IMAGE_OK) {
            image_fail(op, status, "image row source failed at row %u", y);
            png_error(png, "row source failed");
        }
        if (rows->premultiplied)
            image_unpremultiply_rgba_row(row, row, rows->width);
        png_write_row(png, row);
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &metadata);
    return op->error->code;
}
static ImageStatus image_png_pixel_row(void *user, uint32_t y, uint8_t *row, size_t size) {
    const ImagePixelView *pixels = (const ImagePixelView *)user;
    memcpy(row, pixels->pixels + (size_t)y * pixels->stride, size);
    return IMAGE_OK;
}
ImageStatus image_png_encode(ImageOperation *op, const ImagePixelView *pixels, const ImageEncodeOptions *,
                             const ImageSink *sink) {
    ImageRowSource rows = {image_png_pixel_row, (void *)pixels,   pixels->width,
                           pixels->height,      pixels->channels, pixels->premultiplied};
    return image_png_encode_rows(op, &rows, sink);
}
