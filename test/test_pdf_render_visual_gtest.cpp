/**
 * GTest visual regression coverage for Radiant's SVG/PDF/PNG export backends.
 *
 * This file holds two independent groups of tests:
 *
 * 1. RenderOutputParity.* — export-backend correctness on small, hand-written
 *    HTML snippets rendered through `./lambda.exe render`. Two sub-themes:
 *      - Pixel parity across render paths: render the same HTML two ways
 *        (tiled vs. untiled, 1 vs. 2 render threads, embedded-SVG replay) and
 *        require byte-identical PNGs via expect_pngs_exactly_equal().
 *      - Effect lowering / fallback strategy: verify that CSS effects the SVG
 *        and PDF backends cannot express natively are lowered to the expected
 *        fallback, by grepping the output file for marker bytes. Examples:
 *        inline SVG -> PDF inline image / SVG subscene; filter/box-shadow/
 *        blend-mode/gradient -> raster fallback (SVG `effect-raster`, PDF inline
 *        image or `/XObject`); alpha effects -> PDF soft mask (`/SMask`);
 *        backdrop-filter -> opaque flattened raster (no `/SMask`); plain
 *        opacity -> native PDF `ExtGState` rather than rasterizing.
 *
 * 2. PdfRenderVisual.CompareLambdaPagesAgainstPopplerReference — end-to-end
 *    fidelity of Lambda's own PDF import-and-render path. For every `*.pdf`
 *    fixture under test/pdf (up to MAX_PAGES_PER_PDF pages each), the reference
 *    page is rasterized with Poppler (`pdfinfo` + `pdftoppm`) — the most widely
 *    deployed, stable open-source PDF rasterizer available from the command
 *    line — to a RENDER_WIDTH-wide PNG under temp/pdf_visual/reference. Lambda
 *    renders the same page through the PDF package (`pdf.pdf_to_svg`) and
 *    Radiant's PNG renderer, then the two images are compared pixel-by-pixel
 *    (composited over white). Each page's mismatch percentage is checked
 *    against test/pdf/baseline.txt and fails on regression beyond
 *    BASELINE_REGRESSION_EPSILON; a magenta-highlighted diff PNG is written to
 *    temp/pdf_visual/diff for inspection. New/missing baselines auto-initialize,
 *    and `--update-baseline` rewrites the baseline when there are no regressions.
 *
 * All tests skip gracefully when lambda.exe is unbuilt, Poppler is missing, or
 * no PDF fixtures are present.
 */

#include <gtest/gtest.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <png.h>

extern "C" {
#include "../lib/image.h"
}

#define PDF_DIR "test/pdf"
#define PDF_BASELINE_FILE "test/pdf/baseline.txt"
#define PDF_TEMP_DIR "temp/pdf_visual"
#define PDF_REF_DIR "temp/pdf_visual/reference"
#define PDF_DIFF_DIR "temp/pdf_visual/diff"
#define LAMBDA_EXE "./lambda.exe"
#define RENDER_WIDTH 600
#define MAX_PAGES_PER_PDF 4
#define MAX_PDFS 64
#define PIXEL_DELTA_THRESHOLD 32
#define MAX_PDF_PAGE_RESULTS (MAX_PDFS * MAX_PAGES_PER_PDF)
#define BASELINE_REGRESSION_EPSILON 0.2

static bool g_update_baseline = false;

struct PdfFileInfo {
    char path[PATH_MAX];
    char base[256];
};

struct BaselineEntry {
    char test_id[512];
    double mismatch_percent;
    bool seen;
};

struct BaselineData {
    BaselineEntry entries[MAX_PDF_PAGE_RESULTS];
    int count;
    bool loaded;
};

struct PdfPageResult {
    char test_id[512];
    char pdf_path[PATH_MAX];
    char diff_path[PATH_MAX];
    char failure_reason[256];
    int page;
    double mismatch_percent;
    double mean_abs_delta;
    double baseline_percent;
    bool has_baseline;
    bool failed;
    bool regressed;
    bool is_new_baseline;
};

struct CommandResult {
    int exit_code;
    char output[8192];
};

struct ImageData {
    unsigned char* pixels;
    int width;
    int height;
    int channels;
};

static bool file_exists(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    fclose(fp);
    return true;
}

static bool path_is_dir(const char* path) {
    if (!path || !*path) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool ensure_dir(const char* path) {
    if (!path || !*path) return false;
    if (path_is_dir(path)) return true;

    char buf[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return false;
    memcpy(buf, path, len + 1);

    for (char* p = buf + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (buf[0] && !path_is_dir(buf)) {
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return false;
            }
            if (!path_is_dir(buf)) {
                *p = '/';
                return false;
            }
        }
        *p = '/';
    }

    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return false;
    return path_is_dir(buf);
}

static bool command_exists(const char* tool) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "command -v '%s' >/dev/null 2>&1", tool);
    return system(cmd) == 0;
}

static void shell_quote(const char* src, char* dst, size_t dst_size) {
    size_t pos = 0;
    if (dst_size == 0) return;
    dst[pos++] = '\'';
    for (const char* p = src; *p && pos + 5 < dst_size; p++) {
        if (*p == '\'') {
            dst[pos++] = '\'';
            dst[pos++] = '\\';
            dst[pos++] = '\'';
            dst[pos++] = '\'';
        } else {
            dst[pos++] = *p;
        }
    }
    if (pos + 1 < dst_size) dst[pos++] = '\'';
    dst[pos] = '\0';
}

static void lambda_string_escape(const char* src, char* dst, size_t dst_size) {
    size_t pos = 0;
    for (const char* p = src; *p && pos + 2 < dst_size; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\\' || ch == '"') {
            dst[pos++] = '\\';
            dst[pos++] = (char)ch;
        } else if (ch == '\n') {
            dst[pos++] = '\\';
            dst[pos++] = 'n';
        } else if (ch == '\r') {
            dst[pos++] = '\\';
            dst[pos++] = 'r';
        } else if (ch == '\t') {
            dst[pos++] = '\\';
            dst[pos++] = 't';
        } else {
            dst[pos++] = (char)ch;
        }
    }
    dst[pos] = '\0';
}

