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
 *   - Static fixtures under test/pdf/data/basic/   (pre-existing PDFs that
 *     already exercise indirect-object refs and multi-page kid lists)
 *   - Generated multi-page PDF written via libharu into temp/ in SetUp,
 *     giving the test a fully deterministic page count + media-box assertion
 *     even if the static fixtures change.
 */

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/mark_reader.hpp"
#include "../lib/log.h"
#include "../lib/url.h"

extern "C" {
#include "../lib/pdf_writer.h"
}

/* ══════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void ensure_temp_dir(void) {
    mkdir("temp", 0755);
}

// Static fixture paths (PDFs that already contain indirect objects).
static const char* PDF_FIXTURE_SIMPLE   = "test/pdf/data/basic/test.pdf";
static const char* PDF_FIXTURE_ADVANCED = "test/pdf/data/basic/advanced_test.pdf";

// Generated fixture path (3-page PDF written in SetUp).
static const char* PDF_GEN_3PAGE        = "temp/test_input_pdf_3page.pdf";

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

/** Build a 3-page PDF with text on each page using libharu. The text
 *  forces libharu to emit a /Font dict on each page's resources. */
static void generate_3page_pdf(const char* path) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    if (!doc) return;
    HPDF_SetCompressionMode(doc, HPDF_COMP_NONE);  // keep parser happy

    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);

    for (int i = 1; i <= 3; i++) {
        HPDF_Page page = HPDF_AddPage(doc);
        // Use distinct dimensions per page so we can assert per-page
        // media_box propagation through the postprocess.
        HPDF_Page_SetWidth(page,  500.0f + (float)i * 10.0f);
        HPDF_Page_SetHeight(page, 700.0f + (float)i * 10.0f);
        HPDF_Page_SetFontAndSize(page, font, 12.0f);
        HPDF_Page_BeginText(page);
        char label[64];
        snprintf(label, sizeof(label), "Page %d body text", i);
        HPDF_Page_TextOut(page, 100.0f, 600.0f, label);
        HPDF_Page_EndText(page);
    }

    HPDF_SaveToFile(doc, path);
    HPDF_Free(doc);
}

/* ══════════════════════════════════════════════════════════════════════
 * Fixture
 * ══════════════════════════════════════════════════════════════════════ */

class InputPdfTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        log_init(NULL);
        ensure_temp_dir();
        generate_3page_pdf(PDF_GEN_3PAGE);
    }

    static void TearDownTestSuite() {
        unlink(PDF_GEN_3PAGE);
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

TEST_F(InputPdfTest, AdvancedFixtureHasManyIndirectObjects) {
    Input* input = parse_pdf_file(PDF_FIXTURE_ADVANCED);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader objs = root.get("objects").asArray();
    // advanced_test.pdf has Catalog + Pages + 2× Page + fonts + content
    // streams = 18 indirect objects in the captured trace.
    EXPECT_GE(objs.length(), 10);
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
    Input* input = parse_pdf_file(PDF_GEN_3PAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ASSERT_TRUE(root.has("pages"));
    ArrayReader pages = root.get("pages").asArray();
    EXPECT_EQ(pages.length(), 3) << "libharu emitted 3 /Page kids; flatten "
                                    "should expose all of them";
}

TEST_F(InputPdfTest, FlattenedPageCarriesType) {
    Input* input = parse_pdf_file(PDF_GEN_3PAGE);
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

TEST_F(InputPdfTest, FlattenedPageInheritsMediaBox) {
    Input* input = parse_pdf_file(PDF_GEN_3PAGE);
    ASSERT_NE(input, nullptr);
    MapReader root = MapReader::fromItem(input->root);
    ArrayReader pages = root.get("pages").asArray();
    ASSERT_EQ(pages.length(), 3);
    // libharu sets MediaBox on each /Page directly, not inherited from /Pages,
    // but either way the flattener must surface it on each entry.
    for (int64_t i = 0; i < pages.length(); i++) {
        MapReader p = pages.get(i).asMap();
        EXPECT_TRUE(p.has("media_box"))
            << "page " << i << " missing media_box after flatten";
    }
}

TEST_F(InputPdfTest, FlattenedPageInheritsResources) {
    Input* input = parse_pdf_file(PDF_GEN_3PAGE);
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
    Input* input = parse_pdf_file(PDF_GEN_3PAGE);
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
    // sufficient — the deeper coverage of CMap parsing lives in the unit
    // tests for radiant/pdf/fonts.cpp.
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
