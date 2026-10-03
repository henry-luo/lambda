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
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <io.h>
#include <direct.h>
#define popen _popen
#define pclose _pclose
// windows mkdir accepts only the path; map the POSIX mode-bearing calls used by this test.
#define mkdir(path, mode) _mkdir(path)
// windows pclose returns the process exit code directly, unlike POSIX wait status.
#define WIFEXITED(status) (1)
#define WEXITSTATUS(status) (status)
#endif
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

static bool render_html_fixture(const char* html_path, const char* output_path,
                                    const char* html) {
    if (!write_file_all(html_path, html, strlen(html))) return false;
    char qhtml[PATH_MAX + 8];
    char qoutput[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(output_path, qoutput, sizeof(qoutput));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s > %s.out 2> %s.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qoutput,
             qoutput, qoutput);
    int status = system(cmd);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool run_html_fixture_view(const char* html_path, const char* events_path,
                                  const char* html, const char* events) {
    if (!write_file_all(html_path, html, strlen(html)) ||
        !write_file_all(events_path, events, strlen(events))) return false;
    char qhtml[PATH_MAX + 8];
    char qevents[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(events_path, qevents, sizeof(qevents));
    snprintf(cmd, sizeof(cmd),
        "%s view %s%s --headless --event-file %s > %s.out 2> %s.err",
        LAMBDA_EXE, lambda_no_log_arg(), qhtml, qevents, qevents, qevents);
    int status = system(cmd);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
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

static bool view_text_x(const char* path, const char* content, float* x) {
    FILE* fp = fopen(path, "rb");
    if (!fp || !content || !x) {
        if (fp) fclose(fp);
        return false;
    }
    char marker[128];
    snprintf(marker, sizeof(marker), "\"content\": \"%s\"", content);
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, marker)) continue;
        for (int i = 0; i < 5 && fgets(line, sizeof(line), fp); i++) {
            const char* pos = strstr(line, "\"x\": ");
            if (pos) {
                *x = (float)strtod(pos + strlen("\"x\": "), nullptr);
                found = true;
                break;
            }
        }
        break;
    }
    fclose(fp);
    return found;
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
             "import pdf: lambda.pdf.pdf\n"
             "\n"
             "let doc = input(\"%s\", 'pdf') ^ { null }\n"
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

TEST(RenderOutputParity, TikzCalibrationPaintsNumericSvgShapes) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* source = "test/input/tikz_calibration_report.tex";
    const char* png_path = "temp/render_output_parity/tikz_calibration.png";
    char qsource[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    shell_quote(source, qsource, sizeof(qsource));
    shell_quote(png_path, qpng, sizeof(qpng));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 1250 --pixel-ratio 1 > temp/render_output_parity/tikz_calibration.out 2> temp/render_output_parity/tikz_calibration.err",
             LAMBDA_EXE, lambda_no_log_arg(), qsource, qpng);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int min_x = image.width;
    int min_y = image.height;
    int max_x = -1;
    int max_y = -1;
    // five samples span the plot; a legend swatch alone cannot satisfy both extents.
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
            if (pixel[0] < 160 || pixel[1] > 100 || pixel[2] > 100 || pixel[3] < 200) continue;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }
    int grid_pixels = 0;
    if (max_x > min_x + 30 && max_y > min_y + 30) {
        // count the light major grid inside the sample bounds, away from text and axes.
        for (int y = min_y + 20; y < max_y - 10; y++) {
            for (int x = min_x + 15; x < max_x - 15; x++) {
                const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
                if (pixel[0] < 205 || pixel[0] > 245 || pixel[3] < 200) continue;
                if (abs((int)pixel[0] - (int)pixel[1]) < 4 &&
                    abs((int)pixel[0] - (int)pixel[2]) < 4) grid_pixels++;
            }
        }
    }
    image_free(image.pixels);
    EXPECT_GT(max_x - min_x, 180);
    EXPECT_GT(max_y - min_y, 80);
    EXPECT_GT(grid_pixels, 200);
}

TEST(RenderOutputParity, TikzNamedWorkflowPaintsShapesAndBranchArrow) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    char qsource[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    const char* png_path = "temp/render_output_parity/tikz_named_workflow.png";
    shell_quote("test/input/tikz_request_workflow.tex", qsource, sizeof(qsource));
    shell_quote(png_path, qpng, sizeof(qpng));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 1250 --pixel-ratio 1 > temp/render_output_parity/tikz_named_workflow.out 2> temp/render_output_parity/tikz_named_workflow.err",
             LAMBDA_EXE, lambda_no_log_arg(), qsource, qpng);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int widest_shape_stroke = 0;
    int longest_branch_stroke = 0;
    // Shape borders make long horizontal runs; the central downward branch
    // makes a vertical run between the diamond and the Reject node.
    for (int y = image.height * 45 / 100; y < image.height * 75 / 100; y++) {
        int run = 0;
        for (int x = image.width * 30 / 100; x < image.width * 70 / 100; x++) {
            const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
            bool dark = pixel[0] < 150 && pixel[1] < 150 && pixel[2] < 150 && pixel[3] > 200;
            run = dark ? run + 1 : 0;
            if (run > widest_shape_stroke) widest_shape_stroke = run;
        }
    }
    for (int x = image.width / 2 - 45; x < image.width / 2 - 5; x++) {
        int run = 0;
        for (int y = image.height * 55 / 100; y < image.height * 70 / 100; y++) {
            const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
            bool dark = pixel[0] < 150 && pixel[1] < 150 && pixel[2] < 150 && pixel[3] > 200;
            run = dark ? run + 1 : 0;
            if (run > longest_branch_stroke) longest_branch_stroke = run;
        }
    }
    image_free(image.pixels);
    EXPECT_GT(widest_shape_stroke, 35);
    EXPECT_GT(longest_branch_stroke, 15);
}

TEST(RenderOutputParity, TikzLatencyYLabelTracksAxisAtTwoX) {
    if (!file_exists(LAMBDA_EXE)) {
        GTEST_SKIP() << "lambda.exe not found; run make build first";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    char qsource[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    const char* png_path = "temp/render_output_parity/tikz_latency_2x.png";
    shell_quote("test/input/tikz_service_latency.tex", qsource, sizeof(qsource));
    shell_quote(png_path, qpng, sizeof(qpng));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 1250 --pixel-ratio 2 > temp/render_output_parity/tikz_latency_2x.out 2> temp/render_output_parity/tikz_latency_2x.err",
             LAMBDA_EXE, lambda_no_log_arg(), qsource, qpng);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int axis_x = -1;
    int axis_top = 0;
    int axis_bottom = 0;
    int longest_axis_run = 0;
    // Find the long y-axis stroke, then check for the rotated label to its left.
    for (int x = image.width * 30 / 100; x < image.width * 48 / 100; x++) {
        int run = 0;
        int run_top = 0;
        for (int y = image.height * 45 / 100; y < image.height * 78 / 100; y++) {
            const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
            bool dark = pixel[0] < 190 && pixel[1] < 190 && pixel[2] < 190 && pixel[3] > 200;
            if (dark) {
                if (run == 0) run_top = y;
                run++;
                if (run > longest_axis_run) {
                    longest_axis_run = run;
                    axis_x = x;
                    axis_top = run_top;
                    axis_bottom = y;
                }
            } else run = 0;
        }
    }
    int label_ink = 0;
    if (axis_x >= 110) {
        for (int y = axis_top + 35; y < axis_bottom - 35; y++) {
            // The rotated label sits beyond the tick labels; font metrics can
            // shift it within this left-side band at two-pixel rendering.
            for (int x = axis_x - 180; x < axis_x - 100; x++) {
                const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
                if (pixel[0] < 150 && pixel[1] < 150 && pixel[2] < 150 && pixel[3] > 200)
                    label_ink++;
            }
        }
    }
    image_free(image.pixels);
    EXPECT_GT(longest_axis_run, 200);
    EXPECT_GT(label_ink, 40);
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

TEST(RenderOutputParity, SvgExportTextShadowWithoutAuthoredFont) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/text_shadow_only.html";
    const char* svg_path = "temp/render_output_parity/text_shadow_only.svg";
    const char* html =
        "<!doctype html><html><body>"
        "<p style=\"text-shadow:2px 3px 0 red\">Shadow</p>"
        "</body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s > temp/render_output_parity/text_shadow_only.out 2> temp/render_output_parity/text_shadow_only.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(svg_path));
    EXPECT_TRUE(file_contains_text(svg_path,
        "text-shadow: 2.0px 3.0px 0.0px rgb(255,0,0)"));
}

TEST(RenderOutputParity, TextDecorationListPaintsBothLines) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/text_decoration_list.html";
    const char* svg_path = "temp/render_output_parity/text_decoration_list.svg";
    const char* png_path = "temp/render_output_parity/text_decoration_list.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}</style>"
        "<div style='font:24px/32px Arial;color:black;"
        "text-decoration:underline overline red 3px'>Decorated</div>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    shell_quote(png_path, qpng, sizeof(qpng));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 300 > temp/render_output_parity/text_decoration_list_svg.out 2> temp/render_output_parity/text_decoration_list_svg.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    EXPECT_TRUE(file_contains_text(svg_path, "text-decoration: underline overline"));
    EXPECT_TRUE(file_contains_text(svg_path, "text-decoration-color: rgb(255,0,0)"));

    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s -vw 300 > temp/render_output_parity/text_decoration_list_png.out 2> temp/render_output_parity/text_decoration_list_png.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpng);
    status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int red_bands = 0;
    bool in_red_band = false;
    // Two long red row bands distinguish both decorations from isolated glyph pixels.
    for (int y = 0; y < image.height && y < 50; y++) {
        int red_pixels = 0;
        for (int x = 0; x < image.width && x < 220; x++) {
            const unsigned char* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
            if (pixel[0] > 170 && pixel[1] < 80 && pixel[2] < 80 && pixel[3] > 200) {
                red_pixels++;
            }
        }
        bool is_red_band = red_pixels > 80;
        if (is_red_band && !in_red_band) red_bands++;
        in_red_band = is_red_band;
    }
    image_free(image.pixels);
    EXPECT_EQ(red_bands, 2);
}

TEST(RenderOutputParity, TextEmphasisPaintsMarksAndRespectsLonghandOrder) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/text_emphasis.html";
    const char* svg_path = "temp/render_output_parity/text_emphasis.svg";
    const char* png_path = "temp/render_output_parity/text_emphasis.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}p{font:30px Arial;"
        "color:black;margin:20px 0}"
        "#dot{text-emphasis:filled dot red;text-emphasis-color:blue}"
        "#circle{text-emphasis-color:blue;text-emphasis:open circle green;"
        "text-emphasis-position:under left}"
        "#reset{text-emphasis:filled dot red;text-emphasis-style:none}"
        "#current{text-emphasis:filled dot currentColor;color:purple}"
        "</style><p id='dot'>ABC</p><p id='circle'>DEF</p>"
        "<p id='reset'>GHI</p><p id='current'>JKL</p>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "fill=\"rgb(0,0,255)\">&#x2022;</text>"));
    EXPECT_TRUE(file_contains_text(svg_path,
        "fill=\"rgb(0,128,0)\">&#x25CB;</text>"));
    EXPECT_TRUE(file_contains_text(svg_path,
        "fill=\"rgb(128,0,128)\">&#x2022;</text>"));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int blue_marks = 0, green_marks = 0, red_marks = 0;
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            const unsigned char* pixel = image.pixels +
                ((size_t)y * image.width + x) * 4;
            if (pixel[2] > 100 && pixel[2] > pixel[0] * 2 &&
                pixel[2] > pixel[1] * 2) blue_marks++;
            if (pixel[1] > 55 && pixel[1] > pixel[0] * 2 &&
                pixel[1] > pixel[2] * 2) green_marks++;
            if (pixel[0] > 100 && pixel[0] > pixel[1] * 2 &&
                pixel[0] > pixel[2] * 2) red_marks++;
        }
    }
    EXPECT_GT(blue_marks, 3);
    EXPECT_GT(green_marks, 3);
    EXPECT_EQ(red_marks, 0);
    image_free(image.pixels);
}

TEST(RenderOutputParity, TextJustifyNoneDoesNotExpandPaintedSpaces) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/text_justify.html";
    const char* png_path = "temp/render_output_parity/text_justify.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}div{font:20px Arial;"
        "width:200px;text-align:justify-all;margin:10px 0}"
        "#none{text-justify:none}#auto{text-justify:none;text-justify:auto}"
        "#word{text-justify:inter-word}"
        "</style><div id='none'>A B C</div><div id='auto'>A B C</div>"
        "<div id='word'>A B C</div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int right_pixels[3] = {};
    for (int row = 0; row < 3; row++) {
        for (int y = 10 + row * 33; y < 30 + row * 33 && y < image.height; y++) {
            for (int x = 160; x < 205 && x < image.width; x++) {
                const unsigned char* pixel = image.pixels +
                    ((size_t)y * image.width + x) * 4;
                if (pixel[0] < 100 && pixel[1] < 100 && pixel[2] < 100)
                    right_pixels[row]++;
            }
        }
    }
    EXPECT_EQ(right_pixels[0], 0);
    EXPECT_GT(right_pixels[1], 5);
    EXPECT_GT(right_pixels[2], 5);
    image_free(image.pixels);
}

TEST(RenderOutputParity, TextJustifyCjkAutoExpandsBetweenIdeographs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/text_justify_cjk.html";
    const char* png_path = "temp/render_output_parity/text_justify_cjk.png";
    const char* svg_path = "temp/render_output_parity/text_justify_cjk.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}div{font:30px Arial;"
        "width:160px;text-align:justify-all;white-space:nowrap;margin:10px 0}"
        "#none{text-justify:none}#auto{text-justify:auto;text-shadow:0 0 4px red}"
        "#word{text-justify:inter-word}</style>"
        "<div id='none'>漢字</div><div id='auto'>漢字</div>"
        "<div id='word'>漢字</div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int right_pixels[3] = {};
    int right_shadow_pixels = 0;
    for (int row = 0; row < 3; row++) {
        for (int y = 10 + row * 52; y < 45 + row * 52 && y < image.height; y++) {
            for (int x = 110; x < 165 && x < image.width; x++) {
                const unsigned char* pixel = image.pixels +
                    ((size_t)y * image.width + x) * 4;
                if (pixel[0] < 100 && pixel[1] < 100 && pixel[2] < 100)
                    right_pixels[row]++;
                if (row == 1 && pixel[0] > pixel[1] + 30 &&
                    pixel[0] > pixel[2] + 30) right_shadow_pixels++;
            }
        }
    }
    EXPECT_EQ(right_pixels[0], 0);
    EXPECT_GT(right_pixels[1], 5);
    EXPECT_GT(right_shadow_pixels, 5);
    EXPECT_EQ(right_pixels[2], 0);
    image_free(image.pixels);
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path, "dx=\"0.00 100.00\""));
}

