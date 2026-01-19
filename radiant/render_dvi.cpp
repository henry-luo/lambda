// render_dvi.cpp - Render LaTeX to DVI format
//
// Renders LaTeX documents to DVI (Device Independent) format
// using the unified TeX typesetting pipeline.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
extern "C" {
#include "../lib/log.h"
#include "../lib/url.h"
#include "../lib/file.h"
#include "../lib/mempool.h"
#include "../lib/arena.h"
}
#include "../lambda/input/input.hpp"
#include "../lambda/tex/tex_node.hpp"
#include "../lambda/tex/tex_tfm.hpp"
#include "../lambda/tex/tex_linebreak.hpp"
#include "../lambda/tex/tex_pagebreak.hpp"
#include "../lambda/tex/tex_dvi_out.hpp"
#include "../lambda/tex/tex_latex_bridge.hpp"
#include "../lambda/tex/tex_document_model.hpp"

/**
 * Render LaTeX file to DVI format using the unified pipeline
 *
 * @param latex_file Path to input LaTeX file
 * @param dvi_file Path to output DVI file
 * @return 0 on success, non-zero on error
 */
int render_latex_to_dvi(const char* latex_file, const char* dvi_file) {
    log_debug("render_latex_to_dvi (unified) called with latex_file='%s', dvi_file='%s'",
              latex_file, dvi_file);

    auto total_start = std::chrono::high_resolution_clock::now();

    // Create memory pool
    Pool* pool = pool_create();
    if (!pool) {
        log_error("Failed to create memory pool");
        return 1;
    }

    // Get current directory for URL resolution
    Url* cwd = get_current_dir();
    if (!cwd) {
        log_error("Could not get current directory");
        pool_destroy(pool);
        return 1;
    }

    // Parse LaTeX file URL
    Url* latex_url = url_parse_with_base(latex_file, cwd);
    url_destroy(cwd);
    if (!latex_url) {
        log_error("Failed to parse LaTeX URL: %s", latex_file);
        pool_destroy(pool);
        return 1;
    }

    // Step 1: Read LaTeX file
    auto step1_start = std::chrono::high_resolution_clock::now();

    char* latex_filepath = url_to_local_path(latex_url);
    char* latex_content = read_text_file(latex_filepath);
    if (!latex_content) {
        log_error("Failed to read LaTeX file: %s", latex_filepath);
        pool_destroy(pool);
        return 1;
    }
    size_t latex_len = strlen(latex_content);

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Read LaTeX file: %.1fms",
             std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    // Step 2: Create arena and font manager
    auto step2_start = std::chrono::high_resolution_clock::now();

    Arena* arena = arena_create_default(pool);
    if (!arena) {
        log_error("Failed to create arena for TeX typesetting");
        free(latex_content);
        pool_destroy(pool);
        return 1;
    }

    tex::TFMFontManager* fonts = tex::create_font_manager(arena);

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Setup arena/fonts: %.1fms",
             std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    // Step 3: Parse LaTeX using unified document model
    auto step3_start = std::chrono::high_resolution_clock::now();

    tex::TexDocumentModel* doc_model = tex::doc_model_from_string(
        latex_content, latex_len, arena, fonts
    );
    free(latex_content);

    if (!doc_model || !doc_model->root) {
        log_error("Failed to parse LaTeX document: %s", latex_filepath);
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - Parse to document model: %.1fms",
             std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 4: Typeset with line and page breaking using unified pipeline
    auto step4_start = std::chrono::high_resolution_clock::now();

    // Create LaTeX context
    tex::LaTeXContext ctx = tex::LaTeXContext::create(arena, fonts, "article");

    // Set page dimensions to match LaTeX article class defaults
    ctx.doc_ctx.page_width = 612.0f;   // 8.5 inches
    ctx.doc_ctx.page_height = 795.0f;  // 11 inches
    ctx.doc_ctx.margin_left = 72.0f;   // 1 inch
    ctx.doc_ctx.margin_right = 72.0f;
    ctx.doc_ctx.margin_top = 72.0f;
    ctx.doc_ctx.margin_bottom = 72.0f;
    ctx.doc_ctx.text_width = 345.0f;   // LaTeX article class default
    ctx.doc_ctx.text_height = 550.0f;  // LaTeX article class default

    // Configure line breaking parameters
    tex::LineBreakParams line_params = tex::LineBreakParams::defaults();
    line_params.hsize = ctx.doc_ctx.text_width;

    // Configure page breaking parameters
    tex::PageBreakParams page_params = tex::PageBreakParams::defaults();
    page_params.page_height = ctx.doc_ctx.text_height;

    // Use unified typesetting pipeline
    tex::TexNode* document = tex::doc_model_typeset(
        doc_model, arena, ctx, line_params, page_params
    );

    if (!document) {
        log_error("Failed to typeset document using unified pipeline");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - Typeset (unified pipeline): %.1fms",
             std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 5: Write to DVI file
    auto step5_start = std::chrono::high_resolution_clock::now();

    tex::DVIParams dvi_params = tex::DVIParams::defaults();
    dvi_params.comment = "Lambda Script TeX Output (Unified Pipeline)";

    // Use write_dvi_page for single-page output (or the document VList)
    bool success = tex::write_dvi_page(
        dvi_file,
        document,
        fonts,
        arena,
        dvi_params
    );

    if (!success) {
        log_error("Failed to write DVI file: %s", dvi_file);
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    auto step5_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 5 - Write DVI: %.1fms",
             std::chrono::duration<double, std::milli>(step5_end - step5_start).count());

    // Cleanup
    arena_destroy(arena);
    pool_destroy(pool);

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] render_latex_to_dvi total: %.1fms",
             std::chrono::duration<double, std::milli>(total_end - total_start).count());

    log_info("Successfully rendered LaTeX to DVI (unified pipeline): %s", dvi_file);
    return 0;
}
