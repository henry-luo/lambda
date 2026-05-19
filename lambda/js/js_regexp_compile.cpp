#include "js_regexp_compile.h"
#include "js_regex_wrapper.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <utf8proc.h>

#define JS_REGEXP_MAX_NAMED_GROUPS 96
#define JS_REGEXP_MAX_NAMED_BACKREFS 96

typedef struct JsRegExpNameRef {
    const char* name;
    int len;
} JsRegExpNameRef;

static int js_regexp_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

static bool js_regexp_is_name_start_cp(uint32_t cp) {
    if (cp == '$' || cp == '_') return true;
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
    utf8proc_category_t cat = utf8proc_category((utf8proc_int32_t)cp);
    return cat == UTF8PROC_CATEGORY_LU || cat == UTF8PROC_CATEGORY_LL ||
        cat == UTF8PROC_CATEGORY_LT || cat == UTF8PROC_CATEGORY_LM ||
        cat == UTF8PROC_CATEGORY_LO || cat == UTF8PROC_CATEGORY_NL;
}

static bool js_regexp_is_name_continue_cp(uint32_t cp) {
    if (js_regexp_is_name_start_cp(cp)) return true;
    if (cp >= '0' && cp <= '9') return true;
    if (cp == 0x200C || cp == 0x200D) return true;
    utf8proc_category_t cat = utf8proc_category((utf8proc_int32_t)cp);
    return cat == UTF8PROC_CATEGORY_MN || cat == UTF8PROC_CATEGORY_MC ||
        cat == UTF8PROC_CATEGORY_ND || cat == UTF8PROC_CATEGORY_PC;
}

static bool js_regexp_read_hex_escape_cp(const char* pattern, int pattern_len,
    int index, int* next_index, uint32_t* out_cp) {
    if (!pattern || index + 1 >= pattern_len || pattern[index] != '\\' ||
        pattern[index + 1] != 'u') return false;
    if (index + 2 < pattern_len && pattern[index + 2] == '{') {
        int j = index + 3;
        uint32_t cp = 0;
        int digits = 0;
        while (j < pattern_len && pattern[j] != '}') {
            int hv = js_regexp_hex_value(pattern[j]);
            if (hv < 0 || digits >= 6) return false;
            cp = (cp << 4) | (uint32_t)hv;
            digits++;
            j++;
        }
        if (j >= pattern_len || digits <= 0 || cp > 0x10FFFFu) return false;
        if (next_index) *next_index = j + 1;
        if (out_cp) *out_cp = cp;
        return true;
    }
    if (index + 5 >= pattern_len) return false;
    uint32_t value = 0;
    for (int j = index + 2; j < index + 6; j++) {
        int hv = js_regexp_hex_value(pattern[j]);
        if (hv < 0) return false;
        value = (value << 4) | (uint32_t)hv;
    }
    int end = index + 6;
    if (value >= 0xD800u && value <= 0xDBFFu && end + 5 < pattern_len &&
        pattern[end] == '\\' && pattern[end + 1] == 'u') {
        uint32_t low = 0;
        bool low_ok = true;
        for (int j = end + 2; j < end + 6; j++) {
            int hv = js_regexp_hex_value(pattern[j]);
            if (hv < 0) { low_ok = false; break; }
            low = (low << 4) | (uint32_t)hv;
        }
        if (low_ok && low >= 0xDC00u && low <= 0xDFFFu) {
            value = 0x10000u + ((value - 0xD800u) << 10) + (low - 0xDC00u);
            end += 6;
        }
    }
    if (next_index) *next_index = end;
    if (out_cp) *out_cp = value;
    return true;
}

