#include "layout_bidi.hpp"
#include "../lib/log.h"
#include <limits.h>
#include <stdlib.h>

BidiScopeControls bidi_scope_controls(CssEnum mode, CssEnum direction) {
    // css writing modes §2.2: markup contributes these zero-advance codes
    // to the UAX #9 input without changing source text or box ownership.
    const static struct {
        CssEnum mode;
        uint32_t ltr[2], rtl[2], end[2];
        int count;
    } scopes[] = {
        {CSS_VALUE_EMBED, {0x202a, 0}, {0x202b, 0}, {0x202c, 0}, 1},
        {CSS_VALUE_BIDI_OVERRIDE, {0x202d, 0}, {0x202e, 0}, {0x202c, 0}, 1},
        {CSS_VALUE_ISOLATE, {0x2066, 0}, {0x2067, 0}, {0x2069, 0}, 1},
        {CSS_VALUE_ISOLATE_OVERRIDE, {0x2068, 0x202d}, {0x2068, 0x202e}, {0x202c, 0x2069}, 2},
        {CSS_VALUE_PLAINTEXT, {0x2068, 0}, {0x2068, 0}, {0x2069, 0}, 1}
    };
    BidiScopeControls controls = {};
    for (const auto& scope : scopes) {
        if (scope.mode != mode) continue;
        const uint32_t* start = direction == CSS_VALUE_RTL ? scope.rtl : scope.ltr;
        for (int i = 0; i < scope.count; i++) {
            controls.start[i] = start[i];
            controls.end[i] = scope.end[i];
        }
        controls.count = scope.count;
        break;
    }
    return controls;
}

bool bidi_codepoint_triggers_reorder(uint32_t codepoint) {
#if RDT_HAS_FRIBIDI
    FriBidiCharType type = fribidi_get_bidi_type((FriBidiChar)codepoint);
    return !text_codepoint_has_zero_advance(codepoint) &&
           (type == FRIBIDI_TYPE_RTL || type == FRIBIDI_TYPE_AL);
#else
    return !text_codepoint_has_zero_advance(codepoint) &&
           utf_bidi_strong_class(codepoint) == 1;
#endif
}

bool bidi_codepoint_is_format_control(uint32_t codepoint) {
#if RDT_HAS_FRIBIDI
    FriBidiCharType type = fribidi_get_bidi_type((FriBidiChar)codepoint);
    return type == FRIBIDI_TYPE_LRE || type == FRIBIDI_TYPE_RLE ||
           type == FRIBIDI_TYPE_LRO || type == FRIBIDI_TYPE_RLO ||
           type == FRIBIDI_TYPE_PDF || type == FRIBIDI_TYPE_LRI ||
           type == FRIBIDI_TYPE_RLI || type == FRIBIDI_TYPE_FSI ||
           type == FRIBIDI_TYPE_PDI;
#else
    return codepoint == 0x202A || codepoint == 0x202B ||
           codepoint == 0x202C || codepoint == 0x202D ||
           codepoint == 0x202E || codepoint == 0x2066 ||
           codepoint == 0x2067 || codepoint == 0x2068 ||
           codepoint == 0x2069;
#endif
}

uint32_t bidi_source_codepoint(DomNode* source, uint32_t codepoint) {
    codepoint = layout_text_resolve_css_whitespace(codepoint);
    // CSS Text §4 transforms a collapsed segment break before bidi resolution;
    // preserved breaks remain paragraph boundaries rather than line characters.
    return layout_text_is_segment_break(codepoint) &&
        layout_white_space_collapses_newlines(get_white_space_value(source)) ? ' ' : codepoint;
}

