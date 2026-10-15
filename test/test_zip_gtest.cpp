#include <gtest/gtest.h>
#include "../lambda/io/zip_archive.hpp"
#include "../lambda/io/fs_node.hpp"
#include "../lambda/core/print.h"
#include "../lambda/input/input.hpp"
#include "../lib/file.h"
#include "../lib/endian.h"
#include "../lib/memtrack.h"
#include "../lib/log.h"

class ZipTest : public ::testing::Test {
protected:
    ZipError error = {};
    void SetUp() override { log_init(nullptr); }
    ZipArchive* open(const char* file, const ZipLimits* limits = nullptr,
            ArchiveFormat format = ARCHIVE_ZIP) {
        char* bytes = nullptr; size_t length = 0;
        if (!file_read_all(file, MEM_CAT_TEMP, &bytes, &length)) return nullptr;
        if (format == ARCHIVE_UNKNOWN) format = archive_source_format(file, bytes, length);
        ZipArchive* archive = archive_open(bytes, length, format, limits, nullptr, 0, &error);
        mem_free(bytes);
        return archive;
    }
    uint32_t find(ZipArchive* archive, const char* name) {
        for (int i = 0; archive && i < archive->entries->length; i++)
            if (!strcmp(zip_archive_entry(archive, i)->path, name)) return i;
        return UINT32_MAX;
    }
};

TEST_F(ZipTest, CapturesIndexesAndLazilyDecodesOneMember) {
    ZipArchive* archive = open("test/input/zip/sample.docx");
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->decompressions, 0u);
    EXPECT_STREQ(zip_archive_entry(archive, 1)->path, "word");
    EXPECT_EQ(zip_archive_entry(archive, 0)->child_count, 7u);
    uint32_t file = find(archive, "data.json");
    ByteSpan bytes = {};
    ASSERT_TRUE(zip_entry_bytes(archive, file, &bytes, &error)) << error.message;
    EXPECT_EQ(bytes.length, 13u);
    EXPECT_EQ(memcmp(byte_span_data(&bytes), "{\"answer\":42}", 13), 0);
    EXPECT_EQ(archive->decompressions, 1u);
    ASSERT_TRUE(zip_entry_bytes(archive, file, &bytes, &error));
    EXPECT_EQ(archive->decompressions, 1u);
    zip_archive_release(archive);
}

TEST_F(ZipTest, JavaJarKeepsClassAndManifestReadsLazy) {
    ZipArchive* archive = open("test/input/zip/sample.jar");
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(zip_archive_entry(archive, 0)->child_count, 4u);
    uint32_t compiled = find(archive, "example/Hello.class");
    uint32_t manifest = find(archive, "META-INF/MANIFEST.MF");
    ASSERT_NE(compiled, UINT32_MAX); ASSERT_NE(manifest, UINT32_MAX);
    EXPECT_EQ(zip_archive_entry(archive, compiled)->method, 8u);
    EXPECT_NE(zip_archive_entry(archive, compiled)->flags & 8, 0);
    EXPECT_EQ(archive->decompressions, 0u);
    ByteSpan bytes = {};
    ASSERT_TRUE(zip_entry_bytes(archive, compiled, &bytes, &error)) << error.message;
    const uint8_t java8[] = {0xca, 0xfe, 0xba, 0xbe, 0, 0, 0, 52};
    ASSERT_GE(bytes.length, sizeof(java8));
    EXPECT_EQ(memcmp(byte_span_data(&bytes), java8, sizeof(java8)), 0);
    EXPECT_EQ(archive->decompressions, 1u);
    ASSERT_TRUE(zip_entry_bytes(archive, manifest, &bytes, &error)) << error.message;
    const char expected[] = "Manifest-Version: 1.0\r\nMain-Class: example.Hello\r\nCreated-By: Lambda ZIP fixture\r\n\r\n";
    ASSERT_EQ(bytes.length, sizeof(expected) - 1);
    EXPECT_EQ(memcmp(byte_span_data(&bytes), expected, sizeof(expected) - 1), 0);
    ASSERT_TRUE(zip_entry_bytes(archive, compiled, &bytes, &error));
    EXPECT_EQ(archive->decompressions, 2u);
    ASSERT_TRUE(zip_entry_bytes(archive, find(archive, "resources/opaque.bin"), &bytes, &error));
    const uint8_t opaque[] = {0, 255, 'P', 'K', 0};
    ASSERT_EQ(bytes.length, sizeof(opaque));
    EXPECT_EQ(memcmp(byte_span_data(&bytes), opaque, sizeof(opaque)), 0);
    EXPECT_EQ(archive->decompressions, 3u);
    zip_archive_release(archive);
}