static bool js_regexp_read_name_cp(const char* pattern, int index, int limit,
    int* next_index, uint32_t* out_cp) {
    if (!pattern || index >= limit) return false;
    if (pattern[index] == '\\') {
        if (index + 1 < limit && pattern[index + 1] == 'u') {
            int next = index;
            uint32_t cp = 0;
            if (!js_regexp_read_hex_escape_cp(pattern, limit, index, &next, &cp)) return false;
            if (next <= index || next > limit) return false;
            if (next_index) *next_index = next;
            if (out_cp) *out_cp = cp;
            return true;
        }
        return false;
    }
    utf8proc_int32_t cp = 0;
    utf8proc_ssize_t width = utf8proc_iterate(
        (const utf8proc_uint8_t*)pattern + index,
        (utf8proc_ssize_t)(limit - index), &cp);
    if (width <= 0 || cp < 0) return false;
    if (next_index) *next_index = index + (int)width;
    if (out_cp) *out_cp = (uint32_t)cp;
    return true;
}

static bool js_regexp_is_valid_group_name(const char* pattern, int name_start, int name_end) {
    if (!pattern || name_start >= name_end) return false;
    int pos = name_start;
    uint32_t cp = 0;
    int next = pos;
    if (!js_regexp_read_name_cp(pattern, pos, name_end, &next, &cp) ||
        !js_regexp_is_name_start_cp(cp)) {
        return false;
    }
    pos = next;
    while (pos < name_end) {
        if (!js_regexp_read_name_cp(pattern, pos, name_end, &next, &cp) ||
            !js_regexp_is_name_continue_cp(cp)) {
            return false;
        }
        pos = next;
    }
    return true;
}

static bool js_regexp_same_name_decoded(const JsRegExpNameRef* a, const char* b, int b_len) {
    if (!a || !a->name || !b) return false;
    int apos = 0;
    int bpos = 0;
    while (apos < a->len && bpos < b_len) {
        uint32_t acp = 0;
        uint32_t bcp = 0;
        int anext = apos;
        int bnext = bpos;
        if (!js_regexp_read_name_cp(a->name, apos, a->len, &anext, &acp) ||
            !js_regexp_read_name_cp(b, bpos, b_len, &bnext, &bcp)) {
            return false;
        }
        if (acp != bcp) return false;
        apos = anext;
        bpos = bnext;
    }
    return apos == a->len && bpos == b_len;
}

static void js_regexp_set_error(JsRegExpCompileInfo* out, const char* fmt,
    const char* pattern, int pattern_len, const char* flags, int flags_len,
    const char* extra) {
    if (!out) return;
    if (!extra) extra = "";
    snprintf(out->error, sizeof(out->error), fmt, pattern_len, pattern, flags_len, flags, extra);
}