TEST(RenderOutputParity, LogicalFloatAndClearUseContainingBlockDirection) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/logical_float.html";
    const char* svg_path = "temp/render_output_parity/logical_float.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}section{width:200px;"
        "height:90px;margin:0 0 10px;background:#eee}.f{width:40px;"
        "height:20px;background:red;direction:ltr}.c{height:20px;"
        "background:blue}#ltr{direction:ltr}#rtl{direction:rtl}</style>"
        "<section id='ltr'><div class='f' style='float:inline-start'></div>"
        "<div class='c' style='clear:inline-start'></div></section>"
        "<section id='rtl'><div class='f' style='float:inline-start'></div>"
        "<div class='c' style='clear:inline-start'></div></section>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"0.00\" width=\"40.00\" height=\"20.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"160.00\" y=\"100.00\" width=\"40.00\" height=\"20.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"20.00\" width=\"200.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"120.00\" width=\"200.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, AppearanceNoneKeepsAuthorBoxesWithoutNativeChrome) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/appearance_none.html";
    const char* png_path = "temp/render_output_parity/appearance_none.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "input[type=checkbox],input[type=radio]{appearance:none;width:30px;"
        "height:30px;margin:0 10px 0 0;background:red;border:0}"
        "button{display:block;width:50px;height:30px;margin:0;border:0}"
        "#bare{appearance:none}#native{appearance:auto}</style>"
        "<input type=checkbox checked><input type=radio checked>"
        "<input type=radio checked style='appearance:radio'>"
        "<button id=bare>Hi</button><button id=native>Hi</button>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GT(image.width, 110);
    ASSERT_GT(image.height, 90);
    const unsigned char* checkbox = image.pixels +
        ((size_t)15 * image.width + 15) * 4;
    const unsigned char* radio = image.pixels +
        ((size_t)15 * image.width + 55) * 4;
    const unsigned char* native_radio = image.pixels +
        ((size_t)15 * image.width + 95) * 4;
    const unsigned char* bare = image.pixels +
        ((size_t)45 * image.width + 40) * 4;
    const unsigned char* native = image.pixels +
        ((size_t)75 * image.width + 40) * 4;
    EXPECT_GT(checkbox[0], 200);
    EXPECT_LT(checkbox[1], 30);
    EXPECT_GT(radio[0], 200);
    EXPECT_LT(radio[1], 30);
    EXPECT_LT(native_radio[0], 100);
    EXPECT_GT(bare[0], 240);
    EXPECT_LT(native[0], 235);
    image_free(image.pixels);
}

TEST(RenderOutputParity, RgbPercentageAlphaPaintsLikeNumericAlpha) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/rgb_percent_alpha.html";
    const char* png_path = "temp/render_output_parity/rgb_percent_alpha.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{width:20px;height:20px;display:inline-block}"
        "#modern{background:rgb(255 0 0 / 50%)}"
        "#numeric{background:rgb(255 0 0 / .5)}"
        "#legacy{background:rgba(255,0,0,50%)}</style>"
        "<div id=modern></div><div id=numeric></div><div id=legacy></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 60);
    ASSERT_GE(image.height, 20);
    for (int x : {10, 30, 50}) {
        const unsigned char* pixel = image.pixels +
            ((size_t)10 * image.width + x) * 4;
        EXPECT_GT(pixel[0], 245);
        EXPECT_NEAR(pixel[1], 127, 2);
        EXPECT_NEAR(pixel[2], 127, 2);
        EXPECT_EQ(pixel[3], 255);
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, HwbColorsNormalizeWhitenessBlacknessAndAlpha) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/hwb_color.html";
    const char* png_path = "temp/render_output_parity/hwb_color.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;width:20px;height:20px;top:0}"
        "#gray{left:0;background-color:hwb(45 40% 80%)}"
        "#green{left:25px;background:hwb(120 20% 10%)}"
        "#alpha{left:50px;background-color:hwb(.5turn 0% 0% / 50%)}"
        "#hsl{left:75px;background-color:hsl(180deg 100% 50% / .5)}"
        "#missing{left:100px;background:hwb(120 none none / none)}"
        "#black{left:125px;background:hwb(none none 100%)}"
        "</style><div id=gray></div><div id=green></div>"
        "<div id=alpha></div><div id=hsl></div>"
        "<div id=missing></div><div id=black></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 145);
    ASSERT_GE(image.height, 20);
    const unsigned char* gray = image.pixels +
        ((size_t)10 * image.width + 10) * 4;
    const unsigned char* green = image.pixels +
        ((size_t)10 * image.width + 35) * 4;
    const unsigned char* alpha = image.pixels +
        ((size_t)10 * image.width + 60) * 4;
    const unsigned char* hsl = image.pixels +
        ((size_t)10 * image.width + 85) * 4;
    for (int channel = 0; channel < 3; channel++) EXPECT_NEAR(gray[channel], 85, 2);
    EXPECT_NEAR(green[0], 51, 2);
    EXPECT_NEAR(green[1], 230, 2);
    EXPECT_NEAR(green[2], 51, 2);
    for (int channel = 0; channel < 4; channel++) {
        EXPECT_NEAR(alpha[channel], hsl[channel], 2);
    }
    EXPECT_NEAR(alpha[0], 127, 2);
    EXPECT_GT(alpha[1], 245);
    EXPECT_GT(alpha[2], 245);
    const unsigned char* missing = image.pixels +
        ((size_t)10 * image.width + 110) * 4;
    const unsigned char* black = image.pixels +
        ((size_t)10 * image.width + 135) * 4;
    EXPECT_LT(missing[0], 10);
    EXPECT_GT(missing[1], 245);
    EXPECT_LT(missing[2], 10);
    for (int channel = 0; channel < 3; channel++) EXPECT_LT(black[channel], 10);
    image_free(image.pixels);
}

TEST(RenderOutputParity, AngleUnitsAgreeAcrossColorGradientAndFilter) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/css_angle_units.html";
    const char* png_path = "temp/render_output_parity/css_angle_units.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;width:60px;height:30px}"
        ".left{left:0}.right{left:70px}"
        ".hsl1{top:0;background:hsl(180deg 100% 50%)}"
        ".hsl2{top:40px;background:hsl(200grad 100% 50%)}"
        ".grad1{top:80px;background:linear-gradient(90deg,red,blue)}"
        ".grad2{top:120px;background:linear-gradient(100grad,red,blue)}"
        ".filter{top:160px;background:red}"
        ".conic{top:200px;background:conic-gradient(from 0deg,red,blue,red)}"
        ".right.hsl1{background:hsl(.5turn 100% 50%)}"
        ".right.hsl2{background:hsl(3.14159265rad 100% 50%)}"
        ".right.grad1{background:linear-gradient(.25turn,red,blue)}"
        ".right.grad2{background:linear-gradient(1.57079633rad,red,blue)}"
        ".left.filter{filter:hue-rotate(180deg)}"
        ".right.filter{filter:hue-rotate(.5turn)}"
        ".right.conic{background:conic-gradient(from 1turn,red,blue,red)}"
        "</style>"
        "<div class='left hsl1'></div><div class='right hsl1'></div>"
        "<div class='left hsl2'></div><div class='right hsl2'></div>"
        "<div class='left grad1'></div><div class='right grad1'></div>"
        "<div class='left grad2'></div><div class='right grad2'></div>"
        "<div class='left filter'></div><div class='right filter'></div>"
        "<div class='left conic'></div><div class='right conic'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 130);
    ASSERT_GE(image.height, 230);
    const unsigned char* hsl = image.pixels +
        ((size_t)15 * image.width + 10) * 4;
    EXPECT_LT(hsl[0], 10);
    EXPECT_GT(hsl[1], 245);
    EXPECT_GT(hsl[2], 245);
    const unsigned char* gradient_left = image.pixels +
        ((size_t)95 * image.width + 10) * 4;
    const unsigned char* gradient_right = image.pixels +
        ((size_t)95 * image.width + 50) * 4;
    EXPECT_GT(gradient_left[0], gradient_left[2]);
    EXPECT_GT(gradient_right[2], gradient_right[0]);
    const unsigned char* hue_rotated = image.pixels +
        ((size_t)175 * image.width + 10) * 4;
    EXPECT_GT(hue_rotated[1], hue_rotated[0] + 60);
    const int sample_y[] = {15, 55, 95, 135, 175, 215};
    for (int y : sample_y) {
        for (int x : {10, 30, 50}) {
            const unsigned char* left = image.pixels +
                ((size_t)y * image.width + x) * 4;
            const unsigned char* right = image.pixels +
                ((size_t)y * image.width + x + 70) * 4;
            for (int channel = 0; channel < 4; channel++) {
                EXPECT_NEAR(left[channel], right[channel], 2)
                    << "at x=" << x << ", y=" << y << ", channel=" << channel;
            }
        }
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, DropShadowUsesNamedAndCurrentColor) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/drop_shadow_colors.html";
    const char* png_path = "temp/render_output_parity/drop_shadow_colors.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;left:0;width:20px;height:20px;background:red}"
        "#named{top:0;filter:drop-shadow(25px 0 0 lime)}"
        "#current{top:30px;color:blue;filter:drop-shadow(25px 0 0 currentColor)}"
        "</style><div id=named></div><div id=current></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 50);
    ASSERT_GE(image.height, 50);
    const unsigned char* named = image.pixels +
        ((size_t)10 * image.width + 35) * 4;
    const unsigned char* current = image.pixels +
        ((size_t)40 * image.width + 35) * 4;
    EXPECT_LT(named[0], 20);
    EXPECT_GT(named[1], 240);
    EXPECT_LT(named[2], 20);
    EXPECT_LT(current[0], 20);
    EXPECT_LT(current[1], 20);
    EXPECT_GT(current[2], 240);
    image_free(image.pixels);
}

TEST(RenderOutputParity, GradientCornerDirectionUsesPaintedBoxAspectRatio) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/gradient_corner.html";
    const char* png_path = "temp/render_output_parity/gradient_corner.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{width:200px;height:100px;"
        "background:linear-gradient(to top right,red,white,blue)}"
        "</style><div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 200);
    ASSERT_GE(image.height, 100);
    const unsigned char* top_left = image.pixels +
        ((size_t)2 * image.width + 2) * 4;
    const unsigned char* bottom_right = image.pixels +
        ((size_t)97 * image.width + 197) * 4;
    const unsigned char* bottom_left = image.pixels +
        ((size_t)97 * image.width + 2) * 4;
    const unsigned char* top_right = image.pixels +
        ((size_t)2 * image.width + 197) * 4;
    for (int channel = 0; channel < 3; channel++) {
        EXPECT_GT(top_left[channel], 230);
        EXPECT_GT(bottom_right[channel], 230);
    }
    EXPECT_GT(bottom_left[0], 230);
    EXPECT_LT(bottom_left[2], 25);
    EXPECT_LT(top_right[0], 25);
    EXPECT_GT(top_right[2], 230);
    image_free(image.pixels);
}

TEST(RenderOutputParity, GradientCentersResolveKeywordsPercentagesAndLengths) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/gradient_centers.html";
    const char* png_path = "temp/render_output_parity/gradient_centers.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;width:80px;height:80px}"
        ".a{left:0}.b{left:90px}.c{left:180px}"
        ".conic{top:0}.radial{top:90px}"
        ".conic.a{background:conic-gradient(at left top,red,blue)}"
        ".conic.b{background:conic-gradient(at 0% 0%,red,blue)}"
        ".conic.c{background:conic-gradient(red,blue)}"
        ".radial.a{background:radial-gradient(circle at 20px 20px,red,blue)}"
        ".radial.b{background:radial-gradient(circle at 25% 25%,red,blue)}"
        ".radial.c{background:radial-gradient(circle,red,blue)}"
        "</style><div class='a conic'></div><div class='b conic'></div>"
        "<div class='c conic'></div><div class='a radial'></div>"
        "<div class='b radial'></div><div class='c radial'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 260);
    ASSERT_GE(image.height, 170);
    for (int y : {10, 30, 60, 100, 120, 150}) {
        for (int x : {10, 40, 65}) {
            const unsigned char* keyword_or_length = image.pixels +
                ((size_t)y * image.width + x) * 4;
            const unsigned char* percentage = image.pixels +
                ((size_t)y * image.width + x + 90) * 4;
            for (int channel = 0; channel < 4; channel++) {
                EXPECT_NEAR(keyword_or_length[channel], percentage[channel], 2)
                    << "at x=" << x << ", y=" << y << ", channel=" << channel;
            }
        }
    }
    const unsigned char* conic_corner = image.pixels +
        ((size_t)10 * image.width + 40) * 4;
    const unsigned char* conic_default = image.pixels +
        ((size_t)10 * image.width + 220) * 4;
    EXPECT_GT(conic_default[0], conic_corner[0] + 60);
    EXPECT_GT(conic_corner[2], conic_default[2] + 60);
    const unsigned char* radial_offset = image.pixels +
        ((size_t)110 * image.width + 20) * 4;
    const unsigned char* radial_default = image.pixels +
        ((size_t)110 * image.width + 200) * 4;
    EXPECT_GT(radial_offset[0], radial_default[0] + 30);
    image_free(image.pixels);
}

TEST(RenderOutputParity, ConicAngleStopsSetRepeatPeriod) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/conic_angle_stops.html";
    const char* png_path = "temp/render_output_parity/conic_angle_stops.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;width:80px;height:80px}"
        "#angles{left:0;top:0;background-image:repeating-conic-gradient(red 0deg,blue 90deg)}"
        "#percent{left:90px;background:repeating-conic-gradient(red 0%,blue 25%)}"
        "#once{left:180px;background:conic-gradient(red 0deg,blue 90deg)}"
        "#percent,#once{top:0}"
        "#radial-positioned{left:0;top:90px;background:radial-gradient(red 0%,blue 100%)}"
        "#radial-default{left:90px;top:90px;background:radial-gradient(red,blue)}"
        "</style><div id=angles></div><div id=percent></div><div id=once></div>"
        "<div id=radial-positioned></div><div id=radial-default></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 260);
    ASSERT_GE(image.height, 170);
    for (int y : {20, 60}) {
        for (int x : {20, 60}) {
            const unsigned char* angle = image.pixels +
                ((size_t)y * image.width + x) * 4;
            const unsigned char* percent = image.pixels +
                ((size_t)y * image.width + x + 90) * 4;
            for (int channel = 0; channel < 4; channel++) {
                EXPECT_NEAR(angle[channel], percent[channel], 2)
                    << "at x=" << x << ", y=" << y << ", channel=" << channel;
            }
        }
    }
    const unsigned char* repeating = image.pixels +
        ((size_t)60 * image.width + 60) * 4;
    const unsigned char* once = image.pixels +
        ((size_t)60 * image.width + 240) * 4;
    EXPECT_GT(repeating[0], once[0] + 80);
    EXPECT_GT(once[2], repeating[2] + 80);
    for (int y : {100, 125, 150}) {
        for (int x : {10, 40, 65}) {
            const unsigned char* positioned = image.pixels +
                ((size_t)y * image.width + x) * 4;
            const unsigned char* unpositioned = image.pixels +
                ((size_t)y * image.width + x + 90) * 4;
            for (int channel = 0; channel < 4; channel++) {
                EXPECT_NEAR(positioned[channel], unpositioned[channel], 2)
                    << "at x=" << x << ", y=" << y << ", channel=" << channel;
            }
        }
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, RepeatingRadialGradientUsesColorStopPeriod) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/repeating_radial.html";
    const char* png_path = "temp/render_output_parity/repeating_radial.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;top:0;width:80px;height:80px}"
        "#repeat{left:0;background-image:repeating-radial-gradient(circle,red 0%,blue 25%)}"
        "#once{left:90px;background:radial-gradient(circle,red 0%,blue 25%)}"
        "</style><div id=repeat></div><div id=once></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 170);
    ASSERT_GE(image.height, 80);
    const unsigned char* repeating = image.pixels +
        ((size_t)40 * image.width + 69) * 4;
    const unsigned char* once = image.pixels +
        ((size_t)40 * image.width + 159) * 4;
    EXPECT_GT(repeating[0], once[0] + 150);
    EXPECT_GT(once[2], repeating[2] + 150);
    image_free(image.pixels);
}

