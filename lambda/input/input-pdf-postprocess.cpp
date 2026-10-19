// PDF post-processing pass.
//
// Runs after the raw parser (input-pdf.cpp) has produced the indirect-object
// table. Performs two structural enrichments that consumers (notably the
// upcoming `lambda/pdf/` Lambda Script package) would otherwise have
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
#include "input-pdf-type1.hpp"
#include "input-pdf-image.hpp"
#include "../io/mark_builder.hpp"
#include "../core/mark_reader.hpp"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/base64.h"
#include "lib/byte_builder.h"
#include "lib/str.h"
#include "lib/strbuf.h"
#include "lib/utf.h"
#include "lib/string.h"
#include "lib/endian.h"
#include "lib/color.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

static inline Map* item_as_map(Item it) {
    return it.get_safe_map();
}

static inline Array* item_as_array(Item it) {
    return it.get_safe_array();
}

static inline String* item_as_string(Item it) {
    return it.get_safe_string();
}

// Compare a String*'s chars to a literal.
static inline bool str_eq(String* s, const char* lit) {
    if (!s || !lit) return false;
    size_t n = strlen(lit);
    return s->len == n && memcmp(s->chars, lit, n) == 0;
}

static inline String* item_as_pdf_name(Item it) {
    if (String* s = item_as_string(it)) return s;
    Map* m = item_as_map(it);
    if (!m) return nullptr;
    String* kind = item_as_string(map_lookup(m, "kind"));
    if (!str_eq(kind, "name")) return nullptr;
    return item_as_string(map_lookup(m, "value"));
}

// Read a numeric Item as int (handles inline/boxed floats and packed ints).
static inline int item_as_int(Item it, int dflt) {
    TypeId t = get_type_id(it);
    if (t == LMD_TYPE_INT) {
        return (int)lambda_int_item_to_i64(it);
    }
    if (t == LMD_TYPE_FLOAT) {
        // PDF object references are stored as numeric Items; self-tagged
        // floats do not have a boxed payload to dereference.
        return (int)it.get_double();
    }
    return dflt;
}

// Quick "is this Map our wrapper of type X" check.
static bool map_has_type(Map* m, const char* type_str) {
    Item ty = map_lookup(m, "type");
    String* s = item_as_string(ty);
    return str_eq(s, type_str);
}

static Map* stream_dictionary(Map* m) {
    if (!m || !map_has_type(m, "stream")) return nullptr;
    return item_as_map(map_lookup(m, "dictionary"));
}

static Map* pdf_dictionary_for_item(Item it) {
    Map* m = item_as_map(it);
    if (!m) return nullptr;
    Map* dict = stream_dictionary(m);
    return dict ? dict : m;
}

static bool pdf_dict_has_type(Map* m, const char* type_str) {
    return str_eq(item_as_pdf_name(map_lookup(m, "Type")), type_str);
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
        int num = item_as_int(num_it, -1);
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
    int num = item_as_int(num_it, -1);
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

static Map* find_catalog_from_objects(ObjTable* table) {
    if (!table) return nullptr;
    for (int i = 0; i < table->count; i++) {
        Map* dict = pdf_dictionary_for_item(table->entries[i].content);
        if (pdf_dict_has_type(dict, "Catalog")) return dict;
    }
    return nullptr;
}

static Map* find_catalog_from_xref_stream(ObjTable* table) {
    if (!table) return nullptr;
    for (int i = 0; i < table->count; i++) {
        Map* dict = pdf_dictionary_for_item(table->entries[i].content);
        if (!pdf_dict_has_type(dict, "XRef")) continue;
        Item root = resolve_ref_deep(map_lookup(dict, "Root"), table);
        Map* catalog = item_as_map(root);
        if (pdf_dict_has_type(catalog, "Catalog")) return catalog;
    }
    return nullptr;
}

static Map* find_catalog(MarkBuilder& builder, Map* pdf_info, ObjTable* table) {
    Item trailer_it = map_lookup(pdf_info, "trailer");
    Map* trailer_wrap = item_as_map(trailer_it);
    if (trailer_wrap) {
        Item dict_it = map_lookup(trailer_wrap, "dictionary");
        Map* trailer_dict = item_as_map(dict_it);
        if (trailer_dict) {
            Item root_it = resolve_ref_deep(map_lookup(trailer_dict, "Root"), table);
            Map* catalog = item_as_map(root_it);
            if (catalog) return catalog;
        }
    }

    Map* catalog = find_catalog_from_xref_stream(table);
    if (catalog) {
        log_info("pdf_postprocess: found catalog through XRef stream /Root");
    } else {
        catalog = find_catalog_from_objects(table);
        if (catalog) log_info("pdf_postprocess: found catalog by object scan");
    }
    if (catalog) {
        builder.putToMap(lam::gc_borrow(pdf_info), builder.createString("catalog"),
                         {.item = (uint64_t)catalog});
    } else if (!trailer_wrap) {
        log_info("pdf_postprocess: no trailer or catalog found");
    }
    return catalog;
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
    builder.putToMap(lam::gc_borrow(flat), builder.createString("type"),
                     {.item = s2it(builder.createString("page"))});
    builder.putToMap(lam::gc_borrow(flat), builder.createString("dict"),
                     {.item = (uint64_t)node});
    if (resources.item != ITEM_NULL) {
        Item r = resolve_ref_deep(resources, table);
        builder.putToMap(lam::gc_borrow(flat), builder.createString("resources"), r);
    }
    if (mediabox.item != ITEM_NULL) {
        builder.putToMap(lam::gc_borrow(flat), builder.createString("media_box"), mediabox);
    }
    if (cropbox.item != ITEM_NULL) {
        builder.putToMap(lam::gc_borrow(flat), builder.createString("crop_box"), cropbox);
    }
    if (rotate.item != ITEM_NULL) {
        builder.putToMap(lam::gc_borrow(flat), builder.createString("rotate"), rotate);
    }
    // Page index assigned by caller via array position.
    array_append(out_pages, {.item = (uint64_t)flat}, input->pool, input->arena);
}

static void flatten_page_objects(Input* input, MarkBuilder& builder,
                                 ObjTable* table, Array* out_pages) {
    if (!table || !out_pages) return;
    for (int i = 0; i < table->count; i++) {
        Map* dict = pdf_dictionary_for_item(table->entries[i].content);
        if (!pdf_dict_has_type(dict, "Page")) continue;
        flatten_page_node(input, builder, dict,
                          {.item = ITEM_NULL}, {.item = ITEM_NULL},
                          {.item = ITEM_NULL}, {.item = ITEM_NULL},
                          table, out_pages, 0);
    }
}

static void flatten_page_tree(Input* input, MarkBuilder& builder,
                              Map* pdf_info, ObjTable* table) {
    Map* catalog = find_catalog(builder, pdf_info, table);
    if (!catalog) {
        log_info("pdf_postprocess: no resolvable /Root catalog, skipping page-tree flatten");
        return;
    }

    Item pages_it = resolve_ref_deep(map_lookup(catalog, "Pages"), table);
    Map* pages_root = item_as_map(pages_it);
    Array* out = array_pooled(input->pool);
    if (!out) return;

    if (pages_root) {
        flatten_page_node(input, builder, pages_root,
                          {.item = ITEM_NULL}, {.item = ITEM_NULL},
                          {.item = ITEM_NULL}, {.item = ITEM_NULL},
                          table, out, 0);
    } else {
        log_info("pdf_postprocess: catalog has no resolvable /Pages, scanning page objects");
        flatten_page_objects(input, builder, table, out);
    }

    builder.putToMap(lam::gc_borrow(pdf_info), builder.createString("pages"),
                     {.item = (uint64_t)out});
    log_info("pdf_postprocess: flattened page tree into %d pages", out->length);
}

struct PdfLabelRange {
    int64_t index, start;
    char style;
    String* prefix;
    PdfLabelRange* next;
};

struct PdfLabelTree {
    Input* input;
    MarkBuilder* builder;
    ObjTable* objects;
    Array* pages;
    PdfLabelRange* first;
    PdfLabelRange* last;
    Map* ancestors[64];
    size_t nodes;
    const char* error;
};

static bool pdf_label_fail(PdfLabelTree* tree, const char* reason) {
    tree->error = reason; return false;
}

static bool pdf_label_integer(Item item, int64_t* value) {
    TypeId type = get_type_id(item);
    if (type == LMD_TYPE_INT) *value = lambda_int_item_to_i64(item);
    else if (type == LMD_TYPE_FLOAT) {
        // The PDF parser stores numeric objects as floats; the PDF integer domain is exact here.
        double number = item.get_double();
        if (!isfinite(number) || floor(number) != number || number < INT32_MIN || number > INT32_MAX) return false;
        *value = (int64_t)number;
    } else return false;
    return *value >= INT32_MIN && *value <= INT32_MAX;
}

// PDF text strings use PDFDocEncoding or a Unicode BOM, unlike content-stream glyph strings.
static String* pdf_label_prefix(PdfLabelTree* tree, Item item) {
    if (item.item == ITEM_NULL) return tree->builder->createString("");
    String* raw = item_as_string(item);
    if (!raw) { pdf_label_fail(tree, "page label prefix must be a text string"); return nullptr; }
    const uint8_t* bytes = (const uint8_t*)raw->chars;
    StrBuf* text = strbuf_new(); if (!text) return nullptr;
    bool valid = true;
    if (raw->len >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        valid = raw->len % 2 == 0;
        for (size_t i = 2; valid && i + 1 < raw->len; i += 2) {
            uint32_t cp = ((uint32_t)bytes[i] << 8) | bytes[i + 1];
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (i + 3 >= raw->len) { valid = false; break; }
                uint16_t low = ((uint16_t)bytes[i + 2] << 8) | bytes[i + 3];
                if (low < 0xDC00 || low > 0xDFFF) { valid = false; break; }
                cp = utf16_decode_pair((uint16_t)cp, low); i += 2;
            }
            if (!cp || !strbuf_append_utf8(text, cp)) valid = false;
        }
    } else if (raw->len >= 3 && !memcmp(bytes, "\xEF\xBB\xBF", 3)) {
        valid = utf8_valid(raw->chars + 3, raw->len - 3) && !memchr(bytes + 3, 0, raw->len - 3);
        if (valid) strbuf_append_str_n(text, raw->chars + 3, raw->len - 3);
    } else {
        static const uint16_t accents[] = {0x02D8, 0x02C7, 0x02C6, 0x02D9, 0x02DD, 0x02DB, 0x02DA, 0x02DC};
        static const uint16_t specials[] = {0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x0192, 0x2044,
            0x2039, 0x203A, 0x2212, 0x2030, 0x201E, 0x201C, 0x201D, 0x2018,
            0x2019, 0x201A, 0x2122, 0xFB01, 0xFB02, 0x0141, 0x0152, 0x0160,
            0x0178, 0x017D, 0x0131, 0x0142, 0x0153, 0x0161, 0x017E};
        for (size_t i = 0; valid && i < raw->len; i++) {
            uint32_t cp = bytes[i];
            if (!cp || cp == 0x7F || cp == 0x9F || cp == 0xAD) { valid = false; break; }
            if (cp >= 0x18 && cp <= 0x1F) cp = accents[cp - 0x18];
            else if (cp >= 0x80 && cp <= 0x9E) cp = specials[cp - 0x80];
            else if (cp == 0xA0) cp = 0x20AC;
            valid = strbuf_append_utf8(text, cp);
        }
    }
    String* result = valid && text->length <= 65536 ? tree->builder->createString(text->str, text->length) : nullptr;
    strbuf_free(text);
    if (!result) pdf_label_fail(tree, "page label prefix has invalid encoding, NUL or exceeds its byte budget");
    return result;
}

