/**
 * GTest-based test suite for namespace infrastructure
 *
 * Tests the foundational namespace support added to Lambda:
 * - Symbol struct with Target* ns field
 * - Target struct with url_hash, target_equal(), target_compute_hash()
 * - Name struct with name_equal()
 * - ShapeEntry and TypeElmt ns fields
 * - get_chars() / get_len() type-safe accessors for Symbol vs String
 * - MarkBuilder symbol creation (ns = nullptr for unqualified symbols)
 */

#include <gtest/gtest.h>
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/lambda.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/name_pool.hpp"
#include "../lambda/shape_pool.hpp"
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"
#include "../lib/url.h"
#include <cstring>
#include <cstdlib>

// Local item_to_target for testing - the real one is excluded under SIMPLE_SCHEMA_PARSER
// because it depends on item_type_id() from the full runtime. This version uses Item::type_id().
static Target* test_item_to_target(uint64_t raw, Url* cwd) {
    Item it; it.item = raw;
    TypeId type_id = it.type_id();

    Target* target = (Target*)calloc(1, sizeof(Target));
    if (!target) return nullptr;

    if (type_id == LMD_TYPE_STRING || type_id == LMD_TYPE_SYMBOL) {
        const char* url_str;
        if (type_id == LMD_TYPE_SYMBOL) {
            Symbol* sym = (Symbol*)(raw & 0x00FFFFFFFFFFFFFFULL);
            if (!sym) { free(target); return nullptr; }
            url_str = sym->chars;
        } else {
            String* str = (String*)(raw & 0x00FFFFFFFFFFFFFFULL);
            if (!str) { free(target); return nullptr; }
            url_str = str->chars;
        }
        target->original = url_str;
        Url* url = cwd ? url_parse_with_base(url_str, cwd) : url_parse(url_str);
        if (!url) { free(target); return nullptr; }
        target->type = TARGET_TYPE_URL;
        target->url = url;
        // use the Url's parsed scheme enum to set TargetScheme
        switch (url->scheme) {
            case URL_SCHEME_HTTP:  target->scheme = TARGET_SCHEME_HTTP; break;
            case URL_SCHEME_HTTPS: target->scheme = TARGET_SCHEME_HTTPS; break;
            case URL_SCHEME_FILE:  target->scheme = TARGET_SCHEME_FILE; break;
            default: target->scheme = TARGET_SCHEME_UNKNOWN; break;
        }
        // compute hash from href string using hashmap_sip (matching target.cpp)
        if (url->href && url->href->chars) {
            target->url_hash = hashmap_sip(url->href->chars, strlen(url->href->chars),
                                           0x12AE406AB1E59A3CULL, 0x7F4A519D3E2B8C01ULL);
        }
        return target;
    }
    free(target);
    return nullptr;
}

// Redirect item_to_target calls in tests to our local version
#define item_to_target test_item_to_target

// ============================================================================
// Test fixture
// ============================================================================

class NamespaceTest : public ::testing::Test {
protected:
    Input* input;
    Pool* mem_pool;

    void SetUp() override {
        log_init(NULL);
        input = InputManager::create_input(nullptr);
        mem_pool = pool_create();
    }

    void TearDown() override {
        // InputManager handles cleanup
        if (mem_pool) pool_destroy(mem_pool);
    }
};

// ============================================================================
// 1. Symbol struct memory layout
// ============================================================================

// verify sizeof(Symbol) includes the ns pointer between ref_cnt and chars
TEST_F(NamespaceTest, SymbolStructLayout) {
    // Symbol has: uint32_t (len+ref_cnt) + Target* ns + flexible array chars[]
    // On 64-bit: 4 bytes + 8 bytes = 12 bytes before chars[]
    EXPECT_EQ(sizeof(Symbol), 16u);  // 4 + padding/alignment to 8 + 8 for ns, or 4+4(pad)+8
    // the key check: Symbol is larger than String due to ns field
    EXPECT_GT(sizeof(Symbol), sizeof(String));
}

