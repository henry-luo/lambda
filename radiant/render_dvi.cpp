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
#include "../lib/strbuf.h"
}
#include "../lambda/input/input.hpp"
#include "../lambda/tex/tex_node.hpp"
#include "../lambda/tex/tex_tfm.hpp"
#include "../lambda/tex/tex_linebreak.hpp"
#include "../lambda/tex/tex_pagebreak.hpp"
#include "../lambda/tex/tex_dvi_out.hpp"
#include "../lambda/tex/tex_latex_bridge.hpp"
#include "../lambda/tex/tex_document_model.hpp"
#include "../lambda/tex/tex_math_ast.hpp"
#include "../lambda/tex/tex_math_bridge.hpp"

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

/**
 * Render a single math formula to DVI format
 *
 * @param math_formula LaTeX math formula (without $ delimiters)
 * @param dvi_file Path to output DVI file (NULL for stdout dump)
 * @param dump_ast If true, dump AST to stderr instead of rendering
 * @param dump_boxes If true, dump box structure to stderr
 * @return 0 on success, non-zero on error
 */
int render_math_to_dvi(const char* math_formula, const char* dvi_file, bool dump_ast, bool dump_boxes) {
    log_info("[MATH] render_math_to_dvi: formula='%s', dvi='%s', dump_ast=%d, dump_boxes=%d",
             math_formula, dvi_file ? dvi_file : "(null)", dump_ast, dump_boxes);

    // Create memory pool and arena
    Pool* pool = pool_create();
    if (!pool) {
        log_error("[MATH] Failed to create memory pool");
        return 1;
    }

    Arena* arena = arena_create_default(pool);
    if (!arena) {
        log_error("[MATH] Failed to create arena");
        pool_destroy(pool);
        return 1;
    }

    // Create font manager
    tex::TFMFontManager* fonts = tex::create_font_manager(arena);
    if (!fonts) {
        log_error("[MATH] Failed to create font manager");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    // Step 1: Parse math formula to AST (Phase A)
    log_info("[MATH] Phase A: Parsing formula to AST...");
    tex::MathASTNode* ast = tex::parse_math_string_to_ast(math_formula, strlen(math_formula), arena);
    if (!ast) {
        log_error("[MATH] Failed to parse math formula: %s", math_formula);
        fprintf(stderr, "Error: Failed to parse math formula\n");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }
    log_info("[MATH] Phase A complete: AST node type=%s", tex::math_node_type_name(ast->type));

    // Dump AST if requested
    if (dump_ast) {
        StrBuf* buf = strbuf_new_cap(256);
        tex::math_ast_dump(ast, buf, 0);
        fprintf(stderr, "=== Math AST ===\n%s\n", buf->str);
        strbuf_free(buf);

        if (!dvi_file && !dump_boxes) {
            // Only AST dump was requested, we're done
            arena_destroy(arena);
            pool_destroy(pool);
            return 0;
        }
    }

    // Step 2: Typeset AST to TexNode (Phase B)
    log_info("[MATH] Phase B: Typesetting AST to TexNode...");

    // Create math context using the proper factory method
    tex::MathContext math_ctx = tex::MathContext::create(arena, fonts, 10.0f);
    math_ctx.style = tex::MathStyle::Display;  // Display style for standalone formula

    tex::TexNode* tex_node = tex::typeset_math_ast(ast, math_ctx);
    if (!tex_node) {
        log_error("[MATH] Failed to typeset math formula");
        fprintf(stderr, "Error: Failed to typeset math formula\n");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }
    log_info("[MATH] Phase B complete: TexNode width=%.2fpt, height=%.2fpt, depth=%.2fpt",
             tex_node->width, tex_node->height, tex_node->depth);

    // Dump boxes if requested
    if (dump_boxes) {
        fprintf(stderr, "=== TexNode Box Structure ===\n");
        fprintf(stderr, "Root: node_class=%d, width=%.2fpt, height=%.2fpt, depth=%.2fpt\n",
                (int)tex_node->node_class, tex_node->width, tex_node->height, tex_node->depth);
        // TODO: Add recursive box dump

        if (!dvi_file) {
            // Only box dump was requested, we're done
            arena_destroy(arena);
            pool_destroy(pool);
            return 0;
        }
    }

    // Step 3: Write to DVI if output file specified
    if (dvi_file) {
        log_info("[MATH] Phase C: Writing DVI to '%s'...", dvi_file);

        tex::DVIParams dvi_params = tex::DVIParams::defaults();
        dvi_params.comment = "Lambda Math Formula";

        bool success = tex::write_dvi_page(
            dvi_file,
            tex_node,
            fonts,
            arena,
            dvi_params
        );

        if (!success) {
            log_error("[MATH] Failed to write DVI file: %s", dvi_file);
            fprintf(stderr, "Error: Failed to write DVI file\n");
            arena_destroy(arena);
            pool_destroy(pool);
            return 1;
        }

        log_info("[MATH] Successfully wrote DVI: %s", dvi_file);
        fprintf(stderr, "Math formula rendered to: %s\n", dvi_file);
    }

    // Cleanup
    arena_destroy(arena);
    pool_destroy(pool);

    return 0;
}

/**
 * Render a math formula to AST JSON format (MathLive-compatible)
 *
 * @param math_formula LaTeX math formula string
 * @param json_file Path to output JSON file
 * @return 0 on success, non-zero on error
 */
int render_math_to_ast_json(const char* math_formula, const char* json_file) {
    log_info("[MATH_AST] render_math_to_ast_json: formula='%s', json='%s'",
             math_formula, json_file ? json_file : "(stdout)");

    // Create memory pool and arena
    Pool* pool = pool_create();
    if (!pool) {
        log_error("[MATH_AST] Failed to create memory pool");
        return 1;
    }

    Arena* arena = arena_create_default(pool);
    if (!arena) {
        log_error("[MATH_AST] Failed to create arena");
        pool_destroy(pool);
        return 1;
    }

    // Parse math formula to AST
    log_info("[MATH_AST] Parsing formula to AST...");
    tex::MathASTNode* ast = tex::parse_math_string_to_ast(math_formula, strlen(math_formula), arena);
    if (!ast) {
        log_error("[MATH_AST] Failed to parse math formula: %s", math_formula);
        fprintf(stderr, "Error: Failed to parse math formula\n");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }
    log_info("[MATH_AST] Parse complete: AST node type=%s", tex::math_node_type_name(ast->type));

    // Convert AST to JSON (MathLive-compatible format)
    log_info("[MATH_AST] Converting AST to JSON...");
    StrBuf* json_buf = strbuf_new_cap(1024);
    tex::math_ast_to_json(ast, json_buf);

    const char* json = json_buf->str;
    log_info("[MATH_AST] JSON conversion complete: length=%zu", strlen(json));

    // Write to file or stdout
    if (json_file) {
        FILE* f = fopen(json_file, "w");
        if (!f) {
            log_error("[MATH_AST] Failed to open output file: %s", json_file);
            fprintf(stderr, "Error: Failed to open output file: %s\n", json_file);
            strbuf_free(json_buf);
            arena_destroy(arena);
            pool_destroy(pool);
            return 1;
        }

        size_t len = strlen(json);
        size_t written = fwrite(json, 1, len, f);
        fclose(f);

        if (written != len) {
            log_error("[MATH_AST] Failed to write JSON file");
            fprintf(stderr, "Error: Failed to write JSON file\n");
            strbuf_free(json_buf);
            arena_destroy(arena);
            pool_destroy(pool);
            return 1;
        }

        log_info("[MATH_AST] Successfully wrote JSON: %s", json_file);
    } else {
        // Output to stdout
        printf("%s\n", json);
    }

    // Cleanup
    strbuf_free(json_buf);
    arena_destroy(arena);
    pool_destroy(pool);

    return 0;
}
#include "../lambda/tex/tex_html_render.hpp"

/**
 * Render a math formula to HTML format
 *
 * @param math_formula LaTeX math formula string
 * @param html_file Path to output HTML file
 * @param standalone If true, output full HTML document with CSS
 * @return 0 on success, non-zero on error
 */
int render_math_to_html(const char* math_formula, const char* html_file, bool standalone) {
    log_info("[MATH_HTML] render_math_to_html: formula='%s', html='%s', standalone=%d",
             math_formula, html_file ? html_file : "(stdout)", standalone);

    // Create memory pool and arena
    Pool* pool = pool_create();
    if (!pool) {
        log_error("[MATH_HTML] Failed to create memory pool");
        return 1;
    }

    Arena* arena = arena_create_default(pool);
    if (!arena) {
        log_error("[MATH_HTML] Failed to create arena");
        pool_destroy(pool);
        return 1;
    }

    // Create font manager
    tex::TFMFontManager* fonts = tex::create_font_manager(arena);
    if (!fonts) {
        log_error("[MATH_HTML] Failed to create font manager");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    // Step 1: Parse math formula to AST
    log_info("[MATH_HTML] Phase A: Parsing formula to AST...");
    tex::MathASTNode* ast = tex::parse_math_string_to_ast(math_formula, strlen(math_formula), arena);
    if (!ast) {
        log_error("[MATH_HTML] Failed to parse math formula: %s", math_formula);
        fprintf(stderr, "Error: Failed to parse math formula\n");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }
    log_info("[MATH_HTML] Phase A complete: AST node type=%s", tex::math_node_type_name(ast->type));

    // Step 2: Typeset AST to TexNode
    log_info("[MATH_HTML] Phase B: Typesetting AST to TexNode...");

    tex::MathContext math_ctx = tex::MathContext::create(arena, fonts, 10.0f);
    math_ctx.style = tex::MathStyle::Display;

    tex::TexNode* tex_node = tex::typeset_math_ast(ast, math_ctx);
    if (!tex_node) {
        log_error("[MATH_HTML] Failed to typeset math formula");
        fprintf(stderr, "Error: Failed to typeset math formula\n");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }
    log_info("[MATH_HTML] Phase B complete: TexNode width=%.2fpx, height=%.2fpx, depth=%.2fpx",
             tex_node->width, tex_node->height, tex_node->depth);

    // Step 3: Render TexNode to HTML
    log_info("[MATH_HTML] Phase C: Rendering to HTML...");

    tex::HtmlRenderOptions opts;
    opts.base_font_size_px = 16.0f;
    opts.include_styles = true;
    opts.standalone = standalone;

    const char* html;
    if (standalone) {
        html = tex::render_texnode_to_html_document(tex_node, arena, opts);
    } else {
        html = tex::render_texnode_to_html(tex_node, arena, opts);
    }

    if (!html) {
        log_error("[MATH_HTML] Failed to render HTML");
        fprintf(stderr, "Error: Failed to render HTML\n");
        arena_destroy(arena);
        pool_destroy(pool);
        return 1;
    }

    log_info("[MATH_HTML] Phase C complete: HTML length=%zu", strlen(html));

    // Step 4: Write to file or stdout
    if (html_file) {
        FILE* f = fopen(html_file, "w");
        if (!f) {
            log_error("[MATH_HTML] Failed to open output file: %s", html_file);
            fprintf(stderr, "Error: Failed to open output file: %s\n", html_file);
            arena_destroy(arena);
            pool_destroy(pool);
            return 1;
        }

        size_t len = strlen(html);
        size_t written = fwrite(html, 1, len, f);
        fclose(f);

        if (written != len) {
            log_error("[MATH_HTML] Failed to write HTML file");
            fprintf(stderr, "Error: Failed to write HTML file\n");
            arena_destroy(arena);
            pool_destroy(pool);
            return 1;
        }

        log_info("[MATH_HTML] Successfully wrote HTML: %s", html_file);
    } else {
        // Output to stdout
        printf("%s\n", html);
    }

    // Cleanup
    arena_destroy(arena);
    pool_destroy(pool);

    return 0;
}