static bool pdf_label_range(PdfLabelTree* tree, Item key, Item value) {
    int64_t index = -1;
    if (!pdf_label_integer(key, &index) || index < 0 || index >= tree->pages->length ||
        (tree->last && index <= tree->last->index))
        return pdf_label_fail(tree, "page label indices must be ordered, unique and within the page sequence");
    Map* dict = item_as_map(resolve_ref_deep(value, tree->objects));
    if (!dict || map_has_type(dict, "indirect_ref")) return pdf_label_fail(tree, "page label requires a resolved dictionary");
    Item kind = map_lookup(dict, "Type");
    if (kind.item != ITEM_NULL && !str_eq(item_as_pdf_name(kind), "PageLabel"))
        return pdf_label_fail(tree, "page label dictionary has an invalid Type");
    Item raw_style = map_lookup(dict, "S"); String* style = item_as_pdf_name(raw_style);
    if (raw_style.item != ITEM_NULL && (!style || style->len != 1 || !strchr("DRrAa", style->chars[0])))
        return pdf_label_fail(tree, "page label numbering style is unsupported");
    int64_t start = 1; Item raw_start = resolve_ref_deep(map_lookup(dict, "St"), tree->objects);
    if (raw_start.item != ITEM_NULL && (!pdf_label_integer(raw_start, &start) || start < 1))
        return pdf_label_fail(tree, "page label start must be a positive integer");
    String* prefix = pdf_label_prefix(tree, resolve_ref_deep(map_lookup(dict, "P"), tree->objects));
    if (!prefix) return false;
    PdfLabelRange* range = (PdfLabelRange*)pool_alloc(tree->input->pool, sizeof(PdfLabelRange));
    if (!range) return pdf_label_fail(tree, "page label allocation failed");
    *range = {index, start, style ? style->chars[0] : '\0', prefix, nullptr};
    if (tree->last) tree->last->next = range; else tree->first = range;
    tree->last = range; return true;
}

static bool pdf_label_tree(PdfLabelTree* tree, Item item, size_t depth) {
    if (depth >= 64 || ++tree->nodes > (size_t)tree->pages->length * 2 + 64)
        return pdf_label_fail(tree, "page label number tree exceeds its traversal budget");
    Map* node = item_as_map(resolve_ref_deep(item, tree->objects));
    if (!node || map_has_type(node, "indirect_ref")) return pdf_label_fail(tree, "page label number tree requires resolved nodes");
    for (size_t i = 0; i < depth; i++) if (tree->ancestors[i] == node)
        return pdf_label_fail(tree, "page label number tree contains a cycle");
    tree->ancestors[depth] = node;
    Item nums_item = map_lookup(node, "Nums"), kids_item = map_lookup(node, "Kids");
    if ((nums_item.item == ITEM_NULL) == (kids_item.item == ITEM_NULL))
        return pdf_label_fail(tree, "page label number tree requires either Nums or Kids");
    PdfLabelRange* previous = tree->last;
    if (nums_item.item != ITEM_NULL) {
        Array* nums = item_as_array(resolve_ref_deep(nums_item, tree->objects));
        if (!nums || !nums->length || nums->length % 2) return pdf_label_fail(tree, "page label Nums requires key/dictionary pairs");
        for (int i = 0; i < nums->length; i += 2)
            if (!pdf_label_range(tree, nums->items[i], nums->items[i + 1])) return false;
    } else {
        Array* kids = item_as_array(resolve_ref_deep(kids_item, tree->objects));
        if (!kids || !kids->length) return pdf_label_fail(tree, "page label Kids requires a nonempty node array");
        for (int i = 0; i < kids->length; i++) if (!pdf_label_tree(tree, kids->items[i], depth + 1)) return false;
    }
    Item limits_item = map_lookup(node, "Limits");
    if (depth || limits_item.item != ITEM_NULL) {
        Array* limits = item_as_array(resolve_ref_deep(limits_item, tree->objects));
        int64_t low = -1, high = -1;
        PdfLabelRange* first = previous ? previous->next : tree->first;
        if (!limits || limits->length != 2 || !pdf_label_integer(limits->items[0], &low) ||
            !pdf_label_integer(limits->items[1], &high) || !first || low != first->index || high != tree->last->index)
            return pdf_label_fail(tree, "page label number tree Limits must match its descendant keys");
    }
    return true;
}

static String* pdf_label_text(PdfLabelTree* tree, const PdfLabelRange* range, int64_t index) {
    uint64_t number = (uint64_t)range->start + (uint64_t)(index - range->index);
    size_t digits = range->style == 'A' || range->style == 'a' ? (number - 1) / 26 + 1 :
        range->style == 'R' || range->style == 'r' ? number / 1000 + 16 : 24;
    size_t available = 65536 - range->prefix->len;
    if ((range->style == 'A' || range->style == 'a') && digits > available) {
        pdf_label_fail(tree, "page label exceeds its byte budget"); return nullptr;
    }
    if (digits > available) digits = available;
    StrBuf* text = strbuf_new_cap(range->prefix->len + digits + 1);
    if (!text) return nullptr;
    strbuf_append_str_n(text, range->prefix->chars, range->prefix->len);
    if (range->style == 'D') strbuf_append_uint64(text, number);
    else if (range->style == 'A' || range->style == 'a')
        strbuf_append_char_n(text, (range->style == 'A' ? 'A' : 'a') + (number - 1) % 26, digits);
    else if (range->style == 'R' || range->style == 'r') {
        size_t length = str_format_roman(number, text->str + text->length,
            text->capacity - text->length, range->style == 'R');
        if (!length) { strbuf_free(text); pdf_label_fail(tree, "page label exceeds its byte budget"); return nullptr; }
        text->length += length;
    }
    String* result = text->length <= 65536 ? tree->builder->createString(text->str, text->length) : nullptr;
    if (!result) pdf_label_fail(tree, "page label exceeds its byte budget or allocation failed");
    strbuf_free(text); return result;
}

