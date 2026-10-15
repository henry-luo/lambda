#include <gtest/gtest.h>
#include "../lib/image.h"
#include "../lib/endian.h"
#include "../lib/file.h"
#include "../lib/memtrack.h"
#include "../lambda/io/image_io.h"
#include <string.h>
#include <stdio.h>
#include <tiffio.h>
#include <avif/avif.h>

static const ImageCodecFormat writable_formats[] = {IMAGE_CODEC_PNG,  IMAGE_CODEC_JPEG, IMAGE_CODEC_WEBP,
                                                    IMAGE_CODEC_AVIF, IMAGE_CODEC_BMP,  IMAGE_CODEC_TIFF,
                                                    IMAGE_CODEC_ICO};

struct ImageFixture {
    uint8_t pixels[32 * 24 * 4];
    ImagePixelView view;
    ImageFixture() : view{pixels, sizeof(pixels), 32 * 4, 32, 24, 4, false} {
        create_dir("temp");
        create_dir("temp/image-io-tests");
        for (unsigned y = 0; y < 24; y++)
            for (unsigned x = 0; x < 32; x++) {
                uint8_t *p = pixels + (y * 32 + x) * 4;
                p[0] = (uint8_t)(x * 7);
                p[1] = (uint8_t)(y * 9);
                p[2] = (uint8_t)((x + y) * 4);
                p[3] = 255;
            }
    }
};
static ImageStatus encode_fixture(ImageFixture *fixture, ImageCodecFormat format, ImageEncoded *bytes,
                                  ImageError *error) {
    ImageEncodeOptions options;
    image_encode_options_init(&options, format);
    options.lossless = true;
    options.quality = 100;
    return image_encode_to_memory(NULL, &fixture->view, &options, bytes, error);
}