template<class Visitor>
static void bidi_walk_paragraph_source(DomNode* node, Visitor& visitor,
                                       bool isolated = false, int depth = 0) {
    if (!node || visitor.done()) return;
    if (depth >= MAX_LAYOUT_DEPTH) { visitor.truncated(); return; }
    if (node->is_text()) {
        DomText* text = node->as_text();
        const unsigned char* start = text->text_data();
        if (!start || !visitor.text_begin(text)) return;
        const unsigned char* cursor = start;
        const unsigned char* end = start + text->length;
        bool preserve_breaks = !layout_white_space_collapses_newlines(get_white_space_value(node));
        bool upright = layout_text_uses_upright_orientation(node);
        while (cursor < end && !visitor.done()) {
            const unsigned char* before = cursor;
            uint32_t cp = 0;
            if (!layout_utf8_next_codepoint(&cursor,end,&cp)) continue;
            bool paragraph_break = cp == 0x2029 ||
                (preserve_breaks && layout_text_is_segment_break(cp));
            visitor.codepoint(text,(size_t)(before-start),(size_t)(cursor-start),
                cp,paragraph_break,upright,isolated);
        }
        visitor.text_end(text);
        return;
    }
    if (!node->is_element()) return;
    DomElement* element = node->as_element();
    DisplayValue display = resolve_display_value(element);
    if (layout_display_is_none(display)) return;
    if (layout_element_is_abs_or_fixed(element) || layout_element_is_floated(element)) {
        visitor.out_of_flow(element);
        return;
    }
    if (element->tag() == MARKUP_NAME_BR) { visitor.boundary(node); return; }
    if (layout_intrinsic_element_is_atomic_inline(element) || node->view_type == RDT_VIEW_MARKER) {
        visitor.object(element);
        return;
    }
    if (display.outer != CSS_VALUE_INLINE && display.outer != CSS_VALUE_CONTENTS) {
        visitor.boundary(node);
        return;
    }
    CssEnum mode = display.outer == CSS_VALUE_CONTENTS ? CSS_VALUE_NORMAL :
        layout_inline_boundary_keyword(element,CSS_PROPERTY_UNICODE_BIDI,
            CSS_VALUE_NORMAL,CSS_VALUE_NORMAL);
    typename Visitor::InlineFrame frame = {};
    if (!visitor.enter_inline(element,mode,&frame)) return;
    bool child_isolated = isolated || layout_unicode_bidi_isolate(mode);
    for (DomNode* child = visitor.children(element); child && !visitor.done(); child = child->next_sibling)
        bidi_walk_paragraph_source(child,visitor,child_isolated,depth+1);
    visitor.leave_inline(&frame);
}

struct BidiParagraphSearch {
    struct InlineFrame {};
    DomText* anchor = nullptr;
    size_t anchor_byte = 0;
    size_t start_byte = 0, end_byte = 0;
    int first_strong = 0;
    int isolate_depth = 0;
    bool contains_anchor = false;
    bool complete = false;

    bool done() const { return complete; }
    void truncated() { complete = true; }
    bool text_begin(DomText*) { return true; }
    void text_end(DomText* text) {
        if (text == anchor && anchor_byte == text->length) contains_anchor = true;
    }
    DomNode* children(DomElement* element) { return element->first_child; }
    bool enter_inline(DomElement*,CssEnum,InlineFrame*) { return true; }
    void leave_inline(InlineFrame*) {}
    void object(DomElement*) {}
    void out_of_flow(DomElement*) {}
    void boundary(DomNode*) {
        if (contains_anchor) complete = true;
        else { first_strong = 0; isolate_depth = 0; }
    }
    void codepoint(DomText* text,size_t byte,size_t end,uint32_t cp,
                   bool paragraph_break,bool upright,bool isolated) {
        if (text == anchor && anchor_byte >= byte && anchor_byte < end) contains_anchor = true;
        if (paragraph_break) {
            if (text == anchor) {
                if (contains_anchor) end_byte = end;
                else start_byte = end;
            }
            boundary(text);
            return;
        }
        // P2 ignores the contents of both CSS and source Unicode isolates.
        if (isolated) return;
        if (upright) {
            if (!isolate_depth && !first_strong) first_strong = -1;
        } else if (cp >= 0x2066 && cp <= 0x2068) isolate_depth++;
        else if (cp == 0x2069 && isolate_depth > 0) isolate_depth--;
        else if (!isolate_depth && !first_strong) first_strong = utf_bidi_strong_class(cp);
    }
};

