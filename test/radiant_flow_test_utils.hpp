#pragma once

#include "../radiant/view.hpp"
#include "../lib/strbuf.h"
#include <string.h>

struct RadiantTestInlineBoundaryRecipe {
    const char* name;
    const char* before;
    const char* unit;
    const char* after;
    bool atomic = false;
};

inline constexpr RadiantTestInlineBoundaryRecipe radiant_test_inline_boundary_recipes[] = {
    {"word", "XXXXX", "XX", "XXXXX"},
    {"space-before", "XXXXX ", "XX", "XXXXX"},
    {"space-after", "XXXXX", "XX", " XXXXX"},
    {"hyphen", "XX", "-", "XX"},
    {"cjk", "XX", "中", "XX"},
    {"joiner", "XX", "X", "\xe2\x81\xa0XX"},
    {"atomic", "XX", "X", "XX", true}
};

struct RadiantTestInlineBoundaryOptions {
    const char* mode;
    bool rtl;
    unsigned recipe;
    bool combined;
};

inline const char* radiant_test_inline_boundary_white_space(bool combined, unsigned phase) {
    if (phase == 1) return combined ? "pre-wrap" : "normal";
    return phase == 2 || combined ? "pre" : "nowrap";
}

inline StrBuf* radiant_test_inline_boundary_fixture(
        const RadiantTestInlineBoundaryOptions& options,
        bool reference = false, bool paired = true) {
    const auto& recipe = radiant_test_inline_boundary_recipes[options.recipe];
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        // reserve visible space around signed inline overflow in raster and vector controls.
        "html,body{margin:0;background:white}body{padding:300px;font:20px/20px Ahem;color:blue}"
        ".boundary-case{writing-mode:%s;direction:%s;inline-size:3em}</style><body id=boundary-host>",
        options.mode, options.rtl ? "rtl" : "ltr");
    for (unsigned index = 0; index < (paired ? 2u : 1u); index++) {
        bool control = paired ? index != 0 : reference;
        const char* id = index ? "control" : "result";
        const char* white_space = control ? (options.combined ? "pre-wrap" : "normal")
            : radiant_test_inline_boundary_white_space(options.combined, 0);
        strbuf_append_format(source,
            "<div id=%s class=boundary-case><span id=%s-before>%s</span>"
            "<span id=%s-unit style='white-space:%s;%s'>%s</span>"
            "<span id=%s-after%s>%s</span></div>",
            id, id, recipe.before, id, white_space,
            options.combined ? "text-combine-upright:all" : "",
            options.combined ? "X\nX" : recipe.unit, id,
            recipe.atomic ? " style='display:inline-block;inline-size:2em;block-size:1em'" : "",
            recipe.after);
    }
    return source;
}

struct RadiantTestContainedOrthogonalState {
    float preferred_size;
    float intrinsic_size;
    bool contained;
    bool large_viewport = false;
};

inline constexpr RadiantTestContainedOrthogonalState radiant_test_contained_orthogonal_states[] = {
    {-1.0f, -1.0f, true}, {-1.0f, 80.0f, true}, {120.0f, 80.0f, true},
    {0.0f, 80.0f, true}, {-1.0f, 80.0f, true}, {-1.0f, 80.0f, false},
    {-1.0f, 80.0f, true}, {-1.0f, -1.0f, true},
    {-1.0f, -1.0f, true, true}, {-1.0f, -1.0f, true}
};

inline StrBuf* radiant_test_contained_orthogonal_fixture(const char* mode, bool rtl,
        const char* containment, float intrinsic = -1.0f) {
    bool vertical = strcmp(mode, "horizontal-tb") != 0;
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white;font:20px/20px Ahem}#host{writing-mode:%s;"
        "direction:%s;inline-size:140px}#parent{inline-size:60px;contain:%s;contain-intrinsic-%s:",
        vertical ? "horizontal-tb" : "vertical-lr", rtl ? "rtl" : "ltr", containment,
        vertical ? "height" : "width");
    if (intrinsic < 0.0f) strbuf_append_str(source, "none");
    else strbuf_append_format(source, "%gpx", intrinsic);
    strbuf_append_format(source, "}#result{writing-mode:%s;block-size:20px;word-break:break-all;"
        "color:blue}</style><div id=host><div id=parent><div id=result>", mode);
    for (unsigned glyph = 0; glyph < 100; glyph++) strbuf_append_char(source, 'X');
    strbuf_append_str(source, "</div></div></div>");
    return source;
}

struct RadiantTestAlternativePaintCase {
    const char* mode;
    bool rtl;
    bool missing;
    const char* text;
    const char* style;
    const char* host_style;
    int density;
};

