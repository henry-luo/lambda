#include "js_regex_generated_properties.h"
#include <cstring>

static bool js_regex_match_property_name(const char* name, int len, const char* target) {
    return (int)strlen(target) == len && strncmp(name, target, len) == 0;
}

static bool js_regex_sorted_range_contains(const JsRegexRange* ranges, int count, int cp) {
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

extern "C" bool js_unicode_id_is_start(uint32_t cp) {
    if (cp == '$' || cp == '_') return true;
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    return js_regex_sorted_range_contains(js_regex_generated_ranges_70_id_start,
        (int)(sizeof(js_regex_generated_ranges_70_id_start) / sizeof(js_regex_generated_ranges_70_id_start[0])),
        (int)cp);
}

extern "C" bool js_unicode_id_is_continue(uint32_t cp) {
    if (cp == '$' || cp == '_' || cp == 0x200C || cp == 0x200D) return true;
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                          (cp >= '0' && cp <= '9');
    return js_regex_sorted_range_contains(js_regex_generated_ranges_69_id_continue,
        (int)(sizeof(js_regex_generated_ranges_69_id_continue) / sizeof(js_regex_generated_ranges_69_id_continue[0])),
        (int)cp);
}
