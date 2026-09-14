// test/lib/test_hash_gtest.cpp - tests for lib/hash inline hashers.
#include <gtest/gtest.h>
#include <cstring>
#include <unordered_set>
#include "../../lib/hash.h"

TEST(HashTest, Djb2DeterministicAndMatchesCstrVariant) {
    const char* s = "Hello, World!";
    EXPECT_EQ(hash_djb2(s, strlen(s)), hash_djb2_cstr(s));
}

TEST(HashTest, Djb2EmptyStringSeed) {
    EXPECT_EQ(hash_djb2("", 0), 5381u);
    EXPECT_EQ(hash_djb2_cstr(""), 5381u);
}

TEST(HashTest, Djb2DistinguishesShortInputs) {
    EXPECT_NE(hash_djb2_cstr("a"), hash_djb2_cstr("b"));
    EXPECT_NE(hash_djb2_cstr("ab"), hash_djb2_cstr("ba"));
}

TEST(HashTest, Djb2AddExtendPreservesInitialState) {
    const char* text = "lambda";
    uint64_t expected = 0;
    for (const char* cursor = text; *cursor; cursor++) {
        expected = expected * 33 + (unsigned char)*cursor;
    }
    EXPECT_EQ(hash_djb2_add_extend(0, text, strlen(text)), expected);
    EXPECT_EQ(hash_djb2_add_extend_cstr(5381, text),
        hash_djb2_add_extend(5381, text, strlen(text)));
}

TEST(HashTest, Fnv1a32DeterministicAndMatchesCstrVariant) {
    const char* s = "lambda";
    EXPECT_EQ(hash_fnv1a_32(s, strlen(s)), hash_fnv1a_32_cstr(s));
}

TEST(HashTest, Fnv1a64DeterministicAndMatchesCstrVariant) {
    const char* s = "lambda lambda lambda";
    EXPECT_EQ(hash_fnv1a_64(s, strlen(s)), hash_fnv1a_64_cstr(s));
    EXPECT_EQ(hash_cstr(s), hash_fnv1a_64_cstr(s));
}

TEST(HashTest, Fnv1a32EmptyStringSeed) {
    EXPECT_EQ(hash_fnv1a_32("", 0), 0x811c9dc5u);
}

TEST(HashTest, Fnv1a32ExtendPreservesCustomSeed) {
    const char* text = "lambda";
    uint32_t expected = 0x12345678u;
    for (const char* cursor = text; *cursor; cursor++) {
        expected = (expected ^ (unsigned char)*cursor) * 0x01000193u;
    }
    EXPECT_EQ(hash_fnv1a_32_extend(0x12345678u, text, strlen(text)), expected);
}

TEST(HashTest, Fnv1a64EmptyStringSeed) {
    EXPECT_EQ(hash_fnv1a_64("", 0), 0xcbf29ce484222325ULL);
}

TEST(HashTest, Fnv1a64ExtendMatchesOneShotAndCstr) {
    const char* prefix = "lambda ";
    const char* suffix = "script";
    uint64_t hash = hash_fnv1a_64_extend(HASH_FNV1A_64_OFFSET_BASIS,
        prefix, strlen(prefix));
    hash = hash_fnv1a_64_extend_cstr(hash, suffix);
    EXPECT_EQ(hash, hash_fnv1a_64_cstr("lambda script"));
}

TEST(HashTest, Fnv1a64ExtendU64LeMatchesExplicitBytes) {
    const unsigned char bytes[] = {0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};
    uint64_t hash = hash_fnv1a_64_extend_u64le(HASH_FNV1A_64_OFFSET_BASIS,
        UINT64_C(0x0123456789abcdef));
    EXPECT_EQ(hash, hash_fnv1a_64(bytes, sizeof(bytes)));
}

TEST(HashTest, CombineU64MatchesStableValue) {
    EXPECT_EQ(hash_combine_u64(UINT64_C(0xcbf29ce484222325),
        UINT64_C(0x0123456789abcdef)), UINT64_C(0x050c039fb6a5bf28));
}

TEST(HashTest, Fnv1a64DistributesCommonWords) {
    const char* words[] = {
        "alpha","beta","gamma","delta","epsilon","zeta","eta","theta",
        "iota","kappa","lambda","mu","nu","xi","omicron","pi","rho",
        "sigma","tau","upsilon","phi","chi","psi","omega",
    };
    std::unordered_set<uint64_t> seen;
    for (auto w : words) seen.insert(hash_fnv1a_64_cstr(w));
    EXPECT_EQ(seen.size(), sizeof(words) / sizeof(words[0]));
}

TEST(HashTest, HashPtrIsDeterministicAndDistinct) {
    int a = 0, b = 0;
    EXPECT_EQ(hash_ptr(&a), hash_ptr(&a));
    EXPECT_NE(hash_ptr(&a), hash_ptr(&b));
    EXPECT_EQ(hash_ptr(nullptr), hash_ptr(nullptr));
}

TEST(HashTest, BinaryDataHashing) {
    unsigned char bytes1[] = {0x00, 0x01, 0x02, 0x03};
    unsigned char bytes2[] = {0x03, 0x02, 0x01, 0x00};
    EXPECT_NE(hash_djb2(bytes1, 4), hash_djb2(bytes2, 4));
    EXPECT_NE(hash_fnv1a_32(bytes1, 4), hash_fnv1a_32(bytes2, 4));
    EXPECT_NE(hash_fnv1a_64(bytes1, 4), hash_fnv1a_64(bytes2, 4));
}
