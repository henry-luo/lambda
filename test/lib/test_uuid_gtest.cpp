#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <cctype>

extern "C" {
#include "../../lib/uuid.h"
}

TEST(UuidTest, FormatShape) {
    uint8_t bytes[16];
    for (int i = 0; i < 16; i++) bytes[i] = (uint8_t)i;
    char out[UUID_STR_LEN];
    uuid_v4_format(bytes, out);

    EXPECT_EQ(strlen(out), 36u);
    // hyphen positions
    EXPECT_EQ(out[8], '-');
    EXPECT_EQ(out[13], '-');
    EXPECT_EQ(out[18], '-');
    EXPECT_EQ(out[23], '-');
    // every other char is a lowercase hex digit
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        char c = out[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) << "pos " << i;
    }
}

TEST(UuidTest, VersionAndVariantBits) {
    uint8_t bytes[16];
    memset(bytes, 0xFF, sizeof(bytes));
    char out[UUID_STR_LEN];
    uuid_v4_format(bytes, out);
    // version nibble (index 14 in the string) must be '4'
    EXPECT_EQ(out[14], '4');
    // variant nibble (index 19) must be 8/9/a/b
    char v = out[19];
    EXPECT_TRUE(v == '8' || v == '9' || v == 'a' || v == 'b') << "variant=" << v;
}

TEST(UuidTest, KnownVector) {
    // all-zero entropy -> version/variant bits set, rest zero
    uint8_t bytes[16];
    memset(bytes, 0, sizeof(bytes));
    char out[UUID_STR_LEN];
    uuid_v4_format(bytes, out);
    EXPECT_STREQ(out, "00000000-0000-4000-8000-000000000000");
}
