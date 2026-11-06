#pragma once

struct DomDocument;
struct Runtime;

bool lambda_loader_runtime_host_register();
Runtime* layout_document_package_runtime(DomDocument* document, const char* package_module);