TEST(RenderOutputParity, AppearanceNoneTextInputShowsParentBackground) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/appearance_none_text.html";
    const char* png_path = "temp/render_output_parity/appearance_none_text.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}.parent{width:120px;"
        "height:35px;background:magenta}input{width:80px;height:30px;"
        "border:0;margin:0;padding:0}</style>"
        "<div class=parent><input style='appearance:none' value=A></div>"
        "<div class=parent><input style='appearance:auto' value=A></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GT(image.width, 80);
    ASSERT_GT(image.height, 70);
    const unsigned char* primitive = image.pixels +
        ((size_t)15 * image.width + 40) * 4;
    const unsigned char* native = image.pixels +
        ((size_t)50 * image.width + 40) * 4;
    EXPECT_GT(primitive[0], 200);
    EXPECT_LT(primitive[1], 50);
    EXPECT_GT(primitive[2], 200);
    EXPECT_GT(native[0], 230);
    EXPECT_GT(native[1], 230);
    EXPECT_GT(native[2], 230);
    image_free(image.pixels);
}

TEST(RenderOutputParity, DefinedSelectorRestylesAfterCustomElementRegistration) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/defined_selector.html";
    const char* svg_path = "temp/render_output_parity/defined_selector.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "x-card,x-pending{display:block;height:10px;background:blue}"
        "x-card:defined,x-pending:defined{width:80px;background:green}"
        "x-card:not(:defined),x-pending:not(:defined){width:20px;background:red}"
        "</style><x-card></x-card><x-pending></x-pending>"
        "<script>"
        "if(document.querySelector('x-card:defined'))"
        "document.querySelector('x-card').style.background='magenta';"
        "customElements.define('x-card',class extends HTMLElement{});"
        "if(document.querySelectorAll('x-card:defined').length!==1)"
        "document.querySelector('x-card').style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
}

TEST(RenderOutputParity, DefaultSelectorUsesFormDefaults) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/default_selector.html";
    const char* svg_path = "temp/render_output_parity/default_selector.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#signal{display:block;width:80px;height:10px;background:red}"
        "button:default ~ #signal{background:green}"
        "</style><form>"
        "<button id='first' disabled>One</button><button id='second'>Two</button>"
        "<input id='check' type='checkbox' checked>"
        "<select><option id='option' selected>Choice</option></select>"
        "<div id='signal'></div></form>"
        "<script>"
        "let defaults=document.querySelectorAll(':default');"
        "if(defaults.length!==3||!document.querySelector('#first:default')||"
        "document.querySelector('#second:default')||"
        "!document.querySelector('#check:default')||"
        "!document.querySelector('#option:default'))"
        "document.querySelector('#signal').style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, IndeterminateSelectorTracksFormState) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/indeterminate_selector.html";
    const char* svg_path = "temp/render_output_parity/indeterminate_selector.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#signal{display:block;width:80px;height:10px;background:red}"
        "#check:indeterminate ~ #signal{background:green}"
        "</style><input id='check' type='checkbox'>"
        "<input id='radio1' type='radio' name='choice'>"
        "<input id='radio2' type='radio' name='choice'>"
        "<progress id='progress'></progress><div id='signal'></div>"
        "<script>"
        "document.querySelector('#check').indeterminate=true;"
        "document.querySelector('#radio1').checked=true;"
        "if(document.querySelector('#radio1:indeterminate')||"
        "document.querySelector('#radio2:indeterminate'))"
        "document.querySelector('#signal').style.background='magenta';"
        "document.querySelector('#radio1').checked=false;"
        "if(document.querySelectorAll(':indeterminate').length!==4||"
        "!document.querySelector('#check').indeterminate)"
        "document.querySelector('#signal').style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, HiddenAttributeYieldsToAuthoredDisplay) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/hidden_display.html";
    const char* svg_path = "temp/render_output_parity/hidden_display.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#shown[hidden]{display:block;width:50px;height:10px;background:green}"
        "</style><div hidden id='default'>hidden</div>"
        "<div hidden id='shown'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"50.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_FALSE(file_contains_text(svg_path, "hidden"));
}

TEST(RenderOutputParity, CascadeLayersOrderNormalAndImportantDeclarations) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/cascade_layers.html";
    const char* svg_path = "temp/render_output_parity/cascade_layers.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "@layer base,theme;"
        "@layer theme{#normal{background:green}#important{background:red!important}"
        "#plain{background:red}#priority{background:green!important}}"
        "@layer base{#normal{background:red}#important{background:green!important}}"
        "#plain{background:green}#priority{background:red!important}"
        "</style><div id='normal' style='width:51px;height:10px'></div>"
        "<div id='important' style='width:52px;height:10px'></div>"
        "<div id='plain' style='width:53px;height:10px'></div>"
        "<div id='priority' style='width:54px;height:10px'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    for (int width = 51; width <= 54; width++) {
        char expected[120];
        snprintf(expected, sizeof(expected),
            "width=\"%d.00\" height=\"10.00\" fill=\"rgb(0,128,0)\"",
            width);
        EXPECT_TRUE(file_contains_text(svg_path, expected)) << width;
    }
}

TEST(RenderOutputParity, CascadeLayersRevertAndNestedOrder) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/cascade_layer_revert.html";
    const char* svg_path = "temp/render_output_parity/cascade_layer_revert.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "@layer first,second;"
        "@layer first{#normal,#allRollback{background:green}"
        "#inline,#inlineRollback{background:green!important}"
        "#important{background:revert-layer!important}}"
        "@layer second{#normal{background:red}#normal{background:revert-layer}"
        "#allRollback{background:red;all:revert-layer}"
        "#important{background:blue!important}}"
        "@layer outer{@layer inner{#nested{background:red}"
        "#nestedImportant{background:green!important}}"
        "#nested{background:green}#nestedImportant{background:red!important}}"
        "</style><div id='normal' style='width:61px;height:10px'></div>"
        "<div id='important' style='width:62px;height:10px'></div>"
        "<div id='nested' style='width:63px;height:10px'></div>"
        "<div id='nestedImportant' style='width:64px;height:10px'></div>"
        "<div id='inline' style='width:65px;height:10px;background:red!important'></div>"
        "<div id='inlineRollback' style='width:66px;height:10px;"
        "background:revert-layer!important'></div>"
        "<div id='allRollback' style='width:67px;height:10px'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"61.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"62.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"63.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"64.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"65.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"66.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"67.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, AllInheritUsesParentComputedBoxAndTextValues) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/all_inherit.html";
    const char* svg_path = "temp/render_output_parity/all_inherit.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#parent{display:block;width:80px;height:40px;padding:5px;"
        "border:2px solid blue;background:green;color:red}"
        "#child{all:inherit}#explicit{color:inherit}"
        "</style><div id='parent'><span id='child'>x</span>"
        "<span id='explicit'>y</span></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"7.00\" y=\"7.00\" width=\"94.00\" height=\"54.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"7.00,7.00 101.00,7.00 99.00,9.00 9.00,9.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "fill=\"rgb(255,0,0)\">x</text>"));
    EXPECT_TRUE(file_contains_text(svg_path,
        "fill=\"rgb(255,0,0)\">y</text>"));
    EXPECT_FALSE(file_contains_text(svg_path, "data-radiant-fallback=\"effect-raster\""));
}

TEST(RenderOutputParity, AllInheritOpacityAndAllRevertPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/all_opacity_revert.html";
    const char* svg_path = "temp/render_output_parity/all_opacity_revert.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#parent{opacity:.5;width:20px;height:20px;background:red}"
        "#child{all:inherit;background:blue}"
        "#revert{color:red;background:red;width:30px;height:10px;all:revert}"
        "</style><div id='parent'><div id='child'></div></div>"
        "<div id='revert'>r</div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<g opacity=\"0.5000\">\n            <rect x=\"0.00\" y=\"0.00\" width=\"20.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "fill=\"rgb(0,0,0)\">r</text>"));
    EXPECT_FALSE(file_contains_text(svg_path,
        "width=\"30.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
}

TEST(RenderOutputParity, LogicalBorderPairsMapByDirectionAndWritingMode) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/logical_border_pairs.html";
    const char* svg_path = "temp/render_output_parity/logical_border_pairs.svg";
    const char* rtl_html =
        "<!doctype html><style>html,body{margin:0}#box{direction:rtl;"
        "width:30px;height:20px;border-inline-width:3px 5px;"
        "border-inline-style:solid;border-inline-color:red blue}"
        "</style><div id='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, rtl_html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"35.00,0.00 38.00,0.00 38.00,20.00 35.00,20.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"0.00,0.00 5.00,0.00 5.00,20.00 0.00,20.00\" fill=\"rgb(0,0,255)\""));

    const char* vertical_html =
        "<!doctype html><style>html,body{margin:0}#box{writing-mode:vertical-rl;"
        "width:30px;height:20px;border-inline-width:3px 5px;"
        "border-inline-style:solid;border-inline-color:red blue;"
        "border-block-width:2px 4px;border-block-style:solid;"
        "border-block-color:green purple}"
        "</style><div id='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, vertical_html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"0.00,0.00 36.00,0.00 34.00,3.00 4.00,3.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"34.00,3.00 36.00,0.00 36.00,28.00 34.00,23.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"4.00,23.00 34.00,23.00 36.00,28.00 0.00,28.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"0.00,0.00 4.00,3.00 4.00,23.00 0.00,28.00\" fill=\"rgb(128,0,128)\""));

    const char* single_html =
        "<!doctype html><style>html,body{margin:0}#box{direction:rtl;"
        "width:30px;height:20px;border:3px solid black;"
        "border-inline-start-style:none;border-inline-end-color:lime;"
        "border-block-start-width:5px;border-block-end-style:none}"
        "</style><div id='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, single_html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"0.00,0.00 33.00,0.00 33.00,5.00 3.00,5.00\" fill=\"rgb(0,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "points=\"0.00,0.00 3.00,5.00 3.00,25.00 0.00,25.00\" fill=\"rgb(0,255,0)\""));
    EXPECT_FALSE(file_contains_text(svg_path, "points=\"33.00,5.00"));
}