inline constexpr RadiantTestAlternativePaintCase radiant_test_alternative_paint_cases[] = {
    {"horizontal-tb", false, false, "miAw", "font:24px/36px serif;color:blue;text-transform:uppercase",
        "opacity:.75", 2},
    {"vertical-rl", true, true, "miAw", "font:26px/38px serif;color:blue;font-style:italic",
        "opacity:.75", 1},
    {"sideways-lr", false, false, "miAw", "font:24px/36px monospace;color:blue;text-transform:uppercase",
        "transform:translate(4px,6px) scale(1.2);transform-origin:0 0;opacity:.75", 1},
    {"vertical-lr", true, true, "AB", "font:24px/32px Ahem;color:blue;text-orientation:upright",
        "opacity:.75", 1}
};

inline StrBuf* radiant_test_alt_image_fixture(const char* mode, bool rtl,
                                               const char* display, bool missing,
                                               float font_size = 20.0f,
                                               float line_height = 20.0f,
                                               const char* alternative = "XXXX",
                                               const char* extra_style = "",
                                               bool reference = false, bool paired = false) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}%s{display:inline-block;writing-mode:%s;direction:%s;"
        "font:%gpx/%gpx Ahem;vertical-align:top;%s}%s{display:%s;vertical-align:top;"
        "background:#ff0;%s}</style>",
        paired ? "#result,#control" : "#result", mode, rtl ? "rtl" : "ltr", font_size, line_height,
        strcmp(display, "block") == 0 ? "inline-size:300px;block-size:200px" : "",
        paired ? "#item,#control-item" : "#item", display, extra_style);
    for (unsigned index = 0; index < (paired ? 2u : 1u); index++) {
        const char* id = index ? "control-item" : "item";
        strbuf_append_format(source, "<div id=%s>", index ? "control" : "result");
        if (paired ? index != 0 : reference) {
            // ordinary text models the existing UA icon reservation as logical padding.
            strbuf_append_format(source, "<span id=%s><span style='padding-inline-start:16px'>%s</span></span>",
                id, alternative);
        } else {
            strbuf_append_format(source, "<img id=%s%s alt='%s'>", id,
                missing ? " src='#'" : "", alternative);
        }
        strbuf_append_str(source, "</div>");
    }
    return source;
}

struct RadiantTestBidiScope { const char* code; const char* css; const char* direction; bool reverse; };

struct RadiantTestBaselineContributorOptions {
    const char* mode;
    bool rtl;
    const char* overflow;
    unsigned own_line; // 0: absent, 1: before, 2: after the block child
    bool wide_child = false;
};

inline StrBuf* radiant_test_baseline_contributor_fixture(
    const RadiantTestBaselineContributorOptions& options, bool reference = false, bool paired = false) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;font:20px/20px Ahem}body{direction:%s}.ib{display:inline-block;%s}"
        ".child{writing-mode:%s;overflow:%s;color:transparent;%s}.spacer{width:%s;height:3em}"
        ".own{color:#060}</style><span id=before>aaa</span>", options.rtl ? "rtl" : "ltr",
        options.own_line == 0 || options.wide_child ? "width:120px" : "", options.mode,
        options.overflow, options.wide_child ? "block-size:120px" : "",
        options.wide_child ? "120px" : "1em");
    for (unsigned index = 0; index < (paired ? 2u : 1u); index++) {
        const char* id = index ? "control" : "result";
        bool spacer = (paired ? index != 0 : reference) && strcmp(options.mode, "horizontal-tb") != 0;
        strbuf_append_format(source, "<div id=%s class=ib>", id);
        if (options.own_line == 1)
            strbuf_append_format(source, "<span id=%s-own class=own>bbb</span>", id);
        strbuf_append_format(source, "<div id=%s-child class=%s>%s</div>", id,
            spacer ? "spacer" : "child", spacer ? "" : "vvv");
        if (options.own_line == 2)
            strbuf_append_format(source, "<span id=%s-own class=own>bbb</span>", id);
        strbuf_append_str(source, "</div>");
    }
    strbuf_append_str(source, "<span id=after>ccc</span>");
    return source;
}

inline StrBuf* radiant_test_contents_orientation_fixture(const char* mode, bool rtl,
    const char* orientation, unsigned depth, bool paired = true) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>html,body{margin:0;background:white}.box{display:inline-block;"
        "writing-mode:%s;direction:%s;font:20px/30px monospace;vertical-align:top}"
        ".scope{display:contents}.control .scope{display:inline}</style>", mode, rtl ? "rtl" : "ltr");
    for (unsigned index = 0; index < (paired ? 2u : 1u); index++) {
        const char* prefix = index ? "control-" : "";
        strbuf_append_format(source, "<div id=%s class='box %s'>",
            index ? "control" : "result", index ? "control" : "");
        for (unsigned level = 1; level < depth; level++)
            strbuf_append_str(source, "<span class=scope style='text-orientation:mixed'>");
        strbuf_append_format(source,
            "<span class=scope id=%sscope style='text-orientation:%s;color:#060'>AB</span>",
            prefix, orientation);
        for (unsigned level = 1; level < depth; level++) strbuf_append_str(source, "</span>");
        // unwrapped following text detects a style context that leaks past the boxless scope.
        strbuf_append_str(source, "cd</div>");
    }
    return source;
}
inline constexpr RadiantTestBidiScope radiant_test_bidi_scopes[] = {
    {"202E","bidi-override","rtl",true}, {"202D","bidi-override","ltr",false},
    {"202B","embed","rtl",false}, {"202A","embed","ltr",false}
};