// verify that String does NOT have the ns field
TEST_F(NamespaceTest, StringStructLayout) {
    // String has: uint32_t (len+ref_cnt) + flexible array chars[]
    // On 64-bit: 4 bytes before chars[]
    EXPECT_EQ(sizeof(String), 4u);
}

// ============================================================================
// 2. Symbol creation via MarkBuilder
// ============================================================================

// basic symbol creation sets ns to nullptr
TEST_F(NamespaceTest, CreateSymbol_NsIsNull) {
    MarkBuilder builder(input);
    Symbol* sym = builder.createSymbol("hello");

    ASSERT_NE(sym, nullptr);
    EXPECT_STREQ(sym->chars, "hello");
    EXPECT_EQ(sym->len, 5u);
    EXPECT_EQ(sym->ref_cnt, 1u);
    EXPECT_EQ(sym->ns, nullptr);  // unqualified symbol
}

// symbol via createSymbolItem wraps in Item with correct type tag
TEST_F(NamespaceTest, CreateSymbolItem_TypeTag) {
    MarkBuilder builder(input);
    Item sym_item = builder.createSymbolItem("world");

    ASSERT_EQ(get_type_id(sym_item), LMD_TYPE_SYMBOL);
    Symbol* sym = sym_item.get_symbol();
    ASSERT_NE(sym, nullptr);
    EXPECT_STREQ(sym->chars, "world");
    EXPECT_EQ(sym->ns, nullptr);
}

// symbol via createNameItem also uses Symbol (not String)
TEST_F(NamespaceTest, CreateNameItem_IsSymbol) {
    MarkBuilder builder(input);
    Item name_item = builder.createNameItem("tag_name");

    ASSERT_EQ(get_type_id(name_item), LMD_TYPE_SYMBOL);
    Symbol* sym = name_item.get_symbol();
    ASSERT_NE(sym, nullptr);
    EXPECT_STREQ(sym->chars, "tag_name");
    EXPECT_EQ(sym->ns, nullptr);
}

// empty symbol returns null item
TEST_F(NamespaceTest, CreateSymbolItem_Empty) {
    MarkBuilder builder(input);
    Item sym_item = builder.createSymbolItem("");
    EXPECT_EQ(get_type_id(sym_item), LMD_TYPE_NULL);
}

// symbol with length parameter
TEST_F(NamespaceTest, CreateSymbol_WithLength) {
    MarkBuilder builder(input);
    Symbol* sym = builder.createSymbol("hello_world", 5);  // only "hello"

    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->len, 5u);
    EXPECT_EQ(strncmp(sym->chars, "hello", 5), 0);
    EXPECT_EQ(sym->chars[5], '\0');
    EXPECT_EQ(sym->ns, nullptr);
}

// ============================================================================
// 3. get_chars() / get_len() type-safe accessors
// ============================================================================

// get_chars/get_len on String items access chars at offset 4
TEST_F(NamespaceTest, GetChars_String) {
    MarkBuilder builder(input);
    Item str_item = builder.createStringItem("hello");

    ASSERT_EQ(get_type_id(str_item), LMD_TYPE_STRING);
    EXPECT_STREQ(str_item.get_chars(), "hello");
    EXPECT_EQ(str_item.get_len(), 5u);

    // verify get_string() also works
    String* str = str_item.get_string();
    EXPECT_STREQ(str->chars, "hello");
    EXPECT_EQ(str->len, 5u);
}

// get_chars/get_len on Symbol items access chars at offset 12 (past ns pointer)
TEST_F(NamespaceTest, GetChars_Symbol) {
    MarkBuilder builder(input);
    Item sym_item = builder.createSymbolItem("world");

    ASSERT_EQ(get_type_id(sym_item), LMD_TYPE_SYMBOL);
    EXPECT_STREQ(sym_item.get_chars(), "world");
    EXPECT_EQ(sym_item.get_len(), 5u);

    // verify get_symbol() also works
    Symbol* sym = sym_item.get_symbol();
    EXPECT_STREQ(sym->chars, "world");
    EXPECT_EQ(sym->len, 5u);
}