TEST(ImageIO, AllFormatRoundTrips) {
    ImageFixture fixture;
    for (ImageCodecFormat format : writable_formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded bytes = {};
        ImageError error = {};
        ASSERT_EQ(encode_fixture(&fixture, format, &bytes, &error), IMAGE_OK) << error.message;
        ASSERT_EQ(image_detect(bytes.data, bytes.length), format);
        ImageSource source = image_source_memory(bytes.data, bytes.length);
        ImageInfo info = {};
        ASSERT_EQ(image_probe(NULL, &source, &info, &error), IMAGE_OK) << error.message;
        EXPECT_EQ(info.width, 32u);
        EXPECT_EQ(info.height, 24u);
        EXPECT_EQ(info.format, format);
        ImageDecoded decoded = {};
        ASSERT_EQ(image_decode(NULL, &source, NULL, &decoded, &error), IMAGE_OK) << error.message;
        ASSERT_EQ(decoded.size, sizeof(fixture.pixels));
        for (size_t i = 0; i < decoded.size; i++) {
            if (format == IMAGE_CODEC_JPEG)
                EXPECT_NEAR(decoded.pixels[i], fixture.pixels[i], 3);
            else
                ASSERT_EQ(decoded.pixels[i], fixture.pixels[i]) << "byte " << i;
        }
        image_decoded_dispose(&decoded);
        image_free(bytes.data);
    }
}
TEST(ImageIO, LosslessTransparency) {
    ImageFixture fixture;
    for (size_t i = 0; i < sizeof(fixture.pixels); i += 4)
        fixture.pixels[i + 3] = (uint8_t)(64 + (i / 4) % 192);
    const ImageCodecFormat formats[] = {IMAGE_CODEC_PNG, IMAGE_CODEC_WEBP, IMAGE_CODEC_AVIF, IMAGE_CODEC_TIFF,
                                        IMAGE_CODEC_ICO};
    for (ImageCodecFormat format : formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded bytes = {};
        ImageError error = {};
        ASSERT_EQ(encode_fixture(&fixture, format, &bytes, &error), IMAGE_OK) << error.message;
        ImageSource source = image_source_memory(bytes.data, bytes.length);
        ImageDecoded decoded = {};
        ASSERT_EQ(image_decode(NULL, &source, NULL, &decoded, &error), IMAGE_OK) << error.message;
        for (size_t i = 0; i < decoded.size; i++)
            ASSERT_EQ(decoded.pixels[i], fixture.pixels[i]) << "byte " << i;
        image_decoded_dispose(&decoded);
        image_free(bytes.data);
    }
}
// exercise layouts at the public boundary so shared conversion cannot read row padding.
TEST(ImageIO, ChannelLayoutsAndPremultipliedInputs) {
    const uint8_t channels[] = {1, 3, 4};
    const uint8_t samples[] = {96, 128, 160};
    for (ImageCodecFormat format : writable_formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        for (uint8_t count : channels) {
            SCOPED_TRACE(count);
            for (bool premultiplied : {false, true}) {
                if (premultiplied && count != 4)
                    continue;
                uint8_t pixels[3 * (5 * 4 + 7)], original[sizeof(pixels)];
                memset(pixels, 0xa7, sizeof(pixels));
                size_t stride = 5 * count + 7;
                for (unsigned y = 0; y < 3; y++)
                    for (unsigned x = 0; x < 5; x++) {
                        uint8_t *p = pixels + y * stride + x * count;
                        for (unsigned c = 0; c < count; c++)
                            p[c] = c == 3 ? 128 : premultiplied ? samples[c] / 2 : samples[c];
                    }
                memcpy(original, pixels, sizeof(pixels));
                ImagePixelView view = {pixels, sizeof(pixels), stride, 5, 3, count, premultiplied};
                ImageEncodeOptions options;
                image_encode_options_init(&options, format);
                options.lossless = true;
                options.quality = 100;
                options.has_background = true;
                options.background[0] = 32;
                options.background[1] = 64;
                options.background[2] = 96;
                ImageEncoded bytes = {};
                ImageError error = {};
                ASSERT_EQ(image_encode_to_memory(NULL, &view, &options, &bytes, &error), IMAGE_OK)
                    << error.message;
                EXPECT_EQ(memcmp(pixels, original, sizeof(pixels)), 0);
                ImageSource source = image_source_memory(bytes.data, bytes.length);
                ImageDecoded decoded = {};
                ASSERT_EQ(image_decode(NULL, &source, NULL, &decoded, &error), IMAGE_OK) << error.message;
                bool flatten = format == IMAGE_CODEC_JPEG || format == IMAGE_CODEC_BMP;
                for (size_t i = 0; i < decoded.size; i++) {
                    unsigned c = i % 4;
                    uint8_t expected = c == 3 ? (count == 4 && !flatten ? 128 : 255)
                                              : count == 1 ? samples[0] : samples[c];
                    if (count == 4 && c < 3) {
                        unsigned sample = premultiplied ? samples[c] / 2 : samples[c];
                        if (premultiplied && format != IMAGE_CODEC_JPEG)
                            sample = (sample * 255 + 64) / 128;
                        expected = flatten ? (sample * (premultiplied && format == IMAGE_CODEC_JPEG ? 255 : 128) +
                                              options.background[c] * 127 + 127) / 255 : sample;
                    }
                    if (format == IMAGE_CODEC_JPEG)
                        EXPECT_NEAR(decoded.pixels[i], expected, 3) << "byte " << i;
                    else
                        ASSERT_EQ(decoded.pixels[i], expected) << "byte " << i;
                }
                image_decoded_dispose(&decoded);
                image_free(bytes.data);
            }
        }
    }
}
TEST(ImageIO, AlphaFlatteningRequiresBackground) {
    ImageFixture fixture;
    // the BMP writer visits the last row first; fail after it has already produced opaque rows.
    fixture.pixels[3] = 0;
    const ImageCodecFormat formats[] = {IMAGE_CODEC_JPEG, IMAGE_CODEC_BMP};
    for (ImageCodecFormat format : formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded bytes = {};
        ImageError error = {};
        EXPECT_EQ(encode_fixture(&fixture, format, &bytes, &error), IMAGE_INVALID_ARGUMENT);
        EXPECT_EQ(error.format, format);
        EXPECT_STREQ(error.operation, "encode");
        EXPECT_NE(strstr(error.message, "background"), nullptr);
        EXPECT_EQ(bytes.data, nullptr);
        EXPECT_EQ(bytes.length, 0u);
    }
}
struct ImageWarningCapture {
    unsigned count;
    ImageError warning;
};
static void capture_image_warning(void *user, const ImageError *warning) {
    ImageWarningCapture *capture = (ImageWarningCapture *)user;
    capture->count++;
    capture->warning = *warning;
}
TEST(ImageIO, RecoverableMetadataWarningsUseOperationContext) {
    ImageFixture fixture;
    const ImageCodecFormat formats[] = {IMAGE_CODEC_PNG, IMAGE_CODEC_TIFF};
    for (ImageCodecFormat format : formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded bytes = {};
        ImageError error = {};
        ASSERT_EQ(encode_fixture(&fixture, format, &bytes, &error), IMAGE_OK);
        uint8_t tagged[8192];
        ASSERT_LE(bytes.length + 15, sizeof(tagged));
        size_t length = bytes.length;
        if (format == IMAGE_CODEC_PNG) {
            memcpy(tagged, bytes.data, 33);
            // invalid significant-bit metadata is recoverable; the pixels remain intact.
            write_be32(tagged + 33, 3);
            memcpy(tagged + 37, "sBIT", 4);
            memset(tagged + 41, 9, 3);
            memset(tagged + 44, 0, 4);
            memcpy(tagged + 48, bytes.data + 33, bytes.length - 33);
            length += 15;
        } else {
            memcpy(tagged, bytes.data, bytes.length);
            bool little = tagged[0] == 'I';
            size_t offset = little ? read_le32(tagged + 4) : read_be32(tagged + 4);
            ASSERT_LE(offset + 2, length);
            unsigned count = little ? read_le16(tagged + offset) : read_be16(tagged + offset);
            bool replaced = false;
            for (unsigned i = 0; i < count; i++) {
                uint8_t *entry = tagged + offset + 2 + i * 12;
                ASSERT_LE((size_t)(entry - tagged) + 12, length);
                unsigned tag = little ? read_le16(entry) : read_be16(entry);
                if (tag == TIFFTAG_ORIENTATION) {
                    // an unknown optional TIFF tag warns and leaves the default orientation.
                    if (little) write_le16(entry, 65000);
                    else write_be16(entry, 65000);
                    replaced = true;
                }
            }
            ASSERT_TRUE(replaced);
        }
        ImageContext context;
        image_context_init(&context);
        ImageWarningCapture capture = {};
        context.warning = capture_image_warning;
        context.user = &capture;
        ImageSource source = image_source_memory(tagged, length);
        ImageDecoded image = {};
        ASSERT_EQ(image_decode(&context, &source, NULL, &image, &error), IMAGE_OK) << error.message;
        ASSERT_GT(capture.count, 0u);
        EXPECT_EQ(capture.warning.code, IMAGE_OK);
        EXPECT_EQ(capture.warning.format, format);
        EXPECT_STREQ(capture.warning.operation, "decode");
        EXPECT_NE(capture.warning.message[0], '\0');
        EXPECT_EQ(memcmp(image.pixels, fixture.pixels, sizeof(fixture.pixels)), 0);
        image_decoded_dispose(&image);
        image_free(bytes.data);
    }
}
static void expect_oriented_corners(const ImageDecoded *actual, const uint8_t *reference,
                                    uint32_t width, uint32_t height, unsigned orientation) {
    // corner labels are TL, TR, BL, BR; this asserts observable EXIF placement.
    static const unsigned corners[8][4] = {{0, 1, 2, 3}, {1, 0, 3, 2}, {3, 2, 1, 0}, {2, 3, 0, 1},
                                           {0, 2, 1, 3}, {2, 0, 3, 1}, {3, 1, 2, 0}, {1, 3, 0, 2}};
    ASSERT_EQ(actual->width, orientation >= 5 ? height : width);
    ASSERT_EQ(actual->height, orientation >= 5 ? width : height);
    for (unsigned i = 0; i < 4; i++) {
        unsigned source = corners[orientation - 1][i];
        const uint8_t *expected = reference + ((source / 2) * (height - 1) * width +
                                               (source % 2) * (width - 1)) * 4;
        const uint8_t *pixel = actual->pixels + (i / 2) * (actual->height - 1) * actual->stride +
                                                (i % 2) * (actual->width - 1) * 4;
        EXPECT_EQ(memcmp(pixel, expected, 4), 0) << "corner " << i;
    }
}
TEST(ImageIO, AvifRotationsAndMirrorsPlacePixels) {
    ImageFixture fixture;
    // irot is counterclockwise; imir axis 0 flips vertically, axis 1 horizontally.
    const unsigned orientations[4][3] = {{1, 4, 2}, {8, 5, 7}, {3, 2, 4}, {6, 7, 5}};
    for (unsigned angle = 0; angle < 4; angle++) {
        for (unsigned mirror = 0; mirror < 3; mirror++) {
            SCOPED_TRACE(angle * 3 + mirror);
            avifImage *image = avifImageCreate(32, 24, 8, AVIF_PIXEL_FORMAT_YUV444);
            ASSERT_NE(image, nullptr);
            image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
            image->yuvRange = AVIF_RANGE_FULL;
            image->transformFlags = AVIF_TRANSFORM_IROT;
            image->irot.angle = (uint8_t)angle;
            if (mirror) {
                image->transformFlags |= AVIF_TRANSFORM_IMIR;
                image->imir.axis = (uint8_t)(mirror - 1);
            }
            avifRGBImage rgb;
            avifRGBImageSetDefaults(&rgb, image);
            rgb.format = AVIF_RGB_FORMAT_RGBA;
            rgb.pixels = fixture.pixels;
            rgb.rowBytes = (uint32_t)fixture.view.stride;
            ASSERT_EQ(avifImageRGBToYUV(image, &rgb), AVIF_RESULT_OK);
            avifEncoder *encoder = avifEncoderCreate();
            ASSERT_NE(encoder, nullptr);
            encoder->quality = encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
            encoder->speed = 10;
            avifRWData bytes = AVIF_DATA_EMPTY;
            ASSERT_EQ(avifEncoderWrite(encoder, image, &bytes), AVIF_RESULT_OK) << encoder->diag.error;
            ImageSource source = image_source_memory(bytes.data, bytes.size);
            ImageInfo info = {};
            ImageError error = {};
            ASSERT_EQ(image_probe(NULL, &source, &info, &error), IMAGE_OK) << error.message;
            EXPECT_EQ(info.orientation, orientations[angle][mirror]);
            ImageDecoded decoded = {};
            ASSERT_EQ(image_decode(NULL, &source, NULL, &decoded, &error), IMAGE_OK) << error.message;
            expect_oriented_corners(&decoded, fixture.pixels, 32, 24, orientations[angle][mirror]);
            image_decoded_dispose(&decoded);
            avifRWDataFree(&bytes);
            avifEncoderDestroy(encoder);
            avifImageDestroy(image);
        }
    }
}
TEST(ImageIO, ThumbnailBoundsAndStrategy) {
    ImageFixture fixture;
    for (ImageCodecFormat format : writable_formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded bytes = {};
        ImageError error = {};
        ASSERT_EQ(encode_fixture(&fixture, format, &bytes, &error), IMAGE_OK) << error.message;
        ImageSource source = image_source_memory(bytes.data, bytes.length);
        ImageDecodeOptions options;
        image_decode_options_init(&options);
        options.mode = IMAGE_DECODE_THUMBNAIL;
        options.target_width = 9;
        options.target_height = 9;
        ImageDecoded image = {};
        ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
        EXPECT_EQ(image.width, 9u);
        EXPECT_EQ(image.height, 6u);
        EXPECT_EQ(image.info.width, 32u);
        EXPECT_EQ(image.info.height, 24u);
        if (format == IMAGE_CODEC_PNG)
            EXPECT_EQ(image.strategy, IMAGE_STRATEGY_STREAMED_REDUCE);
        if (format == IMAGE_CODEC_JPEG || format == IMAGE_CODEC_WEBP)
            EXPECT_EQ(image.strategy, IMAGE_STRATEGY_NATIVE_SCALED);
        if (format == IMAGE_CODEC_AVIF || format == IMAGE_CODEC_BMP || format == IMAGE_CODEC_WEBP)
            EXPECT_TRUE(image.full_decode_fallback);
        image_decoded_dispose(&image);
        options.allow_full_decode = false;
        ImageStatus status = image_decode(NULL, &source, &options, &image, &error);
        if (format == IMAGE_CODEC_AVIF || format == IMAGE_CODEC_BMP || format == IMAGE_CODEC_WEBP)
            EXPECT_EQ(status, IMAGE_FAST_PATH_UNAVAILABLE) << error.message;
        else
            EXPECT_EQ(status, IMAGE_OK) << error.message;
        image_decoded_dispose(&image);
        image_free(bytes.data);
    }
}
TEST(ImageIO, NoThumbnailUpscale) {
    ImageFixture fixture;
    ImageEncoded bytes = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_PNG, &bytes, &error), IMAGE_OK);
    ImageSource source = image_source_memory(bytes.data, bytes.length);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_THUMBNAIL;
    options.target_width = options.target_height = 256;
    options.allow_full_decode = false;
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.width, 32u);
    EXPECT_EQ(image.height, 24u);
    image_decoded_dispose(&image);
    image_free(bytes.data);
}
TEST(ImageIO, ThumbnailAlphaIsPremultipliedDuringReduction) {
    uint8_t pixels[] = {255, 0, 0, 255, 0, 0, 255, 0};
    ImagePixelView view = {pixels, sizeof(pixels), 8, 2, 1, 4, false};
    ImageEncodeOptions encoding;
    image_encode_options_init(&encoding, IMAGE_CODEC_PNG);
    ImageEncoded bytes = {};
    ImageError error = {};
    ASSERT_EQ(image_encode_to_memory(NULL, &view, &encoding, &bytes, &error), IMAGE_OK);
    ImageSource source = image_source_memory(bytes.data, bytes.length);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_THUMBNAIL;
    options.target_width = options.target_height = 1;
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.pixels[0], 255);
    EXPECT_EQ(image.pixels[1], 0);
    EXPECT_EQ(image.pixels[2], 0);
    EXPECT_EQ(image.pixels[3], 128);
    image_decoded_dispose(&image);
    image_free(bytes.data);
}
struct ChunkedImage {
    const uint8_t *data;
    size_t length;
};
static size_t read_chunk(void *user, uint64_t offset, uint8_t *data, size_t size) {
    ChunkedImage *source = (ChunkedImage *)user;
    if (offset >= source->length)
        return 0;
    if (size > 7)
        size = 7;
    if (size > source->length - (size_t)offset)
        size = source->length - (size_t)offset;
    memcpy(data, source->data + (size_t)offset, size);
    return size;
}
TEST(ImageIO, ChunkedReadersShareMemoryBehavior) {
    ImageFixture fixture;
    for (ImageCodecFormat format : writable_formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded bytes = {};
        ImageError error = {};
        ASSERT_EQ(encode_fixture(&fixture, format, &bytes, &error), IMAGE_OK) << error.message;
        ChunkedImage reader = {bytes.data, bytes.length};
        ImageSource source = {NULL, bytes.length, read_chunk, &reader};
        ImageDecoded image = {};
        ASSERT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_OK) << error.message;
        EXPECT_EQ(image.width, 32u);
        EXPECT_EQ(image.height, 24u);
        image_decoded_dispose(&image);
        image_free(bytes.data);
    }
}
TEST(ImageIO, FileContentWinsOverExtension) {
    ImageFixture fixture;
    ImageError error = {};
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_AVIF);
    ASSERT_EQ(image_io_save_path(NULL, "temp/image-io-tests/content.png", &fixture.view, &options, &error),
              IMAGE_OK)
        << error.message;
    ImageInfo info = {};
    ASSERT_EQ(image_io_probe_path(NULL, "temp/image-io-tests/content.png", &info, &error), IMAGE_OK)
        << error.message;
    EXPECT_EQ(info.format, IMAGE_CODEC_AVIF);
    ImageDecoded image = {};
    ASSERT_EQ(image_io_decode_path(NULL, "temp/image-io-tests/content.png", NULL, &image, &error), IMAGE_OK)
        << error.message;
    image_decoded_dispose(&image);
    remove("temp/image-io-tests/content.png");
}
static size_t failing_write(void *, const uint8_t *, size_t) {
    return 0;
}
static int cancel_image(void *) {
    return 1;
}
TEST(ImageIO, UniformFailuresAndOutputOwnership) {
    ImageFixture fixture;
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageSink sink = {failing_write, NULL};
    ImageError error = {};
    EXPECT_EQ(image_encode(NULL, &fixture.view, &options, &sink, &error), IMAGE_IO_ERROR);
    EXPECT_STREQ(error.operation, "encode");
    EXPECT_EQ(error.format, IMAGE_CODEC_PNG);
    ImageContext context;
    image_context_init(&context);
    context.cancelled = cancel_image;
    ImageEncoded bytes = {};
    EXPECT_EQ(image_encode_to_memory(&context, &fixture.view, &options, &bytes, &error), IMAGE_CANCELLED);
    EXPECT_EQ(bytes.data, nullptr);
    image_context_init(&context);
    context.limits.max_encoded_bytes = 4;
    EXPECT_EQ(image_encode_to_memory(&context, &fixture.view, &options, &bytes, &error),
              IMAGE_LIMIT_EXCEEDED);
    EXPECT_EQ(bytes.data, nullptr);
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_PNG, &bytes, &error), IMAGE_OK);
    ImageSource source = image_source_memory(bytes.data, bytes.length);
    ImageDecoded image = {};
    image_context_init(&context);
    context.limits.max_source_pixels = 32;
    EXPECT_EQ(image_decode(&context, &source, NULL, &image, &error), IMAGE_LIMIT_EXCEEDED);
    EXPECT_EQ(image.pixels, nullptr);
    source.length = 12;
    EXPECT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_TRUNCATED_INPUT);
    EXPECT_EQ(image.pixels, nullptr);
    image_free(bytes.data);
    uint8_t unknown[] = {1, 2, 3, 4};
    source = image_source_memory(unknown, sizeof(unknown));
    EXPECT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_UNKNOWN_FORMAT);
    EXPECT_EQ(image_io_decode_path(NULL, "temp/image-io-tests/does-not-exist", NULL, &image, &error),
              IMAGE_IO_ERROR);
    EXPECT_STREQ(error.operation, "open");
}
TEST(ImageIO, CancelledSavePreservesDestination) {
    const char *path = "temp/image-io-tests/preserved.png";
    const char content[] = "original";
    ASSERT_EQ(write_binary_file(path, content, sizeof(content)), 0);
    ImageFixture fixture;
    ImageContext context;
    image_context_init(&context);
    context.cancelled = cancel_image;
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageError error = {};
    EXPECT_EQ(image_io_save_path(&context, path, &fixture.view, &options, &error), IMAGE_CANCELLED);
    char *bytes = NULL;
    size_t length = 0;
    ASSERT_TRUE(file_read_all(path, MEM_CAT_IMAGE, &bytes, &length));
    EXPECT_EQ(length, sizeof(content));
    EXPECT_EQ(memcmp(bytes, content, sizeof(content)), 0);
    image_free((uint8_t *)bytes);
    remove(path);
}
TEST(ImageIO, GifFirstFrameAndLegacyAnimationRemainAvailable) {
    const uint8_t gif[] = {'G', 'I',  'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 255,  0, 0, 0,   0,
                           0,   0x2c, 0,   0,   0,   0,   1, 0, 1, 0, 0,    2, 2, 0x44, 1, 0, 0x3b};
    ImageSource source = image_source_memory(gif, sizeof(gif));
    ImageDecoded image = {};
    ImageError error = {};
    ASSERT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.width, 1u);
    EXPECT_EQ(image.height, 1u);
    EXPECT_EQ(image.pixels[0], 255);
    EXPECT_EQ(image.pixels[3], 255);
    EXPECT_EQ(image_gif_frame_count_from_memory(gif, sizeof(gif)), 1);
    image_decoded_dispose(&image);
}
TEST(ImageIO, JpegAtLeastModeRetainsIntrinsicDimensions) {
    ImageFixture fixture;
    ImageEncoded bytes = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_JPEG, &bytes, &error), IMAGE_OK);
    ImageSource source = image_source_memory(bytes.data, bytes.length);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_AT_LEAST;
    options.target_width = 9;
    options.target_height = 7;
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_GE(image.width, 9u);
    EXPECT_GE(image.height, 7u);
    EXPECT_EQ(image.info.width, 32u);
    EXPECT_EQ(image.info.height, 24u);
    image_decoded_dispose(&image);
    image_free(bytes.data);
}

