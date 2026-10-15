#include "image_internal.h"
#include <tiffio.h>
#include <stdio.h>

typedef struct ImageTiffIO {
    ImageOperation *op;
    const ImageSource *source;
    ImageByteBuffer buffer;
    size_t position;
} ImageTiffIO;

static int image_tiff_diagnostic(TIFF *, void *user, const char *module, const char *format, va_list args) {
    ImageOperation *op = (ImageOperation *)user;
    char text[192];
    vsnprintf(text, sizeof(text), format, args);
    image_fail(op, IMAGE_INVALID_DATA, "TIFF %s: %s", module ? module : "codec", text);
    return 1;
}
static int image_tiff_warning(TIFF *, void *user, const char *module, const char *format, va_list args) {
    ImageOperation *op = (ImageOperation *)user;
    if (op->context->warning) {
        char text[192];
        vsnprintf(text, sizeof(text), format, args);
        image_warn(op, IMAGE_CODEC_TIFF, "TIFF %s: %s", module ? module : "codec", text);
    }
    return 1;
}
static tmsize_t image_tiff_read(thandle_t handle, void *data, tmsize_t requested) {
    ImageTiffIO *io = (ImageTiffIO *)handle;
    if (requested < 0 || io->position > io->buffer.length)
        return 0;
    size_t size = (size_t)requested;
    if (size > io->buffer.length - io->position)
        size = io->buffer.length - io->position;
    if (io->source) {
        if (!image_source_read(io->op, io->source, io->position, data, size))
            return 0;
    } else
        memcpy(data, io->buffer.data + io->position, size);
    io->position += size;
    return (tmsize_t)size;
}
static tmsize_t image_tiff_write(thandle_t handle, void *data, tmsize_t requested) {
    ImageTiffIO *io = (ImageTiffIO *)handle;
    if (requested < 0 || io->source) {
        image_fail(io->op, IMAGE_INVALID_ARGUMENT, "invalid TIFF write");
        return 0;
    }
    if (image_op_cancelled(io->op))
        return 0;
    if (!image_buffer_write_at(&io->buffer, io->position, data, (size_t)requested))
        return 0;
    io->position += (size_t)requested;
    return requested;
}
static toff_t image_tiff_seek(thandle_t handle, toff_t offset, int whence) {
    ImageTiffIO *io = (ImageTiffIO *)handle;
    uint64_t position = offset;
    if (whence != SEEK_SET) {
        uint64_t base = whence == SEEK_CUR ? io->position : io->buffer.length;
        int64_t delta = (int64_t)offset;
        if (delta < 0) {
            uint64_t distance = (uint64_t)(-(delta + 1)) + 1;
            if (distance > base)
                return (toff_t)-1;
            position = base - distance;
        } else {
            if ((uint64_t)delta > UINT64_MAX - base)
                return (toff_t)-1;
            position = base + (uint64_t)delta;
        }
    }
    if (position > SIZE_MAX || position > io->op->context->limits.max_encoded_bytes)
        return (toff_t)-1;
    if (io->source && position > io->buffer.length)
        return (toff_t)-1;
    io->position = (size_t)position;
    return (toff_t)position;
}
static int image_tiff_close(thandle_t) {
    return 0;
}
static toff_t image_tiff_size(thandle_t handle) {
    return ((ImageTiffIO *)handle)->buffer.length;
}
static TIFF *image_tiff_open(ImageTiffIO *io, bool write) {
    TIFFOpenOptions *options = TIFFOpenOptionsAlloc();
    if (!options) {
        image_fail(io->op, IMAGE_OUT_OF_MEMORY, "cannot create TIFF options");
        return NULL;
    }
    TIFFOpenOptionsSetErrorHandlerExtR(options, image_tiff_diagnostic, io->op);
    TIFFOpenOptionsSetWarningHandlerExtR(options, image_tiff_warning, io->op);
    TIFFOpenOptionsSetMaxSingleMemAlloc(options, (tmsize_t)io->op->context->limits.max_scratch_bytes);
    TIFFOpenOptionsSetMaxCumulatedMemAlloc(options, (tmsize_t)io->op->context->limits.max_scratch_bytes);
    TIFF *tiff = TIFFClientOpenExt("image", write ? "w" : "r", io, image_tiff_read, image_tiff_write,
                                   image_tiff_seek, image_tiff_close, image_tiff_size, NULL, NULL, options);
    TIFFOpenOptionsFree(options);
    if (!tiff)
        image_fail(io->op, IMAGE_INVALID_DATA, "cannot open TIFF container");
    return tiff;
}
static bool image_tiff_info(ImageOperation *op, TIFF *tiff, ImageInfo *info) {
    uint16_t bits = 8, samples = 1, orientation = 1;
    if (!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &info->width) ||
        !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &info->height)) {
        image_fail(op, IMAGE_INVALID_DATA, "TIFF page has no dimensions");
        return false;
    }
    TIFFGetFieldDefaulted(tiff, TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_ORIENTATION, &orientation);
    info->bit_depth = bits;
    info->has_alpha = samples == 2 || samples == 4;
    info->orientation = orientation >= 1 && orientation <= 8 ? (uint8_t)orientation : 1;
    info->frame_count = 1;
    info->frames_known = true;
    info->pages_known = false;
    uint32_t profile_size = 0;
    void *profile = NULL;
    if (TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &profile_size, &profile)) {
        if (profile_size > op->context->limits.max_metadata_bytes) {
            image_fail(op, IMAGE_LIMIT_EXCEEDED, "TIFF color profile exceeds metadata limit");
            return false;
        }
        info->has_icc = true;
    }
    return image_check_dimensions(op, info->width, info->height);
}
ImageStatus image_tiff_probe(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    ImageTiffIO io = {op, source, {op, NULL, source->length, 0}, 0};
    TIFF *tiff = image_tiff_open(&io, false);
    if (tiff) {
        image_tiff_info(op, tiff, info);
        TIFFClose(tiff);
    }
    return op->error->code;
}
static void image_tiff_unpack(uint8_t *output, const uint32_t *raster, uint32_t count) {
    // libtiff's RGBA convenience API returns associated alpha in packed ABGR words.
    for (uint32_t x = 0; x < count; x++) {
        uint32_t p = raster[x];
        unsigned a = TIFFGetA(p);
        unsigned samples[3] = {TIFFGetR(p), TIFFGetG(p), TIFFGetB(p)};
        for (unsigned c = 0; c < 3; c++)
            output[x * 4 + c] = image_unpremultiply_channel((uint8_t)samples[c], (uint8_t)a);
        output[x * 4 + 3] = (uint8_t)a;
    }
}
static uint32_t image_tiff_select_level(ImageOperation *op, TIFF *tiff, const ImageInfo *primary,
                                        uint32_t target_width, uint32_t target_height) {
    uint16_t count = 0;
    toff_t *subifds = NULL;
    if (!TIFFGetField(tiff, TIFFTAG_SUBIFD, &count, &subifds) || !count)
        return 0;
    if (count > op->context->limits.max_pages) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "TIFF SubIFD count exceeds traversal limit");
        return 0;
    }
    toff_t base = TIFFCurrentDirOffset(tiff), chosen = base;
    toff_t *offsets = (toff_t *)image_op_alloc(op, (size_t)count * sizeof(*offsets), false);
    if (!offsets)
        return 0;
    memcpy(offsets, subifds, (size_t)count * sizeof(*offsets));
    uint16_t primary_photo = 0, primary_samples = 1;
    TIFFGetFieldDefaulted(tiff, TIFFTAG_PHOTOMETRIC, &primary_photo);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &primary_samples);
    uint32_t profile_size = 0;
    void *profile = NULL;
    uint8_t *profile_copy = NULL;
    if (TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &profile_size, &profile) && profile_size) {
        profile_copy = (uint8_t *)image_op_alloc(op, profile_size, false);
        if (!profile_copy)
            return 0;
        memcpy(profile_copy, profile, profile_size);
    }
    uint64_t chosen_area = (uint64_t)primary->width * primary->height;
    uint32_t representation = 0;
    // only direct reduced-image SubIFDs qualify; ordinary pages are never previews.
    for (uint16_t i = 0; i < count && op->error->code == IMAGE_OK; i++) {
        if (image_op_cancelled(op))
            break;
        if (!offsets[i] || offsets[i] == base || !TIFFSetSubDirectory(tiff, offsets[i])) {
            image_fail(op, IMAGE_INVALID_DATA, "invalid TIFF reduced-image directory");
            break;
        }
        uint32_t flags = 0;
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SUBFILETYPE, &flags);
        if (!(flags & FILETYPE_REDUCEDIMAGE))
            continue;
        ImageInfo level = {};
        if (!image_tiff_info(op, tiff, &level))
            break;
        uint16_t photo = 0, samples = 1;
        TIFFGetFieldDefaulted(tiff, TIFFTAG_PHOTOMETRIC, &photo);
        TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
        uint64_t area = (uint64_t)level.width * level.height;
        if (level.width < target_width || level.height < target_height || area >= chosen_area ||
            (uint64_t)level.width * primary->height != (uint64_t)level.height * primary->width ||
            level.orientation != primary->orientation || level.bit_depth != primary->bit_depth ||
            level.has_alpha != primary->has_alpha || photo != primary_photo || samples != primary_samples)
            continue;
        uint32_t candidate_size = 0;
        void *candidate_profile = NULL;
        TIFFGetField(tiff, TIFFTAG_ICCPROFILE, &candidate_size, &candidate_profile);
        if (candidate_size != profile_size ||
            (profile_size && memcmp(profile_copy, candidate_profile, profile_size)))
            continue;
        chosen = offsets[i];
        chosen_area = area;
        representation = (uint32_t)i + 1;
    }
    if (op->error->code == IMAGE_OK && !TIFFSetSubDirectory(tiff, chosen))
        image_fail(op, IMAGE_INVALID_DATA, "cannot restore selected TIFF directory");
    image_op_free(op, offsets);
    image_op_free(op, profile_copy);
    return representation;
}
ImageStatus image_tiff_decode(ImageOperation *op, const ImageSource *source,
                              const ImageDecodeOptions *options, ImageDecoded *result) {
    ImageTiffIO io = {op, source, {op, NULL, source->length, 0}, 0};
    TIFF *tiff = image_tiff_open(&io, false);
    if (!tiff)
        return op->error->code;
    if (options->page_index >= op->context->limits.max_pages)
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "TIFF page index exceeds traversal limit");
    else if (options->page_index && !TIFFSetDirectory(tiff, (tdir_t)options->page_index))
        image_fail(op, IMAGE_INVALID_ARGUMENT, "TIFF page index out of range");
    if (op->error->code != IMAGE_OK || !image_tiff_info(op, tiff, &result->info)) {
        TIFFClose(tiff);
        return op->error->code;
    }
    uint32_t width = result->info.width, height = result->info.height, tw, th;
    image_target_size(&result->info, options, &tw, &th);
    if (options->mode == IMAGE_DECODE_THUMBNAIL && (tw < width || th < height)) {
        result->representation_index = image_tiff_select_level(op, tiff, &result->info, tw, th);
        if (op->error->code != IMAGE_OK) {
            TIFFClose(tiff);
            return op->error->code;
        }
        TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
        TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    }
    uint16_t photometric = 0;
    TIFFGetFieldDefaulted(tiff, TIFFTAG_PHOTOMETRIC, &photometric);
    if (photometric != PHOTOMETRIC_MINISWHITE && photometric != PHOTOMETRIC_MINISBLACK &&
        photometric != PHOTOMETRIC_RGB && photometric != PHOTOMETRIC_PALETTE) {
        TIFFClose(tiff);
        return image_fail(op, IMAGE_UNSUPPORTED_FEATURE, "unsupported TIFF photometric interpretation");
    }
    // preserve encoded orientation for the common normalizer; do not apply it twice.
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    bool tiled = TIFFIsTiled(tiff) != 0;
    uint16_t samples = 1, planar = PLANARCONFIG_CONTIG, extras = 0;
    uint16_t *extra_types = NULL;
    TIFFGetFieldDefaulted(tiff, TIFFTAG_SAMPLESPERPIXEL, &samples);
    TIFFGetFieldDefaulted(tiff, TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetField(tiff, TIFFTAG_EXTRASAMPLES, &extras, &extra_types);
    // preserve unassociated 8-bit samples exactly; RGBA convenience decoding premultiplies them.
    bool direct = !tiled && result->info.bit_depth == 8 && photometric == PHOTOMETRIC_RGB &&
                  planar == PLANARCONFIG_CONTIG && (samples == 3 || samples == 4) &&
                  (samples == 3 || (extras == 1 && extra_types[0] == EXTRASAMPLE_UNASSALPHA));
    uint32_t block_width = width, block_height = height;
    if (tiled) {
        TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &block_width);
        TIFFGetField(tiff, TIFFTAG_TILELENGTH, &block_height);
    } else
        TIFFGetFieldDefaulted(tiff, TIFFTAG_ROWSPERSTRIP, &block_height);
    if (direct)
        block_height = 1;
    if (!block_width || !block_height) {
        TIFFClose(tiff);
        return image_fail(op, IMAGE_INVALID_DATA, "invalid TIFF block size");
    }
    uint32_t band_height = block_height < height ? block_height : height;
    size_t raster_bytes = 0, band_bytes = 0;
    if (!math_checked_mul((size_t)block_width * 4, tiled ? block_height : band_height, &raster_bytes) ||
        !math_checked_mul((size_t)width * 4, band_height, &band_bytes)) {
        TIFFClose(tiff);
        return image_fail(op, IMAGE_LIMIT_EXCEEDED, "TIFF block layout overflow");
    }
    if (options->mode == IMAGE_DECODE_THUMBNAIL && !options->allow_full_decode &&
        !result->representation_index && (tw < width || th < height) && band_height == height &&
        (!tiled || block_width >= width)) {
        TIFFClose(tiff);
        return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE, "TIFF source has one full-resolution block");
    }
    uint32_t *raster = (uint32_t *)image_op_alloc(op, raster_bytes, false);
    uint8_t *band = tiled ? (uint8_t *)image_op_alloc(op, band_bytes, false) : NULL;
    uint8_t *row = (uint8_t *)image_op_alloc(op, (size_t)width * 4, false);
    ImageReducer reducer;
    if (!raster || !row || (tiled && !band) ||
        !image_reducer_init(&reducer, op, width, height, tw, th, result)) {
        TIFFClose(tiff);
        return op->error->code;
    }
    for (uint32_t base = 0; base < height && op->error->code == IMAGE_OK;) {
        uint32_t rows = height - base < block_height ? height - base : block_height;
        if (tiled) {
            for (uint32_t left = 0; left < width && op->error->code == IMAGE_OK;) {
                uint32_t columns = width - left < block_width ? width - left : block_width;
                if (!TIFFReadRGBATile(tiff, left, base, raster)) {
                    image_fail(op, IMAGE_INVALID_DATA, "cannot decode TIFF tile");
                    break;
                }
                for (uint32_t y = 0; y < rows; y++)
                    image_tiff_unpack(band + ((size_t)y * width + left) * 4,
                                      raster + (size_t)(block_height - 1 - y) * block_width, columns);
                left += columns;
            }
        } else if (direct ? TIFFReadScanline(tiff, raster, base, 0) < 0
                          : !TIFFReadRGBAStrip(tiff, base, raster)) {
            image_fail(op, IMAGE_INVALID_DATA, "cannot decode TIFF strip");
            break;
        }
        for (uint32_t y = 0; y < rows && op->error->code == IMAGE_OK; y++) {
            if (tiled)
                memcpy(row, band + (size_t)y * width * 4, (size_t)width * 4);
            else if (direct) {
                const uint8_t *input = (const uint8_t *)raster;
                for (uint32_t x = 0; x < width; x++) {
                    memcpy(row + x * 4, input + (size_t)x * samples, 3);
                    row[x * 4 + 3] = samples == 4 ? input[x * 4 + 3] : 255;
                }
            } else
                image_tiff_unpack(row, raster + (size_t)(rows - 1 - y) * width, width);
            image_reducer_row(&reducer, row, base + y);
        }
        base += rows;
    }
    image_reducer_finish(&reducer);
    if (result->representation_index)
        result->strategy = IMAGE_STRATEGY_SELECTED_RESOLUTION;
    else if (tw < width || th < height)
        result->strategy = IMAGE_STRATEGY_STREAMED_REDUCE;
    TIFFClose(tiff);
    return op->error->code;
}
ImageStatus image_tiff_encode(ImageOperation *op, const ImagePixelView *pixels, const ImageEncodeOptions *,
                              const ImageSink *sink) {
    ImageTiffIO io = {op, NULL, {op, NULL, 0, 0}, 0};
    TIFF *tiff = image_tiff_open(&io, true);
    if (!tiff)
        return op->error->code;
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, pixels->width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, pixels->height);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, pixels->channels);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, pixels->channels == 1 ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_ADOBE_DEFLATE);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 16);
    if (pixels->channels == 4) {
        uint16_t alpha = EXTRASAMPLE_UNASSALPHA;
        TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, 1, &alpha);
    }
    for (uint32_t y = 0; y < pixels->height && op->error->code == IMAGE_OK; y++) {
        if (image_op_cancelled(op))
            break;
        if (TIFFWriteScanline(tiff, (void *)(pixels->pixels + (size_t)y * pixels->stride), y, 0) < 0)
            image_fail(op, IMAGE_INVALID_DATA, "cannot encode TIFF row");
    }
    if (op->error->code == IMAGE_OK && !TIFFWriteDirectory(tiff))
        image_fail(op, IMAGE_INVALID_DATA, "cannot finish TIFF directory");
    TIFFClose(tiff);
    if (op->error->code == IMAGE_OK)
        image_sink_write(op, sink, io.buffer.data, io.buffer.length);
    return op->error->code;
}