static void resolve_page_labels(Input* input, MarkBuilder& builder, Map* pdf_info, ObjTable* objects) {
    Map* catalog = find_catalog(builder, pdf_info, objects);
    Item labels = map_lookup(catalog, "PageLabels");
    if (labels.item == ITEM_NULL) return;
    Array* pages = item_as_array(map_lookup(pdf_info, "pages"));
    if (!pages) return;
    PdfLabelTree tree = {input, &builder, objects, pages, nullptr, nullptr, {}, 0, nullptr};
    bool valid = pdf_label_tree(&tree, labels, 0);
    if (valid && (!tree.first || tree.first->index != 0)) valid = pdf_label_fail(&tree, "page label number tree must begin at page index zero");
    Array* texts = array_pooled(input->pool);
    for (const PdfLabelRange* range = tree.first; valid && range; range = range->next) {
        int64_t end = range->next ? range->next->index : pages->length;
        for (int64_t i = range->index; valid && i < end; i++) {
            String* text = pdf_label_text(&tree, range, i);
            if (!text || !texts) valid = pdf_label_fail(&tree, tree.error ? tree.error : "page label allocation failed");
            else array_append(texts, {.item = s2it(text)}, input->pool, input->arena);
        }
    }
    if (!valid) {
        builder.putToMap(lam::gc_borrow(pdf_info), builder.createString("page_labels_error"), builder.createStringItem(tree.error));
        log_warn("pdf_postprocess: invalid page labels: %s", tree.error); return;
    }
    // Publish only a complete label set; continuous consumers can retain their tolerant intake.
    for (int i = 0; i < pages->length; i++)
        builder.putToMap(lam::gc_borrow(item_as_map(pages->items[i])), builder.createString("label"), texts->items[i]);
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
        int d = str_hex_val(c);
        if (d < 0) {
            if (str_is_space(c)) continue;
            return false;
        }
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
    while (s < end && (str_is_space(*s) || *s == '%')) {
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
            if (after >= end || str_is_space(*after) ||
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
    builder.putToMap(lam::gc_borrow(out), key, {.item = s2it(val)});
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
        int d = str_hex_val(c);
        if (d < 0) {
            if (str_is_space(c)) continue;
            return 0;
        }
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
            String* fs = item_as_pdf_name(fa->items[i]);
            if (fs) filters_buf[n_filters++] = fs->chars;
        }
    } else if (String* fs = item_as_pdf_name(filter_it)) {
        filters_buf[n_filters++] = fs->chars;
    }
    if (n_filters == 0) {
        *out_len = data_s->len;
        return data_s->chars;
    }

    PDFDecodeParams params[8];
    Item parms = map_lookup(dict, "DecodeParms");
    Array* array = item_as_array(parms);
    for (int i = 0; i < n_filters; i++) {
        pdf_decode_params_init(params + i);
        Map* values = item_as_map(array && i < array->length ? array->items[i] : parms);
        if (!values) continue;
        params[i].predictor = item_as_int(map_lookup(values, "Predictor"), 1);
        params[i].colors = item_as_int(map_lookup(values, "Colors"), 1);
        params[i].bits = item_as_int(map_lookup(values, "BitsPerComponent"), 8);
        params[i].columns = item_as_int(map_lookup(values, "Columns"), 1);
        params[i].early_change = item_as_int(map_lookup(values, "EarlyChange"), 1);
    }
    char* decoded = pdf_decompress_stream_with_params(data_s->chars, data_s->len,
        filters_buf, n_filters, params, out_len);
    if (!decoded) {
        log_warn("pdf_postprocess: ToUnicode stream decompression failed "
                 "(filter=%s)", filters_buf[0]);
        return nullptr;
    }
    *needs_free = true;
    return decoded;
}

static bool stream_data_looks_cmap(String* data_s) {
    if (!data_s || data_s->len == 0) return false;
    const char* start = data_s->chars;
    const char* end = data_s->chars + data_s->len;
    return find_keyword(start, end, "beginbfchar") != nullptr ||
           find_keyword(start, end, "beginbfrange") != nullptr ||
           find_keyword(start, end, "begincmap") != nullptr;
}

// Process a single Font dict: locate /ToUnicode, decompress, parse, attach.
static void process_font_dict(Input* input, MarkBuilder& builder, Map* font,
                              ObjTable* table) {
    if (!font) return;
    Item ty = map_lookup(font, "Type");
    String* ts = item_as_string(ty);
    if (!ts || !str_eq(ts, "Font")) return;

    // Type 1 encodings address outlines, not Unicode; keep the embedded shapes for SVG painting.
    if (str_eq(item_as_string(map_lookup(font, "Subtype")), "Type1")) {
        Map* descriptor = item_as_map(resolve_ref_deep(map_lookup(font, "FontDescriptor"), table));
        Map* program = descriptor ? item_as_map(resolve_ref_deep(map_lookup(descriptor, "FontFile"), table)) : nullptr;
        if (program && map_has_type(program, "stream")) {
            Map* dict = item_as_map(map_lookup(program, "dictionary"));
            Item paths = pdf_type1_glyph_paths(input, item_as_string(map_lookup(program, "data")),
                item_as_int(map_lookup(dict, "Length1"), 0), resolve_ref_deep(map_lookup(font, "Encoding"), table));
            if (paths.item != ITEM_NULL) builder.putToMap(lam::gc_borrow(font), builder.createString("glyph_paths"), paths);
        }
    }

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
    String* raw_data = item_as_string(map_lookup(stream_map, "data"));
    const char* dbuf = nullptr;
    if (stream_data_looks_cmap(raw_data)) {
        dbuf = raw_data->chars;
        dlen = raw_data->len;
    } else {
        dbuf = get_decompressed_stream(stream_map, &dlen, &needs_free);
    }
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
        builder.putToMap(lam::gc_borrow(font), builder.createString("to_unicode"),
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
#ifndef NDEBUG
    int processed = 0;
#endif
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
#ifndef NDEBUG
            processed++;
#endif
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
    FOR_EACH_MAP_FIELD(mt, se) {
        if (se->name && (size_t)se->name->length == flen
            && memcmp(se->name->str, field, flen) == 0) {
            *(String**)((char*)m->data + se->byte_offset) = new_val;
            return;
        }
    }
}

static bool is_image_stream_dict(Map* dict) {
    if (!dict) return false;
    String* type = item_as_pdf_name(map_lookup(dict, "Type"));
    String* subtype = item_as_pdf_name(map_lookup(dict, "Subtype"));
    return str_eq(type, "XObject") && str_eq(subtype, "Image");
}

static bool is_font_program_stream_dict(Map* dict) {
    if (!dict) return false;
    return map_lookup(dict, "Length1").item != ITEM_NULL ||
           map_lookup(dict, "Length2").item != ITEM_NULL ||
           map_lookup(dict, "Length3").item != ITEM_NULL;
}

static void append_octal_escape(char* out, size_t* pos, unsigned char c) {
    out[(*pos)++] = '\\';
    out[(*pos)++] = (char)('0' + ((c >> 6) & 7));
    out[(*pos)++] = (char)('0' + ((c >> 3) & 7));
    out[(*pos)++] = (char)('0' + (c & 7));
}

static void advance_literal_state(unsigned char c, bool* in_literal, bool* escaped,
                                   int* literal_depth) {
    if (*in_literal) {
        if (*escaped) {
            *escaped = false;
        } else if (c == '\\') {
            *escaped = true;
        } else if (c == '(') {
            (*literal_depth)++;
        } else if (c == ')') {
            (*literal_depth)--;
            if (*literal_depth <= 0) {
                *in_literal = false;
                *literal_depth = 0;
            }
        }
    } else if (c == '(') {
        *in_literal = true;
        *literal_depth = 1;
        *escaped = false;
    }
}

static String* content_bytes_to_token_text(Input* input, String* src) {
    if (!input || !src) return nullptr;
    size_t out_len = 0;
    bool changed = false;
    bool in_literal = false;
    bool escaped = false;
    int literal_depth = 0;
    for (uint32_t i = 0; i < src->len; i++) {
        unsigned char c = (unsigned char)src->chars[i];
        if (in_literal && (c >= 0x80 || c == 0)) {
            out_len += 4;
            changed = true;
        } else {
            out_len++;
        }
        advance_literal_state(c, &in_literal, &escaped, &literal_depth);
    }
    if (!changed) return src;
    String* out = (String*)pool_calloc(input->pool, sizeof(String) + out_len + 1);
    if (!out) return nullptr;
    size_t j = 0;
    in_literal = false;
    escaped = false;
    literal_depth = 0;
    for (uint32_t i = 0; i < src->len; i++) {
        unsigned char c = (unsigned char)src->chars[i];
        if (in_literal && (c >= 0x80 || c == 0)) {
            append_octal_escape(out->chars, &j, c);
        } else {
            out->chars[j++] = (char)c;
        }
        advance_literal_state(c, &in_literal, &escaped, &literal_depth);
    }
    out->chars[j] = '\0';
    out->len = (uint32_t)out_len;
    out->is_ascii = 1;
    return out;
}

// Pass 3: walk every indirect_object content; if it's a stream Map with
// a Filter, decompress in place. After this pass, the `data` field of
// every stream object holds the plain (post-filter) byte payload, so
// callers (e.g. content-stream tokenizer) need not re-implement filter
// chains.
static void add_text_data_to_stream(Input* input, MarkBuilder& builder, Map* sm) {
    if (!input || !sm) return;
    Map* dict = item_as_map(map_lookup(sm, "dictionary"));
    if (is_image_stream_dict(dict)) return;
    if (is_font_program_stream_dict(dict)) return;
    String* data = item_as_string(map_lookup(sm, "data"));
    if (!data || data->len == 0) return;
    String* text_data = content_bytes_to_token_text(input, data);
    if (!text_data) return;
    builder.putToMap(lam::gc_borrow(sm), builder.createString("text_data"), {.item = s2it(text_data)});
}

static void decompress_streams(Input* input, MarkBuilder& builder, Array* objects) {    if (!objects) return;
#ifndef NDEBUG
    int decoded_count = 0;
#endif
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
        if (filter_it.item == ITEM_NULL) {
            add_text_data_to_stream(input, builder, sm);
            continue;
        }

        size_t out_len = 0;
        bool needs_free = false;
        const char* dec = get_decompressed_stream(sm, &out_len, &needs_free);
        if (!dec) continue;

        // Copy decompressed bytes into a String owned by the input pool.
        String* new_data = string_from_strview(strview_init(dec, out_len), input->pool);
        if (!new_data) {
            if (needs_free) mem_free((void*)dec);
            continue;
        }
        // Buffers from pdf_decompress_stream are mem_alloc'd, not malloc'd —
        // must use mem_free() to avoid corrupting the memtrack tinfo header.
        if (needs_free) mem_free((void*)dec);

        replace_string_field(sm, "data", new_data);
        add_text_data_to_stream(input, builder, sm);
#ifndef NDEBUG
        decoded_count++;
#endif
    }
    log_info("pdf_postprocess: decompressed %d stream(s)", decoded_count);
}