TEST(RenderOutputParity, LogicalAndPhysicalBordersRespectLayerOrder) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/logical_border_layer.html";
    const char* svg_path = "temp/render_output_parity/logical_border_layer.svg";
    const char* normal =
        "<!doctype html><style>html,body{margin:0}@layer early,late;"
        "#box{width:30px;height:20px;border-left-width:4px;border-left-style:solid}"
        "@layer late{#box{border-inline-start-color:red}}"
        "@layer early{#box{border-left-color:blue}}"
        "</style><div id='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, normal));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<rect x=\"0.00\" y=\"0.00\" width=\"4.00\" height=\"20.00\" fill=\"rgb(255,0,0)\""));

    const char* important =
        "<!doctype html><style>html,body{margin:0}@layer early,late;"
        "#box{width:30px;height:20px;border-left-width:4px;border-left-style:solid}"
        "@layer late{#box{border-inline-start-color:red!important}}"
        "@layer early{#box{border-left-color:blue!important}}"
        "</style><div id='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, important));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<rect x=\"0.00\" y=\"0.00\" width=\"4.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, LogicalCornerRadiiMapAndCompeteWithPhysicalCorners) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/logical_corner_radii.html";
    const char* svg_path = "temp/render_output_parity/logical_corner_radii.svg";
    const char* base =
        "width:80px;height:40px;background:green;border:2px solid red;"
        "border-start-start-radius:10px;border-start-end-radius:20px;"
        "border-end-start-radius:3px;border-end-end-radius:4px";
    char html[1024];
    snprintf(html, sizeof(html),
        "<!doctype html><style>html,body{margin:0}#box{%s}</style><div id='box'></div>",
        base);
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<path d=\"M10.00,0.00 L64.00,0.00"));

    snprintf(html, sizeof(html),
        "<!doctype html><style>html,body{margin:0}#box{direction:rtl;%s}</style><div id='box'></div>",
        base);
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<path d=\"M20.00,0.00 L74.00,0.00"));

    snprintf(html, sizeof(html),
        "<!doctype html><style>html,body{margin:0}#box{writing-mode:vertical-rl;%s}</style><div id='box'></div>",
        base);
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<path d=\"M3.00,0.00 L74.00,0.00"));

    snprintf(html, sizeof(html),
        "<!doctype html><style>html,body{margin:0}#box{%s;border-top-left-radius:15px}</style><div id='box'></div>",
        base);
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "<path d=\"M15.00,0.00 L64.00,0.00"));

    const char* inherited_html =
        "<!doctype html><style>html,body{margin:0}"
        "#parent{border-start-start-radius:17px;background:red;"
        "width:80px;height:40px}"
        "#child{writing-mode:vertical-rl;border-start-start-radius:inherit;"
        "background:blue;width:40px;height:20px}"
        "</style><div id='parent'><div id='child'></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, inherited_html));
    // The child's logical corner maps to its physical top-right before inherit.
    EXPECT_TRUE(file_contains_text(svg_path,
        "<rect x=\"0.00\" y=\"0.00\" width=\"40.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, CascadeLayersShareOrderAcrossStylesheets) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/cascade_layer_sheets.html";
    const char* svg_path = "temp/render_output_parity/cascade_layer_sheets.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}@layer first,second;"
        "#invalid{background:green}</style>"
        "<style>@layer second{#cross{background:green}}</style>"
        "<style>@layer first{#cross{background:red}}"
        "@layer first,second{#invalid{background:red}}</style>"
        "<div id='cross' style='width:67px;height:10px'></div>"
        "<div id='invalid' style='width:68px;height:10px'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"67.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"68.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, ImportedRulesKeepTheirSourcePosition) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* child_css = "temp/render_output_parity/cascade_import_child.css";
    const char* conditional_css =
        "temp/render_output_parity/cascade_import_conditional.css";
    const char* anonymous_css =
        "temp/render_output_parity/cascade_import_anonymous.css";
    const char* parent_css = "temp/render_output_parity/cascade_import_parent.css";
    const char* child =
        "#cross{background:green}#order{background:green}";
    const char* parent =
        "@layer first,second;@import 'cascade_import_child.css' "
        "layer(second) supports(display:block) screen;"
        "@import 'cascade_import_anonymous.css' layer;"
        "@import 'cascade_import_conditional.css' print;"
        "@import 'cascade_import_conditional.css' supports(display:bogus);"
        "@layer first{#cross{background:red}}"
        "@layer second{#anonymous{background:red}}"
        "#order{background:red}#conditional{background:green}";
    ASSERT_TRUE(write_file_all(child_css, child, strlen(child)));
    const char* anonymous = "#anonymous{background:green}";
    ASSERT_TRUE(write_file_all(anonymous_css, anonymous,
        strlen(anonymous)));
    const char* conditional = "#conditional{background:red}";
    ASSERT_TRUE(write_file_all(conditional_css, conditional,
        strlen(conditional)));
    ASSERT_TRUE(write_file_all(parent_css, parent, strlen(parent)));
    const char* html =
        "<!doctype html><style>html,body{margin:0}</style>"
        "<link rel='stylesheet' href='cascade_import_parent.css'>"
        "<div id='cross' style='width:69px;height:10px'></div>"
        "<div id='order' style='width:70px;height:10px'></div>"
        "<div id='conditional' style='width:71px;height:10px'></div>"
        "<div id='anonymous' style='width:72px;height:10px'></div>";
    const char* svg_path = "temp/render_output_parity/cascade_import.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/cascade_import.html", svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"69.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"70.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"71.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"72.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, RangeSelectorsTrackLiveNumericValue) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/range_selectors.html";
    const char* svg_path = "temp/render_output_parity/range_selectors.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#signal{display:block;width:80px;height:10px;background:red}"
        "#over:in-range ~ #signal{background:green}"
        "</style><input id='over' type='number' value='15' min='0' max='10'>"
        "<input id='inside' type='number' value='5' min='0' max='10'>"
        "<input id='free' type='number' value='5'>"
        "<input id='range' type='range' value='5' min='0' max='10'>"
        "<div id='signal'></div>"
        "<script>"
        "let over=document.querySelector('#over');"
        "let good=over.matches(':out-of-range')&&"
        "document.querySelector('#inside').matches(':in-range')&&"
        "!document.querySelector('#free').matches(':in-range')&&"
        "document.querySelector('#range').matches(':in-range');"
        "over.value='5';"
        "if(!good||over.value!=='5'||!over.matches(':in-range')||"
        "over.matches(':out-of-range'))"
        "document.querySelector('#signal').style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, ValiditySelectorsUseInitialAndLiveConstraints) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/validity_selectors.html";
    const char* svg_path = "temp/render_output_parity/validity_selectors.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#signal{display:block;width:80px;height:10px;background:red}"
        "form:invalid ~ #signal{background:green}"
        "input:user-invalid ~ #signal{background:blue}"
        "</style><form id='form'><fieldset id='field'>"
        "<input id='needed' required></fieldset></form><div id='signal'></div>"
        "<script>"
        "let form=document.querySelector('#form');"
        "let needed=document.querySelector('#needed');"
        "let signal=document.querySelector('#signal');"
        "if(!form.matches(':invalid')||"
        "!document.querySelector('#field').matches(':invalid')||"
        "!needed.matches(':invalid')||needed.matches(':user-invalid')||"
        "document.querySelectorAll(':valid').length!==0)"
        "signal.style.background='magenta';"
        "form.requestSubmit();"
        "if(!needed.matches(':user-invalid'))signal.style.background='magenta';"
        "form.reset();"
        "if(needed.matches(':user-invalid'))signal.style.background='magenta';"
        "form.requestSubmit();"
        "needed.value='ok';"
        "if(!needed.matches(':user-valid')||form.matches(':invalid'))"
        "signal.style.background='magenta';"
        "needed.value='';"
        "if(!needed.matches(':user-invalid')||!form.matches(':invalid'))"
        "signal.style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, StaticValidityAndPlaceholderSelectorsPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/static_validity.html";
    const char* svg_path = "temp/render_output_parity/static_validity.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#validity,#placeholder{display:block;width:80px;height:10px;background:red}"
        "form:invalid + #validity{background:green}"
        "input:placeholder-shown + #placeholder{background:blue}"
        "</style><form><input required></form><div id='validity'></div>"
        "<input placeholder='hint'><div id='placeholder'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, HasSelectorRestylesAfterChildAndSiblingMutation) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/has_mutation.html";
    const char* svg_path = "temp/render_output_parity/has_mutation.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#struct,#sibling{display:block;width:80px;height:10px;background:red}"
        "#struct:has(> .hit){background:green}"
        "#sibling:has(+ .adj){background:blue}"
        "</style><div id='struct'></div><div id='sibling'></div><div id='next'></div>"
        "<script>"
        "let target=document.querySelector('#struct');"
        "let sibling=document.querySelector('#sibling');"
        "let next=document.querySelector('#next');"
        "let child=document.createElement('span');"
        "child.className='hit';target.appendChild(child);"
        "next.className='adj';"
        "if(!target.matches(':has(> .hit)')||"
        "!sibling.matches(':has(+ .adj)'))"
        "sibling.style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, ColumnCombinatorStylesCellsAfterColumnMutation) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/column_selector.html";
    const char* svg_path = "temp/render_output_parity/column_selector.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "table{border-collapse:collapse}td{width:30px;height:20px;background:red}"
        "col.selected || td{background:green}"
        "</style><table><colgroup><col id='first'><col id='second' class='selected'>"
        "</colgroup><tbody><tr><td id='a'></td><td id='b'></td></tr>"
        "<tr><td id='c'></td><td id='d'></td></tr></tbody></table>"
        "<script>"
        "let first=document.querySelector('#first');"
        "let second=document.querySelector('#second');"
        "let cells=document.querySelectorAll('col.selected || td');"
        "if(cells.length!==2||cells[0].id!=='b'||cells[1].id!=='d')"
        "document.querySelector('#a').style.background='blue';"
        "second.className='';first.className='selected';"
        "if(!document.querySelector('#a').matches('col.selected || td'))"
        "document.querySelector('#a').style.background='blue';"
        "first.className='';second.className='selected';"
        "document.querySelector('#a').setAttribute('colspan','2');"
        "if(!document.querySelector('#a').matches('col.selected || td')||"
        "document.querySelector('#b').matches('col.selected || td'))"
        "document.querySelector('#a').style.background='blue';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"0.00\" width=\"64.00\" height=\"22.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"64.00\" y=\"0.00\" width=\"32.00\" height=\"22.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"32.00\" y=\"22.00\" width=\"32.00\" height=\"22.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_FALSE(file_contains_text(svg_path, "fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, PercentageScaleMatchesNumericTransform) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/scale_percent.html";
    const char* svg_path = "temp/render_output_parity/scale_percent.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        ".box{width:40px;height:40px;background:red;transform-origin:0px 0px}"
        "#numeric{transform:scale(0.5)}#percent{transform:scale(50%)}"
        "</style><div id='numeric' class='box'></div>"
        "<div id='percent' class='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "transform=\"matrix(0.5 0 0 0.5 0 0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "transform=\"matrix(0.5 0 0 0.5 0 20)\""));
}

TEST(RenderOutputParity, UnitlessZeroTransformOriginMatchesLengthZero) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/origin_zero.html";
    const char* svg_path = "temp/render_output_parity/origin_zero.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        ".box{width:40px;height:40px;background:red;transform:scale(0.5)}"
        "#length{transform-origin:0px 0px}"
        "#unitless{transform-origin:0 0}"
        "</style><div id='length' class='box'></div>"
        "<div id='unitless' class='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "transform=\"matrix(0.5 0 0 0.5 0 0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "transform=\"matrix(0.5 0 0 0.5 0 20)\""));
}

TEST(RenderOutputParity, DialogAndPopoverStateSelectorsRestyle) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* dialog_html =
        "<!doctype html><style>html,body{margin:0}"
        "#signal{display:block;width:80px;height:10px;background:red}"
        "#closedSignal{display:block;width:82px;height:10px;background:red}"
        "#done{display:block;width:81px;height:10px;background:red}"
        "dialog:modal ~ #signal{background:green}"
        "#closed:modal + #closedSignal{background:green}"
        "</style><dialog id='d'>Dialog</dialog><div id='signal'></div>"
        "<dialog id='closed'>Closed</dialog><div id='closedSignal'></div>"
        "<div id='done'></div>"
        "<script>let d=document.querySelector('#d');d.showModal();"
        "let other=document.querySelector('#closed');other.showModal();"
        "let opened=other.matches(':modal');other.removeAttribute('open');"
        "let closed=!other.matches(':modal');"
        "if(!opened||!closed||!d.matches(':modal'))"
        "document.querySelector('#signal').style.background='magenta';"
        "document.querySelector('#done').style.background='yellow';</script>";
    const char* dialog_svg = "temp/render_output_parity/modal_selector.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/modal_selector.html", dialog_svg, dialog_html));
    EXPECT_TRUE(file_contains_text(dialog_svg,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(dialog_svg,
        "width=\"82.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(dialog_svg,
        "width=\"81.00\" height=\"10.00\" fill=\"rgb(255,255,0)\""));

    const char* popover_html =
        "<!doctype html><style>html,body{margin:0}"
        "#signal{display:block;width:80px;height:10px;background:red}"
        "#closedSignal{display:block;width:82px;height:10px;background:red}"
        "#done{display:block;width:81px;height:10px;background:red}"
        "#pop:popover-open ~ #signal{background:blue}"
        "#closed:popover-open + #closedSignal{background:blue}"
        "</style><div id='pop' popover='manual'>Popover</div><div id='signal'></div>"
        "<div id='closed' popover='manual'>Closed</div><div id='closedSignal'></div>"
        "<div id='done'></div>"
        "<script>let p=document.querySelector('#pop');p.showPopover();"
        "let other=document.querySelector('#closed');other.showPopover();"
        "let opened=other.matches(':popover-open');other.hidePopover();"
        "let closed=!other.matches(':popover-open');"
        "if(!opened||!closed||!p.matches(':popover-open'))"
        "document.querySelector('#signal').style.background='magenta';"
        "document.querySelector('#done').style.background='yellow';</script>";
    const char* popover_svg = "temp/render_output_parity/popover_selector.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/popover_selector.html", popover_svg, popover_html));
    EXPECT_TRUE(file_contains_text(popover_svg,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(popover_svg,
        "width=\"82.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(popover_svg,
        "width=\"81.00\" height=\"10.00\" fill=\"rgb(255,255,0)\""));
}

TEST(RenderOutputParity, NamespaceQualifiedStylesheetSelectorsPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/namespace_selectors.html";
    const char* svg_path = "temp/render_output_parity/namespace_selectors.svg";
    const char* html =
        "<!doctype html><style>"
        "@namespace html \"http://www.w3.org/1999/xhtml\";"
        "@namespace \"http://www.w3.org/2000/svg\";"
        "html,body{margin:0}"
        "#named,#wild{display:block;width:80px;height:10px;background:red}"
        "#done{display:block;width:81px;height:10px;background:red}"
        "html|div#named{background:green}"
        "div#named,|div#named{background:magenta}"
        "*|div#wild{background:blue}"
        "</style><div id='named'></div><div id='wild'></div>"
        "<svg width='1' height='1'><rect id='rect' width='1' height='1'/></svg>"
        "<div id='done'></div>"
        "<script>let named=document.querySelector('#named');"
        "let rect=document.querySelector('#rect');"
        "if(!named.matches('*|div')||named.matches('|div')||"
        "!rect.matches('*|rect')||rect.matches('|rect'))"
        "named.style.background='magenta';"
        "document.querySelector('#done').style.background='yellow';</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"81.00\" height=\"10.00\" fill=\"rgb(255,255,0)\""));
}

TEST(RenderOutputParity, NamespacedAttributesRestyleAndMatchLiveQueries) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/namespace_attributes.html";
    const char* svg_path = "temp/render_output_parity/namespace_attributes.svg";
    const char* html =
        "<!doctype html><style>"
        "@namespace x \"urn:test\";"
        "@namespace l \"http://www.w3.org/1999/xlink\";"
        "html,body{margin:0}div{width:80px;height:10px;background:red}"
        "[x|mark='a']{background:green}"
        "[|mark='a']{background:blue}"
        "[l|href='#icon']{background:purple}"
        "#done{width:81px;background:red}"
        "</style><div id='qualified'></div><div id='plain'></div>"
        "<div id='xlink'></div><div id='prefixless'></div>"
        "<div id='removed'></div>"
        "<div id='done'></div><script>"
        "let q=document.querySelector('#qualified');"
        "let p=document.querySelector('#plain');"
        "let x=document.querySelector('#xlink');"
        "let n=document.querySelector('#prefixless');"
        "let r=document.querySelector('#removed');"
        "q.setAttributeNS('urn:test','custom:mark','a');"
        "p.setAttribute('mark','a');"
        "x.setAttributeNS('http://www.w3.org/1999/xlink','xlink:href','#icon');"
        "n.setAttributeNS('urn:test','mark','a');"
        "r.setAttributeNS('urn:test','custom:mark','a');"
        "r.removeAttributeNS('urn:test','mark');"
        "r.setAttribute('custom:mark','a');"
        "let ok=q.getAttributeNS('urn:test','mark')==='a'&&"
        "q.matches('[*|mark]')&&!q.matches('[|mark]')&&"
        "p.matches('[|mark]')&&p.matches('[*|mark]')&&"
        "x.getAttributeNS('http://www.w3.org/1999/xlink','href')==='#icon'&&"
        "x.getAttribute('href')===null&&x.matches('[*|href]')&&"
        "!x.matches('[|href]')&&n.matches('[*|mark]')&&"
        "!n.matches('[|mark]')&&!r.matches('[*|mark]')&&"
        "!r.matches('[|mark]')&&"
        "r.getAttributeNS('urn:test','mark')===null;"
        "if(ok)document.querySelector('#done').style.background='yellow';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(128,0,128)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"81.00\" height=\"10.00\" fill=\"rgb(255,255,0)\""));
}

TEST(RenderOutputParity, MarkerColorPaintsSeparatelyFromListText) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/marker_color.html";
    const char* svg_path = "temp/render_output_parity/marker_color.svg";
    const char* png_path = "temp/render_output_parity/marker_color.png";
    const char* html =
        "<!doctype html><style>li{color:red;list-style-type:disc}"
        "li.styled::marker{color:green}</style>"
        "<ul><li id='item'>marker text</li></ul>"
        "<script>document.querySelector('#item').className='styled'</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path, "fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path, "fill=\"rgb(255,0,0)\""));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int green_pixels = 0;
    int red_pixels = 0;
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            const unsigned char* pixel = image.pixels +
                ((size_t)y * image.width + x) * 4;
            if (pixel[3] < 200) continue;
            if (pixel[1] > pixel[0] + 30 && pixel[1] > pixel[2] + 20)
                green_pixels++;
            if (pixel[0] > pixel[1] + 60 && pixel[0] > pixel[2] + 60)
                red_pixels++;
        }
    }
    image_free(image.pixels);
    EXPECT_GT(green_pixels, 3);
    EXPECT_GT(red_pixels, 20);
}

TEST(RenderOutputParity, FileSelectorButtonPaintsSeparateFromHostAfterRestyle) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/file_button.html";
    const char* png_path = "temp/render_output_parity/file_button.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "input{width:253px;height:21px;color:black}"
        "input.live::file-selector-button{color:red;background:lime;"
        "border:2px solid blue;padding:2px}</style>"
        "<input id='file' type='file'>"
        "<script>document.querySelector('#file').className='live'</script>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 160);
    ASSERT_GE(image.height, 21);
    int blue = 0, green = 0, red = 0, host_black = 0;
    for (int y = 0; y < 21; y++) {
        for (int x = 0; x < 160; x++) {
            const unsigned char* pixel = image.pixels +
                ((size_t)y * image.width + x) * 4;
            if (pixel[3] < 200) continue;
            if (x < 90) {
                if (pixel[2] > 180 && pixel[0] < 80 && pixel[1] < 80) blue++;
                if (pixel[1] > 180 && pixel[0] < 80 && pixel[2] < 80) green++;
                if (pixel[0] > 180 && pixel[1] < 80 && pixel[2] < 80) red++;
            } else if (pixel[0] < 80 && pixel[1] < 80 && pixel[2] < 80) {
                host_black++;
            }
        }
    }
    image_free(image.pixels);
    EXPECT_GT(blue, 20);
    EXPECT_GT(green, 20);
    EXPECT_GT(red, 8);
    EXPECT_GT(host_black, 15);
}