CssEnum bidi_plaintext_paragraph_direction(DomElement* scope, DomText* anchor,
        size_t anchor_byte, LayoutPlaintextParagraphCache* cache) {
    // upright's strong-LTR policy precedes plaintext P2/P3, including soft-wrapped lines.
    if (layout_text_uses_upright_orientation(scope)) return CSS_VALUE_LTR;
    if (cache && cache->scope == scope && cache->text == anchor &&
        anchor_byte >= cache->start_byte && anchor_byte < cache->end_byte) return cache->direction;
    BidiParagraphSearch search = {};
    search.anchor = anchor;
    search.anchor_byte = anchor_byte;
    search.end_byte = anchor ? (size_t)anchor->length + 1 : 0;
    search.contains_anchor = anchor == nullptr;
    for (DomNode* child = scope->first_child; child && !search.complete; child = child->next_sibling)
        bidi_walk_paragraph_source(child,search);
    CssEnum direction = search.contains_anchor && search.first_strong > 0 ? CSS_VALUE_RTL : CSS_VALUE_LTR;
    if (cache && anchor) {
        cache->scope = lam::up(scope);
        cache->text = lam::up(anchor);
        cache->start_byte = search.start_byte;
        cache->end_byte = search.end_byte;
        cache->direction = direction;
    }
    return direction;
}

CssEnum layout_resolve_plaintext_direction(DomElement* element, CssEnum fallback) {
    return element ? bidi_plaintext_paragraph_direction(element, nullptr, 0) : fallback;
}


#if RDT_HAS_FRIBIDI
struct BidiSourceToken {
    DomNode* source;
    size_t byte;
    uint32_t codepoint;
    int paragraph;
    bool upright;
};
struct BidiSourceIndex {
    DomNode* source;
    size_t byte;
    BidiParagraphSourceLevel value;
};
struct BidiSourceParagraph {
    size_t start, end;
    CssEnum direction;
    bool has_reorder;
};

// tier-3: source pointers are borrowed from the document; all arrays belong to pass_arena.
struct LayoutBidiParagraphCache {
    LayoutBidiParagraphCache* next;
    DomElement* scope;
    CssEnum direction, root_mode;
    BidiSourceIndex* index;
    size_t index_count;
    BidiSourceParagraph* paragraphs;
    size_t paragraph_count;
};

struct BidiParagraphCollector {
    struct InlineFrame {
        InlineFrame* previous;
        BidiScopeControls controls;
    };
    lam::ArrayList<BidiSourceToken> tokens;
    lam::ArrayList<BidiSourceParagraph> paragraphs;
    InlineFrame* frame = nullptr;
    bool valid = true;
    int paragraph = 0;
    CssEnum direction;

    explicit BidiParagraphCollector(CssEnum dir) : direction(dir) {
        valid = paragraphs.append({0,0,dir,false});
    }
    bool done() const { return !valid; }
    void truncated() { valid = false; }
    void emit(DomNode* node,size_t byte,uint32_t cp,bool upright = false) {
        if (!valid) return;
        valid = tokens.append({node,byte,cp,paragraph,upright});
        paragraphs[paragraph].has_reorder |= bidi_codepoint_triggers_reorder(cp) ||
            bidi_codepoint_is_format_control(cp);
    }
    void controls(const uint32_t* codes,int count) {
        for (int i = 0; i < count; i++) emit(nullptr,0,codes[i]);
    }
    void open(InlineFrame* current,CssEnum mode,CssEnum dir) {
        current->previous = frame;
        current->controls = bidi_scope_controls(mode,dir);
        frame = current;
        controls(current->controls.start,current->controls.count);
    }
    void leave_inline(InlineFrame* current) {
        controls(current->controls.end,current->controls.count);
        frame = current->previous;
    }
    void reopen(InlineFrame* current) {
        if (!current) return;
        reopen(current->previous);
        controls(current->controls.start,current->controls.count);
    }
    void paragraph_break(DomNode* node,size_t byte) {
        // CSS Writing Modes §2.4.4 reopens markup scopes, while raw Unicode state terminates.
        for (InlineFrame* current = frame; current; current = current->previous)
            controls(current->controls.end,current->controls.count);
        emit(node,byte,0x2029);
        paragraphs[paragraph].end = tokens.size();
        paragraph++;
        valid = valid && paragraphs.append({tokens.size(),0,direction,false});
        reopen(frame);
    }
    void boundary(DomNode* node) { paragraph_break(node,0); }
    bool text_begin(DomText* text) {
        if (!layout_text_combine_upright_applies(text)) return true;
        emit(text,0,0xFFFC,true);
        return false;
    }
    void text_end(DomText*) {}
    void codepoint(DomText* text,size_t byte,size_t,uint32_t cp,
                   bool paragraph_boundary,bool upright,bool) {
        if (paragraph_boundary) { paragraph_break(text,byte); return; }
        cp = bidi_source_codepoint(text,cp);
        if (layout_white_space_collapses(get_white_space_value(text)) &&
            (cp == '\n' || cp == '\t')) cp = ' ';
        emit(text,byte,cp,upright);
    }
    DomNode* children(DomElement* element) { return layout_render_child_list(element); }
    bool enter_inline(DomElement* element,CssEnum mode,InlineFrame* current) {
        if (layout_is_shadow_slot(element)) { valid = false; return false; }
        if (layout_ruby_annotation_node(element) && element->parent &&
            resolve_display_value(element->parent).inner == CSS_VALUE_RUBY) return false;
        if (resolve_display_value(element).outer != CSS_VALUE_CONTENTS && element->specified_style &&
            style_tree_get_declaration(element->specified_style,CSS_PROPERTY_UNICODE_BIDI)) {
            mode = css_prop_compute_cascaded_keyword(element,CSS_PROPERTY_UNICODE_BIDI,CSS_VALUE_NORMAL);
        }
        if (layout_unicode_bidi_isolate(mode)) {
            object(element);
            return false;
        }
        open(current,mode,layout_text_used_direction(element));
        return true;
    }
    void object(DomElement* element) {
        uint32_t cp = 0xFFFC;
        if (element->view_type == RDT_VIEW_MARKER) {
            MarkerProp* marker = element->marker_prop();
            if (!marker || marker->is_outside || marker->width <= 0.0f) return;
            cp = direction == CSS_VALUE_RTL ? 0x0627 : 0x0041;
        }
        emit(element,0,cp);
    }
    void out_of_flow(DomElement* element) {
        if (layout_element_is_abs_or_fixed(element) && element->position &&
            element->positionp()->static_inline_bidi_candidate)
            emit(element,0,0xFFFC);
    }
};