// ---------------------------------------------------------------------------
// Pass 4: PDF object streams → regular indirect objects
// ---------------------------------------------------------------------------
//
// PDF 1.5+ may store small indirect objects inside /Type /ObjStm streams.
// Page resource dictionaries can then point at object numbers that never
// appear as top-level `n 0 obj` records. Expanding those streams here keeps
// the rest of the resolver model simple: every indirect reference can be
// looked up in `pdf.objects`.

struct ObjStreamParser {
    Input* input;
    MarkBuilder* builder;
    const char* pos;
    const char* end;
};

static void osp_skip_ws(ObjStreamParser* p) {
    while (p->pos < p->end) {
        unsigned char c = (unsigned char)*p->pos;
        if (c == '%') {
            while (p->pos < p->end && *p->pos != '\n' && *p->pos != '\r') p->pos++;
            continue;
        }
        if (!str_is_space((char)c)) break;
        p->pos++;
    }
}

static bool osp_is_delim(char c) {
    return str_is_space(c) || c == '/' || c == '(' || c == ')' ||
           c == '<' || c == '>' || c == '[' || c == ']' || c == '{' ||
           c == '}' || c == '%';
}

static String* osp_make_string(Input* input, const char* data, size_t len) {
    return string_from_strview(strview_init(data, len), input->pool);
}

static String* osp_parse_name(ObjStreamParser* p) {
    if (p->pos >= p->end || *p->pos != '/') return nullptr;
    p->pos++;
    size_t cap = (size_t)(p->end - p->pos) + 1;
    char* buf = (char*)pool_calloc(p->input->pool, cap > 0 ? cap : 1);
    if (!buf) return nullptr;
    size_t len = 0;
    while (p->pos < p->end && !osp_is_delim(*p->pos)) {
        if (*p->pos == '#' && p->pos + 2 < p->end) {
            int hi = str_hex_val(p->pos[1]);
            int lo = str_hex_val(p->pos[2]);
            if (hi >= 0 && lo >= 0) {
                buf[len++] = (char)((hi << 4) | lo);
                p->pos += 3;
                continue;
            }
        }
        buf[len++] = *p->pos++;
    }
    return osp_make_string(p->input, buf, len);
}

static String* osp_parse_literal_string(ObjStreamParser* p) {
    if (p->pos >= p->end || *p->pos != '(') return nullptr;
    p->pos++;
    size_t cap = (size_t)(p->end - p->pos) + 1;
    char* buf = (char*)pool_calloc(p->input->pool, cap > 0 ? cap : 1);
    if (!buf) return nullptr;
    size_t len = 0;
    int depth = 1;
    while (p->pos < p->end && depth > 0) {
        char c = *p->pos++;
        if (c == '\\' && p->pos < p->end) {
            char e = *p->pos++;
            switch (e) {
                case 'n': buf[len++] = '\n'; break;
                case 'r': buf[len++] = '\r'; break;
                case 't': buf[len++] = '\t'; break;
                case 'b': buf[len++] = '\b'; break;
                case 'f': buf[len++] = '\f'; break;
                case '(':
                case ')':
                case '\\': buf[len++] = e; break;
                case '\n': break;
                case '\r':
                    if (p->pos < p->end && *p->pos == '\n') p->pos++;
                    break;
                default: buf[len++] = e; break;
            }
        } else if (c == '(') {
            depth++;
            buf[len++] = c;
        } else if (c == ')') {
            depth--;
            if (depth > 0) buf[len++] = c;
        } else {
            buf[len++] = c;
        }
    }
    return osp_make_string(p->input, buf, len);
}

static String* osp_parse_hex_string(ObjStreamParser* p) {
    if (p->pos >= p->end || *p->pos != '<') return nullptr;
    p->pos++;
    size_t cap = (size_t)(p->end - p->pos + 1) / 2 + 1;
    char* buf = (char*)pool_calloc(p->input->pool, cap > 0 ? cap : 1);
    if (!buf) return nullptr;
    size_t len = 0;
    int hi = -1;
    while (p->pos < p->end && *p->pos != '>') {
        int v = str_hex_val(*p->pos++);
        if (v < 0) continue;
        if (hi < 0) {
            hi = v;
        } else {
            buf[len++] = (char)((hi << 4) | v);
            hi = -1;
        }
    }
    if (hi >= 0) buf[len++] = (char)(hi << 4);
    if (p->pos < p->end && *p->pos == '>') p->pos++;
    return osp_make_string(p->input, buf, len);
}

static Item osp_parse_object(ObjStreamParser* p, int depth);

static Item osp_make_ref(ObjStreamParser* p, int obj_num, int gen_num) {
    Map* ref = map_pooled(p->input->pool);
    if (!ref) return {.item = ITEM_ERROR};
    p->builder->putToMap(lam::gc_borrow(ref), p->builder->createString("type"),
                         {.item = s2it(p->builder->createString("indirect_ref"))});
    double* obj_val = (double*)pool_calloc(p->input->pool, sizeof(double));
    double* gen_val = (double*)pool_calloc(p->input->pool, sizeof(double));
    if (!obj_val || !gen_val) return {.item = ITEM_ERROR};
    *obj_val = (double)obj_num;
    *gen_val = (double)gen_num;
    p->builder->putToMap(lam::gc_borrow(ref), p->builder->createString("object_num"), lambda_float_ptr_to_item(obj_val));
    p->builder->putToMap(lam::gc_borrow(ref), p->builder->createString("gen_num"), lambda_float_ptr_to_item(gen_val));
    return {.item = (uint64_t)ref};
}

static Item osp_parse_number_or_ref(ObjStreamParser* p) {
    const char* saved = p->pos;
    char* end = nullptr;
    long obj_num = strtol(p->pos, &end, 10);
    if (end != p->pos) {
        const char* after_obj = end;
        while (after_obj < p->end && str_is_space(*after_obj)) after_obj++;
        if (after_obj < p->end && (*after_obj == '+' || *after_obj == '-' || str_is_digit(*after_obj))) {
            char* gen_end = nullptr;
            long gen_num = strtol(after_obj, &gen_end, 10);
            const char* after_gen = gen_end;
            while (after_gen < p->end && str_is_space(*after_gen)) after_gen++;
            if (gen_end != after_obj && after_gen < p->end && *after_gen == 'R') {
                p->pos = after_gen + 1;
                return osp_make_ref(p, (int)obj_num, (int)gen_num);
            }
        }
    }

    p->pos = saved;
    double* val = (double*)pool_calloc(p->input->pool, sizeof(double));
    if (!val) return {.item = ITEM_ERROR};
    *val = strtod(p->pos, &end);
    if (end == p->pos) return {.item = ITEM_ERROR};
    p->pos = end;
    return lambda_float_ptr_to_item(val);
}