TEST(RenderOutputParity, ImageRenderingCrispEdgesUsesSourcePixels) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/image_rendering_source.png";
    const char* html_path = "temp/render_output_parity/image_rendering.html";
    const char* png_path = "temp/render_output_parity/image_rendering.png";
    const unsigned char source_pixels[] = {
        255, 0, 0, 255,   0, 0, 255, 255,
        0, 255, 0, 255,   255, 255, 255, 255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source_pixels, 2, 2));
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "img{position:absolute;top:0;width:8px;height:8px}"
        "#crisp{left:0}#smooth{left:16px;image-rendering:smooth}"
        "#background{position:absolute;left:0;top:16px;width:8px;height:8px;"
        "image-rendering:crisp-edges;background-image:url(image_rendering_source.png);"
        "background-size:8px 8px;background-repeat:no-repeat}"
        "</style><div style='image-rendering:crisp-edges'>"
        "<img id='crisp' src='image_rendering_source.png'></div>"
        "<img id='smooth' src='image_rendering_source.png'>"
        "<div id='background'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 24);
    ASSERT_GE(image.height, 24);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_EQ(pixel(3, 2)[0], 255);
    EXPECT_EQ(pixel(3, 2)[1], 0);
    EXPECT_EQ(pixel(3, 2)[2], 0);
    EXPECT_EQ(pixel(4, 2)[2], 255);
    EXPECT_EQ(pixel(3, 4)[1], 255);
    EXPECT_GT(pixel(19, 2)[0], 30);
    EXPECT_GT(pixel(19, 2)[2], 30);
    EXPECT_EQ(pixel(3, 18)[0], 255);
    EXPECT_EQ(pixel(3, 18)[1], 0);
    EXPECT_EQ(pixel(4, 18)[2], 255);
    image_free(image.pixels);
}

TEST(RenderOutputParity, ImageRenderingPixelatedBlendsAtNonintegerScale) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/image_pixelated_source.png";
    const char* html_path = "temp/render_output_parity/image_pixelated.html";
    const char* png_path = "temp/render_output_parity/image_pixelated.png";
    const unsigned char source_pixels[] = {
        255, 0, 0, 255,   0, 0, 255, 255,
        0, 255, 0, 255,   255, 255, 255, 255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source_pixels, 2, 2));
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        "img{position:absolute;top:0;width:5px;height:5px}"
        "#pixelated{left:0;image-rendering:pixelated}"
        "#crisp{left:10px;image-rendering:crisp-edges}"
        "#background{position:absolute;left:20px;top:0;width:5px;height:5px;"
        "image-rendering:pixelated;background-image:url(image_pixelated_source.png);"
        "background-size:5px 5px;background-repeat:no-repeat}</style>"
        "<img id='pixelated' src='image_pixelated_source.png'>"
        "<img id='crisp' src='image_pixelated_source.png'>"
        "<div id='background'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 25);
    ASSERT_GE(image.height, 5);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_GT(pixel(1, 1)[0], 220);
    EXPECT_LT(pixel(1, 1)[2], 30);
    EXPECT_GT(pixel(2, 1)[0], 90);
    EXPECT_GT(pixel(2, 1)[2], 90);
    EXPECT_LT(pixel(12, 1)[0], 30);
    EXPECT_GT(pixel(12, 1)[2], 220);
    EXPECT_GT(pixel(22, 1)[0], 90);
    EXPECT_GT(pixel(22, 1)[2], 90);
    image_free(image.pixels);
}

TEST(RenderOutputParity, BackgroundShorthandUrlPaintsImage) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/background_shorthand_source.png";
    const char* html_path = "temp/render_output_parity/background_shorthand_url.html";
    const char* svg_path = "temp/render_output_parity/background_shorthand_url.svg";
    const char* png_path = "temp/render_output_parity/background_shorthand_url.png";
    const unsigned char source_pixels[] = {
        255, 0, 0, 255,   0, 0, 255, 255,
        0, 255, 0, 255,   255, 255, 255, 255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source_pixels, 2, 2));
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#box{width:8px;height:8px;background:url(background_shorthand_source.png) "
        "no-repeat;background-size:8px 8px;image-rendering:crisp-edges}"
        "</style><div id='box'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "background_shorthand_source.png"));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 8);
    ASSERT_GE(image.height, 8);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_GT(pixel(2, 2)[0], 220);
    EXPECT_LT(pixel(2, 2)[2], 30);
    EXPECT_GT(pixel(5, 2)[2], 220);
    EXPECT_GT(pixel(2, 5)[1], 220);
    image_free(image.pixels);
}

TEST(RenderOutputParity, MarkerPseudoFontSizesMeasuredAndPaintedGlyphs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/marker_pseudo_font.html";
    const char* svg_path = "temp/render_output_parity/marker_pseudo_font.svg";
    const char* png_path = "temp/render_output_parity/marker_pseudo_font.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "ol{margin:0;padding-left:80px}li{font:10px Arial;color:red}"
        "li.large::marker{font-size:30px;font-weight:bold;color:green}</style>"
        "<ol><li id='item'>Item</li></ol>"
        "<script>document.querySelector('#item').className='large'</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "font-size=\"30\" fill=\"rgb(0,128,0)\" font-weight=\"700\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "font-size=\"10.00\" fill=\"rgb(255,0,0)\">Item</text>"));

    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int first_green_y = image.height;
    int last_green_y = -1;
    for (int y = 0; y < image.height && y < 60; y++) {
        for (int x = 0; x < image.width && x < 80; x++) {
            const unsigned char* pixel = image.pixels +
                ((size_t)y * image.width + x) * 4;
            if (pixel[1] > 80 && pixel[0] < 40 && pixel[2] < 40) {
                if (y < first_green_y) first_green_y = y;
                if (y > last_green_y) last_green_y = y;
            }
        }
    }
    EXPECT_GE(last_green_y - first_green_y, 15);
    image_free(image.pixels);
}

TEST(RenderOutputParity, GridAutoFlowDenseBackfillsEarlierHole) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/grid_dense.html";
    const char* png_path = "temp/render_output_parity/grid_dense.png";
    const char* prefix =
        "<!doctype html><style>html,body{margin:0}"
        "#grid{display:grid;width:60px;grid-template-columns:repeat(3,20px);"
        "grid-template-rows:20px 20px;grid-auto-flow:";
    const char* suffix =
        "}#a{grid-column:span 2;background:red}"
        "#b{grid-column:span 2;background:blue}"
        "#c{background:lime}</style>"
        "<div id='grid'><div id='a'></div><div id='b'></div>"
        "<div id='c'></div></div>";
    char html[1024];
    snprintf(html, sizeof(html), "%srow;%s", prefix, suffix);
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData sparse = {};
    ASSERT_TRUE(load_png_rgba(png_path, &sparse));
    ASSERT_GE(sparse.width, 60);
    ASSERT_GE(sparse.height, 40);
    const unsigned char* sparse_hole = sparse.pixels +
        ((size_t)10 * sparse.width + 45) * 4;
    EXPECT_GT(sparse_hole[0], 230);
    EXPECT_GT(sparse_hole[1], 230);
    image_free(sparse.pixels);

    snprintf(html, sizeof(html), "%srow dense;%s", prefix, suffix);
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData dense = {};
    ASSERT_TRUE(load_png_rgba(png_path, &dense));
    ASSERT_GE(dense.width, 60);
    ASSERT_GE(dense.height, 40);
    const unsigned char* filled_hole = dense.pixels +
        ((size_t)10 * dense.width + 45) * 4;
    EXPECT_LT(filled_hole[0], 40);
    EXPECT_GT(filled_hole[1], 200);
    EXPECT_LT(filled_hole[2], 40);
    image_free(dense.pixels);

    const char* column_html =
        "<!doctype html><style>html,body{margin:0}"
        "#grid{display:grid;width:40px;height:60px;"
        "grid-template-columns:20px 20px;grid-template-rows:repeat(3,20px);"
        "grid-auto-flow:column dense}"
        "#a,#b{grid-row:span 2}#a{background:red}"
        "#b{background:blue}#c{background:lime}</style>"
        "<div id='grid'><div id='a'></div><div id='b'></div>"
        "<div id='c'></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, column_html));
    ImageData column_dense = {};
    ASSERT_TRUE(load_png_rgba(png_path, &column_dense));
    ASSERT_GE(column_dense.width, 40);
    ASSERT_GE(column_dense.height, 60);
    const unsigned char* column_hole = column_dense.pixels +
        ((size_t)45 * column_dense.width + 10) * 4;
    EXPECT_LT(column_hole[0], 40);
    EXPECT_GT(column_hole[1], 200);
    EXPECT_LT(column_hole[2], 40);
    image_free(column_dense.pixels);
}

TEST(RenderOutputParity, GridTracksResolveFontUnitsAndLengthCalc) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/grid_track_units.html";
    const char* svg_path = "temp/render_output_parity/grid_track_units.svg";
    const char* explicit_html =
        "<!doctype html><style>html,body{margin:0}"
        "#grid{display:grid;font-size:10px;width:50px;"
        "grid-template-columns:2em repeat(2,1.5em);grid-template-rows:2em}"
        "#grid>div:nth-child(1){background:red}"
        "#grid>div:nth-child(2){background:blue}"
        "#grid>div:nth-child(3){background:lime}</style>"
        "<div id='grid'><div></div><div></div><div></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, explicit_html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"0.00\" width=\"20.00\" height=\"20.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"20.00\" y=\"0.00\" width=\"15.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"35.00\" y=\"0.00\" width=\"15.00\" height=\"20.00\" fill=\"rgb(0,255,0)\""));

    const char* calc_html =
        "<!doctype html><style>html,body{margin:0}"
        "#grid{display:grid;font-size:10px;width:50px;"
        "grid-template-columns:calc(10px + 2.5px) minmax(1em,2em) 1fr;"
        "grid-auto-rows:2em}#grid>div:nth-child(1){background:red}"
        "#grid>div:nth-child(2){background:blue}"
        "#grid>div:nth-child(3){background:lime}"
        "#grid>div:nth-child(4){background:orange}</style>"
        "<div id='grid'><div></div><div></div><div></div><div></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, calc_html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"0.00\" width=\"13.00\" height=\"20.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"13.00\" y=\"0.00\" width=\"20.00\" height=\"20.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"20.00\" width=\"13.00\" height=\"20.00\" fill=\"rgb(255,165,0)\""));
}

TEST(RenderOutputParity, FocusedCaretPaintsAuthoredAndInheritedColors) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/caret_color.html";
    const char* events_path = "temp/render_output_parity/caret_color_ui.json";
    const char* html =
        "<!doctype html><style>body{margin:0}input{display:block;width:120px;"
        "height:40px;color:red;font-size:30px}"
        "#editor{caret-color:rgb(0,255,0)}#group{caret-color:blue}"
        "#reset{caret-color:auto}#rich{height:40px;font-size:30px;"
        "color:blue;caret-color:rgb(0,255,0)}</style>"
        "<input id='editor' value=''><div id='group'>"
        "<input id='inherited' value=''><input id='reset' value=''></div>"
        "<div id='rich' contenteditable='true'>word</div>";
    const char* events =
        "{\"name\":\"authored caret color\",\"html\":\"temp/render_output_parity/caret_color.html\","
        "\"viewport\":{\"width\":180,\"height\":190},"
        "\"events\":["
        "{\"type\":\"click\",\"target\":{\"selector\":\"#editor\",\"offset_x\":6,\"offset_y\":20}},"
        "{\"type\":\"assert_pixel\",\"target\":{\"selector\":\"#editor\",\"offset_x\":6,\"offset_y\":20},"
        "\"search_radius\":8,\"min_g\":180,\"max_r\":80,\"max_b\":80},"
        "{\"type\":\"click\",\"target\":{\"selector\":\"#inherited\",\"offset_x\":6,\"offset_y\":20}},"
        "{\"type\":\"assert_pixel\",\"target\":{\"selector\":\"#inherited\",\"offset_x\":6,\"offset_y\":20},"
        "\"search_radius\":8,\"min_b\":180,\"max_r\":80,\"max_g\":80},"
        "{\"type\":\"click\",\"target\":{\"selector\":\"#reset\",\"offset_x\":6,\"offset_y\":20}},"
        "{\"type\":\"assert_pixel\",\"target\":{\"selector\":\"#reset\",\"offset_x\":6,\"offset_y\":20},"
        "\"search_radius\":8,\"min_r\":180,\"max_g\":80,\"max_b\":80},"
        "{\"type\":\"click\",\"target\":{\"selector\":\"#rich\",\"offset_x\":6,\"offset_y\":20}},"
        "{\"type\":\"assert_pixel\",\"target\":{\"selector\":\"#rich\",\"offset_x\":6,\"offset_y\":20},"
        "\"search_radius\":12,\"min_g\":180,\"max_r\":80,\"max_b\":80}]}";
    ASSERT_TRUE(run_html_fixture_view(html_path, events_path, html, events));
}

TEST(RenderOutputParity, SelectionBackgroundUsesPseudoStyle) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/selection_color.html";
    const char* events_path = "temp/render_output_parity/selection_color_ui.json";
    const char* html =
        "<!doctype html><style>body{margin:0}#editor{height:48px;"
        "font-size:30px;line-height:40px;color:black}"
        "#editor::selection{background-color:rgb(0,255,0);color:red}"
        "#control{display:block;height:40px;font-size:30px}"
        "#control::selection{background:rgb(0,0,255);color:red}</style>"
        "<div id='editor' contenteditable='true'>MMMMMM</div>"
        "<input id='control' value='MMMMMM'>";
    const char* events =
        "{\"name\":\"selection pseudo background\","
        "\"html\":\"temp/render_output_parity/selection_color.html\","
        "\"viewport\":{\"width\":240,\"height\":110},\"events\":["
        "{\"type\":\"set_editing_selection\","
        "\"target\":{\"selector\":\"#editor\"},\"start\":0,\"end\":6},"
        "{\"type\":\"assert_pixel\","
        "\"target\":{\"selector\":\"#editor\",\"offset_x\":15,"
        "\"offset_y\":20,\"search_radius\":10,"
        "\"min_g\":190,\"max_r\":80,\"max_b\":80},"
        "{\"type\":\"assert_pixel\","
        "\"target\":{\"selector\":\"#editor\",\"offset_x\":19,"
        "\"offset_y\":20,\"search_radius\":12,"
        "\"min_r\":190,\"max_g\":80,\"max_b\":80},"
        "{\"type\":\"set_editing_selection\","
        "\"target\":{\"selector\":\"#control\"},\"start\":0,\"end\":6},"
        "{\"type\":\"assert_pixel\","
        "\"target\":{\"selector\":\"#control\",\"offset_x\":15,"
        "\"offset_y\":20,\"search_radius\":10,"
        "\"min_b\":190,\"max_r\":80,\"max_g\":80},"
        "{\"type\":\"assert_pixel\","
        "\"target\":{\"selector\":\"#control\",\"offset_x\":19,"
        "\"offset_y\":20,\"search_radius\":12,"
        "\"min_r\":190,\"max_g\":80,\"max_b\":80}]}";
    ASSERT_TRUE(run_html_fixture_view(html_path, events_path, html, events));
}

