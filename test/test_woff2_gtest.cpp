/**
 * WOFF2 (and WOFF1) font decompression unit tests.
 *
 * Verifies that the bundled libwoff2 sources correctly decode WOFF2 fonts
 * to valid TTF/OTF data, and that WOFF1 inflate also works.
 *
 * Test fonts sourced from WPT (Web Platform Tests) and KaTeX font bundles
 * already present in the repo under test/.
 */

#include <gtest/gtest.h>

// font module internals — decompression + format detection
#include "../lib/font/font_internal.h"

#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Helpers
// ============================================================================

// read an entire file into a malloc'd buffer (caller must free)
static uint8_t* read_file_bytes(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (nr != (size_t)sz) { free(buf); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

// ============================================================================
// Test fixture — provides Pool + Arena for decompression
// ============================================================================

class Woff2Test : public ::testing::Test {
protected:
    void SetUp() override {
        pool  = pool_create();
        arena = arena_create(pool, ARENA_MEDIUM_CHUNK_SIZE, ARENA_LARGE_CHUNK_SIZE);
    }

    void TearDown() override {
        if (arena) arena_destroy(arena);
        if (pool)  pool_destroy(pool);
    }

    Pool*  pool  = nullptr;
    Arena* arena = nullptr;
};

// ============================================================================
// Format Detection (magic bytes)
// ============================================================================

TEST_F(Woff2Test, DetectFormat_WOFF2_Magic) {
    // 'wOF2' = 0x774F4632
    const uint8_t wof2[] = { 0x77, 0x4F, 0x46, 0x32 };
    EXPECT_EQ(font_detect_format(wof2, sizeof(wof2)), FONT_FORMAT_WOFF2);
}

TEST_F(Woff2Test, DetectFormat_WOFF1_Magic) {
    // 'wOFF' = 0x774F4646
    const uint8_t woff[] = { 0x77, 0x4F, 0x46, 0x46 };
    EXPECT_EQ(font_detect_format(woff, sizeof(woff)), FONT_FORMAT_WOFF);
}

TEST_F(Woff2Test, DetectFormat_TTF_Magic) {
    const uint8_t ttf[] = { 0x00, 0x01, 0x00, 0x00 };
    EXPECT_EQ(font_detect_format(ttf, sizeof(ttf)), FONT_FORMAT_TTF);
}

TEST_F(Woff2Test, DetectFormat_OTF_Magic) {
    // 'OTTO'
    const uint8_t otf[] = { 0x4F, 0x54, 0x54, 0x4F };
    EXPECT_EQ(font_detect_format(otf, sizeof(otf)), FONT_FORMAT_OTF);
}

TEST_F(Woff2Test, DetectFormat_TTC_Magic) {
    // 'ttcf'
    const uint8_t ttc[] = { 0x74, 0x74, 0x63, 0x66 };
    EXPECT_EQ(font_detect_format(ttc, sizeof(ttc)), FONT_FORMAT_TTC);
}

TEST_F(Woff2Test, DetectFormat_Unknown) {
    const uint8_t bad[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    EXPECT_EQ(font_detect_format(bad, sizeof(bad)), FONT_FORMAT_UNKNOWN);
}

TEST_F(Woff2Test, DetectFormat_TooShort) {
    const uint8_t tiny[] = { 0x77, 0x4F };
    EXPECT_EQ(font_detect_format(tiny, sizeof(tiny)), FONT_FORMAT_UNKNOWN);
}

TEST_F(Woff2Test, DetectFormat_Null) {
    EXPECT_EQ(font_detect_format(NULL, 0), FONT_FORMAT_UNKNOWN);
}

// ============================================================================
// Format Detection (file extension)
// ============================================================================

TEST_F(Woff2Test, DetectFormatExt_WOFF2) {
    EXPECT_EQ(font_detect_format_ext("font.woff2"), FONT_FORMAT_WOFF2);
    EXPECT_EQ(font_detect_format_ext("/path/to/MyFont.WOFF2"), FONT_FORMAT_WOFF2);
}

TEST_F(Woff2Test, DetectFormatExt_WOFF) {
    EXPECT_EQ(font_detect_format_ext("font.woff"), FONT_FORMAT_WOFF);
}

TEST_F(Woff2Test, DetectFormatExt_TTF) {
    EXPECT_EQ(font_detect_format_ext("font.ttf"), FONT_FORMAT_TTF);
}

TEST_F(Woff2Test, DetectFormatExt_OTF) {
    EXPECT_EQ(font_detect_format_ext("font.otf"), FONT_FORMAT_OTF);
}

TEST_F(Woff2Test, DetectFormatExt_TTC) {
    EXPECT_EQ(font_detect_format_ext("font.ttc"), FONT_FORMAT_TTC);
}

TEST_F(Woff2Test, DetectFormatExt_Unknown) {
    EXPECT_EQ(font_detect_format_ext("font.svg"), FONT_FORMAT_UNKNOWN);
    EXPECT_EQ(font_detect_format_ext("font.txt"), FONT_FORMAT_UNKNOWN);
}

TEST_F(Woff2Test, DetectFormatExt_Null) {
    EXPECT_EQ(font_detect_format_ext(NULL), FONT_FORMAT_UNKNOWN);
}

// ============================================================================
// WOFF2 Decompression — real file
// ============================================================================

// WPT valid WOFF2 file (small, ~980 bytes)
static const char* WOFF2_WPT_VALID = "test/wpt/css/WOFF2/support/valid-001.woff2";
// KaTeX WOFF2 file (larger, ~26KB)
static const char* WOFF2_KATEX     = "test/latex/node_modules/katex/dist/fonts/KaTeX_Main-Regular.woff2";

TEST_F(Woff2Test, DecompressWoff2_WPT) {
    size_t len = 0;
    uint8_t* data = read_file_bytes(WOFF2_WPT_VALID, &len);
    if (!data) {
        GTEST_SKIP() << "WOFF2 test file not found: " << WOFF2_WPT_VALID;
    }

    // verify magic bytes identify as WOFF2
    EXPECT_EQ(font_detect_format(data, len), FONT_FORMAT_WOFF2);

    // decompress
    uint8_t* out = NULL;
    size_t out_len = 0;
    bool ok = font_decompress_woff2(arena, data, len, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_NE(out, nullptr);
    EXPECT_GT(out_len, (size_t)0);

    // decompressed output must be valid TTF or OTF
    if (ok && out && out_len >= 4) {
        FontFormat fmt = font_detect_format(out, out_len);
        EXPECT_TRUE(fmt == FONT_FORMAT_TTF || fmt == FONT_FORMAT_OTF || fmt == FONT_FORMAT_TTC)
            << "decompressed WOFF2 should yield TTF/OTF/TTC, got format " << (int)fmt;
    }

    free(data);
}

TEST_F(Woff2Test, DecompressWoff2_KaTeX) {
    size_t len = 0;
    uint8_t* data = read_file_bytes(WOFF2_KATEX, &len);
    if (!data) {
        GTEST_SKIP() << "WOFF2 test file not found: " << WOFF2_KATEX;
    }

    EXPECT_EQ(font_detect_format(data, len), FONT_FORMAT_WOFF2);

    uint8_t* out = NULL;
    size_t out_len = 0;
    bool ok = font_decompress_woff2(arena, data, len, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_NE(out, nullptr);
    EXPECT_GT(out_len, len) << "decompressed TTF should be larger than compressed WOFF2";

    // validate output is TTF/OTF
    if (ok && out && out_len >= 4) {
        FontFormat fmt = font_detect_format(out, out_len);
        EXPECT_TRUE(fmt == FONT_FORMAT_TTF || fmt == FONT_FORMAT_OTF)
            << "KaTeX WOFF2 should decompress to TTF or OTF, got " << (int)fmt;
    }

    free(data);
}

// ============================================================================
// WOFF2 Decompression — edge cases
// ============================================================================

TEST_F(Woff2Test, DecompressWoff2_NullArgs) {
    uint8_t dummy[4] = { 0x77, 0x4F, 0x46, 0x32 };
    uint8_t* out = NULL;
    size_t out_len = 0;

    EXPECT_FALSE(font_decompress_woff2(NULL, dummy, 4, &out, &out_len));
    EXPECT_FALSE(font_decompress_woff2(arena, NULL, 4, &out, &out_len));
    EXPECT_FALSE(font_decompress_woff2(arena, dummy, 4, NULL, &out_len));
    EXPECT_FALSE(font_decompress_woff2(arena, dummy, 4, &out, NULL));
}

TEST_F(Woff2Test, DecompressWoff2_TruncatedData) {
    // just the magic bytes, not a real WOFF2 — should fail gracefully
    uint8_t truncated[] = { 0x77, 0x4F, 0x46, 0x32, 0x00, 0x00, 0x00, 0x00 };
    uint8_t* out = NULL;
    size_t out_len = 0;
    EXPECT_FALSE(font_decompress_woff2(arena, truncated, sizeof(truncated), &out, &out_len));
}

// ============================================================================
// WOFF1 Decompression — real file
// ============================================================================

// WPT WOFF1 file (FreeSans, ~420KB)
static const char* WOFF1_FILE = "test/wpt/svg/import/woffs/FreeSans.woff";

TEST_F(Woff2Test, DecompressWoff1_FreeSans) {
    size_t len = 0;
    uint8_t* data = read_file_bytes(WOFF1_FILE, &len);
    if (!data) {
        GTEST_SKIP() << "WOFF1 test file not found: " << WOFF1_FILE;
    }

    EXPECT_EQ(font_detect_format(data, len), FONT_FORMAT_WOFF);

    uint8_t* out = NULL;
    size_t out_len = 0;
    bool ok = font_decompress_woff1(arena, data, len, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_NE(out, nullptr);
    EXPECT_GT(out_len, (size_t)0);

    // decompressed WOFF1 should be TTF or OTF
    if (ok && out && out_len >= 4) {
        FontFormat fmt = font_detect_format(out, out_len);
        EXPECT_TRUE(fmt == FONT_FORMAT_TTF || fmt == FONT_FORMAT_OTF)
            << "decompressed WOFF1 should yield TTF or OTF, got " << (int)fmt;
    }

    free(data);
}

TEST_F(Woff2Test, DecompressWoff1_NullArgs) {
    uint8_t dummy[4] = { 0x77, 0x4F, 0x46, 0x46 };
    uint8_t* out = NULL;
    size_t out_len = 0;

    EXPECT_FALSE(font_decompress_woff1(NULL, dummy, 4, &out, &out_len));
    EXPECT_FALSE(font_decompress_woff1(arena, NULL, 4, &out, &out_len));
    EXPECT_FALSE(font_decompress_woff1(arena, dummy, 4, NULL, &out_len));
    EXPECT_FALSE(font_decompress_woff1(arena, dummy, 4, &out, NULL));
}

// ============================================================================
// font_decompress_if_needed — unified API
// ============================================================================

TEST_F(Woff2Test, DecompressIfNeeded_WOFF2) {
    size_t len = 0;
    uint8_t* data = read_file_bytes(WOFF2_WPT_VALID, &len);
    if (!data) {
        GTEST_SKIP() << "WOFF2 test file not found: " << WOFF2_WPT_VALID;
    }

    const uint8_t* out = NULL;
    size_t out_len = 0;
    bool ok = font_decompress_if_needed(arena, data, len, FONT_FORMAT_WOFF2, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_NE(out, nullptr);
    EXPECT_GT(out_len, (size_t)0);

    // verify the output is a known raw font format
    if (ok && out && out_len >= 4) {
        FontFormat fmt = font_detect_format(out, out_len);
        EXPECT_NE(fmt, FONT_FORMAT_WOFF2) << "output should no longer be WOFF2";
        EXPECT_NE(fmt, FONT_FORMAT_UNKNOWN);
    }

    free(data);
}

TEST_F(Woff2Test, DecompressIfNeeded_WOFF1) {
    size_t len = 0;
    uint8_t* data = read_file_bytes(WOFF1_FILE, &len);
    if (!data) {
        GTEST_SKIP() << "WOFF1 test file not found: " << WOFF1_FILE;
    }

    const uint8_t* out = NULL;
    size_t out_len = 0;
    bool ok = font_decompress_if_needed(arena, data, len, FONT_FORMAT_WOFF, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_NE(out, nullptr);
    EXPECT_GT(out_len, (size_t)0);

    free(data);
}

TEST_F(Woff2Test, DecompressIfNeeded_TTF_Passthrough) {
    // for TTF/OTF/TTC, the function should pass through the original buffer
    uint8_t raw[] = { 0x00, 0x01, 0x00, 0x00, 0xAA, 0xBB, 0xCC, 0xDD };
    const uint8_t* out = NULL;
    size_t out_len = 0;

    bool ok = font_decompress_if_needed(arena, raw, sizeof(raw), FONT_FORMAT_TTF, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_EQ(out, raw) << "TTF passthrough should return the same pointer";
    EXPECT_EQ(out_len, sizeof(raw));
}

TEST_F(Woff2Test, DecompressIfNeeded_Unknown_Fails) {
    uint8_t raw[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const uint8_t* out = NULL;
    size_t out_len = 0;
    EXPECT_FALSE(font_decompress_if_needed(arena, raw, sizeof(raw), FONT_FORMAT_UNKNOWN, &out, &out_len));
}

// ============================================================================
// Consistency: detect format from file, then decompress
// ============================================================================

TEST_F(Woff2Test, DetectThenDecompress_WOFF2) {
    size_t len = 0;
    uint8_t* data = read_file_bytes(WOFF2_KATEX, &len);
    if (!data) {
        GTEST_SKIP() << "KaTeX WOFF2 font not found";
    }

    // detect from extension
    FontFormat ext_fmt = font_detect_format_ext(WOFF2_KATEX);
    EXPECT_EQ(ext_fmt, FONT_FORMAT_WOFF2);

    // detect from magic bytes
    FontFormat magic_fmt = font_detect_format(data, len);
    EXPECT_EQ(magic_fmt, FONT_FORMAT_WOFF2);

    // both should agree
    EXPECT_EQ(ext_fmt, magic_fmt);

    // decompress using the unified API
    const uint8_t* out = NULL;
    size_t out_len = 0;
    bool ok = font_decompress_if_needed(arena, data, len, magic_fmt, &out, &out_len);
    EXPECT_TRUE(ok);
    EXPECT_NE(out, nullptr);
    EXPECT_GT(out_len, (size_t)0);

    // decompressed must be TTF/OTF
    if (ok && out && out_len >= 4) {
        FontFormat result_fmt = font_detect_format(out, out_len);
        EXPECT_TRUE(result_fmt == FONT_FORMAT_TTF || result_fmt == FONT_FORMAT_OTF);
    }

    free(data);
}
