#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/stringbuf.h"
#include "../lib/mempool.h"
}

class StringBufTest : public ::testing::Test {
protected:
    Pool *test_pool = nullptr;

    void SetUp() override {
        test_pool = pool_create();
        ASSERT_NE(test_pool, nullptr) << "Failed to create memory pool";
    }

    void TearDown() override {
        if (test_pool) {
            pool_destroy(test_pool);
            test_pool = nullptr;
        }
    }
};

TEST_F(StringBufTest, TestStringbufCreation) {
    StringBuf *sb = stringbuf_new(test_pool);
    ASSERT_NE(sb, nullptr) << "stringbuf_new should return non-NULL";
    ASSERT_EQ(sb->pool, test_pool) << "pool should be set correctly";
    ASSERT_EQ(sb->length, 0) << "initial length should be 0";
    ASSERT_TRUE(sb->str == nullptr || sb->capacity > 0) << "str should be NULL or capacity > 0";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufCreationWithCapacity) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 100);
    ASSERT_NE(sb, nullptr) << "stringbuf_new_cap should return non-NULL";
    ASSERT_GE(sb->capacity, sizeof(String) + 100) << "capacity should be at least requested + String header";
    ASSERT_EQ(sb->length, 0) << "initial length should be 0";
    ASSERT_NE(sb->str, nullptr) << "str should be allocated";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendStr) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_str(sb, "Hello");
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 5) << "length should be 5 after appending 'Hello'";
    ASSERT_STREQ(sb->str->chars, "Hello") << "content should be 'Hello'";

    stringbuf_append_str(sb, " World");
    ASSERT_EQ(sb->str->len, 11) << "length should be 11 after appending ' World'";
    ASSERT_STREQ(sb->str->chars, "Hello World") << "content should be 'Hello World'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendChar) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_char(sb, 'A');
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 1) << "length should be 1 after appending 'A'";
    ASSERT_EQ(sb->str->chars[0], 'A') << "first character should be 'A'";
    ASSERT_EQ(sb->str->chars[1], '\0') << "should be null terminated";

    stringbuf_append_char(sb, 'B');
    ASSERT_EQ(sb->str->len, 2) << "length should be 2 after appending 'B'";
    ASSERT_STREQ(sb->str->chars, "AB") << "content should be 'AB'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendStrN) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_str_n(sb, "Hello World", 5);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 5) << "length should be 5 after appending first 5 chars";
    ASSERT_STREQ(sb->str->chars, "Hello") << "content should be 'Hello'";

    stringbuf_append_str_n(sb, " World!", 6);
    ASSERT_EQ(sb->str->len, 11) << "length should be 11 after appending ' World'";
    ASSERT_STREQ(sb->str->chars, "Hello World") << "content should be 'Hello World'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendCharN) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_char_n(sb, 'X', 3);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 3) << "length should be 3 after appending 3 'X's";
    ASSERT_STREQ(sb->str->chars, "XXX") << "content should be 'XXX'";

    stringbuf_append_char_n(sb, 'Y', 2);
    ASSERT_EQ(sb->str->len, 5) << "length should be 5 after appending 2 'Y's";
    ASSERT_STREQ(sb->str->chars, "XXXYY") << "content should be 'XXXYY'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendFormat) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_format(sb, "Number: %d", 42);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_STREQ(sb->str->chars, "Number: 42") << "content should be 'Number: 42'";

    stringbuf_append_format(sb, ", String: %s", "test");
    ASSERT_STREQ(sb->str->chars, "Number: 42, String: test") << "content should include both parts";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufAppendNumbers) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_int(sb, 123);
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_STREQ(sb->str->chars, "123") << "content should be '123'";

    stringbuf_reset(sb);
    stringbuf_append_format(sb, "%u", 456U);
    ASSERT_STREQ(sb->str->chars, "456") << "content should be '456'";

    stringbuf_reset(sb);
    stringbuf_append_format(sb, "%.2f", 3.14159);
    ASSERT_EQ(strncmp(sb->str->chars, "3.14", 4), 0) << "content should start with '3.14'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufReset) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_str(sb, "Hello World");
    ASSERT_EQ(sb->str->len, 11) << "length should be 11 before reset";

    stringbuf_reset(sb);
    ASSERT_EQ(sb->str->len, 0) << "length should be 0 after reset";
    ASSERT_EQ(sb->str->chars[0], '\0') << "should be null terminated after reset";

    // Should be able to append after reset
    stringbuf_append_str(sb, "New");
    ASSERT_EQ(sb->str->len, 3) << "length should be 3 after appending to reset buffer";
    ASSERT_STREQ(sb->str->chars, "New") << "content should be 'New'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufFullReset) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_str(sb, "Hello World");

    stringbuf_full_reset(sb);
    ASSERT_EQ(sb->str, nullptr) << "str should be NULL after full reset";
    ASSERT_EQ(sb->length, 0) << "length should be 0 after full reset";
    ASSERT_EQ(sb->capacity, 0) << "capacity should be 0 after full reset";

    // Should be able to append after full reset
    stringbuf_append_str(sb, "New");
    ASSERT_NE(sb->str, nullptr) << "str should be allocated after append";
    ASSERT_EQ(sb->str->len, 3) << "length should be 3";
    ASSERT_STREQ(sb->str->chars, "New") << "content should be 'New'";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufCopy) {
    StringBuf *sb1 = stringbuf_new(test_pool);
    StringBuf *sb2 = stringbuf_new(test_pool);

    stringbuf_append_str(sb1, "Hello World");
    stringbuf_copy(sb2, sb1);

    ASSERT_NE(sb2->str, nullptr) << "destination str should be allocated";
    ASSERT_EQ(sb2->str->len, sb1->str->len) << "lengths should match";
    ASSERT_STREQ(sb2->str->chars, sb1->str->chars) << "contents should match";
    ASSERT_NE(sb2->str, sb1->str) << "should be different String objects";

    stringbuf_free(sb1);
    stringbuf_free(sb2);
}