static bool js_regexp_scan_named_groups(const char* pattern, int pattern_len,
    const char* flags, int flags_len, JsRegExpCompileInfo* out) {
    JsRegExpNameRef groups[JS_REGEXP_MAX_NAMED_GROUPS];
    JsRegExpNameRef backrefs[JS_REGEXP_MAX_NAMED_BACKREFS];
    int group_count = 0;
    int backref_count = 0;
    bool strict_names = out && (out->unicode || out->unicode_sets);
    bool in_class = false;

    for (int i = 0; i < pattern_len; i++) {
        unsigned char ch = (unsigned char)pattern[i];
        if (ch == '\\') {
            if (i + 1 >= pattern_len) break;
            char next = pattern[i + 1];
            if (!in_class && next == 'k' && i + 2 < pattern_len && pattern[i + 2] == '<') {
                int name_start = i + 3;
                int j = name_start;
                while (j < pattern_len && pattern[j] != '>') j++;
                if (j >= pattern_len) {
                    if (strict_names) {
                        js_regexp_set_error(out,
                            "Invalid regular expression: /%.*s/%.*s: unterminated named backreference%s",
                            pattern, pattern_len, flags, flags_len, "");
                        return false;
                    }
                    i++;
                    continue;
                }
                int name_len = j - name_start;
                if (name_len <= 0 || !js_regexp_is_valid_group_name(pattern, name_start, j)) {
                    if (strict_names) {
                        js_regexp_set_error(out,
                            "Invalid regular expression: /%.*s/%.*s: invalid named backreference%s",
                            pattern, pattern_len, flags, flags_len, "");
                        return false;
                    }
                    i = j;
                    continue;
                }
                if (backref_count < JS_REGEXP_MAX_NAMED_BACKREFS) {
                    backrefs[backref_count].name = pattern + name_start;
                    backrefs[backref_count].len = name_len;
                    backref_count++;
                }
                i = j;
                continue;
            }
            i++;
            continue;
        }
        if (ch == '[') {
            in_class = true;
            continue;
        }
        if (ch == ']' && in_class) {
            in_class = false;
            continue;
        }
        if (in_class || ch != '(' || i + 3 >= pattern_len) continue;
        if (pattern[i + 1] != '?' || pattern[i + 2] != '<') continue;
        if (pattern[i + 3] == '=' || pattern[i + 3] == '!') continue;

        int name_start = i + 3;
        int j = name_start;
        while (j < pattern_len && pattern[j] != '>') j++;
        if (j >= pattern_len) {
            js_regexp_set_error(out,
                "Invalid regular expression: /%.*s/%.*s: unterminated named capture%s",
                pattern, pattern_len, flags, flags_len, "");
            return false;
        }
        int name_len = j - name_start;
        if (name_len <= 0 || !js_regexp_is_valid_group_name(pattern, name_start, j)) {
            js_regexp_set_error(out,
                "Invalid regular expression: /%.*s/%.*s: invalid named capture%s",
                pattern, pattern_len, flags, flags_len, "");
            return false;
        }
        for (int g = 0; g < group_count; g++) {
            if (js_regexp_same_name_decoded(&groups[g], pattern + name_start, name_len)) {
                js_regexp_set_error(out,
                    "Invalid regular expression: /%.*s/%.*s: duplicate capture group name%s",
                    pattern, pattern_len, flags, flags_len, "");
                return false;
            }
        }
        if (group_count < JS_REGEXP_MAX_NAMED_GROUPS) {
            groups[group_count].name = pattern + name_start;
            groups[group_count].len = name_len;
            group_count++;
        }
        i = j;
    }

    if (group_count == 0 && !strict_names) return true;
    for (int b = 0; b < backref_count; b++) {
        bool found = false;
        for (int g = 0; g < group_count; g++) {
            if (js_regexp_same_name_decoded(&groups[g], backrefs[b].name, backrefs[b].len)) {
                found = true;
                break;
            }
        }
        if (!found) {
            js_regexp_set_error(out,
                "Invalid regular expression: /%.*s/%.*s: invalid named capture referenced%s",
                pattern, pattern_len, flags, flags_len, "");
            return false;
        }
    }
    return true;
}

bool js_regexp_compile_frontend(const char* pattern, int pattern_len,
    const char* flags, int flags_len, JsRegExpCompileInfo* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!pattern) {
        pattern = "";
        pattern_len = 0;
    }
    if (!flags) {
        flags = "";
        flags_len = 0;
    }

    unsigned int seen = 0;
    for (int i = 0; i < flags_len; i++) {
        char fc = flags[i];
        int bit = 0;
        switch (fc) {
        case 'd': bit = 1 << 0; out->has_indices = true; break;
        case 'g': bit = 1 << 1; out->global = true; break;
        case 'i': bit = 1 << 2; out->ignore_case = true; break;
        case 'm': bit = 1 << 3; out->multiline = true; break;
        case 's': bit = 1 << 4; out->dot_all = true; break;
        case 'u': bit = 1 << 5; out->unicode = true; break;
        case 'v': bit = 1 << 6; out->unicode_sets = true; break;
        case 'y': bit = 1 << 7; out->sticky = true; break;
        default: bit = 0; break;
        }
        if (bit == 0 || (seen & bit)) {
            snprintf(out->error, sizeof(out->error),
                "Invalid flags supplied to RegExp constructor '%.*s'", flags_len, flags);
            return false;
        }
        seen |= bit;
    }
    if (out->unicode && out->unicode_sets) {
        snprintf(out->error, sizeof(out->error),
            "Invalid flags: u and v are mutually exclusive");
        return false;
    }

    int fi = 0;
    static const char flag_order[] = "dgimsuy";
    for (int oi = 0; flag_order[oi]; oi++) {
        for (int si = 0; si < flags_len; si++) {
            if (flags[si] == flag_order[oi]) {
                out->canonical_flags[fi++] = flag_order[oi];
                break;
            }
        }
    }
    out->canonical_flags[fi] = '\0';
    out->canonical_flags_len = fi;

    if (out->unicode || out->unicode_sets) {
        if (!js_regex_wrapper_validate_unicode(pattern, pattern_len)) {
            js_regexp_set_error(out,
                "Invalid regular expression: /%.*s/%.*s: Annex B legacy syntax not allowed under `u` flag%s",
                pattern, pattern_len, flags, flags_len, "");
            return false;
        }
    }
    if (!js_regexp_scan_named_groups(pattern, pattern_len, flags, flags_len, out)) {
        log_debug("js regexp frontend: named group validation failed for /%.*s/%.*s",
            pattern_len, pattern, flags_len, flags);
        return false;
    }
    return true;
}