struct RadiantTestBidiFixtureOptions {
    bool word_spaces = false, grouped_text = false, preserve_breaks = false;
    const char* boundary = nullptr;
    const char* word_style = "";
    const char* inline_prefix = "", *inline_suffix = "";
    const char* word_prefix = "", *word_suffix = "";
};

inline StrBuf* radiant_test_bidi_fixture(const char* mode, bool rtl, const RadiantTestBidiScope& scope,
                                         const RadiantTestBidiFixtureOptions& options = {}) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:1200px;height:1200px;writing-mode:%s}"
        "section{font:20px/20px Ahem;inline-size:180px;direction:%s;white-space:%s}i{font-style:normal}"
        "</style><main id=result>",mode,rtl ? "rtl" : "ltr",options.preserve_breaks ? "pre-line" : "normal");
    for (bool raw : {true,false}) {
        strbuf_append_format(source,"<section id=%s><span>",raw ? "raw" : "css");
        if (raw) strbuf_append_format(source,"&#x%s;",scope.code);
        else strbuf_append_format(source,"<span style='unicode-bidi:%s;direction:%s'>",scope.css,scope.direction);
        strbuf_append_str(source,options.inline_prefix);
        unsigned group_size = options.grouped_text ? 3u : 1u;
        for (unsigned index = 0; index < 6; index++) {
            if (index == 3 && options.boundary) strbuf_append_str(source,options.boundary);
            else if (index && !options.word_spaces) strbuf_append_char(source,' ');
            if (index % group_size == 0)
                strbuf_append_format(source,"<i id=%s-word%u style='color:%s;%s'>",
                    raw ? "raw" : "css",index/group_size,(index/group_size) % 2 ? "#00f" : "#060",options.word_style);
            strbuf_append_str(source,options.word_prefix);
            strbuf_append_str(source,"XXXX");
            strbuf_append_str(source,options.word_suffix);
            if (options.word_spaces && index+1 < 6) strbuf_append_char(source,' ');
            if ((index+1) % group_size == 0) strbuf_append_str(source,"</i>");
        }
        strbuf_append_str(source,options.inline_suffix);
        strbuf_append_str(source,raw ? "&#x202C;" : "</span>");
        strbuf_append_str(source,"</span></section>");
    }
    strbuf_append_str(source,"</main>");
    return source;
}

struct RadiantTestAtomicState {
    float available;
    const char* align;
    bool inline_block, padded, inherited;
};
inline constexpr RadiantTestAtomicState radiant_test_atomic_states[] = {
    {60,"start",false,false,true}, {48,"end",true,true,false},
    {110,"center",false,true,true}, {60,"start",true,false,false}};
inline constexpr float radiant_test_atomic_inline_sizes[] = {22,31,19,28};
inline constexpr float radiant_test_atomic_block_sizes[] = {13,17,11,19};

inline StrBuf* radiant_test_atomic_parent_style(const char* mode, bool rtl,
                                                const RadiantTestAtomicState& state) {
    StrBuf* style = strbuf_new();
    strbuf_append_format(style, "display:%s;writing-mode:%s;direction:%s;inline-size:%gpx;"
        "block-size:200px;text-align:%s;padding:%s;border:%s solid transparent",
        state.inline_block ? "inline-block" : "block", mode,
        state.inherited ? "inherit" : rtl ? "rtl" : "ltr", state.available, state.align,
        state.padded ? "3px" : "0", state.padded ? "2px" : "0");
    return style;
}

inline StrBuf* radiant_test_atomic_item_style(unsigned kind, unsigned index, bool padded) {
    StrBuf* style = strbuf_new();
    strbuf_append_format(style, "display:%s;inline-size:%gpx;block-size:%gpx;"
        "margin-inline:%spx %spx;padding:%s;border:%s solid",
        kind == 2 ? "inline-table" : "inline-block", radiant_test_atomic_inline_sizes[index],
        radiant_test_atomic_block_sizes[index], padded ? "3" : "0", padded ? "5" : "0",
        padded ? "1px" : "0", padded ? "1px" : "0");
    return style;
}

