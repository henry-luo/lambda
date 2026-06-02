#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

extern "C" {
#include "../../lib/color.h"
}

struct RGBA { uint8_t r, g, b, a; };
static bool parse(const char* s, RGBA* out) {
    return color_parse_hex(s, &out->r, &out->g, &out->b, &out->a);
}

TEST(ColorTest, SixDigit) {
    RGBA c;
    ASSERT_TRUE(parse("#ff6600", &c));
    EXPECT_EQ(c.r, 0xFF); EXPECT_EQ(c.g, 0x66); EXPECT_EQ(c.b, 0x00); EXPECT_EQ(c.a, 0xFF);
}

TEST(ColorTest, NoHashAccepted) {
    RGBA c;
    ASSERT_TRUE(parse("ff6600", &c));
    EXPECT_EQ(c.r, 0xFF); EXPECT_EQ(c.g, 0x66); EXPECT_EQ(c.b, 0x00);
}

TEST(ColorTest, ThreeDigitExpands) {
    RGBA c;
    ASSERT_TRUE(parse("#f60", &c));
    EXPECT_EQ(c.r, 0xFF); EXPECT_EQ(c.g, 0x66); EXPECT_EQ(c.b, 0x00); EXPECT_EQ(c.a, 0xFF);
}

TEST(ColorTest, EightDigitAlpha) {
    RGBA c;
    ASSERT_TRUE(parse("#11223344", &c));
    EXPECT_EQ(c.r, 0x11); EXPECT_EQ(c.g, 0x22); EXPECT_EQ(c.b, 0x33); EXPECT_EQ(c.a, 0x44);
}

TEST(ColorTest, FourDigitAlphaExpands) {
    RGBA c;
    ASSERT_TRUE(parse("#1234", &c));
    EXPECT_EQ(c.r, 0x11); EXPECT_EQ(c.g, 0x22); EXPECT_EQ(c.b, 0x33); EXPECT_EQ(c.a, 0x44);
}

TEST(ColorTest, CaseInsensitive) {
    RGBA lo, up;
    ASSERT_TRUE(parse("#abcdef", &lo));
    ASSERT_TRUE(parse("#ABCDEF", &up));
    EXPECT_EQ(lo.r, up.r); EXPECT_EQ(lo.g, up.g); EXPECT_EQ(lo.b, up.b);
}

TEST(ColorTest, RejectsInvalid) {
    RGBA c;
    EXPECT_FALSE(parse("#xyz", &c));      // non-hex
    EXPECT_FALSE(parse("#12345", &c));    // bad length (5)
    EXPECT_FALSE(parse("#1", &c));        // bad length (1)
    EXPECT_FALSE(parse("#gg00ff", &c));   // non-hex digit
    EXPECT_FALSE(parse("", &c));          // empty
    EXPECT_FALSE(parse("#", &c));         // just hash
}

TEST(ColorTest, FormatHex) {
    char out[8];
    color_format_hex(0xFF, 0x66, 0x00, out);
    EXPECT_STREQ(out, "#ff6600");
    color_format_hex(0x00, 0x00, 0x00, out);
    EXPECT_STREQ(out, "#000000");
}

TEST(ColorTest, ParseFormatRoundTrip) {
    RGBA c;
    ASSERT_TRUE(parse("#deadbe", &c));
    char out[8];
    color_format_hex(c.r, c.g, c.b, out);
    EXPECT_STREQ(out, "#deadbe");
}
