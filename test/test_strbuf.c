#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <string.h>
#include "../lib/strbuf.h"

// Setup and teardown functions
void setup(void) {
    // Runs before each test
}

void teardown(void) {
    // Runs after each test
}

TestSuite(strbuf_tests, .init = setup, .fini = teardown);

Test(strbuf_tests, test_new) {
    StrBuf* sb = strbuf_new();
    cr_assert_not_null(sb, "strbuf_new() should return non-null pointer");
    cr_assert_not_null(sb->s, "String buffer should be allocated");
    cr_assert_eq(sb->length, 0, "Initial length should be 0");
    cr_assert_gt(sb->capacity, 0, "Initial capacity should be at least 1");
    cr_assert_eq(sb->s[0], '\0', "Buffer should be null-terminated");
    strbuf_free(sb);
}

Test(strbuf_tests, test_new_cap) {
    size_t cap = 64;
    StrBuf* sb = strbuf_new_cap(cap);
    cr_assert_not_null(sb);
    cr_assert_not_null(sb->s);
    cr_assert_eq(sb->length, 0);
    cr_assert_geq(sb->capacity, cap);
    cr_assert_eq(sb->s[0], '\0');
    strbuf_free(sb);
}

Test(strbuf_tests, test_create) {
    const char* test_str = "Hello";
    StrBuf* sb = strbuf_create(test_str);
    cr_assert_not_null(sb);
    cr_assert_str_eq(sb->s, test_str);
    cr_assert_eq(sb->length, strlen(test_str));
    cr_assert_geq(sb->capacity, sb->length + 1);
    strbuf_free(sb);
}

Test(strbuf_tests, test_reset) {
    StrBuf* sb = strbuf_create("Test");
    strbuf_reset(sb);
    cr_assert_eq(sb->length, 0);
    cr_assert_eq(sb->s[0], '\0');
    cr_assert_gt(sb->capacity, 0);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_str) {
    StrBuf* sb = strbuf_new();
    const char* str1 = "Hello";
    const char* str2 = " World";
    
    strbuf_append_str(sb, str1);
    cr_assert_str_eq(sb->s, str1);
    cr_assert_eq(sb->length, strlen(str1));
    
    strbuf_append_str(sb, str2);
    cr_assert_str_eq(sb->s, "Hello World");
    cr_assert_eq(sb->length, strlen("Hello World"));
    
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_str_n) {
    StrBuf* sb = strbuf_new();
    const char* test = "HelloWorld";
    strbuf_append_str_n(sb, test, 5);
    cr_assert_str_eq(sb->s, "Hello");
    cr_assert_eq(sb->length, 5);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_char) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char(sb, 'A');
    cr_assert_str_eq(sb->s, "A");
    cr_assert_eq(sb->length, 1);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_char_n) {
    StrBuf* sb = strbuf_new();
    strbuf_append_char_n(sb, 'x', 3);
    cr_assert_str_eq(sb->s, "xxx");
    cr_assert_eq(sb->length, 3);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_all) {
    StrBuf* sb = strbuf_new();
    strbuf_append_all(sb, 3, "One", "Two", "Three");
    cr_assert_str_eq(sb->s, "OneTwoThree");
    cr_assert_eq(sb->length, strlen("OneTwoThree"));
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_format) {
    StrBuf* sb = strbuf_new();
    strbuf_append_format(sb, "Number: %d, String: %s", 42, "test");
    char expected[50];
    snprintf(expected, 50, "Number: %d, String: %s", 42, "test");
    cr_assert_str_eq(sb->s, expected);
    cr_assert_eq(sb->length, strlen(expected));
    strbuf_free(sb);
}

Test(strbuf_tests, test_resize) {
    StrBuf* sb = strbuf_create("Test");
    bool result = strbuf_resize(sb, 10);
    cr_assert_eq(result, true);
    cr_assert_str_eq(sb->s, "Test");
    cr_assert_geq(sb->capacity, 11);
    cr_assert_eq(sb->length, 4);
    strbuf_free(sb);
}

Test(strbuf_tests, test_trim_to_length) {
    StrBuf* sb = strbuf_create("Test");
    size_t old_cap = sb->capacity;
    strbuf_trim_to_length(sb);
    cr_assert_str_eq(sb->s, "Test");
    cr_assert_eq(sb->length, 4);
    cr_assert_eq(sb->capacity, 5);
    cr_assert_leq(sb->capacity, old_cap);
    strbuf_free(sb);
}

Test(strbuf_tests, test_copy_and_dup) {
    StrBuf* src = strbuf_create("Original");
    StrBuf* dst = strbuf_new();
    
    strbuf_copy(dst, src);
    cr_assert_str_eq(dst->s, "Original");
    cr_assert_eq(dst->length, src->length);
    
    StrBuf* dup = strbuf_dup(src);
    cr_assert_str_eq(dup->s, "Original");
    cr_assert_eq(dup->length, src->length);
    
    strbuf_free(src);
    strbuf_free(dst);
    strbuf_free(dup);
}

Test(strbuf_tests, test_chomp) {
    StrBuf* sb = strbuf_create("Hello\n\r");
    strbuf_chomp(sb);
    cr_assert_str_eq(sb->s, "Hello");
    cr_assert_eq(sb->length, 5);
    strbuf_free(sb);
}

Test(strbuf_tests, test_append_file) {
    // Create a temporary file for testing
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file(sb, temp);
    
    cr_assert_eq(result, true);
    cr_assert_str_eq(sb->s, content);
    cr_assert_eq(sb->length, strlen(content));
    
    strbuf_free(sb);
    fclose(temp);
}

Test(strbuf_tests, test_append_file_head) {
    FILE* temp = tmpfile();
    const char* content = "FileContent";
    fwrite(content, 1, strlen(content), temp);
    
    StrBuf* sb = strbuf_new();
    rewind(temp);
    bool result = strbuf_append_file_head(sb, temp, 4);
    
    cr_assert_eq(result, true);
    cr_assert_str_eq(sb->s, "File");
    cr_assert_eq(sb->length, 4);
    
    strbuf_free(sb);
    fclose(temp);
}

int main(int argc, char *argv[]) {
    struct criterion_test_set *tests = criterion_initialize();
    int result = 0;
    if (criterion_handle_args(argc, argv, true))
        result = !criterion_run_all_tests(tests);
    criterion_finalize(tests);
    return result;
}