TEST_F(StringBufTest, TestStringbufDup) {
    StringBuf *sb1 = stringbuf_new(test_pool);
    stringbuf_append_str(sb1, "Hello World");

    StringBuf *sb2 = stringbuf_dup(sb1);
    ASSERT_NE(sb2, nullptr) << "dup should return non-NULL";
    ASSERT_NE(sb2->str, nullptr) << "dup str should be allocated";
    ASSERT_EQ(sb2->str->len, sb1->str->len) << "lengths should match";
    ASSERT_STREQ(sb2->str->chars, sb1->str->chars) << "contents should match";
    ASSERT_NE(sb2->str, sb1->str) << "should be different String objects";
    ASSERT_EQ(sb2->pool, sb1->pool) << "pools should match";

    stringbuf_free(sb1);
    stringbuf_free(sb2);
}

TEST_F(StringBufTest, TestStringbufToString) {
    StringBuf *sb = stringbuf_new(test_pool);
    stringbuf_append_str(sb, "Hello World");

    String *str = stringbuf_to_string(sb);
    ASSERT_NE(str, nullptr) << "to_string should return non-NULL";
    ASSERT_EQ(str->len, 11) << "string length should be 11";
    ASSERT_STREQ(str->chars, "Hello World") << "string content should be 'Hello World'";

    // Buffer should be reset after to_string (str pointer becomes NULL)
    ASSERT_EQ(sb->str, nullptr) << "buffer str should be NULL after to_string";
    ASSERT_EQ(sb->length, 0) << "buffer length should be 0 after to_string";
    ASSERT_EQ(sb->capacity, 0) << "buffer capacity should be 0 after to_string";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufCapacityGrowth) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 10);
    size_t initial_capacity = sb->capacity;

    // Append enough data to force growth
    for (int i = 0; i < 100; i++) {
        stringbuf_append_char(sb, 'A');
    }

    ASSERT_GT(sb->capacity, initial_capacity) << "capacity should have grown";
    ASSERT_EQ(sb->str->len, 100) << "length should be 100";

    // Verify content
    bool all_a = true;
    for (int i = 0; i < 100; i++) {
        if (sb->str->chars[i] != 'A') {
            all_a = false;
            break;
        }
    }
    ASSERT_TRUE(all_a) << "all characters should be 'A'";
    ASSERT_EQ(sb->str->chars[100], '\0') << "should be null terminated";

    stringbuf_free(sb);
}

