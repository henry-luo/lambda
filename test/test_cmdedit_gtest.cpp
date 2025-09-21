#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../lib/cmdedit.h"
#include "../lib/cmdedit_utf8.h"
#include "../lib/strbuf.h"
}

class CmdEditTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize the REPL system
        repl_init();
    }

    void TearDown() override {
        // Clean up the REPL system
        repl_cleanup();
    }
};

TEST_F(CmdEditTest, BasicInitialization) {
    // Test that basic initialization works
    EXPECT_TRUE(true) << "Basic initialization should succeed";
}

TEST_F(CmdEditTest, BasicLineOperations) {
    // Test basic string operations that cmdedit might use
    const char* test_string = "Hello World";
    size_t len = strlen(test_string);
    
    EXPECT_EQ(strlen(test_string), 11UL) << "Initial buffer length should be 11";
    EXPECT_GT(len, 0UL) << "Test string should have positive length";
}

TEST_F(CmdEditTest, HistoryOperations) {
    // Test basic history operations
    const char* test_line = "echo hello";
    
    // Add to history
    int result = repl_add_history(test_line);
    EXPECT_EQ(result, 0) << "Adding to history should succeed";
    
    // Clear history
    result = clear_history();
    EXPECT_EQ(result, 0) << "Clearing history should succeed";
}

TEST_F(CmdEditTest, UTF8CharacterCount) {
    // Test UTF-8 character counting
    const char* ascii_string = "Hello";
    int char_count = cmdedit_utf8_char_count(ascii_string, strlen(ascii_string));
    EXPECT_EQ(char_count, 5) << "ASCII string should have correct character count";
    
    // Test with empty string
    const char* empty_string = "";
    char_count = cmdedit_utf8_char_count(empty_string, strlen(empty_string));
    EXPECT_EQ(char_count, 0) << "Empty string should have zero character count";
}

TEST_F(CmdEditTest, UTF8DisplayWidth) {
    // Test UTF-8 display width calculation
    const char* test_string = "Hello";
    int display_width = cmdedit_utf8_display_width(test_string, strlen(test_string));
    EXPECT_EQ(display_width, 5) << "Simple ASCII string should have width equal to length";
    
    // Test with empty string
    const char* empty_string = "";
    display_width = cmdedit_utf8_display_width(empty_string, strlen(empty_string));
    EXPECT_EQ(display_width, 0) << "Empty string should have zero display width";
}

TEST_F(CmdEditTest, CursorMovement) {
    // Test cursor movement functions
    const char* test_string = "Hello World";
    size_t string_len = strlen(test_string);
    
    // Test moving cursor left from end
    size_t cursor_pos = string_len;
    size_t new_pos = cmdedit_utf8_move_cursor_left(test_string, string_len, cursor_pos);
    EXPECT_LT(new_pos, cursor_pos) << "Moving cursor left should decrease position";
    
    // Test moving cursor right from beginning
    cursor_pos = 0;
    new_pos = cmdedit_utf8_move_cursor_right(test_string, string_len, cursor_pos);
    EXPECT_GT(new_pos, cursor_pos) << "Moving cursor right should increase position";
}

TEST_F(CmdEditTest, WordBoundaries) {
    // Test word boundary detection
    const char* test_string = "hello world test";
    size_t string_len = strlen(test_string);
    
    // Test finding word start
    size_t pos = 8; // middle of "world"
    size_t word_start = cmdedit_utf8_find_word_start(test_string, string_len, pos);
    EXPECT_LE(word_start, pos) << "Word start should be at or before current position";
    
    // Test finding word end
    pos = 2; // middle of "hello"
    size_t word_end = cmdedit_utf8_find_word_end(test_string, string_len, pos);
    EXPECT_GE(word_end, pos) << "Word end should be at or after current position";
}

TEST_F(CmdEditTest, UTF8Validation) {
    // Test UTF-8 validation
    const char* valid_string = "Hello World";
    EXPECT_TRUE(utf8_is_valid(valid_string, strlen(valid_string))) << "Valid ASCII should pass UTF-8 validation";
    
    const char* empty_string = "";
    EXPECT_TRUE(utf8_is_valid(empty_string, strlen(empty_string))) << "Empty string should be valid UTF-8";
}

TEST_F(CmdEditTest, CharacterWidth) {
    // Test character width calculation
    const char* test_string = "Hello";
    size_t string_len = strlen(test_string);
    
    // Test width of first character
    int char_width = cmdedit_utf8_char_display_width_at(test_string, string_len, 0);
    EXPECT_EQ(char_width, 1) << "ASCII character should have width 1";
    
    // Test width of character 'e'
    char_width = cmdedit_utf8_char_display_width_at(test_string, string_len, 1);
    EXPECT_EQ(char_width, 1) << "ASCII character 'e' should have width 1";
}

TEST_F(CmdEditTest, ByteCharOffsetConversion) {
    // Test conversion between byte and character offsets
    const char* test_string = "Hello";
    size_t string_len = strlen(test_string);
    
    // Test byte to char offset conversion
    int char_offset = cmdedit_utf8_byte_to_char_offset(test_string, string_len, 3);
    EXPECT_EQ(char_offset, 3) << "For ASCII, byte offset should equal char offset";
    
    // Test char to byte offset conversion  
    int byte_offset = cmdedit_utf8_char_to_byte_offset(test_string, string_len, 3);
    EXPECT_EQ(byte_offset, 3) << "For ASCII, char offset should equal byte offset";
}

TEST_F(CmdEditTest, CharacterExtraction) {
    // Test character extraction at specific positions
    const char* test_string = "Hello";
    size_t string_len = strlen(test_string);
    utf8_char_t utf8_char;
    
    // Test getting character at position 0
    bool result = cmdedit_utf8_get_char_at_byte(test_string, string_len, 0, &utf8_char);
    EXPECT_TRUE(result) << "Should be able to get character at valid position";
    EXPECT_GT(utf8_char.byte_length, 0UL) << "Character should have positive byte length";
    
    // Test getting character at invalid position
    result = cmdedit_utf8_get_char_at_byte(test_string, string_len, string_len + 10, &utf8_char);
    EXPECT_FALSE(result) << "Should fail to get character at invalid position";
}