static Array* osp_parse_array(ObjStreamParser* p, int depth) {
    if (p->pos >= p->end || *p->pos != '[') return nullptr;
    p->pos++;
    Array* arr = array_pooled(p->input->pool);
    if (!arr) return nullptr;
    int count = 0;
    while (p->pos < p->end && count < 10000) {
        osp_skip_ws(p);
        if (p->pos >= p->end) break;
        if (*p->pos == ']') {
            p->pos++;
            break;
        }
        Item it = osp_parse_object(p, depth + 1);
        if (it.item != ITEM_ERROR && it.item != ITEM_NULL) {
            array_append(arr, it, p->input->pool, p->input->arena);
            count++;
        } else if (p->pos < p->end) {
            p->pos++;
        }
    }
    return arr;
}

static Map* osp_parse_dictionary(ObjStreamParser* p, int depth) {
    if (p->pos + 1 >= p->end || p->pos[0] != '<' || p->pos[1] != '<') return nullptr;
    p->pos += 2;
    Map* dict = map_pooled(p->input->pool);
    if (!dict) return nullptr;
    int pairs = 0;
    while (p->pos < p->end && pairs < 256) {
        osp_skip_ws(p);
        if (p->pos + 1 < p->end && p->pos[0] == '>' && p->pos[1] == '>') {
            p->pos += 2;
            break;
        }
        if (p->pos >= p->end || *p->pos != '/') {
            if (p->pos < p->end) p->pos++;
            continue;
        }
        String* key = osp_parse_name(p);
        if (!key) break;
        osp_skip_ws(p);
        Item val = osp_parse_object(p, depth + 1);
        if (val.item != ITEM_ERROR && val.item != ITEM_NULL) {
            p->builder->putToMap(lam::gc_borrow(dict), key, val);
            pairs++;
        }
    }
    return dict;
}

static Item osp_parse_object(ObjStreamParser* p, int depth) {
    if (depth > 32) return {.item = ITEM_NULL};
    osp_skip_ws(p);
    if (p->pos >= p->end) return {.item = ITEM_NULL};

    if (p->pos + 4 <= p->end && memcmp(p->pos, "null", 4) == 0 &&
        (p->pos + 4 == p->end || osp_is_delim(p->pos[4]))) {
        p->pos += 4;
        return {.item = ITEM_NULL};
    }
    if (p->pos + 4 <= p->end && memcmp(p->pos, "true", 4) == 0 &&
        (p->pos + 4 == p->end || osp_is_delim(p->pos[4]))) {
        p->pos += 4;
        return {.item = b2it(true)};
    }
    if (p->pos + 5 <= p->end && memcmp(p->pos, "false", 5) == 0 &&
        (p->pos + 5 == p->end || osp_is_delim(p->pos[5]))) {
        p->pos += 5;
        return {.item = b2it(false)};
    }
    if (*p->pos == '/') {
        String* name = osp_parse_name(p);
        return name ? (Item){.item = s2it(name)} : (Item){.item = ITEM_ERROR};
    }
    if (*p->pos == '(') {
        String* s = osp_parse_literal_string(p);
        return s ? (Item){.item = s2it(s)} : (Item){.item = ITEM_ERROR};
    }
    if (*p->pos == '<' && p->pos + 1 < p->end && p->pos[1] != '<') {
        String* s = osp_parse_hex_string(p);
        return s ? (Item){.item = s2it(s)} : (Item){.item = ITEM_ERROR};
    }
    if (*p->pos == '[') {
        Array* arr = osp_parse_array(p, depth);
        return arr ? (Item){.item = (uint64_t)arr} : (Item){.item = ITEM_ERROR};
    }
    if (*p->pos == '<' && p->pos + 1 < p->end && p->pos[1] == '<') {
        Map* dict = osp_parse_dictionary(p, depth);
        return dict ? (Item){.item = (uint64_t)dict} : (Item){.item = ITEM_ERROR};
    }
    if (*p->pos == '+' || *p->pos == '-' || *p->pos == '.' || str_is_digit(*p->pos)) {
        return osp_parse_number_or_ref(p);
    }
    p->pos++;
    return {.item = ITEM_NULL};
}

static Map* make_indirect_object(Input* input, MarkBuilder& builder, int obj_num, Item content) {
    Map* obj = map_pooled(input->pool);
    if (!obj) return nullptr;
    builder.putToMap(lam::gc_borrow(obj), builder.createString("type"),
                     {.item = s2it(builder.createString("indirect_object"))});
    double* obj_val = (double*)pool_calloc(input->pool, sizeof(double));
    double* gen_val = (double*)pool_calloc(input->pool, sizeof(double));
    if (!obj_val || !gen_val) return nullptr;
    *obj_val = (double)obj_num;
    *gen_val = 0.0;
    builder.putToMap(lam::gc_borrow(obj), builder.createString("object_num"), lambda_float_ptr_to_item(obj_val));
    builder.putToMap(lam::gc_borrow(obj), builder.createString("gen_num"), lambda_float_ptr_to_item(gen_val));
    if (content.item != ITEM_ERROR && content.item != ITEM_NULL) {
        builder.putToMap(lam::gc_borrow(obj), builder.createString("content"), content);
    }
    return obj;
}

static int expand_object_stream(Input* input, MarkBuilder& builder, Array* objects,
                                ObjTable* table, Map* stream_map) {
    Map* dict = stream_dictionary(stream_map);
    if (!pdf_dict_has_type(dict, "ObjStm")) return 0;
    String* data = item_as_string(map_lookup(stream_map, "data"));
    if (!data || data->len == 0) return 0;

    int n = item_as_int(map_lookup(dict, "N"), 0);
    int first = item_as_int(map_lookup(dict, "First"), -1);
    if (n <= 0 || n > 10000 || first < 0 || first >= (int)data->len) return 0;

    int* obj_nums = (int*)pool_calloc(input->pool, sizeof(int) * n);
    int* offsets = (int*)pool_calloc(input->pool, sizeof(int) * n);
    if (!obj_nums || !offsets) return 0;

    const char* base = data->chars;
    const char* end = data->chars + data->len;
    const char* p = base;
    for (int i = 0; i < n; i++) {
        p = skip_ws(p, end);
        if (p >= base + first) return 0;
        char* after_num = nullptr;
        obj_nums[i] = (int)strtol(p, &after_num, 10);
        if (after_num == p) return 0;
        p = skip_ws(after_num, end);
        char* after_off = nullptr;
        offsets[i] = (int)strtol(p, &after_off, 10);
        if (after_off == p) return 0;
        p = after_off;
    }

    int expanded = 0;
    for (int i = 0; i < n; i++) {
        int obj_num = obj_nums[i];
        if (obj_num < 0 || obj_table_get(table, obj_num).item != ITEM_NULL) continue;
        int start_off = first + offsets[i];
        int end_off = (i + 1 < n) ? first + offsets[i + 1] : (int)data->len;
        if (start_off < first || start_off >= (int)data->len || end_off <= start_off || end_off > (int)data->len) continue;

        ObjStreamParser parser = {input, &builder, base + start_off, base + end_off};
        Item content = osp_parse_object(&parser, 0);
        if (content.item == ITEM_ERROR || content.item == ITEM_NULL) continue;
        Map* wrapper = make_indirect_object(input, builder, obj_num, content);
        if (!wrapper) continue;
        array_append(objects, {.item = (uint64_t)wrapper}, input->pool, input->arena);
        expanded++;
    }
    return expanded;
}

static void expand_object_streams(Input* input, MarkBuilder& builder, Array* objects, ObjTable* table) {
    if (!objects || !table) return;
    int original_len = objects->length;
#ifndef NDEBUG
    int expanded = 0;
#endif
    for (int i = 0; i < original_len; i++) {
        Map* iobj = item_as_map(objects->items[i]);
        if (!iobj || !map_has_type(iobj, "indirect_object")) continue;
        Map* sm = item_as_map(map_lookup(iobj, "content"));
        if (!sm || !map_has_type(sm, "stream")) continue;
#ifndef NDEBUG
        expanded += expand_object_stream(input, builder, objects, table, sm);
#else
        expand_object_stream(input, builder, objects, table, sm);
#endif
    }
    log_info("pdf_postprocess: expanded %d object-stream object(s)", expanded);
}

// ---------------------------------------------------------------------------
// Pass 5: Image XObject → data:image/png;base64,... URI
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
// Packed and 16-bit samples, Indexed palettes, Lab and tint alternates share
// this path. ICC profiles currently use their device component interpretation.
// JPEG bytes are preserved; JPX, fax and generic-region JBIG2 use native codecs.
// Unsupported colour functions or JBIG2 segment kinds remain unresolved.

static double pdf_number(Item value, double fallback = 0) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_FLOAT ? value.get_double() :
        type == LMD_TYPE_INT ? (double)lambda_int_item_to_i64(value) : fallback;
}

