#include "image_internal.h"
#include <avif/avif.h>
#include <limits.h>

static ImageStatus image_avif_error(ImageOperation *op, avifResult status,
                                    const avifDiagnostics *diagnostics) {
    ImageStatus code = status == AVIF_RESULT_OUT_OF_MEMORY        ? IMAGE_OUT_OF_MEMORY
                       : status == AVIF_RESULT_TRUNCATED_DATA     ? IMAGE_TRUNCATED_INPUT
                       : status == AVIF_RESULT_NO_CODEC_AVAILABLE ? IMAGE_CODEC_UNAVAILABLE
                                                                  : IMAGE_INVALID_DATA;
    image_fail(op, code, "AVIF: %s (%s)", avifResultToString(status), diagnostics ? diagnostics->error : "");
    op->error->native_code = status;
    return op->error->code;
}
static uint8_t image_avif_orientation(const avifImage *image) {
    static const unsigned rotation[4] = {0, 7, 2, 5};
    unsigned angle = image->transformFlags & AVIF_TRANSFORM_IROT ? image->irot.angle : 0;
    int8_t matrix[4];
    memcpy(matrix, image_orientation_matrices[rotation[angle & 3]], sizeof(matrix));
    if (image->transformFlags & AVIF_TRANSFORM_IMIR) {
        unsigned first = image->imir.axis ? 0 : 2;
        matrix[first] = -matrix[first];
        matrix[first + 1] = -matrix[first + 1];
    }
    for (unsigned i = 0; i < 8; i++)
        if (!memcmp(matrix, image_orientation_matrices[i], sizeof(matrix)))
            return (uint8_t)(i + 1);
    return 1;
}
static avifDecoder *image_avif_open(ImageOperation *op, const ImageSource *source) {
    const uint8_t *data = image_source_bytes(op, source);
    if (!data)
        return NULL;
    avifDecoder *decoder = avifDecoderCreate();
    if (!decoder) {
        image_fail(op, IMAGE_OUT_OF_MEMORY, "cannot create AVIF decoder");
        return NULL;
    }
    decoder->maxThreads = (int)(op->context->limits.max_threads ? op->context->limits.max_threads : 1);
    decoder->imageSizeLimit = op->context->limits.max_source_pixels > UINT32_MAX
                                  ? UINT32_MAX
                                  : (uint32_t)op->context->limits.max_source_pixels;
    decoder->imageDimensionLimit = op->context->limits.max_dimension;
    decoder->imageCountLimit = op->context->limits.max_frames;
    decoder->ignoreXMP = AVIF_TRUE;
    decoder->ignoreExif = AVIF_TRUE;
    avifResult status = avifDecoderSetIOMemory(decoder, data, source->length);
    if (status == AVIF_RESULT_OK)
        status = avifDecoderParse(decoder);
    if (status != AVIF_RESULT_OK) {
        image_avif_error(op, status, &decoder->diag);
        avifDecoderDestroy(decoder);
        return NULL;
    }
    if (decoder->image->icc.size > op->context->limits.max_metadata_bytes) {
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "AVIF color profile exceeds metadata limit");
        avifDecoderDestroy(decoder);
        return NULL;
    }
    return decoder;
}
static void image_avif_info(const avifDecoder *decoder, ImageInfo *info) {
    const avifImage *image = decoder->image;
    info->width = image->width;
    info->height = image->height;
    info->bit_depth = (uint16_t)image->depth;
    info->has_alpha = decoder->alphaPresent != 0;
    info->has_icc = image->icc.size != 0;
    info->frame_count = (uint32_t)decoder->imageCount;
    info->page_count = 1;
    info->frames_known = info->pages_known = true;
    info->orientation = image_avif_orientation(image);
    info->color_primaries = image->colorPrimaries;
    info->transfer_characteristics = image->transferCharacteristics;
    info->matrix_coefficients = image->matrixCoefficients;
}
ImageStatus image_avif_probe(ImageOperation *op, const ImageSource *source, ImageInfo *info) {
    avifDecoder *decoder = image_avif_open(op, source);
    if (!decoder)
        return op->error->code;
    image_avif_info(decoder, info);
    avifDecoderDestroy(decoder);
    return op->error->code;
}
ImageStatus image_avif_decode(ImageOperation *op, const ImageSource *source,
                              const ImageDecodeOptions *options, ImageDecoded *result) {
    uint32_t tw, th;
    image_target_size(&result->info, options, &tw, &th);
    if (options->mode == IMAGE_DECODE_THUMBNAIL && !options->allow_full_decode &&
        (tw < result->info.width || th < result->info.height))
        return image_fail(op, IMAGE_FAST_PATH_UNAVAILABLE, "AVIF requires full-resolution AV1 planes");
    avifDecoder *decoder = image_avif_open(op, source);
    if (!decoder)
        return op->error->code;
    avifImage *image = decoder->image;
    if (image->transformFlags & AVIF_TRANSFORM_CLAP)
        image_fail(op, IMAGE_UNSUPPORTED_FEATURE, "AVIF clean aperture is not enabled");
    else if (image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_SMPTE2084 ||
             image->transferCharacteristics == AVIF_TRANSFER_CHARACTERISTICS_HLG)
        image_fail(op, IMAGE_UNSUPPORTED_FEATURE, "HDR AVIF requires explicit tone mapping");
    // admit native YUV/alpha working planes separately from the returned RGBA buffer.
    else if ((uint64_t)image->width * image->height * 8 > op->context->limits.max_scratch_bytes)
        image_fail(op, IMAGE_LIMIT_EXCEEDED, "AVIF working planes exceed scratch budget");
    else if (!image_op_cancelled(op)) {
        avifResult status = avifDecoderNextImage(decoder);
        if (status != AVIF_RESULT_OK)
            image_avif_error(op, status, &decoder->diag);
        else if (image_allocate_pixels(op, image->width, image->height, result)) {
            avifRGBImage rgb;
            avifRGBImageSetDefaults(&rgb, image);
            rgb.depth = 8;
            rgb.format = AVIF_RGB_FORMAT_RGBA;
            rgb.alphaPremultiplied = AVIF_FALSE;
            rgb.pixels = result->pixels;
            rgb.rowBytes = (uint32_t)result->stride;
            status = avifImageYUVToRGB(image, &rgb);
            if (status != AVIF_RESULT_OK)
                image_avif_error(op, status, &decoder->diag);
        }
    }
    avifDecoderDestroy(decoder);
    return op->error->code;
}
