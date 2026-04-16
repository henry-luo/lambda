// test_npm_semver_gtest.cpp — Unit tests for npm semver parser and range matcher

#include <gtest/gtest.h>
#include "../lambda/npm/semver.h"
#include <cstring>

// ---------------------------------------------------------------------------
// SemVer parsing
// ---------------------------------------------------------------------------

TEST(SemverParse, BasicVersion) {
    SemVer v = semver_parse("1.2.3");
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
    EXPECT_STREQ(v.prerelease, "");
    EXPECT_STREQ(v.build, "");
}

TEST(SemverParse, LeadingV) {
    SemVer v = semver_parse("v2.0.0");
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.major, 2);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(SemverParse, Prerelease) {
    SemVer v = semver_parse("1.0.0-alpha.1");
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
    EXPECT_STREQ(v.prerelease, "alpha.1");
}

TEST(SemverParse, BuildMetadata) {
    SemVer v = semver_parse("1.0.0+build.42");
    EXPECT_TRUE(v.valid);
    EXPECT_STREQ(v.build, "build.42");
}

TEST(SemverParse, PrereleaseAndBuild) {
    SemVer v = semver_parse("1.0.0-beta.2+build.123");
    EXPECT_TRUE(v.valid);
    EXPECT_STREQ(v.prerelease, "beta.2");
    EXPECT_STREQ(v.build, "build.123");
}

TEST(SemverParse, MajorOnly) {
    SemVer v = semver_parse("5");
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.major, 5);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(SemverParse, MajorMinorOnly) {
    SemVer v = semver_parse("3.7");
    EXPECT_TRUE(v.valid);
    EXPECT_EQ(v.major, 3);
    EXPECT_EQ(v.minor, 7);
    EXPECT_EQ(v.patch, 0);
}

// ---------------------------------------------------------------------------
// SemVer comparison
// ---------------------------------------------------------------------------

TEST(SemverCompare, Equal) {
    SemVer a = semver_parse("1.2.3");
    SemVer b = semver_parse("1.2.3");
    EXPECT_EQ(semver_compare(&a, &b), 0);
}

TEST(SemverCompare, MajorDiff) {
    SemVer a = semver_parse("2.0.0");
    SemVer b = semver_parse("1.0.0");
    EXPECT_GT(semver_compare(&a, &b), 0);
    EXPECT_LT(semver_compare(&b, &a), 0);
}

TEST(SemverCompare, MinorDiff) {
    SemVer a = semver_parse("1.3.0");
    SemVer b = semver_parse("1.2.0");
    EXPECT_GT(semver_compare(&a, &b), 0);
}

TEST(SemverCompare, PatchDiff) {
    SemVer a = semver_parse("1.2.4");
    SemVer b = semver_parse("1.2.3");
    EXPECT_GT(semver_compare(&a, &b), 0);
}

TEST(SemverCompare, PrereleaseVsRelease) {
    // release > prerelease
    SemVer a = semver_parse("1.0.0");
    SemVer b = semver_parse("1.0.0-alpha");
    EXPECT_GT(semver_compare(&a, &b), 0);
}

TEST(SemverCompare, PrereleaseOrder) {
    // alpha < beta
    SemVer a = semver_parse("1.0.0-alpha");
    SemVer b = semver_parse("1.0.0-beta");
    EXPECT_LT(semver_compare(&a, &b), 0);
}

TEST(SemverCompare, NumericPrerelease) {
    // 1.0.0-1 < 1.0.0-2
    SemVer a = semver_parse("1.0.0-1");
    SemVer b = semver_parse("1.0.0-2");
    EXPECT_LT(semver_compare(&a, &b), 0);
}

TEST(SemverCompare, BuildIgnored) {
    SemVer a = semver_parse("1.0.0+build.1");
    SemVer b = semver_parse("1.0.0+build.2");
    EXPECT_EQ(semver_compare(&a, &b), 0);
}

// ---------------------------------------------------------------------------
// SemVer formatting
// ---------------------------------------------------------------------------

TEST(SemverFormat, Basic) {
    SemVer v = semver_parse("1.2.3");
    char buf[128];
    semver_format(&v, buf, sizeof(buf));
    EXPECT_STREQ(buf, "1.2.3");
}

