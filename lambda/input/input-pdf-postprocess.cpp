// PDF post-processing pass.
//
// Runs after the raw parser (input-pdf.cpp) has produced the indirect-object
// table. Performs two structural enrichments that consumers (notably the
// upcoming `lambda/package/pdf/` Lambda Script package) would otherwise have
// to redo for every consumer:
//
//   1. Page-tree flattening   — resolves /Root → /Pages and recursively walks
//      the /Kids tree, materialising a flat `pages: [Map]` array on the root
//      pdf_info Map. Each entry has its inheritable attributes
//      (Resources / MediaBox / CropBox / Rotate) merged in.
//
//   2. ToUnicode CMap parsing — for every Font dict that carries a
//      /ToUnicode reference, resolves and decompresses the CMap stream,
//      parses the bfchar / bfrange sections, and attaches the resulting
//      `cid -> unicode codepoint` mapping as a `to_unicode: Map` field on
//      the font dict.
//
// Both passes are best-effort: failures are logged and silently skipped so
// that the parser's tolerance for malformed PDFs is preserved.

#include "input.hpp"
#include "input-parsers.h"
#include "../mark_builder.hpp"
#include "../mark_reader.hpp"
#include "lib/log.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

extern "C" {
    #include "pdf_decompress.h"
}

// ---------------------------------------------------------------------------
// Small helpers for working with the parser's Map/Item shape.
// ---------------------------------------------------------------------------