// get_chars/get_len returns correct chars for both types (distinguishes layout)
TEST_F(NamespaceTest, GetChars_DistinguishesStringAndSymbol) {
    MarkBuilder builder(input);
    Item str_item = builder.createStringItem("string_val");
    Item sym_item = builder.createSymbolItem("symbol_val");

    // both return correct chars despite different memory layouts
    EXPECT_STREQ(str_item.get_chars(), "string_val");
    EXPECT_STREQ(sym_item.get_chars(), "symbol_val");
    EXPECT_EQ(str_item.get_len(), 10u);
    EXPECT_EQ(sym_item.get_len(), 10u);

    // but types are different
    EXPECT_EQ(get_type_id(str_item), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(sym_item), LMD_TYPE_SYMBOL);
}

// get_chars on a symbol with long name (beyond NAME_POOL_SYMBOL_LIMIT)
TEST_F(NamespaceTest, GetChars_LongSymbol) {
    MarkBuilder builder(input);
    // 40 chars, exceeds NAME_POOL_SYMBOL_LIMIT (32)
    const char* long_name = "this_is_a_very_long_symbol_name_over_32c";
    Item sym_item = builder.createSymbolItem(long_name);

    ASSERT_EQ(get_type_id(sym_item), LMD_TYPE_SYMBOL);
    EXPECT_STREQ(sym_item.get_chars(), long_name);
    EXPECT_EQ(sym_item.get_len(), strlen(long_name));
}

// ============================================================================
// 4. Target struct and target_equal()
// ============================================================================

// target created from a string item has valid hash
TEST_F(NamespaceTest, Target_FromStringItem) {
    MarkBuilder builder(input);
    Item str = builder.createStringItem("https://example.com/ns");

    Target* target = item_to_target(str.item, nullptr);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->scheme, TARGET_SCHEME_HTTPS);
    EXPECT_NE(target->url_hash, 0u);

    target_free(target);
}

// two targets from same URL are equal
TEST_F(NamespaceTest, Target_EqualSameUrl) {
    MarkBuilder builder(input);
    Item str1 = builder.createStringItem("https://example.com/ns");
    Item str2 = builder.createStringItem("https://example.com/ns");

    Target* t1 = item_to_target(str1.item, nullptr);
    Target* t2 = item_to_target(str2.item, nullptr);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);

    // same URL → same hash → target_equal
    EXPECT_TRUE(target_equal(t1, t2));
    EXPECT_EQ(t1->url_hash, t2->url_hash);

    target_free(t1);
    target_free(t2);
}

// two targets from different URLs are not equal
TEST_F(NamespaceTest, Target_NotEqualDifferentUrl) {
    MarkBuilder builder(input);
    Item str1 = builder.createStringItem("https://example.com/ns1");
    Item str2 = builder.createStringItem("https://example.com/ns2");

    Target* t1 = item_to_target(str1.item, nullptr);
    Target* t2 = item_to_target(str2.item, nullptr);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);

    EXPECT_FALSE(target_equal(t1, t2));
    EXPECT_NE(t1->url_hash, t2->url_hash);

    target_free(t1);
    target_free(t2);
}

// target_equal with NULL arguments
TEST_F(NamespaceTest, Target_EqualNull) {
    // both null → equal (same pointer shortcut)
    EXPECT_TRUE(target_equal(nullptr, nullptr));

    MarkBuilder builder(input);
    Item str = builder.createStringItem("https://example.com");
    Target* t = item_to_target(str.item, nullptr);
    ASSERT_NE(t, nullptr);

    // one null, one not → not equal
    EXPECT_FALSE(target_equal(t, nullptr));
    EXPECT_FALSE(target_equal(nullptr, t));

    target_free(t);
}

