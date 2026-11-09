#pragma once
#include "radiant/export_runtime_host.hpp"

DomDocument* lambda_radiant_load_export_transform(RenderExportSession* session, int layout_width,
    int layout_height, const char* document_file, const LambdaDocumentTransformConfig* transform,
    const LambdaDocumentTransformOption* options, int option_count, InputResourcePolicy policy);
