#include <gtest/gtest.h>
#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/input/input-postscript.hpp"
#include "../lambda/core/mark_reader.hpp"
#include "../lambda/input/pdf_decompress.h"
#include "../lib/strbuf.h"

static Input* eps_parse(const char* bytes, size_t length) {
    Input* input = InputManager::create_input(nullptr);
    parse_eps(input, bytes, length);
    return input;
}

static MapReader eps_scan(const char* bytes, size_t length, size_t cap = 1048576) {
    Input* owner = InputManager::create_input(nullptr);
    MarkBuilder builder(owner);
    Item source = builder.createStringItem(bytes, length);
    return MapReader(postscript_scan_io(owner, source, 0, cap, 32).get_safe_map());
}

TEST(EpsInput, MetadataTrailerAndNestedDocuments) {
    const char source[] = "%!PS-Adobe-3.0 EPSF-3.0\r\n%%BoundingBox: (atend)\r\n%%Title: example\r\n"
        "%%EndComments\n%%BeginDocument: child\n%%BoundingBox: 0 0 900 900\n%%EndDocument\n"
        "(line\n%%BoundingBox: 0 0 800 800\n) pop\n%%Trailer\n%%BoundingBox: -5 10 95 60\n";
    Input* input = eps_parse(source, sizeof(source) - 1);
    ASSERT_FALSE(input->parse_failed);
    MapReader doc(input->root.get_safe_map());
    EXPECT_DOUBLE_EQ(doc.get("bbox").asArray().get(0).asFloat(), -5);
    EXPECT_DOUBLE_EQ(doc.get("bbox").asArray().get(3).asFloat(), 60);
    EXPECT_STREQ(doc.get("metadata").asMap().get("Title").cstring(), "example");
    EXPECT_EQ(doc.get("source").item().get_safe_binary() ? binary_length(doc.get("source").item().get_binary()) : 0,
              sizeof(source) - 1);
}

TEST(EpsInput, RejectsMalformedEnvelopes) {
    const char* cases[] = {"", "%!PS\n%%BoundingBox: 0 0 10 10\n", "%!PS-Adobe-3.0 EPSF-3.0\n",
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 0 5\n",
        "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n%%BeginData: 500 Bytes\na"};
    for (const char* source : cases) EXPECT_TRUE(eps_parse(source, strlen(source))->parse_failed) << source;
}

TEST(EpsInput, BinaryWrapperUsesValidatedSection) {
    const char source[] = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 20\n";
    char wrapped[sizeof(source) + 30] = {};
    wrapped[0] = (char)0xc5; wrapped[1] = (char)0xd0; wrapped[2] = (char)0xd3; wrapped[3] = (char)0xc6;
    wrapped[4] = 30; wrapped[8] = sizeof(source) - 1;
    wrapped[28] = wrapped[29] = (char)0xff;
    memcpy(wrapped + 30, source, sizeof(source) - 1);
    Input* input = eps_parse(wrapped, sizeof(wrapped));
    ASSERT_FALSE(input->parse_failed);
    EXPECT_EQ(MapReader(input->root.get_safe_map()).get("program").asMap().get("offset").asInt(), 30);
    wrapped[8] = (char)0xff;
    EXPECT_TRUE(eps_parse(wrapped, sizeof(wrapped))->parse_failed);
}

TEST(EpsScanner, NumbersProceduresAndByteStrings) {
    MapReader real = eps_scan("1.25e2", 6);
    EXPECT_DOUBLE_EQ(real.get("value").asFloat(), 125);
    EXPECT_EQ(eps_scan("16#ff", 5).get("value").asInt(), 255);
    const char text[] = "(a\\000\\377(b)\\\r\nc)";
    ArrayReader bytes = eps_scan(text, sizeof(text) - 1).get("value").asArray();
    ASSERT_EQ(bytes.length(), 7);
    EXPECT_EQ(bytes.get(1).asInt(), 0); EXPECT_EQ(bytes.get(2).asInt(), 255);
    EXPECT_EQ(eps_scan("<f>", 3).get("value").asArray().get(0).asInt(), 240);
    const char procedure[] = "{ /x //y [ 2 3 add ] }";
    ArrayReader body = eps_scan(procedure, sizeof(procedure) - 1).get("value").asArray();
    EXPECT_EQ(body.length(), 7);
    EXPECT_STREQ(body.get(1).asMap().get("kind").cstring(), "immediate");
}

TEST(EpsScanner, ReportsSyntaxAndAllocationLimits) {
    const char* cases[] = {"(unfinished", "{ 1", ")", "<gg>", "<~!~>", "<~uuuuu~>"};
    for (const char* source : cases)
        EXPECT_STREQ(eps_scan(source, strlen(source)).get("kind").cstring(), "error") << source;
    EXPECT_STREQ(eps_scan("(12345)", 7, 4).get("kind").cstring(), "error");
    EXPECT_STREQ(eps_scan("longname", 8, 4).get("kind").cstring(), "error");
}

TEST(EpsScanner, SharedAscii85HandlesExpansionAndSourceBoundary) {
    StrBuf* output = strbuf_new(); size_t consumed = 0;
    ASSERT_TRUE(ascii85_decode_bounded("<~zzzz~>tail", 12, output, 16, true, &consumed));
    EXPECT_EQ(output->length, 16u); EXPECT_EQ(consumed, 8u);
    EXPECT_FALSE(ascii85_decode_bounded("<~zzzz~>", 8, output, 15, true, &consumed));
    size_t length = 0;
    char* decoded = ascii85_decode("zzzz~>", 6, &length);
    ASSERT_NE(decoded, nullptr); EXPECT_EQ(length, 16u);
    mem_free(decoded); strbuf_free(output);
}

TEST(EpsInput, MetadataPreservesUtf8AndOpaqueBytes) {
    const char source[] = "%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 10 10\n%%Title: caf\xc3\xa9\n%%Creator: \xff\n";
    Input* input = eps_parse(source, sizeof(source) - 1);
    ASSERT_FALSE(input->parse_failed);
    MapReader metadata = MapReader(input->root.get_safe_map()).get("metadata").asMap();
    EXPECT_STREQ(metadata.get("Title").cstring(), "caf\xc3\xa9");
    Binary* creator = metadata.get("Creator").item().get_safe_binary();
    ASSERT_NE(creator, nullptr);
    ASSERT_EQ(binary_length(creator), 1u);
    EXPECT_EQ(binary_data(creator)[0], 255u);
}

TEST(EpsScanner, ProgramBoundaryCannotConsumePreviewBytes) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item source = builder.createStringItem("(open)preview");
    MapReader token(postscript_scan_io(input, source, 0, 100, 10, 5).get_safe_map());
    EXPECT_STREQ(token.get("kind").cstring(), "error");
}

TEST(EpsScanner, ProcedureObjectCountIsBoundedBeforeMaterialization) {
    Input* input = InputManager::create_input(nullptr);
    MarkBuilder builder(input);
    Item source = builder.createStringItem("{1 2 3}");
    MapReader token(postscript_scan_io(input, source, 0, 100, 10, SIZE_MAX, 2).get_safe_map());
    EXPECT_STREQ(token.get("kind").cstring(), "error");
}