TEST(ImageIO, ExifOrientationUsesDisplayedThumbnailBounds) {
    ImageFixture fixture;
    ImageEncoded bytes = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_JPEG, &bytes, &error), IMAGE_OK);
    ImageSource raw_source = image_source_memory(bytes.data, bytes.length);
    ImageDecoded reference = {};
    ASSERT_EQ(image_decode(NULL, &raw_source, NULL, &reference, &error), IMAGE_OK);
    uint8_t tagged[8192] = {};
    ASSERT_LE(bytes.length + 36, sizeof(tagged));
    memcpy(tagged, bytes.data, 2);
    tagged[2] = 255;
    tagged[3] = 225;
    tagged[5] = 34;
    memcpy(tagged + 6, "Exif\0\0II", 8);
    write_le16(tagged + 14, 42);
    write_le32(tagged + 16, 8);
    write_le16(tagged + 20, 1);
    write_le16(tagged + 22, 0x112);
    write_le16(tagged + 24, 3);
    write_le32(tagged + 26, 1);
    memcpy(tagged + 38, bytes.data + 2, bytes.length - 2);
    for (unsigned orientation = 1; orientation <= 8; orientation++) {
        SCOPED_TRACE(orientation);
        write_le16(tagged + 30, (uint16_t)orientation);
        ImageSource source = image_source_memory(tagged, bytes.length + 36);
        ImageDecodeOptions options;
        image_decode_options_init(&options);
        options.mode = IMAGE_DECODE_THUMBNAIL;
        options.target_width = options.target_height = 9;
        ImageDecoded image = {};
        ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
        EXPECT_EQ(image.width, orientation >= 5 ? 6u : 9u);
        EXPECT_EQ(image.height, orientation >= 5 ? 9u : 6u);
        EXPECT_EQ(image.info.orientation, 1);
        image_decoded_dispose(&image);
        ASSERT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_OK) << error.message;
        expect_oriented_corners(&image, reference.pixels, reference.width, reference.height, orientation);
        image_decoded_dispose(&image);
    }
    image_decoded_dispose(&reference);
    image_free(bytes.data);
}