char* js_regexp_rewrite_named_backrefs(const char* pattern, int pattern_len,
    int* out_len) {
    if (out_len) *out_len = pattern_len;
    if (!pattern || pattern_len <= 0) return NULL;

    JsRegExpNameRef groups[JS_REGEXP_MAX_NAMED_GROUPS];
    int group_count = 0;
    bool in_class = false;
    bool has_named_backref = false;

    for (int i = 0; i < pattern_len; i++) {
        unsigned char ch = (unsigned char)pattern[i];
        if (ch == '\\') {
            if (!in_class && i + 2 < pattern_len && pattern[i + 1] == 'k' && pattern[i + 2] == '<') {
                has_named_backref = true;
            }
            if (i + 1 < pattern_len) i++;
            continue;
        }
        if (ch == '[') {
            in_class = true;
            continue;
        }
        if (ch == ']' && in_class) {
            in_class = false;
            continue;
        }
        if (in_class || ch != '(' || i + 1 >= pattern_len) continue;
        if (pattern[i + 1] != '?') {
            group_count++;
            continue;
        }
        if (i + 3 >= pattern_len || pattern[i + 2] != '<' ||
            pattern[i + 3] == '=' || pattern[i + 3] == '!') {
            continue;
        }
        group_count++;
        int name_start = i + 3;
        int j = name_start;
        while (j < pattern_len && pattern[j] != '>') j++;
        if (j >= pattern_len) continue;
        if (group_count <= JS_REGEXP_MAX_NAMED_GROUPS) {
            groups[group_count - 1].name = pattern + name_start;
            groups[group_count - 1].len = j - name_start;
        }
        i = j;
    }
    if (!has_named_backref) return NULL;

    char* rewritten = (char*)mem_alloc((size_t)pattern_len + 1, MEM_CAT_JS_RUNTIME);
    if (!rewritten) return NULL;
    int out_pos = 0;
    in_class = false;
    int current_group = 0;
    int paren_stack[128];
    int paren_depth = 0;
    for (int i = 0; i < pattern_len; i++) {
        unsigned char ch = (unsigned char)pattern[i];
        if (ch == '\\' && !in_class && i + 2 < pattern_len &&
            pattern[i + 1] == 'k' && pattern[i + 2] == '<') {
            int name_start = i + 3;
            int j = name_start;
            while (j < pattern_len && pattern[j] != '>') j++;
            if (j < pattern_len) {
                int name_len = j - name_start;
                int numeric_index = 0;
                for (int g = 0; g < group_count && g < JS_REGEXP_MAX_NAMED_GROUPS; g++) {
                    if (js_regexp_same_name_decoded(&groups[g], pattern + name_start, name_len)) {
                        numeric_index = g + 1;
                        break;
                    }
                }
                if (numeric_index > 0 && numeric_index <= 9) {
                    bool refers_to_open_group = false;
                    for (int d = 0; d < paren_depth; d++) {
                        if (paren_stack[d] == numeric_index) {
                            refers_to_open_group = true;
                            break;
                        }
                    }
                    if (refers_to_open_group) {
                        i = j;
                        continue;
                    }
                    rewritten[out_pos++] = '\\';
                    rewritten[out_pos++] = (char)('0' + numeric_index);
                    i = j;
                    continue;
                }
                if (group_count == 0) {
                    rewritten[out_pos++] = 'k';
                    for (int k = name_start - 1; k <= j; k++) {
                        rewritten[out_pos++] = pattern[k];
                    }
                    i = j;
                    continue;
                }
            }
        }
        if (ch == '\\') {
            rewritten[out_pos++] = pattern[i];
            if (i + 1 < pattern_len) rewritten[out_pos++] = pattern[++i];
            continue;
        }
        if (ch == '[') in_class = true;
        else if (ch == ']' && in_class) in_class = false;
        else if (!in_class && ch == '(') {
            int group_index = 0;
            if (i + 1 < pattern_len && pattern[i + 1] == '?') {
                if (i + 3 < pattern_len && pattern[i + 2] == '<' &&
                    pattern[i + 3] != '=' && pattern[i + 3] != '!') {
                    group_index = ++current_group;
                }
            } else {
                group_index = ++current_group;
            }
            if (paren_depth < 128) paren_stack[paren_depth++] = group_index;
        } else if (!in_class && ch == ')') {
            if (paren_depth > 0) paren_depth--;
        }
        rewritten[out_pos++] = pattern[i];
    }
    rewritten[out_pos] = '\0';
    if (out_len) *out_len = out_pos;
    return rewritten;
}