TEST_F(ZipTest, Zip64AndBothDescriptors) {
    const char* names[] = {"test/input/zip/wide.zip", "test/input/zip/descriptor.zip", "test/input/zip/descriptor-signed.zip",
        "test/input/zip/descriptor64.zip", "test/input/zip/descriptor64-signed.zip"};
    for (int i = 0; i < 5; i++) {
        ZipArchive* archive = open(names[i]);
        ASSERT_NE(archive, nullptr) << error.message;
        EXPECT_EQ(archive->zip64, i == 0 || i >= 3);
        ByteSpan bytes = {};
        EXPECT_TRUE(zip_entry_bytes(archive, 1, &bytes, &error)) << error.message;
        EXPECT_EQ(bytes.length, i == 0 ? 5u : 9u);
        zip_archive_release(archive);
    }
}

TEST_F(ZipTest, PayloadFailureIsDeferredAndCached) {
    ZipArchive* archive = open("test/input/zip/corrupt.zip");
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->decompressions, 0u);
    ByteSpan bytes = {};
    EXPECT_FALSE(zip_entry_bytes(archive, find(archive, "word/document.xml"), &bytes, &error));
    uint64_t attempts = archive->decompressions;
    EXPECT_FALSE(zip_entry_bytes(archive, find(archive, "word/document.xml"), &bytes, &error));
    EXPECT_EQ(archive->decompressions, attempts);
    EXPECT_TRUE(zip_entry_bytes(archive, find(archive, "data.json"), &bytes, &error));
    zip_archive_release(archive);
}

TEST_F(ZipTest, EndSignatureInsideCommentDoesNotHideDirectory) {
    char* original = nullptr; size_t length = 0;
    ASSERT_TRUE(file_read_all("test/input/zip/wide.zip", MEM_CAT_TEMP, &original, &length));
    StrBuf* bytes = strbuf_new();
    strbuf_append_str_n(bytes, original, length);
    mem_free(original);
    uint8_t comment[22] = {};
    write_le32(comment, 0x06054b50);
    write_le16((uint8_t*)bytes->str + length - 2, sizeof(comment));
    strbuf_append_str_n(bytes, (const char*)comment, sizeof(comment));
    ZipArchive* archive = zip_archive_open(bytes->str, bytes->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->entries->length, 2);
    zip_archive_release(archive);
    strbuf_free(bytes);
}

TEST_F(ZipTest, RejectsPathsFeaturesAndResourceLimits) {
    EXPECT_EQ(open("test/input/zip/traversal.zip"), nullptr);
    ZipLimits limits = zip_default_limits(); limits.entries = 1;
    EXPECT_EQ(open("test/input/zip/sample.docx", &limits), nullptr);
    limits = zip_default_limits(); limits.member_bytes = 2;
    EXPECT_EQ(open("test/input/zip/wide.zip", &limits), nullptr);
    uint8_t malformed[22] = {}; write_le32(malformed, 0x06054b50); write_le16(malformed + 4, 1);
    EXPECT_EQ(zip_archive_open(malformed, sizeof(malformed), nullptr, nullptr, 0, &error), nullptr);
    ZipArchive* empty = open("test/input/zip/empty.zip");
    ASSERT_NE(empty, nullptr); EXPECT_EQ(empty->entries->length, 1); zip_archive_release(empty);
}

TEST_F(ZipTest, WriterRoundTripsBinaryEmptyAndZip64Deterministically) {
    const uint8_t payload[] = {0, 255, 1, 0};
    ZipOutputEntry file = {"dir/data.bin", payload, sizeof(payload), 0100640, 0x50221883, false};
    ZipOutputEntry empty = {"empty/", nullptr, 0, 0040755, 0, true};
    ArrayList* entries = arraylist_new(2); arraylist_append(entries, &file); arraylist_append(entries, &empty);
    for (int method : {0, 8}) for (bool wide : {false, true}) {
        ZipWriteOptions options = zip_default_write_options();
        options.method = method; options.force_zip64 = wide; options.deterministic = true;
        StrBuf* first = strbuf_new(); StrBuf* second = strbuf_new();
        ASSERT_TRUE(zip_write_entries(entries, &options, first, &error)) << error.message;
        ASSERT_TRUE(zip_write_entries(entries, &options, second, &error)) << error.message;
        EXPECT_EQ(first->length, second->length); EXPECT_EQ(memcmp(first->str, second->str, first->length), 0);
        ZipArchive* archive = zip_archive_open(first->str, first->length, nullptr, nullptr, 0, &error);
        ASSERT_NE(archive, nullptr) << error.message;
        EXPECT_EQ(archive->zip64, wide);
        ByteSpan bytes = {};
        ASSERT_TRUE(zip_entry_bytes(archive, find(archive, "dir/data.bin"), &bytes, &error));
        EXPECT_EQ(bytes.length, sizeof(payload)); EXPECT_EQ(memcmp(byte_span_data(&bytes), payload, sizeof(payload)), 0);
        EXPECT_EQ(zip_archive_entry(archive, find(archive, "dir/data.bin"))->mode, 0100640u);
        zip_archive_release(archive); strbuf_free(first); strbuf_free(second);
    }
    arraylist_free(entries);
}