inline StrBuf* radiant_test_atomic_fixture(unsigned kind, bool prefix, const char* mode,
                                           bool rtl, const RadiantTestAtomicState& state) {
    StrBuf* source = strbuf_new();
    StrBuf* style = radiant_test_atomic_parent_style(mode, rtl, state);
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:320px;height:220px;color:green;font-size:0;line-height:0}"
        "main{width:320px;text-align:left}section{font:10px/10px Ahem;margin-left:0;margin-right:auto;vertical-align:top}"
        ".atom{vertical-align:top;box-sizing:content-box;table-layout:fixed;border-spacing:0;background:#cfc}"
        "td{padding:0;border:0}</style><main id=host style='direction:%s'><section id=result style='%s'>%s",
        state.inherited && rtl ? "rtl" : "ltr", style->str, prefix ? "X" : "");
    strbuf_free(style);
    const char* tag = kind == 0 ? "img" : kind == 1 ? "span" : "table";
    for (unsigned index = 0; index < 4; index++) {
        style = radiant_test_atomic_item_style(kind, index, state.padded);
        strbuf_append_format(source, "<%s class=atom id=atom%u style='%s'%s>", tag, index, style->str,
            kind == 0 ? " src='../../test/layout/data/wpt-css-writing-modes/css-writing-modes/support/test-tr.png'" : "");
        strbuf_free(style);
        if (kind == 2) strbuf_append_str(source, "<tr><td></td></tr>");
        if (kind != 0) strbuf_append_format(source, "</%s>", tag);
    }
    strbuf_append_str(source, "</section></main>");
    return source;
}

struct RadiantTestAtomicItem {
    float inline_size, block_size, margin_start, margin_end;
};

inline float radiant_test_pack_atomic_lines(const RadiantTestAtomicItem* items, unsigned count,
                                            float available, bool rtl, const char* align,
                                            const char* mode, Rect* boxes) {
    // independent CSS inline packing: source order, greedy wrapping and top-aligned margin boxes.
    unsigned begin = 0;
    float block_cursor = 0.0f;
    while (begin < count) {
        unsigned end = begin;
        float used = 0.0f, line_height = 0.0f;
        while (end < count) {
            float outer = items[end].inline_size+items[end].margin_start+items[end].margin_end;
            if (end > begin && used+outer > available) break;
            used += outer;
            if (items[end].block_size > line_height) line_height = items[end].block_size;
            end++;
        }
        bool right = (strcmp(align, "start") == 0 && rtl) ||
            (strcmp(align, "end") == 0 && !rtl);
        float offset = strcmp(align, "center") == 0 ? (available-used)/2 : right ? available-used : 0;
        float advance = 0.0f;
        for (unsigned index = begin; index < end; index++) {
            const RadiantTestAtomicItem& item = items[index];
            float u = rtl ? used-advance-item.margin_start-item.inline_size : advance+item.margin_start;
            // Writing Modes 3 section 6.3: vertical-lr line-over is opposite its block-start.
            float b = block_cursor+(strcmp(mode, "vertical-lr") == 0 ? line_height-item.block_size : 0);
            boxes[index] = {offset+u, b, item.inline_size, item.block_size};
            advance += item.inline_size+item.margin_start+item.margin_end;
        }
        block_cursor += line_height;
        begin = end;
    }
    return block_cursor;
}

inline Rect radiant_test_flow_reference_rect(const char* mode, float inline_extent, float block_extent,
                         Rect logical, float origin_x = 0, float origin_y = 0) {
    // independent test frame: x is line-left distance, y is block-start distance.
    if (strcmp(mode, "horizontal-tb") == 0)
        return {origin_x+logical.x, origin_y+logical.y, logical.width, logical.height};
    bool reverse_block = strcmp(mode, "vertical-rl") == 0 || strcmp(mode, "sideways-rl") == 0;
    return {origin_x+(reverse_block ? block_extent-logical.y-logical.height : logical.y),
        origin_y+(strcmp(mode, "sideways-lr") == 0 ? inline_extent-logical.x-logical.width : logical.x),
        logical.height, logical.width};
}

struct RadiantTestInlineHeightState { float available, leading; };
static const RadiantTestInlineHeightState radiant_test_inline_height_states[] = {
    {100,60}, {200,60}, {300,60}, {200,20}, {100,40}, {100,60}
};

inline bool radiant_test_inline_word_is_tall(unsigned index) {
    return index < 6 || index >= 9;
}

struct RadiantTestWordEdges { float before, after, leading; };

inline float radiant_test_pack_word_lines(const RadiantTestWordEdges* edges, unsigned count,
                                          float available, bool rtl, Rect* words) {
    unsigned begin = 0;
    float block_cursor = 0.0f;
    while (begin < count) {
        unsigned end = begin;
        float used = 0.0f, line_height = 20.0f;
        while (end < count) {
            float advance = (end > begin ? 20.0f : 0.0f) + edges[end].before + 80.0f + edges[end].after;
            if (end > begin && used + advance > available) break;
            used += advance;
            if (edges[end].leading > line_height) line_height = edges[end].leading;
            end++;
        }
        float cursor = 0.0f;
        for (unsigned index = begin; index < end; index++) {
            if (index > begin) cursor += 20.0f;
            cursor += edges[index].before;
            // equal Ahem faces share a baseline; half-leading centers their 20px content boxes.
            words[index] = {rtl ? available-cursor-80.0f : cursor,
                block_cursor+(line_height-20.0f)/2.0f, 80.0f, 20.0f};
            cursor += 80.0f;
            cursor += edges[index].after;
        }
        block_cursor += line_height;
        begin = end;
    }
    return block_cursor;
}

