#pragma once
#include "../../lib/rdt_geometry_types.h"
#include <stdbool.h>

struct DomDocument;
struct DomElement;
struct RdtLogicalPoint;

typedef bool (*SvgPathContainsFn)(const RdtPath*, const RdtMatrix*, RdtFillRule, const RdtLogicalPoint*);

// synchronous source scopes borrow their previous document/instance/registry; they transfer no ownership.
struct DomSvgSourceContext {
    DomDocument* document;
    DomElement* instance;
    void* registry;
};