TEST_F(ZipTest, WriterRejectsDuplicatesAndUnsafeTrees) {
    ZipOutputEntry file = {"a", (const uint8_t*)"x", 1, 0, 0, false};
    ArrayList* entries = arraylist_new(2); arraylist_append(entries, &file); arraylist_append(entries, &file);
    StrBuf* output = strbuf_new(); EXPECT_FALSE(zip_write_entries(entries, nullptr, output, &error));
    entries->length = 1; file.path = "../bad"; EXPECT_FALSE(zip_write_entries(entries, nullptr, output, &error));
    strbuf_free(output); arraylist_free(entries);
}

TEST_F(ZipTest, RejectsUnsupportedFlagsMethodsLinksAndLocalMismatches) {
    ZipOutputEntry file = {"a", (const uint8_t*)"hello", 5, 0100644, 0, false};
    ArrayList* entries = arraylist_new(1); arraylist_append(entries, &file);
    StrBuf* original = strbuf_new(); ZipWriteOptions options = zip_default_write_options(); options.method = 0;
    ASSERT_TRUE(zip_write_entries(entries, &options, original, &error));
    size_t central = read_le32((uint8_t*)original->str + original->length - 6);
    for (int variant = 0; variant < 9; variant++) {
        StrBuf* bad = strbuf_dup(original); uint8_t* data = (uint8_t*)bad->str;
        switch (variant) {
        case 0: write_le16(data + central + 8, 0x801); write_le16(data + 6, 0x801); break;
        case 1: write_le16(data + central + 10, 12); write_le16(data + 8, 12); break;
        case 2: write_le16(data + central + 34, 1); break;
        case 3: write_le32(data + central + 38, 0120777u << 16); break;
        case 4: data[30] = 'b'; break;
        case 5: write_le32(data + central + 42, UINT32_MAX); break;
        case 6: write_le32(data + central + 20, UINT32_MAX); break;
        case 7: write_le16(data + central + 6, 63); break;
        case 8: data[central + 46] = 0xff; data[30] = 0xff; break;
        }
        EXPECT_EQ(zip_archive_open(data, bad->length, nullptr, nullptr, 0, &error), nullptr) << variant;
        strbuf_free(bad);
    }
    strbuf_free(original); arraylist_free(entries);
}

