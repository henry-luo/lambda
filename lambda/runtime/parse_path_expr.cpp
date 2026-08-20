#include "parse_path_expr.hpp"
#include "type_build.hpp"
#include "transpiler.hpp"

#include <limits.h>
#include <string.h>

namespace {

struct PathLexer {
    Transpiler* tp;
    const char* p;
    const char* end;
    LambdaSourceSpan origin;
    bool failed;
    bool report_errors;
    ArrayList* segments;
};

bool is_path_ident_start(char c) {
    return c == '_' || c == '$' ||
        (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (unsigned char)c >= 0x80;
}

bool is_path_ident_continue(char c) {
    return is_path_ident_start(c) || (c >= '0' && c <= '9');
}

bool is_path_digit(char c) {
    return c >= '0' && c <= '9';
}

void path_fail(PathLexer* lx, const char* detail) {
    if (lx->failed) { return; }
    lx->failed = true;
    if (lx->report_errors) {
        record_semantic_error_span(lx->tp, lx->origin, ERR_INVALID_LITERAL,
            "invalid path expression: %s", detail);
    }
}

void skip_path_space(PathLexer* lx) {
    for (;;) {
        while (lx->p < lx->end && (*lx->p == ' ' || *lx->p == '\t' ||
                *lx->p == '\r' || *lx->p == '\n' || *lx->p == '\f' ||
                *lx->p == '\v')) {
            lx->p++;
        }
        if (lx->p + 1 < lx->end && lx->p[0] == '/' && lx->p[1] == '/') {
            lx->p += 2;
            while (lx->p < lx->end && *lx->p != '\n') { lx->p++; }
            continue;
        }
        if (lx->p + 1 < lx->end && lx->p[0] == '/' && lx->p[1] == '*') {
            lx->p += 2;
            while (lx->p + 1 < lx->end && !(lx->p[0] == '*' && lx->p[1] == '/')) {
                lx->p++;
            }
            if (lx->p + 1 < lx->end) { lx->p += 2; }
            else { lx->p = lx->end; }
            continue;
        }
        return;
    }
}

bool path_word_is(StrView word, const char* text) {
    size_t length = strlen(text);
    return word.length == length && memcmp(word.str, text, length) == 0;
}

StrView take_path_word(PathLexer* lx) {
    skip_path_space(lx);
    StrView word = {lx->p, 0};
    if (lx->p >= lx->end || !is_path_ident_start(*lx->p)) { return word; }
    while (lx->p < lx->end && is_path_ident_continue(*lx->p)) { lx->p++; }
    word.length = (size_t)(lx->p - word.str);
    return word;
}

bool parse_path_scheme(PathLexer* lx, PathScheme* out) {
    StrView word = take_path_word(lx);
    if (path_word_is(word, "file")) { *out = PATH_SCHEME_FILE; return true; }
    if (path_word_is(word, "http")) { *out = PATH_SCHEME_HTTP; return true; }
    if (path_word_is(word, "https")) { *out = PATH_SCHEME_HTTPS; return true; }
    if (path_word_is(word, "sys")) { *out = PATH_SCHEME_SYS; return true; }
    return false;
}

AstPathSegment* append_path_segment(PathLexer* lx, LPathSegmentType type,
        String* name, int64_t int_value) {
    AstPathSegment* segment = (AstPathSegment*)pool_calloc(lx->tp->pool,
        sizeof(AstPathSegment));
    segment->name = name;
    segment->type = type;
    segment->int_value = int_value;
    arraylist_append(lx->segments, segment);
    return segment;
}

bool parse_path_quoted_name(PathLexer* lx, String** out) {
    if (lx->p >= lx->end || *lx->p != '\'') { return false; }
    lx->p++;
    const char* start = lx->p;
    while (lx->p < lx->end && *lx->p != '\'') {
        if (*lx->p == '\\' && lx->p + 1 < lx->end) { lx->p++; }
        lx->p++;
    }
    if (lx->p >= lx->end) {
        path_fail(lx, "unterminated quoted segment");
        return false;
    }
    StrView name = {start, (size_t)(lx->p - start)};
    lx->p++;
    *out = name_pool_create_strview(lx->tp->name_pool, name);
    return true;
}

bool parse_path_integer(PathLexer* lx, int64_t* out) {
    if (lx->p >= lx->end || !is_path_digit(*lx->p)) { return false; }
    int64_t value = 0;
    while (lx->p < lx->end && is_path_digit(*lx->p)) {
        int digit = *lx->p - '0';
        if (value > (INT64_MAX - digit) / 10) {
            path_fail(lx, "integer segment is out of range");
            return false;
        }
        value = value * 10 + digit;
        lx->p++;
    }
    *out = value;
    return true;
}

// path_static_segment in the full grammar: a field key, `~~`, or `/` after a
// dot. This parser owns only static segments; bracket expressions remain AST
// wrappers in the general Lambda expression grammar.
bool parse_path_static_segment(PathLexer* lx) {
    skip_path_space(lx);
    if (lx->p >= lx->end) { return false; }

    if (*lx->p == '~') {
        if (lx->p + 1 >= lx->end || lx->p[1] != '~') { return false; }
        lx->p += 2;
        append_path_segment(lx, LPATH_SEG_PARENT, NULL, 0);
        return true;
    }
    if (*lx->p == '/') {
        lx->p++;
        append_path_segment(lx, LPATH_SEG_ROOT, NULL, 0);
        return true;
    }
    if (*lx->p == '*') {
        lx->p++;
        LPathSegmentType type = LPATH_SEG_WILDCARD;
        if (lx->p < lx->end && *lx->p == '*') {
            lx->p++;
            type = LPATH_SEG_WILDCARD_REC;
        }
        append_path_segment(lx, type, NULL, 0);
        return true;
    }
    if (*lx->p == '\'') {
        String* name = NULL;
        if (!parse_path_quoted_name(lx, &name)) { return false; }
        append_path_segment(lx, LPATH_SEG_NORMAL, name, 0);
        return true;
    }
    if (is_path_digit(*lx->p)) {
        int64_t value = 0;
        if (!parse_path_integer(lx, &value)) { return false; }
        append_path_segment(lx, LPATH_SEG_INT, NULL, value);
        return true;
    }
    StrView word = take_path_word(lx);
    if (!word.length) { return false; }
    append_path_segment(lx, LPATH_SEG_NORMAL,
        name_pool_create_strview(lx->tp->name_pool, word), 0);
    return true;
}

bool parse_path_dot_segment(PathLexer* lx) {
    skip_path_space(lx);
    if (lx->p >= lx->end || *lx->p != '.') { return false; }
    lx->p++;
    return parse_path_static_segment(lx);
}

bool parse_path_root_or_scheme(PathLexer* lx, PathScheme* scheme,
        bool* file_local) {
    skip_path_space(lx);
    *file_local = false;
    if (lx->p >= lx->end) { return false; }
    if (*lx->p == '/') {
        lx->p++;
        *scheme = PATH_SCHEME_LOGICAL;
        return true;
    }
    if (*lx->p == '.') {
        lx->p++;
        *scheme = PATH_SCHEME_REL;
        return true;
    }

    const char* start = lx->p;
    if (!parse_path_scheme(lx, scheme)) {
        lx->p = start;
        return false;
    }
    skip_path_space(lx);
    if (lx->p >= lx->end || *lx->p != '.') {
        lx->p = start;
        return false;
    }
    lx->p++;
    skip_path_space(lx);
    if (*scheme == PATH_SCHEME_FILE && lx->p < lx->end && *lx->p == '/') {
        *file_local = true;
    }
    return parse_path_static_segment(lx);
}

bool parse_path_static_expr(PathLexer* lx, PathScheme* scheme,
        bool* file_local) {
    if (!parse_path_root_or_scheme(lx, scheme, file_local)) { return false; }

    // S2.4.1v2 requires rooted `/.a` and relative `.a`; accepting a bare root
    // here would reintroduce the `/` division and `.123` float ambiguities.
    if (*scheme == PATH_SCHEME_LOGICAL) {
        if (lx->p >= lx->end || *lx->p != '.' ||
                !parse_path_dot_segment(lx)) { return false; }
    } else if (*scheme == PATH_SCHEME_REL) {
        if (lx->p >= lx->end || is_path_digit(*lx->p) ||
                !parse_path_static_segment(lx)) { return false; }
    }

    for (;;) {
        const char* before_dot = lx->p;
        if (!parse_path_dot_segment(lx)) {
            lx->p = before_dot;
            break;
        }
    }
    return true;
}

}  // namespace

AstNode* build_static_path_ast_from_span(Transpiler* tp, LambdaSourceSpan span,
        PathScheme scheme,
        String* authority, ArrayList* segments, int first_segment) {
    int segment_count = segments ? segments->length - first_segment : 0;
    if (segment_count < 0) { segment_count = 0; }

    AstPathNode* path = (AstPathNode*)alloc_ast_node_from_span(tp,
        AST_NODE_PATH_EXPR, span, sizeof(AstPathNode));
    path->scheme = scheme;
    path->authority = authority;
    path->segment_count = segment_count;
    path->segments = NULL;
    if (segment_count > 0) {
        path->segments = (AstPathSegment*)pool_calloc(tp->pool,
            sizeof(AstPathSegment) * (size_t)segment_count);
        for (int i = 0; i < segment_count; i++) {
            path->segments[i] = *(AstPathSegment*)segments->data[i + first_segment];
        }
    }
    path->type = &TYPE_PATH;
    return (AstNode*)path;
}

AstNode* build_static_path_ast(Transpiler* tp, TSNode origin, PathScheme scheme,
        String* authority, ArrayList* segments, int first_segment) {
    LambdaSourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return build_static_path_ast_from_span(tp, span, scheme, authority, segments,
        first_segment);
}

static AstNode* parse_path_expr_text_impl(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span, bool report_errors) {
    PathLexer lx = {tp, begin, end, span, false, report_errors, arraylist_new(8)};
    PathScheme scheme = PATH_SCHEME_REL;
    bool file_local = false;
    if (!parse_path_static_expr(&lx, &scheme, &file_local) || lx.failed) {
        arraylist_free(lx.segments);
        return NULL;
    }
    skip_path_space(&lx);
    if (lx.p != lx.end) {
        path_fail(&lx, "unexpected trailing input");
        arraylist_free(lx.segments);
        return NULL;
    }

    String* authority = NULL;
    int first_segment = 0;
    if (scheme == PATH_SCHEME_FILE && !file_local && lx.segments->length > 0) {
        AstPathSegment* first = (AstPathSegment*)lx.segments->data[0];
        if (first->type == LPATH_SEG_NORMAL && first->name) {
            authority = first->name;
            first_segment = 1;
        }
    }
    AstNode* path = build_static_path_ast_from_span(tp, span, scheme, authority,
        lx.segments, first_segment);
    arraylist_free(lx.segments);
    return path;
}

AstNode* parse_path_expr_text(Transpiler* tp, const char* begin, const char* end,
        TSNode origin) {
    LambdaSourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return parse_path_expr_text_impl(tp, begin, end, span, true);
}

AstNode* try_parse_path_expr_text(Transpiler* tp, const char* begin,
        const char* end, TSNode origin) {
    LambdaSourceSpan span = {ts_node_start_byte(origin), ts_node_end_byte(origin)};
    return parse_path_expr_text_impl(tp, begin, end, span, false);
}

AstNode* parse_path_expr_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span) {
    return parse_path_expr_text_impl(tp, begin, end, span, true);
}

AstNode* try_parse_path_expr_text_span(Transpiler* tp, const char* begin,
        const char* end, LambdaSourceSpan span) {
    return parse_path_expr_text_impl(tp, begin, end, span, false);
}