static CommandResult run_command_capture(const char* cmd) {
    CommandResult result;
    result.exit_code = -1;
    result.output[0] = '\0';

    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        snprintf(result.output, sizeof(result.output), "popen failed for: %s", cmd);
        return result;
    }

    size_t used = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        size_t len = strlen(buf);
        if (used + len + 1 < sizeof(result.output)) {
            memcpy(result.output + used, buf, len);
            used += len;
            result.output[used] = '\0';
        }
    }

    int status = pclose(pipe);
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else result.exit_code = status;
    return result;
}

static const char* lambda_no_log_arg() {
    return "--no-log ";
}

static bool write_file_all(const char* path, const char* data, size_t len) {
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    return written == len;
}

static bool file_contains_text(const char* path, const char* needle) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    char buf[8192];
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        fclose(fp);
        return true;
    }
    size_t carry = 0;
    while (!feof(fp)) {
        size_t n = fread(buf + carry, 1, sizeof(buf) - carry, fp);
        size_t total = carry + n;
        if (total >= needle_len) {
            for (size_t i = 0; i <= total - needle_len; i++) {
                if (memcmp(buf + i, needle, needle_len) == 0) {
                    fclose(fp);
                    return true;
                }
            }
        }
        if (needle_len > 1 && total >= needle_len - 1) {
            carry = needle_len - 1;
            memmove(buf, buf + total - carry, carry);
        } else {
            carry = total;
        }
    }
    fclose(fp);
    return false;
}


static bool has_pdf_ext(const char* name) {
    size_t len = strlen(name);
    return len > 4 && strcmp(name + len - 4, ".pdf") == 0;
}

static void basename_without_pdf(const char* name, char* out, size_t out_size) {
    size_t len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".pdf") == 0) len -= 4;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, name, len);
    out[len] = '\0';
    for (size_t i = 0; out[i]; i++) {
        if (!isalnum((unsigned char)out[i]) && out[i] != '-' && out[i] != '_') out[i] = '_';
    }
}

static void make_page_test_id(const PdfFileInfo* pdf, int page, char* out, size_t out_size) {
    snprintf(out, out_size, "%s_page_%02d", pdf->base, page);
}

static void load_pdf_baseline(BaselineData* baseline) {
    baseline->count = 0;
    baseline->loaded = false;

    FILE* fp = fopen(PDF_BASELINE_FILE, "r");
    if (!fp) {
        fprintf(stderr, "[pdf-render] No baseline file found (%s) — regression checking disabled\n",
                PDF_BASELINE_FILE);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), fp) && baseline->count < MAX_PDF_PAGE_RESULTS) {
        char* p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        char test_id[512];
        double mismatch_percent = 0.0;
        if (sscanf(p, "%511s %lf", test_id, &mismatch_percent) == 2) {
            BaselineEntry* entry = &baseline->entries[baseline->count++];
            snprintf(entry->test_id, sizeof(entry->test_id), "%s", test_id);
            entry->mismatch_percent = mismatch_percent;
            entry->seen = false;
        }
    }
    fclose(fp);
    baseline->loaded = true;
    fprintf(stderr, "[pdf-render] Loaded baseline: %d page results from %s\n",
            baseline->count, PDF_BASELINE_FILE);
}

static BaselineEntry* find_baseline_entry(BaselineData* baseline, const char* test_id) {
    if (!baseline->loaded) return NULL;
    for (int i = 0; i < baseline->count; i++) {
        if (strcmp(baseline->entries[i].test_id, test_id) == 0) return &baseline->entries[i];
    }
    return NULL;
}

static bool write_pdf_baseline(const PdfPageResult* results, int result_count) {
    FILE* fp = fopen(PDF_BASELINE_FILE, "w");
    if (!fp) {
        fprintf(stderr, "[pdf-render] ERROR: cannot write baseline file: %s\n", PDF_BASELINE_FILE);
        return false;
    }

    fprintf(fp, "# PDF render visual baseline (auto-updated)\n");
    fprintf(fp, "# format: <test-id> <pixel-diff-percent>\n");
    fprintf(fp, "# regression: current pixel diff must not exceed baseline by more than %.6f\n",
            BASELINE_REGRESSION_EPSILON);
    for (int i = 0; i < result_count; i++) {
        fprintf(fp, "%s %.8f\n", results[i].test_id, results[i].mismatch_percent);
    }
    fclose(fp);
    fprintf(stderr, "[pdf-render] Wrote baseline: %d page results to %s\n",
            result_count, PDF_BASELINE_FILE);
    return true;
}

static bool has_new_baseline_results(const PdfPageResult* results, int result_count) {
    for (int i = 0; i < result_count; i++) {
        if (results[i].is_new_baseline) return true;
    }
    return false;
}

static int compare_pdf_file_info(const void* a, const void* b) {
    const PdfFileInfo* pa = (const PdfFileInfo*)a;
    const PdfFileInfo* pb = (const PdfFileInfo*)b;
    return strcmp(pa->path, pb->path);
}

static int discover_pdfs(PdfFileInfo* files, int max_files) {
    DIR* dir = opendir(PDF_DIR);
    if (!dir) return 0;
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        if (!has_pdf_ext(entry->d_name)) continue;
        snprintf(files[count].path, sizeof(files[count].path), "%s/%s", PDF_DIR, entry->d_name);
        basename_without_pdf(entry->d_name, files[count].base, sizeof(files[count].base));
        count++;
    }
    closedir(dir);
    qsort(files, count, sizeof(PdfFileInfo), compare_pdf_file_info);
    return count;
}

static int pdf_page_count(const char* pdf_path) {
    char qpath[PATH_MAX + 8];
    char cmd[PATH_MAX + 128];
    shell_quote(pdf_path, qpath, sizeof(qpath));
    snprintf(cmd, sizeof(cmd), "pdfinfo %s 2>&1", qpath);
    CommandResult result = run_command_capture(cmd);
    if (result.exit_code != 0) return 0;

    const char* pages = strstr(result.output, "Pages:");
    if (!pages) return 0;
    pages += 6;
    while (*pages && !isdigit((unsigned char)*pages)) pages++;
    return atoi(pages);
}