TEST_F(ZipTest, StoredCrcFailureAndSnapshotOwnership) {
    ZipOutputEntry file = {"a", (const uint8_t*)"hello", 5, 0, 0, false};
    ArrayList* entries = arraylist_new(1); arraylist_append(entries, &file);
    StrBuf* bytes = strbuf_new(); ZipWriteOptions options = zip_default_write_options(); options.method = 0;
    ASSERT_TRUE(zip_write_entries(entries, &options, bytes, &error));
    ZipArchive* snapshot = zip_archive_open(bytes->str, bytes->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(snapshot, nullptr);
    bytes->str[31] ^= 1;
    ZipArchive* corrupt = zip_archive_open(bytes->str, bytes->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(corrupt, nullptr);
    ByteSpan payload = {};
    EXPECT_TRUE(zip_entry_bytes(snapshot, 1, &payload, &error));
    EXPECT_EQ(memcmp(byte_span_data(&payload), "hello", 5), 0);
    EXPECT_FALSE(zip_entry_bytes(corrupt, 1, &payload, &error));
    EXPECT_FALSE(zip_entry_bytes(corrupt, 1, &payload, &error));
    EXPECT_EQ(corrupt->decompressions, 0u);
    zip_archive_release(snapshot); zip_archive_release(corrupt);
    strbuf_free(bytes); arraylist_free(entries);
}

TEST_F(ZipTest, ExtractionVersionUsesLowByteInBothHeaders) {
    ZipOutputEntry file = {"a", (const uint8_t*)"hello", 5, 0100644, 0, false};
    ArrayList* entries = arraylist_new(1); arraylist_append(entries, &file);
    StrBuf* bytes = strbuf_new();
    ASSERT_TRUE(zip_write_entries(entries, nullptr, bytes, &error));
    uint8_t* data = (uint8_t*)bytes->str;
    size_t central = read_le32(data + bytes->length - 6);
    for (uint16_t platform : {0u, 3u, 19u, 255u}) {
        for (uint16_t version : {20u, 45u, 63u}) {
            write_le16(data + 4, (platform << 8) | version);
            write_le16(data + central + 6, (platform << 8) | version);
            ZipArchive* archive = zip_archive_open(data, bytes->length, nullptr, nullptr, 0, &error);
            if (version <= 45) {
                ASSERT_NE(archive, nullptr) << error.message;
                ByteSpan payload = {};
                ASSERT_TRUE(zip_entry_bytes(archive, find(archive, "a"), &payload, &error));
                EXPECT_EQ(payload.length, 5u);
                EXPECT_EQ(memcmp(byte_span_data(&payload), "hello", 5), 0);
                zip_archive_release(archive);
            } else EXPECT_EQ(archive, nullptr);
        }
    }
    write_le16(data + central + 6, 20);
    EXPECT_EQ(zip_archive_open(data, bytes->length, nullptr, nullptr, 0, &error), nullptr);
    strbuf_free(bytes); arraylist_free(entries);
}

TEST_F(ZipTest, LegacyNamesNormalizeAndExplicitDirectoriesMerge) {
    ZipOutputEntry dir = {"dir/", nullptr, 0, 0, 0, true};
    ZipOutputEntry file = {"dir/e", (const uint8_t*)"x", 1, 0, 0, false};
    ArrayList* entries = arraylist_new(2); arraylist_append(entries, &file); arraylist_append(entries, &dir);
    StrBuf* bytes = strbuf_new(); ASSERT_TRUE(zip_write_entries(entries, nullptr, bytes, &error));
    ZipArchive* archive = zip_archive_open(bytes->str, bytes->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->entries->length, 3);
    EXPECT_TRUE(zip_archive_entry(archive, 1)->explicit_directory);
    zip_archive_release(archive);
    // The high CP437 byte 0x82 becomes U+00E9 when EFS is absent.
    ZipOutputEntry legacy = {"e", (const uint8_t*)"x", 1, 0, 0, false};
    entries->length = 0; arraylist_append(entries, &legacy);
    ASSERT_TRUE(zip_write_entries(entries, nullptr, bytes, &error));
    uint8_t* data = (uint8_t*)bytes->str;
    size_t central = read_le32(data + bytes->length - 6);
    write_le16(data + 6, 0); write_le16(data + central + 8, 0);
    data[30] = data[central + 46] = 0x82;
    archive = zip_archive_open(data, bytes->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(archive, nullptr); EXPECT_STREQ(zip_archive_entry(archive, 1)->path, "\xc3\xa9");
    zip_archive_release(archive); strbuf_free(bytes); arraylist_free(entries);
}

TEST_F(ZipTest, NestedLimitsShareDeclaredAndActualExpansion) {
    ZipArchive* outer = open("test/input/zip/sample.docx"); ASSERT_NE(outer, nullptr);
    ByteSpan nested = {}; ASSERT_TRUE(zip_entry_bytes(outer, find(outer, "nested.zip"), &nested, &error));
    outer->budget->limits.expanded_bytes = outer->budget->declared_bytes + 5;
    EXPECT_EQ(zip_archive_open(byte_span_data(&nested), nested.length, nullptr, outer->budget, 1, &error), nullptr);
    outer->budget->limits.nesting_depth = 0;
    EXPECT_EQ(zip_archive_open(byte_span_data(&nested), nested.length, nullptr, outer->budget, 1, &error), nullptr);
    zip_archive_release(outer);
}

TEST_F(ZipTest, DiagnosticLoggingDoesNotDecodeArchiveViews) {
    char* bytes = nullptr; size_t length = 0;
    ASSERT_TRUE(file_read_all("test/input/zip/sample.docx", MEM_CAT_TEMP, &bytes, &length));
    Input* input = input_from_source_n(bytes, length, nullptr, nullptr, nullptr);
    mem_free(bytes);
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed);
    ZipArchive* archive = fs_node_archive(input->root);
    ASSERT_NE(archive, nullptr);
    Item children = fs_node_content(input->root);
    MarkBuilder builder(input);
    Item row = builder.element("tree_entry").attr("source", input->root).attr("children", children).final();
    int previous_level = log_default_category->level;
    log_set_level(log_default_category, LOG_LEVEL_DEBUG);
    log_item(row, "ZIP_DIAGNOSTIC_TEST");
    log_set_level(log_default_category, previous_level);
    EXPECT_EQ(archive->decompressions, 0u);
    StrBuf* rendered = strbuf_new();
    print_item(rendered, varray_get(children.varray, 1));
    EXPECT_EQ(archive->decompressions, 1u);
    strbuf_free(rendered);
}

TEST_F(ZipTest, InputNodesUseSharedContentAndRetainRawBytesAfterParseFailure) {
    char* bytes = nullptr; size_t length = 0;
    ASSERT_TRUE(file_read_all("test/input/zip/sample.docx", MEM_CAT_TEMP, &bytes, &length));
    Input* input = input_from_source_n(bytes, length, nullptr, nullptr, nullptr);
    mem_free(bytes);
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed);
    ASSERT_TRUE(fs_node_is(input->root));
    ZipArchive* archive = fs_node_archive(input->root);
    EXPECT_EQ(archive->decompressions, 0u);
    Item children = fs_node_content(input->root);
    ASSERT_EQ(get_type_id(children), LMD_TYPE_VARRAY); EXPECT_EQ(varray_count(children.varray), 7);
    EXPECT_EQ(archive->decompressions, 0u);
    Item bad = varray_get(children.varray, 6);
    EXPECT_EQ(get_type_id(fs_node_content(bad)), LMD_TYPE_ERROR);
    Input* options = InputManager::create_input(nullptr); MarkBuilder builder(options);
    String* binary = builder.createString("binary");
    Item raw = fs_node_input(bad, binary, nullptr);
    ASSERT_EQ(get_type_id(raw), LMD_TYPE_BINARY); EXPECT_EQ(raw.get_len(), 8u);
}

TEST_F(ZipTest, Zip64EntryCountBoundary) {
    const int count = UINT16_MAX;
    Pool* pool = pool_create(); ArrayList* entries = arraylist_new(count);
    for (int i = 0; i < count; i++) {
        ZipOutputEntry* entry = (ZipOutputEntry*)pool_calloc(pool, sizeof(ZipOutputEntry));
        char* name = (char*)pool_alloc(pool, 16);
        snprintf(name, 16, "f%06d", i);
        entry->path = name; arraylist_append(entries, entry);
    }
    StrBuf* bytes = strbuf_new(); ZipWriteOptions options = zip_default_write_options(); options.method = 0;
    ASSERT_TRUE(zip_write_entries(entries, &options, bytes, &error)) << error.message;
    ZipArchive* archive = zip_archive_open(bytes->str, bytes->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_TRUE(archive->zip64); EXPECT_EQ(archive->entries->length, count + 1);
    EXPECT_EQ(zip_archive_entry(archive, find(archive, "f000000"))->dos_time, 0x00210000u);
    zip_archive_release(archive); strbuf_free(bytes); arraylist_free(entries); pool_destroy(pool);
}


TEST_F(ZipTest, NativeSevenZipIndexesWithoutBodyDecoding) {
    const char* files[] = {"7z-copy.7z", "7z-lzma.7z", "7z-lzma2.7z", "7z-delta.7z", "7z-bcj.7z"};
    for (const char* file : files) {
        char pathname[160];
        snprintf(pathname, sizeof(pathname), "test/input/archive/%s", file);
        Archive* archive = open(pathname, nullptr, ARCHIVE_7Z);
        ASSERT_NE(archive, nullptr) << file << ": " << error.message;
        EXPECT_EQ(archive->format, ARCHIVE_7Z);
        EXPECT_EQ(archive->decompressions, 0u);
        EXPECT_EQ(archive->budget->expanded_bytes, 0u);
        ByteSpan bytes = {};
        ASSERT_TRUE(archive_member_bytes(archive, 1, &bytes, &error)) << error.message;
        EXPECT_EQ(bytes.length, archive_entry(archive, 1)->size);
        uint64_t charged = archive->budget->expanded_bytes;
        uint64_t decoded = archive->decompressions;
        EXPECT_GE(charged, bytes.length);
        ASSERT_TRUE(archive_member_bytes(archive, 1, &bytes, &error));
        EXPECT_EQ(archive->budget->expanded_bytes, charged);
        EXPECT_EQ(archive->decompressions, decoded);
        archive_release(archive);
    }
}

TEST_F(ZipTest, NativeSevenZipSolidAccountsForPredecessors) {
    const char* files[] = {"test/input/archive/7z-solid.7z", "test/input/archive/7z-solid-copy.7z"};
    for (const char* file : files) {
        Archive* archive = open(file, nullptr, ARCHIVE_7Z);
        ASSERT_NE(archive, nullptr) << error.message;
        EXPECT_EQ(archive->decompressions, 0u);
        ByteSpan bytes = {};
        ASSERT_TRUE(archive_member_bytes(archive, find(archive, "file4"), &bytes, &error)) << error.message;
        EXPECT_EQ(bytes.length, 52u);
        EXPECT_GE(archive->budget->expanded_bytes, 13u + 26u + 39u + 52u);
        archive_release(archive);
    }
}

TEST_F(ZipTest, NativeSevenZipCachesLazyIntegrityFailures) {
    char* data = nullptr;
    size_t length = 0;
    ASSERT_TRUE(file_read_all("test/input/archive/7z-copy.7z", MEM_CAT_TEMP, &data, &length));
    ASSERT_GT(length, 32u);
    // the 7z Copy stream follows the fixed 32-byte signature/start header.
    data[32] ^= 1;
    Archive* archive = archive_open(data, length, ARCHIVE_7Z, nullptr, nullptr, 0, &error);
    mem_free(data);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->decompressions, 0u);
    ByteSpan bytes = {};
    EXPECT_FALSE(archive_member_bytes(archive, 1, &bytes, &error));
    uint64_t charged = archive->budget->expanded_bytes;
    EXPECT_FALSE(archive_member_bytes(archive, 1, &bytes, &error));
    EXPECT_EQ(archive->decompressions, 1u);
    EXPECT_EQ(archive->budget->expanded_bytes, charged);
    archive_release(archive);
}

TEST_F(ZipTest, NativeRarFiveCapturesAndChecksAllPayloads) {
    const char* files[] = {"rar5-stored.rar", "rar5-compressed.rar", "rar5-solid.rar", "rar5-blake2.rar"};
    for (const char* file : files) {
        char pathname[160];
        snprintf(pathname, sizeof(pathname), "test/input/archive/%s", file);
        Archive* archive = open(pathname, nullptr, ARCHIVE_RAR);
        ASSERT_NE(archive, nullptr) << file << ": " << error.message;
        EXPECT_EQ(archive->format, ARCHIVE_RAR5);
        uint64_t charged = archive->budget->expanded_bytes;
        EXPECT_GT(charged, 0u);
        for (int index = 1; index < archive->entries->length; index++) {
            ArchiveEntry* entry = archive_entry(archive, index);
            if (entry->directory) continue;
            ASSERT_TRUE(entry->decoded);
            ASSERT_NE(entry->payload, nullptr);
            ByteSpan bytes = {};
            EXPECT_TRUE(archive_member_bytes(archive, index, &bytes, &error));
            EXPECT_EQ(bytes.length, entry->size);
        }
        EXPECT_EQ(archive->budget->expanded_bytes, charged);
        archive_release(archive);
    }
}

TEST_F(ZipTest, NativeRarFiveRejectsCorruptPayloadAtInput) {
    char* data = nullptr;
    size_t length = 0;
    ASSERT_TRUE(file_read_all("test/input/archive/rar5-stored.rar", MEM_CAT_TEMP, &data, &length));
    const char* payload = "hello libarchive test suite!\n";
    bool changed = false;
    for (size_t i = 0; i + strlen(payload) <= length; i++) {
        if (memcmp(data + i, payload, strlen(payload))) continue;
        data[i] ^= 1;
        changed = true;
        break;
    }
    ASSERT_TRUE(changed);
    EXPECT_EQ(archive_open(data, length, ARCHIVE_RAR5, nullptr, nullptr, 0, &error), nullptr);
    mem_free(data);
}

TEST_F(ZipTest, NativeEmptySevenZipAndRejectedSpecialProfiles) {
    Archive* archive = open("test/input/archive/7z-empty.7z", nullptr, ARCHIVE_7Z);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->entries->length, 1);
    archive_release(archive);
    const char* files[] = {"7z-encrypted.7z", "rar5-encrypted.rar", "rar5-links.rar", "rar5-unicode.rar"};
    for (const char* file : files) {
        char pathname[160];
        snprintf(pathname, sizeof(pathname), "test/input/archive/%s", file);
        EXPECT_EQ(open(pathname, nullptr, ARCHIVE_UNKNOWN), nullptr) << file;
    }
    ArchiveLimits limits = archive_default_limits();
    limits.index_bytes = 32;
    EXPECT_EQ(open("test/input/archive/7z-lzma2.7z", &limits, ARCHIVE_7Z), nullptr);
    limits = archive_default_limits();
    limits.member_bytes = 1;
    EXPECT_EQ(open("test/input/archive/rar5-solid.rar", &limits, ARCHIVE_RAR5), nullptr);
}


