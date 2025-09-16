#include "../lib/unit_test/include/criterion/criterion.h"
#include "../lib/stringbuf.h"
#include "../lib/mem-pool/include/mem_pool.h"

// Global memory pool for tests
static VariableMemPool *test_pool = NULL;

void setup(void) {
    MemPoolError err = pool_variable_init(&test_pool, 1024 * 1024, 10);
    cr_assert_eq(err, MEM_POOL_ERR_OK, "Failed to create memory pool");
}

void teardown(void) {
    if (test_pool) {
        pool_variable_destroy(test_pool);
        test_pool = NULL;
    }
}

Test(stringbuf_tests, test_stringbuf_creation, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    cr_assert_not_null(sb, "stringbuf_new should return non-NULL");
    cr_assert_eq(sb->pool, test_pool, "pool should be set correctly");
    cr_assert_eq(sb->length, 0, "initial length should be 0");
    cr_assert(sb->str == NULL || sb->capacity > 0, "str should be NULL or capacity > 0");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_creation_with_capacity, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 100);
    cr_assert_not_null(sb, "stringbuf_new_cap should return non-NULL");
    cr_assert_geq(sb->capacity, sizeof(String) + 100, "capacity should be at least requested + String header");
    cr_assert_eq(sb->length, 0, "initial length should be 0");
    cr_assert_not_null(sb->str, "str should be allocated");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_append_str, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello");
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_eq(sb->str->len, 5, "length should be 5 after appending 'Hello'");
    cr_assert_str_eq(sb->str->chars, "Hello", "content should be 'Hello'");
    
    stringbuf_append_str(sb, " World");
    cr_assert_eq(sb->str->len, 11, "length should be 11 after appending ' World'");
    cr_assert_str_eq(sb->str->chars, "Hello World", "content should be 'Hello World'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_append_char, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_char(sb, 'A');
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_eq(sb->str->len, 1, "length should be 1 after appending 'A'");
    cr_assert_eq(sb->str->chars[0], 'A', "first character should be 'A'");
    cr_assert_eq(sb->str->chars[1], '\0', "should be null terminated");
    
    stringbuf_append_char(sb, 'B');
    cr_assert_eq(sb->str->len, 2, "length should be 2 after appending 'B'");
    cr_assert_str_eq(sb->str->chars, "AB", "content should be 'AB'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_append_str_n, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str_n(sb, "Hello World", 5);
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_eq(sb->str->len, 5, "length should be 5 after appending first 5 chars");
    cr_assert_str_eq(sb->str->chars, "Hello", "content should be 'Hello'");
    
    stringbuf_append_str_n(sb, " World!", 6);
    cr_assert_eq(sb->str->len, 11, "length should be 11 after appending ' World'");
    cr_assert_str_eq(sb->str->chars, "Hello World", "content should be 'Hello World'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_append_char_n, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_char_n(sb, 'X', 3);
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_eq(sb->str->len, 3, "length should be 3 after appending 3 'X's");
    cr_assert_str_eq(sb->str->chars, "XXX", "content should be 'XXX'");
    
    stringbuf_append_char_n(sb, 'Y', 2);
    cr_assert_eq(sb->str->len, 5, "length should be 5 after appending 2 'Y's");
    cr_assert_str_eq(sb->str->chars, "XXXYY", "content should be 'XXXYY'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_append_format, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_format(sb, "Number: %d", 42);
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_str_eq(sb->str->chars, "Number: 42", "content should be 'Number: 42'");
    
    stringbuf_append_format(sb, ", String: %s", "test");
    cr_assert_str_eq(sb->str->chars, "Number: 42, String: test", "content should include both parts");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_append_numbers, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_int(sb, 123);
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_str_eq(sb->str->chars, "123", "content should be '123'");
    
    stringbuf_reset(sb);
    stringbuf_append_format(sb, "%u", 456U);
    cr_assert_str_eq(sb->str->chars, "456", "content should be '456'");
    
    stringbuf_reset(sb);
    stringbuf_append_format(sb, "%.2f", 3.14159);
    cr_assert(strncmp(sb->str->chars, "3.14", 4) == 0, "content should start with '3.14'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_reset, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello World");
    cr_assert_eq(sb->str->len, 11, "length should be 11 before reset");
    
    stringbuf_reset(sb);
    cr_assert_eq(sb->str->len, 0, "length should be 0 after reset");
    cr_assert_eq(sb->str->chars[0], '\0', "should be null terminated after reset");
    
    // Should be able to append after reset
    stringbuf_append_str(sb, "New");
    cr_assert_eq(sb->str->len, 3, "length should be 3 after appending to reset buffer");
    cr_assert_str_eq(sb->str->chars, "New", "content should be 'New'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_full_reset, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    
    stringbuf_append_str(sb, "Hello World");
    
    stringbuf_full_reset(sb);
    cr_assert_null(sb->str, "str should be NULL after full reset");
    cr_assert_eq(sb->length, 0, "length should be 0 after full reset");
    cr_assert_eq(sb->capacity, 0, "capacity should be 0 after full reset");
    
    // Should be able to append after full reset
    stringbuf_append_str(sb, "New");
    cr_assert_not_null(sb->str, "str should be allocated after append");
    cr_assert_eq(sb->str->len, 3, "length should be 3");
    cr_assert_str_eq(sb->str->chars, "New", "content should be 'New'");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_copy, .init = setup, .fini = teardown) {
    StringBuf *sb1 = stringbuf_new(test_pool);
    StringBuf *sb2 = stringbuf_new(test_pool);
    
    cr_assert_neq(sb1, NULL, "sb1 creation should succeed");
    cr_assert_neq(sb2, NULL, "sb2 creation should succeed");
    
    stringbuf_append_str(sb1, "Hello World");
    cr_assert_neq(sb1->str, NULL, "sb1 str should be allocated after append");
    
    stringbuf_copy(sb2, sb1);
    
    cr_assert_not_null(sb2->str, "destination str should be allocated");
    cr_assert_eq(sb2->str->len, sb1->str->len, "lengths should match");
    cr_assert_str_eq(sb2->str->chars, sb1->str->chars, "contents should match");
    cr_assert_neq(sb2->str, sb1->str, "should be different String objects");
    
    stringbuf_free(sb1);
    stringbuf_free(sb2);
}

Test(stringbuf_tests, test_stringbuf_dup, .init = setup, .fini = teardown) {
    StringBuf *sb1 = stringbuf_new(test_pool);
    stringbuf_append_str(sb1, "Hello World");
    
    StringBuf *sb2 = stringbuf_dup(sb1);
    cr_assert_not_null(sb2, "dup should return non-NULL");
    cr_assert_not_null(sb2->str, "dup str should be allocated");
    cr_assert_eq(sb2->str->len, sb1->str->len, "lengths should match");
    cr_assert_str_eq(sb2->str->chars, sb1->str->chars, "contents should match");
    cr_assert_neq(sb2->str, sb1->str, "should be different String objects");
    cr_assert_eq(sb2->pool, sb1->pool, "pools should match");
    
    stringbuf_free(sb1);
    stringbuf_free(sb2);
}

Test(stringbuf_tests, test_stringbuf_to_string, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    stringbuf_append_str(sb, "Hello World");
    
    String *str = stringbuf_to_string(sb);
    cr_assert_not_null(str, "to_string should return non-NULL");
    cr_assert_eq(str->len, 11, "string length should be 11");
    cr_assert_str_eq(str->chars, "Hello World", "string content should be 'Hello World'");
    
    // Buffer should be reset after to_string (str pointer becomes NULL)
    cr_assert_null(sb->str, "buffer str should be NULL after to_string");
    cr_assert_eq(sb->length, 0, "buffer length should be 0 after to_string");
    cr_assert_eq(sb->capacity, 0, "buffer capacity should be 0 after to_string");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_capacity_growth, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 10);
    cr_assert_neq(sb, NULL, "StringBuf creation should succeed");
    size_t initial_capacity = sb->capacity;
    
    // Append enough data to force growth
    for (int i = 0; i < 100; i++) {
        stringbuf_append_char(sb, 'A');
    }
    
    cr_assert_gt(sb->capacity, initial_capacity, "capacity should have grown");
    cr_assert_eq(sb->str->len, 100, "length should be 100");
    
    // Verify content
    bool all_a = true;
    for (int i = 0; i < 100; i++) {
        if (sb->str->chars[i] != 'A') {
            all_a = false;
            break;
        }
    }
    cr_assert(all_a, "all characters should be 'A'");
    cr_assert_eq(sb->str->chars[100], '\0', "should be null terminated");
    
    stringbuf_free(sb);
}

Test(stringbuf_tests, test_stringbuf_edge_cases, .init = setup, .fini = teardown) {
    StringBuf *sb = stringbuf_new(test_pool);
    cr_assert_neq(sb, NULL, "StringBuf creation should succeed");
    
    // Test empty string append
    stringbuf_append_str(sb, "");
    cr_assert_neq(sb->str, NULL, "StringBuf should have valid string after append");
    cr_assert_eq(sb->str->len, 0, "empty string append should not change length");
    
    // Test zero character append
    stringbuf_append_char_n(sb, 'X', 0);
    cr_assert_eq(sb->str->len, 0, "zero char append should not change length");
    
    // Test zero length string append
    stringbuf_append_str_n(sb, "Hello", 0);
    cr_assert_eq(sb->str->len, 0, "zero length append should not change length");
    
    stringbuf_free(sb);
}