TEST(RenderOutputParity, LogicalOverflowResolvesPhysicalAxesAndCascadeOrder) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/logical_overflow.html";
    const char* png_path = "temp/render_output_parity/logical_overflow.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;width:40px;height:40px;background:green}"
        ".child{position:absolute;width:30px;height:30px;background:red;left:30px;top:5px}"
        "#logical{left:10px;top:10px;overflow-inline:clip;overflow-block:visible}"
        "#shorthand{left:100px;top:10px;overflow-x:hidden;overflow:visible}"
        "#physical{left:190px;top:10px;overflow-x:clip}"
        "#vertical{left:280px;top:10px;writing-mode:vertical-rl;"
        "overflow-inline:clip;overflow-block:visible}"
        "#vertical .child{left:5px;top:30px}</style>"
        "<div id='logical' class='box'><div class='child'></div></div>"
        "<div id='shorthand' class='box'><div class='child'></div></div>"
        "<div id='physical' class='box'><div class='child'></div></div>"
        "<div id='vertical' class='box'><div class='child'></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GT(image.width, 315);
    ASSERT_GT(image.height, 70);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    // The later shorthand releases horizontal overflow; the logical and
    // physical clip declarations retain it in their own boxes.
    EXPECT_GT(pixel(60, 20)[1], 200);
    EXPECT_GT(pixel(150, 20)[0], 200);
    EXPECT_LT(pixel(150, 20)[1], 50);
    EXPECT_GT(pixel(240, 20)[1], 200);
    EXPECT_GT(pixel(290, 45)[0], 200);
    EXPECT_LT(pixel(290, 45)[1], 50);
    EXPECT_GT(pixel(290, 60)[1], 200);
    image_free(image.pixels);
}

TEST(RenderOutputParity, UaMonospaceKeepsRelativeHeadingAndPreSizes) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/ua_monospace.html";
    const char* svg_path = "temp/render_output_parity/ua_monospace.svg";
    const char* html =
        "<!doctype html><style>body{font-family:system-ui}</style>"
        "<pre>PRE</pre><h3><code>HEADING</code></h3>"
        "<div style='font-size:20px'><code>FIXED</code></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "font-size=\"13.00\" fill=\"rgb(0,0,0)\">PRE</text>"));
    EXPECT_TRUE(file_contains_text(svg_path,
        "font-size=\"15.21\" fill=\"rgb(0,0,0)\" font-weight=\"700\">HEADING</text>"));
    EXPECT_TRUE(file_contains_text(svg_path,
        "font-size=\"20.00\" fill=\"rgb(0,0,0)\">FIXED</text>"));
}

TEST(RenderOutputParity, TextUnderlineOffsetMovesRasterLineAndSurvivesSvg) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/underline_offset.html";
    const char* svg_path = "temp/render_output_parity/underline_offset.svg";
    const char* png_path = "temp/render_output_parity/underline_offset.png";
    const char* html =
        "<!doctype html><style>body{margin:0}div{position:absolute;top:0;"
        "font:30px Arial;color:black;text-decoration:underline red}"
        "#near{left:10px;text-underline-offset:0px}"
        "#far{left:210px;text-underline-offset:10px}"
        "#percent,#fixed{top:60px;font-size:20px}"
        "#percent{left:10px}#fixed{left:210px;text-underline-offset:10px}"
        "#percent-parent{text-underline-offset:50%}"
        "</style><div id='near'>MMMM</div><div id='far'>MMMM</div>"
        "<section id='percent-parent'><div id='percent'>MMMM</div></section>"
        "<div id='fixed'>MMMM</div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path, "text-underline-offset: 10.00px"));
    EXPECT_TRUE(file_contains_text(svg_path, "text-underline-offset: 50.00%"));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    int near_y = -1;
    int far_y = -1;
    int percent_y = -1;
    int fixed_y = -1;
    for (int y = 0; y < image.height && y < 115; y++) {
        for (int x = 10; x < image.width && x < 105; x++) {
            const unsigned char* pixel = image.pixels +
                ((size_t)y * image.width + x) * 4;
            if (pixel[3] >= 200 && pixel[0] > 180 &&
                pixel[1] < 90 && pixel[2] < 90) {
                if (y < 50) near_y = y;
                else percent_y = y;
            }
        }
        for (int x = 210; x < image.width && x < 305; x++) {
            const unsigned char* pixel = image.pixels +
                ((size_t)y * image.width + x) * 4;
            if (pixel[3] >= 200 && pixel[0] > 180 &&
                pixel[1] < 90 && pixel[2] < 90) {
                if (y < 50) far_y = y;
                else fixed_y = y;
            }
        }
    }
    image_free(image.pixels);
    ASSERT_GE(near_y, 0);
    ASSERT_GE(far_y, 0);
    EXPECT_NEAR(far_y - near_y, 10, 2);
    ASSERT_GE(percent_y, 0);
    ASSERT_GE(fixed_y, 0);
    EXPECT_NEAR(percent_y - fixed_y, 0, 2);
}

TEST(RenderOutputParity, TextDecorationSkipInkControlsRasterGapsAndSvgStyle) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/skip_ink.html";
    const char* png_path = "temp/render_output_parity/skip_ink.png";
    const char* svg_path = "temp/render_output_parity/skip_ink.svg";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        ".txt{position:absolute;top:10px;font:70px Arial;color:black;"
        "text-decoration:underline red;text-decoration-thickness:5px;"
        "text-underline-offset:-10px}#auto{left:10px}"
        "#none{left:310px;text-decoration-skip-ink:none}"
        "#all{left:610px;text-decoration-skip-ink:all}</style>"
        "<div class='txt' id='auto'>gggg</div>"
        "<div class='txt' id='none'>gggg</div>"
        "<div class='txt' id='all'>gggg</div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path, "text-decoration-skip-ink: none;"));
    EXPECT_TRUE(file_contains_text(svg_path, "text-decoration-skip-ink: all;"));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 800);
    int red[3] = {};
    const int lefts[3] = {10, 310, 610};
    for (int region = 0; region < 3; region++) {
        for (int y = 69; y <= 76 && y < image.height; y++) {
            for (int x = lefts[region]; x < lefts[region] + 160; x++) {
                const unsigned char* pixel = image.pixels +
                    ((size_t)y * image.width + x) * 4;
                if (pixel[3] >= 200 && pixel[0] > 180 &&
                    pixel[1] < 90 && pixel[2] < 90) red[region]++;
            }
        }
    }
    image_free(image.pixels);
    EXPECT_GT(red[1], red[0] + 300);
    EXPECT_NEAR(red[0], red[2], 30);
}

TEST(RenderOutputParity, TextUnderlinePositionUnderMovesStrokeAndInherits) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/underline_position.html";
    const char* svg_path = "temp/render_output_parity/underline_position.svg";
    const char* png_path = "temp/render_output_parity/underline_position.png";
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        ".txt{position:absolute;top:10px;font:70px Arial;color:black;"
        "text-decoration:underline red;text-decoration-thickness:5px;"
        "text-decoration-skip-ink:none}#auto{left:10px}"
        "#under{left:210px;text-underline-position:under}"
        "#inherited{text-underline-position:under}#child{left:410px}"
        "#font{left:610px;text-underline-position:from-font}</style>"
        "<div class='txt' id='auto'>gggg</div>"
        "<div class='txt' id='under'>gggg</div>"
        "<section id='inherited'><div class='txt' id='child'>gggg</div></section>"
        "<div class='txt' id='font'>gggg</div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path, "text-underline-position: under;"));
    EXPECT_TRUE(file_contains_text(svg_path, "text-underline-position: from-font;"));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 800);
    int last_red_y[4] = {-1, -1, -1, -1};
    const int lefts[4] = {10, 210, 410, 610};
    for (int region = 0; region < 4; region++) {
        for (int y = 0; y < image.height && y < 120; y++) {
            for (int x = lefts[region]; x < lefts[region] + 150; x++) {
                const unsigned char* pixel = image.pixels +
                    ((size_t)y * image.width + x) * 4;
                if (pixel[3] >= 200 && pixel[0] > 180 &&
                    pixel[1] < 90 && pixel[2] < 90) last_red_y[region] = y;
            }
        }
    }
    image_free(image.pixels);
    ASSERT_GE(last_red_y[0], 0);
    EXPECT_GE(last_red_y[1] - last_red_y[0], 3);
    EXPECT_NEAR(last_red_y[1], last_red_y[2], 1);
    EXPECT_NEAR(last_red_y[0], last_red_y[3], 1);
}

TEST(RenderOutputParity, MediaRangeAndOrSelectVisibleRules) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/media_range.html";
    const char* png_path = "temp/render_output_parity/media_range.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:0;width:10px;height:10px;background:red}"
        "#range{left:0}#false{left:20px}#or{left:40px}#unknown{left:60px}"
        "#ratio{left:80px}#resolution{left:100px}"
        "@media (width >= 100px){#range{background:green}}"
        "@media (width < 10px){#false{background:green}}"
        "@media (orientation: portrait) or (width >= 100px)"
        "{#or{background:green}}"
        "@media (unknown-feature){#unknown{background:green}}"
        "@media (aspect-ratio >= 1/1){#ratio{background:green}}"
        "@media (resolution >= 1dppx){#resolution{background:green}}"
        "</style><div class='box' id='range'></div>"
        "<div class='box' id='false'></div>"
        "<div class='box' id='or'></div>"
        "<div class='box' id='unknown'></div>"
        "<div class='box' id='ratio'></div>"
        "<div class='box' id='resolution'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 110);
    const int expected_green[6] = {1, 0, 1, 0, 1, 1};
    for (int region = 0; region < 6; region++) {
        int x = region * 20 + 5;
        const unsigned char* pixel = image.pixels +
            ((size_t)5 * image.width + x) * 4;
        if (expected_green[region]) {
            EXPECT_GT(pixel[1], pixel[0]) << region;
        } else {
            EXPECT_GT(pixel[0], pixel[1]) << region;
        }
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, PdfPrintMediaSelectsRulesAndLinkedStylesheet) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    if (!command_exists("sips") && !command_exists("pdftoppm")) {
        GTEST_SKIP() << "a PDF rasterizer is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/print_media.html";
    const char* css_path = "temp/render_output_parity/print_media.css";
    const char* pdf_path = "temp/render_output_parity/print_media.pdf";
    const char* png_path = "temp/render_output_parity/print_media_pdf.png";
    const char* css = "#linked{background:green}";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:0;width:40px;height:40px;background:red}"
        "#inline{left:0}#linked{left:50px}#screen{left:100px}"
        "@media print{#inline{background:green}}"
        "@media screen{#screen{background:blue}}</style>"
        "<link rel='stylesheet' media='print' href='print_media.css'>"
        "<div class='box' id='inline'></div>"
        "<div class='box' id='linked'></div>"
        "<div class='box' id='screen'></div>";
    ASSERT_TRUE(write_file_all(css_path, css, strlen(css)));
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html));

    char qpdf[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 128];
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    shell_quote(png_path, qpng, sizeof(qpng));
    if (command_exists("sips")) {
        snprintf(cmd, sizeof(cmd), "sips -s format png %s --out %s >/dev/null 2>&1",
                 qpdf, qpng);
    } else {
        const char* prefix = "temp/render_output_parity/print_media_pdf";
        char qprefix[PATH_MAX + 8];
        shell_quote(prefix, qprefix, sizeof(qprefix));
        snprintf(cmd, sizeof(cmd),
                 "pdftoppm -png -f 1 -l 1 -singlefile -r 72 %s %s >/dev/null 2>&1",
                 qpdf, qprefix);
    }
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 140);
    ASSERT_GE(image.height, 40);
    const int centers[3] = {20, 70, 120};
    for (int region = 0; region < 3; region++) {
        const unsigned char* pixel = image.pixels +
            ((size_t)20 * image.width + centers[region]) * 4;
        if (region < 2) EXPECT_GT(pixel[1], pixel[0]) << region;
        else EXPECT_GT(pixel[0], pixel[2]) << region;
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, NestedSelectorsPaintAcrossDepthAndPreserveSpecificity) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/nested_selectors.html";
    const char* png_path = "temp/render_output_parity/nested_selectors.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:0;width:20px;height:20px;background:red}"
        "#desc{left:0}#self{left:30px}#reverse{left:60px}"
        "#deep{left:90px}#post{left:120px}#specific{left:150px}"
        "#order{left:180px}#media{left:210px}"
        "#media-child{left:240px}#supports{left:270px}"
        ".host{.target{background:green}}"
        ".self{&.active{background:green}}"
        ".child{.ancestor &{background:green}}"
        ".deep{.middle{.inner{background:green}}}"
        ".post{background:blue;.unused{color:red}background:green}"
        ".order{.unused{color:red}background:blue;&{background:green}}"
        ".media{@media screen{background:green}}"
        ".media-parent{@media screen{.target{background:green}}}"
        ".supports{@supports (display:grid){background:green}}"
        "#unmatched,.host2{.target{background:green}}"
        ".host2 .target{background:red}</style>"
        "<div class=host><div class='box target' id=desc></div></div>"
        "<div class='box self active' id=self></div>"
        "<div class=ancestor><div class='box child' id=reverse></div></div>"
        "<div class=deep><div class=middle>"
        "<div class='box inner' id=deep></div></div></div>"
        "<div class='box post' id=post></div>"
        "<div class=host2><div class='box target' id=specific></div></div>"
        "<div class='box order' id=order></div>"
        "<div class='box media' id=media></div>"
        "<div class=media-parent><div class='box target' id=media-child>"
        "</div></div><div class='box supports' id=supports></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 290);
    for (int region = 0; region < 10; region++) {
        const unsigned char* pixel = image.pixels +
            ((size_t)10 * image.width + region * 30 + 10) * 4;
        EXPECT_GT(pixel[1], pixel[0]) << region;
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, BorderImageWidthMultiplierAndOutsetPaintUsedAreas) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/border_image_areas.html";
    const char* png_path = "temp/render_output_parity/border_image_areas.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:20px;width:40px;height:40px;"
        "border:10px solid transparent;"
        "border-image-source:linear-gradient(red,red);"
        "border-image-slice:1}"
        "#multiple{left:20px;border-image-width:2}"
        "#normal{left:100px;border-image-width:1}"
        "#outset{left:180px;border-image-width:5px;"
        "border-image-outset:5px}</style>"
        "<div class=box id=multiple></div>"
        "<div class=box id=normal></div>"
        "<div class=box id=outset></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 240);
    const unsigned char* multiple = image.pixels +
        ((size_t)50 * image.width + 35) * 4;
    const unsigned char* normal_inner = image.pixels +
        ((size_t)50 * image.width + 115) * 4;
    const unsigned char* outside = image.pixels +
        ((size_t)50 * image.width + 177) * 4;
    EXPECT_GT(multiple[0], multiple[1] + 100);
    EXPECT_GT(normal_inner[1], 200);
    EXPECT_GT(outside[0], outside[1] + 100);
    image_free(image.pixels);
}

