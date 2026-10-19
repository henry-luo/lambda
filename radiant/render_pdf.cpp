// PDF generation belongs to the pure Lambda package (S2.6.6v3).
// This adapter lends a committed edition and performs only the final atomic save.
#include "render.hpp"
#include "layout.hpp"
#include "event.hpp"
#include "view_tree_model.hpp"
#include "../lambda/module/radiant/radiant_pdf_source.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/interp.hpp"
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/io/mark_output_builder.hpp"
#include "../lib/file.h"
#include "../lib/log.h"

extern "C" Item vmap_new();

static bool render_lambda_pdf(RenderExportSession* session, const char* filename,
        const ViewPageSelection* selection) {
    if (!session || !filename || !session->document || !session->ui_context) return false;
    Runtime* runtime = context ? context->runtime : nullptr;
    if (!runtime) {
        // the UI owns cached modules across batch jobs; no Items cross evaluator lifetimes (D5.4.1).
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

bool render_secondary_view_to_pdf(ViewTree* tree, const char* filename,
        const ViewPageSelection* selection, UiContext* ui) {
    if (!tree || !tree->model || !tree->model->committed || !view_tree_model_source_valid(tree) ||
        tree->model->environment.presentation != VIEW_PRESENTATION_PAGED) return false;
    log_info("PDF_LAMBDA_SOURCE: physical pages=%zu", tree->model->page_count);
    RenderExportSession session = {};
    session.document = lam::up(tree->model->document.get());
    // embedding callers may omit the host already retained by the edition environment.
    session.ui_context = lam::up(ui ? ui : tree->model->environment.ui_context.get());
    session.paged_view = lam::up(tree); session.output_scale = 1;
    return render_lambda_pdf(&session, filename, selection);
}

int render_export_session_to_pdf(RenderExportSession* session, const char* filename) {
    return render_lambda_pdf(session, filename, nullptr) ? 0 : 1;
}

int render_uicontext_to_pdf(UiContext* ui, const char* filename) {
    if (!ui || !ui->document || !ui->document->view_tree) return 1;
    // borrow the settled live edition; capture must not reload scripts or clocks.
    RenderExportSession session = {};
    session.ui_context = lam::up(ui);
    session.document = lam::up(ui->document.get());
    session.output_scale = ui_context_raster_scale(ui);
    session.content_width = ui->viewport_width;
    session.content_height = ui->viewport_height;
    calculate_content_bounds(ui->document->view_tree->root,
        &session.content_width, &session.content_height);
    session.content_width += 50;
    session.content_height += 50;
    return render_export_session_to_pdf(&session, filename);
}

int render_html_to_pdf(const char* source, const char* filename, int width,
        int height, float scale, bool paged, float device_scale) {
    RenderExportSession session = {};
    if (!render_export_session_begin(&session, source, width, height, 800, 1200, scale, true, paged, device_scale)) return 1;
    int status = render_export_session_to_pdf(&session, filename);
    render_export_session_end(&session); return status;
}

int render_document_transform_to_pdf(const char* source, const LambdaDocumentTransformConfig* transform,
        const LambdaDocumentTransformOption* options, int count, const char* filename,
        int width, int height, float scale, bool paged, float device_scale) {
    RenderExportSession session = {};
    if (!render_export_session_begin_document_transform(&session, source, transform, options, count,
            width, height, 800, 1200, scale, device_scale, false, paged, true)) return 1;
    int status = render_export_session_to_pdf(&session, filename);
    render_export_session_end(&session); return status;
}
