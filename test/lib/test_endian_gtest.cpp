#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

extern "C" {
#include "../../lib/endian.h"
}

TEST(EndianTest, ReadBigEndian) {
    uint8_t b[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    EXPECT_EQ(read_be16(b), 0x1234u);
    EXPECT_EQ(read_be24(b), 0x123456u);
    EXPECT_EQ(read_be32(b), 0x12345678u);
    EXPECT_EQ(read_be64(b), 0x123456789ABCDEF0ull);
}

TEST(EndianTest, ReadLittleEndian) {
    uint8_t b[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    EXPECT_EQ(read_le16(b), 0x3412u);
    EXPECT_EQ(read_le32(b), 0x78563412u);
    EXPECT_EQ(read_le64(b), 0xF0DEBC9A78563412ull);
}

TEST(EndianTest, SignedReads) {
    uint8_t neg16[2] = {0xFF, 0xFE}; // be: 0xFFFE = -2
    EXPECT_EQ(read_be16s(neg16), -2);
    uint8_t neg32[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(read_be32s(neg32), -1);
    uint8_t pos16[2] = {0x00, 0x7F};
    EXPECT_EQ(read_be16s(pos16), 0x7F);
}

TEST(EndianTest, WriteRoundTrip) {
    uint8_t buf[8];
    write_be16(buf, 0xBEEF);
    EXPECT_EQ(read_be16(buf), 0xBEEFu);
    write_be32(buf, 0xDEADBEEF);
    EXPECT_EQ(read_be32(buf), 0xDEADBEEFu);
    write_le16(buf, 0xBEEF);
    EXPECT_EQ(read_le16(buf), 0xBEEFu);
    write_le32(buf, 0xDEADBEEF);
    EXPECT_EQ(read_le32(buf), 0xDEADBEEFu);
}

TEST(EndianTest, ByteSwap) {
    EXPECT_EQ(bswap16(0x1234), 0x3412u);
    EXPECT_EQ(bswap32(0x12345678u), 0x78563412u);
    EXPECT_EQ(bswap64(0x0123456789ABCDEFull), 0xEFCDAB8967452301ull);
    // double swap is identity
    EXPECT_EQ(bswap32(bswap32(0xCAFEBABEu)), 0xCAFEBABEu);
}

TEST(EndianTest, BeLeAgreeViaSwap) {
    uint8_t b[4] = {0x01, 0x02, 0x03, 0x04};
    EXPECT_EQ(read_be32(b), bswap32(read_le32(b)));
}