TEST(ImageIO, IcoLegacyDibMaskAndBounds) {
    uint8_t icon[70] = {};
    write_le16(icon + 2, 1);
    write_le16(icon + 4, 1);
    icon[6] = icon[7] = 1;
    write_le16(icon + 10, 1);
    write_le16(icon + 12, 32);
    write_le32(icon + 14, 48);
    write_le32(icon + 18, 22);
    write_le32(icon + 22, 40);
    write_le32(icon + 26, 1);
    write_le32(icon + 30, 2);
    write_le16(icon + 34, 1);
    write_le16(icon + 36, 32);
    icon[64] = 255;
    ImageSource source = image_source_memory(icon, sizeof(icon));
    ImageDecoded image = {};
    ImageError error = {};
    ASSERT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.pixels[0], 255);
    EXPECT_EQ(image.pixels[3], 255);
    image_decoded_dispose(&image);
    icon[66] = 0x80;
    ASSERT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.pixels[3], 0);
    image_decoded_dispose(&image);
    write_le32(icon + 18, UINT32_MAX);
    EXPECT_EQ(image_decode(NULL, &source, NULL, &image, &error), IMAGE_INVALID_DATA);
    EXPECT_EQ(image.pixels, nullptr);
}

TEST(ImageIO, DecoderFramesOwnPixelsAfterSessionClose) {
    ImageFixture fixture;
    ImageEncoded bytes = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_PNG, &bytes, &error), IMAGE_OK);
    ImageSource source = image_source_memory(bytes.data, bytes.length);
    ImageDecoder *decoder = nullptr;
    ASSERT_EQ(image_decoder_open(nullptr, &source, nullptr, &decoder, &error), IMAGE_OK);
    ImageDecoded image = {};
    ASSERT_EQ(image_decoder_read_frame(decoder, 0, &image, &error), IMAGE_OK);
    image_decoder_close(decoder);
    image_free(bytes.data);
    EXPECT_EQ(image.pixels[3], 255);
    EXPECT_EQ(image.width, 32u);
    image_decoded_dispose(&image);
}