namespace {

// Pool used for transient key allocations during the postprocess passes.
// Set once at the entry of pdf_postprocess(); kept for any future use.
static Pool* g_post_pool = nullptr;

// Look up a string-keyed field in a Map. Uses MarkReader's MapReader, which
// already implements name-based lookup against ShapeEntry without exposing
// the internal map_get() runtime helper (gated out by LAMBDA_STATIC).
static inline Item map_lookup(Map* m, const char* key) {
    if (!m) return {.item = ITEM_NULL};
    MapReader mr(m);
    ItemReader val = mr.get(key);
    return val.item();
}

// Treat any Item as a Container* and read its leading TypeId byte.
static inline TypeId item_container_type(Item it) {
    if (!it.item || it.item == ITEM_NULL || it.item == ITEM_ERROR) {
        return LMD_TYPE_NULL;
    }
    TypeId tag = (TypeId)((it.item >> 56) & 0xFF);
    // Containers have a zero high byte and the type sits at offset 0 of the
    // pointed-to struct.
    if (tag == 0) {
        Container* c = (Container*)it.item;
        return c->type_id;
    }
    return tag;
}

static inline Map* item_as_map(Item it) {
    return item_container_type(it) == LMD_TYPE_MAP ? (Map*)it.item : nullptr;
}

static inline Array* item_as_array(Item it) {
    return item_container_type(it) == LMD_TYPE_ARRAY ? (Array*)it.item : nullptr;
}

static inline String* item_as_string(Item it) {
    TypeId t = (TypeId)((it.item >> 56) & 0xFF);
    if (t == LMD_TYPE_STRING || t == LMD_TYPE_SYMBOL) {
        return (String*)(it.item & 0x00FFFFFFFFFFFFFFULL);
    }
    return nullptr;
}

// Compare a String*'s chars to a literal.
static inline bool str_eq(String* s, const char* lit) {
    if (!s || !lit) return false;
    size_t n = strlen(lit);
    return s->len == n && memcmp(s->chars, lit, n) == 0;
}

// Read a numeric Item as int (handles d2it floats and packed ints).
static inline int item_as_int(Item it, int dflt) {
    TypeId t = (TypeId)((it.item >> 56) & 0xFF);
    if (t == LMD_TYPE_INT) {
        return (int)(int32_t)(it.item & 0xFFFFFFFFULL);
    }
    if (t == 0 && it.item) {
        // Could be a heap double (d2it). Conservative: peek at high tag again.
        // d2it stores LMD_TYPE_FLOAT in the high byte; if we got here the tag
        // was zero, so this isn't a float — give up.
    }
    if (t == LMD_TYPE_FLOAT) {
        double* d = (double*)(it.item & 0x00FFFFFFFFFFFFFFULL);
        return d ? (int)*d : dflt;
    }
    return dflt;
}

// Quick "is this Map our wrapper of type X" check.
static bool map_has_type(Map* m, const char* type_str) {
    Item ty = map_lookup(m, "type");
    String* s = item_as_string(ty);
    return str_eq(s, type_str);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Object table: object_num -> content Item
// ---------------------------------------------------------------------------

namespace {

struct ObjEntry {
    int obj_num;
    Item content;
};

struct ObjTable {
    ObjEntry* entries;
    int count;
    int capacity;
};

static void obj_table_init(ObjTable* t, Pool* pool, int cap) {
    t->entries = (ObjEntry*)pool_calloc(pool, sizeof(ObjEntry) * cap);
    t->count = 0;
    t->capacity = t->entries ? cap : 0;
}

static void obj_table_add(ObjTable* t, int num, Item content) {
    if (t->count >= t->capacity) return;  // best-effort
    t->entries[t->count++] = { num, content };
}

static Item obj_table_get(ObjTable* t, int num) {
    // Linear search is fine — typical PDFs have <1000 objects in the parser
    // truncated view, and this runs once per font/page reference.
    for (int i = 0; i < t->count; i++) {
        if (t->entries[i].obj_num == num) return t->entries[i].content;
    }
    return {.item = ITEM_NULL};
}

// Build the obj_num -> content map from the parser's `objects` array.
static void build_obj_table(Array* objects, ObjTable* table, Pool* pool) {
    if (!objects) return;
    obj_table_init(table, pool, objects->length > 0 ? objects->length + 8 : 8);
    for (int i = 0; i < objects->length; i++) {
        Item it = objects->items[i];
        Map* m = item_as_map(it);
        if (!m || !map_has_type(m, "indirect_object")) continue;
        Item num_it = map_lookup(m, "object_num");
        int num = -1;
        TypeId nt = (TypeId)((num_it.item >> 56) & 0xFF);
        if (nt == LMD_TYPE_FLOAT) {
            double* d = (double*)(num_it.item & 0x00FFFFFFFFFFFFFFULL);
            if (d) num = (int)*d;
        } else {
            num = item_as_int(num_it, -1);
        }
        if (num < 0) continue;
        Item content = map_lookup(m, "content");
        if (content.item == ITEM_NULL) continue;
        obj_table_add(table, num, content);
    }
    log_info("pdf_postprocess: built object table with %d entries", table->count);
}

// Resolve an indirect_ref Map to its content (one hop). Non-refs pass through.
static Item resolve_ref(Item it, ObjTable* table) {
    Map* m = item_as_map(it);
    if (!m || !map_has_type(m, "indirect_ref")) return it;
    Item num_it = map_lookup(m, "object_num");
    int num = -1;
    TypeId nt = (TypeId)((num_it.item >> 56) & 0xFF);
    if (nt == LMD_TYPE_FLOAT) {
        double* d = (double*)(num_it.item & 0x00FFFFFFFFFFFFFFULL);
        if (d) num = (int)*d;
    } else {
        num = item_as_int(num_it, -1);
    }
    if (num < 0) return {.item = ITEM_NULL};
    return obj_table_get(table, num);
}

// Resolve all the way through any chain of indirect_refs (cycle-safe up to N).
static Item resolve_ref_deep(Item it, ObjTable* table) {
    for (int hops = 0; hops < 8; hops++) {
        Map* m = item_as_map(it);
        if (!m || !map_has_type(m, "indirect_ref")) return it;
        it = resolve_ref(it, table);
        if (it.item == ITEM_NULL) return it;
    }
    return it;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Pass 1: Page-tree flattening
// ---------------------------------------------------------------------------

namespace {

// Forward decl
static void flatten_page_node(Input* input, MarkBuilder& builder, Map* node,
                              Item inherited_resources, Item inherited_mediabox,
                              Item inherited_cropbox, Item inherited_rotate,
                              ObjTable* table, Array* out_pages, int depth);

static void flatten_page_node(Input* input, MarkBuilder& builder, Map* node,
                              Item inherited_resources, Item inherited_mediabox,
                              Item inherited_cropbox, Item inherited_rotate,
                              ObjTable* table, Array* out_pages, int depth) {
    if (!node || depth > 32) return;  // depth cap protects against cycles

    // Inheritable attrs: child's own value wins, otherwise inherited value.
    Item resources = map_lookup(node, "Resources");
    if (resources.item == ITEM_NULL) resources = inherited_resources;
    Item mediabox = map_lookup(node, "MediaBox");
    if (mediabox.item == ITEM_NULL) mediabox = inherited_mediabox;
    Item cropbox = map_lookup(node, "CropBox");
    if (cropbox.item == ITEM_NULL) cropbox = inherited_cropbox;
    Item rotate = map_lookup(node, "Rotate");
    if (rotate.item == ITEM_NULL) rotate = inherited_rotate;

    // Determine node kind via /Type. Some PDFs omit /Type on intermediate
    // nodes — fall back to "has /Kids" for Pages, "has /Contents" for Page.
    Item type_it = map_lookup(node, "Type");
    String* type_s = item_as_string(type_it);
    Item kids = map_lookup(node, "Kids");
    Array* kids_arr = item_as_array(kids);

    bool is_pages = (type_s && str_eq(type_s, "Pages")) || (kids_arr != nullptr);

    if (is_pages && kids_arr) {
        for (int i = 0; i < kids_arr->length; i++) {
            Item kid = resolve_ref_deep(kids_arr->items[i], table);
            Map* kid_m = item_as_map(kid);
            if (!kid_m) continue;
            flatten_page_node(input, builder, kid_m,
                              resources, mediabox, cropbox, rotate,
                              table, out_pages, depth + 1);
        }
        return;
    }

    // Treat as a leaf page. Build a flattened page Map carrying both the
    // original page dict and the resolved inheritable attrs.
    Map* flat = map_pooled(input->pool);
    if (!flat) return;
    builder.putToMap(flat, builder.createString("type"),
                     {.item = s2it(builder.createString("page"))});
    builder.putToMap(flat, builder.createString("dict"),
                     {.item = (uint64_t)node});
    if (resources.item != ITEM_NULL) {
        Item r = resolve_ref_deep(resources, table);
        builder.putToMap(flat, builder.createString("resources"), r);
    }
    if (mediabox.item != ITEM_NULL) {
        builder.putToMap(flat, builder.createString("media_box"), mediabox);
    }
    if (cropbox.item != ITEM_NULL) {
        builder.putToMap(flat, builder.createString("crop_box"), cropbox);
    }
    if (rotate.item != ITEM_NULL) {
        builder.putToMap(flat, builder.createString("rotate"), rotate);
    }
    // Page index assigned by caller via array position.
    array_append(out_pages, {.item = (uint64_t)flat}, input->pool, input->arena);
}

static void flatten_page_tree(Input* input, MarkBuilder& builder,
                              Map* pdf_info, ObjTable* table) {
    Item trailer_it = map_lookup(pdf_info, "trailer");
    Map* trailer_wrap = item_as_map(trailer_it);
    if (!trailer_wrap) {
        log_info("pdf_postprocess: no trailer found, skipping page-tree flatten");
        return;
    }
    Item dict_it = map_lookup(trailer_wrap, "dictionary");
    Map* trailer_dict = item_as_map(dict_it);
    if (!trailer_dict) return;

    Item root_it = resolve_ref_deep(map_lookup(trailer_dict, "Root"), table);
    Map* catalog = item_as_map(root_it);
    if (!catalog) {
        log_info("pdf_postprocess: trailer has no resolvable /Root, skipping");
        return;
    }

    Item pages_it = resolve_ref_deep(map_lookup(catalog, "Pages"), table);
    Map* pages_root = item_as_map(pages_it);
    if (!pages_root) {
        log_info("pdf_postprocess: catalog has no resolvable /Pages, skipping");
        return;
    }

    Array* out = array_pooled(input->pool);
    if (!out) return;

    flatten_page_node(input, builder, pages_root,
                      {.item = ITEM_NULL}, {.item = ITEM_NULL},
                      {.item = ITEM_NULL}, {.item = ITEM_NULL},
                      table, out, 0);

    builder.putToMap(pdf_info, builder.createString("pages"),
                     {.item = (uint64_t)out});
    log_info("pdf_postprocess: flattened page tree into %d pages", out->length);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Pass 2: ToUnicode CMap parsing
// ---------------------------------------------------------------------------

namespace {

// Parse a hex string like <0041> or <00410042> at *pp. Advances *pp past the
// closing '>'. Returns the value and the byte count (hex_len/2) via outputs.
// Returns false on malformed input.
static bool parse_hex_token(const char** pp, const char* end,
                            uint32_t* out_value, int* out_byte_count) {
    const char* s = *pp;
    if (s >= end || *s != '<') return false;
    s++;
    uint32_t v = 0;
    int hex_chars = 0;
    while (s < end && *s != '>') {
        char c = *s++;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (isspace((unsigned char)c)) continue;
        else return false;
        if (hex_chars >= 8) return false;  // up to 32 bits
        v = (v << 4) | (uint32_t)d;
        hex_chars++;
    }
    if (s >= end || *s != '>') return false;
    s++;  // skip '>'
    *pp = s;
    *out_value = v;
    if (out_byte_count) *out_byte_count = (hex_chars + 1) / 2;
    return true;
}

static const char* skip_ws(const char* s, const char* end) {
    while (s < end && (isspace((unsigned char)*s) || *s == '%')) {
        if (*s == '%') {  // comment to end-of-line
            while (s < end && *s != '\n' && *s != '\r') s++;
        } else {
            s++;
        }
    }
    return s;
}

// Find the next occurrence of `kw` (whole-word: preceded/followed by ws or
// boundary). Returns pointer to first char after kw, or NULL.
static const char* find_keyword(const char* s, const char* end, const char* kw) {
    size_t kl = strlen(kw);
    while (s + kl <= end) {
        if (memcmp(s, kw, kl) == 0) {
            const char* after = s + kl;
            if (after >= end || isspace((unsigned char)*after) ||
                *after == '<' || *after == '[') {
                return after;
            }
        }
        s++;
    }
    return nullptr;
}

// Add a CID -> unicode codepoint mapping into the to_unicode result map.
// Key is a small decimal string (CID), value is a String* containing the UTF-8
// encoding of the unicode code point. Multi-codepoint mappings (ligatures) are
// concatenated into a single UTF-8 string.
static void add_unicode_mapping(Input* input, MarkBuilder& builder, Map* out,
                                uint32_t cid, const uint32_t* cps, int n_cps) {
    char key_buf[16];
    int kn = snprintf(key_buf, sizeof(key_buf), "%u", cid);
    if (kn <= 0 || kn >= (int)sizeof(key_buf)) return;
    String* key = builder.createString(key_buf, (size_t)kn);
    if (!key) return;

    // UTF-8 encode all code points into a single buffer (max 4 bytes/cp).
    char utf8[64];
    int len = 0;
    for (int i = 0; i < n_cps && len + 4 < (int)sizeof(utf8); i++) {
        uint32_t cp = cps[i];
        if (cp < 0x80) {
            utf8[len++] = (char)cp;
        } else if (cp < 0x800) {
            utf8[len++] = (char)(0xC0 | (cp >> 6));
            utf8[len++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8[len++] = (char)(0xE0 | (cp >> 12));
            utf8[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8[len++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            utf8[len++] = (char)(0xF0 | (cp >> 18));
            utf8[len++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            utf8[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            utf8[len++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    String* val = builder.createString(utf8, (size_t)len);
    if (!val) return;
    builder.putToMap(out, key, {.item = s2it(val)});
}

// Parse a single bfchar entry's destination — either a hex string (one or
// more code points) into cps[] (max 8). Returns number of code points or 0
// on error. Advances *pp past the destination.
static int parse_bfchar_dst(const char** pp, const char* end,
                            uint32_t* cps, int max_cps) {
    const char* s = skip_ws(*pp, end);
    if (s >= end || *s != '<') return 0;
    s++;
    int n = 0;
    int hex_chars = 0;
    uint32_t acc = 0;
    while (s < end && *s != '>') {
        char c = *s++;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (isspace((unsigned char)c)) continue;
        else return 0;
        acc = (acc << 4) | (uint32_t)d;
        hex_chars++;
        // Each unicode code point is 4 hex chars (UTF-16 BE). Surrogate
        // pairs are handled by combining two adjacent BMP values.
        if (hex_chars == 4) {
            if (n < max_cps) cps[n++] = acc;
            acc = 0;
            hex_chars = 0;
        }
    }
    if (s >= end || *s != '>') return 0;
    s++;
    *pp = s;

    // Combine UTF-16 surrogate pairs into single code points.
    int out = 0;
    for (int i = 0; i < n; i++) {
        uint32_t v = cps[i];
        if (v >= 0xD800 && v <= 0xDBFF && i + 1 < n) {
            uint32_t lo = cps[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cps[out++] = 0x10000 + (((v - 0xD800) << 10) | (lo - 0xDC00));
                i++;
                continue;
            }
        }
        cps[out++] = v;
    }
    return out;
}

static const char* parse_bfchar(const char* s, const char* end,
                                Input* input, MarkBuilder& builder, Map* out) {
    s = skip_ws(s, end);
    while (s < end) {
        s = skip_ws(s, end);
        if (s + 9 <= end && memcmp(s, "endbfchar", 9) == 0) return s + 9;

        uint32_t cid;
        int byte_count;
        if (!parse_hex_token(&s, end, &cid, &byte_count)) break;
        s = skip_ws(s, end);

        uint32_t cps[8];
        int n = parse_bfchar_dst(&s, end, cps, 8);
        if (n <= 0) break;
        add_unicode_mapping(input, builder, out, cid, cps, n);
    }
    return s;
}

static const char* parse_bfrange(const char* s, const char* end,
                                 Input* input, MarkBuilder& builder, Map* out) {
    s = skip_ws(s, end);
    while (s < end) {
        s = skip_ws(s, end);
        if (s + 10 <= end && memcmp(s, "endbfrange", 10) == 0) return s + 10;

        uint32_t lo, hi;
        int bc;
        if (!parse_hex_token(&s, end, &lo, &bc)) break;
        s = skip_ws(s, end);
        if (!parse_hex_token(&s, end, &hi, &bc)) break;
        s = skip_ws(s, end);

        if (s >= end) break;
        if (*s == '[') {
            s++;
            for (uint32_t code = lo; code <= hi && s < end; code++) {
                s = skip_ws(s, end);
                if (s < end && *s == ']') break;
                uint32_t cps[8];
                int n = parse_bfchar_dst(&s, end, cps, 8);
                if (n <= 0) break;
                add_unicode_mapping(input, builder, out, code, cps, n);
            }
            while (s < end && *s != ']') s++;
            if (s < end) s++;
        } else {
            uint32_t cps[8];
            int n = parse_bfchar_dst(&s, end, cps, 8);
            if (n <= 0) break;
            // Generate (hi - lo + 1) mappings, incrementing the *last* code
            // point of the destination per the PDF spec.
            for (uint32_t code = lo; code <= hi; code++) {
                uint32_t off = code - lo;
                uint32_t shifted[8];
                for (int i = 0; i < n; i++) shifted[i] = cps[i];
                shifted[n - 1] += off;
                add_unicode_mapping(input, builder, out, code, shifted, n);
            }
        }
    }
    return s;
}

// Parse a complete decompressed CMap stream, populating `out` with
// CID->utf8-string mappings.
static int parse_to_unicode_cmap(const char* data, size_t len,
                                 Input* input, MarkBuilder& builder, Map* out) {
    const char* s = data;
    const char* end = data + len;
    int sections = 0;
    while (s < end) {
        const char* bfchar = find_keyword(s, end, "beginbfchar");
        const char* bfrange = find_keyword(s, end, "beginbfrange");
        const char* next = nullptr;
        bool is_char = false;
        if (bfchar && (!bfrange || bfchar < bfrange)) {
            next = bfchar; is_char = true;
        } else if (bfrange) {
            next = bfrange; is_char = false;
        }
        if (!next) break;
        if (is_char) {
            s = parse_bfchar(next, end, input, builder, out);
        } else {
            s = parse_bfrange(next, end, input, builder, out);
        }
        sections++;
    }
    return sections;
}

// Extract decompressed payload from a stream Map. Caller must free if
// `*needs_free` is true.
static const char* get_decompressed_stream(Map* stream_map, size_t* out_len,
                                           bool* needs_free) {
    *needs_free = false;
    *out_len = 0;
    Item data_it = map_lookup(stream_map, "data");
    String* data_s = item_as_string(data_it);
    if (!data_s || data_s->len == 0) return nullptr;

    Item dict_it = map_lookup(stream_map, "dictionary");
    Map* dict = item_as_map(dict_it);
    if (!dict) {
        // No dict — treat as already-plain data.
        *out_len = data_s->len;
        return data_s->chars;
    }

    Item filter_it = map_lookup(dict, "Filter");
    if (filter_it.item == ITEM_NULL) {
        *out_len = data_s->len;
        return data_s->chars;
    }

    // Filter may be a single name (String) or an array of names.
    const char* filters_buf[8];
    int n_filters = 0;
    if (Array* fa = item_as_array(filter_it)) {
        for (int i = 0; i < fa->length && n_filters < 8; i++) {
            String* fs = item_as_string(fa->items[i]);
            if (fs) filters_buf[n_filters++] = fs->chars;
        }
    } else if (String* fs = item_as_string(filter_it)) {
        filters_buf[n_filters++] = fs->chars;
    }
    if (n_filters == 0) {
        *out_len = data_s->len;
        return data_s->chars;
    }

    char* decoded = pdf_decompress_stream(data_s->chars, data_s->len,
                                          filters_buf, n_filters, out_len);
    if (!decoded) {
        log_warn("pdf_postprocess: ToUnicode stream decompression failed "
                 "(filter=%s)", filters_buf[0]);
        return nullptr;
    }
    *needs_free = true;
    return decoded;
}

// Process a single Font dict: locate /ToUnicode, decompress, parse, attach.
static void process_font_dict(Input* input, MarkBuilder& builder, Map* font,
                              ObjTable* table) {
    if (!font) return;
    Item ty = map_lookup(font, "Type");
    String* ts = item_as_string(ty);
    if (!ts || !str_eq(ts, "Font")) return;

    Item tu = map_lookup(font, "ToUnicode");
    if (tu.item == ITEM_NULL) return;
    Item resolved = resolve_ref_deep(tu, table);
    Map* stream_map = item_as_map(resolved);
    if (!stream_map || !map_has_type(stream_map, "stream")) {
        log_debug("pdf_postprocess: ToUnicode does not resolve to a stream");
        return;
    }

    size_t dlen = 0;
    bool needs_free = false;
    const char* dbuf = get_decompressed_stream(stream_map, &dlen, &needs_free);
    if (!dbuf || dlen == 0) {
        if (needs_free && dbuf) mem_free((void*)dbuf);
        return;
    }

    Map* tu_map = map_pooled(input->pool);
    if (!tu_map) {
        if (needs_free) mem_free((void*)dbuf);
        return;
    }

    int sections = parse_to_unicode_cmap(dbuf, dlen, input, builder, tu_map);
    if (needs_free) mem_free((void*)dbuf);

    if (sections > 0) {
        builder.putToMap(font, builder.createString("to_unicode"),
                         {.item = (uint64_t)tu_map});
        log_info("pdf_postprocess: attached ToUnicode map to font "
                 "(%d CMap sections parsed)", sections);
    } else {
        log_debug("pdf_postprocess: ToUnicode CMap had no parseable sections");
    }
}

// Walk every indirect_object content; if it's a Map with /Type=Font, process.
// Also descend into Resources/Font dictionaries (some PDFs only reference
// fonts inline rather than as standalone indirect objects).
static void walk_fonts(Input* input, MarkBuilder& builder,
                       Array* objects, ObjTable* table) {
    if (!objects) return;
    int processed = 0;
    for (int i = 0; i < objects->length; i++) {
        Map* iobj = item_as_map(objects->items[i]);
        if (!iobj || !map_has_type(iobj, "indirect_object")) continue;
        Item content = map_lookup(iobj, "content");
        Map* m = item_as_map(content);
        if (!m) continue;

        // Direct Font object?
        Item ty = map_lookup(m, "Type");
        String* ts = item_as_string(ty);
        if (ts && str_eq(ts, "Font")) {
            process_font_dict(input, builder, m, table);
            processed++;
            continue;
        }

        // Resources dict containing /Font sub-dict?
        Item font_dict_it = map_lookup(m, "Font");
        Map* font_dict = item_as_map(resolve_ref_deep(font_dict_it, table));
        if (!font_dict) continue;
        // Iterate fields of font_dict via its TypeMap shape — but we don't
        // have a public iterator. Walk known font slot names heuristically
        // is brittle; instead, the indirect-object pass above covers the
        // common case (each font is its own indirect object). Inline fonts
        // are uncommon in practice and can be added in a follow-up.
    }
    log_info("pdf_postprocess: processed %d Font dicts for ToUnicode",
             processed);
}

// Mutate an existing String slot in a Map in-place. Walks the Map's
// ShapeEntry list, finds the slot named `field`, and overwrites the
// String* there. Used by the stream-decompress pass to swap out the
// raw `data` payload for its decoded form without invalidating any
// other consumer of the Map.
static void replace_string_field(Map* m, const char* field, String* new_val) {
    if (!m || !m->type || !m->data || !field || !new_val) return;
    TypeMap* mt = (TypeMap*)m->type;
    size_t flen = strlen(field);
    for (ShapeEntry* se = mt->shape; se; se = se->next) {
        if (se->name && (size_t)se->name->length == flen
            && memcmp(se->name->str, field, flen) == 0) {
            *(String**)((char*)m->data + se->byte_offset) = new_val;
            return;
        }
    }
}

// Pass 3: walk every indirect_object content; if it's a stream Map with
// a Filter, decompress in place. After this pass, the `data` field of
// every stream object holds the plain (post-filter) byte payload, so
// callers (e.g. content-stream tokenizer) need not re-implement filter
// chains.
static void decompress_streams(Input* input, Array* objects) {    if (!objects) return;
    int decoded_count = 0;
    for (int i = 0; i < objects->length; i++) {
        Map* iobj = item_as_map(objects->items[i]);
        if (!iobj || !map_has_type(iobj, "indirect_object")) continue;
        Item content = map_lookup(iobj, "content");
        Map* sm = item_as_map(content);
        if (!sm || !map_has_type(sm, "stream")) continue;
        // Skip streams without a Filter — `data` is already plain.
        Item dict_it = map_lookup(sm, "dictionary");
        Map* dict = item_as_map(dict_it);
        if (!dict) continue;
        Item filter_it = map_lookup(dict, "Filter");
        if (filter_it.item == ITEM_NULL) continue;

        size_t out_len = 0;
        bool needs_free = false;
        const char* dec = get_decompressed_stream(sm, &out_len, &needs_free);
        if (!dec) continue;

        // Pool-allocate the new String so it survives with the input pool.
        String* new_data = (String*)pool_calloc(input->pool, sizeof(String) + out_len + 1);
        if (!new_data) {
            if (needs_free) mem_free((void*)dec);
            continue;
        }
        memcpy(new_data->chars, dec, out_len);
        new_data->chars[out_len] = '\0';
        new_data->len = (uint32_t)out_len;
        new_data->is_ascii = 1;
        // Buffers from pdf_decompress_stream are mem_alloc'd, not malloc'd —
        // must use mem_free() to avoid corrupting the memtrack tinfo header.
        if (needs_free) mem_free((void*)dec);

        replace_string_field(sm, "data", new_data);
        decoded_count++;
    }
    log_info("pdf_postprocess: decompressed %d stream(s)", decoded_count);
}

// ---------------------------------------------------------------------------
// Pass 4: Image XObject → data:image/png;base64,... URI
// ---------------------------------------------------------------------------
//
// Walks every indirect_object content; if it's an Image XObject stream
// (Type=XObject, Subtype=Image) with a supported colour space, encodes
// the pixels (combined with the SMask alpha channel when present) as a
// PNG and stores the resulting `data:image/png;base64,...` string under
// a new `data_uri` field on the stream Map. The Lambda PDF package
// reads this field and emits a regular SVG <image href="data:..."/>
// element so downstream renderers (Radiant, browsers) can display the
// image without needing to know about PDF's `img:N` handle convention.
//
// Supported in this pass:
//   - /Filter /FlateDecode (already inflated by Pass 3)
//   - /BitsPerComponent 8
//   - /ColorSpace DeviceGray, DeviceRGB, or ["ICCBased", N-component]
//     (3-component ICC treated as DeviceRGB, 1-component as DeviceGray)
//   - Optional 8-bit /SMask alpha
// JPEG-compressed images (/DCTDecode) get the original (pre-decompress)
// bytes wrapped as data:image/jpeg;base64,... — but only when the
// parser preserved the encoded bytes (currently no, so JPEG falls into
// the generic raw-pixel path). Out-of-scope formats are skipped silently.

// Determine the number of colour components for a PDF colour space Item.
// Returns 1 (gray), 3 (RGB) or 0 (unsupported).
static int color_space_ncomp(Item cs, ObjTable* table) {
    String* s = item_as_string(cs);
    if (s) {
        if (str_eq(s, "DeviceGray") || str_eq(s, "G")) return 1;
        if (str_eq(s, "DeviceRGB")  || str_eq(s, "RGB")) return 3;
        return 0;  // DeviceCMYK and others not handled here
    }
    Array* arr = item_as_array(cs);
    if (!arr || arr->length < 1) return 0;
    String* head = item_as_string(arr->items[0]);
    if (!head) return 0;
    if (str_eq(head, "ICCBased") && arr->length >= 2) {
        // ["ICCBased", indirect_ref-to-stream]. The stream's dictionary
        // has /N (number of components).
        Item stream_it = resolve_ref_deep(arr->items[1], table);
        Map* sm = item_as_map(stream_it);
        if (!sm) return 0;
        Item dict_it = map_lookup(sm, "dictionary");
        Map* dict = item_as_map(dict_it);
        if (!dict) return 0;
        int n = item_as_int(map_lookup(dict, "N"), 0);
        if (n == 1 || n == 3) return n;
        return 0;
    }
    if (str_eq(head, "DeviceGray") || str_eq(head, "G")) return 1;
    if (str_eq(head, "DeviceRGB")  || str_eq(head, "RGB")) return 3;
    // CalGray, CalRGB, Lab, Indexed, Pattern, Separation, DeviceN — skip.
    return 0;
}

// Growable byte buffer for PNG assembly. Backed by malloc; explicit free.
struct ByteBuf {
    uint8_t* data;
    size_t   len;
    size_t   cap;
};

static bool bb_init(ByteBuf* b, size_t cap) {
    b->data = (uint8_t*)malloc(cap > 0 ? cap : 1);
    b->len = 0;
    b->cap = b->data ? cap : 0;
    return b->data != nullptr;
}

static void bb_free(ByteBuf* b) {
    if (b->data) free(b->data);
    b->data = nullptr; b->len = 0; b->cap = 0;
}

static bool bb_reserve(ByteBuf* b, size_t want) {
    if (b->len + want <= b->cap) return true;
    size_t newcap = b->cap ? b->cap : 64;
    while (newcap < b->len + want) newcap *= 2;
    uint8_t* p = (uint8_t*)realloc(b->data, newcap);
    if (!p) return false;
    b->data = p; b->cap = newcap;
    return true;
}

static bool bb_append(ByteBuf* b, const void* src, size_t n) {
    if (!bb_reserve(b, n)) return false;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return true;
}

static void bb_u32be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t) v;
}

// Append a PNG chunk (length, type, data, CRC32). type must be 4 ASCII bytes.
static bool png_chunk(ByteBuf* out, const char type[4],
                      const uint8_t* data, size_t dlen) {
    uint8_t hdr[8];
    bb_u32be(hdr, (uint32_t)dlen);
    memcpy(hdr + 4, type, 4);
    if (!bb_append(out, hdr, 8)) return false;
    if (dlen && !bb_append(out, data, dlen)) return false;
    // CRC covers type + data
    uLong crc = crc32(0L, (const Bytef*)hdr + 4, 4);
    if (dlen) crc = crc32(crc, (const Bytef*)data, (uInt)dlen);
    uint8_t crcb[4];
    bb_u32be(crcb, (uint32_t)crc);
    return bb_append(out, crcb, 4);
}

// Encode RGBA pixels (width*height*4 bytes, top-down, 8-bit per channel)
// to a PNG byte stream. Returns malloc'd buffer in *out_data and length
// in *out_len. Caller must free(). Returns false on allocation/zlib errors.
static bool encode_png_rgba(const uint8_t* pixels, int width, int height,
                            uint8_t** out_data, size_t* out_len) {
    if (!pixels || width <= 0 || height <= 0) return false;
    ByteBuf png; bb_init(&png, 4096);

    // PNG signature
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    if (!bb_append(&png, sig, 8)) { bb_free(&png); return false; }

    // IHDR
    uint8_t ihdr[13];
    bb_u32be(ihdr,     (uint32_t)width);
    bb_u32be(ihdr + 4, (uint32_t)height);
    ihdr[8]  = 8;   // bit depth
    ihdr[9]  = 6;   // colour type: RGBA
    ihdr[10] = 0;   // compression
    ihdr[11] = 0;   // filter method
    ihdr[12] = 0;   // interlace
    if (!png_chunk(&png, "IHDR", ihdr, sizeof(ihdr))) { bb_free(&png); return false; }

    // Build raw scanlines: one filter byte (None=0) per row, then 4*width
    // bytes of pixel data.
    size_t row_bytes = (size_t)width * 4;
    size_t raw_len = (row_bytes + 1) * (size_t)height;
    uint8_t* raw = (uint8_t*)malloc(raw_len);
    if (!raw) { bb_free(&png); return false; }
    for (int y = 0; y < height; y++) {
        raw[y * (row_bytes + 1)] = 0;  // no filter
        memcpy(raw + y * (row_bytes + 1) + 1,
               pixels + (size_t)y * row_bytes, row_bytes);
    }

    // Deflate (zlib wrapper) into IDAT
    uLongf zlen = compressBound((uLong)raw_len);
    uint8_t* zbuf = (uint8_t*)malloc(zlen);
    if (!zbuf) { free(raw); bb_free(&png); return false; }
    int zr = compress2(zbuf, &zlen, raw, (uLong)raw_len, Z_DEFAULT_COMPRESSION);
    free(raw);
    if (zr != Z_OK) { free(zbuf); bb_free(&png); return false; }
    bool ok = png_chunk(&png, "IDAT", zbuf, (size_t)zlen);
    free(zbuf);
    if (!ok) { bb_free(&png); return false; }

    // IEND
    if (!png_chunk(&png, "IEND", nullptr, 0)) { bb_free(&png); return false; }

    *out_data = png.data;  // transfer ownership
    *out_len = png.len;
    return true;
}

// Base64 encoder (no line breaks) — RFC 4648. Returns malloc'd
// nul-terminated string; caller frees().
static char* base64_alloc(const uint8_t* src, size_t n) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((n + 2) / 3) * 4;
    char* out = (char*)malloc(out_len + 1);
    if (!out) return nullptr;
    size_t i = 0, j = 0;
    while (i + 2 < n) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i+1] << 8) | src[i+2];
        out[j++] = tbl[(v >> 18) & 0x3F];
        out[j++] = tbl[(v >> 12) & 0x3F];
        out[j++] = tbl[(v >>  6) & 0x3F];
        out[j++] = tbl[ v        & 0x3F];
        i += 3;
    }
    if (i < n) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < n) v |= (uint32_t)src[i+1] << 8;
        out[j++] = tbl[(v >> 18) & 0x3F];
        out[j++] = tbl[(v >> 12) & 0x3F];
        out[j++] = (i + 1 < n) ? tbl[(v >> 6) & 0x3F] : '=';
        out[j++] = '=';
    }
    out[j] = '\0';
    return out;
}

// Build the RGBA buffer for one image and convert it to a
// data:image/png;base64,... String*. Returns nullptr on failure /
// unsupported configurations.
static String* image_to_data_uri(Input* input, Map* stream_map,
                                 ObjTable* table) {
    Item dict_it = map_lookup(stream_map, "dictionary");
    Map* dict = item_as_map(dict_it);
    if (!dict) return nullptr;

    // Subtype must be Image
    String* sub = item_as_string(map_lookup(dict, "Subtype"));
    if (!sub || !str_eq(sub, "Image")) return nullptr;

    int w   = item_as_int(map_lookup(dict, "Width"),  0);
    int h   = item_as_int(map_lookup(dict, "Height"), 0);
    int bpc = item_as_int(map_lookup(dict, "BitsPerComponent"), 8);
    if (w <= 0 || h <= 0 || bpc != 8) return nullptr;

    int ncomp = color_space_ncomp(map_lookup(dict, "ColorSpace"), table);
    if (ncomp != 1 && ncomp != 3) return nullptr;

    // Pixel data must be plain (Pass 3 should have decompressed it).
    Item data_it = map_lookup(stream_map, "data");
    String* pix = item_as_string(data_it);
    if (!pix) return nullptr;
    size_t expect = (size_t)w * (size_t)h * (size_t)ncomp;
    if ((size_t)pix->len < expect) return nullptr;
    const uint8_t* rgb = (const uint8_t*)pix->chars;

    // Optional alpha from /SMask.
    const uint8_t* alpha = nullptr;
    Item smask_it = map_lookup(dict, "SMask");
    if (smask_it.item != ITEM_NULL) {
        Item smr = resolve_ref_deep(smask_it, table);
        Map* sms = item_as_map(smr);
        if (sms && map_has_type(sms, "stream")) {
            Item sd_it = map_lookup(sms, "dictionary");
            Map* sd = item_as_map(sd_it);
            int sw  = sd ? item_as_int(map_lookup(sd, "Width"),  0) : 0;
            int sh  = sd ? item_as_int(map_lookup(sd, "Height"), 0) : 0;
            int sbpc = sd ? item_as_int(map_lookup(sd, "BitsPerComponent"), 8) : 0;
            String* sp = item_as_string(map_lookup(sms, "data"));
            if (sw == w && sh == h && sbpc == 8 && sp &&
                (size_t)sp->len >= (size_t)w * (size_t)h) {
                alpha = (const uint8_t*)sp->chars;
            }
        }
    }

    // Compose RGBA top-down (PDF image data is top-down, no y-flip needed
    // in the pixel buffer — the SVG <image> sits inside the page-level
    // y-flip group).
    size_t rgba_len = (size_t)w * (size_t)h * 4;
    uint8_t* rgba = (uint8_t*)malloc(rgba_len);
    if (!rgba) return nullptr;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t pi = (size_t)y * w + x;
            size_t di = pi * 4;
            if (ncomp == 1) {
                uint8_t g = rgb[pi];
                rgba[di+0] = g; rgba[di+1] = g; rgba[di+2] = g;
            } else {
                rgba[di+0] = rgb[pi*3+0];
                rgba[di+1] = rgb[pi*3+1];
                rgba[di+2] = rgb[pi*3+2];
            }
            rgba[di+3] = alpha ? alpha[pi] : 255;
        }
    }

    uint8_t* png_buf = nullptr; size_t png_len = 0;
    bool ok = encode_png_rgba(rgba, w, h, &png_buf, &png_len);
    free(rgba);
    if (!ok) return nullptr;

    char* b64 = base64_alloc(png_buf, png_len);
    free(png_buf);
    if (!b64) return nullptr;

    static const char prefix[] = "data:image/png;base64,";
    size_t plen = sizeof(prefix) - 1;
    size_t blen = strlen(b64);
    size_t total = plen + blen;
    String* out = (String*)pool_calloc(input->pool, sizeof(String) + total + 1);
    if (!out) { free(b64); return nullptr; }
    memcpy(out->chars, prefix, plen);
    memcpy(out->chars + plen, b64, blen);
    out->chars[total] = '\0';
    out->len = (uint32_t)total;
    out->is_ascii = 1;
    free(b64);
    return out;
}

// Pass 4 entry: walk objects, encode images that look supported.
static void encode_image_xobjects(Input* input, MarkBuilder& builder,
                                  Array* objects, ObjTable* table) {
    if (!objects) return;
    int encoded = 0;
    for (int i = 0; i < objects->length; i++) {
        Map* iobj = item_as_map(objects->items[i]);
        if (!iobj || !map_has_type(iobj, "indirect_object")) continue;
        Item content = map_lookup(iobj, "content");
        Map* sm = item_as_map(content);
        if (!sm || !map_has_type(sm, "stream")) continue;

        Item dict_it = map_lookup(sm, "dictionary");
        Map* dict = item_as_map(dict_it);
        if (!dict) continue;
        // Only XObject Images
        String* tystr  = item_as_string(map_lookup(dict, "Type"));
        String* substr = item_as_string(map_lookup(dict, "Subtype"));
        if (!tystr || !str_eq(tystr, "XObject")) continue;
        if (!substr || !str_eq(substr, "Image")) continue;
        // Skip SMask streams themselves — they're consumed alongside their owners.

        String* uri = image_to_data_uri(input, sm, table);
        if (!uri) continue;
        builder.putToMap(sm, builder.createString("data_uri"),
                         {.item = s2it(uri)});
        encoded++;
    }
    log_info("pdf_postprocess: encoded %d Image XObject(s) to PNG data URI", encoded);
}

// Build a `data:font/<fmt>;base64,...` URI from a stream Map's `data`
// field. Returns nullptr on failure. Caller must already have run Pass 3
// so the stream `data` is the post-filter (raw) font program.
static String* font_stream_to_data_uri(Input* input, Map* sm,
                                       const char* mime, const char* /*fmt*/) {
    if (!sm) return nullptr;
    String* data = item_as_string(map_lookup(sm, "data"));
    if (!data || data->len == 0) return nullptr;
    char* b64 = base64_alloc((const uint8_t*)data->chars, data->len);
    if (!b64) return nullptr;
    size_t b64_len = strlen(b64);
    size_t prefix_len = strlen("data:") + strlen(mime) + strlen(";base64,");
    size_t total = prefix_len + b64_len;
    String* out = (String*)pool_calloc(input->pool, sizeof(String) + total + 1);
    if (!out) { free(b64); return nullptr; }
    int n = snprintf(out->chars, total + 1, "data:%s;base64,%s", mime, b64);
    if (n < 0 || (size_t)n != total) { free(b64); return nullptr; }
    out->len = (uint32_t)total;
    out->is_ascii = 1;
    free(b64);
    return out;
}

// Pass 5 entry: walk objects, encode embedded font programs.
// Looks for FontDescriptor dicts and, if they reference a FontFile/
// FontFile2/FontFile3 stream, attaches `font_data_uri` + `font_format`
// to the descriptor. Lambda-side renderers can then emit @font-face.
static void encode_embedded_fonts(Input* input, MarkBuilder& builder,
                                  Array* objects, ObjTable* table) {
    if (!objects) return;
    int encoded = 0;
    for (int i = 0; i < objects->length; i++) {
        Map* iobj = item_as_map(objects->items[i]);
        if (!iobj || !map_has_type(iobj, "indirect_object")) continue;
        Item content = map_lookup(iobj, "content");
        Map* fd = item_as_map(content);
        if (!fd) continue;
        String* ts = item_as_string(map_lookup(fd, "Type"));
        if (!ts || !str_eq(ts, "FontDescriptor")) continue;

        // try FontFile2 (TrueType) → "truetype"
        // try FontFile3 (OpenType / CFF) → "opentype"
        // try FontFile  (Type1)         → "embedded-opentype" fallback,
        //                                  not directly usable in browsers
        const char* mime = nullptr;
        const char* fmt  = nullptr;
        Item ff_it = map_lookup(fd, "FontFile2");
        if (ff_it.item != ITEM_NULL) {
            mime = "font/ttf"; fmt = "truetype";
        } else {
            ff_it = map_lookup(fd, "FontFile3");
            if (ff_it.item != ITEM_NULL) {
                // FontFile3 Subtype tells us OpenType vs CIDFontType0C
                Map* sm_peek = item_as_map(resolve_ref_deep(ff_it, table));
                if (sm_peek) {
                    Map* d2 = item_as_map(map_lookup(sm_peek, "dictionary"));
                    String* sub = d2 ? item_as_string(map_lookup(d2, "Subtype"))
                                     : nullptr;
                    if (sub && str_eq(sub, "OpenType")) {
                        mime = "font/otf"; fmt = "opentype";
                    } else {
                        // CFF/Type1C — browsers can't load these directly
                        continue;
                    }
                }
            } else {
                ff_it = map_lookup(fd, "FontFile");
                if (ff_it.item == ITEM_NULL) continue;
                // Type1 PFB — not directly usable; skip.
                continue;
            }
        }
        if (!mime) continue;

        Map* sm = item_as_map(resolve_ref_deep(ff_it, table));
        if (!sm || !map_has_type(sm, "stream")) continue;

        String* uri = font_stream_to_data_uri(input, sm, mime, fmt);
        if (!uri) continue;
        builder.putToMap(fd, builder.createString("font_data_uri"),
                         {.item = s2it(uri)});
        builder.putToMap(fd, builder.createString("font_format"),
                         {.item = s2it(builder.createString(fmt))});
        encoded++;
    }
    log_info("pdf_postprocess: encoded %d embedded font(s) to data URI",
             encoded);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point.
// ---------------------------------------------------------------------------

void pdf_postprocess(Input* input) {
    if (!input || input->root.item == ITEM_NULL ||
        input->root.item == ITEM_ERROR) {
        return;
    }
    Map* pdf_info = item_as_map(input->root);
    if (!pdf_info) return;

    Pool* pool = input->pool;
    g_post_pool = pool;

    Item objects_it = map_lookup(pdf_info, "objects");
    Array* objects = item_as_array(objects_it);

    ObjTable table = {nullptr, 0, 0};
    build_obj_table(objects, &table, pool);

    MarkBuilder builder(input);

    // Pass 1: page-tree flattening
    flatten_page_tree(input, builder, pdf_info, &table);

    // Pass 2: ToUnicode CMap parsing
    walk_fonts(input, builder, objects, &table);

    // Pass 3: stream decompression (FlateDecode, ASCII85Decode, etc.)
    decompress_streams(input, objects);

    // Pass 4: encode Image XObjects to PNG data URIs
    encode_image_xobjects(input, builder, objects, &table);

    // Pass 5: encode embedded font programs to data URIs
    encode_embedded_fonts(input, builder, objects, &table);

    g_post_pool = nullptr;
}
