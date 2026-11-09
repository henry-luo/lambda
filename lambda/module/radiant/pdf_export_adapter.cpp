#include "loader_runtime_adapter.hpp"
// The Lambda writer borrows a committed edition and owns its capability lifetime (D6.4.1).
#include "pdf_export_adapter.hpp"
#include "radiant_pdf_source.hpp"
#include "radiant/render.hpp"
#include "radiant/layout.hpp"
#include "radiant/event.hpp"
#include "radiant/view_tree_model.hpp"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/interp.hpp"
#include "../../runtime/runtime-state.h"
#include "../../io/mark_output_builder.hpp"
#include "lib/file.h"
#include "lib/log.h"

extern "C" Item vmap_new();

bool lambda_radiant_export_pdf(RenderExportSession* session, const char* filename,
        const ViewPageSelection* selection) {
    if (!session || !filename || !session->document || !session->ui_context) return false;
    Runtime* runtime = context ? context->runtime : nullptr;
    if (!runtime) {
        // the UI owns cached modules across batch jobs; no Items cross evaluator lifetimes (D6.4.1).
        if (context && context->execution_depth) return false;
        runtime = ui_context_loader_runtime(session->ui_context);
        if (!runtime) return false;
        EvalContext* existing = runtime_get_eval_context(runtime);
        // runtime_init reserves the owner before its first package initializes the heap.
        if (existing && existing->heap) {
            if (!radiant_eval_context_switch(existing)) return false;
        } else {
            Script* package = nullptr;
            if (!run_lambda_package_module(runtime, "lambda.doc.pdf.output", &package) || !package) {
                log_error("PDF_LAMBDA_EXPORT: cannot initialize PDF package"); return false;
            }
        }
    }
    EvalContext* owner = runtime_get_eval_context(runtime);
    if (!owner || owner != context) return false;
    RuntimeExecutionScope execution(owner);
    RootFrame roots(3);
    Rooted<Item> source(roots, ItemNull), options(roots, vmap_new()), file(roots, ItemNull);
    BinarySchemaOwner* lifetime = binary_schema_owner_create();
    if (!lifetime) return false;
    struct CapabilityCleanup {
        BinarySchemaOwner* value;
        ~CapabilityCleanup() { binary_schema_owner_invalidate(value); binary_schema_owner_release(value); }
    } capability_cleanup = {lifetime};
    RadiantPdfSource native = {session, selection};
    source.set(radiant_pdf_source_view(&native, lifetime));
    Item args[] = {source.get(), options.get()};
    file.set(interp_call_package_export(runtime, "lambda.doc.pdf.output", "render_view", args, 2));
    const char* bytes = nullptr; size_t size = 0;
    if (!virtual_output_bytes(file.get(), &bytes, &size)) {
        log_error("PDF_LAMBDA_EXPORT: functional PDF generation failed"); return false;
    }
    if (write_binary_file_atomic(filename, bytes, size) != 0) {
        log_error("PDF_LAMBDA_EXPORT: cannot save %s", filename); return false;
    }
    log_info("PDF_LAMBDA_EXPORT: saved %zu bytes to %s", size, filename);
    return true;
}
