// grep_internal.hpp — structures shared by the lib/grep translation units.
// Not part of the public API (lib/grep/grep.h): RE2 appears here only as a
// forward declaration, and only grep_literal.cpp sees RE2's parse tree.
#pragma once

#include "grep.h"
#include "../str.h"

namespace re2 { class RE2; }

// Literal acceleration tiers (vibe/Lambda_Lib_Grep.md §5.1).
enum GrepTier {
    GREP_TIER_LITERAL = 0,   // the pattern is one case-sensitive literal: no RE2 at all
    GREP_TIER_REQUIRED = 1,  // every match contains one of a few literals: scan, then confirm the line
    GREP_TIER_REGEX = 2,     // nothing usable: RE2 searches the buffer
};

// a plan's literal set: one literal goes through the packed pair, 2..64 through
// Teddy (GRP29)
#define GREP_MAX_LITERALS STR_TEDDY_MAX

struct GrepLiteralPlan {
    GrepTier tier;
    int count;
    StrNeedle needles[GREP_MAX_LITERALS];
    StrTeddy teddy;          // the whole set at once, when count >= 2
    char* storage;           // the literal bytes; needles and teddy point into it (mem-owned)
    bool has_text_anchor;    // \A or \z: a buffer-wide search would misplace them
    bool has_end_line;       // $: a buffer-wide search misses "x$" before "\r\n"
};

// Fills `plan` from the compiled pattern; returns false only on allocation
// failure (an unusable pattern simply yields GREP_TIER_REGEX).
bool grep_literal_plan(const re2::RE2* re, GrepLiteralPlan* plan);
void grep_literal_plan_release(GrepLiteralPlan* plan);

struct GrepMatcher {
    GrepOptions options;
    char* regex;             // the assembled RE2 source (mem-owned)
    size_t regex_len;
    int report_group;        // 0, or 1 when word mode wraps the pattern in a group
    bool every_line;         // an unwrapped empty pattern: every line matches
    re2::RE2* re;            // compiled once to validate and plan; searchers compile their own
    GrepLiteralPlan plan;
};

// Compiles the matcher's regex again for a searcher (GRP6: one RE2 per worker).
re2::RE2* grep_matcher_compile(const GrepMatcher* matcher);
void grep_matcher_release_re(re2::RE2* re);

// Starts a new input on a searcher: resets per-input counters. Used by the
// stream and by the walker, which wraps the caller's sink.
void grep_searcher_reset_stop(GrepSearcher* searcher);
