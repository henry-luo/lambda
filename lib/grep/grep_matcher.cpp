// grep_matcher.cpp — pattern preparation and compilation (GRP5, GRP12).

#include "grep_internal.hpp"
#include "../byte_builder.h"
#include "../log.h"
#include "../re2_glue.hpp"

#include <string.h>

// A letter, digit or '_' on either side breaks a whole-word match; group 1 is
// the reported match, so a trailing separator can lead the next match.
static const char WORD_PREFIX[] = "(?:^|[^\\pL\\pN_])(";
static const char WORD_SUFFIX[] = ")(?:[^\\pL\\pN_]|$)";

static re2::RE2::Options grep_re2_options(const GrepOptions* options) {
    re2::RE2::Options opts = lam::re2_glue_default_options();
    // no match may contain a line terminator (GRP1): RE2 drops '\n' from
    // literals, classes and '.' at parse time
    opts.set_never_nl(true);
    opts.set_case_sensitive(!options->ignore_case);
    return opts;
}

static bool append_cstr(ByteBuilder* b, const char* s) {
    return byte_builder_append(b, s, strlen(s));
}

// RE2::QuoteMeta's rules without std::string: everything but ASCII word
// characters and UTF-8 bytes is escaped; NUL needs \x00
static bool append_escaped(ByteBuilder* b, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (c == 0) {
            if (!append_cstr(b, "\\x00")) return false;
            continue;
        }
        if (!word && c < 0x80 && !byte_builder_append(b, "\\", 1)) return false;
        if (!byte_builder_append(b, &s[i], 1)) return false;
    }
    return true;
}

// RE2 rejects backreferences and lookaround; report them as unsupported
// rather than as a syntax error so the caller can route the pattern to its
// backtracking engine (GRP12). Character classes and escapes are skipped.
static bool pattern_uses_unsupported(const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '\\' && i + 1 < n) {
            char c = p[i + 1];
            if ((c >= '1' && c <= '9') || (c == 'k' && i + 2 < n && p[i + 2] == '<')) return true;
            i++;
        } else if (p[i] == '[') {
            size_t j = i + 1;
            if (j < n && (p[j] == '^')) j++;
            if (j < n && p[j] == ']') j++;
            while (j < n && p[j] != ']') {
                if (p[j] == '\\') j++;
                j++;
            }
            i = j;
        } else if (p[i] == '(' && i + 2 < n && p[i + 1] == '?') {
            char c = p[i + 2];
            if (c == '=' || c == '!') return true;
            if (c == '<' && i + 3 < n && (p[i + 3] == '=' || p[i + 3] == '!')) return true;
        }
    }
    return false;
}

re2::RE2* grep_matcher_compile(const GrepMatcher* matcher) {
    if (!matcher) return NULL;
    re2::RE2::Options opts = grep_re2_options(&matcher->options);
    return lam::re2_glue_compile(matcher->regex, matcher->regex_len, opts, "grep");
}

void grep_matcher_release_re(re2::RE2* re) {
    lam::re2_glue_release(re);
}

GrepStatus grep_matcher_create(const char* const* patterns, const size_t* lengths, size_t count,
                               const GrepOptions* options, GrepMatcher** out,
                               char* error_buf, size_t error_buf_len) {
    if (out) *out = NULL;
    if (error_buf && error_buf_len) error_buf[0] = '\0';
    if (!out || !patterns || !lengths || count == 0 || !options) return GREP_ERR_ARGUMENT;

    ByteBuilder rx;
    if (!byte_builder_init(&rx, 64, MEM_CAT_TEMP, true)) return GREP_ERR_MEMORY;
    bool ok = append_cstr(&rx, "(?m)");
    int report_group = 0;
    if (options->whole_line) ok = ok && append_cstr(&rx, "^(?:");
    else if (options->word) {
        ok = ok && append_cstr(&rx, WORD_PREFIX);
        report_group = 1;
    }
    for (size_t i = 0; ok && i < count; i++) {
        if (i > 0) ok = append_cstr(&rx, "|");
        // several patterns join as an alternation of groups, so each keeps its
        // own precedence (a top-level | inside one stays inside it)
        if (count > 1) ok = ok && append_cstr(&rx, "(?:");
        if (options->fixed_string) ok = ok && append_escaped(&rx, patterns[i], lengths[i]);
        else ok = ok && byte_builder_append(&rx, patterns[i], lengths[i]);
        if (count > 1) ok = ok && append_cstr(&rx, ")");
    }
    if (options->whole_line) ok = ok && append_cstr(&rx, ")$");
    else if (options->word) ok = ok && append_cstr(&rx, WORD_SUFFIX);
    if (!ok) {
        byte_builder_destroy(&rx);
        return GREP_ERR_MEMORY;
    }

    GrepMatcher* m = (GrepMatcher*)mem_calloc(1, sizeof(GrepMatcher), MEM_CAT_TEMP);
    if (!m) {
        byte_builder_destroy(&rx);
        return GREP_ERR_MEMORY;
    }
    m->options = *options;
    m->regex = (char*)byte_builder_take(&rx, &m->regex_len);
    m->report_group = report_group;

    re2::RE2::Options opts = grep_re2_options(options);
    m->re = lam::re2_glue_compile(m->regex, m->regex_len, opts, "grep", error_buf, error_buf_len);
    if (!m->re) {
        GrepStatus status = GREP_ERR_SYNTAX;
        for (size_t i = 0; i < count && !options->fixed_string; i++) {
            if (pattern_uses_unsupported(patterns[i], lengths[i])) status = GREP_ERR_UNSUPPORTED;
        }
        log_debug("grep matcher: compile failed (%s) for '%.*s'",
                  status == GREP_ERR_UNSUPPORTED ? "unsupported" : "syntax", (int)m->regex_len, m->regex);
        grep_matcher_destroy(m);
        return status;
    }
    if (!grep_literal_plan(m->re, &m->plan)) {
        grep_matcher_destroy(m);
        return GREP_ERR_MEMORY;
    }
    *out = m;
    return GREP_OK;
}

void grep_matcher_destroy(GrepMatcher* matcher) {
    if (!matcher) return;
    grep_literal_plan_release(&matcher->plan);
    lam::re2_glue_release(matcher->re);
    if (matcher->regex) mem_free(matcher->regex);
    mem_free(matcher);
}

const GrepOptions* grep_matcher_options(const GrepMatcher* matcher) {
    return matcher ? &matcher->options : NULL;
}