TEST(ImageIO, PremultipliedPngEncodingDoesNotModifySource) {
    uint8_t premultiplied[] = {64, 32, 0, 128};
    uint8_t original[4];
    memcpy(original, premultiplied, 4);
    ImagePixelView pixels = {premultiplied, 4, 4, 1, 1, 4, true};
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageEncoded bytes = {};
    ImageError error = {};
    ASSERT_EQ(image_encode_to_memory(nullptr, &pixels, &options, &bytes, &error), IMAGE_OK) << error.message;
    ImageSource source = image_source_memory(bytes.data, bytes.length);
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(nullptr, &source, nullptr, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.pixels[0], 128);
    EXPECT_EQ(image.pixels[1], 64);
    EXPECT_EQ(image.pixels[3], 128);
    EXPECT_EQ(memcmp(original, premultiplied, 4), 0);
    image_decoded_dispose(&image);
    image_free(bytes.data);
}

static ImageStatus fixture_row(void *user, uint32_t y, uint8_t *row, size_t size) {
    ImageFixture *fixture = (ImageFixture *)user;
    memcpy(row, fixture->pixels + y * fixture->view.stride, size);
    return IMAGE_OK;
}
static ImageStatus failed_row(void *, uint32_t y, uint8_t *row, size_t size) {
    memset(row, 0, size);
    return y == 2 ? IMAGE_IO_ERROR : IMAGE_OK;
}
TEST(ImageIO, RowEncoderMatchesPixelsAndPreservesCallbackFailure) {
    ImageFixture fixture;
    ImageRowSource rows = {fixture_row, &fixture, 32, 24, 4, false};
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageEncoded encoded = {};
    ImageError error = {};
    ASSERT_EQ(image_encode_rows_to_memory(NULL, &rows, &options, &encoded, &error), IMAGE_OK)
        << error.message;
    ImageSource source = image_source_memory(encoded.data, encoded.length);
    ImageDecoded decoded = {};
    ASSERT_EQ(image_decode(NULL, &source, NULL, &decoded, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(memcmp(decoded.pixels, fixture.pixels, sizeof(fixture.pixels)), 0);
    image_decoded_dispose(&decoded);
    image_free(encoded.data);
    rows.read_row = failed_row;
    EXPECT_EQ(image_encode_rows_to_memory(NULL, &rows, &options, &encoded, &error), IMAGE_IO_ERROR);
    EXPECT_EQ(error.code, IMAGE_IO_ERROR);
    EXPECT_EQ(encoded.data, nullptr);
}

static bool write_tiff_page(TIFF *tiff, uint32_t width, uint32_t height, uint8_t red, bool tiled) {
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    uint8_t pixels[32 * 32 * 3] = {};
    for (size_t i = 0; i < sizeof(pixels); i += 3)
        pixels[i] = red;
    if (tiled) {
        TIFFSetField(tiff, TIFFTAG_TILEWIDTH, 16);
        TIFFSetField(tiff, TIFFTAG_TILELENGTH, 16);
        for (uint32_t y = 0; y < height; y += 16)
            for (uint32_t x = 0; x < width; x += 16)
                if (TIFFWriteTile(tiff, pixels, x, y, 0, 0) < 0)
                    return false;
    } else {
        TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 4);
        for (uint32_t y = 0; y < height; y++)
            if (TIFFWriteScanline(tiff, pixels, y, 0) < 0)
                return false;
    }
    return TIFFWriteDirectory(tiff) != 0;
}
TEST(ImageIO, TiffPagesTilesAndReducedSubIfds) {
    ImageFixture fixture;
    const char *pages = "temp/image-io-tests/pages.tiff";
    TIFF *tiff = TIFFOpen(pages, "w");
    ASSERT_NE(tiff, nullptr);
    ASSERT_TRUE(write_tiff_page(tiff, 32, 24, 50, true));
    ASSERT_TRUE(write_tiff_page(tiff, 16, 12, 200, false));
    TIFFClose(tiff);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_THUMBNAIL;
    options.target_width = options.target_height = 8;
    options.allow_full_decode = false;
    ImageDecoded image = {};
    ImageError error = {};
    ASSERT_EQ(image_io_decode_path(NULL, pages, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.pixels[0], 50); // a different document page must never become the preview.
    EXPECT_EQ(image.strategy, IMAGE_STRATEGY_STREAMED_REDUCE);
    image_decoded_dispose(&image);
    options.mode = IMAGE_DECODE_FULL;
    options.page_index = 1;
    ASSERT_EQ(image_io_decode_path(NULL, pages, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.pixels[0], 200);
    EXPECT_EQ(image.width, 16u);
    EXPECT_EQ(image.info.display_width, 16u);
    image_decoded_dispose(&image);
    options.page_index = 2;
    EXPECT_EQ(image_io_decode_path(NULL, pages, &options, &image, &error), IMAGE_INVALID_ARGUMENT);
    const char *pyramid = "temp/image-io-tests/pyramid.tiff";
    tiff = TIFFOpen(pyramid, "w");
    ASSERT_NE(tiff, nullptr);
    toff_t subifd = 0;
    TIFFSetField(tiff, TIFFTAG_SUBIFD, 1, &subifd);
    ASSERT_TRUE(write_tiff_page(tiff, 32, 24, 50, false));
    TIFFSetField(tiff, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
    ASSERT_TRUE(write_tiff_page(tiff, 16, 12, 50, false));
    TIFFClose(tiff);
    options.mode = IMAGE_DECODE_THUMBNAIL;
    options.page_index = 0;
    ASSERT_EQ(image_io_decode_path(NULL, pyramid, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.strategy, IMAGE_STRATEGY_SELECTED_RESOLUTION);
    EXPECT_EQ(image.representation_index, 1u);
    EXPECT_EQ(image.width, 8u);
    EXPECT_EQ(image.height, 6u);
    EXPECT_EQ(image.info.width, 32u);
    image_decoded_dispose(&image);
}
TEST(ImageIO, AvifIdentificationSkipsLargePaddingWithoutBufferingIt) {
    ImageFixture fixture;
    ImageEncoded encoded = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_AVIF, &encoded, &error), IMAGE_OK) << error.message;
    size_t padding = 8192;
    uint8_t *bytes = (uint8_t *)mem_alloc(encoded.length + padding, MEM_CAT_IMAGE);
    ASSERT_NE(bytes, nullptr);
    memset(bytes, 0, padding);
    write_be32(bytes, (uint32_t)padding);
    memcpy(bytes + 4, "free", 4);
    memcpy(bytes + padding, encoded.data, encoded.length);
    ImageSource source = image_source_memory(bytes, encoded.length + padding);
    ImageInfo info = {};
    // probing exercises the reader-based identifier; libavif's absolute item offsets are unchanged.
    EXPECT_NE(image_probe(NULL, &source, &info, &error), IMAGE_UNKNOWN_FORMAT);
    EXPECT_EQ(error.format, IMAGE_CODEC_AVIF);
    mem_free(bytes);
    image_free(encoded.data);
}
TEST(ImageIO, IcoThumbnailSelectsSufficientRepresentation) {
    ImageFixture fixture;
    ImageEncoded large = {}, small = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_PNG, &large, &error), IMAGE_OK);
    fixture.view.width = 16;
    fixture.view.height = 12;
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_PNG, &small, &error), IMAGE_OK);
    size_t size = 38 + large.length + small.length;
    uint8_t *bytes = (uint8_t *)mem_alloc(size, MEM_CAT_IMAGE);
    ASSERT_NE(bytes, nullptr);
    memset(bytes, 0, 38);
    write_le16(bytes + 2, 1);
    write_le16(bytes + 4, 2);
    const ImageEncoded *images[] = {&large, &small};
    size_t offset = 38;
    for (unsigned i = 0; i < 2; i++) {
        uint8_t *entry = bytes + 6 + i * 16;
        entry[0] = i ? 16 : 32;
        entry[1] = i ? 12 : 24;
        write_le16(entry + 4, 1);
        write_le16(entry + 6, 32);
        write_le32(entry + 8, (uint32_t)images[i]->length);
        write_le32(entry + 12, (uint32_t)offset);
        memcpy(bytes + offset, images[i]->data, images[i]->length);
        offset += images[i]->length;
    }
    ImageSource source = image_source_memory(bytes, size);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_THUMBNAIL;
    options.target_width = options.target_height = 8;
    options.allow_full_decode = false;
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.strategy, IMAGE_STRATEGY_SELECTED_RESOLUTION);
    EXPECT_EQ(image.representation_index, 1u);
    EXPECT_EQ(image.info.width, 32u);
    EXPECT_EQ(image.width, 8u);
    EXPECT_EQ(image.height, 6u);
    image_decoded_dispose(&image);
    mem_free(bytes);
    image_free(large.data);
    image_free(small.data);
}