TEST_F(StringBufTest, TestStringbufEdgeCases) {
    StringBuf *sb = stringbuf_new(test_pool);

    // Test empty string append
    stringbuf_append_str(sb, "");
    ASSERT_EQ(sb->str->len, 0) << "empty string append should not change length";

    // Test zero character append
    stringbuf_append_char_n(sb, 'X', 0);
    ASSERT_EQ(sb->str->len, 0) << "zero char append should not change length";

    // Test zero length string append
    stringbuf_append_str_n(sb, "Hello", 0);
    ASSERT_EQ(sb->str->len, 0) << "zero length append should not change length";

    stringbuf_free(sb);
}

// Test for the critical bug fix: vsnprintf buffer calculation
TEST_F(StringBufTest, TestStringbufFormatLongString) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 50);  // Start with small capacity

    // Create a format string that will result in a long output
    // This tests the vsnprintf buffer calculation bug fix
    stringbuf_append_format(sb, "This is a very long formatted string with number %d and string '%s' that should trigger buffer growth", 12345, "test_string_value");

    ASSERT_NE(sb->str, nullptr) << "str should be allocated after format append";
    ASSERT_GT(sb->str->len, 50) << "formatted string should be longer than initial capacity";

    // Verify the content is correct
    const char* expected = "This is a very long formatted string with number 12345 and string 'test_string_value' that should trigger buffer growth";
    ASSERT_STREQ(sb->str->chars, expected) << "formatted string content should be correct";
    ASSERT_EQ(sb->str->len, strlen(expected)) << "length should match expected string length";

    stringbuf_free(sb);
}

// Test multiple format appends to ensure buffer calculations remain correct
TEST_F(StringBufTest, TestStringbufMultipleFormatAppends) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 20);  // Start small

    stringbuf_append_format(sb, "First: %d", 1);
    ASSERT_STREQ(sb->str->chars, "First: 1") << "first format should be correct";

    stringbuf_append_format(sb, ", Second: %s", "hello");
    ASSERT_STREQ(sb->str->chars, "First: 1, Second: hello") << "second format should append correctly";

    stringbuf_append_format(sb, ", Third: %.2f", 3.14);
    ASSERT_EQ(strncmp(sb->str->chars, "First: 1, Second: hello, Third: 3.14", 37), 0) << "third format should append correctly";

    // Add a very long format that forces significant buffer growth
    stringbuf_append_format(sb, ", Fourth: This is a very long string with multiple placeholders %d %s %f %c",
                           42, "world", 2.718, 'X');

    ASSERT_GT(sb->str->len, 100) << "final string should be quite long";
    ASSERT_NE(strstr(sb->str->chars, "Fourth: This is a very long string"), nullptr) << "should contain the long format";
    ASSERT_NE(strstr(sb->str->chars, "42 world"), nullptr) << "should contain formatted values";

    stringbuf_free(sb);
}

// Test stress case: repeated appends with buffer growth
TEST_F(StringBufTest, TestStringbufRepeatedAppends) {
    StringBuf *sb = stringbuf_new_cap(test_pool, 16);  // Very small initial capacity

    // Append many small strings to force multiple buffer growth operations
    for (int i = 0; i < 100; i++) {
        stringbuf_append_format(sb, "Item%d ", i);
    }

    ASSERT_GT(sb->str->len, 500) << "accumulated string should be quite long";
    ASSERT_GT(sb->capacity, 16) << "capacity should have grown significantly";

    // Verify some content
    ASSERT_EQ(strncmp(sb->str->chars, "Item0 Item1 Item2", 17), 0) << "should start with first items";
    ASSERT_NE(strstr(sb->str->chars, "Item99 "), nullptr) << "should contain last item";

    stringbuf_free(sb);
}