static char* js_regexp_replace_all_owned(char* pattern, int* pattern_len,
    const char* from, const char* to) {
    if (!pattern || !pattern_len || !from || !to) return pattern;
    int from_len = (int)strlen(from);
    int to_len = (int)strlen(to);
    if (from_len <= 0) return pattern;

    int count = 0;
    for (int i = 0; i + from_len <= *pattern_len; ) {
        if (memcmp(pattern + i, from, from_len) == 0) {
            count++;
            i += from_len;
        } else {
            i++;
        }
    }
    if (count == 0) return pattern;

    int new_len = *pattern_len + count * (to_len - from_len);
    char* replaced = (char*)mem_alloc((size_t)new_len + 1, MEM_CAT_JS_RUNTIME);
    if (!replaced) return pattern;

    int in_pos = 0;
    int out_pos = 0;
    while (in_pos < *pattern_len) {
        if (in_pos + from_len <= *pattern_len &&
            memcmp(pattern + in_pos, from, from_len) == 0) {
            memcpy(replaced + out_pos, to, (size_t)to_len);
            out_pos += to_len;
            in_pos += from_len;
        } else {
            replaced[out_pos++] = pattern[in_pos++];
        }
    }
    replaced[out_pos] = '\0';
    mem_free(pattern);
    *pattern_len = out_pos;
    return replaced;
}

static void js_regexp_replace_property_alias(char** pattern, int* pattern_len,
    const char* key, const char* value, const char* canonical) {
    char from[128];
    char to[128];
    snprintf(from, sizeof(from), "\\p{%s=%s}", key, value);
    snprintf(to, sizeof(to), "\\p{%s}", canonical);
    *pattern = js_regexp_replace_all_owned(*pattern, pattern_len, from, to);
    snprintf(from, sizeof(from), "\\P{%s=%s}", key, value);
    snprintf(to, sizeof(to), "\\P{%s}", canonical);
    *pattern = js_regexp_replace_all_owned(*pattern, pattern_len, from, to);
}

static void js_regexp_replace_bare_property_alias(char** pattern, int* pattern_len,
    const char* value, const char* canonical) {
    char from[96];
    char to[96];
    snprintf(from, sizeof(from), "\\p{%s}", value);
    snprintf(to, sizeof(to), "\\p{%s}", canonical);
    *pattern = js_regexp_replace_all_owned(*pattern, pattern_len, from, to);
    snprintf(from, sizeof(from), "\\P{%s}", value);
    snprintf(to, sizeof(to), "\\P{%s}", canonical);
    *pattern = js_regexp_replace_all_owned(*pattern, pattern_len, from, to);
}