static double pdf_array_number(Array* values, int index, double fallback) {
    return values && index >= 0 && index < values->length ? pdf_number(values->items[index], fallback) : fallback;
}

static uint8_t pdf_color_byte(double value) {
    return (uint8_t)floor(fmax(0.0, fmin(1.0, value)) * 255 + 0.5);
}

static bool pdf_tint_function(Item function, ObjTable* table, const double* inputs, int input_count, double* output, int count) {
    function = resolve_ref_deep(function, table);
    Map* stream = item_as_map(function), *dict = pdf_dictionary_for_item(function);
    if (!dict || count < 1 || count > 4) return false;
    int type = item_as_int(map_lookup(dict, "FunctionType"), -1);
    if (type == 2) {
        if (input_count != 1) return false;
        Array* c0 = item_as_array(map_lookup(dict, "C0"));
        Array* c1 = item_as_array(map_lookup(dict, "C1"));
        double fraction = pow(inputs[0], pdf_number(map_lookup(dict, "N"), 1));
        for (int c = 0; c < count; c++) {
            double first = pdf_array_number(c0, c, 0), last = pdf_array_number(c1, c, 1);
            output[c] = first + fraction * (last - first);
        }
        return true;
    }
    if (type == 0) {
        Array* size = item_as_array(map_lookup(dict, "Size"));
        int bits = item_as_int(map_lookup(dict, "BitsPerSample"), 0);
        String* data = item_as_string(map_lookup(stream, "data"));
        if (!size || size->length != input_count || input_count < 1 || input_count > 4 ||
                !(bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16) ||
                !data || item_as_int(map_lookup(dict, "Order"), 1) != 1) return false;
        Array* encode = item_as_array(map_lookup(dict, "Encode"));
        Array* decode = item_as_array(map_lookup(dict, "Decode"));
        Array* range = item_as_array(map_lookup(dict, "Range"));
        size_t entries = 1, stride[4]; int first[4], last[4]; double fraction[4];
        for (int d = 0; d < input_count; d++) {
            int extent = (int)pdf_array_number(size, d, 0);
            if (extent < 1 || extent > 65536 || entries > 1048576u / extent) return false;
            stride[d] = entries; entries *= extent;
            double encoded = fmax(0, fmin(extent - 1, pdf_array_number(encode, d * 2, 0) + inputs[d] *
                (pdf_array_number(encode, d * 2 + 1, extent - 1) - pdf_array_number(encode, d * 2, 0))));
            first[d] = (int)floor(encoded); last[d] = first[d] + 1 < extent ? first[d] + 1 : first[d];
            fraction[d] = encoded - first[d];
        }
        if (data->len < (entries * count * bits + 7) / 8) return false;
        double maximum = (1u << bits) - 1;
        for (int c = 0; c < count; c++) {
            double sample = 0;
            for (int corner = 0; corner < (1 << input_count); corner++) {
                size_t index = 0; double weight = 1;
                for (int d = 0; d < input_count; d++) {
                    bool upper = corner & (1 << d);
                    index += (upper ? last[d] : first[d]) * stride[d];
                    weight *= upper ? fraction[d] : 1 - fraction[d];
                }
                sample += weight * pdf_read_sample((const uint8_t*)data->chars, (index * count + c) * bits, bits);
            }
            double low = pdf_array_number(decode, c * 2, pdf_array_number(range, c * 2, 0));
            double high = pdf_array_number(decode, c * 2 + 1, pdf_array_number(range, c * 2 + 1, 1));
            output[c] = low + (sample / maximum) * (high - low);
        }
        return true;
    }
    if (type != 4) return false;
    String* program = item_as_string(map_lookup(stream, "data"));
    if (!program) return false;
    // bounded calculator stack for straight-line PDF tint functions; unknown operators reject.
    if (input_count < 1 || input_count > 32) return false;
    double stack[64] = {}; int used = input_count;
    memcpy(stack, inputs, input_count * sizeof(double));
    const char* p = program->chars, *end = p + program->len;
    while (p < end) {
        p = skip_ws(p, end);
        if (p == end) break;
        if (*p == '{' || *p == '}') { p++; continue; }
        char* after = nullptr; double value = strtod(p, &after);
        if (after != p) {
            if (used == 64 || after > end || !isfinite(value)) return false;
            stack[used++] = value; p = after; continue;
        }
        const char* token = p;
        while (p < end && !str_is_space(*p) && *p != '}') p++;
        StrView op = strview_init(token, p - token);
        if (strview_equal(&op, "cvr")) { if (!used) return false; }
        else if (strview_equal(&op, "dup")) {
            if (!used || used == 64) return false; stack[used] = stack[used - 1]; used++;
        } else if (strview_equal(&op, "exch")) {
            if (used < 2) return false; double swap = stack[used - 1]; stack[used - 1] = stack[used - 2]; stack[used - 2] = swap;
        } else if (strview_equal(&op, "pop")) {
            if (!used) return false; used--;
        } else if (strview_equal(&op, "index")) {
            if (used < 2 || stack[used - 1] < 0 || stack[used - 1] >= used - 1 || floor(stack[used - 1]) != stack[used - 1]) return false;
            int index = (int)stack[--used]; stack[used] = stack[used - 1 - index]; used++;
        } else if (strview_equal(&op, "roll")) {
            if (used < 2) return false;
            double turns = stack[used - 1], size = stack[used - 2]; used -= 2;
            if (size < 0 || size > used || floor(size) != size || !isfinite(turns) || floor(turns) != turns) return false;
            int n = (int)size, shift = n ? (int)fmod(turns, n) : 0;
            if (shift < 0) shift += n;
            double copy[64];
            for (int i = 0; i < n; i++) copy[(i + shift) % n] = stack[used - n + i];
            for (int i = 0; i < n; i++) stack[used - n + i] = copy[i];
        } else {
            const char* operators[] = {"add", "sub", "mul", "div"}; int operation = -1;
            for (int i = 0; i < 4; i++) if (strview_equal(&op, operators[i])) operation = i;
            if (operation < 0 || used < 2) return false;
            double right = stack[--used], left = stack[used - 1];
            if (operation == 3 && right == 0) return false;
            stack[used - 1] = operation == 0 ? left + right : operation == 1 ? left - right : operation == 2 ? left * right : left / right;
        }
    }
    if (used != count) return false;
    Array* range = item_as_array(map_lookup(dict, "Range"));
    for (int c = 0; c < count; c++) output[c] = fmax(pdf_array_number(range, c * 2, 0),
        fmin(pdf_array_number(range, c * 2 + 1, 1), stack[c]));
    return true;
}

struct PdfDeviceColor {
    int components = 0;
    bool lab = false;
    double white[3] = {0,1,0};
    double low[4] = {}, high[4] = {1,1,1,1};
};

struct PdfImageColor {
    int components = 0;
    bool indexed = false, lookup = false;
    Item tint = ItemNull;
    PdfDeviceColor device;
    uint8_t colors[256][3] = {};
};

static void pdf_device_rgb(const PdfDeviceColor* device, const double* values, uint8_t* rgb) {
    if (device->lab) {
        double converted[3]; color_lab_to_rgb(values[0], values[1], values[2], device->white, converted);
        for (int c = 0; c < 3; c++) rgb[c] = pdf_color_byte(converted[c]);
        return;
    }
    int components = device->components;
    for (int c = 0; c < 3; c++) rgb[c] = pdf_color_byte(components == 1 ? values[0] :
        components == 3 ? values[c] : (1 - values[c]) * (1 - values[3]));
}

static bool pdf_image_rgb(const PdfImageColor* color, ObjTable* table, const double* values, uint8_t* rgb) {
    if (color->lookup) {
        unsigned index = color->indexed ? (unsigned)fmax(0, fmin(255, floor(values[0] + 0.5))) : pdf_color_byte(values[0]);
        memcpy(rgb, color->colors[index], 3); return true;
    }
    if (color->tint.item != ITEM_NULL) {
        double transformed[4] = {};
        if (!pdf_tint_function(color->tint, table, values, color->components, transformed, color->device.components)) return false;
        pdf_device_rgb(&color->device, transformed, rgb);
    } else pdf_device_rgb(&color->device, values, rgb);
    return true;
}