TEST_F(ZipTest, NativeTarVariantsCaptureExactBytesAndNames) {
    const char* files[] = {"sample.tar", "sample.tar.gz", "sample.tar.bz2", "sample.tar.xz", "sample.tar.zst", "concat.tar.gz"};
    for (const char* file : files) {
        char path[160]; snprintf(path, sizeof(path), "test/input/archive/%s", file);
        Archive* archive = open(path, nullptr, ARCHIVE_UNKNOWN);
        ASSERT_NE(archive, nullptr) << file << ": " << error.message;
        EXPECT_EQ(archive->format, ARCHIVE_TAR);
        EXPECT_EQ(archive->decompressions, 0u);
        EXPECT_EQ(archive_entry(archive, 0)->child_count, 6u);
        EXPECT_EQ(archive_entry(archive, find(archive, "hello.txt"))->modified_seconds, 1700000000);
        EXPECT_EQ(archive_entry(archive, find(archive, "hello.txt"))->mode, 0100640u);
        uint64_t expanded = archive->budget->expanded_bytes;
        EXPECT_EQ(expanded, !strcmp(file, "sample.tar") ? 0u : 10240u);
        ByteSpan bytes = {};
        ASSERT_TRUE(archive_member_bytes(archive, find(archive, "docs/data.json"), &bytes, &error)) << error.message;
        EXPECT_EQ(bytes.length, 13u);
        EXPECT_EQ(memcmp(byte_span_data(&bytes), "{\"answer\":42}", 13), 0);
        ASSERT_TRUE(archive_member_bytes(archive, find(archive, "caf\xc3\xa9/\xe4\xb8\xad\xe6\x96\x87.txt"), &bytes, &error));
        EXPECT_EQ(bytes.length, 7u);
        ASSERT_TRUE(archive_member_bytes(archive, find(archive, "opaque.bin"), &bytes, &error));
        const uint8_t opaque[] = {0, 255, 'A', 0};
        EXPECT_EQ(bytes.length, sizeof(opaque));
        EXPECT_EQ(memcmp(byte_span_data(&bytes), opaque, sizeof(opaque)), 0);
        EXPECT_EQ(archive->budget->expanded_bytes, expanded ? expanded : 24u);
        archive_release(archive);
    }
    const char* variants[] = {"ustar.tar", "gnu.tar", "v7.tar", "empty.tar"};
    for (const char* file : variants) {
        char path[160]; snprintf(path, sizeof(path), "test/input/archive/%s", file);
        Archive* archive = open(path, nullptr, ARCHIVE_TAR);
        ASSERT_NE(archive, nullptr) << file << ": " << error.message;
        for (int i = 1; i < archive->entries->length; i++) {
            if (archive_entry(archive, i)->directory) continue;
            ByteSpan bytes = {};
            ASSERT_TRUE(archive_member_bytes(archive, i, &bytes, &error)) << error.message;
        }
        archive_release(archive);
    }
}

