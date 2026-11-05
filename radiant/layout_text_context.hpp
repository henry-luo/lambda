#pragma once

#include <stddef.h>

struct LayoutContext;
struct DomNode;

// borrows one immutable source span for a single text walk; nested measurements restore their caller.
struct LayoutTextShapingScope {
    LayoutContext* context;
    LayoutTextShapingScope* previous;
    DomNode* node;
    const unsigned char* source_start;
    const unsigned char* source_end;
    const unsigned char* word_start = nullptr;
    const unsigned char* word_end = nullptr;
    bool cursive = false;
    size_t requests = 0, hits = 0;

    LayoutTextShapingScope(LayoutContext* context, DomNode* node,
        const unsigned char* source_start, const unsigned char* source_end);
    ~LayoutTextShapingScope();
    LayoutTextShapingScope(const LayoutTextShapingScope&) = delete;
    LayoutTextShapingScope& operator=(const LayoutTextShapingScope&) = delete;
};

// byte range of the shaping run adjoining the selected edge of a UTF-8 slice.
void layout_text_shaping_context_slice(const char* source, size_t length,
    bool forward, bool cursive, size_t* first, size_t* last);