// Test edge case: format string that exactly fits in remaining buffer
TEST_F(StringBufTest, TestStringbufFormatExactFit) {
    StringBuf *sb = stringbuf_new_cap(test_pool, sizeof(String) + 20);  // Exact capacity

    // First, partially fill the buffer
    stringbuf_append_str(sb, "Start:");  // 6 chars

    // Now append a format that should exactly fit in remaining space
    stringbuf_append_format(sb, "%d", 1234);  // 4 chars, total would be 10

    ASSERT_STREQ(sb->str->chars, "Start:1234") << "exact fit format should work";
    ASSERT_EQ(sb->str->len, 10) << "length should be exactly 10";

    // Now add something that forces growth
    stringbuf_append_format(sb, " and more %s", "content");
    ASSERT_NE(strstr(sb->str->chars, "Start:1234 and more content"), nullptr) << "should contain all content after growth";

    stringbuf_free(sb);
}

// Test very large format string to ensure no overflow issues
TEST_F(StringBufTest, TestStringbufVeryLargeFormat) {
    StringBuf *sb = stringbuf_new(test_pool);

    // Create a very large string through formatting
    char large_input[1000];
    memset(large_input, 'A', 999);
    large_input[999] = '\0';

    stringbuf_append_format(sb, "Large string: %s", large_input);

    ASSERT_GT(sb->str->len, 1000) << "resulting string should be very large";
    ASSERT_EQ(strncmp(sb->str->chars, "Large string: AAA", 17), 0) << "should start with expected prefix";
    ASSERT_EQ(sb->str->chars[sb->str->len - 1], 'A') << "should end with 'A'";

    stringbuf_free(sb);
}

// Test format with NULL format string (edge case)
TEST_F(StringBufTest, TestStringbufFormatNullFormat) {
    StringBuf *sb = stringbuf_new(test_pool);

    stringbuf_append_str(sb, "Before");
    stringbuf_append_format(sb, nullptr);  // Should be handled gracefully
    stringbuf_append_str(sb, "After");

    ASSERT_STREQ(sb->str->chars, "BeforeAfter") << "null format should not affect other appends";

    stringbuf_free(sb);
}

// Test length field overflow protection (22-bit limit = 4,194,303)
TEST_F(StringBufTest, TestStringbufLengthOverflowProtection) {
    StringBuf *sb = stringbuf_new(test_pool);

    // First, get to exactly the limit
    const size_t max_len = 0x3FFFFF; // 22-bit max value: 4,194,303

    // Manually set length to exactly the limit (for testing purposes)
    // In real usage, this would happen through many append operations
    if (stringbuf_ensure_cap(sb, max_len + 100)) {
        sb->length = max_len;
        sb->str->len = max_len;
        sb->str->chars[max_len] = '\0';

        size_t old_length = sb->length;

        // Try to append a character that would exceed the limit
        stringbuf_append_char(sb, 'X');

        // Length should remain unchanged due to overflow protection
        ASSERT_EQ(sb->length, old_length) << "char append should be rejected due to overflow";
        ASSERT_EQ(sb->str->len, old_length) << "str->len should not change when overflow would occur";

        // Try to append a string that would exceed the limit
        stringbuf_append_str(sb, "This should be rejected");
        ASSERT_EQ(sb->length, old_length) << "string append should be rejected due to overflow";

        // Try format append that should also be rejected
        stringbuf_append_format(sb, "Number: %d", 42);
        ASSERT_EQ(sb->length, old_length) << "format append should be rejected due to overflow";
    }

    stringbuf_free(sb);
}