static bool render_reference_page(const PdfFileInfo* pdf, int page, char* out_png, size_t out_size) {
    char prefix[PATH_MAX];
    char qpdf[PATH_MAX + 8];
    char qprefix[PATH_MAX + 8];
    char cmd[PATH_MAX * 2 + 256];

    snprintf(prefix, sizeof(prefix), "%s/%s_page_%02d_ref", PDF_REF_DIR, pdf->base, page);
    snprintf(out_png, out_size, "%s.png", prefix);
    unlink(out_png);

    shell_quote(pdf->path, qpdf, sizeof(qpdf));
    shell_quote(prefix, qprefix, sizeof(qprefix));
    snprintf(cmd, sizeof(cmd),
             "pdftoppm -png -f %d -l %d -singlefile -scale-to-x %d -scale-to-y -1 %s %s 2>&1",
             page, page, RENDER_WIDTH, qpdf, qprefix);
    CommandResult result = run_command_capture(cmd);
    if (result.exit_code != 0 || !file_exists(out_png)) {
        fprintf(stderr, "Reference render failed for %s page %d:\n%s\n", pdf->path, page, result.output);
        return false;
    }
    return true;
}

static bool write_lambda_page_script(const PdfFileInfo* pdf, int page_index, int height, const char* script_path) {
    char pdf_path_escaped[PATH_MAX * 2];
    char script[4096];

    lambda_string_escape(pdf->path, pdf_path_escaped, sizeof(pdf_path_escaped));
    snprintf(script, sizeof(script),
             "import pdf: lambda.package.pdf.pdf\n"
             "\n"
             "let doc^err = input(\"%s\", 'pdf')\n"
             "let page = pdf.pdf_to_svg(doc, %d, {show_label: false})\n"
             "<html;\n"
             "  <head;\n"
             "    <meta charset: \"utf-8\">\n"
             "    <style; \"html,body{margin:0;padding:0;background:white;overflow:hidden;}svg{display:block;width:%dpx;height:%dpx;}\">\n"
             "  >\n"
             "  <body; page>\n"
             ">\n",
             pdf_path_escaped, page_index, RENDER_WIDTH, height);

    return write_file_all(script_path, script, strlen(script));
}

static bool render_lambda_png_page(const PdfFileInfo* pdf, int page_index, int height, char* out_png, size_t out_size) {
    char script_path[PATH_MAX];
    char qscript[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];

    snprintf(script_path, sizeof(script_path), "%s/%s_page_%02d.ls", PDF_TEMP_DIR, pdf->base, page_index + 1);
    snprintf(out_png, out_size, "%s/%s_page_%02d_lambda.png", PDF_TEMP_DIR, pdf->base, page_index + 1);
    unlink(out_png);

    if (!write_lambda_page_script(pdf, page_index, height, script_path)) return false;

    shell_quote(script_path, qscript, sizeof(qscript));
    shell_quote(out_png, qpng, sizeof(qpng));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw %d -vh %d --pixel-ratio 1 > %s/%s_page_%02d_render.out 2> %s/%s_page_%02d_render.err",
             LAMBDA_EXE, lambda_no_log_arg(), qscript, qpng, RENDER_WIDTH, height,
             PDF_TEMP_DIR, pdf->base, page_index + 1,
             PDF_TEMP_DIR, pdf->base, page_index + 1);
    int status = system(cmd);
    return status == 0 && file_exists(out_png);
}

static bool load_png_rgba(const char* path, ImageData* image) {
    image->width = 0;
    image->height = 0;
    image->channels = 0;
    image->pixels = image_load(path, &image->width, &image->height, &image->channels, 4);
    return image->pixels != NULL && image->channels == 4;
}

static int composite_over_white(unsigned char color, unsigned char alpha) {
    return (int)((color * alpha + 255 * (255 - alpha) + 127) / 255);
}

static bool write_png_rgba(const char* path, const unsigned char* pixels, int width, int height) {
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(fp);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    png_bytep* rows = (png_bytep*)malloc((size_t)height * sizeof(png_bytep));
    if (!rows) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return false;
    }
    for (int y = 0; y < height; y++) {
        rows[y] = (png_bytep)(pixels + (size_t)y * (size_t)width * 4);
    }
    png_write_image(png_ptr, rows);
    png_write_end(png_ptr, NULL);

    free(rows);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
    return true;
}

static void compare_pngs(const char* reference_path, const char* lambda_path,
                         const char* diff_path, double* mismatch_percent, double* mean_abs_delta) {
    ImageData ref;
    ImageData got;
    ASSERT_TRUE(load_png_rgba(reference_path, &ref)) << "failed to load reference PNG: " << reference_path;
    ASSERT_TRUE(load_png_rgba(lambda_path, &got)) << "failed to load lambda PNG: " << lambda_path;

    ASSERT_EQ(ref.width, got.width) << "width mismatch for " << lambda_path;
    ASSERT_EQ(ref.height, got.height) << "height mismatch for " << lambda_path;

    uint64_t mismatched = 0;
    uint64_t total_delta = 0;
    uint64_t total_pixels = (uint64_t)ref.width * (uint64_t)ref.height;
    unsigned char* diff = (unsigned char*)malloc((size_t)ref.width * (size_t)ref.height * 4);
    ASSERT_NE(diff, nullptr) << "failed to allocate diff image for " << diff_path;
    for (uint64_t i = 0; i < total_pixels; i++) {
        uint64_t off = i * 4;
        int max_delta = 0;
        int ref_rgb[3];
        int got_rgb[3];
        for (int c = 0; c < 3; c++) {
            ref_rgb[c] = composite_over_white(ref.pixels[off + c], ref.pixels[off + 3]);
            got_rgb[c] = composite_over_white(got.pixels[off + c], got.pixels[off + 3]);
            int delta = abs(ref_rgb[c] - got_rgb[c]);
            if (delta > max_delta) max_delta = delta;
            total_delta += (uint64_t)delta;
        }
        if (max_delta > PIXEL_DELTA_THRESHOLD) {
            mismatched++;
            diff[off + 0] = 255;
            diff[off + 1] = 0;
            diff[off + 2] = 255;
            diff[off + 3] = 255;
        } else {
            int gray = (ref_rgb[0] + ref_rgb[1] + ref_rgb[2]) / 3;
            diff[off + 0] = (unsigned char)((gray * 3 + 255) / 4);
            diff[off + 1] = (unsigned char)((gray * 3 + 255) / 4);
            diff[off + 2] = (unsigned char)((gray * 3 + 255) / 4);
            diff[off + 3] = 255;
        }
    }

    *mismatch_percent = total_pixels ? (100.0 * (double)mismatched / (double)total_pixels) : 100.0;
    *mean_abs_delta = total_pixels ? ((double)total_delta / (double)(total_pixels * 3)) : 255.0;

    ASSERT_TRUE(write_png_rgba(diff_path, diff, ref.width, ref.height))
        << "failed to write PDF visual diff PNG: " << diff_path;

    free(diff);
    image_free(ref.pixels);
    image_free(got.pixels);
}

