#pragma once

#include "layout.hpp"

#if __has_include(<fribidi/fribidi.h>)
#include <fribidi/fribidi.h>
#define RDT_HAS_FRIBIDI 1
#else
#define RDT_HAS_FRIBIDI 0
#endif

struct BidiScopeControls {
    uint32_t start[2], end[2];
    int count;
};

BidiScopeControls bidi_scope_controls(CssEnum mode, CssEnum direction);
bool bidi_codepoint_triggers_reorder(uint32_t codepoint);
bool bidi_codepoint_is_format_control(uint32_t codepoint);
uint32_t bidi_source_codepoint(DomNode* source, uint32_t codepoint);
CssEnum bidi_plaintext_paragraph_direction(DomElement* scope, DomText* anchor,
    size_t anchor_byte, LayoutPlaintextParagraphCache* cache = nullptr);

struct BidiParagraphSourceLevel {
    uint32_t type;
    int level, paragraph;
};

LayoutBidiParagraphCache* bidi_source_paragraphs(LayoutContext* context,
    DomElement* scope, CssEnum direction, CssEnum root_mode);
const BidiParagraphSourceLevel* bidi_source_level(const LayoutBidiParagraphCache* cache,
    DomNode* source, size_t byte);
bool bidi_source_paragraph_has_reorder(const LayoutBidiParagraphCache* cache, int paragraph);
CssEnum bidi_source_paragraph_direction(const LayoutBidiParagraphCache* cache, int paragraph);