static bool pdf_image_color(Item raw, ObjTable* table, PdfImageColor* color, int depth = 0) {
    if (depth > 8) return false;
    Item cs = resolve_ref_deep(raw, table);
    Array* array = item_as_array(cs);
    String* name = item_as_pdf_name(array && array->length ? array->items[0] : cs);
    if (!name) return false;
    if (str_eq(name, "DeviceGray") || str_eq(name, "G") || str_eq(name, "CalGray")) color->components = 1;
    else if (str_eq(name, "DeviceRGB") || str_eq(name, "RGB") || str_eq(name, "CalRGB")) color->components = 3;
    else if (str_eq(name, "DeviceCMYK") || str_eq(name, "CMYK")) color->components = 4;
    else if (str_eq(name, "Lab") && array && array->length >= 2) {
        Map* dict = item_as_map(resolve_ref_deep(array->items[1], table));
        Array* white = item_as_array(resolve_ref_deep(map_lookup(dict, "WhitePoint"), table));
        Array* range = item_as_array(resolve_ref_deep(map_lookup(dict, "Range"), table));
        if (!white || white->length != 3) return false;
        color->components = 3; color->device.lab = true;
        color->device.high[0] = 100;
        for (int c = 0; c < 3; c++) {
            color->device.white[c] = pdf_array_number(white, c, 0);
            if (!isfinite(color->device.white[c]) || color->device.white[c] <= 0) return false;
            if (c) {
                color->device.low[c] = pdf_array_number(range, (c - 1) * 2, -100);
                color->device.high[c] = pdf_array_number(range, (c - 1) * 2 + 1, 100);
            }
        }
    }
    else if (str_eq(name, "ICCBased") && array && array->length >= 2) {
        Map* profile = pdf_dictionary_for_item(resolve_ref_deep(array->items[1], table));
        color->components = item_as_int(map_lookup(profile, "N"), 0);
    } else if (array && array->length >= 4 &&
            (str_eq(name, "Indexed") || str_eq(name, "I") || str_eq(name, "Separation") || str_eq(name, "DeviceN"))) {
        bool indexed = str_eq(name, "Indexed") || str_eq(name, "I");
        PdfImageColor base;
        if (!pdf_image_color(array->items[indexed ? 1 : 2], table, &base, depth + 1) || (!indexed && base.lookup)) return false;
        color->components = 1; color->indexed = indexed; color->lookup = true;
        Item lookup = resolve_ref_deep(array->items[3], table);
        String* palette = item_as_string(lookup);
        if (!palette) palette = item_as_string(map_lookup(item_as_map(lookup), "data"));
        int highest = indexed ? item_as_int(array->items[2], -1) : 255;
        if (highest < 0 || highest > 255 || (indexed && (!palette || palette->len < (size_t)(highest + 1) * base.components))) return false;
        if (str_eq(name, "DeviceN")) {
            Array* names = item_as_array(resolve_ref_deep(array->items[1], table));
            if (!names || names->length < 1 || names->length > 32) return false;
            color->components = names->length;
            if (names->length > 1) {
                color->lookup = false; color->tint = array->items[3];
                color->device = base.device; return true;
            }
        }
        for (int i = 0; i < 256; i++) {
            double values[32] = {};
            if (indexed) for (int c = 0; c < base.components; c++) {
                double sample = (uint8_t)palette->chars[(i > highest ? highest : i) * base.components + c] / 255.0;
                values[c] = base.tint.item != ITEM_NULL ? sample :
                    base.device.low[c] + sample * (base.device.high[c] - base.device.low[c]);
            }
            else {
                double tint = i / 255.0;
                if (!pdf_tint_function(array->items[3], table, &tint, 1, values, base.components)) return false;
            }
            if (!pdf_image_rgb(&base, table, values, color->colors[i])) return false;
        }
    }
    color->device.components = color->components;
    return color->components == 1 || color->components == 3 || color->components == 4;
}

static void bb_u32be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8); p[3] = (uint8_t) v;
}

// Append a PNG chunk (length, type, data, CRC32). type must be 4 ASCII bytes.
static bool png_chunk(ByteBuilder* out, const char type[4],
                      const uint8_t* data, size_t dlen) {
    uint8_t hdr[8];
    bb_u32be(hdr, (uint32_t)dlen);
    memcpy(hdr + 4, type, 4);
    if (!byte_builder_append(out, hdr, 8)) return false;
    if (dlen && !byte_builder_append(out, data, dlen)) return false;
    // CRC covers type + data
    uLong crc = crc32(0L, (const Bytef*)hdr + 4, 4);
    if (dlen) crc = crc32(crc, (const Bytef*)data, (uInt)dlen);
    uint8_t crcb[4];
    bb_u32be(crcb, (uint32_t)crc);
    return byte_builder_append(out, crcb, 4);
}

// Encode RGBA pixels (width*height*4 bytes, top-down, 8-bit per channel)
// to a PNG byte stream. Returns mem_alloc'd buffer in *out_data and length
// in *out_len. Caller must mem_free(). Returns false on allocation/zlib errors.
static bool encode_png_rgba(const uint8_t* pixels, int width, int height,
                            uint8_t** out_data, size_t* out_len) {
    if (!pixels || width <= 0 || height <= 0) return false;
    ByteBuilder png;
    if (!byte_builder_init(&png, 4096, MEM_CAT_INPUT_PDF, false)) return false;

    // PNG signature
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    if (!byte_builder_append(&png, sig, 8)) { byte_builder_destroy(&png); return false; }

    // IHDR
    uint8_t ihdr[13];
    bb_u32be(ihdr,     (uint32_t)width);
    bb_u32be(ihdr + 4, (uint32_t)height);
    ihdr[8]  = 8;   // bit depth
    ihdr[9]  = 6;   // colour type: RGBA
    ihdr[10] = 0;   // compression
    ihdr[11] = 0;   // filter method
    ihdr[12] = 0;   // interlace
    if (!png_chunk(&png, "IHDR", ihdr, sizeof(ihdr))) { byte_builder_destroy(&png); return false; }

    // Build raw scanlines: one filter byte (None=0) per row, then 4*width
    // bytes of pixel data.
    size_t row_bytes = (size_t)width * 4;
    size_t raw_len = (row_bytes + 1) * (size_t)height;
    uint8_t* raw = (uint8_t*)mem_alloc(raw_len, MEM_CAT_INPUT_PDF);
    if (!raw) { byte_builder_destroy(&png); return false; }
    for (int y = 0; y < height; y++) {
        raw[y * (row_bytes + 1)] = 0;  // no filter
        memcpy(raw + y * (row_bytes + 1) + 1,
               pixels + (size_t)y * row_bytes, row_bytes);
    }

    // Deflate (zlib wrapper) into IDAT
    uLongf zlen = compressBound((uLong)raw_len);
    uint8_t* zbuf = (uint8_t*)mem_alloc(zlen, MEM_CAT_INPUT_PDF);
    if (!zbuf) { mem_free(raw); byte_builder_destroy(&png); return false; }
    int zr = compress2(zbuf, &zlen, raw, (uLong)raw_len, Z_DEFAULT_COMPRESSION);
    mem_free(raw);
    if (zr != Z_OK) { mem_free(zbuf); byte_builder_destroy(&png); return false; }
    bool ok = png_chunk(&png, "IDAT", zbuf, (size_t)zlen);
    mem_free(zbuf);
    if (!ok) { byte_builder_destroy(&png); return false; }

    // IEND
    if (!png_chunk(&png, "IEND", nullptr, 0)) { byte_builder_destroy(&png); return false; }

    *out_data = byte_builder_take(&png, out_len);  // transfer ownership
    return true;
}

// Base64 encoder (no line breaks) — RFC 4648. Returns mem_alloc'd
// nul-terminated string in the PDF category; caller mem_free()s.
static char* base64_alloc(const uint8_t* src, size_t n) {
    size_t out_len = base64_encoded_len(n, BASE64_STD);
    char* out = (char*)mem_alloc(out_len + 1, MEM_CAT_INPUT_PDF);
    if (!out) return nullptr;
    base64_encode(src, n, out, BASE64_STD);
    return out;
}

static bool filter_item_has_name(Item filter_it, const char* name1, const char* name2) {
    if (String* fs = item_as_pdf_name(filter_it)) {
        return str_eq(fs, name1) || (name2 && str_eq(fs, name2));
    }
    if (Array* fa = item_as_array(filter_it)) {
        for (int i = 0; i < fa->length; i++) {
            if (filter_item_has_name(fa->items[i], name1, name2)) return true;
        }
    }
    return false;
}

