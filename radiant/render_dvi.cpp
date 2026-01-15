// render_dvi.cpp - Render LaTeX to DVI format
//
// Renders LaTeX documents to DVI (Device Independent) format
// using the TeX typesetting pipeline.

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
#include "../lambda/tex/tex_pagebreak.hpp"
#include "../lambda/tex/tex_dvi_out.hpp"
#include "../lambda/tex/tex_latex_bridge.hpp"

/**
 * Render LaTeX file to DVI format
 *
 * @param latex_file Path to input LaTeX file
 * @param dvi_file Path to output DVI file
 * @return 0 on success, non-zero on error
 */
int render_latex_to_dvi(const char* latex_file, const char* dvi_file) {
    log_debug("render_latex_to_dvi called with latex_file='%s', dvi_file='%s'",
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

    auto step1_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 1 - Read LaTeX file: %.1fms",
             std::chrono::duration<double, std::milli>(step1_end - step1_start).count());

    // Step 2: Parse LaTeX with tree-sitter
    auto step2_start = std::chrono::high_resolution_clock::now();

    String* type_str = (String*)pool_alloc(pool, sizeof(String) + 6);
    type_str->len = 5;
    strcpy(type_str->chars, "latex");

    Input* latex_input = input_from_source(latex_content, latex_url, type_str, nullptr);
    free(latex_content);

    if (!latex_input || !latex_input->root.item) {
        log_error("Failed to parse LaTeX file: %s", latex_filepath);
        pool_destroy(pool);
        return 1;
    }

    auto step2_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 2 - Parse LaTeX: %.1fms",
             std::chrono::duration<double, std::milli>(step2_end - step2_start).count());

    // Step 3: Set up TeX typesetting context
    auto step3_start = std::chrono::high_resolution_clock::now();

    Arena* arena = arena_create_default(pool);
    if (!arena) {
        log_error("Failed to create arena for TeX typesetting");
        pool_destroy(pool);
        return 1;
    }

    // Create TFM font manager
    tex::TFMFontManager* fonts = tex::create_font_manager(arena);

    // Create LaTeX context with default document class
    tex::LaTeXContext ctx = tex::LaTeXContext::create(arena, fonts, "article");

    // Set page dimensions to match LaTeX article class defaults
    // LaTeX uses textheight=550pt, textwidth=345pt for US Letter paper
    ctx.doc_ctx.page_width = 612.0f;   // 8.5 inches
    ctx.doc_ctx.page_height = 795.0f;  // 11 inches (TeX default is 794.97pt)
    ctx.doc_ctx.margin_left = 72.0f;   // 1 inch (but textwidth differs)
    ctx.doc_ctx.margin_right = 72.0f;
    ctx.doc_ctx.margin_top = 72.0f;    // Simplified - TeX has complex top margin
    ctx.doc_ctx.margin_bottom = 72.0f;
    ctx.doc_ctx.text_width = 345.0f;   // Match LaTeX article class default
    ctx.doc_ctx.text_height = 550.0f;  // Match LaTeX article class default

    auto step3_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 3 - Setup context: %.1fms",
             std::chrono::duration<double, std::milli>(step3_end - step3_start).count());

    // Step 4: Typeset document
    auto step4_start = std::chrono::high_resolution_clock::now();

    tex::TexNode* document = tex::typeset_latex_document(latex_input->root, ctx);
    if (!document) {
        log_error("Failed to typeset LaTeX document");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    auto step4_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 4 - Typeset document: %.1fms",
             std::chrono::duration<double, std::milli>(step4_end - step4_start).count());

    // Step 5: Break into pages
    auto step5_start = std::chrono::high_resolution_clock::now();

    tex::PageList pages = tex::break_latex_into_pages(document, ctx);
    if (pages.page_count == 0) {
        log_error("Failed to break document into pages");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    log_info("[DVI Pipeline] Document has %d pages", pages.page_count);

    auto step5_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 5 - Page break: %.1fms",
             std::chrono::duration<double, std::milli>(step5_end - step5_start).count());

    // Step 6: Write to DVI file
    auto step6_start = std::chrono::high_resolution_clock::now();

    // Convert PageList to PageContent array for DVI writing
    tex::PageContent* page_contents = (tex::PageContent*)arena_alloc(
        arena, pages.page_count * sizeof(tex::PageContent)
    );
    if (!page_contents) {
        log_error("Failed to allocate page content array");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    for (int i = 0; i < pages.page_count; i++) {
        page_contents[i].vlist = pages.pages[i];
        page_contents[i].height = 0.0f;  // Will be calculated by DVI writer
        page_contents[i].depth = 0.0f;
        page_contents[i].break_penalty = 0;
        page_contents[i].marks_first = nullptr;
        page_contents[i].marks_top = nullptr;
        page_contents[i].marks_bot = nullptr;
        page_contents[i].inserts = nullptr;
    }

    tex::DVIParams dvi_params = tex::DVIParams::defaults();
    dvi_params.comment = "Lambda Script TeX Output";

    bool success = tex::write_dvi_file(
        dvi_file,
        page_contents,
        pages.page_count,
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

    auto step6_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] Step 6 - Write DVI: %.1fms",
             std::chrono::duration<double, std::milli>(step6_end - step6_start).count());

    // Cleanup
    arena_destroy(arena);
    pool_destroy(pool);

    auto total_end = std::chrono::high_resolution_clock::now();
    log_info("[TIMING] render_latex_to_dvi total: %.1fms",
             std::chrono::duration<double, std::milli>(total_end - total_start).count());

    log_info("Successfully rendered LaTeX to DVI: %s", dvi_file);
    return 0;
}