inline float radiant_test_pack_inline_words(float available, float leading, bool decorated,
                                            bool rtl, Rect words[15]) {
    RadiantTestWordEdges edges[15];
    for (unsigned index = 0; index < 15; index++) edges[index] = {
        decorated && (index == 0 || index == 9) ? 5.0f : 0.0f,
        decorated && (index == 5 || index == 14) ? 5.0f : 0.0f,
        radiant_test_inline_word_is_tall(index) ? leading : 20.0f};
    return radiant_test_pack_word_lines(edges,15,available,rtl,words);
}

inline float radiant_test_pack_bidi_words(float available, bool rtl, bool reverse, Rect* words,
                                          unsigned count = 6) {
    RadiantTestWordEdges edges[6] = {};
    for (auto& edge : edges) edge.leading = 20.0f;
    float extent = radiant_test_pack_word_lines(edges,count,available,false,words);
    for (unsigned begin = 0; begin < count;) {
        unsigned end = begin+1;
        while (end < count && words[end].y == words[begin].y) end++;
        float used = words[end-1].x+words[end-1].width;
        for (unsigned index = begin; index < end; index++) {
            // RLO reverses the occupied run; paragraph direction controls start alignment.
            if (reverse) words[index].x = used-words[index].x-words[index].width;
            if (rtl) words[index].x += available-used;
        }
        begin = end;
    }
    return extent;
}

struct RadiantTestSliceEdges { float padding_start, padding_end, border_start, border_end; };
inline constexpr RadiantTestSliceEdges radiant_test_slice_edges[] = {
    {0,7,0,0}, {7,0,0,0}, {3,11,0,0}, {0,0,2,9}
};

inline float radiant_test_pack_slice_words(const RadiantTestSliceEdges& edge, float available,
                                           bool rtl, Rect words[6]) {
    RadiantTestWordEdges edges[6] = {};
    for (auto& item : edges) item.leading = 20.0f;
    edges[0].before = edge.padding_start+edge.border_start;
    edges[5].after = edge.padding_end+edge.border_end;
    return radiant_test_pack_word_lines(edges,6,available,rtl,words);
}

inline StrBuf* radiant_test_slice_fixture(const char* mode, bool rtl,
                                          const RadiantTestSliceEdges& edge, bool explicit_slice,
                                          const char* tail = "") {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:1200px;height:1200px;writing-mode:%s}"
        "#result{font:20px/20px Ahem;inline-size:100px;padding:3px;border:2px solid transparent;"
        "direction:%s;unicode-bidi:bidi-override}.decor{padding-inline:%gpx %gpx;"
        "border-inline-start:%gpx solid #060;border-inline-end:%gpx solid #060;color:#060;%s}"
        "</style><section id=result><span class=decor>",mode,rtl ? "rtl" : "ltr",
        edge.padding_start,edge.padding_end,edge.border_start,edge.border_end,
        explicit_slice ? "box-decoration-break:slice" : "");
    for (unsigned index = 0; index < 6; index++)
        strbuf_append_format(source,"%s<span id=word%u>XXXX</span>",index ? " " : "",index);
    strbuf_append_format(source,"%s</span></section>",tail);
    return source;
}

inline StrBuf* radiant_test_inline_height_fixture(const char* mode, bool rtl, bool nested,
                                                  bool decorated, float available, float leading) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:1200px;height:1200px;writing-mode:%s}"
        "#result{font:20px/20px Ahem;inline-size:%gpx;padding:3px;border:2px solid transparent;"
        "direction:%s;unicode-bidi:bidi-override}.high{line-height:%gpx;color:#060;%s}"
        ".word{color:#00f}.high .word{color:#060}%s</style><section id=result>",
        mode, available, rtl ? "rtl" : "ltr", leading,
        decorated ? "padding-inline:4px;border-inline:1px solid #060" : "",
        nested ? ".high .word{line-height:20px}" : "");
    for (unsigned index = 0; index < 15; index++) {
        if (index) strbuf_append_char(source, ' ');
        if (index == 0 || index == 9)
            strbuf_append_format(source, "<span class=high id=high%u>", index == 0 ? 0u : 1u);
        strbuf_append_format(source, "<span class=word id=word%u>XXXX</span>", index);
        if (index == 5 || index == 14) strbuf_append_str(source, "</span>");
    }
    strbuf_append_str(source, "</section>");
    return source;
}


struct RadiantTestOrthogonalOptions {
    const char* outer_mode;
    const char* inner_mode = "vertical-rl";
    bool rtl = false, hidden = false, fixed = false, floating = false, soft_words = false;
    bool second_block = false;
};

struct RadiantTestFlexAxisOptions {
    const char* mode;
    const char* flow;
    bool rtl;
    unsigned spacing;
    float width = 180.0f, height = 160.0f;
};

inline constexpr struct RadiantTestFlexAxisItem { float width, height; const char* color; }
    radiant_test_flex_axis_items[] = {{10,12,"red"},{20,22,"green"},{30,32,"blue"}};