TEST(RenderOutputParity, BorderImageGradientTilesRepeatRoundAndSpace) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/border_image_repeat.html";
    const char* png_path = "temp/render_output_parity/border_image_repeat.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:20px;width:80px;height:80px;"
        "border:10px solid transparent;border-image-slice:20;"
        "border-image-source:linear-gradient(to right,red 50%,blue 50%)}"
        "#stretch{left:20px;border-image-repeat:stretch}"
        "#repeat{left:140px;border-image-repeat:repeat}"
        "#round{left:260px;border-image-repeat:round}"
        "#space{left:380px;border-image-repeat:space}"
        "#vertical{left:500px;border-image-repeat:stretch repeat;"
        "border-image-source:linear-gradient(to bottom,red 50%,blue 50%)}"
        "</style><div class=box id=stretch></div>"
        "<div class=box id=repeat></div><div class=box id=round></div>"
        "<div class=box id=space></div><div class=box id=vertical></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 600);
    ASSERT_GE(image.height, 120);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_GT(pixel(40, 25)[0], pixel(40, 25)[2]);
    EXPECT_GT(pixel(80, 25)[2], pixel(80, 25)[0]);
    const int red_samples[] = {156, 181, 207, 275, 300, 325};
    const int blue_samples[] = {168, 194, 220, 285, 312, 337};
    for (int x : red_samples)
        EXPECT_GT(pixel(x, 25)[0], pixel(x, 25)[2]) << x;
    for (int x : blue_samples)
        EXPECT_GT(pixel(x, 25)[2], pixel(x, 25)[0]) << x;
    EXPECT_GT(pixel(392, 25)[1], 200);
    EXPECT_GT(pixel(400, 25)[0], pixel(400, 25)[2]);
    EXPECT_GT(pixel(415, 25)[2], pixel(415, 25)[0]);
    EXPECT_GT(pixel(505, 36)[0], pixel(505, 36)[2]);
    EXPECT_GT(pixel(505, 48)[2], pixel(505, 48)[0]);
    EXPECT_GT(pixel(505, 61)[0], pixel(505, 61)[2]);
    image_free(image.pixels);
}

TEST(RenderOutputParity, BorderImageUrlSlicesRasterSourceAndFill) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/border_image_source.png";
    const unsigned char source[] = {
        255,0,0,255, 0,255,0,255, 0,0,255,255,
        255,255,0,255, 255,0,255,255, 0,255,255,255,
        0,0,0,255, 255,255,255,255, 128,128,128,255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source, 3, 3));
    const char* html_path = "temp/render_output_parity/border_image_url.html";
    const char* png_path = "temp/render_output_parity/border_image_url.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:20px;width:80px;height:80px;"
        "border:10px solid transparent;"
        "border-image-source:url(border_image_source.png);"
        "border-image-slice:1;border-image-repeat:stretch}"
        "#plain{left:20px}#filled{left:150px;border-image-slice:1 fill}"
        "#auto{left:280px;border-image-width:auto}"
        "#zero{left:410px;border-width:0;border-image-slice:1 fill}"
        "</style><div class=box id=plain></div>"
        "<div class=box id=filled></div><div class=box id=auto></div>"
        "<div class=box id=zero></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 490);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_GT(pixel(25, 25)[0], 240);
    EXPECT_LT(pixel(25, 25)[1], 20);
    EXPECT_GT(pixel(70, 25)[1], 240);
    EXPECT_GT(pixel(115, 25)[2], 240);
    EXPECT_LT(pixel(25, 115)[0], 20);
    EXPECT_GT(pixel(115, 115)[0], 110);
    EXPECT_GT(pixel(70, 70)[1], 240);
    EXPECT_GT(pixel(200, 70)[0], 240);
    EXPECT_LT(pixel(200, 70)[1], 20);
    EXPECT_GT(pixel(280, 70)[0], 240);
    EXPECT_GT(pixel(285, 70)[1], 240);
    EXPECT_GT(pixel(450, 70)[0], 240);
    EXPECT_LT(pixel(450, 70)[1], 20);
    image_free(image.pixels);
}

TEST(RenderOutputParity, BorderImageShorthandCompetesWithLonghandsInCascade) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/border_image_cascade.html";
    const char* png_path = "temp/render_output_parity/border_image_cascade.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:20px;width:40px;height:40px;"
        "border:10px solid transparent}"
        "#short{left:20px;border-image:linear-gradient(red,red) 1 / 10px}"
        "#late{left:100px;border-image:linear-gradient(red,red) 1 / 10px;"
        "border-image-width:20px}"
        "#early{left:180px;border-image-width:20px;"
        "border-image:linear-gradient(red,red) 1 / 10px}"
        "#reset{left:260px;border-image:linear-gradient(red,red) 1 / 10px;"
        "border-image:none}"
        "#source{left:340px;border-image:linear-gradient(red,red) 1 / 10px;"
        "border-image-source:linear-gradient(blue,blue)}"
        "</style><div class=box id=short></div>"
        "<div class=box id=late></div><div class=box id=early></div>"
        "<div class=box id=reset></div><div class=box id=source></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 400);
    auto pixel = [&](int x) {
        return image.pixels + ((size_t)50 * image.width + x) * 4;
    };
    EXPECT_GT(pixel(25)[0], pixel(25)[2] + 100);
    EXPECT_GT(pixel(115)[0], pixel(115)[2] + 100);
    EXPECT_GT(pixel(195)[1], 200);
    EXPECT_GT(pixel(265)[1], 200);
    EXPECT_GT(pixel(345)[2], pixel(345)[0] + 100);
    image_free(image.pixels);
}

TEST(RenderOutputParity, NestedSelectorsRestyleAfterParentClassMutation) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/nested_restyle.html";
    const char* svg_path = "temp/render_output_parity/nested_restyle.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#child,#status{display:block;width:80px;height:10px;background:red}"
        "#host{&.active{.target{background:green}}}"
        "</style><div id=host><div id=child class=target></div></div>"
        "<div id=status></div><script>"
        "let host=document.querySelector('#host');"
        "host.className='active';"
        "if(!host.matches('&.active')||"
        "host.querySelector('& > .target')!==document.querySelector('#child')||"
        "host.closest('&.active')!==host)"
        "document.querySelector('#status').style.background='magenta';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_FALSE(file_contains_text(svg_path,
        "width=\"80.00\" height=\"10.00\" fill=\"rgb(255,0,255)\""));
}

TEST(RenderOutputParity, NestedCssomSerializesAndRebindsAfterSelectorMutation) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/nested_cssom.html";
    const char* png_path = "temp/render_output_parity/nested_cssom.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".child,.status{display:block;width:20px;height:20px;background:red}"
        ".status{position:absolute;left:30px;top:0;background:green}"
        ".a{color:red;.child{background:green}background:blue;"
        "@media screen{color:purple}}"
        "</style><div class=b><div class=child></div></div>"
        "<div class=status id=status></div><script>"
        "let rule=document.styleSheets[0].cssRules[3];"
        "let text=rule.cssText;"
        "if(text.indexOf('.a {')<0||"
        "text.indexOf('& .child { background: green; }')<0||"
        "text.indexOf('background: blue;')<0||"
        "text.indexOf('@media screen {')<0||"
        "rule.cssRules[0].selectorText!=='& .child'||"
        "rule.cssRules[1].cssText!=='background: blue;')"
        "document.querySelector('#status').style.background='magenta';"
        "rule.cssRules[1].style.background='cyan';"
        "if(rule.cssRules[1].cssText!=='background: cyan;'||"
        "rule.cssText.indexOf('background: cyan;')<0)"
        "document.querySelector('#status').style.background='magenta';"
        "rule.selectorText='.b';"
        "</script>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 50);
    const unsigned char* target = image.pixels +
        ((size_t)10 * image.width + 10) * 4;
    const unsigned char* status = image.pixels +
        ((size_t)10 * image.width + 40) * 4;
    EXPECT_GT(target[1], target[0]);
    EXPECT_GT(status[1], status[0]);
    image_free(image.pixels);
}

TEST(RenderOutputParity, FontAndViewportRelativeUnitsResolveUsedWidths) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/relative_units.html";
    const char* svg_path = "temp/render_output_parity/relative_units.svg";
    const char* png_path = "temp/render_output_parity/relative_units.png";
    const char* html =
        "<!doctype html><style>html{font-size:20px;line-height:30px}"
        "body{margin:0}div{height:10px;background:red}"
        "#a{width:5vi}#b{width:5vb}#c{width:5svmin}"
        "#d{width:5lvmax}#e{width:2cap}#f{width:2ic}"
        "#g{width:2rlh}#h{writing-mode:vertical-rl;width:5vi}"
        "#i{writing-mode:vertical-rl;width:5vb}"
        "#j{width:5dvi}#k{width:5dvb}"
        "#l{font-size:5dvi;width:1em}#m{font-size:5dvb;width:1em}"
        "</style><div id=a></div><div id=b></div><div id=c></div>"
        "<div id=d></div><div id=e></div><div id=f></div>"
        "<div id=g></div><div id=h></div><div id=i></div>"
        "<div id=j></div><div id=k></div>"
        "<div id=l></div><div id=m></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"0.00\" width=\"60.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"10.00\" width=\"40.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"20.00\" width=\"40.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"30.00\" width=\"60.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"60.00\" width=\"60.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"70.00\" width=\"40.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"80.00\" width=\"60.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"90.00\" width=\"60.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"100.00\" width=\"40.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"110.00\" width=\"60.00\" height=\"10.00\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"120.00\" width=\"40.00\" height=\"10.00\""));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GT(image.width, 60);
    ASSERT_GT(image.height, 100);
    // Font metrics vary by host; both units still produce more than a raw 2px.
    for (size_t y : {45u, 55u}) {
        const unsigned char* pixel = image.pixels +
            (y * image.width + 15u) * 4;
        EXPECT_GT(pixel[0], pixel[1]);
    }
    image_free(image.pixels);
}

TEST(RenderOutputParity, MathFunctionsAndInvalidCalcResolveUsedWidths) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/math_functions.html";
    const char* svg_path = "temp/render_output_parity/math_functions.svg";
    const char* html =
        "<!doctype html><style>body{margin:0}"
        "div{height:10px;background:red;width:20px}"
        "#a{width:calc(100px * sin(30deg))}"
        "#b{width:calc(100px + foo)}"
        "#c{width:calc(100px + bogus(1))}"
        "#d{width:round(43px,10px)}"
        "#e{width:mod(43px,10px)}"
        "#f{width:abs(-11px)}"
        "#g{width:hypot(3px,4px)}"
        "#h{width:calc(10px * pi)}"
        "#i{width:calc(10px * sqrt(4))}"
        "#j{width:round(up,13px,10px)}"
        "#k{width:round(down,13px,10px)}"
        "#l{width:mod(-13px,10px)}"
        "#m{width:calc((20px + 10px) * 2)}"
        "#n{width:min(10px + 5px,20px)}"
        "</style><div id=a></div><div id=b></div><div id=c></div>"
        "<div id=d></div><div id=e></div><div id=f></div>"
        "<div id=g></div><div id=h></div><div id=i></div>"
        "<div id=j></div><div id=k></div><div id=l></div>"
        "<div id=m></div><div id=n></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    const char* expected[] = {
        "y=\"0.00\" width=\"50.00\" height=\"10.00\"",
        "y=\"10.00\" width=\"20.00\" height=\"10.00\"",
        "y=\"20.00\" width=\"20.00\" height=\"10.00\"",
        "y=\"30.00\" width=\"40.00\" height=\"10.00\"",
        "y=\"40.00\" width=\"3.00\" height=\"10.00\"",
        "y=\"50.00\" width=\"11.00\" height=\"10.00\"",
        "y=\"60.00\" width=\"5.00\" height=\"10.00\"",
        "y=\"70.00\" width=\"31.42\" height=\"10.00\"",
        "y=\"80.00\" width=\"20.00\" height=\"10.00\"",
        "y=\"90.00\" width=\"20.00\" height=\"10.00\"",
        "y=\"100.00\" width=\"10.00\" height=\"10.00\"",
        "y=\"110.00\" width=\"7.00\" height=\"10.00\"",
        "y=\"120.00\" width=\"60.00\" height=\"10.00\"",
        "y=\"130.00\" width=\"15.00\" height=\"10.00\"",
    };
    for (const char* row : expected) EXPECT_TRUE(file_contains_text(svg_path, row)) << row;
}

TEST(RenderOutputParity, FixedBackgroundAttachmentUsesViewportPosition) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/attachment_source.png";
    const char* html_path = "temp/render_output_parity/attachment.html";
    const char* png_path = "temp/render_output_parity/attachment.png";
    const unsigned char source_pixels[] = {
        255, 0, 0, 255,   0, 0, 255, 255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source_pixels, 2, 1));
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;width:30px;height:15px;"
        "background-image:url(attachment_source.png);"
        "background-size:100px 40px;background-repeat:no-repeat}"
        "#fixed{left:50px;top:0;background-attachment:fixed}"
        "#scroll{left:110px;top:0;background-attachment:scroll}"
        "#parent{background-attachment:fixed}"
        "#inherited{left:50px;top:20px;background-attachment:inherit}"
        "#shorthand{left:50px;top:36px;"
        "background:url(attachment_source.png) fixed no-repeat;"
        "background-size:100px 40px}"
        "</style><div class='box' id='fixed'></div>"
        "<div class='box' id='scroll'></div>"
        "<section id='parent'><div class='box' id='inherited'></div></section>"
        "<div class='box' id='shorthand'></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 140);
    ASSERT_GE(image.height, 50);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_GT(pixel(55, 5)[2], pixel(55, 5)[0]);
    EXPECT_GT(pixel(115, 5)[0], pixel(115, 5)[2]);
    EXPECT_GT(pixel(55, 25)[2], pixel(55, 25)[0]);
    EXPECT_GT(pixel(55, 38)[2], pixel(55, 38)[0]);
    image_free(image.pixels);
}

TEST(RenderOutputParity, LocalBackgroundAttachmentSizesToScrollableContent) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/local_attachment_source.png";
    const char* html_path = "temp/render_output_parity/local_attachment.html";
    const char* png_path = "temp/render_output_parity/local_attachment.png";
    const unsigned char source_pixels[] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        0, 0, 255, 255, 0, 0, 255, 255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source_pixels, 1, 4));
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        ".box{position:absolute;top:0;width:20px;height:20px;overflow:auto;"
        "background-image:url(local_attachment_source.png);"
        "background-size:100% 100%;background-repeat:no-repeat}"
        ".content{height:60px}#scroll{left:0;background-attachment:scroll}"
        "#local{left:40px;background-attachment:local}"
        "</style><div class='box' id='scroll'><div class='content'></div></div>"
        "<div class='box' id='local'><div class='content'></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GE(image.width, 60);
    ASSERT_GE(image.height, 20);
    auto pixel = [&](int x, int y) {
        return image.pixels + ((size_t)y * image.width + x) * 4;
    };
    EXPECT_GT(pixel(5, 15)[2], pixel(5, 15)[0]);
    EXPECT_GT(pixel(45, 15)[0], pixel(45, 15)[2]);
    image_free(image.pixels);
}

TEST(RenderOutputParity, LocalBackgroundAttachmentMovesWithScrollContent) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* source_path = "temp/render_output_parity/local_scroll_source.png";
    const char* html_path = "temp/render_output_parity/local_scroll.html";
    const char* events_path = "temp/render_output_parity/local_scroll_ui.json";
    const unsigned char source_pixels[] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        0, 0, 255, 255, 0, 0, 255, 255
    };
    ASSERT_TRUE(write_png_rgba(source_path, source_pixels, 1, 4));
    const char* html =
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#local{width:30px;height:20px;overflow:auto;"
        "background-image:url(local_scroll_source.png);"
        "background-size:100% 100%;background-repeat:no-repeat;"
        "background-attachment:local}#content{height:60px}"
        "</style><div id='local'><div id='content'></div></div>";
    const char* events =
        "{\"name\":\"local background scrolls with content\","
        "\"html\":\"temp/render_output_parity/local_scroll.html\","
        "\"viewport\":{\"width\":100,\"height\":70},\"events\":["
        "{\"type\":\"assert_pixel\",\"target\":{\"selector\":\"#local\","
        "\"offset_x\":5,\"offset_y\":15},\"search_radius\":1,"
        "\"min_r\":180,\"max_b\":80},"
        "{\"type\":\"scroll\",\"target\":{\"selector\":\"#local\","
        "\"offset_x\":5,\"offset_y\":5},\"dx\":0,\"dy\":-80},"
        "{\"type\":\"assert_pixel\",\"target\":{\"selector\":\"#local\","
        "\"offset_x\":5,\"offset_y\":15},\"search_radius\":1,"
        "\"min_b\":180,\"max_r\":80}]}";
    ASSERT_TRUE(run_html_fixture_view(html_path, events_path, html, events));
}

