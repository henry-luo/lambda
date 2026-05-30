#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "../../lib/digest.h"
#include "../../lib/hex.h"
#include "../../lib/log.h"
}

class DigestTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }
};

TEST_F(DigestTest, Sha256KnownVector) {
    uint8_t hash[32];
    char hex[65];

    ASSERT_TRUE(digest_sha256("abc", 3, hash));
    hex_encode(hash, sizeof(hash), hex);

    EXPECT_STREQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_F(DigestTest, Sha384KnownVector) {
    uint8_t hash[48];
    char hex[97];

    ASSERT_TRUE(digest_sha384("abc", 3, hash));
    hex_encode(hash, sizeof(hash), hex);

    EXPECT_STREQ(hex,
                 "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded163"
                 "1a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
}

TEST_F(DigestTest, Sha512KnownVector) {
    uint8_t hash[64];
    char hex[129];

    ASSERT_TRUE(digest_sha512("abc", 3, hash));
    hex_encode(hash, sizeof(hash), hex);

    EXPECT_STREQ(hex,
                 "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20"
                 "a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
                 "454d4423643ce80e2a9ac94fa54ca49f");
}

TEST_F(DigestTest, StreamingMatchesOneShot) {
    uint8_t one_shot[32];
    uint8_t streamed[32];
    char one_shot_hex[65];
    char streamed_hex[65];

    ASSERT_TRUE(digest_sha256("abc", 3, one_shot));

    DigestCtx* ctx = digest_ctx_new(DIGEST_SHA256);
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(digest_update(ctx, "a", 1));
    EXPECT_TRUE(digest_update(ctx, "bc", 2));
    EXPECT_TRUE(digest_finalize(ctx, streamed, sizeof(streamed)));
    digest_ctx_free(ctx);

    hex_encode(one_shot, sizeof(one_shot), one_shot_hex);
    hex_encode(streamed, sizeof(streamed), streamed_hex);
    EXPECT_STREQ(streamed_hex, one_shot_hex);
}

TEST_F(DigestTest, RejectsShortOutputBuffer) {
    uint8_t hash[31];

    EXPECT_FALSE(digest_compute_bits(DIGEST_SHA256, "abc", 3, hash, sizeof(hash)));
}
