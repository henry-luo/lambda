#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

extern "C" {
#include "../../lib/base64.h"
#include "../../lib/log.h"
}

class Base64Test : public ::testing::Test {
protected:
    void SetUp() override { log_init(NULL); }

    // helper: encode bytes to a std::string-free char buffer comparison
    static std::string enc(const char* s, Base64Variant v) {
        char* out = base64_encode_alloc(s, strlen(s), v);
        std::string r = out ? out : "";
        free(out);
        return r;
    }

    // helper: round-trip and compare against original bytes
    static void round_trip(const char* s, size_t n, Base64Variant v) {
        char* e = base64_encode_alloc(s, n, v);
        ASSERT_NE(e, nullptr);
        size_t dlen = 0;
        uint8_t* d = base64_decode_variant(e, 0, &dlen, v);
        // empty input encodes to "" and decodes to NULL with dlen==0
        if (n == 0) {
            EXPECT_EQ(dlen, 0u);
            free(e);
            free(d);
            return;
        }
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(dlen, n);
        EXPECT_EQ(memcmp(d, s, n), 0);
        free(e);
        free(d);
    }
};

// ── Standard alphabet, RFC 4648 test vectors ──
TEST_F(Base64Test, RfcVectorsStd) {
    EXPECT_EQ(enc("", BASE64_STD), "");
    EXPECT_EQ(enc("f", BASE64_STD), "Zg==");
    EXPECT_EQ(enc("fo", BASE64_STD), "Zm8=");
    EXPECT_EQ(enc("foo", BASE64_STD), "Zm9v");
    EXPECT_EQ(enc("foob", BASE64_STD), "Zm9vYg==");
    EXPECT_EQ(enc("fooba", BASE64_STD), "Zm9vYmE=");
    EXPECT_EQ(enc("foobar", BASE64_STD), "Zm9vYmFy");
}

// ── URL-safe alphabet is unpadded and uses - _ ──
TEST_F(Base64Test, UrlVariantUnpadded) {
    // 0xFB 0xFF -> std "+/8=" ; url "-_8"
    const unsigned char bytes[] = {0xFB, 0xFF};
    char std_out[16], url_out[16];
    base64_encode(bytes, 2, std_out, BASE64_STD);
    base64_encode(bytes, 2, url_out, BASE64_URL);
    EXPECT_STREQ(std_out, "+/8=");
    EXPECT_STREQ(url_out, "-_8");
}

TEST_F(Base64Test, EncodedLen) {
    EXPECT_EQ(base64_encoded_len(0, BASE64_STD), 0u);
    EXPECT_EQ(base64_encoded_len(1, BASE64_STD), 4u);
    EXPECT_EQ(base64_encoded_len(2, BASE64_STD), 4u);
    EXPECT_EQ(base64_encoded_len(3, BASE64_STD), 4u);
    EXPECT_EQ(base64_encoded_len(4, BASE64_STD), 8u);
    // url: unpadded ceil(n*4/3)
    EXPECT_EQ(base64_encoded_len(1, BASE64_URL), 2u);
    EXPECT_EQ(base64_encoded_len(2, BASE64_URL), 3u);
    EXPECT_EQ(base64_encoded_len(3, BASE64_URL), 4u);
}

// ── Round-trip across tail lengths, both variants ──
TEST_F(Base64Test, RoundTripStd) {
    round_trip("", 0, BASE64_STD);
    round_trip("a", 1, BASE64_STD);
    round_trip("ab", 2, BASE64_STD);
    round_trip("abc", 3, BASE64_STD);
    round_trip("abcd", 4, BASE64_STD);
    const char bin[] = {0x00, (char)0xFF, 0x10, (char)0x80, 0x7F, (char)0xAB};
    round_trip(bin, sizeof(bin), BASE64_STD);
}

TEST_F(Base64Test, RoundTripUrl) {
    round_trip("a", 1, BASE64_URL);
    round_trip("ab", 2, BASE64_URL);
    round_trip("abc", 3, BASE64_URL);
    const char bin[] = {(char)0xFB, (char)0xFF, 0x00, 0x3E, 0x3F};
    round_trip(bin, sizeof(bin), BASE64_URL);
}

// ── Decoder ignores embedded whitespace ──
TEST_F(Base64Test, DecodeWithWhitespace) {
    size_t dlen = 0;
    uint8_t* d = base64_decode("Zm9v\n YmFy", 0, &dlen);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(dlen, 6u);
    EXPECT_EQ(memcmp(d, "foobar", 6), 0);
    free(d);
}

// ── URL decode tolerates missing padding ──
TEST_F(Base64Test, DecodeUrlNoPadding) {
    size_t dlen = 0;
    uint8_t* d = base64_decode_variant("Zg", 0, &dlen, BASE64_URL); // "f"
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(dlen, 1u);
    EXPECT_EQ(d[0], 'f');
    free(d);
}

// ── A lone trailing data char (mod 4 == 1) is rejected ──
TEST_F(Base64Test, DecodeRejectsBadLength) {
    size_t dlen = 99;
    uint8_t* d = base64_decode("Z", 0, &dlen);
    EXPECT_EQ(d, nullptr);
    EXPECT_EQ(dlen, 0u);
}

// ── base64_encode writes exactly base64_encoded_len bytes ──
TEST_F(Base64Test, EncodeReturnLength) {
    char out[32];
    size_t n = base64_encode("foobar", 6, out, BASE64_STD);
    EXPECT_EQ(n, base64_encoded_len(6, BASE64_STD));
    EXPECT_EQ(out[n], '\0');
}
