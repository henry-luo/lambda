#include <gtest/gtest.h>
#include "../lambda/format/html_encoder.hpp"
#include "../lib/strbuf.h"
#include <string>

using namespace html;

// Helper functions for test convenience - wraps new API to return std::string
static std::string escape(const char* text) {
    StrBuf* sb = strbuf_new();
    HtmlEncoder::escape(sb, text);
    std::string result(sb->str);
    strbuf_free(sb);
    return result;
}

static std::string escape_attribute(const char* text) {
    StrBuf* sb = strbuf_new();
    HtmlEncoder::escape_attribute(sb, text);
    std::string result(sb->str);
    strbuf_free(sb);
    return result;
}

TEST(HtmlEncoder, BasicEscaping) {
    EXPECT_EQ(escape("hello"), "hello");
    EXPECT_EQ(escape("A & B"), "A &amp; B");
    EXPECT_EQ(escape("1 < 2"), "1 &lt; 2");
    EXPECT_EQ(escape("x > y"), "x &gt; y");
    EXPECT_EQ(escape("say \"hi\""), "say &quot;hi&quot;");
}

TEST(HtmlEncoder, MultipleCharacters) {
    EXPECT_EQ(escape("A&B<C>D\"E"),
              "A&amp;B&lt;C&gt;D&quot;E");
}

TEST(HtmlEncoder, NoEscapeNeeded) {
    std::string text = "normal text without special chars";
    EXPECT_EQ(escape(text.c_str()), text);
}

TEST(HtmlEncoder, AttributeEscaping) {
    EXPECT_EQ(escape_attribute("value='test'"),
              "value=&#39;test&#39;");
    EXPECT_EQ(escape_attribute("A&B<C>D\"E'F"),
              "A&amp;B&lt;C&gt;D&quot;E&#39;F");
}

TEST(HtmlEncoder, NeedsEscaping) {
    EXPECT_FALSE(HtmlEncoder::needs_escaping("normal text"));
    EXPECT_TRUE(HtmlEncoder::needs_escaping("A & B"));
    EXPECT_TRUE(HtmlEncoder::needs_escaping("<tag>"));
    EXPECT_TRUE(HtmlEncoder::needs_escaping("say \"hi\""));
}

TEST(HtmlEncoder, EmptyString) {
    EXPECT_EQ(escape(""), "");
    EXPECT_FALSE(HtmlEncoder::needs_escaping(""));
}

TEST(HtmlEncoder, Constants) {
    EXPECT_STREQ(HtmlEncoder::NBSP, "&nbsp;");
    EXPECT_STREQ(HtmlEncoder::ZWSP, "\u200B");
    EXPECT_STREQ(HtmlEncoder::SHY, "&shy;");
    EXPECT_STREQ(HtmlEncoder::MDASH, "—");
    EXPECT_STREQ(HtmlEncoder::NDASH, "–");
}

TEST(HtmlEncoder, RealWorldExample) {
    // From LaTeX: \# \$ \^{} \& \_ \{ \} \%
    // Expected output: # $ ^ &amp; _ { } %
    std::string input = "# $ ^ & _ { } %";
    std::string expected = "# $ ^ &amp; _ { } %";
    EXPECT_EQ(escape(input.c_str()), expected);
}

TEST(HtmlEncoder, PreserveUnicode) {
    // Unicode should pass through unchanged
    std::string input = "Hello 世界 café";
    EXPECT_EQ(escape(input.c_str()), input);
}

TEST(HtmlEncoder, ConsecutiveSpecialChars) {
    EXPECT_EQ(escape("&&"), "&amp;&amp;");
    EXPECT_EQ(escape("<<>>"), "&lt;&lt;&gt;&gt;");
    EXPECT_EQ(escape("\"\""), "&quot;&quot;");
}