TEST(SemverFormat, WithPrerelease) {
    SemVer v = semver_parse("1.0.0-rc.1");
    char buf[128];
    semver_format(&v, buf, sizeof(buf));
    EXPECT_STREQ(buf, "1.0.0-rc.1");
}

// ---------------------------------------------------------------------------
// Range parsing and matching
// ---------------------------------------------------------------------------

TEST(SemverRange, ExactMatch) {
    SemVerRange r = semver_range_parse("1.2.3");
    EXPECT_TRUE(r.valid);
    SemVer v1 = semver_parse("1.2.3");
    SemVer v2 = semver_parse("1.2.4");
    EXPECT_TRUE(semver_satisfies(&v1, &r));
    EXPECT_FALSE(semver_satisfies(&v2, &r));
}

TEST(SemverRange, GreaterEqual) {
    SemVerRange r = semver_range_parse(">=1.2.3");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.2.3"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.3.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.2.2"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, Combined) {
    SemVerRange r = semver_range_parse(">=1.0.0 <2.0.0");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.9.9"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.0.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("0.9.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, TildeMinorPatch) {
    // ~1.2.3 → >=1.2.3 <1.3.0
    SemVerRange r = semver_range_parse("~1.2.3");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.2.3"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.2.9"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.3.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.2.2"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, TildeMajorOnly) {
    // ~1 → >=1.0.0 <2.0.0
    SemVerRange r = semver_range_parse("~1");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.9.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.0.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, CaretNonZero) {
    // ^1.2.3 → >=1.2.3 <2.0.0
    SemVerRange r = semver_range_parse("^1.2.3");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.2.3"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.9.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.0.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.2.2"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, CaretZeroMinor) {
    // ^0.2.3 → >=0.2.3 <0.3.0
    SemVerRange r = semver_range_parse("^0.2.3");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("0.2.3"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("0.2.9"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("0.3.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, CaretZeroZero) {
    // ^0.0.3 → >=0.0.3 <0.0.4
    SemVerRange r = semver_range_parse("^0.0.3");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("0.0.3"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("0.0.4"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("0.0.2"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, HyphenRange) {
    // 1.0.0 - 2.0.0 → >=1.0.0 <=2.0.0
    SemVerRange r = semver_range_parse("1.0.0 - 2.0.0");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("1.5.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.0.1"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("0.9.9"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, OrRanges) {
    // >=1.0.0 <2.0.0 || >=3.0.0
    SemVerRange r = semver_range_parse(">=1.0.0 <2.0.0 || >=3.0.0");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.5.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("3.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("5.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("2.5.0"); EXPECT_FALSE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, Star) {
    // * matches everything
    SemVerRange r = semver_range_parse("*");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("0.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
    { SemVer _v = semver_parse("99.99.99"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
}

TEST(SemverRange, EmptyMatchesAll) {
    SemVerRange r = semver_range_parse("");
    EXPECT_TRUE(r.valid);
    { SemVer _v = semver_parse("1.0.0"); EXPECT_TRUE(semver_satisfies(&_v, &r)); }
}

// ---------------------------------------------------------------------------
// Max satisfying
// ---------------------------------------------------------------------------

TEST(SemverMaxSatisfying, Basic) {
    SemVer versions[] = {
        semver_parse("1.0.0"),
        semver_parse("1.5.0"),
        semver_parse("2.0.0"),
        semver_parse("2.1.0"),
        semver_parse("3.0.0"),
    };
    SemVerRange range = semver_range_parse("^1.0.0");
    int best = semver_max_satisfying(versions, 5, &range);
    EXPECT_EQ(best, 1); // 1.5.0
}

TEST(SemverMaxSatisfying, NoneMatch) {
    SemVer versions[] = {
        semver_parse("1.0.0"),
        semver_parse("2.0.0"),
    };
    SemVerRange range = semver_range_parse(">=3.0.0");
    int best = semver_max_satisfying(versions, 2, &range);
    EXPECT_EQ(best, -1);
}

TEST(SemverMaxSatisfying, PreReleaseOrdering) {
    SemVer versions[] = {
        semver_parse("1.0.0-alpha"),
        semver_parse("1.0.0-beta"),
        semver_parse("1.0.0"),
    };
    SemVerRange range = semver_range_parse(">=1.0.0-alpha");
    int best = semver_max_satisfying(versions, 3, &range);
    EXPECT_EQ(best, 2); // 1.0.0 (release > any prerelease)
}
