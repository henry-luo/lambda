#include <catch2/catch_test_macros.hpp>
#include <cstring>

extern "C" {
#include "../lib/stringbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"
}

// Global memory pool for tests
static VariableMemPool *test_pool = NULL;

// Setup function to initialize memory pool
void setup_stringbuf_tests() {
    if (!test_pool) {
        MemPoolError err = pool_variable_init(&test_pool, 1024 * 1024, 10);
        REQUIRE(err == MEM_POOL_ERR_OK);
    }
}

// Teardown function to cleanup memory pool
void teardown_stringbuf_tests() {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

TEST_CASE("StringBuf Creation", "[stringbuf][creation]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    REQUIRE(sb != nullptr);
    REQUIRE(sb->pool == test_pool);
    REQUIRE(sb->length == 0);
    REQUIRE((sb->str == nullptr || sb->capacity > 0));
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Creation with Capacity", "[stringbuf][creation]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new_cap(test_pool, 100);
    REQUIRE(sb != nullptr);
    REQUIRE(sb->capacity >= sizeof(String) + 100);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->str != nullptr);
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Append String", "[stringbuf][append]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Single append") {
        stringbuf_append_str(sb, "Hello");
        REQUIRE(sb->str != nullptr);
        REQUIRE(sb->str->len == 5);
        REQUIRE(strcmp(sb->str->chars, "Hello") == 0);
    }
    
    SECTION("Multiple appends") {
        stringbuf_append_str(sb, "Hello");
        stringbuf_append_str(sb, " World");
        REQUIRE(sb->str->len == 11);
        REQUIRE(strcmp(sb->str->chars, "Hello World") == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Append Character", "[stringbuf][append]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Single character") {
        stringbuf_append_char(sb, 'A');
        REQUIRE(sb->str != nullptr);
        REQUIRE(sb->str->len == 1);
        REQUIRE(sb->str->chars[0] == 'A');
        REQUIRE(sb->str->chars[1] == '\0');
    }
    
    SECTION("Multiple characters") {
        stringbuf_append_char(sb, 'A');
        stringbuf_append_char(sb, 'B');
        REQUIRE(sb->str->len == 2);
        REQUIRE(strcmp(sb->str->chars, "AB") == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Append String N", "[stringbuf][append]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Partial string append") {
        stringbuf_append_str_n(sb, "Hello World", 5);
        REQUIRE(sb->str != nullptr);
        REQUIRE(sb->str->len == 5);
        REQUIRE(strcmp(sb->str->chars, "Hello") == 0);
    }
    
    SECTION("Multiple partial appends") {
        stringbuf_append_str_n(sb, "Hello World", 5);
        stringbuf_append_str_n(sb, " World!", 6);
        REQUIRE(sb->str->len == 11);
        REQUIRE(strcmp(sb->str->chars, "Hello World") == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Append Character N", "[stringbuf][append]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Multiple same characters") {
        stringbuf_append_char_n(sb, 'X', 3);
        REQUIRE(sb->str != nullptr);
        REQUIRE(sb->str->len == 3);
        REQUIRE(strcmp(sb->str->chars, "XXX") == 0);
    }
    
    SECTION("Append different character sets") {
        stringbuf_append_char_n(sb, 'X', 3);
        stringbuf_append_char_n(sb, 'Y', 2);
        REQUIRE(sb->str->len == 5);
        REQUIRE(strcmp(sb->str->chars, "XXXYY") == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Append Format", "[stringbuf][append][format]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Single format") {
        stringbuf_append_format(sb, "Number: %d", 42);
        REQUIRE(sb->str != nullptr);
        REQUIRE(strcmp(sb->str->chars, "Number: 42") == 0);
    }
    
    SECTION("Multiple formats") {
        stringbuf_append_format(sb, "Number: %d", 42);
        stringbuf_append_format(sb, ", String: %s", "test");
        REQUIRE(strcmp(sb->str->chars, "Number: 42, String: test") == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Append Numbers", "[stringbuf][append][numbers]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Integer append") {
        stringbuf_append_int(sb, 123);
        REQUIRE(sb->str != nullptr);
        REQUIRE(strcmp(sb->str->chars, "123") == 0);
    }
    
    SECTION("Unsigned integer") {
        stringbuf_reset(sb);
        stringbuf_append_format(sb, "%u", 456U);
        REQUIRE(strcmp(sb->str->chars, "456") == 0);
    }
    
    SECTION("Float formatting") {
        stringbuf_reset(sb);
        stringbuf_append_format(sb, "%.2f", 3.14159);
        REQUIRE(strncmp(sb->str->chars, "3.14", 4) == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Reset", "[stringbuf][reset]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello World");
    REQUIRE(sb->str->len == 11);
    
    stringbuf_reset(sb);
    REQUIRE(sb->str->len == 0);
    REQUIRE(sb->str->chars[0] == '\0');
    
    // Should be able to append after reset
    stringbuf_append_str(sb, "New");
    REQUIRE(sb->str->len == 3);
    REQUIRE(strcmp(sb->str->chars, "New") == 0);
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Full Reset", "[stringbuf][reset]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello World");
    
    stringbuf_full_reset(sb);
    REQUIRE(sb->str == nullptr);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->capacity == 0);
    
    // Should be able to append after full reset
    stringbuf_append_str(sb, "New");
    REQUIRE(sb->str != nullptr);
    REQUIRE(sb->str->len == 3);
    REQUIRE(strcmp(sb->str->chars, "New") == 0);
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Copy", "[stringbuf][copy]") {
    setup_stringbuf_tests();
    
    StringBuf *sb1 = stringbuf_new(test_pool);
    StringBuf *sb2 = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb1, "Hello World");
    stringbuf_copy(sb2, sb1);
    
    REQUIRE(sb2->str != nullptr);
    REQUIRE(sb2->str->len == sb1->str->len);
    REQUIRE(strcmp(sb2->str->chars, sb1->str->chars) == 0);
    REQUIRE(sb2->str != sb1->str);
    
    stringbuf_free(sb1);
    stringbuf_free(sb2);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Duplicate", "[stringbuf][dup]") {
    setup_stringbuf_tests();
    
    StringBuf *sb1 = stringbuf_new(test_pool);
    stringbuf_append_str(sb1, "Hello World");
    
    StringBuf *sb2 = stringbuf_dup(sb1);
    REQUIRE(sb2 != nullptr);
    REQUIRE(sb2->str != nullptr);
    REQUIRE(sb2->str->len == sb1->str->len);
    REQUIRE(strcmp(sb2->str->chars, sb1->str->chars) == 0);
    REQUIRE(sb2->str != sb1->str);
    REQUIRE(sb2->pool == sb1->pool);
    
    stringbuf_free(sb1);
    stringbuf_free(sb2);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf To String", "[stringbuf][conversion]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    stringbuf_append_str(sb, "Hello World");
    
    String *str = stringbuf_to_string(sb);
    REQUIRE(str != nullptr);
    REQUIRE(str->len == 11);
    REQUIRE(strcmp(str->chars, "Hello World") == 0);
    
    // Buffer should be reset after to_string (str pointer becomes NULL)
    REQUIRE(sb->str == nullptr);
    REQUIRE(sb->length == 0);
    REQUIRE(sb->capacity == 0);
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Capacity Growth", "[stringbuf][capacity]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new_cap(test_pool, 10);
    size_t initial_capacity = sb->capacity;
    
    // Append enough data to force growth
    for (int i = 0; i < 100; i++) {
        stringbuf_append_char(sb, 'A');
    }
    
    REQUIRE(sb->capacity > initial_capacity);
    REQUIRE(sb->str->len == 100);
    
    // Verify content
    bool all_a = true;
    for (int i = 0; i < 100; i++) {
        if (sb->str->chars[i] != 'A') {
            all_a = false;
            break;
        }
    }
    REQUIRE(all_a);
    REQUIRE(sb->str->chars[100] == '\0');
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}

TEST_CASE("StringBuf Edge Cases", "[stringbuf][edge_cases]") {
    setup_stringbuf_tests();
    
    StringBuf *sb = stringbuf_new(test_pool);
    
    SECTION("Empty string append") {
        stringbuf_append_str(sb, "");
        REQUIRE(sb->str->len == 0);
    }
    
    SECTION("Zero character append") {
        stringbuf_append_char_n(sb, 'X', 0);
        REQUIRE(sb->str->len == 0);
    }
    
    SECTION("Zero length string append") {
        stringbuf_append_str_n(sb, "Hello", 0);
        REQUIRE(sb->str->len == 0);
    }
    
    stringbuf_free(sb);
    teardown_stringbuf_tests();
}
