#include "js_regex_generated_properties.h"
#include <cstring>

static bool js_regex_match_property_name(const char* name, int len, const char* target) {
    return (int)strlen(target) == len && strncmp(name, target, len) == 0;
}

bool js_regex_sorted_range_contains(const CodePointInterval* ranges, int count, int cp) {
    if (cp < 0) return false;
    uint32_t codepoint = (uint32_t)cp;
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        if (codepoint < ranges[mid].first) {
            hi = mid - 1;
        } else if (codepoint > ranges[mid].last) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

static bool js_regex_sorted_range_contains_cursor(const CodePointInterval* ranges, int count, int cp, int* cursor) {
    if (cp < 0) return false;
    uint32_t codepoint = (uint32_t)cp;
    if (count <= 0) return false;
    int c = *cursor;
    if (c >= 0 && c < count) {
        if (codepoint_interval_contains(&ranges[c], codepoint)) return true;
        if (codepoint > ranges[c].last) {
            int next = c + 1;
            if (next < count) {
                if (codepoint < ranges[next].first) { *cursor = c; return false; }
                if (codepoint <= ranges[next].last) { *cursor = next; return true; }
            } else {
                *cursor = c; return false;
            }
            int lo = next + 1;
            int hi = count - 1;
            while (lo <= hi) {
                int mid = lo + ((hi - lo) / 2);
                if (codepoint < ranges[mid].first) hi = mid - 1;
                else if (codepoint > ranges[mid].last) lo = mid + 1;
                else { *cursor = mid; return true; }
            }
            *cursor = hi >= 0 ? hi : 0;
            return false;
        }
        int lo = 0;
        int hi = c - 1;
        while (lo <= hi) {
            int mid = lo + ((hi - lo) / 2);
            if (codepoint < ranges[mid].first) hi = mid - 1;
            else if (codepoint > ranges[mid].last) lo = mid + 1;
            else { *cursor = mid; return true; }
        }
        *cursor = hi >= 0 ? hi : 0;
        return false;
    }
    int lo = 0;
    int hi = count - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) / 2);
        if (codepoint < ranges[mid].first) hi = mid - 1;
        else if (codepoint > ranges[mid].last) lo = mid + 1;
        else { *cursor = mid; return true; }
    }
    *cursor = hi >= 0 ? hi : 0;
    return false;
}

#include "js_regex_generated_property_tables.inc"

static bool js_regex_generated_property_has_prefix(const char* name, int name_len,
                                                   const char* prefix, int prefix_len) {
    return name && name_len > prefix_len && memcmp(name, prefix, prefix_len) == 0;
}

static int js_regex_generated_property_kind_scan_range(const char* name, int name_len,
                                                       int start, int end) {
    for (int i = start; i < end; i++) {
        if (js_regex_match_property_name(name, name_len, js_regex_generated_property_tables[i].name)) {
            return JS_REGEX_PROP_GENERATED_BASE + i;
        }
    }
    return 0;
}

static void js_regex_generated_property_init_script_buckets(int* script_start, int* script_end,
                                                           int* scx_start, int* scx_end) {
    int table_count = (int)(sizeof(js_regex_generated_property_tables) /
                            sizeof(js_regex_generated_property_tables[0]));
    *script_start = table_count;
    *script_end = 0;
    *scx_start = table_count;
    *scx_end = 0;
    for (int i = 0; i < table_count; i++) {
        const char* table_name = js_regex_generated_property_tables[i].name;
        bool is_script = strncmp(table_name, "Script=", 7) == 0 ||
                         strncmp(table_name, "sc=", 3) == 0;
        bool is_scx = strncmp(table_name, "Script_Extensions=", 18) == 0 ||
                      strncmp(table_name, "scx=", 4) == 0;
        if (is_script) {
            if (i < *script_start) *script_start = i;
            if (i + 1 > *script_end) *script_end = i + 1;
        }
        if (is_scx) {
            if (i < *scx_start) *scx_start = i;
            if (i + 1 > *scx_end) *scx_end = i + 1;
        }
    }
    if (*script_start > *script_end) *script_start = *script_end = 0;
    if (*scx_start > *scx_end) *scx_start = *scx_end = 0;
}