static void expect_pngs_exactly_equal(const char* expected_path, const char* actual_path) {
    ImageData expected;
    ImageData actual;
    ASSERT_TRUE(load_png_rgba(expected_path, &expected))
        << "failed to load expected PNG: " << expected_path;
    ASSERT_TRUE(load_png_rgba(actual_path, &actual))
        << "failed to load actual PNG: " << actual_path;

    ASSERT_EQ(expected.width, actual.width) << "width mismatch for " << actual_path;
    ASSERT_EQ(expected.height, actual.height) << "height mismatch for " << actual_path;

    size_t byte_count = (size_t)expected.width * (size_t)expected.height * 4;
    const unsigned char* expected_pixels = expected.pixels;
    const unsigned char* actual_pixels = actual.pixels;
    size_t first_mismatch = byte_count;
    for (size_t i = 0; i < byte_count; i++) {
        if (expected_pixels[i] != actual_pixels[i]) {
            first_mismatch = i;
            break;
        }
    }

    if (first_mismatch != byte_count) {
        size_t pixel_index = first_mismatch / 4;
        size_t channel = first_mismatch % 4;
        int x = (int)(pixel_index % (size_t)expected.width);
        int y = (int)(pixel_index / (size_t)expected.width);
        ADD_FAILURE() << "PNG mismatch at x=" << x << " y=" << y
                      << " channel=" << channel
                      << " expected=" << (int)expected_pixels[first_mismatch]
                      << " actual=" << (int)actual_pixels[first_mismatch];
    }

    image_free(expected.pixels);
    image_free(actual.pixels);
}

static void report_pdf_failures(const PdfPageResult* results, int result_count) {
    int failure_count = 0;
    for (int i = 0; i < result_count; i++) {
        if (!results[i].failed) continue;
        if (failure_count == 0) {
            fprintf(stderr, "\n=== PDF RENDER VISUAL FAILURES ===\n");
        }
        fprintf(stderr,
            "  %s: mismatch=%.4f%% mean_abs_delta=%.4f pdf=%s page=%d diff=%s%s%s\n",
                results[i].test_id, results[i].mismatch_percent, results[i].mean_abs_delta,
            results[i].pdf_path, results[i].page, results[i].diff_path,
            results[i].failure_reason[0] ? " reason=" : "",
            results[i].failure_reason[0] ? results[i].failure_reason : "");
        failure_count++;
    }
}

static void report_pdf_regressions(const PdfPageResult* results, int result_count) {
    int regression_count = 0;
    for (int i = 0; i < result_count; i++) {
        if (!results[i].regressed) continue;
        if (regression_count == 0) {
            fprintf(stderr, "\n=== PDF RENDER BASELINE REGRESSIONS ===\n");
        }
        fprintf(stderr,
                "  %s: baseline=%.4f%% current=%.4f%% delta=%.4f%% pdf=%s page=%d diff=%s\n",
                results[i].test_id, results[i].baseline_percent, results[i].mismatch_percent,
                results[i].mismatch_percent - results[i].baseline_percent,
                results[i].pdf_path, results[i].page, results[i].diff_path);
        regression_count++;
    }
}

static int count_pdf_failures(const PdfPageResult* results, int result_count) {
    int count = 0;
    for (int i = 0; i < result_count; i++) {
        if (results[i].failed) count++;
    }
    return count;
}

static int count_pdf_regressions(const PdfPageResult* results, int result_count) {
    int count = 0;
    for (int i = 0; i < result_count; i++) {
        if (results[i].regressed) count++;
    }
    return count;
}

