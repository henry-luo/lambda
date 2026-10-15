#pragma once
#include "../../runtime/binary_schema.hpp"
#include "../../../radiant/render.hpp"

// Host-only capability: Lambda cannot inspect or manufacture its session pointer.
struct RadiantPdfSource {
    RenderExportSession* session;
    const ViewPageSelection* selection;
};
Item radiant_pdf_source_view(const RadiantPdfSource*, BinarySchemaOwner*);
