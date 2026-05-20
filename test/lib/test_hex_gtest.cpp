// test/lib/test_hex_gtest.cpp - tests for lib/hex
#include <gtest/gtest.h>
#include <cstring>

#include "../../lib/hex.h"

TEST(HexTest, EncodeNibble) {
    EXPECT_EQ(hex_encode_nibble(0), '0');
    EXPECT_EQ(hex_encode_nibble(9), '9');
    EXPECT_EQ(hex_encode_nibble(10), 'a');
    EXPECT_EQ(hex_encode_nibble(15), 'f');
    EXPECT_EQ(hex_encode_nibble(16), '?');   // out of range
    EXPECT_EQ(hex_encode_nibble(255), '?');
}

TEST(HexTest, DecodeByte) {
    EXPECT_EQ(hex_decode_byte('0'), 0);
    EXPECT_EQ(hex_decode_byte('9'), 9);
    EXPECT_EQ(hex_decode_byte('a'), 10);
    EXPECT_EQ(hex_decode_byte('f'), 15);
    EXPECT_EQ(hex_decode_byte('A'), 10);     // uppercase accepted
    EXPECT_EQ(hex_decode_byte('F'), 15);
    EXPECT_EQ(hex_decode_byte('g'), -1);     // out of range
    EXPECT_EQ(hex_decode_byte('/'), -1);
    EXPECT_EQ(hex_decode_byte(':'), -1);
    EXPECT_EQ(hex_decode_byte('\0'), -1);
}

TEST(HexTest, EncodeEmpty) {
    char out[1] = {'X'};
    hex_encode("", 0, out);
    EXPECT_EQ(out[0], '\0');
}

TEST(HexTest, EncodeSingleByte) {
    char out[3];
    hex_encode("\xab", 1, out);
    EXPECT_STREQ(out, "ab");
}

TEST(HexTest, EncodeKnownPattern) {
    const unsigned char bytes[] = {0x00, 0x01, 0xfe, 0xff, 0xde, 0xad, 0xbe, 0xef};
    char out[2 * 8 + 1];
    hex_encode(bytes, 8, out);
    EXPECT_STREQ(out, "0001feffdeadbeef");
}

TEST(HexTest, DecodeEmpty) {
    unsigned char out[1] = {0xff};
    size_t out_len = 99;
    EXPECT_TRUE(hex_decode("", 0, out, &out_len));
    EXPECT_EQ(out_len, 0u);
    EXPECT_EQ(out[0], 0xff);  // untouched
}

TEST(HexTest, DecodeOddLengthRejected) {
    unsigned char out[4];
    EXPECT_FALSE(hex_decode("abc", 3, out, nullptr));
}

TEST(HexTest, DecodeInvalidCharRejected) {
    unsigned char out[4];
    EXPECT_FALSE(hex_decode("ab/c", 4, out, nullptr));
    EXPECT_FALSE(hex_decode("zz", 2, out, nullptr));
}

TEST(HexTest, DecodeRoundtripsBoth) {
    const char* src = "0001feffdeadbeefcafe";
    unsigned char buf[16];
    size_t n = 0;
    ASSERT_TRUE(hex_decode(src, strlen(src), buf, &n));
    EXPECT_EQ(n, 10u);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x01);
    EXPECT_EQ(buf[2], 0xfe);
    EXPECT_EQ(buf[3], 0xff);
    EXPECT_EQ(buf[8], 0xca);
    EXPECT_EQ(buf[9], 0xfe);

    char back[21];
    hex_encode(buf, 10, back);
    EXPECT_STREQ(back, src);
}

TEST(HexTest, DecodeAcceptsMixedCase) {
    unsigned char buf[2];
    size_t n;
    ASSERT_TRUE(hex_decode("aB12", 4, buf, &n));
    EXPECT_EQ(buf[0], 0xab);
    EXPECT_EQ(buf[1], 0x12);
    EXPECT_EQ(n, 2u);
}

TEST(HexTest, EncodeNibbleUpper) {
    EXPECT_EQ(hex_encode_nibble_upper(0), '0');
    EXPECT_EQ(hex_encode_nibble_upper(9), '9');
    EXPECT_EQ(hex_encode_nibble_upper(10), 'A');
    EXPECT_EQ(hex_encode_nibble_upper(15), 'F');
    EXPECT_EQ(hex_encode_nibble_upper(16), '?');
}

TEST(HexTest, EncodeUpperKnownPattern) {
    const unsigned char bytes[] = {0xde, 0xad, 0xbe, 0xef};
    char out[9];
    hex_encode_upper(bytes, 4, out);
    EXPECT_STREQ(out, "DEADBEEF");

    // hex_decode accepts both cases — verify round-trip
    unsigned char back[4];
    size_t n;
    ASSERT_TRUE(hex_decode(out, 8, back, &n));
    EXPECT_EQ(n, 4u);
    EXPECT_EQ(memcmp(back, bytes, 4), 0);
}