TEST(RenderOutputParity, NormalPngMatchesForcedTiledPng) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/tiled_png_parity.html";
    const char* normal_png = "temp/render_output_parity/normal.png";
    const char* tiled_png = "temp/render_output_parity/tiled.png";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>"
        "html,body{margin:0;padding:0;background:#f6f4ee;}"
        ".page{width:180px;height:920px;background:linear-gradient(180deg,#f6f4ee,#dbeafe);}"
        ".card{position:absolute;left:18px;top:22px;width:138px;height:116px;"
        "background:#ef4444;border:7px solid #111827;border-radius:18px;}"
        ".band{position:absolute;left:0;top:196px;width:180px;height:86px;background:#22c55e;}"
        ".round{position:absolute;left:42px;top:340px;width:96px;height:96px;"
        "background:radial-gradient(circle,#fde68a,#f59e0b);border-radius:48px;}"
        ".foot{position:absolute;left:24px;top:760px;width:132px;height:92px;"
        "background:#3b82f6;border-radius:12px;box-shadow:0 0 0 6px #0f172a;}"
        "</style></head><body><div class=\"page\">"
        "<div class=\"card\"></div><div class=\"band\"></div>"
        "<div class=\"round\"></div><div class=\"foot\"></div>"
        "</div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qnormal[PATH_MAX + 8];
    char qtiled[PATH_MAX + 8];
    char normal_cmd[PATH_MAX * 4 + 256];
    char tiled_cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(normal_png, qnormal, sizeof(qnormal));
    shell_quote(tiled_png, qtiled, sizeof(qtiled));

    snprintf(normal_cmd, sizeof(normal_cmd),
             "RADIANT_TILE_THRESHOLD=1000000000 %s render %s%s -o %s -vw 180 --pixel-ratio 1 > temp/render_output_parity/normal.out 2> temp/render_output_parity/normal.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qnormal);
    snprintf(tiled_cmd, sizeof(tiled_cmd),
             "RADIANT_TILE_THRESHOLD=1 RADIANT_TILE_STRIP_H=64 %s render %s%s -o %s -vw 180 --pixel-ratio 1 > temp/render_output_parity/tiled.out 2> temp/render_output_parity/tiled.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qtiled);

    int normal_status = system(normal_cmd);
    int tiled_status = system(tiled_cmd);

    ASSERT_TRUE(WIFEXITED(normal_status));
    ASSERT_EQ(WEXITSTATUS(normal_status), 0);
    ASSERT_TRUE(WIFEXITED(tiled_status));
    ASSERT_EQ(WEXITSTATUS(tiled_status), 0);
    ASSERT_TRUE(file_exists(normal_png));
    ASSERT_TRUE(file_exists(tiled_png));
    expect_pngs_exactly_equal(normal_png, tiled_png);
}

TEST(RenderOutputParity, ThreadedTiledReplayMatchesSingleReplay) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/threaded_replay_parity.html";
    const char* single_png = "temp/render_output_parity/single_replay.png";
    const char* tiled_png = "temp/render_output_parity/threaded_tiled_replay.png";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>"
        "html,body{margin:0;padding:0;background:#fbfbf7;}"
        ".page{position:relative;width:220px;height:640px;background:#fbfbf7;}"
        ".a{position:absolute;left:14px;top:18px;width:72px;height:118px;background:#ef4444;}"
        ".b{position:absolute;left:108px;top:72px;width:84px;height:190px;background:#22c55e;}"
        ".c{position:absolute;left:32px;top:292px;width:148px;height:92px;background:#3b82f6;}"
        ".d{position:absolute;left:56px;top:458px;width:110px;height:118px;background:#f59e0b;}"
        "</style></head><body><div class=\"page\">"
        "<div class=\"a\"></div><div class=\"b\"></div><div class=\"c\"></div><div class=\"d\"></div>"
        "</div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsingle[PATH_MAX + 8];
    char qtiled[PATH_MAX + 8];
    char single_cmd[PATH_MAX * 4 + 256];
    char tiled_cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(single_png, qsingle, sizeof(qsingle));
    shell_quote(tiled_png, qtiled, sizeof(qtiled));

    snprintf(single_cmd, sizeof(single_cmd),
             "RADIANT_TILE_THRESHOLD=1000000000 RADIANT_RENDER_THREADS=1 %s render %s%s -o %s -vw 220 --pixel-ratio 1 > temp/render_output_parity/single_replay.out 2> temp/render_output_parity/single_replay.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsingle);
    snprintf(tiled_cmd, sizeof(tiled_cmd),
             "RADIANT_TILE_THRESHOLD=1000000000 RADIANT_RENDER_THREADS=2 %s render %s%s -o %s -vw 220 --pixel-ratio 1 > temp/render_output_parity/threaded_tiled_replay.out 2> temp/render_output_parity/threaded_tiled_replay.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qtiled);

    int single_status = system(single_cmd);
    int tiled_status = system(tiled_cmd);

    ASSERT_TRUE(WIFEXITED(single_status));
    ASSERT_EQ(WEXITSTATUS(single_status), 0);
    ASSERT_TRUE(WIFEXITED(tiled_status));
    ASSERT_EQ(WEXITSTATUS(tiled_status), 0);
    ASSERT_TRUE(file_exists(single_png));
    ASSERT_TRUE(file_exists(tiled_png));
    expect_pngs_exactly_equal(single_png, tiled_png);
}

TEST(RenderOutputParity, SvgPictureReplayMatchesThreadedTiledReplay) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/svg_picture_replay.html";
    const char* single_png = "temp/render_output_parity/svg_picture_single.png";
    const char* tiled_png = "temp/render_output_parity/svg_picture_tiled.png";
    const char* svg_uri =
        "data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20"
        "width%3D%22120%22%20height%3D%2280%22%20viewBox%3D%220%200%20120%2080%22%3E"
        "%3Crect%20x%3D%220%22%20y%3D%220%22%20width%3D%22120%22%20height%3D%2280%22%20fill%3D%22%23fef3c7%22%2F%3E"
        "%3Crect%20x%3D%2210%22%20y%3D%2210%22%20width%3D%22100%22%20height%3D%2260%22%20fill%3D%22%233b82f6%22%2F%3E"
        "%3Ctext%20x%3D%2216%22%20y%3D%2252%22%20font-size%3D%2228%22%20font-family%3D%22Arial%22%20fill%3D%22%23ffffff%22%3EOK%3C%2Ftext%3E"
        "%3C%2Fsvg%3E";
    char html[4096];
    snprintf(html, sizeof(html),
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>"
        "html,body{margin:0;padding:0;background:#f8fafc;}"
        ".page{position:relative;width:220px;height:620px;background:#f8fafc;}"
        ".top{position:absolute;left:18px;top:24px;width:94px;height:96px;background:#22c55e;}"
        "img{position:absolute;left:48px;top:280px;width:120px;height:80px;}"
        ".bottom{position:absolute;left:28px;top:460px;width:164px;height:78px;background:#ef4444;}"
        "</style></head><body><div class=\"page\">"
        "<div class=\"top\"></div><img src=\"%s\" alt=\"svg\"><div class=\"bottom\"></div>"
        "</div></body></html>", svg_uri);
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsingle[PATH_MAX + 8];
    char qtiled[PATH_MAX + 8];
    char single_cmd[PATH_MAX * 4 + 256];
    char tiled_cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(single_png, qsingle, sizeof(qsingle));
    shell_quote(tiled_png, qtiled, sizeof(qtiled));

    snprintf(single_cmd, sizeof(single_cmd),
             "RADIANT_TILE_THRESHOLD=1000000000 RADIANT_RENDER_THREADS=1 %s render %s%s -o %s -vw 220 --pixel-ratio 1 > temp/render_output_parity/svg_picture_single.out 2> temp/render_output_parity/svg_picture_single.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsingle);
    snprintf(tiled_cmd, sizeof(tiled_cmd),
             "RADIANT_TILE_THRESHOLD=1000000000 RADIANT_RENDER_THREADS=2 %s render %s%s -o %s -vw 220 --pixel-ratio 1 > temp/render_output_parity/svg_picture_tiled.out 2> temp/render_output_parity/svg_picture_tiled.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qtiled);

    int single_status = system(single_cmd);
    int tiled_status = system(tiled_cmd);

    ASSERT_TRUE(WIFEXITED(single_status));
    ASSERT_EQ(WEXITSTATUS(single_status), 0);
    ASSERT_TRUE(WIFEXITED(tiled_status));
    ASSERT_EQ(WEXITSTATUS(tiled_status), 0);
    ASSERT_TRUE(file_exists(single_png));
    ASSERT_TRUE(file_exists(tiled_png));
    expect_pngs_exactly_equal(single_png, tiled_png);
}

TEST(RenderOutputParity, PdfInlineSvgUsesRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/pdf_inline_svg.html";
    const char* pdf_path = "temp/render_output_parity/pdf_inline_svg.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        "svg{display:block;width:40px;height:30px;}</style></head>"
        "<body><svg viewBox=\"0 0 40 30\" xmlns=\"http://www.w3.org/2000/svg\">"
        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" fill=\"#fef3c7\"/>"
        "<circle cx=\"20\" cy=\"15\" r=\"10\" fill=\"#2563eb\"/>"
        "</svg></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/pdf_inline_svg.out 2> temp/render_output_parity/pdf_inline_svg.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "BI\n/W 40\n/H 30"))
        << "PDF inline SVG fallback should emit an inline image";
}

TEST(RenderOutputParity, SvgExportInlineSvgUsesSubsceneLowering) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/svg_inline_subscene.html";
    const char* svg_path = "temp/render_output_parity/svg_inline_subscene.svg";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;color:#16a34a;}"
        "svg{display:block;width:40px;height:30px;fill:currentColor;}</style></head>"
        "<body><svg viewBox=\"0 0 40 30\" xmlns=\"http://www.w3.org/2000/svg\">"
        "<circle cx=\"20\" cy=\"15\" r=\"10\"/>"
        "</svg></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/svg_inline_subscene.out 2> temp/render_output_parity/svg_inline_subscene.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(svg_path));
    EXPECT_TRUE(file_contains_text(svg_path, "<circle"))
        << "SVG export should serialize the inline SVG subscene";
    EXPECT_TRUE(file_contains_text(svg_path, "color=\"rgb(22,163,74)\""))
        << "SVG subscene should carry inherited currentColor";
}

TEST(RenderOutputParity, SvgExportCssFilterUsesRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/svg_filter_effect.html";
    const char* svg_path = "temp/render_output_parity/svg_filter_effect.svg";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".box{width:40px;height:30px;background:#ef4444;"
        "filter:grayscale(1);}</style></head>"
        "<body><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/svg_filter_effect.out 2> temp/render_output_parity/svg_filter_effect.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(svg_path));
    EXPECT_TRUE(file_contains_text(svg_path, "data-radiant-fallback=\"effect-raster\""))
        << "SVG export should mark unsupported CSS filter groups as raster fallbacks";
    EXPECT_TRUE(file_contains_text(svg_path, "href=\"data:image/png;base64,"))
        << "SVG raster fallback should embed the captured filtered paint";
}

TEST(RenderOutputParity, PdfExportCssFilterUsesRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/pdf_filter_effect.html";
    const char* pdf_path = "temp/render_output_parity/pdf_filter_effect.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".box{width:40px;height:30px;background:#ef4444;"
        "filter:grayscale(1);}</style></head>"
        "<body><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/pdf_filter_effect.out 2> temp/render_output_parity/pdf_filter_effect.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "BI\n/W 40\n/H 30"))
        << "PDF export should embed a raster fallback image for unsupported CSS filters";
}

TEST(RenderOutputParity, PdfExportAlphaFilterUsesSoftMaskFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/pdf_alpha_filter_effect.html";
    const char* pdf_path = "temp/render_output_parity/pdf_alpha_filter_effect.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".box{width:64px;height:36px;color:#111827;font:700 24px Arial;"
        "filter:opacity(.45);}</style></head>"
        "<body><div class=\"box\">Hi</div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/pdf_alpha_filter_effect.out 2> temp/render_output_parity/pdf_alpha_filter_effect.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "/SMask"))
        << "PDF raster fallback images with alpha should use a soft mask";
    EXPECT_TRUE(file_contains_text(pdf_path, "/XObject"))
        << "PDF alpha fallbacks should be emitted as reusable image XObjects";
}

TEST(RenderOutputParity, SvgAndPdfExportBoxShadowUseRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/export_box_shadow_effect.html";
    const char* svg_path = "temp/render_output_parity/export_box_shadow_effect.svg";
    const char* pdf_path = "temp/render_output_parity/export_box_shadow_effect.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".box{margin:16px;width:36px;height:28px;background:#22c55e;"
        "border-radius:8px;box-shadow:8px 6px 10px rgba(15,23,42,.55);}"
        "</style></head><body><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char svg_cmd[PATH_MAX * 3 + 256];
    char pdf_cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(svg_cmd, sizeof(svg_cmd),
             "%s render %s%s -o %s -vw 90 > temp/render_output_parity/export_box_shadow_svg.out 2> temp/render_output_parity/export_box_shadow_svg.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);
    snprintf(pdf_cmd, sizeof(pdf_cmd),
             "%s render %s%s -o %s -vw 90 > temp/render_output_parity/export_box_shadow_pdf.out 2> temp/render_output_parity/export_box_shadow_pdf.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int svg_status = system(svg_cmd);
    ASSERT_TRUE(WIFEXITED(svg_status));
    ASSERT_EQ(WEXITSTATUS(svg_status), 0);
    int pdf_status = system(pdf_cmd);
    ASSERT_TRUE(WIFEXITED(pdf_status));
    ASSERT_EQ(WEXITSTATUS(pdf_status), 0);

    ASSERT_TRUE(file_exists(svg_path));
    EXPECT_TRUE(file_contains_text(svg_path, "data-radiant-fallback=\"effect-raster\""))
        << "SVG export should route box-shadow groups through raster fallback";
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "/XObject"))
        << "PDF export should route box-shadow groups through raster fallback images";
    EXPECT_TRUE(file_contains_text(pdf_path, "/SMask"))
        << "PDF shadow fallback should preserve translucent shadow edges with a soft mask";
}

TEST(RenderOutputParity, SvgAndPdfExportBackdropFilterUseRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/export_backdrop_filter_effect.html";
    const char* svg_path = "temp/render_output_parity/export_backdrop_filter_effect.svg";
    const char* pdf_path = "temp/render_output_parity/export_backdrop_filter_effect.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:linear-gradient(90deg,#ef4444,#3b82f6);}"
        ".box{margin:12px;width:42px;height:30px;background:rgba(255,255,255,.35);"
        "backdrop-filter:blur(4px);}</style></head>"
        "<body><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char svg_cmd[PATH_MAX * 3 + 256];
    char pdf_cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(svg_cmd, sizeof(svg_cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/export_backdrop_filter_svg.out 2> temp/render_output_parity/export_backdrop_filter_svg.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);
    snprintf(pdf_cmd, sizeof(pdf_cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/export_backdrop_filter_pdf.out 2> temp/render_output_parity/export_backdrop_filter_pdf.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int svg_status = system(svg_cmd);
    ASSERT_TRUE(WIFEXITED(svg_status));
    ASSERT_EQ(WEXITSTATUS(svg_status), 0);
    int pdf_status = system(pdf_cmd);
    ASSERT_TRUE(WIFEXITED(pdf_status));
    ASSERT_EQ(WEXITSTATUS(pdf_status), 0);

    ASSERT_TRUE(file_exists(svg_path));
    EXPECT_TRUE(file_contains_text(svg_path, "data-radiant-fallback=\"effect-raster\""))
        << "SVG export should route backdrop-filter through raster fallback";
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "BI\n/W 42\n/H 30"))
        << "PDF export should route backdrop-filter through an opaque raster fallback image";
    EXPECT_FALSE(file_contains_text(pdf_path, "/SMask"))
        << "PDF backdrop-filter fallback should capture the prior page backdrop and flatten to an opaque image";
}

TEST(RenderOutputParity, SvgAndPdfExportBlendModeUseRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/export_blend_effect.html";
    const char* svg_path = "temp/render_output_parity/export_blend_effect.svg";
    const char* pdf_path = "temp/render_output_parity/export_blend_effect.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".back{width:48px;height:32px;background:#fde047;}"
        ".box{width:32px;height:24px;margin-top:-28px;background:#2563eb;"
        "mix-blend-mode:multiply;}</style></head>"
        "<body><div class=\"back\"></div><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/export_blend_svg.out 2> temp/render_output_parity/export_blend_svg.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(svg_path));
    EXPECT_TRUE(file_contains_text(svg_path, "data-radiant-fallback=\"effect-raster\""))
        << "SVG export should rasterize unsupported blend-mode groups";

    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/export_blend_pdf.out 2> temp/render_output_parity/export_blend_pdf.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "BI\n/W 32\n/H 24"))
        << "PDF export should rasterize unsupported blend-mode groups";
}

TEST(RenderOutputParity, PdfGradientBackgroundUsesRasterFallbackImage) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/pdf_gradient.html";
    const char* pdf_path = "temp/render_output_parity/pdf_gradient.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".box{width:40px;height:30px;"
        "background:linear-gradient(90deg,#ef4444 0%,#2563eb 100%);}"
        "</style></head><body><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/pdf_gradient.out 2> temp/render_output_parity/pdf_gradient.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "BI\n/W 40\n/H 30"))
        << "PDF gradient fallback should emit an inline image";
}

TEST(RenderOutputParity, PdfOpacityGroupUsesPaintIrExtGState) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/pdf_opacity.html";
    const char* pdf_path = "temp/render_output_parity/pdf_opacity.pdf";
    const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<style>html,body{margin:0;padding:0;background:#fff;}"
        ".box{width:40px;height:30px;background:#ff0000;opacity:.5;}"
        "</style></head><body><div class=\"box\"></div></body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qpdf[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 80 > temp/render_output_parity/pdf_opacity.out 2> temp/render_output_parity/pdf_opacity.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(pdf_path));
    EXPECT_TRUE(file_contains_text(pdf_path, "/Type /ExtGState"))
        << "PDF opacity group should allocate an ExtGState";
    EXPECT_TRUE(file_contains_text(pdf_path, "/ca 0.5"))
        << "PDF opacity group should set non-stroking alpha";
    EXPECT_TRUE(file_contains_text(pdf_path, "/GS1 gs"))
        << "PDF opacity group should apply the ExtGState through PaintIR";
}

static void apply_pdf_baseline(BaselineData* baseline, PdfPageResult* results, int result_count) {
    for (int i = 0; i < result_count; i++) {
        BaselineEntry* baseline_entry = find_baseline_entry(baseline, results[i].test_id);
        if (!baseline_entry) continue;
        baseline_entry->seen = true;
        results[i].has_baseline = true;
        results[i].is_new_baseline = false;
        results[i].baseline_percent = baseline_entry->mismatch_percent;
        results[i].regressed = results[i].mismatch_percent > baseline_entry->mismatch_percent + BASELINE_REGRESSION_EPSILON;
    }
}

static void mark_new_pdf_baseline_results(PdfPageResult* results, int result_count) {
    for (int i = 0; i < result_count; i++) {
        if (!results[i].has_baseline) results[i].is_new_baseline = true;
    }
}

static PdfPageResult* add_pdf_page_result(PdfPageResult* results, int* result_count,
                                          const char* test_id, const PdfFileInfo* pdf,
                                          int page, const char* diff_path,
                                          double mismatch_percent, double mean_abs_delta,
                                          const char* failure_reason) {
    if (*result_count >= MAX_PDF_PAGE_RESULTS) return NULL;
    PdfPageResult* page_result = &results[(*result_count)++];
    snprintf(page_result->test_id, sizeof(page_result->test_id), "%s", test_id);
    snprintf(page_result->pdf_path, sizeof(page_result->pdf_path), "%s", pdf->path);
    snprintf(page_result->diff_path, sizeof(page_result->diff_path), "%s", diff_path ? diff_path : "");
    snprintf(page_result->failure_reason, sizeof(page_result->failure_reason), "%s",
             failure_reason ? failure_reason : "");
    page_result->page = page;
    page_result->mismatch_percent = mismatch_percent;
    page_result->mean_abs_delta = mean_abs_delta;
    page_result->baseline_percent = 0.0;
    page_result->has_baseline = false;
    page_result->failed = failure_reason != NULL;
    page_result->regressed = false;
    page_result->is_new_baseline = false;
    return page_result;
}

static void parse_pdf_render_args(int* argc, char** argv) {
    int out = 1;
    for (int i = 1; i < *argc; i++) {
        if (strcmp(argv[i], "--update-baseline") == 0) {
            g_update_baseline = true;
        } else {
            argv[out++] = argv[i];
        }
    }
    argv[out] = NULL;
    *argc = out;
}

TEST(PdfRenderVisual, CompareLambdaPagesAgainstPopplerReference) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    if (access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is not executable";
    }
    if (!command_exists("pdfinfo") || !command_exists("pdftoppm")) {
        GTEST_SKIP() << "Poppler tools pdfinfo/pdftoppm not found; install poppler to run PDF visual tests";
    }
    if (!path_is_dir(PDF_DIR)) {
        GTEST_SKIP() << "PDF visual fixtures not found under " << PDF_DIR;
    }

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir(PDF_TEMP_DIR));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    ASSERT_TRUE(ensure_dir(PDF_DIFF_DIR));

    PdfFileInfo files[MAX_PDFS];
    int file_count = discover_pdfs(files, MAX_PDFS);
    if (file_count == 0) {
        GTEST_SKIP() << "no PDF fixtures found under " << PDF_DIR;
    }

    BaselineData baseline;
    load_pdf_baseline(&baseline);

    PdfPageResult results[MAX_PDF_PAGE_RESULTS];
    int result_count = 0;

    int compared_pages = 0;
    for (int i = 0; i < file_count; i++) {
        int pages = pdf_page_count(files[i].path);
        ASSERT_GT(pages, 0) << "pdfinfo did not report pages for " << files[i].path;
        int render_pages = pages > MAX_PAGES_PER_PDF ? MAX_PAGES_PER_PDF : pages;
        fprintf(stderr, "[pdf-render] Comparing %s (%d page%s, rendering %d)\n",
                files[i].path, pages, pages == 1 ? "" : "s", render_pages);
        fflush(stderr);

        for (int page = 1; page <= render_pages; page++) {
            ASSERT_LT(result_count, MAX_PDF_PAGE_RESULTS);
            char ref_png[PATH_MAX];
            char lambda_png[PATH_MAX];
            char diff_png[PATH_MAX];
            char test_id[512];
            make_page_test_id(&files[i], page, test_id, sizeof(test_id));
            fprintf(stderr, "[pdf-render]   page %d/%d: %s\n", page, render_pages, test_id);
            fflush(stderr);
            snprintf(diff_png, sizeof(diff_png), "%s/%s_page_%02d_diff.png",
                     PDF_DIFF_DIR, files[i].base, page);
            if (!render_reference_page(&files[i], page, ref_png, sizeof(ref_png))) {
                add_pdf_page_result(results, &result_count, test_id, &files[i], page, diff_png,
                                    100.0, 255.0, "reference render failed");
                continue;
            }

            int ref_width = 0;
            int ref_height = 0;
            if (image_get_dimensions(ref_png, &ref_width, &ref_height) != 1) {
                add_pdf_page_result(results, &result_count, test_id, &files[i], page, diff_png,
                                    100.0, 255.0, "failed to read reference dimensions");
                continue;
            }
            if (ref_width != RENDER_WIDTH) {
                add_pdf_page_result(results, &result_count, test_id, &files[i], page, diff_png,
                                    100.0, 255.0, "reference renderer width mismatch");
                continue;
            }
            if (ref_height <= 0) {
                add_pdf_page_result(results, &result_count, test_id, &files[i], page, diff_png,
                                    100.0, 255.0, "reference renderer height invalid");
                continue;
            }

            if (!render_lambda_png_page(&files[i], page - 1, ref_height, lambda_png, sizeof(lambda_png))) {
                add_pdf_page_result(results, &result_count, test_id, &files[i], page, diff_png,
                                    100.0, 255.0, "Lambda render failed");
                continue;
            }

            double mismatch_percent = 100.0;
            double mean_abs_delta = 255.0;
            compare_pngs(ref_png, lambda_png, diff_png, &mismatch_percent, &mean_abs_delta);

            PdfPageResult* page_result = add_pdf_page_result(results, &result_count, test_id, &files[i], page,
                                                            diff_png, mismatch_percent, mean_abs_delta, NULL);
            ASSERT_NE(page_result, nullptr);

            compared_pages++;
            fprintf(stderr, "[pdf-render]   done %s diff=%.6f%% mean_delta=%.3f\n",
                    test_id, mismatch_percent, mean_abs_delta);
            fflush(stderr);
        }
    }

    apply_pdf_baseline(&baseline, results, result_count);
    if (baseline.loaded) mark_new_pdf_baseline_results(results, result_count);
    int failure_count = count_pdf_failures(results, result_count);
    int regression_count = count_pdf_regressions(results, result_count);
    bool has_new_results = has_new_baseline_results(results, result_count);
    report_pdf_failures(results, result_count);
    report_pdf_regressions(results, result_count);

    if (!baseline.loaded) {
        fprintf(stderr,
                "[pdf-render] Initializing missing baseline from current results: %s\n",
                PDF_BASELINE_FILE);
        ASSERT_TRUE(write_pdf_baseline(results, result_count));
    } else if (g_update_baseline || has_new_results) {
        if (regression_count == 0 && !::testing::Test::HasFailure()) {
            ASSERT_TRUE(write_pdf_baseline(results, result_count));
        } else {
            fprintf(stderr,
                    "[pdf-render] NOT updating baseline: failures=%d regressions=%d new_results=%s\n",
                    failure_count, regression_count, has_new_results ? "yes" : "no");
        }
    }

    if (baseline.loaded) {
        EXPECT_EQ(regression_count, 0) << "PDF render baseline regressions detected; see command-line report above";
    }
    EXPECT_GT(compared_pages, 0);
}

int main(int argc, char** argv) {
    parse_pdf_render_args(&argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