TEST_F(ZipTest, NativeTarRejectsInvalidRecordsAndFilterIntegrity) {
    const char* files[] = {"unsafe.tar", "unsafe-pax.tar", "unsafe-gnu.tar", "nul-pax.tar",
        "duplicate.tar", "conflict.tar", "bad-header.tar", "truncated.tar", "missing-end.tar", "trailing.tar",
        "symlink.tar", "hardlink.tar", "fifo.tar", "device.tar", "sparse.tar", "bad-crc.tar.gz", "truncated.tar.gz",
        "bad-crc.tar.bz2", "bad-crc.tar.xz", "bad-crc.tar.zst", "not-tar.gz"};
    for (const char* file : files) {
        char path[160]; snprintf(path, sizeof(path), "test/input/archive/%s", file);
        Archive* archive = open(path, nullptr, ARCHIVE_TAR);
        EXPECT_EQ(archive, nullptr) << file;
        archive_release(archive);
    }
    ArchiveLimits limits = archive_default_limits(); limits.expanded_bytes = 10239;
    EXPECT_EQ(open("test/input/archive/sample.tar.gz", &limits, ARCHIVE_TAR), nullptr);
    limits = archive_default_limits(); limits.entries = 2;
    EXPECT_EQ(open("test/input/archive/sample.tar", &limits, ARCHIVE_TAR), nullptr);
    limits = archive_default_limits(); limits.name_bytes = 4;
    EXPECT_EQ(open("test/input/archive/sample.tar", &limits, ARCHIVE_TAR), nullptr);
}

