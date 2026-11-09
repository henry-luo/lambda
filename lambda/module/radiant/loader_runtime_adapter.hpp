#pragma once

struct DomDocument;
struct Runtime;
struct EvalContext;
struct UiContext;

bool lambda_loader_runtime_host_register();
Runtime* layout_document_package_runtime(DomDocument* document, const char* package_module);

// Transfer the evaluator at a quiescent document boundary.
extern "C" bool radiant_eval_context_switch(EvalContext* target);
bool radiant_bind_document_script_host(UiContext* ui, DomDocument* document);