TEST(ImageIO, AllocationFailuresReleaseOperationOwnedBuffers) {
    ASSERT_TRUE(memtrack_init(MEMTRACK_MODE_DEBUG));
    ImageFixture fixture;
    const ImageCodecFormat formats[] = {IMAGE_CODEC_PNG, IMAGE_CODEC_BMP, IMAGE_CODEC_TIFF};
    for (ImageCodecFormat format : formats) {
        SCOPED_TRACE(image_codec_capabilities(format)->name);
        ImageEncoded encoded = {};
        ImageError error = {};
        ASSERT_EQ(encode_fixture(&fixture, format, &encoded, &error), IMAGE_OK) << error.message;
        ImageSource source = image_source_memory(encoded.data, encoded.length);
        for (size_t failure = 0; failure < 40; failure++) {
            SCOPED_TRACE(failure);
            MemtrackStats before = {}, after = {};
            memtrack_get_stats(&before);
            ImageDecoded image = {};
            memtrack_fault_inject(failure);
            ImageStatus status = image_decode(NULL, &source, NULL, &image, &error);
            memtrack_fault_clear();
            EXPECT_TRUE(status == IMAGE_OK || status == IMAGE_OUT_OF_MEMORY) << error.message;
            if (status != IMAGE_OK)
                EXPECT_EQ(image.pixels, nullptr);
            image_decoded_dispose(&image);
            memtrack_get_stats(&after);
            EXPECT_EQ(before.current_count, after.current_count);
            EXPECT_EQ(before.current_bytes, after.current_bytes);
        }
        image_free(encoded.data);
    }
    memtrack_shutdown();
}
TEST(ImageIO, OpaqueLossyWebpSupportsStrictScaledDecode) {
    ImageFixture fixture;
    ImageEncodeOptions encoding;
    image_encode_options_init(&encoding, IMAGE_CODEC_WEBP);
    ImageEncoded encoded = {};
    ImageError error = {};
    ASSERT_EQ(image_encode_to_memory(NULL, &fixture.view, &encoding, &encoded, &error), IMAGE_OK);
    ImageSource source = image_source_memory(encoded.data, encoded.length);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_THUMBNAIL;
    options.target_width = options.target_height = 8;
    options.allow_full_decode = false;
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.strategy, IMAGE_STRATEGY_NATIVE_SCALED);
    EXPECT_FALSE(image.full_decode_fallback);
    image_decoded_dispose(&image);
    image_free(encoded.data);
}

