#include <gtest/gtest.h>
#include "font/font_internal.h"

#ifdef __APPLE__
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