inline StrBuf* radiant_test_flex_axis_fixture(const RadiantTestFlexAxisOptions& options) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>html,body{margin:0;background:white}body{width:300px;height:260px}"
        "#host{display:flex;width:%gpx;height:%gpx;padding:3px;border:2px solid black;"
        "writing-mode:%s;direction:%s;flex-direction:%s;gap:6px;align-items:flex-start}"
        ".item{flex:none;%s}",options.width,options.height,options.mode,options.rtl ? "rtl" : "ltr",
        options.flow,options.spacing == 1 ? "margin:5px 7px 11px 3px" : "");
    if (options.spacing == 2) strbuf_append_format(source,"#item0{margin-%s-start:auto}",
        strncmp(options.flow,"row",3) == 0 ? "inline" : "block");
    strbuf_append_str(source,"</style><div id=result><div id=host>");
    for (unsigned index = 0; index < 3; index++) {
        const auto& item = radiant_test_flex_axis_items[index];
        strbuf_append_format(source,"<div class=item id=item%u style='width:%gpx;height:%gpx;background:%s'></div>",
            index,item.width,item.height,item.color);
    }
    strbuf_append_str(source,"</div></div>");
    return source;
}

inline void radiant_test_flex_axis_boxes(const RadiantTestFlexAxisOptions& options, Rect boxes[3]) {
    bool vertical = strcmp(options.mode,"horizontal-tb") != 0;
    bool row = strncmp(options.flow,"row",3) == 0;
    bool reverse = strstr(options.flow,"reverse") != nullptr;
    bool horizontal = row != vertical;
    bool inline_forward = !(options.rtl != (strcmp(options.mode,"sideways-lr") == 0));
    bool block_forward = strcmp(options.mode,"vertical-rl") != 0 && strcmp(options.mode,"sideways-rl") != 0;
    bool main_forward = (row ? inline_forward : block_forward) != reverse;
    bool cross_forward = row ? block_forward : inline_forward;
    float main_size = horizontal ? options.width : options.height;
    float cross_size = horizontal ? options.height : options.width;
    float left = options.spacing == 1 ? 3.0f : 0.0f, right = options.spacing == 1 ? 7.0f : 0.0f;
    float top = options.spacing == 1 ? 5.0f : 0.0f, bottom = options.spacing == 1 ? 11.0f : 0.0f;
    float before = horizontal ? (main_forward ? left : right) : (main_forward ? top : bottom);
    float after = horizontal ? (main_forward ? right : left) : (main_forward ? bottom : top);
    float cross_start = horizontal ? top : left, cross_end = horizontal ? bottom : right;
    float free = main_size-12.0f-3.0f*(before+after);
    for (const auto& item : radiant_test_flex_axis_items) free -= horizontal ? item.width : item.height;
    float cursor = 0.0f;
    for (unsigned index = 0; index < 3; index++) {
        const auto& item = radiant_test_flex_axis_items[index];
        float main_extent = horizontal ? item.width : item.height;
        float cross_extent = horizontal ? item.height : item.width;
        float start = before, end = after;
        if (options.spacing == 2 && index == 0) {
            // an authored logical-start auto margin moves to main-end when flex direction reverses.
            if (reverse) end += max(free,0.0f);
            else start += max(free,0.0f);
        }
        cursor += start;
        float main = main_forward ? cursor : main_size-cursor-main_extent;
        float cross = cross_forward ? cross_start : cross_size-cross_extent-cross_end;
        boxes[index] = {5.0f+(horizontal ? main : cross),5.0f+(horizontal ? cross : main),item.width,item.height};
        cursor += main_extent+end+6.0f;
    }
}

inline StrBuf* radiant_test_nested_orthogonal_fixture(const RadiantTestOrthogonalOptions& options) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:420px;height:320px}"
        "#host{width:400px;height:300px;writing-mode:%s;direction:%s;font:20px/20px Ahem;background:hotpink}"
        "#middle{writing-mode:%s;width:%s;%s}#inner{writing-mode:%s;background:yellow}"
        "#content,#second{visibility:%s}</style><div id=result><div id=host><div id=middle><div id=inner>"
        "<div id=content>%s</div>%s</div></div></div></div>",
        options.floating ? "horizontal-tb" : options.outer_mode,options.rtl ? "rtl" : "ltr",
        options.floating ? options.outer_mode : "horizontal-tb",options.fixed ? "160px" : "auto",
        options.floating ? "float:right" : "",options.inner_mode,options.hidden ? "hidden" : "visible",
        options.soft_words ? "XX XX<br>XXX XX<br>X XXX<br>XX XXX" : "XXXX<br>XXXXXX<br>XX<br>XXXXX",
        options.second_block ? "<div id=second>X XX</div>" : "");
    return source;
}

struct RadiantTestPositionedInlineOptions {
    const char* mode;
    bool rtl, border_box;
    unsigned insets;
    float before = 0.0f, font = 20.0f, width = 400.0f, height = 300.0f;
    const char* inset_declaration = nullptr;
};