// Test that reproduces the LaTeX formatter crash scenario
// Two StringBufs sharing the same memory pool with interleaved operations
TEST_F(StringBufTest, TestDualStringBufMemoryCorruption) {
    printf("DEBUG: Starting dual StringBuf memory corruption test\n");

    // Create two StringBufs sharing the same memory pool (like LaTeX formatter)
    StringBuf *html_buf = stringbuf_new(test_pool);
    StringBuf *css_buf = stringbuf_new(test_pool);

    ASSERT_NE(html_buf, nullptr) << "html_buf creation should succeed";
    ASSERT_NE(css_buf, nullptr) << "css_buf creation should succeed";
    ASSERT_EQ(html_buf->pool, test_pool) << "html_buf should use test_pool";
    ASSERT_EQ(css_buf->pool, test_pool) << "css_buf should use test_pool";

    // Simulate LaTeX HTML generation with CSS generation
    // This reproduces the exact pattern that causes the crash

    // Phase 1: HTML content generation (like process_latex_element)
    printf("DEBUG: Phase 1 - HTML generation\n");
    stringbuf_append_str(html_buf, "<div class=\"body\">\n");
    stringbuf_append_str(html_buf, "<h1>Test Document</h1>\n");
    stringbuf_append_str(html_buf, "<p>This is some content that will cause ");
    stringbuf_append_str(html_buf, "the HTML StringBuf to grow and allocate memory.</p>\n");

    // Store initial String pointer for corruption detection
    String* initial_html_str = html_buf->str;
    size_t initial_html_length = html_buf->length;
    printf("DEBUG: Initial html_buf->str=%p, length=%zu\n", initial_html_str, initial_html_length);

    // Phase 2: CSS generation (like generate_latex_css) - this causes many reallocations
    printf("DEBUG: Phase 2 - CSS generation (causes reallocations)\n");

    // Simulate the CSS generation that causes the crash
    for (int i = 0; i < 100; i++) {
        // These are the exact CSS strings from the LaTeX formatter
        stringbuf_append_str(css_buf, ".body {\n");
        stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
        stringbuf_append_str(css_buf, "  max-width: 800px;\n");
        stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
        stringbuf_append_str(css_buf, "  padding: 2rem;\n");
        stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
        stringbuf_append_str(css_buf, "  color: #333;\n");
        stringbuf_append_str(css_buf, "}\n");

        // Add more CSS to force reallocations
        stringbuf_append_str(css_buf, ".latex-title {\n");
        stringbuf_append_str(css_buf, "  text-align: center;\n");
        stringbuf_append_str(css_buf, "  font-size: 2.5em;\n");
        stringbuf_append_str(css_buf, "  font-weight: bold;\n");
        stringbuf_append_str(css_buf, "  margin: 2rem 0;\n");
        stringbuf_append_str(css_buf, "}\n");

        // Intermittently add to HTML buffer (simulates interleaved operations)
        if (i % 10 == 0) {
            printf("DEBUG: Iteration %d - Adding to HTML buffer\n", i);
            printf("DEBUG: Before HTML append - html_buf->str=%p, css_buf->str=%p\n",
                   html_buf->str, css_buf->str);

            // This operation might crash if html_buf->str was corrupted by css_buf reallocation
            stringbuf_append_str(html_buf, "<p>More content added during CSS generation</p>\n");

            printf("DEBUG: After HTML append - html_buf->str=%p, css_buf->str=%p\n",
                   html_buf->str, css_buf->str);

            // Check if html_buf String pointer changed due to memory corruption
            if (html_buf->str != initial_html_str) {
                printf("DEBUG: html_buf->str pointer changed from %p to %p at iteration %d\n",
                       initial_html_str, html_buf->str, i);
                initial_html_str = html_buf->str;
            }
        }
    }

    // Phase 3: Final access (like stringbuf_to_string in test)
    printf("DEBUG: Phase 3 - Final access to HTML buffer\n");

    // This might crash if the String pointer was corrupted
    ASSERT_NE(html_buf->str, nullptr) << "html_buf->str should not be NULL";
    ASSERT_GT(html_buf->length, initial_html_length) << "HTML buffer should have grown";
    ASSERT_EQ(html_buf->length, html_buf->str->len) << "Length fields should be synchronized";

    // Try to convert to string (this is where the test crash occurred)
    String* html_result = stringbuf_to_string(html_buf);
    ASSERT_NE(html_result, nullptr) << "stringbuf_to_string should succeed";
    ASSERT_GT(html_result->len, 0) << "Result should have content";

    printf("DEBUG: Test completed successfully - html_buf final length=%zu, css_buf final length=%zu\n",
           html_buf->length, css_buf->length);

    stringbuf_free(html_buf);
    stringbuf_free(css_buf);
}