static String* bytes_to_data_uri(Input* input, const char* mime,
                                 const uint8_t* data, size_t data_len) {
    if (!input || !mime || !data || data_len == 0) return nullptr;

    char* b64 = base64_alloc(data, data_len);
    if (!b64) return nullptr;

    size_t mime_len = strlen(mime);
    size_t b64_len = strlen(b64);
    size_t total = strlen("data:") + mime_len + strlen(";base64,") + b64_len;
    String* out = (String*)pool_calloc(input->pool, sizeof(String) + total + 1);
    if (!out) { mem_free(b64); return nullptr; }

    int n = snprintf(out->chars, total + 1, "data:%s;base64,%s", mime, b64);
    mem_free(b64);
    if (n < 0 || (size_t)n != total) return nullptr;

    out->len = (uint32_t)total;
    out->is_ascii = 1;
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
    String* sub = item_as_pdf_name(map_lookup(dict, "Subtype"));
    if (!sub || !str_eq(sub, "Image")) return nullptr;

    int w   = item_as_int(map_lookup(dict, "Width"),  0);
    int h   = item_as_int(map_lookup(dict, "Height"), 0);
    bool stencil = get_type_id(map_lookup(dict, "ImageMask")) == LMD_TYPE_BOOL && map_lookup(dict, "ImageMask").bool_val;
    int bpc = item_as_int(map_lookup(dict, "BitsPerComponent"), stencil ? 1 : 8);

    Item data_it = map_lookup(stream_map, "data");
    String* pix = item_as_string(data_it);
    if (!pix) return nullptr;

    Item filter_it = map_lookup(dict, "Filter");
    if (filter_item_has_name(filter_it, "DCTDecode", "DCT")) {
        return bytes_to_data_uri(input, "image/jpeg", (const uint8_t*)pix->chars, pix->len);
    }

    if (w <= 0 || h <= 0 || (size_t)w > (128u * 1024 * 1024) / ((size_t)h * 4)) return nullptr;
    PdfImageColor color;
    if (stencil) color.components = color.device.components = 1;
    else if (!pdf_image_color(map_lookup(dict, "ColorSpace"), table, &color)) return nullptr;
    int ncomp = color.components;
    ByteBuilder decoded = {}; byte_builder_init(&decoded, 0, MEM_CAT_INPUT_PDF, false);
    struct Cleanup { ByteBuilder* bytes; ~Cleanup() { byte_builder_destroy(bytes); } } cleanup = {&decoded};
    const uint8_t* samples = (const uint8_t*)pix->chars; size_t length = pix->len;
    bool codec_ok = true;
    if (filter_item_has_name(filter_it, "CCITTFaxDecode", "CCF")) {
        Item parms = resolve_ref_deep(map_lookup(dict, "DecodeParms"), table);
        Array* sequence = item_as_array(parms);
        if (sequence && sequence->length) parms = resolve_ref_deep(sequence->items[sequence->length - 1], table);
        Map* values = item_as_map(parms);
        auto flag = [&](const char* name) { Item value = map_lookup(values, name);
            return get_type_id(value) == LMD_TYPE_BOOL && value.bool_val; };
        int columns = item_as_int(map_lookup(values, "Columns"), 1728);
        int rows = item_as_int(map_lookup(values, "Rows"), h);
        codec_ok = columns == w && (rows == 0 || rows == h) && pdf_decode_fax_samples(samples, length, w, h,
            item_as_int(map_lookup(values, "K"), 0), flag("EndOfLine"), flag("EncodedByteAlign"), flag("BlackIs1"), &decoded);
        bpc = 8;
    } else if (filter_item_has_name(filter_it, "JBIG2Decode", "JBIG2Decode")) {
        codec_ok = pdf_decode_jbig2_samples(samples, length, w, h, &decoded); bpc = 8;
    } else if (filter_item_has_name(filter_it, "JPXDecode", "JPX")) {
        int components = 0;
        codec_ok = pdf_decode_jpx_samples(samples, length, w, h, color.indexed, &components, &bpc, &decoded) && components == ncomp;
    }
    if (!codec_ok || !(bpc == 1 || bpc == 2 || bpc == 4 || bpc == 8 || bpc == 16)) return nullptr;
    if (decoded.length) { samples = decoded.data; length = decoded.length; }
    size_t row_bytes = ((size_t)w * ncomp * bpc + 7) / 8;
    if (length < row_bytes * h) return nullptr;
    Array* decode = item_as_array(resolve_ref_deep(map_lookup(dict, "Decode"), table));
    double first[32], span[32]; unsigned maximum = (1u << bpc) - 1;
    for (int c = 0; c < ncomp; c++) {
        bool direct = !color.lookup && color.tint.item == ITEM_NULL;
        first[c] = pdf_array_number(decode, c * 2, direct ? color.device.low[c] : 0);
        span[c] = pdf_array_number(decode, c * 2 + 1, color.indexed ? maximum : direct ? color.device.high[c] : 1) - first[c];
    }

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
    uint8_t* rgba = (uint8_t*)mem_alloc(rgba_len, MEM_CAT_INPUT_PDF);
    if (!rgba) return nullptr;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t pi = (size_t)y * w + x;
            size_t di = pi * 4;
            double values[32] = {};
            for (int c = 0; c < ncomp; c++) {
                size_t bit = ((size_t)x * ncomp + c) * bpc;
                unsigned value = pdf_read_sample(samples + (size_t)y * row_bytes, bit, bpc);
                values[c] = first[c] + value * span[c] / maximum;
            }
            if (!pdf_image_rgb(&color, table, values, rgba + di)) { mem_free(rgba); return nullptr; }
            rgba[di+3] = stencil ? 255 - pdf_color_byte(values[0]) : alpha ? alpha[pi] : 255;
            if (stencil) rgba[di] = rgba[di+1] = rgba[di+2] = 0;
        }
    }

    String* out = pdf_rgba_image_data_uri(input, rgba, w, h);
    mem_free(rgba);
    return out;
}

// Pass 4 entry: walk objects, encode images that look supported.
static void encode_image_xobjects(Input* input, MarkBuilder& builder,
                                  Array* objects, ObjTable* table) {
    if (!objects) return;
#ifndef NDEBUG
    int encoded = 0;
#endif
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
        String* tystr  = item_as_pdf_name(map_lookup(dict, "Type"));
        String* substr = item_as_pdf_name(map_lookup(dict, "Subtype"));
        if (!tystr || !str_eq(tystr, "XObject")) continue;
        if (!substr || !str_eq(substr, "Image")) continue;
        // Skip SMask streams themselves — they're consumed alongside their owners.

        String* uri = image_to_data_uri(input, sm, table);
        if (!uri) continue;
        builder.putToMap(lam::gc_borrow(sm), builder.createString("data_uri"),
                         {.item = s2it(uri)});
#ifndef NDEBUG
        encoded++;
#endif
    }
    log_info("pdf_postprocess: encoded %d Image XObject(s) to data URI", encoded);
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
    if (!out) { mem_free(b64); return nullptr; }
    int n = snprintf(out->chars, total + 1, "data:%s;base64,%s", mime, b64);
    if (n < 0 || (size_t)n != total) { mem_free(b64); return nullptr; }
    out->len = (uint32_t)total;
    out->is_ascii = 1;
    mem_free(b64);
    return out;
}

// Pass 5 entry: walk objects, encode embedded font programs.
// Looks for FontDescriptor dicts and, if they reference a FontFile/
// FontFile2/FontFile3 stream, attaches `font_data_uri` + `font_format`
// to the descriptor. Lambda-side renderers can then emit @font-face.
static void encode_embedded_fonts(Input* input, MarkBuilder& builder,
                                  Array* objects, ObjTable* table) {
    if (!objects) return;
#ifndef NDEBUG
    int encoded = 0;
#endif
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
        builder.putToMap(lam::gc_borrow(fd), builder.createString("font_data_uri"),
                         {.item = s2it(uri)});
        builder.putToMap(lam::gc_borrow(fd), builder.createString("font_format"),
                         {.item = s2it(builder.createString(fmt))});
#ifndef NDEBUG
        encoded++;
#endif
    }
    log_info("pdf_postprocess: encoded %d embedded font(s) to data URI",
             encoded);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point.
// ---------------------------------------------------------------------------

String* pdf_rgba_image_data_uri(Input* input, const uint8_t* pixels, int width, int height) {
    if (!input || width <= 0 || height <= 0 ||
        (size_t)width > (SIZE_MAX / (size_t)height - 1) / 4 ||
        ((size_t)width * 4 + 1) * (size_t)height > UINT_MAX) return nullptr;
    uint8_t* png = nullptr; size_t length = 0;
    if (!encode_png_rgba(pixels, width, height, &png, &length)) return nullptr;
    String* result = bytes_to_data_uri(input, "image/png", png, length);
    mem_free(png);
    return result;
}

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

    // Pass 1: stream decompression (FlateDecode, ASCII85Decode, etc.)
    decompress_streams(input, builder, objects);

    // Pass 2: object stream expansion, then rebuild the reference table.
    expand_object_streams(input, builder, objects, &table);
    ObjTable expanded_table = {nullptr, 0, 0};
    build_obj_table(objects, &expanded_table, pool);

    // Pass 3: page-tree flattening
    flatten_page_tree(input, builder, pdf_info, &expanded_table);
    resolve_page_labels(input, builder, pdf_info, &expanded_table);

    // Pass 4: ToUnicode CMap parsing
    walk_fonts(input, builder, objects, &expanded_table);

    // Pass 5: encode Image XObjects to PNG data URIs
    encode_image_xobjects(input, builder, objects, &expanded_table);

    // Pass 6: encode embedded font programs to data URIs
    encode_embedded_fonts(input, builder, objects, &expanded_table);

    g_post_pool = nullptr;
}