// target_equal same pointer → equal
TEST_F(NamespaceTest, Target_EqualSamePointer) {
    MarkBuilder builder(input);
    Item str = builder.createStringItem("https://example.com");
    Target* t = item_to_target(str.item, nullptr);
    ASSERT_NE(t, nullptr);

    EXPECT_TRUE(target_equal(t, t));

    target_free(t);
}

// target from symbol item (not just string)
TEST_F(NamespaceTest, Target_FromSymbolItem) {
    MarkBuilder builder(input);
    Item sym = builder.createSymbolItem("https://example.com/ns");

    Target* target = item_to_target(sym.item, nullptr);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->scheme, TARGET_SCHEME_HTTPS);
    EXPECT_NE(target->url_hash, 0u);

    // same URL from string and symbol should produce same hash
    Item str = builder.createStringItem("https://example.com/ns");
    Target* t2 = item_to_target(str.item, nullptr);
    ASSERT_NE(t2, nullptr);
    EXPECT_TRUE(target_equal(target, t2));

    target_free(target);
    target_free(t2);
}

// target scheme detection
TEST_F(NamespaceTest, Target_SchemeDetection) {
    MarkBuilder builder(input);

    // http
    Item http = builder.createStringItem("http://example.com");
    Target* t_http = item_to_target(http.item, nullptr);
    ASSERT_NE(t_http, nullptr);
    EXPECT_EQ(t_http->scheme, TARGET_SCHEME_HTTP);
    target_free(t_http);

    // https
    Item https = builder.createStringItem("https://example.com");
    Target* t_https = item_to_target(https.item, nullptr);
    ASSERT_NE(t_https, nullptr);
    EXPECT_EQ(t_https->scheme, TARGET_SCHEME_HTTPS);
    target_free(t_https);

    // file
    Item file = builder.createStringItem("file:///tmp/test.ls");
    Target* t_file = item_to_target(file.item, nullptr);
    ASSERT_NE(t_file, nullptr);
    EXPECT_EQ(t_file->scheme, TARGET_SCHEME_FILE);
    target_free(t_file);
}

// target_free on null is safe
TEST_F(NamespaceTest, Target_FreeNull) {
    target_free(nullptr);  // should not crash
}

// ============================================================================
// 5. Name struct and name_equal()
// ============================================================================

// two names with same interned string and no ns are equal
TEST_F(NamespaceTest, NameEqual_SameNameNoNs) {
    NamePool* np = name_pool_create(mem_pool, nullptr);

    String* s1 = name_pool_create_len(np, "field", 5);
    String* s2 = name_pool_create_len(np, "field", 5);
    // interned → same pointer
    EXPECT_EQ(s1, s2);

    Name n1 = { s1, nullptr };
    Name n2 = { s2, nullptr };
    EXPECT_TRUE(name_equal(&n1, &n2));

    name_pool_release(np);
}

// two names with different interned strings are not equal
TEST_F(NamespaceTest, NameEqual_DifferentName) {
    NamePool* np = name_pool_create(mem_pool, nullptr);

    String* s1 = name_pool_create_len(np, "alpha", 5);
    String* s2 = name_pool_create_len(np, "bravo", 5);
    EXPECT_NE(s1, s2);

    Name n1 = { s1, nullptr };
    Name n2 = { s2, nullptr };
    EXPECT_FALSE(name_equal(&n1, &n2));

    name_pool_release(np);
}

// same name, same ns → equal
TEST_F(NamespaceTest, NameEqual_SameNameSameNs) {
    NamePool* np = name_pool_create(mem_pool, nullptr);
    MarkBuilder builder(input);

    String* s1 = name_pool_create_len(np, "field", 5);
    String* s2 = name_pool_create_len(np, "field", 5);

    Item url_item = builder.createStringItem("https://example.com/ns");
    Target* ns = item_to_target(url_item.item, nullptr);
    ASSERT_NE(ns, nullptr);

    // same ns pointer
    Name n1 = { s1, ns };
    Name n2 = { s2, ns };
    EXPECT_TRUE(name_equal(&n1, &n2));

    target_free(ns);
    name_pool_release(np);
}