TEST(RenderOutputParity, SvgExportColumnRuleNamedAndFunctionalColors) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/column_rule_colors.html";
    const char* svg_path = "temp/render_output_parity/column_rule_colors.svg";
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        ".cols{width:120px;height:80px;column-count:2;column-gap:20px;"
        "column-rule:4px solid red}</style>"
        "<div class=cols style='column-rule-color:lime'>one two three four five</div>"
        "<div class=cols style='column-rule-color:rgb(0,128,0)'>one two three four five</div>"
        "<div class=cols style='color:purple;column-rule-color:initial'>one two three four five</div>"
        "<div class=cols style='color:teal;column-rule:4px solid'>one two three four five</div>"
        "<div style='column-rule-color:rgb(4,5,6)'>"
        "<div class=cols style='column-rule-color:inherit'>one two three four five</div></div>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    char qhtml[PATH_MAX + 8];
    char qsvg[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(svg_path, qsvg, sizeof(qsvg));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s -o %s > temp/render_output_parity/column_rule_colors.out 2> temp/render_output_parity/column_rule_colors.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qsvg);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    EXPECT_TRUE(file_contains_text(svg_path, "stroke=\"rgb(0,255,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path, "stroke=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path, "stroke=\"rgb(128,0,128)\""));
    EXPECT_TRUE(file_contains_text(svg_path, "stroke=\"rgb(0,128,128)\""));
    EXPECT_TRUE(file_contains_text(svg_path, "stroke=\"rgb(4,5,6)\""));
}

TEST(RenderOutputParity, LayoutCustomPropertyOwnerCycleAndInvalidFallback) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));

    const char* html_path = "temp/render_output_parity/custom_property_owner.html";
    const char* view_path = "temp/render_output_parity/custom_property_owner.json";
    const char* html =
        "<!doctype html><html><body>"
        "<div style=\"--base:40px;--direct:var(--base);"
        "--calculated:calc(var(--base) + 10px)\">"
        "<div style=\"--base:90px;width:var(--direct);height:20px\"></div>"
        "<div style=\"--base:90px;width:var(--calculated);height:20px\"></div>"
        "</div>"
        "<div style=\"--a:var(--b);--b:var(--a);width:var(--a,75px);height:20px\"></div>"
        "<div style=\"--wrong:red;width:var(--wrong);height:20px\"></div>"
        "<div style=\"width:110px\"><div style=\"--wide:inherit;width:var(--wide);height:20px\"></div></div>"
        "</body></html>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));

    char qhtml[PATH_MAX + 8];
    char qview[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(view_path, qview, sizeof(qview));
    snprintf(cmd, sizeof(cmd),
             "%s layout %s%s --view-output %s > temp/render_output_parity/custom_property_owner.out 2> temp/render_output_parity/custom_property_owner.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qview);

    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    ASSERT_TRUE(file_exists(view_path));
    EXPECT_TRUE(file_contains_text(view_path, "\"width\": 40.0"));
    EXPECT_TRUE(file_contains_text(view_path, "\"width\": 50.0"));
    EXPECT_TRUE(file_contains_text(view_path, "\"width\": 75.0"));
    EXPECT_TRUE(file_contains_text(view_path, "\"width\": 110.0"));
    EXPECT_FALSE(file_contains_text(view_path, "\"width\": 0.0"));
}

TEST(RenderOutputParity, CustomPropertyTokensExpandInsideSpacingShorthands) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/var_spacing_tokens.html";
    const char* svg_path = "temp/render_output_parity/var_spacing_tokens.svg";
    const char* html =
        "<!doctype html><style>body{margin:0}"
        ".box{position:absolute;left:0;top:0;width:20px;height:10px;background:red}"
        "#a{--gap:4px 8px;margin:var(--gap) 30px}"
        "#b{--pad:5px 6px;top:30px;padding:var(--pad) 3px;background:blue}"
        "#c{--gap:4px 8px 12px 16px;top:60px;margin:var(--gap) 20px;"
        "background:green}"
        "#d{margin:0 11px;margin:bad(2px);top:90px;background:purple}"
        "</style><div class=box id=a></div><div class=box id=b></div>"
        "<div class=box id=c></div><div class=box id=d></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"8.00\" y=\"4.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"30.00\" width=\"32.00\" height=\"18.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"60.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"11.00\" y=\"90.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(128,0,128)\""));
}

TEST(RenderOutputParity, CustomPropertyTokensExpandInsideRgbFunction) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/var_rgb_tokens.html";
    const char* svg_path = "temp/render_output_parity/var_rgb_tokens.svg";
    const char* html =
        "<!doctype html><style>body{margin:0}"
        "div{width:20px;height:10px}"
        "#a{--rgb:255 0 0;background:rgb(var(--rgb))}"
        "#b{--red:0;background:rgb(var(--red) 0 255)}"
        "</style><div id=a></div><div id=b></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"0.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"10.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, UnitlessMarginRequiresQuirksMode) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* inline_body =
        "<body style='margin:0'><div style='position:absolute;left:0;top:0;"
        "width:20px;height:10px;background:red;margin:0 10px;"
        "margin:0 25'></div></body>";
    const char* sheet_body =
        "<style>body{margin:0}div{position:absolute;left:0;top:0;"
        "width:20px;height:10px;background:red;margin:0 10px;"
        "margin:0 25}</style><div></div>";
    struct Case {
        const char* name;
        const char* doctype;
        const char* body;
        const char* expected_x;
    } cases[] = {
        {"standard_inline", "<!doctype html>", inline_body, "10.00"},
        {"quirks_inline", "<!quirks-mode>", inline_body, "25.00"},
        {"standard_sheet", "<!doctype html>", sheet_body, "10.00"},
        {"quirks_sheet", "<!quirks-mode>", sheet_body, "25.00"},
    };
    for (const Case& fixture : cases) {
        char html[1024];
        char html_path[PATH_MAX];
        char svg_path[PATH_MAX];
        char expected[180];
        snprintf(html, sizeof(html), "%s%s", fixture.doctype, fixture.body);
        snprintf(html_path, sizeof(html_path),
            "temp/render_output_parity/%s_unitless_margin.html", fixture.name);
        snprintf(svg_path, sizeof(svg_path),
            "temp/render_output_parity/%s_unitless_margin.svg", fixture.name);
        snprintf(expected, sizeof(expected),
            "x=\"%s\" y=\"0.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\"",
            fixture.expected_x);
        ASSERT_TRUE(render_html_fixture(html_path, svg_path, html)) << fixture.name;
        EXPECT_TRUE(file_contains_text(svg_path, expected)) << fixture.name;
    }
}

TEST(RenderOutputParity, DisplayVariableControlsBoxGeneration) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/display_var.html";
    const char* svg_path = "temp/render_output_parity/display_var.svg";
    const char* html =
        "<!doctype html><style>body{margin:0}div{width:20px;height:10px}"
        "#hidden{--mode:none;display:var(--mode);background:red}"
        "#fallback{display:var(--missing,none);background:red}"
        "#cycle{--a:var(--b);--b:var(--a);display:var(--a,none);"
        "background:red}"
        "#contents{--mode:contents;display:var(--mode);background:red}"
        "#child{background:blue}"
        "</style><div id=hidden></div><div id=fallback></div>"
        "<div id=cycle></div><div id=contents><div id=child></div>"
        "</div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_FALSE(file_contains_text(svg_path, "fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "width=\"20.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, DisplayVariableExpandsMultipleKeywords) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/display_var_list.html";
    const char* svg_path = "temp/render_output_parity/display_var_list.svg";
    const char* html =
        "<!doctype html><style>body{margin:0}"
        "#flex{--outer:block;--inner:flex;display:var(--outer) var(--inner);"
        "width:40px}#flex>div{width:10px;height:10px}"
        "#a{background:red}#b{background:blue}"
        "</style><div id=flex><div id=a></div><div id=b></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"0.00\" y=\"0.00\" width=\"10.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"10.00\" y=\"0.00\" width=\"10.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, GridRepeatExpandsLiteralAndVariableTrackLists) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    struct Case {
        const char* name;
        const char* columns;
    } cases[] = {
        {"literal", "repeat(2,10px 20px)"},
        {"variable", "repeat(2,var(--tracks))"},
        {"auto_fill_variable", "repeat(auto-fill,var(--tracks))"},
    };
    for (const Case& fixture : cases) {
        char html[1024];
        char html_path[PATH_MAX];
        char svg_path[PATH_MAX];
        snprintf(html, sizeof(html),
            "<!doctype html><style>body{margin:0}#g{display:grid;"
            "--tracks:10px 20px;grid-template-columns:%s;width:100px}"
            "#g>div{height:10px}#a{background:red}#b{background:blue}"
            "#c{background:green}#d{background:yellow}</style>"
            "<div id=g><div id=a></div><div id=b></div><div id=c></div>"
            "<div id=d></div></div>", fixture.columns);
        snprintf(html_path, sizeof(html_path),
            "temp/render_output_parity/grid_repeat_%s.html", fixture.name);
        snprintf(svg_path, sizeof(svg_path),
            "temp/render_output_parity/grid_repeat_%s.svg", fixture.name);
        ASSERT_TRUE(render_html_fixture(html_path, svg_path, html)) << fixture.name;
        EXPECT_TRUE(file_contains_text(svg_path,
            "x=\"0.00\" y=\"0.00\" width=\"10.00\" height=\"10.00\" fill=\"rgb(255,0,0)\"")) << fixture.name;
        EXPECT_TRUE(file_contains_text(svg_path,
            "x=\"10.00\" y=\"0.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,0,255)\"")) << fixture.name;
        EXPECT_TRUE(file_contains_text(svg_path,
            "x=\"30.00\" y=\"0.00\" width=\"10.00\" height=\"10.00\" fill=\"rgb(0,128,0)\"")) << fixture.name;
        EXPECT_TRUE(file_contains_text(svg_path,
            "x=\"40.00\" y=\"0.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,255,0)\"")) << fixture.name;
    }
}

TEST(RenderOutputParity, LayoutTabSizeLengthAndFractionalNumber) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/tab_size_values.html";
    const char* view_path = "temp/render_output_parity/tab_size_values.json";
    const char* html =
        "<!doctype html><style>html,body,pre{margin:0}"
        "pre{font:16px/20px monospace;white-space:pre}</style>"
        "<pre style='tab-size:24px'>a\tb</pre>"
        "<pre style='tab-size:2.5'>a\tb</pre>"
        "<pre style='tab-size:8'>a\tb</pre>"
        "<div style='font-size:20px;tab-size:2em'>"
        "<pre style='font-size:10px'>a\tb</pre></div>"
        "<pre style='font-size:10px;tab-size:40px'>a\tb</pre>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    char qhtml[PATH_MAX + 8];
    char qview[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(view_path, qview, sizeof(qview));
    snprintf(cmd, sizeof(cmd),
             "%s layout %s%s --view-output %s > temp/render_output_parity/tab_size_values.out 2> temp/render_output_parity/tab_size_values.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, qview);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    FILE* fp = fopen(view_path, "rb");
    ASSERT_NE(fp, nullptr);
    ASSERT_EQ(fseek(fp, 0, SEEK_END), 0);
    long bytes = ftell(fp);
    ASSERT_GT(bytes, 0);
    ASSERT_EQ(fseek(fp, 0, SEEK_SET), 0);
    char* json = (char*)malloc((size_t)bytes + 1);
    ASSERT_NE(json, nullptr);
    size_t read = fread(json, 1, (size_t)bytes, fp);
    fclose(fp);
    ASSERT_EQ(read, (size_t)bytes);
    json[bytes] = '\0';

    float widths[5] = {};
    const char* cursor = json;
    for (float& width : widths) {
        cursor = strstr(cursor, "\"content\": \"a\\tb\"");
        ASSERT_NE(cursor, nullptr);
        cursor = strstr(cursor, "\"width\": ");
        ASSERT_NE(cursor, nullptr);
        cursor += strlen("\"width\": ");
        width = (float)strtod(cursor, nullptr);
    }
    free(json);
    // Both short tab stops precede the eight-space tab stop, and a fractional
    // count must not be truncated to an integer before layout.
    EXPECT_LT(widths[0], widths[2]);
    EXPECT_LT(widths[1], widths[2]);
    EXPECT_NEAR(widths[0], widths[1], 5.0f);
    EXPECT_NEAR(widths[3], widths[4], 1.0f);
}

TEST(RenderOutputParity, TextAlignAllAndLastRespectShorthandCascade) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/text_align_all.html";
    const char* view_path = "temp/render_output_parity/text_align_all.json";
    const char* html =
        "<!doctype html><style>html,body,p,div{margin:0;padding:0}"
        "p{width:200px;font:20px/25px monospace;white-space:pre}</style>"
        "<p style='text-align-all:right'>A</p>"
        "<p style='text-align-all:center'>B</p>"
        "<p style='text-align:center;text-align-all:right'>C</p>"
        "<p style='text-align-all:right;text-align:center'>D</p>"
        "<div style='text-align-last:right'><p style='text-align-all:center'>E</p></div>"
        "<p style='text-align:center;text-align-last:initial'>F</p>"
        "<p style='text-align-all:right!important;text-align:center'>G</p>"
        "<p style='text-align-all:left;text-align:right!important'>H</p>"
        "<p style='text-align-all:right;text-align:initial'>I</p>"
        "<div style='text-align:start;direction:rtl'>"
        "<p style='direction:ltr;text-align-all:match-parent'>J</p></div>"
        "<div style='text-align-last:end;direction:ltr'>"
        "<p style='text-align-all:center;text-align-last:match-parent'>K</p></div>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    char qhtml[PATH_MAX + 8];
    char qview[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(view_path, qview, sizeof(qview));
    snprintf(cmd, sizeof(cmd),
        "%s layout %s%s --view-output %s > temp/render_output_parity/text_align_all.out 2> temp/render_output_parity/text_align_all.err",
        LAMBDA_EXE, lambda_no_log_arg(), qhtml, qview);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    const char* labels[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K"};
    float positions[11] = {};
    for (int i = 0; i < 11; i++) {
        ASSERT_TRUE(view_text_x(view_path, labels[i], &positions[i])) << labels[i];
    }
    EXPECT_GT(positions[0], 150.0f);
    EXPECT_GT(positions[1], 70.0f);
    EXPECT_LT(positions[1], 110.0f);
    EXPECT_GT(positions[2], 150.0f);
    EXPECT_GT(positions[3], 70.0f);
    EXPECT_LT(positions[3], 110.0f);
    EXPECT_GT(positions[4], 150.0f);
    EXPECT_GT(positions[5], 70.0f);
    EXPECT_LT(positions[5], 110.0f);
    EXPECT_GT(positions[6], 150.0f);
    EXPECT_GT(positions[7], 150.0f);
    EXPECT_LT(positions[8], 10.0f);
    EXPECT_GT(positions[9], 150.0f);
    EXPECT_GT(positions[10], 150.0f);
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
