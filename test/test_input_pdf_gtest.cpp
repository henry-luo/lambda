/**
 * @file test_input_pdf_gtest.cpp
 * @brief GTest coverage for the C/C++ PDF input parser and post-processing.
 *
 * Exercises the full pipeline: PDF bytes → parse_pdf() + pdf_postprocess() →
 * Map root carrying { version, objects, xref_table, trailer, statistics, pages }.
 *
 * Coverage:
 *   - Top-level PDF info Map shape (version / objects / trailer / statistics)
 *   - Indirect object table population (parser side)
 *   - Page-tree flattening into `pages: [Map]` (postprocess Pass 1)
 *   - Inheritable attribute propagation (resources / media_box on each page)
 *   - ToUnicode font walk (postprocess Pass 2) — runs without crashing on
 *     fonts that don't carry CMap streams
 *   - Multi-page documents with indirect /Pages → /Kids structure
 *
 * Test data sources:
 *   - Static fixtures under test/input/   (pre-existing PDFs that
 *     already exercise indirect-object refs and multi-page kid lists)
 *   - Static three-page PDF with a standard Helvetica resource,
 *     giving the test a fully deterministic page count + media-box assertion
 *     even if the static fixtures change.
 */

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/input/input-pdf-type1.hpp"
#include "../lambda/input/pdf_decompress.h"
#include "../lambda/io/mark_builder.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/strbuf.h"
#include "../lib/base64.h"
#include "../lib/image.h"


/* ══════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void ensure_temp_dir(void) {
    mkdir("temp", 0755);
}

// Static fixture paths (PDFs that already contain indirect objects).
static const char* PDF_FIXTURE_SIMPLE   = "test/input/test.pdf";
static const char* PDF_FIXTURE_ADVANCED = "test/input/advanced_test.pdf";
static const char* PDF_FIXTURE_INVOICE  = "test/input/invoice.pdf";

// Independent three-page fixture with distinct sheet geometry and font resources.
static const char* PDF_FIXTURE_THREEPAGE        = "test/input/pdf_three_pages.pdf";

/** Read a file fully into a malloc'd buffer. Returns NULL on failure.
 *  Sets *out_len. The buffer is null-terminated for safety, but the parser
 *  is given the explicit length so embedded NULs are fine. */
static char* read_binary_file(const char* path, size_t* out_len) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return nullptr;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return nullptr; }
    fseek(fp, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(fp); return nullptr; }
    size_t n = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/** Parse a PDF file into a fresh Input. Caller does NOT free — Input
 *  lifetime is managed by the singleton InputManager pool. */
static Input* parse_pdf_file(const char* path) {
    size_t len = 0;
    char* src = read_binary_file(path, &len);
    if (!src) {
        ADD_FAILURE() << "failed to read PDF fixture: " << path;
        return nullptr;
    }
    Url* url = url_parse(path);
    Input* input = InputManager::create_input(url);
    if (url) url_destroy(url);
    if (!input) {
        free(src);
        return nullptr;
    }
    parse_pdf(input, src, len);
    free(src);
    return input;
}

static MapReader find_indirect_object(ArrayReader objs, int64_t object_num) {
    for (int64_t i = 0; i < objs.length(); i++) {
        ItemReader entry = objs.get(i);
        if (!entry.isMap()) continue;
        MapReader em = entry.asMap();
        if (!em.has("object_num") || !em.has("content")) continue;
        ItemReader num = em.get("object_num");
        int64_t num_value = -1;
        if (num.isInt()) num_value = num.asInt();
        else if (num.isFloat()) num_value = (int64_t)num.asFloat();
        if (num_value != object_num) continue;
        ItemReader content = em.get("content");
        if (!content.isMap()) return MapReader();
        return content.asMap();
    }
    return MapReader();
}

static Input* parse_image_fixture(const char* dictionary, const uint8_t* data, size_t length, const char* extra = "") {
    StrBuf* source = strbuf_new();
    if (!source) return nullptr;
    strbuf_append_str(source, "%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 40 40] >>\nendobj\n");
    strbuf_append_format(source, "4 0 obj\n<< /Type /XObject /Subtype /Image %s /Length %zu >>\nstream\n", dictionary, length);
    strbuf_append_str_n(source, (const char*)data, length);
    strbuf_append_str(source, "\nendstream\nendobj\n6 0 obj\n[/Indexed /DeviceCMYK 1 <000000ff00ffff00>]\nendobj\n");
    strbuf_append_str(source, extra);
    strbuf_append_str(source, "trailer\n<< /Root 1 0 R /Size 7 >>\nstartxref\n0\n%%EOF\n");
    Input* input = InputManager::create_input(nullptr);
    if (input) parse_pdf(input, source->str, source->length);
    strbuf_free(source); return input;
}