template<class T>
static T* bidi_pass_array(Arena* arena,size_t count) {
    size_t bytes = 0;
    return math_checked_mul(count,sizeof(T),&bytes) ? (T*)arena_alloc(arena,bytes) : nullptr;
}

static int bidi_compare_source_index(const void* left,const void* right) {
    const BidiSourceIndex* a = (const BidiSourceIndex*)left;
    const BidiSourceIndex* b = (const BidiSourceIndex*)right;
    uintptr_t node_a = (uintptr_t)a->source,node_b = (uintptr_t)b->source;
    if (node_a != node_b) return node_a < node_b ? -1 : 1;
    return a->byte == b->byte ? 0 : a->byte < b->byte ? -1 : 1;
}

LayoutBidiParagraphCache* bidi_source_paragraphs(LayoutContext* context,
        DomElement* source,CssEnum direction,CssEnum root_mode) {
    if (!context || !context->pass_arena || !source) return nullptr;
    for (LayoutBidiParagraphCache* entry = context->block.bidi_paragraphs; entry; entry = entry->next)
        if (entry->scope == source && entry->direction == direction && entry->root_mode == root_mode)
            return entry;
    BidiParagraphCollector collector(direction);
    BidiParagraphCollector::InlineFrame root = {};
    bool override = root_mode == CSS_VALUE_BIDI_OVERRIDE || root_mode == CSS_VALUE_ISOLATE_OVERRIDE;
    collector.open(&root,override ? CSS_VALUE_BIDI_OVERRIDE : CSS_VALUE_NORMAL,direction);
    for (DomNode* child = layout_render_child_list(source); child && !collector.done(); child = child->next_sibling)
        bidi_walk_paragraph_source(child,collector);
    collector.leave_inline(&root);
    if (!collector.valid) return nullptr;
    collector.paragraphs[collector.paragraph].end = collector.tokens.size();
    if (!collector.valid || collector.tokens.size() == 0 || collector.tokens.size() > INT_MAX)
        return nullptr;

    // resolving paragraphs uses line-local scratch; only copied levels/indexes survive in the pass arena.
    ScratchScope scratch(&context->scratch);
    size_t count = collector.tokens.size();
    FriBidiChar* logical = scratch.array<FriBidiChar>(count);
    FriBidiCharType* types = scratch.array<FriBidiCharType>(count);
    FriBidiBracketType* brackets = scratch.array<FriBidiBracketType>(count);
    FriBidiLevel* levels = scratch.array<FriBidiLevel>(count);
    if (!logical || !types || !brackets || !levels) return nullptr;
    for (size_t i = 0; i < count; i++) logical[i] = collector.tokens[i].codepoint;
    int length = (int)count; // INT_CAST_OK: bounded Unicode codepoint count, not geometry
    fribidi_get_bidi_types(logical,length,types);
    for (size_t i = 0; i < count; i++) if (collector.tokens[i].upright) types[i] = FRIBIDI_TYPE_LTR;
    fribidi_get_bracket_types(logical,length,types,brackets);
    for (size_t i = 0; i < collector.paragraphs.size(); i++) {
        BidiSourceParagraph& paragraph = collector.paragraphs[i];
        if (paragraph.start == paragraph.end) continue;
        FriBidiParType base = root_mode == CSS_VALUE_PLAINTEXT &&
            !layout_text_uses_upright_orientation(source) ? FRIBIDI_PAR_ON :
            direction == CSS_VALUE_RTL ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
        int size = (int)(paragraph.end-paragraph.start); // INT_CAST_OK: bounded Unicode codepoint count
        if (!fribidi_get_par_embedding_levels_ex(types+paragraph.start,brackets+paragraph.start,
                size,&base,levels+paragraph.start)) return nullptr;
        paragraph.direction = base == FRIBIDI_PAR_RTL ? CSS_VALUE_RTL : CSS_VALUE_LTR;
    }
    lam::ArrayList<BidiSourceIndex> index;
    for (size_t i = 0; i < count; i++) {
        const BidiSourceToken& token = collector.tokens[i];
        if (token.source && !index.append({token.source,token.byte,
                {types[i],levels[i],token.paragraph}})) return nullptr;
    }
    LayoutBidiParagraphCache* result = (LayoutBidiParagraphCache*)arena_calloc(
        context->pass_arena,sizeof(LayoutBidiParagraphCache));
    if (!result) return nullptr;
    result->index = bidi_pass_array<BidiSourceIndex>(context->pass_arena,index.size());
    result->paragraphs = bidi_pass_array<BidiSourceParagraph>(context->pass_arena,collector.paragraphs.size());
    if (!result->index || !result->paragraphs) return nullptr;
    memcpy(result->index,index.data(),index.size()*sizeof(BidiSourceIndex));
    memcpy(result->paragraphs,collector.paragraphs.data(),collector.paragraphs.size()*sizeof(BidiSourceParagraph));
    result->index_count = index.size();
    result->paragraph_count = collector.paragraphs.size();
    result->scope = source;
    result->direction = direction;
    result->root_mode = root_mode;
    qsort(result->index,result->index_count,sizeof(BidiSourceIndex),bidi_compare_source_index);
    result->next = context->block.bidi_paragraphs;
    context->block.bidi_paragraphs = lam::up(result);
    return result;
}

