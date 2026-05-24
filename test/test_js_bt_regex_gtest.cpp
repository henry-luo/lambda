// Unit tests for the JS backtracking regex matcher (lambda/js/js_bt_regex.cpp).
//
// These tests exercise the matcher's public C API directly (js_bt_compile /
// js_bt_exec / group + named-group introspection) without going through the
// whole JS runtime. Coverage focuses on the behaviours RE2 cannot do — and that
// the engine must therefore get right — plus the anti-DoS step budget, negative
// cases, malformed-pattern fallback, and stress inputs.
//
// The implementation is compiled in directly so the tests can drive it with
// precise inputs; only lib/ (mempool + log) is linked.

#include <gtest/gtest.h>
#include <chrono>
#include <cstring>
#include <string>

#include "../lambda/js/js_bt_regex.cpp"

namespace {

// One compile+exec round trip. The pool is destroyed before returning, but the
// integer match offsets have already been copied out, so the result is stable.
struct BtResult {
    int compiled;            // 1 if js_bt_compile succeeded, 0 if it returned NULL
    int matched;             // js_bt_exec return value (1 match, 0 no match)
    int group_count;         // capturing groups (excluding group 0)
    int starts[32];
    int ends[32];
    std::string input;       // kept so group_str() can slice it
};

static BtResult run_bt(const char* pat, const char* input, JsBtFlags flags,
                       int start_pos = 0, bool anchor = false) {
    BtResult r;
    memset(&r, 0, sizeof(r));
    for (int i = 0; i < 32; i++) { r.starts[i] = -1; r.ends[i] = -1; }
    r.input = input;
    Pool* pool = pool_create();
    JsBtRegex* re = js_bt_compile(pat, (int)strlen(pat), flags, pool);
    r.compiled = re ? 1 : 0;
    if (re) {
        r.group_count = js_bt_group_count(re);
        r.matched = js_bt_exec(re, input, (int)strlen(input), start_pos, anchor,
                               r.starts, r.ends, 32);
    }
    pool_destroy(pool);
    return r;
}

static JsBtFlags F() { JsBtFlags f; memset(&f, 0, sizeof(f)); return f; }
static JsBtFlags Fi() { JsBtFlags f = F(); f.ignore_case = true; return f; }
static JsBtFlags Fu() { JsBtFlags f = F(); f.unicode = true; return f; }
static JsBtFlags Fm() { JsBtFlags f = F(); f.multiline = true; return f; }
static JsBtFlags Fs() { JsBtFlags f = F(); f.dot_all = true; return f; }

// Substring captured by group g (g == 0 is the whole match). "" when matched but
// empty, "<undef>" when the group did not participate.
static std::string group_str(const BtResult& r, int g) {
    if (r.starts[g] < 0 || r.ends[g] < 0) return "<undef>";
    return r.input.substr(r.starts[g], r.ends[g] - r.starts[g]);
}

// ---------------------------------------------------------------------------
// Basic matching
// ---------------------------------------------------------------------------

TEST(BtBasic, Literal) {
    auto r = run_bt("abc", "xxabcyy", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "abc");
    EXPECT_EQ(r.starts[0], 2);
}

TEST(BtBasic, CharClassAndRange) {
    auto r = run_bt("[b-e]+", "azbcdez", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "bcde");
}

TEST(BtBasic, NegatedClass) {
    auto r = run_bt("[^0-9]+", "12abc34", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "abc");
}

TEST(BtBasic, Shorthands) {
    EXPECT_EQ(group_str(run_bt("\\d+", "ab123cd", F()), 0), "123");
    EXPECT_EQ(group_str(run_bt("\\w+", "  foo_9 ", F()), 0), "foo_9");
    EXPECT_EQ(group_str(run_bt("\\D+", "12ab34", F()), 0), "ab");
}

TEST(BtBasic, Alternation) {
    EXPECT_EQ(group_str(run_bt("cat|dog|bird", "I have a dog", F()), 0), "dog");
}

TEST(BtBasic, GreedyVsLazy) {
    EXPECT_EQ(group_str(run_bt("a.*c", "axxcyyc", F()), 0), "axxcyyc"); // greedy
    EXPECT_EQ(group_str(run_bt("a.*?c", "axxcyyc", F()), 0), "axxc");   // lazy
}

TEST(BtBasic, CountedQuantifier) {
    EXPECT_EQ(group_str(run_bt("a{2,3}", "aaaaa", F()), 0), "aaa");
    EXPECT_EQ(group_str(run_bt("a{2}", "aaaaa", F()), 0), "aa");
    EXPECT_EQ(run_bt("a{4}", "aaa", F()).matched, 0);
}

TEST(BtBasic, Anchors) {
    EXPECT_EQ(run_bt("^abc$", "abc", F()).matched, 1);
    EXPECT_EQ(run_bt("^abc$", "xabc", F()).matched, 0);
    EXPECT_EQ(run_bt("^abc$", "abcx", F()).matched, 0);
}

TEST(BtBasic, MultilineAnchors) {
    EXPECT_EQ(run_bt("^def$", "abc\ndef\nghi", Fm()).matched, 1);
    EXPECT_EQ(run_bt("^def$", "abc\ndef\nghi", F()).matched, 0);
}

TEST(BtBasic, DotAll) {
    EXPECT_EQ(run_bt("a.b", "a\nb", F()).matched, 0);
    EXPECT_EQ(run_bt("a.b", "a\nb", Fs()).matched, 1);
}

TEST(BtBasic, CapturingGroups) {
    auto r = run_bt("(a)(b)(c)", "abc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(r.group_count, 3);
    EXPECT_EQ(group_str(r, 1), "a");
    EXPECT_EQ(group_str(r, 2), "b");
    EXPECT_EQ(group_str(r, 3), "c");
}

TEST(BtBasic, NonParticipatingGroup) {
    auto r = run_bt("(a)|(b)", "b", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 1), "<undef>");
    EXPECT_EQ(group_str(r, 2), "b");
}

TEST(BtBasic, IgnoreCase) {
    EXPECT_EQ(run_bt("abc", "ABC", Fi()).matched, 1);
    EXPECT_EQ(group_str(run_bt("[a-z]+", "ABCdef", Fi()), 0), "ABCdef");
}

// ---------------------------------------------------------------------------
// Backreferences (the headline reason for the matcher)
// ---------------------------------------------------------------------------

TEST(BtBackref, ForwardMatch) {
    auto r = run_bt("(abc)\\1", "abcabc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "abcabc");
}

TEST(BtBackref, GreedyBacktrack) {
    auto r = run_bt("(\\w+)\\1", "aabb", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "aa");
    EXPECT_EQ(group_str(r, 1), "a");
}

TEST(BtBackref, IgnoreCase) {
    auto r = run_bt("(.)\\1", "aA", Fi());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "aA");
}

TEST(BtBackref, NonParticipatingMatchesEmpty) {
    // \1 refers to a group that never participates -> matches the empty string.
    auto r = run_bt("(?:(a)|b)\\1c", "bc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "bc");
}

TEST(BtBackref, NoMatch) {
    EXPECT_EQ(run_bt("(abc)\\1", "abcabd", F()).matched, 0);
}

// ---------------------------------------------------------------------------
// Lookbehind (direction = -1)
// ---------------------------------------------------------------------------

TEST(BtLookbehind, FixedCapture) {
    auto r = run_bt("(?<=(c))def", "abcdef", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "def");
    EXPECT_EQ(group_str(r, 1), "c");
}

TEST(BtLookbehind, GreedyLoopCapture) {
    auto r = run_bt("(?<=(b+))c", "abbbbbbc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 1), "bbbbbb");
}

TEST(BtLookbehind, CapturePerSpecCount) {
    // (\w){3} backward: group keeps the LAST iteration's capture ("a").
    auto r = run_bt("(?<=(\\w){3})def", "abcdef", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 1), "a");
}

TEST(BtLookbehind, BackrefInsideForwardOrder) {
    // (\w+) is to the right of \1: read right-to-left it is captured first, so
    // \1 is constrained -> shortest capture that satisfies it is "ab".
    auto r = run_bt("(?<=\\1(\\w+))c", "ababc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 1), "ab");
}

TEST(BtLookbehind, DoNotBacktrackIntoLookbehind) {
    EXPECT_EQ(run_bt("(?<=([abc]+)).\\1", "abcdbc", F()).matched, 0);
}

TEST(BtLookbehind, WordBoundaryAtRightEdge) {
    auto r = run_bt("(?<=\\b)[d-f]{3}", "abc def", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "def");
    EXPECT_EQ(run_bt("(?<=\\b)[d-f]{3}", "abcdef", F()).matched, 0);
}

TEST(BtLookbehind, NegativeDiscardsCaptures) {
    auto r = run_bt("(?<!(\\d){3})f", "abcdef", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 1), "<undef>");
}

TEST(BtLookbehind, NestedLookaround) {
    auto r = run_bt("(?<=a(?=([bc]{2})d)\\w{3})\\w\\w", "abcdef", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "ef");
    EXPECT_EQ(group_str(r, 1), "bc");
}

// ---------------------------------------------------------------------------
// Lookahead with captures
// ---------------------------------------------------------------------------

TEST(BtLookahead, CaptureSurvives) {
    auto r = run_bt("(?:(?=(abc)))a", "abc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "a");
    EXPECT_EQ(group_str(r, 1), "abc");
}

TEST(BtLookahead, NegativeLookahead) {
    EXPECT_EQ(run_bt("a(?!b)", "ab", F()).matched, 0);
    EXPECT_EQ(run_bt("a(?!b)", "ac", F()).matched, 1);
}

// ---------------------------------------------------------------------------
// Named groups
// ---------------------------------------------------------------------------

TEST(BtNamed, BasicAndIntrospection) {
    Pool* pool = pool_create();
    const char* pat = "(?<year>\\d{4})-(?<month>\\d{2})";
    JsBtRegex* re = js_bt_compile(pat, (int)strlen(pat), F(), pool);
    ASSERT_NE(re, nullptr);
    ASSERT_EQ(js_bt_named_count(re), 2);
    int l0 = 0, l1 = 0;
    EXPECT_EQ(std::string(js_bt_named_name(re, 0, &l0), l0), "year");
    EXPECT_EQ(js_bt_named_index(re, 0), 1);
    EXPECT_EQ(std::string(js_bt_named_name(re, 1, &l1), l1), "month");
    EXPECT_EQ(js_bt_named_index(re, 1), 2);
    int s[8], e[8];
    EXPECT_EQ(js_bt_exec(re, "2026-05", 7, 0, false, s, e, 8), 1);
    EXPECT_EQ(std::string("2026-05").substr(s[1], e[1] - s[1]), "2026");
    pool_destroy(pool);
}

TEST(BtNamed, NamedBackrefAlias) {
    // \k<a> is rewritten to \1 before the matcher sees it; both must agree.
    auto r = run_bt("(?<a>.).\\1", "bab", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "bab");
    EXPECT_EQ(group_str(r, 1), "b");
}

// ---------------------------------------------------------------------------
// RepeatMatcher nullable-iteration semantics
// ---------------------------------------------------------------------------

TEST(BtNullable, OptionalEmptyIterationDiscarded) {
    auto r = run_bt("(a?b?\?)*", "ab", F());  // pattern: (a?b??)*  (\? avoids a ??) trigraph)
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "ab");
}

TEST(BtNullable, StarOfOptionalTerminates) {
    // Must not loop forever on an always-empty body.
    auto r = run_bt("(a?)*", "aaa", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "aaa");
}

TEST(BtNullable, EmptyGlobalLikeMatch) {
    auto r = run_bt("(?:)*", "abc", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "");
}

// ---------------------------------------------------------------------------
// Unicode / surrogate handling under /u
// ---------------------------------------------------------------------------

TEST(BtUnicode, BmpLiteral) {
    EXPECT_EQ(run_bt("\\x{e9}", "caf\xc3\xa9", Fu()).matched, 1); // é
}

TEST(BtUnicode, SurrogatePairCountsAsOneDot) {
    // "\u{10000}\u{10000}" — two astral code points; (.+)\1 must NOT spuriously
    // match by starting inside a surrogate/byte boundary.
    const char* astral = "\xf0\x90\x80\x80\xf0\x90\x80\x80";
    auto r = run_bt(".", astral, Fu());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(r.ends[0] - r.starts[0], 4); // one astral codepoint = 4 UTF-8 bytes
}

TEST(BtUnicode, NoMatchStartingMidSequence) {
    // A backref pattern that previously matched spuriously mid-codepoint.
    // "𐀀\ud800" stored as F0 90 80 80 (pair) + ED A0 80 (lone) under /u.
    const char* s = "\xf0\x90\x80\x80\xed\xa0\x80";
    auto r = run_bt("(.+).*\\1", s, Fu());
    EXPECT_EQ(r.matched, 0);
}

// ---------------------------------------------------------------------------
// Negative / malformed patterns -> compile returns NULL (caller falls back)
// ---------------------------------------------------------------------------

TEST(BtNegative, UnbalancedParen) {
    EXPECT_EQ(run_bt("(abc", "abc", F()).compiled, 0);
}

TEST(BtNegative, UnterminatedClass) {
    EXPECT_EQ(run_bt("[abc", "abc", F()).compiled, 0);
}

TEST(BtNegative, BadHexEscape) {
    EXPECT_EQ(run_bt("\\x{zzzz}", "x", F()).compiled, 0);
}

TEST(BtNegative, NamedBackrefToMissingGroup) {
    EXPECT_EQ(run_bt("\\k<missing>", "x", F()).compiled, 0);
}

TEST(BtNegative, ReversedClassRange) {
    EXPECT_EQ(run_bt("[z-a]", "x", F()).compiled, 0);
}

// ---------------------------------------------------------------------------
// Anti-DoS: catastrophic patterns must terminate via the step budget
// ---------------------------------------------------------------------------

TEST(BtAntiDos, NestedQuantifierTerminates) {
    // (a+)+$ over a long all-'a' string ending in a non-matching char is the
    // classic exponential-backtracking shape. The step budget must bound it.
    std::string input(40, 'a');
    input += '!';
    auto t0 = std::chrono::steady_clock::now();
    auto r = run_bt("(a+)+$", input.c_str(), F());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(r.compiled, 1);
    EXPECT_EQ(r.matched, 0);           // budget bails to "no match"
    EXPECT_LT(ms, 5000) << "step budget did not bound runtime";
}

TEST(BtAntiDos, AlternationExplosionTerminates) {
    std::string input(36, 'a');
    input += 'X';
    auto t0 = std::chrono::steady_clock::now();
    auto r = run_bt("(a|a)+$", input.c_str(), F());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(r.matched, 0);
    EXPECT_LT(ms, 5000);
}

TEST(BtAntiDos, NestedGroupExplosionTerminates) {
    std::string input(30, 'a');
    input += 'b';
    auto t0 = std::chrono::steady_clock::now();
    auto r = run_bt("(a*)*c", input.c_str(), F());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(r.matched, 0);
    EXPECT_LT(ms, 5000);
}

// ---------------------------------------------------------------------------
// Stress: large but well-behaved inputs
// ---------------------------------------------------------------------------

TEST(BtStress, LongLinearMatch) {
    std::string input(50000, 'a');
    auto r = run_bt("a+", input.c_str(), F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(r.ends[0] - r.starts[0], 50000);
}

TEST(BtStress, ManyAlternatives) {
    // 200 alternatives foo0|foo1|...|foo199. "foo7" is reached (index 7) before
    // any prefix-colliding fooNN, and matches exactly — ordered, leftmost.
    std::string pat;
    for (int i = 0; i < 200; i++) { if (i) pat += '|'; pat += "foo" + std::to_string(i); }
    auto r = run_bt(pat.c_str(), "zzz foo7 zzz", F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 0), "foo7");
}

TEST(BtStress, DeepNestingCompilesAndMatches) {
    std::string pat, input;
    for (int i = 0; i < 30; i++) { pat += "(?:a"; }
    pat += "b";
    for (int i = 0; i < 30; i++) { pat += ")"; }
    for (int i = 0; i < 30; i++) input += 'a';
    input += 'b';
    auto r = run_bt(pat.c_str(), input.c_str(), F());
    EXPECT_EQ(r.compiled, 1);
    EXPECT_EQ(r.matched, 1);
}

TEST(BtStress, BackrefScanAcrossLongInput) {
    // (\w+)\1 must find the doubled run far into the string without blowing up.
    std::string input(2000, 'x');
    input += "abab";
    auto r = run_bt("(ab)\\1", input.c_str(), F());
    EXPECT_EQ(r.matched, 1);
    EXPECT_EQ(group_str(r, 1), "ab");
}

// ---------------------------------------------------------------------------
// Sticky / anchored start position
// ---------------------------------------------------------------------------

TEST(BtAnchor, AnchoredStartOnly) {
    EXPECT_EQ(run_bt("abc", "xabc", F(), 0, true).matched, 0);  // anchored at 0
    EXPECT_EQ(run_bt("abc", "xabc", F(), 1, true).matched, 1);  // anchored at 1
}

TEST(BtAnchor, StickyFlag) {
    JsBtFlags f = F(); f.sticky = true;
    EXPECT_EQ(run_bt("abc", "xabc", f, 0).matched, 0);
    EXPECT_EQ(run_bt("abc", "xabc", f, 1).matched, 1);
}

}  // namespace
