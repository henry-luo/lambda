#include <gtest/gtest.h>
#include <string.h>

#include "../lib/span.hpp"

// ---- Span<T> -------------------------------------------------------------

TEST(Span, SizeAndElementAccess) {
    int data[4] = { 10, 20, 30, 40 };
    lam::Span<int> s(data, 4);

    EXPECT_EQ(s.size(), 4u);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s[0], 10);
    EXPECT_EQ(s[3], 40);

    s[1] = 99;                 // operator[] yields a mutable reference
    EXPECT_EQ(data[1], 99);
}

TEST(Span, GetIsNonAbortingProbe) {
    int data[2] = { 1, 2 };
    lam::Span<int> s(data, 2);

    int out = -1;
    EXPECT_TRUE(s.get(1, &out));
    EXPECT_EQ(out, 2);

    out = -1;
    EXPECT_FALSE(s.get(2, &out));   // out of range — no write, no abort
    EXPECT_EQ(out, -1);
}

TEST(Span, SubspanClampsAndNeverOverReads) {
    int data[5] = { 0, 1, 2, 3, 4 };
    lam::Span<int> s(data, 5);

    lam::Span<int> mid = s.subspan(1, 2);
    EXPECT_EQ(mid.size(), 2u);
    EXPECT_EQ(mid[0], 1);
    EXPECT_EQ(mid[1], 2);

    lam::Span<int> past = s.subspan(10, 4);   // offset beyond end
    EXPECT_EQ(past.size(), 0u);

    lam::Span<int> over = s.subspan(3, 100);  // length beyond end
    EXPECT_EQ(over.size(), 2u);                // clamped to remaining
}

TEST(Span, RangeForIteration) {
    int data[3] = { 5, 6, 7 };
    lam::Span<int> s(data, 3);
    int sum = 0;
    for (int v : s) sum += v;
    EXPECT_EQ(sum, 18);
}

#if defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
TEST(SpanDeathTest, OutOfBoundsIndexAborts) {
    int data[2] = { 1, 2 };
    lam::Span<int> s(data, 2);
    EXPECT_DEATH({ volatile int x = s[5]; (void)x; }, "SPAN-OOB");
}
#endif

// ---- ByteCursor ----------------------------------------------------------

TEST(ByteCursor, BasicTraversal) {
    const char* text = "abc";
    lam::ByteCursor c = lam::ByteCursor::of(text, 3);

    EXPECT_EQ(c.remaining(), 3u);
    EXPECT_FALSE(c.eof());
    EXPECT_EQ(c.peek(), 'a');
    EXPECT_EQ(c.peek(2), 'c');

    uint8_t b = 0;
    EXPECT_TRUE(c.take(&b));
    EXPECT_EQ(b, 'a');
    EXPECT_EQ(c.remaining(), 2u);
}

TEST(ByteCursor, PeekPastEndReturnsZeroNotOob) {
    const char* text = "x";
    lam::ByteCursor c = lam::ByteCursor::of(text, 1);
    EXPECT_EQ(c.peek(0), 'x');
    EXPECT_EQ(c.peek(1), 0);     // past end — defined 0, not an over-read
    EXPECT_EQ(c.peek(99), 0);
}

TEST(ByteCursor, AdvanceRefusesToWalkOffEnd) {
    const char* text = "hello";
    lam::ByteCursor c = lam::ByteCursor::of(text, 5);

    EXPECT_TRUE(c.advance(3));
    EXPECT_EQ(c.remaining(), 2u);
    EXPECT_FALSE(c.advance(5));   // not enough left — no movement
    EXPECT_EQ(c.remaining(), 2u);
    EXPECT_TRUE(c.advance(2));
    EXPECT_TRUE(c.eof());
    uint8_t dummy = 0;
    EXPECT_FALSE(c.take(&dummy));  // eof — nothing left to consume
}

// Regression for the H1 finding: a truncated "\u" escape must not advance past the buffer.
TEST(ByteCursor, TruncatedUnicodeEscapeIsRejected) {
    const char* text = "\\u";   // backslash, 'u', then end — no 4 hex digits
    lam::ByteCursor c = lam::ByteCursor::of(text, strlen(text));

    ASSERT_TRUE(c.advance(2));   // consume "\u"
    EXPECT_FALSE(c.has(4));      // the guard that input-mark.cpp is missing
    EXPECT_FALSE(c.advance(4));  // refuses — H1 over-read structurally impossible
    EXPECT_TRUE(c.eof());
}