// same name, different ns targets with same URL → equal (via target_equal)
TEST_F(NamespaceTest, NameEqual_SameNameEqualNs) {
    NamePool* np = name_pool_create(mem_pool, nullptr);
    MarkBuilder builder(input);

    String* s = name_pool_create_len(np, "field", 5);

    Item url1 = builder.createStringItem("https://example.com/ns");
    Item url2 = builder.createStringItem("https://example.com/ns");
    Target* ns1 = item_to_target(url1.item, nullptr);
    Target* ns2 = item_to_target(url2.item, nullptr);
    ASSERT_NE(ns1, nullptr);
    ASSERT_NE(ns2, nullptr);
    ASSERT_NE(ns1, ns2);  // different pointers

    Name n1 = { s, ns1 };
    Name n2 = { s, ns2 };
    EXPECT_TRUE(name_equal(&n1, &n2));

    target_free(ns1);
    target_free(ns2);
    name_pool_release(np);
}

// same name, different ns URLs → not equal
TEST_F(NamespaceTest, NameEqual_SameNameDifferentNs) {
    NamePool* np = name_pool_create(mem_pool, nullptr);
    MarkBuilder builder(input);

    String* s = name_pool_create_len(np, "field", 5);

    Item url1 = builder.createStringItem("https://example.com/ns1");
    Item url2 = builder.createStringItem("https://example.com/ns2");
    Target* ns1 = item_to_target(url1.item, nullptr);
    Target* ns2 = item_to_target(url2.item, nullptr);
    ASSERT_NE(ns1, nullptr);
    ASSERT_NE(ns2, nullptr);

    Name n1 = { s, ns1 };
    Name n2 = { s, ns2 };
    EXPECT_FALSE(name_equal(&n1, &n2));

    target_free(ns1);
    target_free(ns2);
    name_pool_release(np);
}

// one name has ns, other doesn't → not equal
TEST_F(NamespaceTest, NameEqual_OneHasNs) {
    NamePool* np = name_pool_create(mem_pool, nullptr);
    MarkBuilder builder(input);

    String* s = name_pool_create_len(np, "field", 5);

    Item url = builder.createStringItem("https://example.com/ns");
    Target* ns = item_to_target(url.item, nullptr);
    ASSERT_NE(ns, nullptr);

    Name n1 = { s, ns };
    Name n2 = { s, nullptr };
    EXPECT_FALSE(name_equal(&n1, &n2));
    EXPECT_FALSE(name_equal(&n2, &n1));

    target_free(ns);
    name_pool_release(np);
}

// null Name pointers
TEST_F(NamespaceTest, NameEqual_NullPointers) {
    EXPECT_TRUE(name_equal(nullptr, nullptr));

    Name n = { nullptr, nullptr };
    EXPECT_FALSE(name_equal(&n, nullptr));
    EXPECT_FALSE(name_equal(nullptr, &n));
}

// same pointer → equal (identity shortcut)
TEST_F(NamespaceTest, NameEqual_SamePointer) {
    NamePool* np = name_pool_create(mem_pool, nullptr);
    String* s = name_pool_create_len(np, "x", 1);
    Name n = { s, nullptr };
    EXPECT_TRUE(name_equal(&n, &n));
    name_pool_release(np);
}

// ============================================================================
// 6. Symbol ns field manual assignment
// ============================================================================