inline constexpr struct RadiantTestLogicalInsetState {
    const char* pair;
    const char* start;
    const char* end;
    const char* variable = nullptr;
    const char* end_override = nullptr;
} radiant_test_logical_inset_states[] = {
    {"10px 20px", "10px", "20px"},
    {"15px", "15px", "15px"},
    {"auto 10%", "auto", "10%"},
    {"5% auto", "5%", "auto"},
    {"calc(5% + 3px) calc(10% - 2px)", "calc(5% + 3px)", "calc(10% - 2px)"},
    {"var(--edges)", "10px", "20px", "10px 20px"},
    {nullptr, "30px", "40px", "30px 40px"},
    {nullptr, "30px", "20px", nullptr, "20px"},
    {"10px 20px", "10px", "20px", nullptr, ""}
};

inline constexpr float radiant_test_positioned_decoration_x = 20.0f;
inline constexpr float radiant_test_positioned_decoration_y = 14.0f;

inline StrBuf* radiant_test_positioned_inline_fixture(const RadiantTestPositionedInlineOptions& options,
                                                     bool paired = false) {
    const char* insets[] = {"inset-inline-start:10px","inset-inline-end:10px",
        "inset-inline-start:10px;inset-inline-end:20px",""};
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:420px;height:320px}"
        "#host{position:relative;width:%gpx;height:%gpx;font:%gpx/%gpx Ahem}"
        "#item%s{position:absolute;writing-mode:%s;direction:%s;inset-block-start:0;%s;"
        "box-sizing:%s;padding:3px 5px 7px 11px;border:2px solid black;background:yellow}"
        "</style><div id=result><div id=host>",options.width,options.height,options.font,options.font,
        paired ? ",#control" : "",options.mode,options.rtl ? "rtl" : "ltr",
        options.inset_declaration ? options.inset_declaration : insets[options.insets],
        options.border_box ? "border-box" : "content-box");
    if (options.before > 0.0f) strbuf_append_format(source,"<div id=before style='height:%gpx'></div>",options.before);
    strbuf_append_str(source,"<div id=item>XX XXXX</div>");
    if (paired) strbuf_append_str(source,"<div id=control>XX XXXX</div>");
    strbuf_append_str(source,"</div></div>");
    return source;
}

inline Rect radiant_test_positioned_inline_box(const RadiantTestPositionedInlineOptions& options) {
    bool vertical = strcmp(options.mode,"horizontal-tb") != 0;
    float containing_inline = vertical ? options.height : options.width;
    float inline_edges = vertical ? radiant_test_positioned_decoration_y : radiant_test_positioned_decoration_x;
    float block_edges = vertical ? radiant_test_positioned_decoration_x : radiant_test_positioned_decoration_y;
    float available = containing_inline - (options.insets == 2 ? 30.0f : options.insets == 3
        ? (vertical ? options.before : 0.0f) : 10.0f);
    // one soft opportunity separates the 2em and 4em words; normal wrapping never splits either word.
    float inline_extent = options.insets == 2 ? available : min(7.0f*options.font+inline_edges,
        max(4.0f*options.font+inline_edges,available));
    float lines = inline_extent-inline_edges < 7.0f*options.font ? 2.0f : 1.0f;
    float block_extent = lines*options.font+block_edges;
    float line_left = options.insets == 1 ? containing_inline-10.0f-inline_extent : 10.0f;
    if (options.rtl) line_left = containing_inline-line_left-inline_extent;
    Rect box = radiant_test_flow_reference_rect(options.mode,containing_inline,
        vertical ? options.width : options.height,Rect{line_left,0,inline_extent,block_extent});
    // auto inline insets use the horizontal parent's physical static position, not the child's inline start.
    if (options.insets == 3) {
        if (vertical) box.y = options.before;
        else box.x = 0.0f;
    }
    return box;
}

inline constexpr struct RadiantTestCenteredInlineContent {
    const char* html;
    float minimum, maximum;
} radiant_test_centered_inline_content[] = {
    {"XX",40,40}, {"XX XXXX",80,140}, {"<span></span> <span></span>",50,120}
};

inline StrBuf* radiant_test_centered_positioned_fixture(const char* mode, bool rtl,
                                                       unsigned kind, unsigned decoration = 0) {
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0}#host{position:relative;display:flow-root;width:200px;height:200px;font:20px/20px Ahem}"
        "#slot{display:flex;align-items:center;height:40px;margin-top:10px}"
        "#item{position:absolute;left:0;width:100px;writing-mode:%s;direction:%s;background:green;%sbox-sizing:%s}"
        "span{display:inline-block;inline-size:50px;block-size:10px}</style>"
        "<div id=result><div id=host><div id=slot><div id=item>%s</div></div></div></div>",
        mode,rtl ? "rtl" : "ltr",decoration
            ? "margin:3px 0;padding:2px 0 4px;border-top:1px solid;border-bottom:1px solid;" : "",
        decoration == 2 ? "border-box" : "content-box",radiant_test_centered_inline_content[kind].html);
    return source;
}