static unsigned char* image_fixture_pixels(Input* input, int* width, int* height) {
    if (!input) return nullptr;
    MapReader root = MapReader::fromItem(input->root);
    MapReader stream = find_indirect_object(root.get("objects").asArray(), 4);
    String* uri = stream.get("data_uri").asString();
    if (!uri) return nullptr;
    const char* encoded = strchr(uri->chars, ',');
    if (!encoded) return nullptr;
    size_t length = 0; uint8_t* png = base64_decode(encoded + 1, 0, &length);
    if (!png) return nullptr;
    int channels = 0;
    unsigned char* pixels = image_load_from_memory(png, length, width, height, &channels);
    mem_free(png); return pixels;
}

/* ══════════════════════════════════════════════════════════════════════
 * Fixture
 * ══════════════════════════════════════════════════════════════════════ */

class InputPdfTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        log_init(NULL);
        ensure_temp_dir();
    }

};

/* ══════════════════════════════════════════════════════════════════════
 * §1 Top-level Map shape
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputPdfTest, ParseSimpleProducesMapRoot) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    EXPECT_NE(input->root.item, ITEM_NULL);
    EXPECT_NE(input->root.item, ITEM_ERROR);

    MapReader root = MapReader::fromItem(input->root);
    EXPECT_TRUE(root.isValid());
}

TEST_F(InputPdfTest, RootCarriesVersionField) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ASSERT_TRUE(root.has("version"));
    ItemReader v = root.get("version");
    EXPECT_TRUE(v.isString());
    const char* version = v.cstring();
    ASSERT_NE(version, nullptr);
    // PDF version strings look like "1.4", "1.7", "2.0", etc.
    EXPECT_GT(strlen(version), 0u);
}

TEST_F(InputPdfTest, RootCarriesObjectsArray) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ASSERT_TRUE(root.has("objects"));
    ItemReader objs_it = root.get("objects");
    EXPECT_TRUE(objs_it.isArray());
    ArrayReader objs = objs_it.asArray();
    // Even the simplest PDF needs Catalog + Pages + Page = 3 objects min.
    EXPECT_GE(objs.length(), 3);
}

TEST_F(InputPdfTest, RootCarriesTrailer) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    EXPECT_TRUE(root.has("trailer"));
}

TEST_F(InputPdfTest, RootCarriesStatistics) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    EXPECT_TRUE(root.has("statistics"));
    ItemReader stats = root.get("statistics");
    EXPECT_TRUE(stats.isMap());
}

/* ══════════════════════════════════════════════════════════════════════
 * §2 Indirect object table
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputPdfTest, ObjectsAreWrappedAsIndirectObjects) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader objs = root.get("objects").asArray();
    ASSERT_GT(objs.length(), 0);

    // Each object entry should be a Map carrying type=indirect_object,
    // object_num and content fields.
    int indirect_count = 0;
    for (int64_t i = 0; i < objs.length(); i++) {
        ItemReader entry = objs.get(i);
        if (!entry.isMap()) continue;
        MapReader em = entry.asMap();
        if (!em.has("type")) continue;
        const char* ty = em.get("type").cstring();
        if (ty && strcmp(ty, "indirect_object") == 0) {
            EXPECT_TRUE(em.has("object_num"));
            EXPECT_TRUE(em.has("content"));
            indirect_count++;
        }
    }
    EXPECT_GE(indirect_count, 1);
}

TEST_F(InputPdfTest, HexStringPairsCanSpanWhitespace) {
    const char source[] = "%PDF-1.7\n1 0 obj\n<< /Label <4 1 4 2> >>\nendobj\ntrailer\n<< /Size 2 >>\n%%EOF\n";
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    parse_pdf(input, source, sizeof(source) - 1);
    MapReader root = MapReader::fromItem(input->root);
    MapReader object = find_indirect_object(root.get("objects").asArray(), 1);
    String* value = object.get("Label").asString();
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->len, 2u);
    EXPECT_EQ(value->chars[0], 'A');
    EXPECT_EQ(value->chars[1], 'B');
}

TEST_F(InputPdfTest, LinearizedTrailersAndXrefStreamFootersKeepLaterObjects) {
    const char* prefixes[] = {
        "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Root 1 0 R /Size 7 >>\nstartxref\n0\n%%EOF\n",
        "7 0 obj\n<< /Type /XRef /Root 1 0 R /Length 0 >>\nstream\n\nendstream\nendobj\nstartxref\n0\n%%EOF\n"
    };
    for (const char* prefix : prefixes) {
        StrBuf* source = strbuf_new(); ASSERT_NE(source, nullptr);
        strbuf_append_str(source, "%PDF-1.7\n8 0 obj\n<< /Linearized 1 >>\nendobj\n");
        strbuf_append_str(source, prefix);
        strbuf_append_str(source, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
            "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
            "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 40 40] >>\nendobj\n"
            "startxref\n0\n%%EOF\n");
        Input* input = InputManager::create_input(nullptr); ASSERT_NE(input, nullptr);
        parse_pdf(input, source->str, source->length); strbuf_free(source);
        MapReader root = MapReader::fromItem(input->root);
        EXPECT_EQ(root.get("pages").asArray().length(), 1);
        EXPECT_STREQ(find_indirect_object(root.get("objects").asArray(), 1).get("Type").asString()->chars, "Catalog");
    }
}

TEST_F(InputPdfTest, StreamLengthKeepsBinaryNewlinesAndEmbeddedEndstreamText) {
    const uint8_t bytes[] = {'e','n','d','s','t','r','e','a','m','\r','\n'};
    Input* input = parse_image_fixture("/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceRGB", bytes, sizeof(bytes));
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    String* stored = find_indirect_object(root.get("objects").asArray(), 4).get("data").asString();
    ASSERT_NE(stored, nullptr); ASSERT_EQ(stored->len, sizeof(bytes));
    EXPECT_EQ(memcmp(stored->chars, bytes, sizeof(bytes)), 0);
}

TEST_F(InputPdfTest, ImagePackedRowsResolveIndirectCmykPalettes) {
    const uint8_t bytes[] = {0x80, 0x40};
    Input* input = parse_image_fixture("/Width 2 /Height 2 /BitsPerComponent 1 /ColorSpace 6 0 R", bytes, sizeof(bytes));
    int width = 0, height = 0; unsigned char* pixels = image_fixture_pixels(input, &width, &height);
    ASSERT_NE(pixels, nullptr); EXPECT_EQ(width, 2); EXPECT_EQ(height, 2);
    const uint8_t expected[] = {255,0,0,255, 0,0,0,255, 0,0,0,255, 255,0,0,255};
    EXPECT_EQ(memcmp(pixels, expected, sizeof(expected)), 0); image_free(pixels);
}

TEST_F(InputPdfTest, ImageSixteenBitRgbAndInvertedStencilSamplesKeepTheirMeaning) {
    const uint8_t rgb[] = {0xff, 0xff, 0x80, 0, 0, 0};
    Input* input = parse_image_fixture("/Width 1 /Height 1 /BitsPerComponent 16 /ColorSpace /DeviceRGB", rgb, sizeof(rgb));
    int width = 0, height = 0; unsigned char* pixels = image_fixture_pixels(input, &width, &height);
    ASSERT_NE(pixels, nullptr);
    const uint8_t expected[] = {255,128,0,255};
    EXPECT_EQ(memcmp(pixels, expected, sizeof(expected)), 0); image_free(pixels);
    const uint8_t mask[] = {0x80};
    input = parse_image_fixture("/Width 2 /Height 1 /ImageMask true /Decode [1 0]", mask, sizeof(mask));
    pixels = image_fixture_pixels(input, &width, &height); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(pixels[3], 255); EXPECT_EQ(pixels[7], 0); image_free(pixels);
}

TEST_F(InputPdfTest, ImagePredictorsKeepSampleCarryRowPaddingAndPreviousRowPixels) {
    const uint8_t tiff16[] = {0,255, 0,1, 1,35, 255,255};
    const uint8_t tiff16_expected[] = {0,255, 1,0, 1,35, 1,34};
    const uint8_t packed[] = {0x1f,0x2a, 0x1f,0x2b};
    const uint8_t packed_expected[] = {0x10,0x2a, 0x10,0x2b};
    const uint8_t png[] = {4,50,50, 4,211,0};
    const uint8_t png_expected[] = {50,100,5,50};
    const struct { int predictor, bits, columns; const uint8_t* encoded; size_t length;
        const uint8_t* expected; size_t expected_length; } cases[] = {
        {2,16,2,tiff16,sizeof(tiff16),tiff16_expected,sizeof(tiff16_expected)},
        {2,4,3,packed,sizeof(packed),packed_expected,sizeof(packed_expected)},
        {15,8,2,png,sizeof(png),png_expected,sizeof(png_expected)}
    };
    for (const auto& test : cases) {
        PDFDecodeParams params; pdf_decode_params_init(&params);
        params.predictor = test.predictor; params.bits = test.bits; params.columns = test.columns;
        size_t size = 0;
        char* decoded = apply_predictor((const char*)test.encoded, test.length, &size, &params);
        ASSERT_NE(decoded, nullptr); ASSERT_EQ(size, test.expected_length);
        EXPECT_EQ(memcmp(decoded, test.expected, size), 0); mem_free(decoded);
    }
}

TEST_F(InputPdfTest, ImageDeviceNTintsUseTheAlternateColourSpaceCalculator) {
    const uint8_t samples[] = {0, 255};
    Input* input = parse_image_fixture("/Width 2 /Height 1 /BitsPerComponent 8 "
        "/ColorSpace [/DeviceN [/Black] /DeviceCMYK << /FunctionType 2 /N 1 /C0 [0 0 0 0] /C1 [0 0 0 1] >>]",
        samples, sizeof(samples));
    int width = 0, height = 0; unsigned char* pixels = image_fixture_pixels(input, &width, &height);
    ASSERT_NE(pixels, nullptr);
    const uint8_t expected[] = {255,255,255,255, 0,0,0,255};
    EXPECT_EQ(memcmp(pixels, expected, sizeof(expected)), 0); image_free(pixels);
    const char program[] = "{ 0 0 0 4 -1 roll }";
    StrBuf* function = strbuf_new(); ASSERT_NE(function, nullptr);
    strbuf_append_format(function, "5 0 obj\n<< /FunctionType 4 /Domain [0 1] /Range [0 1 0 1 0 1 0 1] /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
        sizeof(program) - 1, program);
    input = parse_image_fixture("/Width 2 /Height 1 /BitsPerComponent 8 /ColorSpace [/DeviceN [/Black] /DeviceCMYK 5 0 R]",
        samples, sizeof(samples), function->str);
    strbuf_free(function);
    pixels = image_fixture_pixels(input, &width, &height); ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(memcmp(pixels, expected, sizeof(expected)), 0); image_free(pixels);
}

TEST_F(InputPdfTest, ImageFiveInkTintsKeepLabUnitsAndTheReferenceWhite) {
    const uint8_t samples[] = {0,128,128,0,0, 255,128,128,0,0,
        128,128,128,0,0, 138,209,198,0,0};
    const char program[] = "{ pop pop 3 -1 roll 100 mul 3 -1 roll 255 mul -128 add "
        "3 -1 roll 255 mul -128 add }";
    StrBuf* function = strbuf_new(); ASSERT_NE(function, nullptr);
    strbuf_append_format(function, "5 0 obj\n<< /FunctionType 4 /Domain [0 1 0 1 0 1 0 1 0 1] "
        "/Range [0 100 -128 127 -128 127] /Length %zu >>\nstream\n%s\nendstream\nendobj\n",
        sizeof(program) - 1, program);
    Input* input = parse_image_fixture("/Width 4 /Height 1 /BitsPerComponent 8 "
        "/ColorSpace [/DeviceN [/A /B /C /D /E] [/Lab << /WhitePoint [0.9642 1 0.8249] >>] 5 0 R]",
        samples, sizeof(samples), function->str);
    strbuf_free(function);
    int width = 0, height = 0; unsigned char* pixels = image_fixture_pixels(input, &width, &height);
    ASSERT_NE(pixels, nullptr); ASSERT_EQ(width, 4); ASSERT_EQ(height, 1);
    for (int c = 0; c < 3; c++) {
        EXPECT_EQ(pixels[c], 0); EXPECT_EQ(pixels[4 + c], 255);
        EXPECT_NEAR(pixels[8 + c], 119, 1);
    }
    // this D50 Lab sample is near the sRGB red primary, so chroma must survive the tint transform.
    EXPECT_NEAR(pixels[12], 255, 2); EXPECT_NEAR(pixels[13], 0, 2); EXPECT_NEAR(pixels[14], 0, 2);
    image_free(pixels);
}

TEST_F(InputPdfTest, AdvancedFixtureHasManyIndirectObjects) {
    Input* input = parse_pdf_file(PDF_FIXTURE_ADVANCED);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader objs = root.get("objects").asArray();
    // advanced_test.pdf has Catalog + Pages + 2× Page + fonts + content
    // streams = 18 indirect objects in the captured trace.
    EXPECT_GE(objs.length(), 10);
}

TEST_F(InputPdfTest, ObjectStreamsExpandCompressedObjects) {
    Input* input = parse_pdf_file(PDF_FIXTURE_INVOICE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ASSERT_TRUE(root.has("objects"));
    ArrayReader objs = root.get("objects").asArray();

    MapReader outlines = find_indirect_object(objs, 4);
    ASSERT_TRUE(outlines.isValid()) << "object stream outline object 4 must be materialized";
    ASSERT_TRUE(outlines.has("Type"));
    EXPECT_STREQ(outlines.get("Type").cstring(), "Outlines");
    ASSERT_TRUE(outlines.has("First"));
    ASSERT_TRUE(outlines.get("First").isMap());
    EXPECT_TRUE(outlines.has("Last"));

    MapReader first_outline = find_indirect_object(objs, 6);
    ASSERT_TRUE(first_outline.isValid()) << "object stream outline object 6 must be materialized";
    ASSERT_TRUE(first_outline.has("Title"));
    EXPECT_STREQ(first_outline.get("Title").cstring(), "Invoice");
    ASSERT_TRUE(first_outline.has("Dest"));
    EXPECT_TRUE(first_outline.get("Dest").isArray());

    MapReader annot = find_indirect_object(objs, 10);
    ASSERT_TRUE(annot.isValid()) << "object stream annotation object 10 must be materialized";
    ASSERT_TRUE(annot.has("Type"));
    EXPECT_STREQ(annot.get("Type").cstring(), "Annot");
    ASSERT_TRUE(annot.has("Subtype"));
    EXPECT_STREQ(annot.get("Subtype").cstring(), "Link");
    ASSERT_TRUE(annot.has("A"));
    ASSERT_TRUE(annot.get("A").isMap());
    MapReader action = annot.get("A").asMap();
    ASSERT_TRUE(action.has("URI"));
    EXPECT_STREQ(action.get("URI").cstring(), "http://www.princexml.com/");
}

/* ══════════════════════════════════════════════════════════════════════
 * §3 Page-tree flattening (postprocess Pass 1)
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputPdfTest, PostprocessAttachesPagesArray) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ASSERT_TRUE(root.has("pages")) << "pdf_postprocess must attach `pages`";
    ItemReader pages_it = root.get("pages");
    EXPECT_TRUE(pages_it.isArray());
}

TEST_F(InputPdfTest, SinglePageFixtureFlattensToOnePage) {
    Input* input = parse_pdf_file(PDF_FIXTURE_SIMPLE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    EXPECT_EQ(pages.length(), 1);
}

TEST_F(InputPdfTest, MultiPageFixtureFlattensToTwoPages) {
    Input* input = parse_pdf_file(PDF_FIXTURE_ADVANCED);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    EXPECT_EQ(pages.length(), 2);
}

TEST_F(InputPdfTest, GeneratedPdfFlattensToThreePages) {
    Input* input = parse_pdf_file(PDF_FIXTURE_THREEPAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ASSERT_TRUE(root.has("pages"));
    ArrayReader pages = root.get("pages").asArray();
    EXPECT_EQ(pages.length(), 3) << "the fixture contains 3 /Page kids; flatten "
                                    "should expose all of them";
}

TEST_F(InputPdfTest, FlattenedPageCarriesType) {
    Input* input = parse_pdf_file(PDF_FIXTURE_THREEPAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    ASSERT_GT(pages.length(), 0);
    MapReader page0 = pages.get(0).asMap();
    ASSERT_TRUE(page0.isValid());
    EXPECT_TRUE(page0.has("type"));
    const char* ty = page0.get("type").cstring();
    ASSERT_NE(ty, nullptr);
    EXPECT_STREQ(ty, "page");
}

static Input* parse_page_label_fixture(const char* labels, const char* nodes = "") {
    StrBuf* pdf = strbuf_new(); if (!pdf) return nullptr;
    strbuf_append_format(pdf, "%%PDF-1.7\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R /PageLabels %s >>\nendobj\n", labels);
    strbuf_append_str(pdf, "2 0 obj\n<< /Type /Pages /Count 6 /Kids [3 0 R 4 0 R 5 0 R 6 0 R 7 0 R 8 0 R] >>\nendobj\n");
    for (int i = 3; i <= 8; i++) strbuf_append_format(pdf,
        "%d 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 60] >>\nendobj\n", i);
    strbuf_append_str(pdf, nodes);
    strbuf_append_str(pdf, "trailer\n<< /Root 1 0 R /Size 30 >>\n%%EOF\n");
    Input* input = InputManager::create_input(nullptr);
    if (input) parse_pdf(input, pdf->str, pdf->length);
    strbuf_free(pdf); return input;
}

TEST_F(InputPdfTest, PageLabelNumberTreesResolvePrefixesStylesAndUnicodeWithoutChangingOrdinals) {
    Input* input = parse_page_label_fixture("<< /Kids [20 0 R 21 0 R] >>",
        "20 0 obj\n<< /Limits [0 2] /Nums [0 << /S /r /St 4 >> 2 << /S /D /P (Part\\050x\\051\\040) /St 8 >>] >>\nendobj\n"
        "21 0 obj\n<< /Limits [4 5] /Nums [4 << /S /A /St 27 >> 5 << /P <FEFF4E2DD83DDE00> >>] >>\nendobj\n");
    ASSERT_NE(input, nullptr); MapReader root = MapReader::fromItem(input->root);
    EXPECT_FALSE(root.has("page_labels_error")); ArrayReader pages = root.get("pages").asArray(); ASSERT_EQ(pages.length(), 6);
    const char* expected[] = {"iv", "v", "Part(x) 8", "Part(x) 9", "AA", "中😀"};
    for (int i = 0; i < 6; i++) EXPECT_STREQ(pages.get(i).asMap().get("label").cstring(), expected[i]);
    input = parse_page_label_fixture("<< /Nums [0 << /S /R /St 3999 >> 2 << /S /a /St 52 >> 4 << /P <8093A0> >> 5 << >>] >>");
    ASSERT_NE(input, nullptr); root = MapReader::fromItem(input->root); EXPECT_FALSE(root.has("page_labels_error"));
    pages = root.get("pages").asArray();
    const char* more[] = {"MMMCMXCIX", "MMMM", "zz", "aaa", "•ﬁ€", ""};
    for (int i = 0; i < 6; i++) EXPECT_STREQ(pages.get(i).asMap().get("label").cstring(), more[i]);
}

TEST_F(InputPdfTest, InvalidPageLabelTreesDiagnoseBeforePublishingAnyLabelSet) {
    struct Invalid { const char* labels; const char* nodes; const char* reason; };
    const Invalid cases[] = {
        {"<< /Nums [1 << /S /D >>] >>", "", "index zero"},
        {"<< /Nums [0 << >> 0 << >>] >>", "", "ordered, unique"},
        {"<< /Nums [0 << >> 6 << >>] >>", "", "within the page"},
        {"<< /Nums [0 << >> 1] >>", "", "key/dictionary pairs"},
        {"<< /Nums [0 << /S /bad >>] >>", "", "style"},
        {"<< /Nums [0 << /St 1.5 >>] >>", "", "positive integer"},
        {"<< /Nums [0 << /St 0 >>] >>", "", "positive integer"},
        {"<< /Nums [0 << /P <FEFFD800> >>] >>", "", "encoding"},
        {"<< /Nums [0 << /P <7F> >>] >>", "", "encoding"},
        {"<< /Nums [0 << /S /A /St 2147483647 >>] >>", "", "byte budget"},
        {"<< /Kids [20 0 R] >>", "20 0 obj\n<< /Kids [20 0 R] /Limits [0 0] >>\nendobj\n", "cycle"},
        {"<< /Kids [20 0 R] >>", "20 0 obj\n<< /Nums [0 << >>] /Limits [0 1] >>\nendobj\n", "Limits"},
        {"<< /Kids [20 0 R] >>", "20 0 obj\n<< /Nums [0 << >>] >>\nendobj\n", "Limits"}
    };
    for (const Invalid& item : cases) {
        SCOPED_TRACE(item.labels); Input* input = parse_page_label_fixture(item.labels, item.nodes); ASSERT_NE(input, nullptr);
        MapReader root = MapReader::fromItem(input->root); const char* error = root.get("page_labels_error").cstring();
        ASSERT_NE(error, nullptr); EXPECT_NE(strstr(error, item.reason), nullptr) << error;
        ArrayReader pages = root.get("pages").asArray(); ASSERT_EQ(pages.length(), 6);
        for (int i = 0; i < 6; i++) EXPECT_FALSE(pages.get(i).asMap().has("label"));
    }
}

TEST_F(InputPdfTest, FlattenedPageInheritsMediaBox) {
    Input* input = parse_pdf_file(PDF_FIXTURE_THREEPAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    ASSERT_EQ(pages.length(), 3);
    // the fixture sets MediaBox on each /Page directly, not inherited from /Pages,
    // but either way the flattener must surface it on each entry.
    for (int64_t i = 0; i < pages.length(); i++) {
        MapReader p = pages.get(i).asMap();
        EXPECT_TRUE(p.has("media_box"))
            << "page " << i << " missing media_box after flatten";
    }
}

TEST_F(InputPdfTest, FlattenedPageInheritsResources) {
    Input* input = parse_pdf_file(PDF_FIXTURE_THREEPAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    ASSERT_GT(pages.length(), 0);
    int with_resources = 0;
    for (int64_t i = 0; i < pages.length(); i++) {
        MapReader p = pages.get(i).asMap();
        if (p.has("resources")) with_resources++;
    }
    // Each generated page references Helvetica → its Resources dict carries
    // a /Font entry. The flattener should preserve that on every page.
    EXPECT_EQ(with_resources, pages.length());
}

TEST_F(InputPdfTest, FlattenedPageRetainsOriginalDict) {
    Input* input = parse_pdf_file(PDF_FIXTURE_THREEPAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    ASSERT_GT(pages.length(), 0);
    MapReader page0 = pages.get(0).asMap();
    EXPECT_TRUE(page0.has("dict")) << "flatten should keep a back-pointer "
                                      "to the original /Page dict";
}

/* ══════════════════════════════════════════════════════════════════════
 * §4 ToUnicode font walk (postprocess Pass 2)
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputPdfTest, ToUnicodeWalkRunsWithoutCrash) {
    // The advanced fixture has 10 fonts; none carry a usable CMap stream
    // (they're Base14 fonts using Identity-H), so we don't expect any
    // `to_unicode` attachments — but the pass must not crash and must
    // log its tally.
    Input* input = parse_pdf_file(PDF_FIXTURE_ADVANCED);
    ASSERT_NE(input, nullptr);
    // Reaching this point with no crash and a populated `pages` array is
    // sufficient; package-level PDF tests cover text decoding behavior.
    MapReader root = MapReader::fromItem(input->root);
    EXPECT_TRUE(root.has("pages"));
}

TEST_F(InputPdfTest, FontsAreReachableViaIndirectObjects) {
    Input* input = parse_pdf_file(PDF_FIXTURE_ADVANCED);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader objs = root.get("objects").asArray();

    int font_objects = 0;
    for (int64_t i = 0; i < objs.length(); i++) {
        ItemReader entry = objs.get(i);
        if (!entry.isMap()) continue;
        MapReader em = entry.asMap();
        if (!em.has("content")) continue;
        ItemReader content = em.get("content");
        if (!content.isMap()) continue;
        MapReader cm = content.asMap();
        if (!cm.has("Type")) continue;
        const char* ty = cm.get("Type").cstring();
        if (ty && strcmp(ty, "Font") == 0) font_objects++;
    }
    // advanced_test.pdf has 10 fonts processed by the postprocess.
    EXPECT_GE(font_objects, 1);
}

TEST_F(InputPdfTest, EmbeddedMathType1FontsRetainTheirEncodedOutlines) {
    Input* input = parse_pdf_file("test/input/math_intensive_test.pdf");
    ASSERT_NE(input, nullptr);
    ArrayReader objects = MapReader::fromItem(input->root).get("objects").asArray();
    int outlined_fonts = 0;
    for (int64_t i = 0; i < objects.length(); i++) {
        MapReader font = objects.get(i).asMap().get("content").asMap();
        const char* subtype = font.get("Subtype").cstring();
        if (!subtype || strcmp(subtype, "Type1")) continue;
        EXPECT_TRUE(font.get("glyph_paths").isMap()) << font.get("BaseFont").cstring();
        if (font.get("glyph_paths").isMap()) outlined_fonts++;
    }
    EXPECT_EQ(outlined_fonts, 21);
    // CMEX's display sum is byte 88, whose ToUnicode fallback misleadingly says "X".
    MapReader extension = find_indirect_object(objects, 14);
    const char* sum_path = extension.get("glyph_paths").asMap().get("88").cstring();
    ASSERT_NE(sum_path, nullptr);
    EXPECT_NE(strchr(sum_path, 'L'), nullptr);
    EXPECT_GT(strlen(sum_path), 100u);
}

static String* type1_test_program(MarkBuilder& builder, const char* outline, size_t length,
        bool encrypted, int* clear_length, const char* subr = nullptr, size_t subr_length = 0) {
    StrBuf* clear = strbuf_create("%!PS-AdobeFont-1.0: OutlineTest 1.0\n"
        "/FontMatrix [0.002 0 0 0.003 0.01 0.02] def\n"
        "/Encoding 256 array dup 65 /A put readonly def\n");
    if (encrypted) strbuf_append_str(clear, "currentfile eexec\n");
    *clear_length = (int)clear->length;
    StrBuf* priv = strbuf_new();
    if (encrypted) strbuf_append_char_n(priv, 0, 4);
    strbuf_append_format(priv, "/lenIV %d def\n/Subrs %d array\n", encrypted ? 4 : -1, subr ? 1 : 0);
    const char* programs[] = {subr, outline};
    size_t lengths[] = {subr_length, length};
    for (int i = 0; i < 2; i++) {
        if (!programs[i]) continue;
        if (i == 0) strbuf_append_format(priv, "dup 0 %zu RD ", lengths[i] + (encrypted ? 4 : 0));
        else strbuf_append_format(priv, "/CharStrings 1 dict dup begin\n/A %zu RD ", lengths[i] + (encrypted ? 4 : 0));
        uint16_t key = 4330;
        for (size_t j = 0; j < lengths[i] + (encrypted ? 4 : 0); j++) {
            uint8_t plain = encrypted && j < 4 ? 0 : (uint8_t)programs[i][j - (encrypted ? 4 : 0)];
            uint8_t cipher = encrypted ? plain ^ (key >> 8) : plain;
            strbuf_append_char(priv, (char)cipher);
            key = (uint16_t)((cipher + key) * 52845u + 22719u);
        }
        strbuf_append_str(priv, i == 0 ? " NP\n" : " ND\nend\n");
    }
    uint16_t key = 55665;
    for (size_t i = 0; i < priv->length; i++) {
        uint8_t cipher = encrypted ? (uint8_t)priv->str[i] ^ (key >> 8) : (uint8_t)priv->str[i];
        strbuf_append_char(clear, (char)cipher);
        key = (uint16_t)((cipher + key) * 52845u + 22719u);
    }
    String* result = builder.createString(clear->str, clear->length);
    strbuf_free(priv); strbuf_free(clear);
    return result;
}

TEST_F(InputPdfTest, Type1PathsRespectFontMatrixEncryptionSubroutinesAndEncodingDifferences) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    // hsbw; rmoveto; rlineto twice; closepath; endchar.
    const char direct[] = "\x8b\xf8\x88\x0d\x8b\x8b\x15\xef\x8b\x05\x8b\xef\x05\x09\x0e";
    const char subr[] = "\xef\x8b\x05\x8b\xef\x05\x0b";
    const char indirect[] = "\x8b\xf8\x88\x0d\x8b\x8b\x15\x8b\x0a\x09\x0e";
    int clear_length;
    String* plain = type1_test_program(builder, direct, sizeof(direct) - 1, false, &clear_length);
    Item plain_paths = pdf_type1_glyph_paths(input, plain, clear_length, ItemNull);
    const char* path = MapReader::fromItem(plain_paths).get("65").cstring();
    ASSERT_NE(path, nullptr);
    EXPECT_STREQ(path, "M10 20 L210 20 L210 320 Z ");

    String* encrypted = type1_test_program(builder, indirect, sizeof(indirect) - 1, true,
                                          &clear_length, subr, sizeof(subr) - 1);
    Item differences = builder.array().append(90).append("A").final();
    Item encoding = builder.map().put("Differences", differences).final();
    Item encrypted_paths = pdf_type1_glyph_paths(input, encrypted, clear_length, encoding);
    EXPECT_STREQ(MapReader::fromItem(encrypted_paths).get("90").cstring(), path);
    EXPECT_STREQ(MapReader::fromItem(encrypted_paths).get("65").cstring(), path);
}

TEST_F(InputPdfTest, Type1DecoderRejectsTruncatedProgramsAndRecursiveSubroutines) {
    Input* input = InputManager::create_input(nullptr);
    ASSERT_NE(input, nullptr);
    MarkBuilder builder(input);
    const char outline[] = "\x8b\xf8\x88\x0d\x8b\x0a\x0e";
    const char recursive[] = "\x8b\x0a\x0b";
    int clear_length;
    String* program = type1_test_program(builder, outline, sizeof(outline) - 1, true,
                                         &clear_length, recursive, sizeof(recursive) - 1);
    EXPECT_EQ(pdf_type1_glyph_paths(input, program, clear_length, ItemNull).item, ITEM_NULL);
    String* truncated = builder.createString("/lenIV -1 def /CharStrings 1 dict /A 99 RD abc");
    EXPECT_EQ(pdf_type1_glyph_paths(input, truncated, 0, ItemNull).item, ITEM_NULL);
    EXPECT_EQ(pdf_type1_glyph_paths(input, program, (int)program->len + 1, ItemNull).item, ITEM_NULL);
}

/* ══════════════════════════════════════════════════════════════════════
 * §5 Robustness
 * ══════════════════════════════════════════════════════════════════════ */

TEST_F(InputPdfTest, EmptyInputDoesNotCrash) {
    Url* url = url_parse("temp/empty.pdf");
    Input* input = InputManager::create_input(url);
    if (url) url_destroy(url);
    ASSERT_NE(input, nullptr);
    parse_pdf(input, "", 0);
    // Either ITEM_NULL or ITEM_ERROR is acceptable; the postprocess pass
    // should bail out cleanly without dereferencing anything.
    SUCCEED();
}

TEST_F(InputPdfTest, GarbageInputDoesNotCrash) {
    Url* url = url_parse("temp/garbage.pdf");
    Input* input = InputManager::create_input(url);
    if (url) url_destroy(url);
    ASSERT_NE(input, nullptr);
    const char* junk = "this is definitely not a PDF file at all";
    parse_pdf(input, junk, strlen(junk));
    SUCCEED();
}