// manually set ns on a symbol and verify it's preserved
TEST_F(NamespaceTest, Symbol_ManualNsAssignment) {
    MarkBuilder builder(input);

    Symbol* sym = builder.createSymbol("element");
    ASSERT_NE(sym, nullptr);
    EXPECT_EQ(sym->ns, nullptr);

    // manually attach a namespace target
    Item url = builder.createStringItem("https://example.com/ns");
    Target* ns = item_to_target(url.item, nullptr);
    ASSERT_NE(ns, nullptr);

    sym->ns = ns;
    EXPECT_EQ(sym->ns, ns);
    EXPECT_TRUE(target_equal(sym->ns, ns));

    // chars still accessible after ns is set
    EXPECT_STREQ(sym->chars, "element");
    EXPECT_EQ(sym->len, 7u);

    // verify get_chars/get_len work on Item wrapping this symbol
    Item sym_item;
    sym_item.item = y2it(sym);
    EXPECT_STREQ(sym_item.get_chars(), "element");
    EXPECT_EQ(sym_item.get_len(), 7u);

    target_free(ns);
}

// two symbols with same name but different ns are distinguishable
TEST_F(NamespaceTest, Symbol_DifferentNsDistinguishable) {
    MarkBuilder builder(input);

    Symbol* sym1 = builder.createSymbol("title");
    Symbol* sym2 = builder.createSymbol("title");

    Item url1 = builder.createStringItem("https://ns1.example.com");
    Item url2 = builder.createStringItem("https://ns2.example.com");
    Target* ns1 = item_to_target(url1.item, nullptr);
    Target* ns2 = item_to_target(url2.item, nullptr);

    sym1->ns = ns1;
    sym2->ns = ns2;

    // same local name
    EXPECT_STREQ(sym1->chars, sym2->chars);
    // different namespace
    EXPECT_FALSE(target_equal(sym1->ns, sym2->ns));

    target_free(ns1);
    target_free(ns2);
}

// ============================================================================
// 7. ShapeEntry ns field
// ============================================================================

TEST_F(NamespaceTest, ShapeEntry_NsField) {
    // verify ShapeEntry struct has ns field
    ShapeEntry entry = {};
    entry.ns = nullptr;
    EXPECT_EQ(entry.ns, nullptr);

    // set a namespace target on a shape entry
    MarkBuilder builder(input);
    Item url = builder.createStringItem("https://schema.org");
    Target* ns = item_to_target(url.item, nullptr);
    ASSERT_NE(ns, nullptr);

    entry.ns = ns;
    EXPECT_EQ(entry.ns, ns);
    EXPECT_TRUE(target_equal(entry.ns, ns));

    target_free(ns);
}

// ============================================================================
// 8. TypeElmt ns field
// ============================================================================

TEST_F(NamespaceTest, TypeElmt_NsField) {
    TypeElmt elmt = {};
    elmt.type_id = LMD_TYPE_ELEMENT;
    elmt.ns = nullptr;
    EXPECT_EQ(elmt.ns, nullptr);

    // set namespace
    MarkBuilder builder(input);
    Item url = builder.createStringItem("https://www.w3.org/1999/xhtml");
    Target* ns = item_to_target(url.item, nullptr);
    ASSERT_NE(ns, nullptr);

    elmt.ns = ns;
    EXPECT_EQ(elmt.ns, ns);
    EXPECT_NE(elmt.ns->url_hash, 0u);

    target_free(ns);
}

// ============================================================================
// 9. Symbol vs String chars offset correctness
// ============================================================================

// the critical test: ensure chars are at different byte offsets for Symbol vs String
TEST_F(NamespaceTest, CharsOffset_SymbolVsString) {
    MarkBuilder builder(input);

    String* str = builder.createString("test");
    Symbol* sym = builder.createSymbol("test");

    ASSERT_NE(str, nullptr);
    ASSERT_NE(sym, nullptr);

    // compute actual byte offsets of chars[]
    ptrdiff_t str_offset = (char*)str->chars - (char*)str;
    ptrdiff_t sym_offset = (char*)sym->chars - (char*)sym;

    // String: chars at offset 4 (after uint32_t len:22/ref_cnt:10)
    EXPECT_EQ(str_offset, 4);

    // Symbol: chars at offset 12 (after uint32_t len:22/ref_cnt:10 + Target* ns)
    // On 64-bit with alignment: uint32_t(4) + padding(4) + Target*(8) = 16,
    // or uint32_t(4) + Target*(8) = 12 if packed
    // the actual offset depends on compiler alignment
    EXPECT_GT(sym_offset, str_offset);

    // both should still contain the same characters
    EXPECT_STREQ(str->chars, "test");
    EXPECT_STREQ(sym->chars, "test");
}