// Stress test version with random operations
TEST_F(StringBufTest, TestDualStringBufStress) {
    printf("DEBUG: Starting dual StringBuf stress test\n");

    const int NUM_ITERATIONS = 1000;
    const int TARGET_DOCUMENT_SIZE = 50000; // ~50KB document

    StringBuf *buf1 = stringbuf_new(test_pool);
    StringBuf *buf2 = stringbuf_new(test_pool);

    ASSERT_NE(buf1, nullptr);
    ASSERT_NE(buf2, nullptr);

    // Array of strings to randomly append (simulating HTML/CSS content)
    const char* html_strings[] = {
        "<div class=\"content\">",
        "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit.</p>",
        "<h1>Section Header</h1>",
        "<ul><li>List item 1</li><li>List item 2</li></ul>",
        "</div>",
        "<span class=\"highlight\">Important text</span>",
    };

    const char* css_strings[] = {
        "body { font-family: Arial, sans-serif; }",
        ".content { margin: 20px; padding: 10px; }",
        "h1 { color: #333; font-size: 24px; }",
        "p { line-height: 1.6; margin-bottom: 16px; }",
        ".highlight { background-color: yellow; }",
        "ul { list-style-type: disc; margin-left: 20px; }",
    };

    int total_size = 0;
    for (int i = 0; i < NUM_ITERATIONS && total_size < TARGET_DOCUMENT_SIZE; i++) {
        // Randomly choose which buffer to append to
        bool use_buf1 = (i % 3 != 0); // Bias toward buf1 (HTML)
        StringBuf* target_buf = use_buf1 ? buf1 : buf2;
        const char** string_array = use_buf1 ? html_strings : css_strings;
        int array_size = use_buf1 ? 6 : 6;

        // Randomly choose string to append
        int string_index = i % array_size;
        const char* str_to_append = string_array[string_index];

        printf("DEBUG: Iteration %d - Appending to %s: '%.20s...'\n",
               i, use_buf1 ? "buf1" : "buf2", str_to_append);

        // This might crash due to memory corruption
        stringbuf_append_str(target_buf, str_to_append);

        total_size += strlen(str_to_append);

        // Periodically verify integrity
        if (i % 100 == 0) {
            ASSERT_NE(buf1->str, nullptr) << "buf1->str should not be NULL at iteration " << i;
            ASSERT_NE(buf2->str, nullptr) << "buf2->str should not be NULL at iteration " << i;
            ASSERT_EQ(buf1->length, buf1->str->len) << "buf1 length sync at iteration " << i;
            ASSERT_EQ(buf2->length, buf2->str->len) << "buf2 length sync at iteration " << i;
        }
    }

    // Final verification
    ASSERT_GT(buf1->length, 0) << "buf1 should have content";
    ASSERT_GT(buf2->length, 0) << "buf2 should have content";

    String* result1 = stringbuf_to_string(buf1);
    String* result2 = stringbuf_to_string(buf2);

    ASSERT_NE(result1, nullptr) << "buf1 stringbuf_to_string should succeed";
    ASSERT_NE(result2, nullptr) << "buf2 stringbuf_to_string should succeed";

    printf("DEBUG: Stress test completed - buf1 size=%zu, buf2 size=%zu\n",
           result1->len, result2->len);

    stringbuf_free(buf1);
    stringbuf_free(buf2);
}