inline StrBuf* radiant_test_positioned_word_reference(const char* mode, bool rtl,
        float available, float extent, const Rect* logical, unsigned count, const char* const* colors) {
    bool vertical = strcmp(mode,"horizontal-tb") != 0;
    StrBuf* reference = strbuf_new();
    strbuf_append_format(reference,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;background:white}body{width:1200px;height:1200px;writing-mode:%s}"
        "section{font:20px/20px Ahem;inline-size:%gpx;block-size:%gpx;padding:3px;"
        "border:2px solid transparent;direction:%s}div{position:relative;inline-size:%gpx;block-size:%gpx}"
        "em{position:absolute;font:20px/20px Ahem;writing-mode:%s}</style><section><div>",
        mode,available,extent,rtl ? "rtl" : "ltr",available,extent,mode);
    for (unsigned index = 0; index < count; index++) {
        Rect box = radiant_test_flow_reference_rect(mode,available,extent,logical[index]);
        strbuf_append_format(reference,
            "<em style='left:%gpx;top:%gpx;width:%gpx;height:%gpx;color:%s'>XXXX</em>",
            box.x,box.y,box.width,box.height,colors ? colors[index] : "#060");
    }
    strbuf_append_format(reference,"</div></section><style>body{width:%gpx;height:%gpx}</style>",
        (vertical ? extent : available)+10.0f,(vertical ? available : extent)+10.0f);
    return reference;
}

struct RadiantTestVerticalOverflowOptions {
    const char* mode;
    bool rtl;
    const char* overflow;
    bool decorated;
    unsigned kind;
};

struct RadiantTestOverflowRecipe {
    const char* text;
    const char* short_text;
    const char* css;
    float inline_em, block_em;
};
inline constexpr RadiantTestOverflowRecipe radiant_test_overflow_recipes[] = {
    {"XXX", "X", "", -1.0f, -1.0f},
    {"XXXX", "X", "inline-size:2em", 2.0f, -1.0f},
    {"XX\nXX", "XX", "block-size:1em;white-space:pre", -1.0f, 1.0f}
};

struct RadiantTestVerticalOverflowState {
    float font, line_height;
    const char* text;
    float natural_inline, natural_block, used_inline, used_block;
};

inline RadiantTestVerticalOverflowState radiant_test_vertical_overflow_state(unsigned kind, unsigned phase) {
    const auto& recipe = radiant_test_overflow_recipes[kind];
    float font = phase == 1 ? 30.0f : 20.0f;
    float line = phase == 1 && kind == 0 ? 40.0f : font;
    const char* text = phase == 1 ? recipe.short_text : recipe.text;
    unsigned line_units = 0, maximum_units = 0, lines = 1;
    for (const char* ch = text; *ch; ch++) {
        if (*ch == '\n') {
            maximum_units = max(maximum_units, line_units);
            line_units = 0;
            lines++;
        } else line_units++;
    }
    maximum_units = max(maximum_units, line_units);
    float natural_inline = maximum_units * font;
    float natural_block = lines * line;
    return {font, line, text, natural_inline, natural_block,
        recipe.inline_em >= 0 ? recipe.inline_em * font : natural_inline,
        recipe.block_em >= 0 ? recipe.block_em * font : natural_block};
}

inline Rect radiant_test_vertical_overflow_box(const RadiantTestVerticalOverflowOptions& options,
                                               const RadiantTestVerticalOverflowState& state) {
    Rect box = radiant_test_flow_reference_rect(options.mode, state.used_inline, state.used_block,
        {0.0f, 0.0f, state.used_inline, state.used_block});
    if (options.decorated) {
        box.width += 22.0f;
        box.height += 14.0f;
    }
    return box;
}

inline StrBuf* radiant_test_vertical_overflow_fixture(const RadiantTestVerticalOverflowOptions& options,
    unsigned phase = 0, float parent_width = -1.0f) {
    auto state = radiant_test_vertical_overflow_state(options.kind, phase);
    StrBuf* source = strbuf_new();
    strbuf_append_format(source,
        "<!doctype html><style>@font-face{font-family:Ahem;src:url('../../test/layout/data/font/Ahem.ttf')}"
        "html,body{margin:0;font:%gpx/%gpx Ahem}#result{display:inline-block;vertical-align:top;"
        "background:#ff0;%s", state.font, state.line_height,
        options.kind ? "width:200px;height:200px;" : "");
    if (parent_width >= 0) strbuf_append_format(source, "width:%gpx;", parent_width);
    strbuf_append_format(source,
        "}#item{writing-mode:%s;direction:%s;overflow:%s;color:blue;%s;%s}"
        "</style><div id=result><div id=item>%s</div></div>", options.mode, options.rtl ? "rtl" : "ltr",
        options.overflow, radiant_test_overflow_recipes[options.kind].css,
        options.decorated ? "padding:1px 3px 5px 7px;border-style:solid;border-width:2px 4px 6px 8px" : "",
        state.text);
    return source;
}
