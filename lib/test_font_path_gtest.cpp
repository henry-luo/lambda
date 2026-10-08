#include <gtest/gtest.h>
#include "font/font_internal.h"
#include "../radiant/view.hpp"
#include <math.h>

#ifdef __APPLE__
// keep MacTypes' Rect separate from Radiant's geometry.
#define Rect MacOSRect
#include <CoreText/CoreText.h>
#undef Rect

TEST(FontMetricTest, RasterPreservesNativeGrayscaleCoverage) {
    const struct { const char* path; FontSlant slant; } faces[] = {
        {"lmd/package/latex/fonts/Serif/cmunrm.woff2", FONT_SLANT_NORMAL},
        {"lmd/package/latex/fonts/Serif/cmunti.woff2", FONT_SLANT_ITALIC},
        {"lmd/package/latex/fonts/Serif/cmunbi.woff2", FONT_SLANT_ITALIC},
        {nullptr, FONT_SLANT_ITALIC}, // the sample's system Times math fallback
    };
    for (float ratio : {1.0f, 2.0f}) {
        FontContextConfig config = {}; config.pixel_ratio = ratio;
        FontContext* context = font_context_create(&config);
        ASSERT_NE(context, nullptr);
        for (const auto& face : faces) {
            for (float size : {8.0f, 16.0f, 96.0f}) {
                SCOPED_TRACE(::testing::Message() << (face.path ? face.path : "Times")
                    << " size=" << size << " ratio=" << ratio);
                FontStyleDesc style = {};
                style.family = "Times"; style.size_px = size;
                style.weight = FONT_WEIGHT_NORMAL; style.slant = face.slant;
                FontHandle* font = face.path ? font_load_from_file(context, face.path, &style)
                    : font_resolve(context, &style);
                ASSERT_NE(font, nullptr);
                CTFontRef native = (CTFontRef)font->ct_raster_ref;
                ASSERT_NE(native, nullptr);
                for (UniChar character : {(UniChar)'e', (UniChar)'m', (UniChar)'x', (UniChar)0x03c0}) {
                    SCOPED_TRACE(::testing::Message() << "codepoint=" << character);
                    const GlyphBitmap* bitmap = font_render_glyph(font, character, GLYPH_RENDER_NORMAL);
                    ASSERT_NE(bitmap, nullptr);
                    ASSERT_EQ(bitmap->pixel_mode, GLYPH_PIXEL_GRAY);
                    ASSERT_GT(bitmap->width, 0); ASSERT_GT(bitmap->height, 0);

                    // compare with native black-on-white RGB text, independently of the mask conversion.
                    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
                    CGContextRef reference = CGBitmapContextCreate(nullptr, bitmap->width,
                        bitmap->height, 8, bitmap->width * 4, rgb,
                        kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
                    CGColorSpaceRelease(rgb);
                    ASSERT_NE(reference, nullptr);
                    CGContextSetAllowsAntialiasing(reference, true);
                    CGContextSetShouldAntialias(reference, true);
                    CGContextSetAllowsFontSmoothing(reference, true);
                    CGContextSetShouldSmoothFonts(reference, true);
                    CGContextSetGrayFillColor(reference, 1.0, 1.0);
                    CGContextFillRect(reference, CGRectMake(0, 0, bitmap->width, bitmap->height));
                    CGContextSetGrayFillColor(reference, 0.0, 1.0);
                    CGContextScaleCTM(reference, ratio, ratio);
                    CGPoint position = {-bitmap->bearing_x / ratio,
                        ((float)bitmap->height - bitmap->bearing_y) / ratio};
                    CGGlyph glyph = 0;
                    ASSERT_TRUE(CTFontGetGlyphsForCharacters(native, &character, &glyph, 1));
                    CTFontDrawGlyphs(native, &glyph, &position, 1, reference);
                    const uint8_t* coverage = (const uint8_t*)CGBitmapContextGetData(reference);
                    size_t pitch = CGBitmapContextGetBytesPerRow(reference);
                    unsigned actual_ink = 0, native_ink = 0, error = 0;
                    for (int y = 0; y < bitmap->height; y++) {
                        for (int x = 0; x < bitmap->width; x++) {
                            int actual = bitmap->buffer[y * bitmap->pitch + x];
                            int expected = 255 - coverage[y * pitch + x * 4 + 1];
                            actual_ink += actual; native_ink += expected;
                            error += abs(actual - expected);
                        }
                    }
                    EXPECT_GT(native_ink, 0u);
                    EXPECT_NEAR(actual_ink, native_ink, native_ink * 0.01);
                    EXPECT_LE(error, native_ink * 0.01);
                    CGContextRelease(reference);
                }
                font_handle_release(font);
            }
        }
        font_context_destroy(context);
    }
}

struct NativePathProbe { size_t commands; bool abort; };
static bool native_path_probe(void* context, FontPathCommand, const float*, int) {
    NativePathProbe* probe = (NativePathProbe*)context;
    probe->commands++;
    return !probe->abort;
}

TEST(FontPathTest, NativeUnicodeMappingDoesNotRequireParsedTables) {
    FontContextConfig config = {}; config.pixel_ratio = 1.0f;
    FontContext* context = font_context_create(&config);
    ASSERT_NE(context, nullptr);
    FontStyleDesc style = {}; style.family = "Ahem"; style.size_px = 20.0f; style.weight = FONT_WEIGHT_NORMAL;
    FontHandle* font = font_load_from_file(context, "test/layout/data/font/Ahem.ttf", &style);
    ASSERT_NE(font, nullptr);
    if (!font->ct_raster_ref) {
        font_handle_release(font); font_context_destroy(context);
        GTEST_SKIP() << "the selected font has no native outline backend";
    }
    Arena* arena = arena_create_default();
    ASSERT_NE(arena, nullptr);
    uint32_t glyph = font_get_glyph_index(font, 'A');
    ASSERT_NE(glyph, 0u);
    FontTables* tables = font->tables;
    font->tables = nullptr; // exercise the native-only handle contract inside its module
    NativePathProbe unicode = {}, indexed = {}, aborted = {0, true};
    EXPECT_TRUE(font_visit_glyph_path(font, 'A', native_path_probe, &unicode, arena));
    EXPECT_TRUE(font_visit_glyph_index_path(font, glyph, native_path_probe, &indexed, arena));
    EXPECT_FALSE(font_visit_glyph_path(font, 'A', native_path_probe, &aborted, arena));
    font->tables = tables;
    EXPECT_GT(unicode.commands, 0u);
    EXPECT_EQ(indexed.commands, unicode.commands);
    EXPECT_EQ(aborted.commands, 1u);
    arena_destroy(arena); font_handle_release(font); font_context_destroy(context);
}
#endif

TEST(FontMetricTest, CssUnitsRetainLogicalSizesAndCachedVerticalAdvances) {
    const char* paths[] = {
        "test/layout/data/font/LiberationSans-Regular.ttf",
        "test/layout/data/font/NotoSansKR-Subset.otf",
    };
    for (float ratio : {1.0f, 2.0f}) {
        FontContextConfig config = {}; config.pixel_ratio = ratio;
        FontContext* context = font_context_create(&config);
        ASSERT_NE(context, nullptr);
        FontStyleDesc style = {};
        style.family = "metric-test"; style.size_px = 10.0f; style.weight = FONT_WEIGHT_NORMAL;
        for (size_t face = 0; face < 2; face++) {
            FontHandle* font = font_load_from_file(context, paths[face], &style);
            EXPECT_NE(font, nullptr);
            if (!font) continue;
            const FontMetrics* metrics = font_get_metrics(font);
            EXPECT_NE(metrics, nullptr);
            float pixels = -1.0f;
            EXPECT_TRUE(css_font_metric_unit_px(font, &style, CSS_UNIT_CAP, 10.0f, false, &pixels));
            if (metrics) EXPECT_NEAR(pixels, metrics->cap_height > 0.0f
                ? metrics->cap_height : metrics->ascender, 0.0001f);
            EXPECT_TRUE(css_font_metric_unit_px(font, &style, CSS_UNIT_EX, 10.0f, false, &pixels));
            EXPECT_NEAR(pixels, font_get_x_height_ratio(font) * 10.0f, 0.0001f);
            for (uint32_t codepoint : {(uint32_t)'0', (uint32_t)0x6C34}) {
                if (font_get_glyph_index(font, codepoint) == 0) continue;
                GlyphInfo first = font_get_glyph(font, codepoint);
                GlyphInfo cached = font_get_glyph(font, codepoint);
                // both advance caches must preserve the orientation selected by the font backend.
                EXPECT_EQ(cached.id, first.id);
                EXPECT_FLOAT_EQ(cached.advance_x, first.advance_x);
                EXPECT_FLOAT_EQ(cached.advance_y, first.advance_y);
#ifdef __APPLE__
                GlyphInfo native = {};
                EXPECT_TRUE(font_rasterize_ct_metrics(font->ct_raster_ref, codepoint,
                    font->bitmap_scale, &native));
                EXPECT_GT(native.advance_y, 0.0f);
                EXPECT_FLOAT_EQ(first.advance_y, native.advance_y);
                if (face == 1) EXPECT_NEAR(native.advance_y, 10.0f, 0.0001f);
#endif
                CssUnit unit = codepoint == '0' ? CSS_UNIT_CH : CSS_UNIT_IC;
                for (bool upright : {false, true}) {
                    EXPECT_TRUE(css_font_metric_unit_px(font, &style, unit, 10.0f, upright, &pixels));
                    float advance = upright ? fabsf(first.advance_y) : first.advance_x;
                    float fallback = unit == CSS_UNIT_CH && !upright ? 5.0f : 10.0f;
                    EXPECT_NEAR(pixels, advance > 0.0f ? advance : fallback, 0.0001f);
                    EXPECT_TRUE(css_font_metric_unit_px(font, &style, unit, 0.0f, upright, &pixels));
                    EXPECT_FLOAT_EQ(pixels, 0.0f);
                }
                for (bool rendering : {false, true}) {
                    LoadedGlyph* loaded = font_load_glyph(font, &style, codepoint, rendering);
                    EXPECT_NE(loaded, nullptr);
                    if (!loaded) continue;
                    float physical_advance = loaded->advance_y;
                    EXPECT_NEAR(physical_advance, first.advance_y * ratio, 0.0001f);
                    loaded = font_load_glyph(font, &style, codepoint, rendering);
                    EXPECT_NE(loaded, nullptr);
                    if (loaded) EXPECT_FLOAT_EQ(loaded->advance_y, physical_advance);
                }
            }
            font_handle_release(font);
        }
        font_context_destroy(context);
    }
}