// ============================================================================
// 10. Target url_hash consistency
// ============================================================================

// same URL parsed multiple times produces same hash
TEST_F(NamespaceTest, Target_HashConsistency) {
    MarkBuilder builder(input);
    const char* url_str = "https://example.com/namespace/v1";

    uint64_t hashes[5];
    for (int i = 0; i < 5; i++) {
        Item s = builder.createStringItem(url_str);
        Target* t = item_to_target(s.item, nullptr);
        ASSERT_NE(t, nullptr);
        hashes[i] = t->url_hash;
        target_free(t);
    }

    // all hashes should be identical
    for (int i = 1; i < 5; i++) {
        EXPECT_EQ(hashes[0], hashes[i]);
    }
}

// different URLs produce different hashes (statistical check)
TEST_F(NamespaceTest, Target_HashUniqueness) {
    MarkBuilder builder(input);
    const char* urls[] = {
        "https://example.com/ns/a",
        "https://example.com/ns/b",
        "https://example.com/ns/c",
        "http://other.org/schema",
        "file:///tmp/local.ls"
    };

    uint64_t hashes[5];
    for (int i = 0; i < 5; i++) {
        Item s = builder.createStringItem(urls[i]);
        Target* t = item_to_target(s.item, nullptr);
        ASSERT_NE(t, nullptr);
        hashes[i] = t->url_hash;
        target_free(t);
    }

    // all hashes should be distinct
    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            EXPECT_NE(hashes[i], hashes[j])
                << "hash collision between URLs[" << i << "] and URLs[" << j << "]";
        }
    }
}

// ============================================================================
// 11. Element creation with tag name uses Symbol
// ============================================================================

TEST_F(NamespaceTest, Element_TagNameIsSymbol) {
    MarkBuilder builder(input);

    // create an element with a tag name via the builder
    auto eb = builder.element("div");
    Item elmt_item = eb.final();

    ASSERT_EQ(get_type_id(elmt_item), LMD_TYPE_ELEMENT);

    // the element's tag name should be accessible
    Element* elmt = elmt_item.element;
    ASSERT_NE(elmt, nullptr);

    // read tag name via ElementReader
    MarkReader doc(elmt_item);
    ItemReader root = doc.getRoot();
    ASSERT_TRUE(root.isElement());
    ElementReader elem_reader = root.asElement();
    EXPECT_STREQ(elem_reader.tagName(), "div");
}

// ============================================================================
// 12. y2it / s2it macros produce correct type tags
// ============================================================================

TEST_F(NamespaceTest, TagMacros_Y2itVsS2it) {
    MarkBuilder builder(input);

    String* str = builder.createString("abc");
    Symbol* sym = builder.createSymbol("abc");

    Item str_item;
    str_item.item = s2it(str);
    Item sym_item;
    sym_item.item = y2it(sym);

    EXPECT_EQ(get_type_id(str_item), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(sym_item), LMD_TYPE_SYMBOL);

    // both should return same chars
    EXPECT_STREQ(str_item.get_chars(), "abc");
    EXPECT_STREQ(sym_item.get_chars(), "abc");
}

// null pointers produce null items
TEST_F(NamespaceTest, TagMacros_NullPtrs) {
    Item str_null;
    str_null.item = s2it(nullptr);
    Item sym_null;
    sym_null.item = y2it(nullptr);

    EXPECT_EQ(get_type_id(str_null), LMD_TYPE_NULL);
    EXPECT_EQ(get_type_id(sym_null), LMD_TYPE_NULL);
}