TEST_F(ZipTest, NativeNamesPreserveDistinctUnicodeSpellings) {
    Archive* archive = open("test/input/archive/unicode.tar", nullptr, ARCHIVE_TAR);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_NE(find(archive, "caf\xc3\xa9.txt"), ARCHIVE_NO_ENTRY);
    EXPECT_NE(find(archive, "cafe\xcc\x81.txt"), ARCHIVE_NO_ENTRY);
    EXPECT_EQ(archive->entries->length, 3);
    archive_release(archive);
}

TEST_F(ZipTest, NativeLegacyRarSkipsBodiesAndRejectsUnsupportedProfiles) {
    Archive* archive = open("test/input/archive/legacy.rar", nullptr, ARCHIVE_RAR);
    ASSERT_NE(archive, nullptr) << error.message;
    EXPECT_EQ(archive->decompressions, 0u);
    EXPECT_EQ(archive->budget->expanded_bytes, 0u);
    ByteSpan bytes = {};
    ASSERT_TRUE(archive_member_bytes(archive, find(archive, "test.txt"), &bytes, &error)) << error.message;
    EXPECT_EQ(bytes.length, 16u);
    EXPECT_EQ(memcmp(byte_span_data(&bytes), "test text file\r\n", 16), 0);
    EXPECT_EQ(archive->decompressions, 1u);
    archive_release(archive);
    const char* files[] = {"rar-solid.rar", "rar-links.rar", "rar-encrypted.rar", "rar5-volume.rar",
        "7z-ppmd.7z", "7z-bcj2.7z"};
    for (const char* file : files) {
        char path[160]; snprintf(path, sizeof(path), "test/input/archive/%s", file);
        EXPECT_EQ(open(path, nullptr, ARCHIVE_UNKNOWN), nullptr) << file;
    }
    EXPECT_EQ(open("test/input/archive/legacy.rar", nullptr, ARCHIVE_RAR5), nullptr);
    EXPECT_EQ(open("test/input/archive/sample.tar", nullptr, ARCHIVE_7Z), nullptr);
}