const BidiParagraphSourceLevel* bidi_source_level(const LayoutBidiParagraphCache* cache,
        DomNode* source,size_t byte) {
    if (!cache || !source) return nullptr;
    BidiSourceIndex key = {source,byte,{}};
    const BidiSourceIndex* entry = (const BidiSourceIndex*)bsearch(&key,cache->index,
        cache->index_count,sizeof(BidiSourceIndex),bidi_compare_source_index);
    return entry ? &entry->value : nullptr;
}

bool bidi_source_paragraph_has_reorder(const LayoutBidiParagraphCache* cache,int paragraph) {
    return cache && paragraph >= 0 && (size_t)paragraph < cache->paragraph_count &&
        cache->paragraphs[paragraph].has_reorder;
}

CssEnum bidi_source_paragraph_direction(const LayoutBidiParagraphCache* cache,int paragraph) {
    return cache && paragraph >= 0 && (size_t)paragraph < cache->paragraph_count
        ? cache->paragraphs[paragraph].direction : CSS_VALUE_LTR;
}
#else
LayoutBidiParagraphCache* bidi_source_paragraphs(LayoutContext*,DomElement*,CssEnum,CssEnum) { return nullptr; }
const BidiParagraphSourceLevel* bidi_source_level(const LayoutBidiParagraphCache*,DomNode*,size_t) { return nullptr; }
bool bidi_source_paragraph_has_reorder(const LayoutBidiParagraphCache*,int) { return false; }
CssEnum bidi_source_paragraph_direction(const LayoutBidiParagraphCache*,int) { return CSS_VALUE_LTR; }
#endif
