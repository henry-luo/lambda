#pragma once
#include "page_document.hpp"
#include "typeset.hpp"

constexpr const char* RADIANT_FO_NAMESPACE = "http://www.w3.org/1999/XSL/Format";

struct RadiantFoSourceSpan {
    const Element* source;
    size_t start, end;
    RadiantFoSourceSpan* next;
};
using RadiantFoOrigin = RadiantSourceOrigin;
struct RadiantFoOptions {
    size_t max_nodes, max_depth;
    const RadiantFoSourceSpan* spans;
};
struct RadiantFoDiagnostic {
    TypesetStatus status;
    DomNodeRef source;
    const char* qname;
    const char* property;
    const char* reason;
    size_t start, end;
    bool has_range;
};
struct RadiantFoTranslation {
    Element* root;
    RadiantFoOrigin* origins;
    RadiantFoDiagnostic diagnostic;
    size_t node_count;
};

// translation creates real source Mark; pagination and painting remain common Radiant services.
RadiantFoOptions radiant_fo_options_default();
RadiantFoTranslation* radiant_fo_translate(DomDocument* owner, DomElement* source, const RadiantFoOptions* options);