TEST(ImageIO, PngAtLeastPreviewPreservesLegacyIntegerStep) {
    ImageFixture fixture;
    ImageEncoded encoded = {};
    ImageError error = {};
    ASSERT_EQ(encode_fixture(&fixture, IMAGE_CODEC_PNG, &encoded, &error), IMAGE_OK);
    ImageSource source = image_source_memory(encoded.data, encoded.length);
    ImageDecodeOptions options;
    image_decode_options_init(&options);
    options.mode = IMAGE_DECODE_AT_LEAST;
    options.target_width = options.target_height = 9;
    ImageDecoded image = {};
    ASSERT_EQ(image_decode(NULL, &source, &options, &image, &error), IMAGE_OK) << error.message;
    EXPECT_EQ(image.width, 16u);
    EXPECT_EQ(image.height, 12u);
    EXPECT_EQ(image.info.width, 32u);
    EXPECT_EQ(image.strategy, IMAGE_STRATEGY_STREAMED_REDUCE);
    image_decoded_dispose(&image);
    image_free(encoded.data);
}

TEST(ImageIO, LegacyWorkerSaveCannotReplaceAnExistingDestination) {
    ImageFixture fixture;
    const char *path = "temp/image-io-tests/exclusive.png";
    ImageEncodeOptions options;
    image_encode_options_init(&options, IMAGE_CODEC_PNG);
    ImageError error = {};
    ASSERT_EQ(image_io_save_path(NULL, path, &fixture.view, &options, &error), IMAGE_OK);
    int created = 99;
    uint8_t original_red = fixture.pixels[0];
    fixture.pixels[0] ^= 255;
    EXPECT_EQ(image_save_png_cancelable(path, fixture.pixels, 32, 24, 4, NULL, NULL, &created), 0);
    EXPECT_EQ(created, 0);
    ImageDecoded image = {};
    ASSERT_EQ(image_io_decode_path(NULL, path, NULL, &image, &error), IMAGE_OK);
    EXPECT_EQ(image.pixels[0], original_red);
    EXPECT_EQ(memcmp(image.pixels + 1, fixture.pixels + 1, sizeof(fixture.pixels) - 1), 0);
    image_decoded_dispose(&image);
    ASSERT_EQ(file_delete(path), 0);
    EXPECT_EQ(image_save_png_cancelable(path, fixture.pixels, 32, 24, 4, NULL, NULL, &created), 1);
    EXPECT_EQ(created, 1);
    EXPECT_EQ(file_delete(path), 0);
}
