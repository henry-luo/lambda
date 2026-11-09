#include "export_transform_adapter.hpp"
#include "radiant/render.hpp"
#include "radiant/radiant.hpp"
#include "../../runtime/transpiler.hpp"
#include "lambda/input/css/dom_lifecycle.hpp"
#include "lib/mem_factory.h"
#include "lib/url.h"
#include "lib/log.h"
#include <stdio.h>
#include <string.h>

DomDocument* lambda_radiant_load_export_transform(RenderExportSession* session, int layout_width,
    int layout_height, const char* document_file, const LambdaDocumentTransformConfig* transform,
    const LambdaDocumentTransformOption* options, int option_count, InputResourcePolicy policy) {
    if (!document_file || !transform) {
        return nullptr;
    }
    Pool* pool = mem_pool_create(mem_context_process(MEM_ROLE_RENDER), MEM_ROLE_LAYOUT, "render.document_transform");
    if (!pool) return nullptr;
    Url* document_url = url_parse_with_base(document_file, session->base_url);
    if (!document_url) {
        pool_destroy(pool);
        return nullptr;
    }
    char text_width_px[32];
    LambdaDocumentTransformOption tikz_option = {};
    const LambdaDocumentTransformOption* transform_options = options;
    int transform_option_count = option_count;
    if (strcmp(transform->input_type, "tikz") == 0) {
        snprintf(text_width_px, sizeof(text_width_px), "%d", layout_width);
        tikz_option = {"text_width_px", LAMBDA_DOCUMENT_TRANSFORM_OPTION_STRING,
            text_width_px, false};
        transform_options = &tikz_option;
        transform_option_count = 1;
    }
    DomDocument* doc = load_lambda_document_transform_doc(document_url,
        transform, transform_options, transform_option_count,
        layout_width, layout_height, pool, policy,
        session->print_media);
    if (!doc) {
        url_destroy(document_url);
        pool_destroy(pool);
        return nullptr;
    }
    if (!dom_document_finalize_loader_pool(doc, pool)) {
        log_error("render document transform: could not transfer loader pool");
        free_document(doc);
        return nullptr;
    }
    return doc;
}
