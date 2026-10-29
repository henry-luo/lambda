#pragma once

#include "../radiant/view.hpp"
#include "../lib/strbuf.h"
#include <string.h>

struct RadiantTestBidiScope { const char* code; const char* css; const char* direction; bool reverse; };
inline constexpr RadiantTestBidiScope radiant_test_bidi_scopes[] = {
    {"202E","bidi-override","rtl",true}, {"202D","bidi-override","ltr",false},
    {"202B","embed","rtl",false}, {"202A","embed","ltr",false}
};

struct RadiantTestBidiFixtureOptions {
    bool word_spaces = false, grouped_text = false, preserve_breaks = false;
    const char* boundary = nullptr;
    const char* word_style = "";
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
        unsigned group_size = options.grouped_text ? 3u : 1u;
        for (unsigned index = 0; index < 6; index++) {
            if (index == 3 && options.boundary) strbuf_append_str(source,options.boundary);
            else if (index && !options.word_spaces) strbuf_append_char(source,' ');
            if (index % group_size == 0)
                strbuf_append_format(source,"<i id=%s-word%u style='color:%s;%s'>",
                    raw ? "raw" : "css",index/group_size,(index/group_size) % 2 ? "#00f" : "#060",options.word_style);
            strbuf_append_str(source,"XXXX");
            if (options.word_spaces && index+1 < 6) strbuf_append_char(source,' ');
            if ((index+1) % group_size == 0) strbuf_append_str(source,"</i>");
        }
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
