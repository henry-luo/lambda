#include <gtest/gtest.h>
#include "../lambda/format/html_encoder.hpp"

using namespace html;

TEST(HtmlEncoder, BasicEscaping) {
    EXPECT_EQ(HtmlEncoder::escape("hello"), "hello");
    EXPECT_EQ(HtmlEncoder::escape("A & B"), "A &amp; B");
    EXPECT_EQ(HtmlEncoder::escape("1 < 2"), "1 &lt; 2");
    EXPECT_EQ(HtmlEncoder::escape("x > y"), "x &gt; y");
    EXPECT_EQ(HtmlEncoder::escape("say \"hi\""), "say &quot;hi&quot;");
}

TEST(HtmlEncoder, MultipleCharacters) {
    EXPECT_EQ(HtmlEncoder::escape("A&B<C>D\"E"),
              "A&amp;B&lt;C&gt;D&quot;E");
}

TEST(HtmlEncoder, NoEscapeNeeded) {
    std::string text = "normal text without special chars";
    EXPECT_EQ(HtmlEncoder::escape(text), text);
}

TEST(HtmlEncoder, AttributeEscaping) {
    EXPECT_EQ(HtmlEncoder::escape_attribute("value='test'"),
              "value=&#39;test&#39;");
    EXPECT_EQ(HtmlEncoder::escape_attribute("A&B<C>D\"E'F"),
              "A&amp;B&lt;C&gt;D&quot;E&#39;F");
}

TEST(HtmlEncoder, NeedsEscaping) {
    EXPECT_FALSE(HtmlEncoder::needs_escaping("normal text"));
    EXPECT_TRUE(HtmlEncoder::needs_escaping("A & B"));
    EXPECT_TRUE(HtmlEncoder::needs_escaping("<tag>"));
    EXPECT_TRUE(HtmlEncoder::needs_escaping("say \"hi\""));
}

TEST(HtmlEncoder, EmptyString) {
    EXPECT_EQ(HtmlEncoder::escape(""), "");
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
    EXPECT_EQ(HtmlEncoder::escape(input), expected);
}

TEST(HtmlEncoder, PreserveUnicode) {
    // Unicode should pass through unchanged
    std::string input = "Hello 世界 café";
    EXPECT_EQ(HtmlEncoder::escape(input), input);
}

TEST(HtmlEncoder, ConsecutiveSpecialChars) {
    EXPECT_EQ(HtmlEncoder::escape("&&"), "&amp;&amp;");
    EXPECT_EQ(HtmlEncoder::escape("<<>>"), "&lt;&lt;&gt;&gt;");
    EXPECT_EQ(HtmlEncoder::escape("\"\""), "&quot;&quot;");
}