TEST_F(ZipTest, NativeSolidRepeatedDecodingAndQuotaFailuresAreBounded) {
    ArchiveLimits limits = archive_default_limits(); limits.expanded_bytes = 130;
    Archive* archive = open("test/input/archive/7z-solid.7z", &limits, ARCHIVE_7Z);
    ASSERT_NE(archive, nullptr) << error.message;
    ByteSpan bytes = {};
    ASSERT_TRUE(archive_member_bytes(archive, find(archive, "file4"), &bytes, &error)) << error.message;
    EXPECT_EQ(archive->budget->expanded_bytes, 130u);
    ASSERT_TRUE(archive_member_bytes(archive, find(archive, "file4"), &bytes, &error));
    EXPECT_FALSE(archive_member_bytes(archive, find(archive, "file2"), &bytes, &error));
    uint64_t failed_charge = archive->budget->expanded_bytes;
    EXPECT_FALSE(archive_member_bytes(archive, find(archive, "file2"), &bytes, &error));
    EXPECT_GT(failed_charge, 130u);
    EXPECT_EQ(archive->budget->expanded_bytes, failed_charge);
    archive_release(archive);
    // Sweep allocation cutoffs through reader setup/header conversion and clean up every partial reader.
    MemtrackStats before = {}, after = {};
    memtrack_get_stats(&before);
    for (uint64_t cutoff = 128; cutoff <= (1u << 20); cutoff *= 2) {
        limits = archive_default_limits(); limits.index_bytes = cutoff;
        Archive* candidate = open("test/input/archive/sample.tar.gz", &limits, ARCHIVE_TAR);
        archive_release(candidate);
        candidate = open("test/input/archive/7z-lzma2.7z", &limits, ARCHIVE_7Z);
        archive_release(candidate);
    }
    memtrack_get_stats(&after);
    EXPECT_EQ(after.current_bytes, before.current_bytes);
}


TEST_F(ZipTest, NativeRarFiveChecksEmptyStoredMembers) {
    Archive* archive = open("test/input/archive/rar5-empty.rar", nullptr, ARCHIVE_RAR5);
    ASSERT_NE(archive, nullptr) << error.message;
    ByteSpan bytes = {};
    ASSERT_TRUE(archive_member_bytes(archive, find(archive, "empty"), &bytes, &error));
    EXPECT_EQ(bytes.length, 0u);
    archive_release(archive);
    EXPECT_EQ(open("test/input/archive/rar5-empty-bad-crc.rar", nullptr, ARCHIVE_RAR5), nullptr);
}


TEST_F(ZipTest, NativeTreeZipConversionPreservesModeAndRepresentableTime) {
    char* data = nullptr; size_t length = 0;
    ASSERT_TRUE(file_read_all("test/input/archive/sample.tar", MEM_CAT_TEMP, &data, &length));
    Input* input = input_from_source_n(data, length, nullptr, nullptr, nullptr);
    mem_free(data);
    ASSERT_NE(input, nullptr); ASSERT_FALSE(input->parse_failed);
    StrBuf* output = strbuf_new();
    ASSERT_TRUE(fs_zip_encode(input->root, nullptr, output, &error)) << error.message;
    Archive* zip = zip_archive_open(output->str, output->length, nullptr, nullptr, 0, &error);
    ASSERT_NE(zip, nullptr) << error.message;
    ArchiveEntry* entry = archive_entry(zip, find(zip, "hello.txt"));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->mode, 0100640u);
    uint32_t date = ((2023u - 1980) << 9) | (11u << 5) | 14u;
    uint32_t time = (22u << 11) | (13u << 5) | 10u;
    EXPECT_EQ(entry->dos_time, (date << 16) | time);
    archive_release(zip); strbuf_free(output);
}
