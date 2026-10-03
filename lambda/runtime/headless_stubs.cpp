// headless_stubs.cpp
// Stub implementations for the Radiant functions the Lambda runtime reaches
// Only compiled into lambda-cli.exe (headless build without Radiant engine)

#ifdef LAMBDA_HEADLESS

#include "../lambda-data.hpp"
#include "transpiler.hpp"
#include "../../lib/log.h"

// without Radiant there is no SVG rasterizer to resolve images; keep the SVG as-is
extern "C" Item fn_pdf_register_svg_image_resolver(Item svg_item, Item pdf_item) {
    (void)pdf_item;
    return svg_item;
}

// runner teardown reaches these only when runtime->dom_doc is set, which only
// the Radiant document loaders do
struct DomDocument;
void free_document(DomDocument* doc) {
    (void)doc;
}

extern "C" bool radiant_eval_context_switch(EvalContext* target) {
    (void)target;
    log_error("headless: radiant_eval_context_switch called without Radiant");
    return false;
}

#endif // LAMBDA_HEADLESS
