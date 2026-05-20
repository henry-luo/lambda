// test/lib/test_binsearch_gtest.cpp - tests for lib/binsearch
#include <gtest/gtest.h>
#include <cstring>
#include "../../lib/binsearch.h"

namespace {

const char* kTable[] = {
    "alpha", "beta", "delta", "gamma", "omega", "sigma", "zeta"
};
constexpr int kTableCount = sizeof(kTable) / sizeof(kTable[0]);

const char* kCaseTable[] = {
    "Alpha", "Beta", "Delta", "Gamma", "Omega", "Sigma", "Zeta"
};
constexpr int kCaseTableCount = sizeof(kCaseTable) / sizeof(kCaseTable[0]);

struct Record {
    int key;
    const char* label;
};
const Record kRecords[] = {
    {1, "one"}, {3, "three"}, {7, "seven"}, {12, "twelve"}, {42, "forty-two"}
};
constexpr int kRecordCount = sizeof(kRecords) / sizeof(kRecords[0]);

int cmp_record_by_key(const void* record, const void* key, void* udata) {
    (void)udata;
    int rkey = ((const Record*)record)->key;
    int qkey = *(const int*)key;
    return (rkey > qkey) - (rkey < qkey);
}

}  // namespace

TEST(BinsearchTest, StrtabHit) {
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "alpha", false), 0);
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "gamma", false), 3);
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "zeta", false), 6);
}

TEST(BinsearchTest, StrtabMiss) {
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "aardvark", false), -1);
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "epsilon", false), -1);
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "zzz", false), -1);
}

TEST(BinsearchTest, StrtabCaseSensitiveRespectsCase) {
    // case-sensitive: capital "Beta" != "beta"
    EXPECT_EQ(binsearch_strtab(kCaseTable, kCaseTableCount, "Beta", false), 1);
    EXPECT_EQ(binsearch_strtab(kCaseTable, kCaseTableCount, "beta", false), -1);
}

TEST(BinsearchTest, StrtabCaseInsensitive) {
    EXPECT_EQ(binsearch_strtab(kCaseTable, kCaseTableCount, "BETA", true), 1);
    EXPECT_EQ(binsearch_strtab(kCaseTable, kCaseTableCount, "beta", true), 1);
    EXPECT_EQ(binsearch_strtab(kCaseTable, kCaseTableCount, "OmEgA", true), 4);
    EXPECT_EQ(binsearch_strtab(kCaseTable, kCaseTableCount, "missing", true), -1);
}

TEST(BinsearchTest, StrtabPrefixOfTableEntryMisses) {
    // "alph" is a prefix of "alpha" but shouldn't match — strncmp with len=4
    // would equal, but the helper enforces equal lengths via the s[key_len] check.
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "alph", false), -1);
    // and the table value being a prefix of the key likewise misses
    EXPECT_EQ(binsearch_strtab(kTable, kTableCount, "alphax", false), -1);
}

TEST(BinsearchTest, StrtabNVariantUsesLengthBound) {
    const char* key = "alpha_extra";
    // looking up just "alpha" inside a longer buffer
    EXPECT_EQ(binsearch_strtab_n(kTable, kTableCount, key, 5, false), 0);
    EXPECT_EQ(binsearch_strtab_n(kTable, kTableCount, key, 11, false), -1);
}

TEST(BinsearchTest, EmptyTable) {
    const char* empty[] = {nullptr};
    EXPECT_EQ(binsearch_strtab(empty, 0, "anything", false), -1);
}

TEST(BinsearchTest, SingleEntryTable) {
    const char* one[] = {"solo"};
    EXPECT_EQ(binsearch_strtab(one, 1, "solo", false), 0);
    EXPECT_EQ(binsearch_strtab(one, 1, "duet", false), -1);
}

TEST(BinsearchTest, RecordsGeneric) {
    int q = 7;
    EXPECT_EQ(binsearch_records(kRecords, kRecordCount, sizeof(Record),
                                 &q, cmp_record_by_key, nullptr), 2);
    q = 1;
    EXPECT_EQ(binsearch_records(kRecords, kRecordCount, sizeof(Record),
                                 &q, cmp_record_by_key, nullptr), 0);
    q = 42;
    EXPECT_EQ(binsearch_records(kRecords, kRecordCount, sizeof(Record),
                                 &q, cmp_record_by_key, nullptr), 4);
    q = 5;  // not present
    EXPECT_EQ(binsearch_records(kRecords, kRecordCount, sizeof(Record),
                                 &q, cmp_record_by_key, nullptr), -1);
}

// ───────── binsearch_range ─────────

namespace {

// Sorted disjoint ranges:  [0,100)  [200,300)  [500,1000)
struct AddrRange { uint32_t start; uint32_t end; const char* label; };
const AddrRange kRanges[] = {
    {0,    100,  "first"},
    {200,  300,  "second"},
    {500,  1000, "third"},
};
constexpr int kRangeCount = sizeof(kRanges) / sizeof(kRanges[0]);

// range_cmp:
//   -1 if record entirely < key   (search higher)
//    0 if key is in [start, end)
//   +1 if record entirely > key   (search lower)
int range_cmp_addr(const void* record, const void* key, void* udata) {
    (void)udata;
    const AddrRange* r = (const AddrRange*)record;
    uint32_t q = *(const uint32_t*)key;
    if (q < r->start) return 1;
    if (q >= r->end)  return -1;
    return 0;
}

}  // namespace

TEST(BinsearchTest, RangeHit) {
    uint32_t q;
    q = 0;    EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), 0);
    q = 50;   EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), 0);
    q = 99;   EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), 0);
    q = 200;  EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), 1);
    q = 250;  EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), 1);
    q = 999;  EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), 2);
}

TEST(BinsearchTest, RangeMiss) {
    uint32_t q;
    q = 100;  // gap between first and second
    EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), -1);
    q = 150;
    EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), -1);
    q = 300;  // gap between second and third
    EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), -1);
    q = 1000; // past last range
    EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), -1);
    q = 99999;
    EXPECT_EQ(binsearch_range(kRanges, kRangeCount, sizeof(AddrRange), &q, range_cmp_addr, nullptr), -1);
}