static void js_regexp_replace_property_escape(char** pattern, int* pattern_len,
    const char* key, const char* value, const char* replacement) {
    char from[128];
    snprintf(from, sizeof(from), "\\p{%s=%s}", key, value);
    *pattern = js_regexp_replace_all_owned(*pattern, pattern_len, from, replacement);
    snprintf(from, sizeof(from), "\\P{%s=%s}", key, value);
    int replacement_len = (int)strlen(replacement);
    char* negated = (char*)mem_alloc((size_t)replacement_len + 3, MEM_CAT_JS_RUNTIME);
    if (!negated) return;
    negated[0] = '[';
    negated[1] = '^';
    if (replacement[0] == '[') {
        memcpy(negated + 2, replacement + 1, (size_t)replacement_len);
        negated[replacement_len + 1] = '\0';
    } else {
        memcpy(negated + 2, replacement, (size_t)replacement_len + 1);
    }
    *pattern = js_regexp_replace_all_owned(*pattern, pattern_len, from, negated);
    mem_free(negated);
}

char* js_regexp_canonicalize_property_escapes(const char* pattern, int pattern_len,
    int* out_len) {
    if (out_len) *out_len = pattern_len;
    if (!pattern || pattern_len < 0) return NULL;
    char* result = (char*)mem_alloc((size_t)pattern_len + 1, MEM_CAT_JS_RUNTIME);
    if (!result) return NULL;
    memcpy(result, pattern, (size_t)pattern_len);
    result[pattern_len] = '\0';
    int result_len = pattern_len;

    static const struct { const char* value; const char* canonical; } gc_aliases[] = {
        {"Cased_Letter", "LC"}, {"LC", "LC"}, {"L&", "LC"},
        {"Close_Punctuation", "Pe"}, {"Connector_Punctuation", "Pc"},
        {"Control", "Cc"}, {"cntrl", "Cc"},
        {"Currency_Symbol", "Sc"}, {"Decimal_Number", "Nd"}, {"digit", "Nd"},
        {"Enclosing_Mark", "Me"}, {"Final_Punctuation", "Pf"},
        {"Format", "Cf"}, {"Initial_Punctuation", "Pi"},
        {"Letter", "L"}, {"Letter_Number", "Nl"}, {"Line_Separator", "Zl"},
        {"Lowercase_Letter", "Ll"}, {"Mark", "M"}, {"Combining_Mark", "M"},
        {"Math_Symbol", "Sm"}, {"Modifier_Letter", "Lm"}, {"Modifier_Symbol", "Sk"},
        {"Nonspacing_Mark", "Mn"}, {"Number", "N"}, {"Open_Punctuation", "Ps"},
        {"Other", "C"}, {"Other_Letter", "Lo"}, {"Other_Number", "No"},
        {"Other_Punctuation", "Po"}, {"Other_Symbol", "So"},
        {"Paragraph_Separator", "Zp"}, {"Private_Use", "Co"},
        {"Punctuation", "P"}, {"punct", "P"}, {"Separator", "Z"},
        {"Space_Separator", "Zs"}, {"Surrogate", "Cs"}, {"Symbol", "S"},
        {"Titlecase_Letter", "Lt"}, {"Unassigned", "Cn"},
        {"Uppercase_Letter", "Lu"},
    };
    for (int i = 0; i < (int)(sizeof(gc_aliases) / sizeof(gc_aliases[0])); i++) {
        js_regexp_replace_property_alias(&result, &result_len, "General_Category", gc_aliases[i].value, gc_aliases[i].canonical);
        js_regexp_replace_property_alias(&result, &result_len, "gc", gc_aliases[i].value, gc_aliases[i].canonical);
        js_regexp_replace_bare_property_alias(&result, &result_len, gc_aliases[i].value, gc_aliases[i].canonical);
    }

    static const char greek_scx[] =
        "[\\x{00B7}\\x{0300}-\\x{0301}\\x{0304}\\x{0306}\\x{0308}\\x{0313}"
        "\\x{0342}\\x{0345}\\x{0370}-\\x{0377}\\x{037A}-\\x{037D}\\x{037F}"
        "\\x{0384}\\x{0386}\\x{0388}-\\x{038A}\\x{038C}\\x{038E}-\\x{03A1}"
        "\\x{03A3}-\\x{03E1}\\x{03F0}-\\x{03FF}\\x{1D26}-\\x{1D2A}"
        "\\x{1D5D}-\\x{1D61}\\x{1D66}-\\x{1D6A}\\x{1DBF}-\\x{1DC1}"
        "\\x{1F00}-\\x{1F15}\\x{1F18}-\\x{1F1D}\\x{1F20}-\\x{1F45}"
        "\\x{1F48}-\\x{1F4D}\\x{1F50}-\\x{1F57}\\x{1F59}\\x{1F5B}"
        "\\x{1F5D}\\x{1F5F}-\\x{1F7D}\\x{1F80}-\\x{1FB4}\\x{1FB6}-\\x{1FC4}"
        "\\x{1FC6}-\\x{1FD3}\\x{1FD6}-\\x{1FDB}\\x{1FDD}-\\x{1FEF}"
        "\\x{1FF2}-\\x{1FF4}\\x{1FF6}-\\x{1FFE}\\x{205D}\\x{2126}"
        "\\x{AB65}\\x{10140}-\\x{1018E}\\x{101A0}\\x{1D200}-\\x{1D245}]";
    js_regexp_replace_property_escape(&result, &result_len, "Script_Extensions", "Greek", greek_scx);
    js_regexp_replace_property_escape(&result, &result_len, "Script_Extensions", "Grek", greek_scx);
    js_regexp_replace_property_escape(&result, &result_len, "scx", "Greek", greek_scx);
    js_regexp_replace_property_escape(&result, &result_len, "scx", "Grek", greek_scx);

    static const struct { const char* value; const char* canonical; } script_aliases[] = {
        {"Arab", "Arabic"}, {"Armn", "Armenian"}, {"Beng", "Bengali"},
        {"Bopo", "Bopomofo"}, {"Brai", "Braille"}, {"Cyrl", "Cyrillic"},
        {"Deva", "Devanagari"}, {"Ethi", "Ethiopic"}, {"Geor", "Georgian"},
        {"Grek", "Greek"}, {"Gujr", "Gujarati"}, {"Guru", "Gurmukhi"},
        {"Hang", "Hangul"}, {"Hani", "Han"}, {"Hans", "Han"}, {"Hant", "Han"},
        {"Hebr", "Hebrew"}, {"Hira", "Hiragana"}, {"Kana", "Katakana"},
        {"Khmr", "Khmer"}, {"Knda", "Kannada"}, {"Laoo", "Lao"},
        {"Latn", "Latin"}, {"Mlym", "Malayalam"}, {"Mong", "Mongolian"},
        {"Mymr", "Myanmar"}, {"Orya", "Oriya"}, {"Sinh", "Sinhala"},
        {"Taml", "Tamil"}, {"Telu", "Telugu"}, {"Thai", "Thai"},
        {"Tibt", "Tibetan"}, {"Zyyy", "Common"}, {"Zinh", "Inherited"},
        {"Qaai", "Inherited"}, {"Zzzz", "Unknown"},
    };
    for (int i = 0; i < (int)(sizeof(script_aliases) / sizeof(script_aliases[0])); i++) {
        js_regexp_replace_property_alias(&result, &result_len, "Script", script_aliases[i].value, script_aliases[i].canonical);
        js_regexp_replace_property_alias(&result, &result_len, "sc", script_aliases[i].value, script_aliases[i].canonical);
        js_regexp_replace_property_alias(&result, &result_len, "Script_Extensions", script_aliases[i].value, script_aliases[i].canonical);
        js_regexp_replace_property_alias(&result, &result_len, "scx", script_aliases[i].value, script_aliases[i].canonical);
    }

    if (out_len) *out_len = result_len;
    return result;
}
