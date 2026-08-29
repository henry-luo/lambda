#include "js_regex_generated_properties.h"
#include "utf8proc.h"
#include <cstring>

static bool js_regex_match_property_name(const char* name, int len, const char* target) {
    return (int)strlen(target) == len && strncmp(name, target, len) == 0;
}

bool js_regex_sorted_range_contains(const JsRegexRange* ranges, int count, int cp) {
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        if (cp < ranges[mid].first) {
            hi = mid - 1;
        } else if (cp > ranges[mid].last) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

// UNUSED_FUNCTION_OK: called from the generated js_regex_generated_property_tables.inc,
// which the unused-function lint does not scan (*.inc extension).
static bool js_regex_sorted_range_contains_cursor(const JsRegexRange* ranges, int count, int cp, int* cursor) {
    if (count <= 0) return false;
    int c = *cursor;
    if (c >= 0 && c < count) {
        if (cp >= ranges[c].first && cp <= ranges[c].last) return true;
        if (cp > ranges[c].last) {
            int next = c + 1;
            if (next < count) {
                if (cp < ranges[next].first) { *cursor = c; return false; }
                if (cp <= ranges[next].last) { *cursor = next; return true; }
            } else {
                *cursor = c; return false;
            }
            int lo = next + 1;
            int hi = count - 1;
            while (lo <= hi) {
                int mid = lo + ((hi - lo) / 2);
                if (cp < ranges[mid].first) hi = mid - 1;
                else if (cp > ranges[mid].last) lo = mid + 1;
                else { *cursor = mid; return true; }
            }
            *cursor = hi >= 0 ? hi : 0;
            return false;
        }
        int lo = 0;
        int hi = c - 1;
        while (lo <= hi) {
            int mid = lo + ((hi - lo) / 2);
            if (cp < ranges[mid].first) hi = mid - 1;
            else if (cp > ranges[mid].last) lo = mid + 1;
            else { *cursor = mid; return true; }
        }
        *cursor = hi >= 0 ? hi : 0;
        return false;
    }
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        if (cp < ranges[mid].first) hi = mid - 1;
        else if (cp > ranges[mid].last) lo = mid + 1;
        else { *cursor = mid; return true; }
    }
    *cursor = hi >= 0 ? hi : 0;
    return false;
}

#include "js_regex_generated_property_tables.inc"

int js_regex_generated_property_lookup_kind(const char* name, int name_len) {
    return js_regex_generated_property_kind_from_name(name, name_len);
}

int js_regex_generated_property_canonicalize_kind(int kind) {
    return js_regex_generated_property_canonical_kind(kind);
}

bool js_regex_generated_property_kind_contains(int kind, int cp) {
    return js_regex_generated_property_contains(kind, cp);
}

bool js_regex_generated_property_kind_contains_cursor(int kind, int cp, int* cursor) {
    return js_regex_generated_property_contains_cursor(kind, cp, cursor);
}

extern "C" int js_regex_wrapper_lookup_property_ranges(const char* name, int name_len,
                                                       int* out_pairs, int max_pairs) {
    if (!name || name_len <= 0 || !out_pairs || max_pairs <= 0) return 0;
    int table_count = (int)(sizeof(js_regex_generated_property_tables) /
                            sizeof(js_regex_generated_property_tables[0]));
    for (int i = 0; i < table_count; i++) {
        const JsRegexGeneratedPropertyTable& t = js_regex_generated_property_tables[i];
        if (js_regex_match_property_name(name, name_len, t.name)) {
            int n = t.count;
            if (n > max_pairs) n = max_pairs;
            for (int k = 0; k < n; k++) {
                out_pairs[k * 2 + 0] = t.ranges[k].first;
                out_pairs[k * 2 + 1] = t.ranges[k].last;
            }
            return n;
        }
    }
    return 0;
}

// ECMAScript IdentifierStart/IdentifierPart follows Unicode categories;
// the generated RegExp tables are pinned to older property data.
static bool js_unicode_id_is_start_base(uint32_t cp) {
    if (cp == 0x2118 || cp == 0x212E || (cp >= 0x309B && cp <= 0x309C) ||
            (cp >= 0x1885 && cp <= 0x1886)) return true;
    utf8proc_category_t category = utf8proc_category((utf8proc_int32_t)cp);
    return category == UTF8PROC_CATEGORY_LU || category == UTF8PROC_CATEGORY_LL ||
           category == UTF8PROC_CATEGORY_LT || category == UTF8PROC_CATEGORY_LM ||
           category == UTF8PROC_CATEGORY_LO || category == UTF8PROC_CATEGORY_NL;
}

extern "C" bool js_unicode_id_is_start(uint32_t cp) {
    if (cp == '$' || cp == '_') return true;
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    return js_unicode_id_is_start_base(cp);
}

extern "C" bool js_unicode_id_is_continue(uint32_t cp) {
    if (cp == '$' || cp == '_' || cp == 0x200C || cp == 0x200D) return true;
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                          (cp >= '0' && cp <= '9');
    if (js_unicode_id_is_start_base(cp)) return true;
    if (cp == 0x00B7 || cp == 0x0387 || cp == 0x30FB || cp == 0xFF65 ||
            (cp >= 0x1369 && cp <= 0x1371) || cp == 0x19DA) return true;
    utf8proc_category_t category = utf8proc_category((utf8proc_int32_t)cp);
    return category == UTF8PROC_CATEGORY_MN || category == UTF8PROC_CATEGORY_MC ||
           category == UTF8PROC_CATEGORY_ND || category == UTF8PROC_CATEGORY_PC;
}