static int js_regex_generated_property_kind_from_name(const char* name, int name_len) {
    static bool script_buckets_ready = false;
    static int script_start = 0;
    static int script_end = 0;
    static int scx_start = 0;
    static int scx_end = 0;

    bool is_script_name =
        js_regex_generated_property_has_prefix(name, name_len, "Script=", 7) ||
        js_regex_generated_property_has_prefix(name, name_len, "sc=", 3);
    bool is_scx_name =
        js_regex_generated_property_has_prefix(name, name_len, "Script_Extensions=", 18) ||
        js_regex_generated_property_has_prefix(name, name_len, "scx=", 4);
    if (is_script_name || is_scx_name) {
        if (!script_buckets_ready) {
            js_regex_generated_property_init_script_buckets(&script_start, &script_end,
                                                            &scx_start, &scx_end);
            script_buckets_ready = true;
        }
        return is_scx_name
            ? js_regex_generated_property_kind_scan_range(name, name_len, scx_start, scx_end)
            : js_regex_generated_property_kind_scan_range(name, name_len, script_start, script_end);
    }

    int table_count = (int)(sizeof(js_regex_generated_property_tables) /
                            sizeof(js_regex_generated_property_tables[0]));
    return js_regex_generated_property_kind_scan_range(name, name_len, 0, table_count);
}

static int js_regex_generated_property_canonical_kind(int kind) {
    int index = kind - JS_REGEX_PROP_GENERATED_BASE;
    int table_count = (int)(sizeof(js_regex_generated_property_tables) /
                            sizeof(js_regex_generated_property_tables[0]));
    if (index < 0 || index >= table_count) return kind;
    const JsRegexGeneratedPropertyTable* table = &js_regex_generated_property_tables[index];
    for (int i = index - 1; i >= 0; i--) {
        const JsRegexGeneratedPropertyTable* prev = &js_regex_generated_property_tables[i];
        if (prev->ranges == table->ranges && prev->count == table->count) {
            kind = JS_REGEX_PROP_GENERATED_BASE + i;
        }
    }
    return kind;
}

static bool js_regex_generated_property_contains(int kind, int cp) {
    int index = kind - JS_REGEX_PROP_GENERATED_BASE;
    int count = (int)(sizeof(js_regex_generated_property_tables) / sizeof(js_regex_generated_property_tables[0]));
    if (index < 0 || index >= count) return false;
    const JsRegexGeneratedPropertyTable* table = &js_regex_generated_property_tables[index];
    return js_regex_sorted_range_contains(table->ranges, table->count, cp);
}

static bool js_regex_generated_property_contains_cursor(int kind, int cp, int* cursor) {
    int index = kind - JS_REGEX_PROP_GENERATED_BASE;
    int count = (int)(sizeof(js_regex_generated_property_tables) / sizeof(js_regex_generated_property_tables[0]));
    if (index < 0 || index >= count) return false;
    const JsRegexGeneratedPropertyTable* table = &js_regex_generated_property_tables[index];
    return js_regex_sorted_range_contains_cursor(table->ranges, table->count, cp, cursor);
}

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

// identifiers and RegExp property escapes share the same Unicode version.
extern "C" bool js_unicode_id_is_start(uint32_t cp) {
    if (cp == '$' || cp == '_') return true;
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
    return js_regex_sorted_range_contains(js_unicode_id_start_ranges, js_unicode_id_start_count, (int)cp);
}

extern "C" bool js_unicode_id_is_continue(uint32_t cp) {
    if (cp == '$' || cp == '_' || cp == 0x200C || cp == 0x200D) return true;
    if (cp < 0x80) return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
                          (cp >= '0' && cp <= '9');
    return js_regex_sorted_range_contains(js_unicode_id_continue_ranges, js_unicode_id_continue_count, (int)cp);
}
