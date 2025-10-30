#include <gtest/gtest.h>
extern "C" {
#include "lambda/input/html5_tokenizer.h"
#include "lambda/input/input.h"
#include "lib/mempool.h"
#include "lib/stringbuf.h"
}

class Html5TokenizerTest : public ::testing::Test {
protected:
    Pool* pool;

    void SetUp() override {
        pool = pool_create();  // no arguments
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    // Helper to tokenize a string and collect all tokens
    std::vector<Html5Token*> tokenize(const char* html) {
        std::vector<Html5Token*> tokens;

        Html5Tokenizer* tokenizer = html5_tokenizer_create(pool, html, strlen(html));
        EXPECT_NE(tokenizer, nullptr);

        while (true) {
            Html5Token* token = html5_tokenizer_next_token(tokenizer);
            if (!token || token->type == HTML5_TOKEN_EOF) {
                break;
            }
            tokens.push_back(token);
        }

        html5_tokenizer_destroy(tokenizer);
        return tokens;
    }

    // Helper to get tag name from token
    std::string getTagName(Html5Token* token) {
        if (token->type != HTML5_TOKEN_START_TAG && token->type != HTML5_TOKEN_END_TAG) {
            return "";
        }
        return std::string(token->tag_data.name->str->chars);
    }
};

// ============================================================================
// Basic Infrastructure Tests
// ============================================================================

TEST_F(Html5TokenizerTest, TokenizerCreate) {
    const char* html = "<html></html>";
    Html5Tokenizer* tokenizer = html5_tokenizer_create(pool, html, strlen(html));

    ASSERT_NE(tokenizer, nullptr);
    EXPECT_EQ(tokenizer->pool, pool);
    EXPECT_EQ(tokenizer->input, html);
    EXPECT_EQ(tokenizer->input_length, strlen(html));
    EXPECT_EQ(tokenizer->position, (size_t)0);
    EXPECT_EQ(tokenizer->line, (size_t)1);
    EXPECT_EQ(tokenizer->column, (size_t)1);
    EXPECT_EQ(tokenizer->state, HTML5_STATE_DATA);

    html5_tokenizer_destroy(tokenizer);
}

TEST_F(Html5TokenizerTest, TokenCreate) {
    Html5Token* token = html5_token_create(pool, HTML5_TOKEN_START_TAG);

    ASSERT_NE(token, nullptr);
    EXPECT_EQ(token->type, HTML5_TOKEN_START_TAG);
}

TEST_F(Html5TokenizerTest, TokenTypeNames) {
    EXPECT_STREQ(html5_token_type_name(HTML5_TOKEN_DOCTYPE), "DOCTYPE");
    EXPECT_STREQ(html5_token_type_name(HTML5_TOKEN_START_TAG), "START_TAG");
    EXPECT_STREQ(html5_token_type_name(HTML5_TOKEN_END_TAG), "END_TAG");
    EXPECT_STREQ(html5_token_type_name(HTML5_TOKEN_COMMENT), "COMMENT");
    EXPECT_STREQ(html5_token_type_name(HTML5_TOKEN_CHARACTER), "CHARACTER");
    EXPECT_STREQ(html5_token_type_name(HTML5_TOKEN_EOF), "EOF");
}

TEST_F(Html5TokenizerTest, AttributeCreate) {
    Html5Attribute* attr = html5_attribute_create(pool, "id", "test");

    ASSERT_NE(attr, nullptr);
    EXPECT_STREQ(attr->name->str->chars, "id");
    EXPECT_STREQ(attr->value->str->chars, "test");
    EXPECT_EQ(attr->next, nullptr);
}

// ============================================================================
// Character Classification Tests
// ============================================================================

TEST_F(Html5TokenizerTest, CharacterClassification) {
    // whitespace
    EXPECT_TRUE(html5_is_whitespace(' '));
    EXPECT_TRUE(html5_is_whitespace('\t'));
    EXPECT_TRUE(html5_is_whitespace('\n'));
    EXPECT_TRUE(html5_is_whitespace('\r'));
    EXPECT_TRUE(html5_is_whitespace('\f'));
    EXPECT_FALSE(html5_is_whitespace('a'));

    // alpha
    EXPECT_TRUE(html5_is_ascii_alpha('a'));
    EXPECT_TRUE(html5_is_ascii_alpha('Z'));
    EXPECT_FALSE(html5_is_ascii_alpha('0'));
    EXPECT_FALSE(html5_is_ascii_alpha(' '));

    // digit
    EXPECT_TRUE(html5_is_ascii_digit('0'));
    EXPECT_TRUE(html5_is_ascii_digit('9'));
    EXPECT_FALSE(html5_is_ascii_digit('a'));

    // hex digit
    EXPECT_TRUE(html5_is_ascii_hex_digit('0'));
    EXPECT_TRUE(html5_is_ascii_hex_digit('F'));
    EXPECT_TRUE(html5_is_ascii_hex_digit('a'));
    EXPECT_FALSE(html5_is_ascii_hex_digit('g'));
}

// ============================================================================
// Simple Tokenization Tests
// ============================================================================

TEST_F(Html5TokenizerTest, EmptyString) {
    auto tokens = tokenize("");
    EXPECT_EQ(tokens.size(), (size_t)0);
}

TEST_F(Html5TokenizerTest, TextOnly) {
    auto tokens = tokenize("Hello World");

    ASSERT_EQ(tokens.size(), (size_t)11);  // 11 character tokens
    for (size_t i = 0; i < tokens.size(); i++) {
        EXPECT_EQ(tokens[i]->type, HTML5_TOKEN_CHARACTER);
    }
}

TEST_F(Html5TokenizerTest, SimpleStartTag) {
    auto tokens = tokenize("<div>");

    ASSERT_EQ(tokens.size(), (size_t)1);
    EXPECT_EQ(tokens[0]->type, HTML5_TOKEN_START_TAG);
    EXPECT_EQ(getTagName(tokens[0]), "div");
    EXPECT_FALSE(tokens[0]->tag_data.self_closing);
}

TEST_F(Html5TokenizerTest, SimpleEndTag) {
    auto tokens = tokenize("</div>");

    ASSERT_EQ(tokens.size(), (size_t)1);
    EXPECT_EQ(tokens[0]->type, HTML5_TOKEN_END_TAG);
    EXPECT_EQ(getTagName(tokens[0]), "div");
}

TEST_F(Html5TokenizerTest, TagPair) {
    auto tokens = tokenize("<div></div>");

    ASSERT_EQ(tokens.size(), (size_t)2);
    EXPECT_EQ(tokens[0]->type, HTML5_TOKEN_START_TAG);
    EXPECT_EQ(getTagName(tokens[0]), "div");
    EXPECT_EQ(tokens[1]->type, HTML5_TOKEN_END_TAG);
    EXPECT_EQ(getTagName(tokens[1]), "div");
}

TEST_F(Html5TokenizerTest, UppercaseTagName) {
    // HTML5 spec: tag names should be lowercased
    auto tokens = tokenize("<DIV></DIV>");

    ASSERT_EQ(tokens.size(), (size_t)2);
    EXPECT_EQ(tokens[0]->type, HTML5_TOKEN_START_TAG);
    EXPECT_EQ(getTagName(tokens[0]), "div");  // should be lowercased
    EXPECT_EQ(tokens[1]->type, HTML5_TOKEN_END_TAG);
    EXPECT_EQ(getTagName(tokens[1]), "div");  // should be lowercased
}

TEST_F(Html5TokenizerTest, MixedContent) {
    auto tokens = tokenize("<p>Hello</p>");

    ASSERT_EQ(tokens.size(), (size_t)7);  // <p>, H, e, l, l, o, </p>
    EXPECT_EQ(tokens[0]->type, HTML5_TOKEN_START_TAG);
    EXPECT_EQ(getTagName(tokens[0]), "p");

    // "Hello" as 5 character tokens
    for (int i = 1; i <= 5; i++) {
        EXPECT_EQ(tokens[i]->type, HTML5_TOKEN_CHARACTER);
    }

    EXPECT_EQ(tokens[6]->type, HTML5_TOKEN_END_TAG);
    EXPECT_EQ(getTagName(tokens[6]), "p");
}

TEST_F(Html5TokenizerTest, MultipleElements) {
    auto tokens = tokenize("<div><span></span></div>");

    ASSERT_EQ(tokens.size(), (size_t)4);
    EXPECT_EQ(tokens[0]->type, HTML5_TOKEN_START_TAG);
    EXPECT_EQ(getTagName(tokens[0]), "div");
    EXPECT_EQ(tokens[1]->type, HTML5_TOKEN_START_TAG);
    EXPECT_EQ(getTagName(tokens[1]), "span");
    EXPECT_EQ(tokens[2]->type, HTML5_TOKEN_END_TAG);
    EXPECT_EQ(getTagName(tokens[2]), "span");
    EXPECT_EQ(tokens[3]->type, HTML5_TOKEN_END_TAG);
    EXPECT_EQ(getTagName(tokens[3]), "div");
}

// ============================================================================
// Tokenizer State Tests
// ============================================================================

TEST_F(Html5TokenizerTest, StateNames) {
    EXPECT_STREQ(html5_tokenizer_state_name(HTML5_STATE_DATA), "DATA");
    EXPECT_STREQ(html5_tokenizer_state_name(HTML5_STATE_TAG_OPEN), "TAG_OPEN");
    EXPECT_STREQ(html5_tokenizer_state_name(HTML5_STATE_TAG_NAME), "TAG_NAME");
    EXPECT_STREQ(html5_tokenizer_state_name(HTML5_STATE_COMMENT), "COMMENT");
}

TEST_F(Html5TokenizerTest, StateTransitions) {
    const char* html = "<div>";
    Html5Tokenizer* tokenizer = html5_tokenizer_create(pool, html, strlen(html));

    EXPECT_EQ(tokenizer->state, HTML5_STATE_DATA);

    // manually step through states
    html5_tokenizer_set_state(tokenizer, HTML5_STATE_TAG_OPEN);
    EXPECT_EQ(tokenizer->state, HTML5_STATE_TAG_OPEN);

    html5_tokenizer_set_state(tokenizer, HTML5_STATE_TAG_NAME);
    EXPECT_EQ(tokenizer->state, HTML5_STATE_TAG_NAME);

    html5_tokenizer_destroy(tokenizer);
}

// ============================================================================
// EOF Handling Tests
// ============================================================================

TEST_F(Html5TokenizerTest, EOFDetection) {
    const char* html = "x";
    Html5Tokenizer* tokenizer = html5_tokenizer_create(pool, html, strlen(html));

    EXPECT_FALSE(html5_tokenizer_is_eof(tokenizer));

    // consume the character
    html5_tokenizer_next_token(tokenizer);

    EXPECT_TRUE(html5_tokenizer_is_eof(tokenizer));

    html5_tokenizer_destroy(tokenizer);
}

TEST_F(Html5TokenizerTest, EOFToken) {
    const char* html = "x";
    Html5Tokenizer* tokenizer = html5_tokenizer_create(pool, html, strlen(html));

    // get character token
    Html5Token* char_token = html5_tokenizer_next_token(tokenizer);
    ASSERT_NE(char_token, nullptr);
    EXPECT_EQ(char_token->type, HTML5_TOKEN_CHARACTER);

    // get EOF token
    Html5Token* eof_token = html5_tokenizer_next_token(tokenizer);
    ASSERT_NE(eof_token, nullptr);
    EXPECT_EQ(eof_token->type, HTML5_TOKEN_EOF);

    html5_tokenizer_destroy(tokenizer);
}

// ============================================================================
// Error Handling Tests (basic)
// ============================================================================

TEST_F(Html5TokenizerTest, InvalidTag) {
    // This should generate an error but still produce tokens
    auto tokens = tokenize("<>");

    // Depending on error recovery, this might emit '<' and '>' as characters
    // For now, just verify it doesn't crash
    EXPECT_GE(tokens.size(), (size_t)0);
}

TEST_F(Html5TokenizerTest, UnterminatedTag) {
    // Tag without closing >
    auto tokens = tokenize("<div");

    // Should emit EOF, exact behavior depends on error recovery
    EXPECT_GE(tokens.size(), (size_t)0);
}

// ============================================================================
// Position Tracking Tests
// ============================================================================

TEST_F(Html5TokenizerTest, LineColumnTracking) {
    const char* html = "a\nb";
    Html5Tokenizer* tokenizer = html5_tokenizer_create(pool, html, strlen(html));

    EXPECT_EQ(tokenizer->line, (size_t)1);
    EXPECT_EQ(tokenizer->column, (size_t)1);

    // consume 'a'
    Html5Token* t1 = html5_tokenizer_next_token(tokenizer);
    EXPECT_EQ(t1->line, (size_t)1);

    // consume '\n'
    Html5Token* t2 = html5_tokenizer_next_token(tokenizer);
    EXPECT_EQ(t2->line, (size_t)1);  // newline is on line 1

    // consume 'b' - should be on line 2
    Html5Token* t3 = html5_tokenizer_next_token(tokenizer);
    EXPECT_EQ(t3->line, (size_t)2);

    html5_tokenizer_destroy(tokenizer);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
