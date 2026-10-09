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
#include "../lib/file.h"
#include "../lib/mem.h"

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
#include "../lib/str.h"
#include "../lib/strbuf.h"
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

static bool render_document_fixture(const char* html_path, const char* output_path,
                                    const char* options = "") {
    char qhtml[PATH_MAX + 8];
    char qoutput[PATH_MAX + 8];
    char cmd[PATH_MAX * 4 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(output_path, qoutput, sizeof(qoutput));
    snprintf(cmd, sizeof(cmd),
             "%s render %s%s %s -o %s > %s.out 2> %s.err",
             LAMBDA_EXE, lambda_no_log_arg(), qhtml, options, qoutput,
             qoutput, qoutput);
    int status = system(cmd);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool render_html_fixture(const char* html_path, const char* output_path,
                                    const char* html, const char* options = "") {
    return write_file_all(html_path, html, strlen(html)) &&
        render_document_fixture(html_path, output_path, options);
}

static CommandResult run_html_fixture_script(const char* html_path, const char* js_path,
                                             const char* script) {
    if (!write_file_all(js_path, script, strlen(script))) {
        CommandResult result = {};
        result.exit_code = -1;
        return result;
    }
    char qhtml[PATH_MAX + 8], qjs[PATH_MAX + 8], command[PATH_MAX * 2 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(js_path, qjs, sizeof(qjs));
    snprintf(command, sizeof(command), "%s js %s --document %s --no-log 2>&1", LAMBDA_EXE, qjs, qhtml);
    return run_command_capture(command);
}

static bool rasterize_fixture_pdf(const char* pdf_path, const char* png_path) {
    char qpdf[PATH_MAX + 8], qpng[PATH_MAX + 8], command[PATH_MAX * 3 + 128];
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    shell_quote(png_path, qpng, sizeof(qpng));
    if (command_exists("sips")) {
        snprintf(command, sizeof(command), "sips -s format png %s --out %s >/dev/null 2>&1", qpdf, qpng);
    } else if (command_exists("pdftoppm")) {
        char prefix[PATH_MAX], qprefix[PATH_MAX + 8];
        str_copy(prefix, sizeof(prefix), png_path, strlen(png_path));
        size_t length = strlen(prefix);
        if (length < 4 || strcmp(prefix + length - 4, ".png") != 0) return false;
        prefix[length - 4] = '\0';
        shell_quote(prefix, qprefix, sizeof(qprefix));
        snprintf(command, sizeof(command), "pdftoppm -png -f 1 -l 1 -singlefile -r 72 %s %s >/dev/null 2>&1", qpdf, qprefix);
    } else return false;
    int status = system(command);
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

static CommandResult pdf_info(const char* pdf_path) {
    char qpath[PATH_MAX + 8];
    char cmd[PATH_MAX + 128];
    shell_quote(pdf_path, qpath, sizeof(qpath));
    snprintf(cmd, sizeof(cmd), "pdfinfo %s 2>&1", qpath);
    return run_command_capture(cmd);
}

static int pdf_page_count(const char* pdf_path) {
    CommandResult result = pdf_info(pdf_path);
    if (result.exit_code != 0) return 0;

    const char* pages = strstr(result.output, "Pages:");
    if (!pages) return 0;
    pages += 6;
    while (*pages && !isdigit((unsigned char)*pages)) pages++;
    return atoi(pages);
}

static bool render_reference_page(const PdfFileInfo* pdf, int page, char* out_png, size_t out_size,
        bool crop_box = false, bool natural_96dpi = false) {
    char prefix[PATH_MAX];
    char qpdf[PATH_MAX + 8];
    char qprefix[PATH_MAX + 8];
    char cmd[PATH_MAX * 2 + 256];

    snprintf(prefix, sizeof(prefix), "%s/%s_page_%02d_ref", PDF_REF_DIR, pdf->base, page);
    snprintf(out_png, out_size, "%s.png", prefix);
    unlink(out_png);

    shell_quote(pdf->path, qpdf, sizeof(qpdf));
    shell_quote(prefix, qprefix, sizeof(qprefix));
    char resolution[64];
    if (natural_96dpi) snprintf(resolution, sizeof(resolution), "-r 96");
    else snprintf(resolution, sizeof(resolution), "-scale-to-x %d -scale-to-y -1", RENDER_WIDTH);
    snprintf(cmd, sizeof(cmd), "pdftoppm -png -f %d -l %d -singlefile %s %s %s %s 2>&1",
        page, page, crop_box ? "-cropbox" : "", resolution, qpdf, qprefix);
    CommandResult result = run_command_capture(cmd);
    if (result.exit_code != 0 || !file_exists(out_png)) {
        fprintf(stderr, "Reference render failed for %s page %d:\n%s\n", pdf->path, page, result.output);
        return false;
    }
    return true;
}

static bool write_lambda_page_script(const PdfFileInfo* pdf, int page_index, int height, const char* script_path,
        bool natural_aspect = false) {
    char pdf_path_escaped[PATH_MAX * 2];
    char script[4096];
    char svg_height[32];
    if (natural_aspect) snprintf(svg_height, sizeof(svg_height), "auto");
    else snprintf(svg_height, sizeof(svg_height), "%dpx", height);

    lambda_string_escape(pdf->path, pdf_path_escaped, sizeof(pdf_path_escaped));
    snprintf(script, sizeof(script),
             "import pdf: lambda.pdf.pdf\n"
             "\n"
             "let doc = input(\"%s\", 'pdf') ^ { null }\n"
             "let page = pdf.pdf_to_svg(doc, %d, {show_label: false});\n"
             "<html\n"
             "  <head\n"
             "    <meta charset: \"utf-8\">\n"
             "    <style \"html,body{margin:0;padding:0;background:white;overflow:hidden;}svg{display:block;width:%dpx;height:%s;}\">\n"
             "  >\n"
             "  <body page>\n"
             ">\n",
             pdf_path_escaped, page_index, RENDER_WIDTH, svg_height);

    return write_file_all(script_path, script, strlen(script));
}

static bool render_lambda_png_page(const PdfFileInfo* pdf, int page_index, int height, char* out_png, size_t out_size,
        bool natural_aspect = false) {
    char script_path[PATH_MAX];
    char qscript[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];

    snprintf(script_path, sizeof(script_path), "%s/%s_page_%02d.ls", PDF_TEMP_DIR, pdf->base, page_index + 1);
    snprintf(out_png, out_size, "%s/%s_page_%02d_lambda.png", PDF_TEMP_DIR, pdf->base, page_index + 1);
    unlink(out_png);

    if (!write_lambda_page_script(pdf, page_index, height, script_path, natural_aspect)) return false;

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

static int sample_png_component(const ImageData& image, int x, int y, int channel, int radius) {
    int total = 0, count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int px = x + dx, py = y + dy;
            if (px < 0 || py < 0 || px >= image.width || py >= image.height) continue;
            size_t offset = ((size_t)py * image.width + px) * 4;
            total += composite_over_white(image.pixels[offset + channel], image.pixels[offset + 3]);
            count++;
        }
    }
    return total / count;
}

static void compare_pngs(const char* reference_path, const char* lambda_path,
                         const char* diff_path, double* mismatch_percent, double* mean_abs_delta,
                         int sample_radius = 0) {
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
            ref_rgb[c] = sample_png_component(ref, (int)(i % ref.width), (int)(i / ref.width), c, sample_radius);
            got_rgb[c] = sample_png_component(got, (int)(i % got.width), (int)(i / got.width), c, sample_radius);
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

static bool render_paged_parity_variant(const char* stem, const char* html,
        char* preview, PdfFileInfo* pdf, const char* preview_options = "--paged --block-remote-resources --page-grid 1x6") {
    char path[PATH_MAX]; snprintf(path, sizeof(path), "temp/render_output_parity/%s.html", stem);
    snprintf(preview, PATH_MAX, "temp/render_output_parity/%s.png", stem);
    snprintf(pdf->path, sizeof(pdf->path), "temp/render_output_parity/%s.pdf", stem);
    snprintf(pdf->base, sizeof(pdf->base), "%s", stem);
    return render_html_fixture(path, pdf->path, html, "--paged --block-remote-resources") &&
        render_document_fixture(path, preview, preview_options);
}

static void expect_paged_pair_output_parity(char previews[2][PATH_MAX], PdfFileInfo pdfs[2],
        int minimum_pages, int maximum_pages) {
    int count = pdf_page_count(pdfs[0].path);
    ASSERT_GE(count, minimum_pages); ASSERT_LE(count, maximum_pages);
    ASSERT_EQ(pdf_page_count(pdfs[1].path), count);
    expect_pngs_exactly_equal(previews[1], previews[0]);
    for (int page = 1; page <= count; page++) {
        char pngs[2][PATH_MAX];
        for (size_t variant = 0; variant < 2; variant++)
            ASSERT_TRUE(render_reference_page(&pdfs[variant], page, pngs[variant], sizeof(pngs[variant])));
        expect_pngs_exactly_equal(pngs[1], pngs[0]);
    }
}

static void expect_html_pair_output_parity(const char* name, const char* actual, const char* reference) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* sources[] = {actual, reference};
    char png_paths[2][PATH_MAX], pdf_png_paths[2][PATH_MAX], svg_paths[2][PATH_MAX];
    bool pdf_available = command_exists("sips") || command_exists("pdftoppm");
    for (size_t i = 0; i < 2; i++) {
        char html_path[PATH_MAX], pdf_path[PATH_MAX];
        const char* suffix = i ? "_reference" : "";
        snprintf(html_path, sizeof(html_path), "temp/render_output_parity/%s%s.html", name, suffix);
        snprintf(png_paths[i], sizeof(png_paths[i]), "temp/render_output_parity/%s%s.png", name, suffix);
        snprintf(svg_paths[i], sizeof(svg_paths[i]), "temp/render_output_parity/%s%s.svg", name, suffix);
        ASSERT_TRUE(render_html_fixture(html_path, png_paths[i], sources[i]));
        ASSERT_TRUE(render_document_fixture(html_path, svg_paths[i]));
        if (pdf_available) {
            snprintf(pdf_path, sizeof(pdf_path), "temp/render_output_parity/%s%s.pdf", name, suffix);
            snprintf(pdf_png_paths[i], sizeof(pdf_png_paths[i]), "temp/render_output_parity/%s%s_pdf.png", name, suffix);
            ASSERT_TRUE(render_document_fixture(html_path, pdf_path));
            ASSERT_TRUE(rasterize_fixture_pdf(pdf_path, pdf_png_paths[i]));
        }
    }
    expect_pngs_exactly_equal(png_paths[1], png_paths[0]);
    if (pdf_available) expect_pngs_exactly_equal(pdf_png_paths[1], pdf_png_paths[0]);
    char* svg[2] = {};
    ASSERT_TRUE(file_read_all(svg_paths[0], MEM_CAT_TEMP, &svg[0], nullptr));
    ASSERT_TRUE(file_read_all(svg_paths[1], MEM_CAT_TEMP, &svg[1], nullptr));
    EXPECT_STREQ(svg[0], svg[1]);
    mem_free(svg[0]);
    mem_free(svg[1]);
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

TEST(RenderOutputParity, PagedPdfUsesCssSheetsAndA4FallbackWithoutRasterScaling) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_sheets.html";
    const char* pdf_path = "temp/render_output_parity/paged_sheets.pdf";
    const char* html = "<!doctype html><html><head><style>"
        "@page { size: 320px 480px; margin: 40px; @bottom-center { content: counter(page) '/' counter(pages) } }"
        "html,body { margin: 0; font: 16px Arial } section { height: 120px }"
        "section + section { break-before: page } @media print { section { color: blue } }"
        "</style></head><body><section>Page one</section><section>Page two</section><section>Page three</section></body></html>";
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html, "--paged -s 2 -vw 900 -vh 700"));
    EXPECT_EQ(pdf_page_count(pdf_path), 3);
    CommandResult info = pdf_info(pdf_path); ASSERT_EQ(info.exit_code, 0) << info.output;
    const char* size = strstr(info.output, "Page size:"); ASSERT_NE(size, nullptr);
    double width = 0.0, height = 0.0;
    ASSERT_EQ(sscanf(size, "Page size: %lf x %lf", &width, &height), 2);
    EXPECT_NEAR(width, 240.0, 0.01); EXPECT_NEAR(height, 360.0, 0.01);
    const char* a4 = "<!doctype html><html><head><style>html,body { margin: 0 }"
        "p + p { break-before: page }</style></head><body><p>First A4 sheet</p><p>Second A4 sheet</p></body></html>";
    const char* a4_path = "temp/render_output_parity/paged_a4.pdf";
    ASSERT_TRUE(render_html_fixture(html_path, a4_path, a4, "--paged"));
    EXPECT_EQ(pdf_page_count(a4_path), 2);
    info = pdf_info(a4_path); ASSERT_EQ(info.exit_code, 0) << info.output;
    size = strstr(info.output, "Page size:"); ASSERT_NE(size, nullptr);
    ASSERT_EQ(sscanf(size, "Page size: %lf x %lf", &width, &height), 2);
    EXPECT_NEAR(width, 210.0 * 72.0 / 25.4, 0.01); EXPECT_NEAR(height, 297.0 * 72.0 / 25.4, 0.01);
    const char* continuous = "temp/render_output_parity/continuous_sheets.pdf";
    ASSERT_TRUE(render_html_fixture(html_path, continuous, html));
    EXPECT_EQ(pdf_page_count(continuous), 1);
}

TEST(RenderOutputParity, LogicalPageLabelsMatchAuthoredTextInPhysicalPdfAndFilteredPreview) {
    ASSERT_TRUE(command_exists("pdftoppm"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    const char* rules[] = {
        "@page { counter-increment: page 2; @bottom-center { content: counter(page) '/' counter(pages) } }"
        "@page:first { counter-reset: page 0 } @page chapter { counter-reset: page 8; counter-increment: page 0 }"
        "a::after { content: target-counter('#target',page) '/' target-counters('#target',page,'.',upper-roman) }",
        "@page { @bottom-center { content: '4/3' } } @page:first { @bottom-center { content: '2/3' } }"
        "@page chapter { @bottom-center { content: '8/3' } } a::after { content: '8/VIII' }"
    };
    char previews[2][PATH_MAX];
    PdfFileInfo pdfs[2] = {};
    for (size_t i = 0; i < 2; i++) {
        char html[4096], html_path[PATH_MAX];
        snprintf(html, sizeof(html), "<!doctype html><html><head><style>"
            "@page { size:240px 120px; margin:20px; @bottom-center { font:10px Arial } }"
            "html,body { margin:0; font:10px/12px Arial } section + section { break-before:page }"
            "section:last-child { page:chapter } %s</style></head><body>"
            "<section><a>See</a></section><section>Middle</section><section id='target'>Final</section></body></html>", rules[i]);
        snprintf(html_path, sizeof(html_path), "temp/render_output_parity/page_labels_%zu.html", i);
        snprintf(pdfs[i].path, sizeof(pdfs[i].path), "temp/render_output_parity/page_labels_%zu.pdf", i);
        snprintf(pdfs[i].base, sizeof(pdfs[i].base), "page_labels_%zu", i);
        ASSERT_TRUE(render_html_fixture(html_path, pdfs[i].path, html, "--paged"));
        ASSERT_EQ(pdf_page_count(pdfs[i].path), 3);
        snprintf(previews[i], sizeof(previews[i]), "temp/render_output_parity/page_labels_%zu.png", i);
        ASSERT_TRUE(render_document_fixture(html_path, previews[i], "--paged --pages 3,1 --page-grid 2x2 --page-scale .75"));
    }
    expect_pngs_exactly_equal(previews[1], previews[0]);
    for (int page = 1; page <= 3; page++) {
        char pngs[2][PATH_MAX];
        for (size_t i = 0; i < 2; i++)
            ASSERT_TRUE(render_reference_page(&pdfs[i], page, pngs[i], sizeof(pngs[i])));
        expect_pngs_exactly_equal(pngs[1], pngs[0]);
    }
}

TEST(RenderOutputParity, PagedPdfUsesPostScriptStylesAndRejectsUnimplementedContexts) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_settled.html";
    const char* pdf_path = "temp/render_output_parity/paged_settled.pdf";
    const char* html = "<!doctype html><html><head><style>html,body{margin:0}"
        "@page{size:320px 480px;margin:40px}</style></head><body><p>First</p><p id='next'>Second</p>"
        "<script>document.getElementById('next').style.breakBefore='page';</script></body></html>";
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html, "--paged"));
    EXPECT_EQ(pdf_page_count(pdf_path), 2);
    const char* invalid = "<!doctype html><html><body><div style='display:grid'>Grid</div></body></html>";
    const char* invalid_path = "temp/render_output_parity/paged_invalid.pdf";
    remove(invalid_path);
    EXPECT_FALSE(render_html_fixture(html_path, invalid_path, invalid, "--paged"));
    EXPECT_FALSE(file_exists(invalid_path));
    EXPECT_TRUE(file_contains_text("temp/render_output_parity/paged_invalid.pdf.err", "formatting context requires"));
    const char* preview = "temp/render_output_parity/paged_settled.png";
    ASSERT_TRUE(render_html_fixture(html_path, preview, html, "--paged"));
    ImageData image = {}; ASSERT_TRUE(load_png_rgba(preview, &image));
    EXPECT_EQ(image.width, 320); EXPECT_EQ(image.height, 960); image_free(image.pixels);
}

TEST(RenderOutputParity, PagedPdfBacktracksLateBlockClosuresWithNotesAndFloats) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_rollback.html";
    const char* pdf_path = "temp/render_output_parity/paged_rollback.pdf";
    const char* html = "<!doctype html><html><head><style>"
        "@page{size:220px 100px;margin:10px;@top-center{content:string(Title);font-size:7px}}"
        "html,body{margin:0}body,div,span{font-size:10px;line-height:12px;white-space:pre-wrap;orphans:1;widows:1}"
        "body{counter-reset:N}.prelude{height:20px;string-set:Title 'Prelude'}"
        ".moved{padding-bottom:16px;break-inside:avoid;counter-increment:N;string-set:Title 'Moved';background:#eee}"
        ".call::before{content:counter(N) ': '}.note{float:footnote;footnote-policy:line;color:blue}"
        ".float{float:top;float-reference:page;color:red}.tail{padding-bottom:16px}"
        "</style></head><body><div class='prelude'>Prelude</div><div class='moved' id='moved'>"
        "<div class='call'>Call <span class='note'>Note A\nNote B</span></div><div class='float'>Float</div>"
        "<div class='tail'></div></div></body></html>";
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html, "--paged"));
    EXPECT_EQ(pdf_page_count(pdf_path), 2);
    CommandResult info = pdf_info(pdf_path); ASSERT_EQ(info.exit_code, 0) << info.output;
    const char* size = strstr(info.output, "Page size:"); ASSERT_NE(size, nullptr);
    double width = 0.0, height = 0.0;
    ASSERT_EQ(sscanf(size, "Page size: %lf x %lf", &width, &height), 2);
    EXPECT_NEAR(width, 165.0, 0.01); EXPECT_NEAR(height, 75.0, 0.01);
}

TEST(RenderOutputParity, PagedPdfBacktracksSiblingKeepChainsWithLateNotes) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_keep_chain.html";
    const char* pdf_path = "temp/render_output_parity/paged_keep_chain.pdf";
    const char* html = "<!doctype html><html><head><style>"
        "@page{size:200px 100px;margin:10px;@top-center{content:string(Title,last);font-size:7px}}"
        "html,body,div,h2,span{margin:0;font-size:10px;line-height:12px;orphans:1;widows:1}"
        ".prelude{height:24px;string-set:Title 'Prelude'}h2{break-after:avoid;color:blue}"
        "#first{string-set:Title 'First'}#second{string-set:Title 'Second'}#third{string-set:Title 'Third'}"
        ".note{float:footnote;footnote-policy:line;white-space:pre-wrap;color:red}"
        "</style></head><body><div class='prelude'>Prelude</div>"
        "<h2 id='first'>First</h2><h2 id='second'>Second</h2><h2 id='third'>Third</h2>"
        "<div>Call <span class='note'>Note A\nNote B</span></div></body></html>";
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html, "--paged"));
    EXPECT_EQ(pdf_page_count(pdf_path), 2);
    CommandResult info = pdf_info(pdf_path); ASSERT_EQ(info.exit_code, 0) << info.output;
    const char* size = strstr(info.output, "Page size:"); ASSERT_NE(size, nullptr);
    double width = 0.0, height = 0.0;
    ASSERT_EQ(sscanf(size, "Page size: %lf x %lf", &width, &height), 2);
    EXPECT_NEAR(width, 150.0, 0.01); EXPECT_NEAR(height, 75.0, 0.01);
}

TEST(RenderOutputParity, PagedPdfListMarkersResumeWithoutRepeatingOrdinals) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_lists.html";
    const char* pdf_path = "temp/render_output_parity/paged_lists.pdf";
    const char* html = "<!doctype html><html><head><style>"
        "@page{size:160px 56px;margin:8px}html,body,div,ol,li{margin:0;font-size:10px;line-height:12px}"
        "ol{padding-left:24px}li{white-space:pre-wrap;orphans:2;widows:1}li::marker{color:blue}"
        ".prelude{height:24px}</style></head><body><div class='prelude'>Prelude</div>"
        "<ol start='4'><li>A\nB\nC\nD</li><li>E</li></ol></body></html>";
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html, "--paged"));
    EXPECT_EQ(pdf_page_count(pdf_path), 3);
    CommandResult info = pdf_info(pdf_path); ASSERT_EQ(info.exit_code, 0) << info.output;
    const char* size = strstr(info.output, "Page size:"); ASSERT_NE(size, nullptr);
    double width = 0.0, height = 0.0;
    ASSERT_EQ(sscanf(size, "Page size: %lf x %lf", &width, &height), 2);
    EXPECT_NEAR(width, 120.0, 0.01); EXPECT_NEAR(height, 42.0, 0.01);
    if (!command_exists("pdftoppm")) GTEST_SKIP() << "Poppler is needed to inspect paged marker placement";
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    PdfFileInfo rendered = {};
    snprintf(rendered.path, sizeof(rendered.path), "%s", pdf_path);
    snprintf(rendered.base, sizeof(rendered.base), "paged_lists");
    for (int page = 1; page <= 3; page++) {
        char png_path[PATH_MAX]; ASSERT_TRUE(render_reference_page(&rendered, page, png_path, sizeof(png_path)));
        ImageData image = {}; ASSERT_TRUE(load_png_rgba(png_path, &image));
        size_t markers = 0;
        for (size_t i = 0; i < (size_t)image.width * image.height; i++) {
            const unsigned char* pixel = image.pixels + i * 4;
            if (pixel[2] > 180 && pixel[0] < 80 && pixel[1] < 80) markers++;
        }
        image_free(image.pixels);
        if (page == 1) EXPECT_EQ(markers, 0u);
        else EXPECT_GT(markers, 0u);
    }
}

static const char* paged_preview_fixture() {
    return "<!doctype html><html><head><style>"
        "@page{size:120px 160px;margin:20px;@bottom-center{content:counter(page)}}"
        "html,body{margin:0;font:12px/16px Arial}div{height:80px}div+div{break-before:page}"
        "</style></head><body><div style='background:#ff0000'>One</div>"
        "<div style='background:#00ff00'>Two</div><div style='background:#0000ff'>Three</div>"
        "<div style='background:#ff8000'>Four</div><div style='background:#8000ff'>Five</div></body></html>";
}

static void expect_preview_pixel(const ImageData& image, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    SCOPED_TRACE(::testing::Message() << "pixel " << x << "," << y << " in " << image.width << "x" << image.height);
    ASSERT_GE(x, 0); ASSERT_GE(y, 0); ASSERT_LT(x, image.width); ASSERT_LT(y, image.height);
    const uint8_t* pixel = image.pixels + ((size_t)y * image.width + x) * 4;
    EXPECT_EQ(pixel[0], r); EXPECT_EQ(pixel[1], g); EXPECT_EQ(pixel[2], b);
}

TEST(RenderOutputParity, PagedTablesRepeatGroupsAndSplitCellsInPdfAndPreview) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_tables.html";
    const char* pdf_path = "temp/render_output_parity/paged_tables.pdf";
    const char* preview_path = "temp/render_output_parity/paged_tables.png";
    for (bool automatic : {false, true}) {
        SCOPED_TRACE(automatic ? "automatic layout" : "fixed layout");
        StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
        strbuf_append_format(html, "<!doctype html><style>@page{size:240px 140px;margin:10px}"
            "html,body{margin:0;font:10px/12px Arial}table{table-layout:fixed;width:100%%;border-collapse:separate;border-spacing:0}"
            "td,th{vertical-align:top;padding:2px;border:1px solid black;orphans:1;widows:1}"
            "thead{background:#cce0ff}tfoot{background:#ddffdd}.long{white-space:pre-wrap}.automatic tbody td:first-child{width:80px}.automatic tfoot td:last-child{width:100px}</style>"
            "<table class='automatic' style=\"%s\"><thead><tr><th>Heading A</th><th>Heading B</th></tr></thead>"
            "<tfoot><tr><td>Footer A</td><td>Footer B</td></tr></tfoot><tbody>", automatic ? "table-layout:auto;width:auto;margin:0 auto" : "");
        for (size_t i = 0; i < 9; i++) strbuf_append_str(html, "<tr><td>Body A</td><td>Body B</td></tr>");
        strbuf_append_str(html, "<tr><td class='long'>A\nB\nC\nD\nE\nF\nG\nH\nI\nJ</td><td>Short</td></tr></tbody></table>");
        bool rendered = render_html_fixture(html_path, pdf_path, html->str, "--paged --block-remote-resources");
        strbuf_free(html); ASSERT_TRUE(rendered);
        ASSERT_EQ(pdf_page_count(pdf_path), 4);
        EXPECT_FALSE(file_contains_text(pdf_path, "/Subtype /Image"));
        ASSERT_TRUE(render_document_fixture(html_path, preview_path,
            "--paged --block-remote-resources --page-grid 2x2 --page-scale .5"));
        ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_path, &preview));
        EXPECT_EQ(preview.width, 240); EXPECT_EQ(preview.height, 140);
        const int footers[] = {106, 106, 115, 97};
        for (int page = 0; page < 4; page++) {
            int x = page % 2 * 120, y = page / 2 * 70;
            expect_preview_pixel(preview, x + 100, y + 8, 204, 224, 255);
            expect_preview_pixel(preview, x + 100, y + footers[page] / 2, 221, 255, 221);
        }
        if (automatic) {
            expect_preview_pixel(preview, 10, 8, 255, 255, 255);
            expect_preview_pixel(preview, 15, 6, 204, 224, 255);
            expect_preview_pixel(preview, 112, 8, 255, 255, 255);
        }
        image_free(preview.pixels);
        ASSERT_TRUE(command_exists("pdftoppm")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
        PdfFileInfo pdf = {}; snprintf(pdf.path, sizeof(pdf.path), "%s", pdf_path);
        snprintf(pdf.base, sizeof(pdf.base), "paged_tables");
        for (int page = 0; page < 4; page++) {
            char png[PATH_MAX]; ASSERT_TRUE(render_reference_page(&pdf, page + 1, png, sizeof(png)));
            ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
            EXPECT_EQ(image.width, 600); EXPECT_EQ(image.height, 350);
            expect_preview_pixel(image, 500, 40, 204, 224, 255);
            expect_preview_pixel(image, 500, footers[page] * 5 / 2, 221, 255, 221);
            if (automatic) {
                expect_preview_pixel(image, 50, 40, 255, 255, 255);
                expect_preview_pixel(image, 75, 30, 204, 224, 255);
                expect_preview_pixel(image, 560, 40, 255, 255, 255);
            }
            image_free(image.pixels);
        }
    }
}

TEST(RenderOutputParity, PagedCaptionsMatchBlockWrappersAcrossTableAndCaptionContinuations) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    const char* stems[] = {"paged_captions", "paged_caption_block_reference"};
    for (bool automatic : {false, true}) for (bool long_caption : {false, true}) {
        SCOPED_TRACE(automatic ? "automatic layout" : "fixed layout");
        SCOPED_TRACE(long_caption ? "caption continuations" : "table continuations");
        char previews[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t variant = 0; variant < 2; variant++) {
            StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
            strbuf_append_format(html, "<!doctype html><style>@page{size:160px 112px;margin:10px}"
                "@page :left{size:140px 112px}html,body{margin:0;font:10px/12px Arial}"
                "table{table-layout:%s;width:100%%;box-sizing:border-box;border:2px solid blue;"
                "padding:1px;border-spacing:0;background:magenta}"
                "caption,.cap{padding:1px;border:1px solid red;background:yellow;text-align:center;"
                "white-space:pre;orphans:1;widows:1;box-decoration-break:clone}"
                ".bottom{caption-side:bottom;background:lime}td,th{padding:0;vertical-align:top}"
                "thead{background:#cce0ff}tfoot{background:#ddffdd}col:first-child{width:40px;background:cyan}"
                "</style>", automatic ? "auto" : "fixed");
            const char* top = long_caption ? "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL" : "Top caption";
            const char* bottom = long_caption ? "M\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX" : "Bottom caption";
            if (variant) strbuf_append_format(html, "<div><div class='cap'>%s</div>", top);
            strbuf_append_str(html, "<table><colgroup><col><col></colgroup>");
            if (!variant) strbuf_append_format(html, "<caption class='bottom'>%s</caption>", bottom);
            if (!long_caption) strbuf_append_str(html,
                "<thead><tr><th>Head A</th><th>Head B</th></tr></thead>"
                "<tfoot><tr><td>Foot A</td><td>Foot B</td></tr></tfoot>");
            strbuf_append_str(html, "<tbody>");
            for (size_t row = 0; row < (long_caption ? 1u : 10u); row++)
                strbuf_append_str(html, "<tr><td>Body A</td><td>Body B</td></tr>");
            strbuf_append_str(html, "</tbody>");
            if (!variant) strbuf_append_format(html, "<caption>%s</caption>", top);
            strbuf_append_str(html, "</table>");
            if (variant) strbuf_append_format(html, "<div class='cap bottom'>%s</div></div>", bottom);
            bool rendered = render_paged_parity_variant(stems[variant], html->str, previews[variant], &pdfs[variant]);
            strbuf_free(html); ASSERT_TRUE(rendered);
        }
        expect_paged_pair_output_parity(previews, pdfs, 2, 6);
    }
}

TEST(RenderOutputParity, PagedColumnLayersAndWidthsMatchExplicitCellsAcrossContinuations) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    const char* stems[] = {"paged_column_boxes", "paged_column_cell_reference"};
    for (bool automatic : {false, true}) {
        SCOPED_TRACE(automatic ? "automatic layout" : "fixed layout");
        char previews[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t variant = 0; variant < 2; variant++) {
            StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
            strbuf_append_format(html, "<!doctype html><style>@page{size:160px 112px;margin:10px}"
                "html,body{margin:0;font:10px/12px Arial}table{table-layout:%s;width:100%%;border-spacing:4px 2px;background:magenta}"
                "td,th{padding:0;white-space:pre-wrap;orphans:1;widows:1}", automatic ? "auto" : "fixed");
            strbuf_append_str(html, variant ?
                "thead th:nth-child(-n+2){width:30px}thead th:first-child,tfoot td:first-child{background:red}"
                "thead th:nth-child(2),tfoot td:nth-child(2){background:yellow}thead th:last-child,tfoot td:last-child{background:blue}"
                "tbody:first-of-type td:first-child{background:red}tbody:first-of-type td:last-child{background:blue}" :
                "colgroup{background:yellow}col:first-child{width:30px;background:red}col:nth-child(2){width:30px}col:last-child{background:blue}");
            strbuf_append_str(html, "#override{background:cyan}#override td:first-child{background:lime}</style><table>");
            if (!variant) strbuf_append_str(html, "<colgroup span='999'><col><col><col></colgroup>");
            strbuf_append_str(html, "<thead><tr><th>H</th><th>H</th><th>H</th></tr></thead>"
                "<tfoot><tr><td>F</td><td>F</td><td>F</td></tr></tfoot>"
                "<tbody><tr><td colspan='2'>A\nB\nC\nD\nE\nF\nG\nH\nI\nJ</td><td>Z</td></tr></tbody>"
                "<tbody><tr id='override'><td>U</td><td>V</td><td>W</td></tr></tbody></table>");
            bool rendered = render_paged_parity_variant(stems[variant], html->str, previews[variant], &pdfs[variant],
                "--paged --block-remote-resources --page-grid 1x3");
            strbuf_free(html); ASSERT_TRUE(rendered);
        }
        expect_paged_pair_output_parity(previews, pdfs, 3, 3);
        ImageData preview = {}; ASSERT_TRUE(load_png_rgba(previews[0], &preview));
        EXPECT_EQ(preview.width, 480); EXPECT_EQ(preview.height, 112);
        for (int page = 0; page < 3; page++) {
            expect_preview_pixel(preview, page * 160 + 36, 15, 255, 0, 0);
            expect_preview_pixel(preview, page * 160 + 70, 15, 255, 255, 0);
            expect_preview_pixel(preview, page * 160 + 132, 15, 0, 0, 255);
            expect_preview_pixel(preview, page * 160 + 46, 15, 255, 0, 255);
            expect_preview_pixel(preview, page * 160 + 70, 25, 255, 0, 255);
            expect_preview_pixel(preview, page * 160 + 70, 30, page == 2 ? 0 : 255, page == 2 ? 255 : 0, page == 2 ? 255 : 0);

        }
        image_free(preview.pixels);
    }
}

TEST(RenderOutputParity, PagedRowSpansMatchExplicitStackedCellsAcrossRepeatedFurniture) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    const char* stems[] = {"paged_rowspans", "paged_rowspan_block_reference"};
    for (bool automatic : {false, true}) for (const char* align : {"top", "middle", "bottom", "baseline"}) {
        SCOPED_TRACE(automatic ? "automatic layout" : "fixed layout");
        SCOPED_TRACE(align);
        char previews[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t variant = 0; variant < 2; variant++) {
            StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
            strbuf_append_format(html, "<!doctype html><style>@page{size:160px 112px;margin:10px}"
                "@page :left{size:140px 112px}html,body{margin:0;font:10px/12px Arial}"
                "table{table-layout:%s;width:100%%;border-spacing:0;background:magenta}"
                "caption{padding:0;text-align:left}td,th{padding:0;vertical-align:top}"
                "col:first-child{width:40px;background:cyan}.span{vertical-align:%s;background:yellow}"
                ".upper{height:20px;background:lime}.lower{height:20px;background:#ccf}"
                "thead{background:#cce0ff}tfoot{background:#ddffdd}</style>"
                "<table><caption>Span clusters</caption><colgroup><col><col></colgroup>"
                "<thead><tr><th>Head A</th><th>Head B</th></tr></thead>"
                "<tfoot><tr><td>Foot A</td><td>Foot B</td></tr></tfoot><tbody>", automatic ? "auto" : "fixed", align);
            // one reference row contains ordinary blocks, giving each two-row cluster the same explicit geometry.
            for (size_t row = 0; row < 5; row++) strbuf_append_str(html, variant ?
                "<tr><td class='span'>Span</td><td><div class='upper'>Upper</div><div class='lower'>Lower</div></td></tr>" :
                "<tr><td class='span' rowspan='2'>Span</td><td class='upper'>Upper</td></tr><tr><td class='lower'>Lower</td></tr>");
            strbuf_append_str(html, "</tbody></table>");
            bool rendered = render_paged_parity_variant(stems[variant], html->str, previews[variant], &pdfs[variant]);
            strbuf_free(html); ASSERT_TRUE(rendered);
        }
        expect_paged_pair_output_parity(previews, pdfs, 5, 5);
    }
}

// hand-authored page dictionaries and streams keep the PDF import oracle independent of our writer.
static bool write_fixed_pdf_fixture(const char* path, int count, bool labels = false) {
    StrBuf* pdf = strbuf_new(); StrBuf* stream = strbuf_new();
    int objects = 3 + 2 * count + (labels ? 3 : 0);
    size_t* offsets = (size_t*)calloc((size_t)objects, sizeof(size_t));
    if (!pdf || !stream || !offsets) { strbuf_free(pdf); strbuf_free(stream); free(offsets); return false; }
    strbuf_append_str(pdf, "%PDF-1.4\n");
    offsets[1] = pdf->length; strbuf_append_str(pdf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R ");
    if (labels) strbuf_append_format(pdf, "/PageLabels %d 0 R ", 3 + 2 * count);
    strbuf_append_str(pdf, ">>\nendobj\n");
    offsets[2] = pdf->length; strbuf_append_format(pdf, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", count);
    for (int i = 0; i < count; i++) strbuf_append_format(pdf, "%d 0 R ", 3 + 2 * i);
    strbuf_append_str(pdf, "] >>\nendobj\n");
    struct Geometry { float media[4], crop[4]; int rotation; };
    const Geometry geometries[] = {{{-10, -20, 100, 80}, {0, -10, 90, 50}, 0},
        {{-10, -20, 100, 80}, {-15, -10, 90, 90}, 90},
        {{10, 20, 160, 120}, {20, 30, 150, 100}, 180},
        {{-50, 10, 130, 90}, {-40, 20, 120, 80}, -90}};
    for (int i = 0; i < count; i++) {
        const Geometry& g = geometries[i % 4];
        offsets[3 + 2 * i] = pdf->length;
        strbuf_append_format(pdf, "%d 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [%g %g %g %g] ",
            3 + 2 * i, g.media[0], g.media[1], g.media[2], g.media[3]);
        for (const char* box : {"CropBox", "BleedBox", "TrimBox", "ArtBox"})
            strbuf_append_format(pdf, "/%s [%g %g %g %g] ", box, g.crop[0], g.crop[1], g.crop[2], g.crop[3]);
        strbuf_append_format(pdf, "/Rotate %d /Resources << >> /Contents %d 0 R >>\nendobj\n", g.rotation, 4 + 2 * i);
        strbuf_reset(stream);
        strbuf_append_format(stream, "0.8 0.9 1 rg %g %g %g %g re f\n1 0 0 rg %g %g 25 20 re f\n"
            "0 0.6 0 rg %g %g 25 20 re f\n0 0 1 rg %g %g 25 20 re f\n",
            g.media[0], g.media[1], g.media[2] - g.media[0], g.media[3] - g.media[1],
            g.media[0] + 15.0f, g.media[1] + 15.0f, g.media[2] - 40.0f, g.media[3] - 35.0f,
            g.media[0] + 30.0f, g.media[1] + 30.0f);
        offsets[4 + 2 * i] = pdf->length;
        strbuf_append_format(pdf, "%d 0 obj\n<< /Length %zu >>\nstream\n", 4 + 2 * i, stream->length);
        strbuf_append_str_n(pdf, stream->str, stream->length); strbuf_append_str(pdf, "endstream\nendobj\n");
    }
    if (labels) {
        int first = 3 + 2 * count;
        offsets[first] = pdf->length;
        strbuf_append_format(pdf, "%d 0 obj\n<< /Kids [%d 0 R %d 0 R] >>\nendobj\n", first, first + 1, first + 2);
        offsets[first + 1] = pdf->length;
        strbuf_append_format(pdf, "%d 0 obj\n<< /Limits [0 2] /Nums [0 << /S /r /St 4 >> 2 << /S /D /P (Section-) /St 8 >>] >>\nendobj\n", first + 1);
        offsets[first + 2] = pdf->length;
        strbuf_append_format(pdf, "%d 0 obj\n<< /Limits [4 5] /Nums [4 << /S /A /St 27 >> 5 << /P <FEFF4E2DD83DDE00> >>] >>\nendobj\n", first + 2);
    }
    size_t xref = pdf->length;
    strbuf_append_format(pdf, "xref\n0 %d\n0000000000 65535 f \n", objects);
    for (int i = 1; i < objects; i++) strbuf_append_format(pdf, "%010zu 00000 n \n", offsets[i]);
    strbuf_append_format(pdf, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", objects, xref);
    bool written = write_file_all(path, pdf->str, pdf->length);
    strbuf_free(pdf); strbuf_free(stream); free(offsets); return written;
}

// PDF paths and CSS colors pass through 8-bit channels; page-boundary antialiasing differs across rasterizers.
static void expect_fixed_pdf_interior_parity(const char* reference, const char* actual) {
    ImageData expected = {}, got = {}; ASSERT_TRUE(load_png_rgba(reference, &expected)); ASSERT_TRUE(load_png_rgba(actual, &got));
    ASSERT_EQ(expected.width, got.width); ASSERT_EQ(expected.height, got.height);
    size_t compared = 0, mismatched = 0;
    for (int y = 1; y + 1 < expected.height; y++) for (int x = 1; x + 1 < expected.width; x++) {
        size_t center = ((size_t)y * expected.width + x) * 4; bool uniform = true;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            size_t neighbor = ((size_t)(y + dy) * expected.width + x + dx) * 4;
            if (memcmp(expected.pixels + center, expected.pixels + neighbor, 4)) uniform = false;
        }
        if (!uniform) continue;
        compared++;
        for (int channel = 0; channel < 3; channel++) if (abs((int)expected.pixels[center + channel] - (int)got.pixels[center + channel]) > 1) {
            mismatched++; break;
        }
    }
    EXPECT_GT(compared, 100u); EXPECT_EQ(mismatched, 0u) << actual;
    image_free(expected.pixels); image_free(got.pixels);
}

TEST(RenderOutputParity, FixedPdfPagesPreserveSourceBoxesQuarterTurnsAndCommonVectorPaint) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    PdfFileInfo source = {}, exported = {};
    snprintf(source.path, sizeof(source.path), "temp/render_output_parity/fixed_pdf_source.pdf");
    snprintf(source.base, sizeof(source.base), "fixed_pdf_source");
    snprintf(exported.path, sizeof(exported.path), "temp/render_output_parity/fixed_pdf_export.pdf");
    snprintf(exported.base, sizeof(exported.base), "fixed_pdf_export");
    ASSERT_TRUE(write_fixed_pdf_fixture(source.path, 4));
    ASSERT_TRUE(render_document_fixture(source.path, exported.path, "--paged --pages 1 --block-remote-resources"));
    EXPECT_EQ(pdf_page_count(exported.path), 4); EXPECT_TRUE(file_contains_text(exported.path, "/Rotate -90"));
    EXPECT_TRUE(file_contains_text(exported.path, "/MediaBox [-10 -20 100 80]"));
    EXPECT_TRUE(file_contains_text(exported.path, "/CropBox [-15 -10 90 90]"));
    EXPECT_TRUE(file_contains_text(exported.path, "/BleedBox [20 30 150 100]"));
    EXPECT_TRUE(file_contains_text(exported.path, "/TrimBox [20 30 150 100]"));
    EXPECT_TRUE(file_contains_text(exported.path, "/ArtBox [-40 20 120 80]"));
    EXPECT_FALSE(file_contains_text(exported.path, "/Subtype /Image"));
    for (int page = 1; page <= 4; page++) {
        SCOPED_TRACE(page); char reference[PATH_MAX], result[PATH_MAX], preview[PATH_MAX], args[128];
        ASSERT_TRUE(render_reference_page(&source, page, reference, sizeof(reference), true, true));
        ASSERT_TRUE(render_reference_page(&exported, page, result, sizeof(result), true, true));
        expect_fixed_pdf_interior_parity(reference, result);
        snprintf(preview, sizeof(preview), "temp/render_output_parity/fixed_pdf_preview_%d.png", page);
        snprintf(args, sizeof(args), "--paged --thumbnail-page %d --block-remote-resources", page);
        ASSERT_TRUE(render_document_fixture(source.path, preview, args)); expect_fixed_pdf_interior_parity(reference, preview);
    }
}

TEST(RenderOutputParity, FixedPdfImportsCompleteSequencesRangesAndFailWithoutTruncatingOutputs) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity")); ASSERT_TRUE(command_exists("pdfinfo"));
    const char* source = "temp/render_output_parity/fixed_pdf_53.pdf";
    const char* complete = "temp/render_output_parity/fixed_pdf_complete.pdf";
    const char* selected = "temp/render_output_parity/fixed_pdf_selected.pdf";
    const char* failure = "temp/render_output_parity/fixed_pdf_budget_failure.pdf";
    ASSERT_TRUE(write_fixed_pdf_fixture(source, 53));
    ASSERT_TRUE(render_document_fixture(source, complete, "--paged --block-remote-resources")); EXPECT_EQ(pdf_page_count(complete), 53);
    ASSERT_TRUE(render_document_fixture(source, selected, "--paged --import-pages 53,2-3,2 --import-page-limit 3 --export-pages 1,3"));
    EXPECT_EQ(pdf_page_count(selected), 2); EXPECT_TRUE(file_contains_text(selected, "/Rotate 90")); EXPECT_TRUE(file_contains_text(selected, "/Rotate 0"));
    ASSERT_TRUE(write_file_all(failure, "existing-output", strlen("existing-output")));
    EXPECT_FALSE(render_document_fixture(source, failure, "--paged --import-page-limit 52")); EXPECT_TRUE(file_contains_text(failure, "existing-output"));
    EXPECT_FALSE(render_document_fixture(source, failure, "--paged --import-pages 54")); EXPECT_TRUE(file_contains_text(failure, "existing-output"));
    EXPECT_FALSE(render_document_fixture(source, failure, "--paged --import-pages 1-3 --import-page-limit 2")); EXPECT_TRUE(file_contains_text(failure, "existing-output"));
}

TEST(RenderOutputParity, FixedPdfSourceLabelsSurviveImportAndExportSelections) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity")); ASSERT_TRUE(command_exists("pdfinfo"));
    const char* source = "temp/render_output_parity/fixed_pdf_labels.pdf";
    const char* complete = "temp/render_output_parity/fixed_pdf_labels_complete.pdf";
    const char* selected = "temp/render_output_parity/fixed_pdf_labels_selected.pdf";
    ASSERT_TRUE(write_fixed_pdf_fixture(source, 6, true));
    ASSERT_TRUE(render_document_fixture(source, complete, "--paged")); EXPECT_EQ(pdf_page_count(complete), 6);
    EXPECT_TRUE(file_contains_text(complete, "0 << /P (iv)")); EXPECT_TRUE(file_contains_text(complete, "2 << /P (Section-8)"));
    EXPECT_TRUE(file_contains_text(complete, "4 << /P (AA)")); EXPECT_TRUE(file_contains_text(complete, "5 << /P <FEFF4E2DD83DDE00>"));
    ASSERT_TRUE(render_document_fixture(source, selected, "--paged --import-pages 2,5-6 --export-pages 1,3"));
    EXPECT_EQ(pdf_page_count(selected), 2);
    EXPECT_TRUE(file_contains_text(selected, "0 << /P (v)")); EXPECT_TRUE(file_contains_text(selected, "1 << /P <FEFF4E2DD83DDE00>"));
    EXPECT_FALSE(file_contains_text(selected, "/P (AA)"));
}

TEST(RenderOutputParity, FoAndNativePageControlsSharePreviewAndPhysicalPdfPages) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    struct Fixture { const char* name; int pages; const char* media_box; };
    const Fixture fixtures[] = {{"basic", 2, "/MediaBox [0 0 300.00 420.00]"},
        {"flow_traits", 3, "/MediaBox [0 0 300.00 180.00]"},
        {"sequences", 5, "/MediaBox [0 0 150.00 60.00]"},
        {"folios", 6, "/MediaBox [0 0 225.00 75.00]"},
        {"regions", 2, "/MediaBox [0 0 180.00 135.00]"},
        {"notes", 3, "/MediaBox [0 0 150.00 75.00]"},
        {"whitespace", 2, "/MediaBox [0 0 225.00 120.00]"},
        {"tables", 3, "/MediaBox [0 0 225.00 120.00]"},
        {"alignment", 3, "/MediaBox [0 0 165.00 105.00]"},
        {"display_alignment", 3, "/MediaBox [0 0 165.00 105.00]"},
        {"graphics", 3, "/MediaBox [0 0 180.00 135.00]"},
        {"lists", 3, "/MediaBox [0 0 180.00 120.00]"},
        {"lists_context", 3, "/MediaBox [0 0 180.00 120.00]"},
        {"cell_flow", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"indents", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"indents_auto", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"corresponding", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"conditional", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"conditional_components", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"conditional_visible", 3, "/MediaBox [0 0 150.00 120.00]"},
        {"proportions", 3, "/MediaBox [0 0 225.00 120.00]"},
        {"proportions_columns", 3, "/MediaBox [0 0 225.00 120.00]"},
        {"numbered_columns", 3, "/MediaBox [0 0 225.00 120.00]"},
        {"table_furniture", 3, "/MediaBox [0 0 225.00 120.00]"}};
    const char* extensions[] = {"fo", "rpd"};
    for (const Fixture& fixture : fixtures) {
        SCOPED_TRACE(fixture.name);
        char previews[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t i = 0; i < 2; i++) {
            char source[PATH_MAX]; snprintf(source, sizeof(source), "test/html/paged_media_%s.%s", fixture.name, extensions[i]);
            snprintf(previews[i], sizeof(previews[i]), "temp/render_output_parity/fo_native_%s_%zu.png", fixture.name, i);
            snprintf(pdfs[i].path, sizeof(pdfs[i].path), "temp/render_output_parity/fo_native_%s_%zu.pdf", fixture.name, i);
            snprintf(pdfs[i].base, sizeof(pdfs[i].base), "fo_native_%s_%zu", fixture.name, i);
            ASSERT_TRUE(render_document_fixture(source, previews[i], "--paged --block-remote-resources --page-grid 1x3"));
            ASSERT_TRUE(render_document_fixture(source, pdfs[i].path, "--paged --block-remote-resources"));
            EXPECT_TRUE(file_contains_text(pdfs[i].path, fixture.media_box));
        }
        expect_paged_pair_output_parity(previews, pdfs, 2, fixture.pages);
    }
}

TEST(RenderOutputParity, PagedMissingCellsMatchExplicitEmptyCellsAcrossSpansAndContinuations) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    const char* stems[] = {"paged_missing_cells", "paged_empty_cell_reference"};
    for (bool automatic : {false, true}) {
        SCOPED_TRACE(automatic ? "automatic layout" : "fixed layout");
        char previews[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t variant = 0; variant < 2; variant++) {
            StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
            strbuf_append_format(html, "<!doctype html><style>@page{size:160px 150px;margin:10px}"
                "@page :left{size:140px 150px}html,body{margin:0;font:10px/12px Arial}"
                "table{table-layout:%s;width:100%%;border-spacing:4px 2px;background:magenta}"
                "caption{padding:0;text-align:left}col{width:20px;background:cyan}"
                "col:last-child{background:blue}td,th{padding:0;vertical-align:top;white-space:pre;orphans:1;widows:1}"
                "thead{background:#cce0ff}tfoot{background:#ddffdd}.cluster tr{height:20px}"
                ".cluster tr:first-child{background:lime}.cluster tr:last-child{background:yellow}"
                ".span{background:pink}.empty{background:magenta}</style><table><caption>Missing cells</caption>"
                "<colgroup><col><col><col><col></colgroup><thead><tr><th>H</th><th>H</th>%s</tr></thead>"
                "<tfoot><tr><td>F</td>%s</tr></tfoot><tbody class='cluster'>"
                "<tr><td>A</td><td rowspan='3' class='span'>S</td><td>B</td>%s</tr>"
                "<tr><td rowspan='2' class='span'>C</td>%s</tr><tr>%s</tr></tbody>"
                "<tbody><tr><td colspan='2'>A\nB\nC\nD\nE\nF\nG\nH\nI\nJ</td>%s</tr></tbody></table>",
                // an opaque table-colored reference reproduces the missing cells' suppressed background layers.
                automatic ? "auto" : "fixed", variant ? "<th class='empty'></th><th class='empty'></th>" : "",
                variant ? "<td class='empty'></td><td class='empty'></td><td class='empty'></td>" : "",
                variant ? "<td class='empty'></td>" : "", variant ? "<td class='empty'></td><td class='empty'></td>" : "",
                variant ? "<td class='empty'></td><td class='empty'></td>" : "",
                variant ? "<td class='empty'></td><td class='empty'></td>" : "");
            bool rendered = render_paged_parity_variant(stems[variant], html->str, previews[variant], &pdfs[variant]);
            strbuf_free(html); ASSERT_TRUE(rendered);
        }
        expect_paged_pair_output_parity(previews, pdfs, 2, 4);
    }
}

TEST(RenderOutputParity, PagedTableGroupBreaksAndAvoidanceMatchExplicitRowBoundaries) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); ASSERT_TRUE(command_exists("pdftoppm"));
    const char* stems[] = {"paged_group_breaks", "paged_group_row_reference"};
    for (bool automatic : {false, true}) {
        SCOPED_TRACE(automatic ? "automatic layout" : "fixed layout");
        char preview_paths[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t variant = 0; variant < 2; variant++) {
            char html_path[PATH_MAX];
            snprintf(html_path, sizeof(html_path), "temp/render_output_parity/%s.html", stems[variant]);
            snprintf(preview_paths[variant], sizeof(preview_paths[variant]), "temp/render_output_parity/%s.png", stems[variant]);
            snprintf(pdfs[variant].path, sizeof(pdfs[variant].path), "temp/render_output_parity/%s.pdf", stems[variant]);
            snprintf(pdfs[variant].base, sizeof(pdfs[variant].base), "%s", stems[variant]);
            StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
            strbuf_append_format(html, "<!doctype html><style>@page{size:120px 100px;margin:10px}"
                "html,body{margin:0;font:10px/12px Arial}div{height:18px}"
                "table{table-layout:%s;width:100%%;border-collapse:separate;border-spacing:4px 2px;background:magenta}"
                "td,th{vertical-align:top;padding:0;orphans:1;widows:1}thead{background:#cce0ff}tfoot{background:#ddffdd}"
                "tbody td:first-child{background:yellow}tbody td:last-child{background:red}", automatic ? "auto" : "fixed");
            strbuf_append_str(html, variant ?
                "#b tr:first-child{break-before:page}#b tr:last-child,#c tr:last-child{break-after:page}" :
                "#b{break-inside:avoid;break-after:page}#c{break-after:page}");
            strbuf_append_str(html, "</style><div>Prelude</div><table><thead><tr><th colspan='2'>Head</th><th>H</th></tr></thead>"
                "<tfoot><tr><td colspan='3'>Foot</td></tr></tfoot><tbody id='a'>"
                "<tr><td colspan='2'>A1</td><td>A</td></tr><tr><td colspan='2'>A2</td><td>A</td></tr></tbody><tbody id='b'>"
                "<tr><td colspan='2'>B1</td><td>B</td></tr><tr><td colspan='2'>B2</td><td>B</td></tr></tbody><tbody id='c'>"
                "<tr><td colspan='2'>C1</td><td>C</td></tr></tbody></table>\n  \t");
            bool rendered = render_html_fixture(html_path, pdfs[variant].path, html->str, "--paged --block-remote-resources");
            strbuf_free(html); ASSERT_TRUE(rendered); ASSERT_EQ(pdf_page_count(pdfs[variant].path), 3);
            ASSERT_TRUE(render_document_fixture(html_path, preview_paths[variant], "--paged --block-remote-resources --page-grid 1x3"));
        }
        expect_pngs_exactly_equal(preview_paths[1], preview_paths[0]);
        ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_paths[0], &preview));
        EXPECT_EQ(preview.width, 360); EXPECT_EQ(preview.height, 100);
        const int header_y[] = {35, 17, 17}, body_y[] = {49, 31, 31}, footer_y[] = {77, 59, 45};
        for (int page = 0; page < 3; page++) {
            expect_preview_pixel(preview, page * 120 + 65, header_y[page], 204, 224, 255);
            expect_preview_pixel(preview, page * 120 + 45, body_y[page], 255, 255, 0);
            expect_preview_pixel(preview, page * 120 + 65, footer_y[page], 221, 255, 221);
            char pngs[2][PATH_MAX];
            for (size_t variant = 0; variant < 2; variant++)
                ASSERT_TRUE(render_reference_page(&pdfs[variant], page + 1, pngs[variant], sizeof(pngs[variant])));
            expect_pngs_exactly_equal(pngs[1], pngs[0]);
            ImageData physical = {}; ASSERT_TRUE(load_png_rgba(pngs[0], &physical));
            EXPECT_EQ(physical.width, RENDER_WIDTH); EXPECT_EQ(physical.height, RENDER_WIDTH * 100 / 120);
            expect_preview_pixel(physical, 65 * RENDER_WIDTH / 120, header_y[page] * RENDER_WIDTH / 120, 204, 224, 255);
            expect_preview_pixel(physical, 45 * RENDER_WIDTH / 120, body_y[page] * RENDER_WIDTH / 120, 255, 255, 0);
            expect_preview_pixel(physical, 65 * RENDER_WIDTH / 120, footer_y[page] * RENDER_WIDTH / 120, 221, 255, 221);
            image_free(physical.pixels);
        }
        image_free(preview.pixels);
    }
}

static void check_paged_table_cell_alignment(bool split, bool baseline = false) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    ASSERT_TRUE(command_exists("pdftoppm"));
    for (bool automatic : {false, true}) for (bool clone : {false, true}) {
        if (!split && clone) continue;
        SCOPED_TRACE(automatic ? "automatic" : "fixed");
        SCOPED_TRACE(clone ? "clone" : "slice");
        char previews[2][PATH_MAX]; PdfFileInfo pdfs[2] = {};
        for (size_t variant = 0; variant < 2; variant++) {
            char html_path[PATH_MAX];
            snprintf(pdfs[variant].base, sizeof(pdfs[variant].base), "paged_cell_align_%s%s_%zu",
                baseline ? "baseline_" : "", split ? "split" : "rows", variant);
            snprintf(html_path, sizeof(html_path), "temp/render_output_parity/%s.html", pdfs[variant].base);
            snprintf(pdfs[variant].path, sizeof(pdfs[variant].path), "temp/render_output_parity/%s.pdf", pdfs[variant].base);
            snprintf(previews[variant], sizeof(previews[variant]), "temp/render_output_parity/%s.png", pdfs[variant].base);
            StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
            strbuf_append_format(html, "<!doctype html><style>@page{size:240px %upx;margin:10px}"
                "html,body,div{margin:0;font:10px/12px Arial}"
                "table{table-layout:%s;width:100%%;border-collapse:separate;border-spacing:%s}"
                "td,th{vertical-align:top;padding:2px;border:1px solid black;white-space:pre-wrap;orphans:1;widows:1;"
                "background:yellow;box-decoration-break:%s}.ink{height:12px;background:#0033cc}",
                split ? 100u : baseline ? 240u : 200u, automatic ? "auto" : "fixed", split ? "0" : "4px 2px", clone ? "clone" : "slice");
            if (baseline && !variant) strbuf_append_str(html, "td,th{margin:17px 23px 29px}");
            if (!split) {
                strbuf_append_str(html, baseline ? "thead tr,tfoot tr{height:42px}tbody tr{height:48px}" :
                    "thead tr{height:36px}tbody tr{height:48px}tfoot tr{height:24px}");
                if (baseline) strbuf_append_str(html, variant ?
                    ".first,.middle,.bottom{padding-top:14px}.middle{padding-bottom:14px}" :
                    "td,th{vertical-align:baseline}.middle{padding-bottom:14px}.bottom{padding-top:14px}");
                else strbuf_append_str(html, variant ?
                    "thead .middle{padding-top:11px}thead .bottom{padding-top:20px}"
                    "tbody .middle{padding-top:17px}tbody .bottom{padding-top:32px}"
                    "tfoot .middle{padding-top:5px}tfoot .bottom{padding-top:8px}" :
                    ".middle{vertical-align:middle}.bottom{vertical-align:bottom}");
                strbuf_append_str(html, "</style><table><thead><tr><th class='first' colspan='2'><div class='ink'>H</div></th>"
                    "<th class='middle'><div class='ink'>H</div></th><th class='bottom'><div class='ink'>H</div></th></tr></thead>"
                    "<tfoot><tr><td class='first' colspan='2'><div class='ink'>F</div></td><td class='middle'><div class='ink'>F</div></td>"
                    "<td class='bottom'><div class='ink'>F</div></td></tr></tfoot><tbody>");
                for (size_t i = 0; i < 3; i++) strbuf_append_str(html,
                    "<tr><td class='first' colspan='2'><div class='ink'>X</div></td><td class='middle'><div class='ink'>X</div></td>"
                    "<td class='bottom'><div class='ink'>X</div></td></tr>");
                strbuf_append_str(html, "</tbody></table>");
            } else if (baseline) {
                if (!variant) {
                    strbuf_append_str(html, "td{vertical-align:baseline}.middle{padding-top:14px}</style><table><tbody><tr>"
                        "<td>A\nB\nC\nD\nE\nF\nG\nH\nI\nJ</td><td class='middle' colspan='2'>K\nL\nM\nN\nO\nP\nQ\nR</td>"
                        "<td>Z</td></tr></tbody></table>");
                } else {
                    strbuf_append_format(html, "tr+tr{break-before:page}tr:first-child td{padding-top:14px}"
                        "tr+tr td{padding-top:%upx}", clone ? 14u : 0u);
                    if (!clone) strbuf_append_str(html,
                        "tr:first-child td{padding-bottom:0;border-bottom:0}tr+tr td{border-top:0}");
                    strbuf_append_str(html, "</style><table><tbody><tr><td>A\nB\nC\nD\nE</td>"
                        "<td colspan='2'>K\nL\nM\nN\nO</td><td>Z</td></tr><tr>"
                        "<td>F\nG\nH\nI\nJ</td><td colspan='2'>P\nQ\nR</td><td></td></tr></tbody></table>");
                }
            } else if (!variant) {
                strbuf_append_str(html, ".middle{vertical-align:middle}.bottom{vertical-align:bottom}</style><table><tbody><tr>"
                    "<td>A\nB\nC\nD\nE\nF\nG\nH\nI\nJ</td><td class='middle' colspan='2'>A\nB\nC\nD\nE\nF\nG\nH</td>"
                    "<td class='bottom'>Z</td></tr></tbody></table>");
            } else {
                strbuf_append_format(html, "tr+tr{break-before:page}tr:first-child .bottom{padding-top:62px}"
                    "tr+tr .middle{padding-top:%upx}", clone ? 14u : 12u);
                if (!clone) strbuf_append_str(html, "tr:first-child td{padding-bottom:0;border-bottom:0}tr+tr td{padding-top:0;border-top:0}"
                    "tr+tr .middle{padding-top:12px}");
                strbuf_append_str(html, "</style><table><tbody><tr><td>A\nB\nC\nD\nE\nF</td>"
                    "<td class='middle' colspan='2'>A\nB\nC\nD\nE\nF</td><td class='bottom'>Z</td></tr><tr>"
                    "<td>G\nH\nI\nJ</td><td class='middle' colspan='2'>G\nH</td><td></td></tr></tbody></table>");
            }
            bool rendered = render_html_fixture(html_path, pdfs[variant].path, html->str, "--paged --block-remote-resources");
            strbuf_free(html); ASSERT_TRUE(rendered); ASSERT_EQ(pdf_page_count(pdfs[variant].path), 2);
            ASSERT_TRUE(render_document_fixture(html_path, previews[variant], "--paged --block-remote-resources --page-grid 1x2"));
        }
        expect_pngs_exactly_equal(previews[1], previews[0]);
        if (!split) {
            ImageData preview = {}; ASSERT_TRUE(load_png_rgba(previews[0], &preview));
            EXPECT_EQ(preview.width, 480); EXPECT_EQ(preview.height, baseline ? 240 : 200);
            for (int page = 0; page < 2; page++) {
                if (baseline) {
                    expect_preview_pixel(preview, page * 240 + 140, 75, 0, 51, 204);
                    expect_preview_pixel(preview, page * 240 + 200, 75, 0, 51, 204);
                    expect_preview_pixel(preview, page * 240 + 140, 67, 255, 255, 0);
                } else {
                    expect_preview_pixel(preview, page * 240 + 140, 55, 255, 255, 0);
                    expect_preview_pixel(preview, page * 240 + 140, 70, 0, 51, 204);
                    expect_preview_pixel(preview, page * 240 + 200, 85, 0, 51, 204);
                }
            }
            image_free(preview.pixels);
        }
        for (int page = 1; page <= 2; page++) {
            char pngs[2][PATH_MAX];
            for (size_t variant = 0; variant < 2; variant++)
                ASSERT_TRUE(render_reference_page(&pdfs[variant], page, pngs[variant], sizeof(pngs[variant])));
            expect_pngs_exactly_equal(pngs[1], pngs[0]);
        }
    }
}

TEST(RenderOutputParity, PagedTableCellAlignmentMatchesExplicitPaddingInRepeatedRows) {
    check_paged_table_cell_alignment(false);
}

TEST(RenderOutputParity, PagedSplitCellAlignmentMatchesExplicitRowsWithSliceAndCloneEdges) {
    check_paged_table_cell_alignment(true);
}

TEST(RenderOutputParity, PagedTableBaselinesMatchExplicitPaddingInRepeatedRows) {
    check_paged_table_cell_alignment(false, true);
}

TEST(RenderOutputParity, PagedSplitCellBaselinesMatchIndependentExplicitRowSlices) {
    check_paged_table_cell_alignment(true, true);
}

static void check_paged_table_spacing(bool spanning) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = spanning ? "temp/render_output_parity/paged_table_colspan.html" : "temp/render_output_parity/paged_table_spacing.html";
    const char* pdf_path = spanning ? "temp/render_output_parity/paged_table_colspan.pdf" : "temp/render_output_parity/paged_table_spacing.pdf";
    const char* preview_path = spanning ? "temp/render_output_parity/paged_table_colspan.png" : "temp/render_output_parity/paged_table_spacing.png";
    for (const char* algorithm : {"fixed", "auto"}) {
        SCOPED_TRACE(algorithm); StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
        strbuf_append_format(html, "<!doctype html><style>@page{size:120px 100px;margin:10px}"
            "html,body{margin:0;font:10px/12px Arial}table{table-layout:%s;width:100%%;border-spacing:4px 3px;background:#ff00ff}"
            "td{vertical-align:top;padding:2px;border:1px solid black;orphans:1;widows:1}"
            "thead{background:#cce0ff}tfoot{background:#ddffdd}tbody tr{background:#ffff00}"
            "tbody td:last-child{background:red}.long{white-space:pre-wrap}</style>", algorithm);
        if (spanning) strbuf_append_str(html,
            "<table><thead><tr><td style='width:28px;box-sizing:border-box'>H</td>"
            "<td style='width:28px;box-sizing:border-box'>H</td><td style='width:28px;box-sizing:border-box'>H</td></tr></thead>"
            "<tfoot><tr><td colspan=3>Foot</td></tr></tfoot>"
            "<tbody><tr><td colspan=2 class=long>A\nB\nC\nD\nE\nF</td><td>X</td></tr></tbody></table>");
        else strbuf_append_str(html,
            "<table><thead><tr><td>Head</td><td>Head</td></tr></thead><tfoot><tr><td>Foot</td><td>Foot</td></tr></tfoot>"
            "<tbody><tr><td class=long>A\nB\nC\nD\nE\nF</td><td>Short</td></tr></tbody></table>");
        bool rendered = render_html_fixture(html_path, pdf_path, html->str, "--paged --block-remote-resources");
        strbuf_free(html); ASSERT_TRUE(rendered); ASSERT_EQ(pdf_page_count(pdf_path), 3);
        ASSERT_TRUE(render_document_fixture(html_path, preview_path,
            "--paged --block-remote-resources --page-grid 1x3"));
        ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_path, &preview));
        EXPECT_EQ(preview.width, 360); EXPECT_EQ(preview.height, 100);
        const int footer_y[] = {64, 61, 64};
        const struct { int x, y; uint8_t r, g, b; } points[] = {
            {12, 18, 255, 0, 255}, {spanning ? 44 : 60, 18, 255, 0, 255}, {50, 11, 255, 0, 255},
            {50, 32, 255, 0, 255}, {spanning ? 66 : 50, 18, 204, 224, 255},
            {50, 40, 255, 255, 0}, {100, 40, 255, 0, 0}};
        for (int page = 0; page < 3; page++) {
            if (spanning) {
                expect_preview_pixel(preview, page * 120 + 60, 40, 255, 255, 0);
                expect_preview_pixel(preview, page * 120 + 76, 40, 255, 0, 255);
                expect_preview_pixel(preview, page * 120 + 44, footer_y[page] + 6, 221, 255, 221);
            }
            for (const auto& point : points)
                expect_preview_pixel(preview, page * 120 + point.x, point.y, point.r, point.g, point.b);
            expect_preview_pixel(preview, page * 120 + 50, footer_y[page] + 6, 221, 255, 221);
            expect_preview_pixel(preview, page * 120 + 50, footer_y[page] - 2, 255, 0, 255);
            expect_preview_pixel(preview, page * 120 + 50, footer_y[page] + 19, 255, 0, 255);
            expect_preview_pixel(preview, page * 120 + 50, footer_y[page] + 23, 255, 255, 255);
        }
        image_free(preview.pixels);
        ASSERT_TRUE(command_exists("pdftoppm")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
        PdfFileInfo pdf = {}; snprintf(pdf.path, sizeof(pdf.path), "%s", pdf_path);
        snprintf(pdf.base, sizeof(pdf.base), "%s", spanning ? "paged_table_colspan" : "paged_table_spacing");
        for (int page = 0; page < 3; page++) {
            char png[PATH_MAX]; ASSERT_TRUE(render_reference_page(&pdf, page + 1, png, sizeof(png)));
            ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
            EXPECT_EQ(image.width, RENDER_WIDTH); EXPECT_EQ(image.height, RENDER_WIDTH * 100 / 120);
            if (spanning) {
                expect_preview_pixel(image, 60 * RENDER_WIDTH / 120, 40 * RENDER_WIDTH / 120, 255, 255, 0);
                expect_preview_pixel(image, 76 * RENDER_WIDTH / 120, 40 * RENDER_WIDTH / 120, 255, 0, 255);
                expect_preview_pixel(image, 44 * RENDER_WIDTH / 120, (footer_y[page] + 6) * RENDER_WIDTH / 120, 221, 255, 221);
            }
            for (const auto& point : points)
                expect_preview_pixel(image, point.x * RENDER_WIDTH / 120, point.y * RENDER_WIDTH / 120, point.r, point.g, point.b);
            expect_preview_pixel(image, 50 * RENDER_WIDTH / 120, (footer_y[page] + 6) * RENDER_WIDTH / 120, 221, 255, 221);
            expect_preview_pixel(image, 50 * RENDER_WIDTH / 120, (footer_y[page] - 2) * RENDER_WIDTH / 120, 255, 0, 255);
            expect_preview_pixel(image, 50 * RENDER_WIDTH / 120, (footer_y[page] + 19) * RENDER_WIDTH / 120, 255, 0, 255);
            expect_preview_pixel(image, 50 * RENDER_WIDTH / 120, (footer_y[page] + 23) * RENDER_WIDTH / 120, 255, 255, 255);
            image_free(image.pixels);
        }
    }
}

TEST(RenderOutputParity, PagedTableSpacingKeepsBackgroundGapsAcrossCellContinuations) {
    check_paged_table_spacing(false);
}

TEST(RenderOutputParity, PagedColumnSpansCoverInternalGapsAcrossPdfAndPreviewContinuations) {
    check_paged_table_spacing(true);
}

static void append_svg_image_fixture(StrBuf* html, const char* attributes,
        const char* color, const char* style = nullptr) {
    strbuf_append_str(html, "<img");
    if (style) { strbuf_append_str(html, " style='"); strbuf_append_str(html, style); strbuf_append_char(html, '\''); }
    strbuf_append_str(html, " src=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' ");
    strbuf_append_str(html, attributes);
    strbuf_append_str(html, "%3E%3Crect width='100%25' height='100%25' fill='");
    strbuf_append_str(html, color); strbuf_append_str(html, "'/%3E%3C/svg%3E\">");
}

TEST(RenderOutputParity, PagedImagesWrapIntoPhysicalSheetsAndScaledPreviewCells) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_atomic_images.html";
    const char* pdf_path = "temp/render_output_parity/paged_atomic_images.pdf";
    const char* preview_path = "temp/render_output_parity/paged_atomic_images.png";
    const char* colors[] = {"red", "lime", "blue"};
    const uint8_t expected[][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
    strbuf_append_str(html, "<!doctype html><style>@page{size:100px 60px;margin:10px}"
        "html,body{margin:0;font-size:10px;line-height:12px;orphans:1;widows:1}"
        "img{width:50px;height:20px;white-space:nowrap}</style><body>");
    for (const char* color : colors) append_svg_image_fixture(html, "width='50' height='20'", color);
    strbuf_append_str(html, "</body>");
    bool rendered = render_html_fixture(html_path, pdf_path, html->str, "--paged --block-remote-resources");
    strbuf_free(html); ASSERT_TRUE(rendered);
    ASSERT_EQ(pdf_page_count(pdf_path), 3);
    EXPECT_FALSE(file_contains_text(pdf_path, "/Subtype /Image"));
    ASSERT_TRUE(render_document_fixture(html_path, preview_path,
        "--paged --block-remote-resources --page-grid 1x3 --page-scale 0.5"));
    ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_path, &preview));
    EXPECT_EQ(preview.width, 150); EXPECT_EQ(preview.height, 30);
    for (int page = 0; page < 3; page++)
        expect_preview_pixel(preview, page * 50 + 12, 8, expected[page][0], expected[page][1], expected[page][2]);
    image_free(preview.pixels);
    if (!command_exists("pdftoppm")) GTEST_SKIP() << "Poppler is needed to inspect atomic image page order";
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    PdfFileInfo pdf = {};
    snprintf(pdf.path, sizeof(pdf.path), "%s", pdf_path);
    snprintf(pdf.base, sizeof(pdf.base), "paged_atomic_images");
    for (int page = 0; page < 3; page++) {
        char png[PATH_MAX]; ASSERT_TRUE(render_reference_page(&pdf, page + 1, png, sizeof(png)));
        ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
        expect_preview_pixel(image, 80, 80, expected[page][0], expected[page][1], expected[page][2]);
        image_free(image.pixels);
    }
}

TEST(RenderOutputParity, PagedPercentageHeightsPreserveAutoAndDefiniteContainingBlocks) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_height_percentages.html";
    const char* pdf_path = "temp/render_output_parity/paged_height_percentages.pdf";
    const char* preview_path = "temp/render_output_parity/paged_height_percentages.png";
    const char* html = "<!doctype html><style>@page{size:120px 100px;margin:10px}"
        "html,body,div{margin:0;padding:0} .auto .child,.fixed .child{height:50%;background:blue}"
        ".ink{height:10px;width:10px;background:green}.after{height:5px;background:red}"
        ".fixed{height:40px;break-before:page}</style>"
        "<div class='auto'><div class='child'><div class='ink'></div></div><div class='after'></div></div>"
        "<div class='fixed'><div class='child'><div class='ink'></div></div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html, "--paged --block-remote-resources"));
    ASSERT_EQ(pdf_page_count(pdf_path), 2);
    ASSERT_TRUE(render_document_fixture(html_path, preview_path, "--paged --block-remote-resources --page-grid 1x2"));
    ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_path, &preview));
    EXPECT_EQ(preview.width, 240); EXPECT_EQ(preview.height, 100);
    expect_preview_pixel(preview, 30, 15, 0, 0, 255);
    expect_preview_pixel(preview, 30, 22, 255, 0, 0);
    expect_preview_pixel(preview, 30, 28, 255, 255, 255);
    expect_preview_pixel(preview, 150, 25, 0, 0, 255);
    expect_preview_pixel(preview, 150, 35, 255, 255, 255); image_free(preview.pixels);
    ASSERT_TRUE(command_exists("pdftoppm")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    PdfFileInfo pdf = {}; snprintf(pdf.path, sizeof(pdf.path), "%s", pdf_path);
    snprintf(pdf.base, sizeof(pdf.base), "paged_height_percentages");
    for (int page = 1; page <= 2; page++) {
        char png[PATH_MAX]; ASSERT_TRUE(render_reference_page(&pdf, page, png, sizeof(png)));
        ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
        expect_preview_pixel(image, image.width / 4, image.width * (page == 1 ? 15 : 25) / 120, 0, 0, 255);
        if (page == 1) expect_preview_pixel(image, image.width / 4, image.width * 22 / 120, 255, 0, 0);
        expect_preview_pixel(image, image.width / 4, image.width * 35 / 120, 255, 255, 255); image_free(image.pixels);
    }
}

TEST(RenderOutputParity, ResponsivePicturePrintSelectionRetainsDensityAndPhysicalPageGeometry) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_responsive.html";
    const char* pdf_path = "temp/render_output_parity/paged_responsive.pdf";
    const char* preview_path = "temp/render_output_parity/paged_responsive.png";
    const char* red = "data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='40'%20height='20'%3E%3Crect%20width='40'%20height='20'%20fill='red'/%3E%3C/svg%3E";
    const char* blue = "data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'%20width='40'%20height='20'%3E%3Crect%20width='40'%20height='20'%20fill='blue'/%3E%3C/svg%3E";
    StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
    strbuf_append_str(html, "<!doctype html><style>@page{size:100px 60px;margin:10px}"
        "html,body{margin:0} picture{display:block} picture+picture{break-before:page} img{display:block}</style>");
    for (const char* url : {red, blue}) {
        strbuf_append_str(html, "<picture><source type='image/avif' srcset='missing.avif'>"
            "<source media='print' type='image/svg+xml' srcset=\"");
        strbuf_append_str(html, url); strbuf_append_str(html, " 2x\"><img src='missing-fallback.png'></picture>");
    }
    bool rendered = render_html_fixture(html_path, pdf_path, html->str, "--paged --block-remote-resources");
    strbuf_free(html); ASSERT_TRUE(rendered); ASSERT_EQ(pdf_page_count(pdf_path), 2);
    EXPECT_FALSE(file_contains_text(pdf_path, "/Subtype /Image"));
    ASSERT_TRUE(render_document_fixture(html_path, preview_path, "--paged --block-remote-resources --page-grid 1x2"));
    ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_path, &preview));
    EXPECT_EQ(preview.width, 200); EXPECT_EQ(preview.height, 60);
    expect_preview_pixel(preview, 15, 15, 255, 0, 0); expect_preview_pixel(preview, 115, 15, 0, 0, 255);
    expect_preview_pixel(preview, 35, 15, 255, 255, 255); // natural width is 40 / 2 CSS pixels.
    image_free(preview.pixels);
    ASSERT_TRUE(command_exists("pdftoppm")); ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    PdfFileInfo pdf = {}; snprintf(pdf.path, sizeof(pdf.path), "%s", pdf_path);
    snprintf(pdf.base, sizeof(pdf.base), "paged_responsive");
    for (int page = 1; page <= 2; page++) {
        char png[PATH_MAX]; ASSERT_TRUE(render_reference_page(&pdf, page, png, sizeof(png)));
        ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
        expect_preview_pixel(image, image.width * 15 / 100, image.width * 15 / 100, page == 1 ? 255 : 0, 0, page == 2 ? 255 : 0);
        expect_preview_pixel(image, image.width * 35 / 100, image.width * 15 / 100, 255, 255, 255); image_free(image.pixels);
    }
}

TEST(RenderOutputParity, SvgNaturalAxesProduceFractionalPagedPdfAndGridGeometry) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_svg_axes.html";
    const char* pdf_path = "temp/render_output_parity/paged_svg_axes.pdf";
    const char* preview_path = "temp/render_output_parity/paged_svg_axes.png";
    const char* roots[] = {"width='25.5'", "height='18.25'", "width='16.25' height='8.125'",
        "width='25.5' viewBox='0 0 30.75 10.25'", "width='25.5'", "height='18.25'"};
    const char* fills[] = {"red", "blue", "lime", "red", "red", "blue"};
    const char* styles[] = {nullptr, nullptr, nullptr, nullptr, "width:51px", "height:36.5px"};
    StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
    strbuf_append_str(html, "<!doctype html><style>@page{size:360px 240px;margin:10px}html,body{margin:0}"
        "img{display:block;break-before:page}</style><body>");
    for (size_t i = 0; i < 6; i++) append_svg_image_fixture(html, roots[i], fills[i], styles[i]);
    strbuf_append_str(html, "</body>");
    bool rendered = render_html_fixture(html_path, pdf_path, html->str, "--paged --block-remote-resources");
    strbuf_free(html); ASSERT_TRUE(rendered);
    ASSERT_EQ(pdf_page_count(pdf_path), 6); EXPECT_FALSE(file_contains_text(pdf_path, "/Subtype /Image"));
    ASSERT_TRUE(render_document_fixture(html_path, preview_path, "--paged --block-remote-resources --page-grid 2x3"));
    ImageData preview = {}; ASSERT_TRUE(load_png_rgba(preview_path, &preview));
    EXPECT_EQ(preview.width, 1080); EXPECT_EQ(preview.height, 480);
    expect_preview_pixel(preview, 34, 150, 255, 0, 0); expect_preview_pixel(preview, 37, 150, 255, 255, 255);
    expect_preview_pixel(preview, 660, 26, 0, 0, 255); expect_preview_pixel(preview, 660, 30, 255, 255, 255);
    expect_preview_pixel(preview, 744, 14, 0, 255, 0); expect_preview_pixel(preview, 748, 14, 255, 255, 255);
    expect_preview_pixel(preview, 34, 255, 255, 0, 0); expect_preview_pixel(preview, 37, 255, 255, 255, 255);
    expect_preview_pixel(preview, 418, 390, 255, 0, 0); expect_preview_pixel(preview, 424, 390, 255, 255, 255);
    expect_preview_pixel(preview, 1000, 284, 0, 0, 255); expect_preview_pixel(preview, 1000, 290, 255, 255, 255);
    image_free(preview.pixels);
    const char* default_path = "temp/render_output_parity/paged_svg_axes_default.png";
    ASSERT_TRUE(render_document_fixture(html_path, default_path, "--viewport-width 360 --viewport-height 400"));
    ImageData browsing = {}; ASSERT_TRUE(load_png_rgba(default_path, &browsing));
    EXPECT_EQ(browsing.width, 360); EXPECT_EQ(browsing.height, 400);
    expect_preview_pixel(browsing, 49, 200, 255, 0, 0); expect_preview_pixel(browsing, 55, 200, 255, 255, 255);
    expect_preview_pixel(browsing, 280, 350, 0, 0, 255); expect_preview_pixel(browsing, 280, 375, 255, 255, 255);
    image_free(browsing.pixels);
    if (!command_exists("pdftoppm")) GTEST_SKIP() << "Poppler is needed to inspect SVG natural-axis physical pages";
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR)); PdfFileInfo pdf = {};
    snprintf(pdf.path, sizeof(pdf.path), "%s", pdf_path); snprintf(pdf.base, sizeof(pdf.base), "paged_svg_axes");
    const int inside[][2] = {{34, 150}, {300, 26}, {24, 14}, {34, 15}, {58, 150}, {280, 44}};
    const int outside[][2] = {{37, 150}, {300, 30}, {28, 14}, {37, 15}, {64, 150}, {280, 50}};
    const uint8_t colors[][3] = {{255, 0, 0}, {0, 0, 255}, {0, 255, 0}, {255, 0, 0}, {255, 0, 0}, {0, 0, 255}};
    for (int page = 0; page < 6; page++) {
        char png[PATH_MAX]; ASSERT_TRUE(render_reference_page(&pdf, page + 1, png, sizeof(png)));
        ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
        float density = image.width / 360.0f; // the shared Poppler helper fits physical pages to RENDER_WIDTH.
        expect_preview_pixel(image, (int)(inside[page][0] * density), (int)(inside[page][1] * density),
            colors[page][0], colors[page][1], colors[page][2]);
        expect_preview_pixel(image, (int)(outside[page][0] * density), (int)(outside[page][1] * density), 255, 255, 255);
        image_free(image.pixels);
    }
}

TEST(RenderOutputParity, SvgImagesRetainMissingAndFractionalAxesInDefaultGridAndFlex) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/svg_intrinsic_containers.html";
    const char* png_path = "temp/render_output_parity/svg_intrinsic_containers.png";
    const char* roots[] = {"width='25.5'", "width='25.5' viewBox='0 0 30.75 10.25'",
        "height='18.25'", "viewBox='0 0 30.75 10.25'"};
    StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
    strbuf_append_str(html, "<!doctype html><style>html,body{margin:0}"
        ".grid{display:grid;grid-template-columns:51px;grid-template-rows:max-content;align-items:start;width:51px}"
        ".flex{display:flex;flex-direction:column;align-items:start;width:51px}img{display:block;width:51px}</style><body>");
    for (size_t i = 0; i < 4; i++) for (size_t kind = 0; kind < 2; kind++) {
        strbuf_append_str(html, kind ? "<div class='flex'>" : "<div class='grid'>");
        append_svg_image_fixture(html, roots[i], kind ? "blue" : "red"); strbuf_append_str(html, "</div>");
    }
    strbuf_append_str(html, "</body>");
    bool rendered = render_html_fixture(html_path, png_path, html->str, "--viewport-width 80 --viewport-height 430");
    strbuf_free(html); ASSERT_TRUE(rendered);
    ImageData image = {}; ASSERT_TRUE(load_png_rgba(png_path, &image));
    EXPECT_EQ(image.width, 80); EXPECT_EQ(image.height, 430);
    const int centers[] = {100, 250, 308, 326, 342, 360, 378, 396};
    for (size_t i = 0; i < 8; i++) {
        SCOPED_TRACE(i);
        expect_preview_pixel(image, 10, centers[i], i % 2 ? 0 : 255, 0, i % 2 ? 255 : 0);
        expect_preview_pixel(image, 55, centers[i], 255, 255, 255);
    }
    expect_preview_pixel(image, 10, 410, 255, 255, 255); image_free(image.pixels);
}

TEST(RenderOutputParity, PagedPreviewFilesArrangeGridsAndApplyDensityOnce) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_preview.html";
    const char* png_path = "temp/render_output_parity/paged_preview_grid.png";
    const char* options = "--paged --page-grid 2x2 --page-padding 8 --page-column-gap 12 --page-row-gap 10 --page-group-gap 14";
    ASSERT_TRUE(render_html_fixture(html_path, png_path, paged_preview_fixture(), options));
    ImageData image = {}; ASSERT_TRUE(load_png_rgba(png_path, &image));
    EXPECT_EQ(image.width, 268); EXPECT_EQ(image.height, 520);
    expect_preview_pixel(image, 40, 70, 255, 0, 0); expect_preview_pixel(image, 170, 70, 0, 255, 0);
    expect_preview_pixel(image, 40, 240, 0, 0, 255); expect_preview_pixel(image, 170, 240, 255, 128, 0);
    expect_preview_pixel(image, 40, 402, 128, 0, 255); image_free(image.pixels);
    const char* dense = "temp/render_output_parity/paged_preview_grid_dense.png";
    const char* device = "temp/render_output_parity/paged_preview_grid_device.png";
    char arguments[512]; snprintf(arguments, sizeof(arguments), "%s --scale 2", options);
    ASSERT_TRUE(render_document_fixture(html_path, dense, arguments));
    snprintf(arguments, sizeof(arguments), "%s --pixel-ratio 2", options);
    ASSERT_TRUE(render_document_fixture(html_path, device, arguments)); expect_pngs_exactly_equal(dense, device);
    ASSERT_TRUE(load_png_rgba(dense, &image)); EXPECT_EQ(image.width, 536); EXPECT_EQ(image.height, 1040); image_free(image.pixels);
    const char* svg = "temp/render_output_parity/paged_preview_grid.svg";
    ASSERT_TRUE(render_document_fixture(html_path, svg, options));
    EXPECT_TRUE(file_contains_text(svg, "viewBox=\"0 0 268 520\""));
    EXPECT_TRUE(file_contains_text(svg, "<path")); EXPECT_FALSE(file_contains_text(svg, "<image"));
    if (command_exists("rsvg-convert")) {
        const char* independent = "temp/render_output_parity/paged_preview_grid_rsvg.png";
        CommandResult rendered = run_command_capture("rsvg-convert temp/render_output_parity/paged_preview_grid.svg -o temp/render_output_parity/paged_preview_grid_rsvg.png");
        ASSERT_EQ(rendered.exit_code, 0) << rendered.output;
        ASSERT_TRUE(load_png_rgba(independent, &image)); EXPECT_EQ(image.width, 268); EXPECT_EQ(image.height, 520);
        expect_preview_pixel(image, 40, 70, 255, 0, 0); expect_preview_pixel(image, 170, 240, 255, 128, 0);
        expect_preview_pixel(image, 40, 402, 128, 0, 255); image_free(image.pixels);
    }
}

TEST(RenderOutputParity, PagedBookPreviewAndPhysicalThumbnailKeepOriginalPages) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_preview_book.html";
    const char* book = "temp/render_output_parity/paged_preview_book.png";
    ASSERT_TRUE(render_html_fixture(html_path, book, paged_preview_fixture(),
        "--paged --book --book-page 2 --pages 1,3 --page-padding 8 --page-column-gap 12"));
    ImageData image = {}; ASSERT_TRUE(load_png_rgba(book, &image));
    EXPECT_EQ(image.width, 268); EXPECT_EQ(image.height, 176);
    expect_preview_pixel(image, 40, 70, 232, 232, 232); expect_preview_pixel(image, 170, 70, 0, 0, 255); image_free(image.pixels);
    const char* thumbnail = "temp/render_output_parity/paged_preview_thumbnail.png";
    ASSERT_TRUE(render_document_fixture(html_path, thumbnail,
        "--paged --book --book-page 1 --pages 1 --page-scale .25 --page-padding 40 --thumbnail-page 5 --scale .5"));
    ASSERT_TRUE(load_png_rgba(thumbnail, &image)); EXPECT_EQ(image.width, 60); EXPECT_EQ(image.height, 80);
    expect_preview_pixel(image, 15, 30, 128, 0, 255); image_free(image.pixels);
    const char* jpeg = "temp/render_output_parity/paged_preview_thumbnail.jpg";
    ASSERT_TRUE(render_document_fixture(html_path, jpeg, "--paged --thumbnail-page 5 --scale .5"));
    int channels = 0; image.pixels = image_load(jpeg, &image.width, &image.height, &channels, 4);
    ASSERT_NE(image.pixels, nullptr); EXPECT_EQ(image.width, 60); EXPECT_EQ(image.height, 80);
    const uint8_t* pixel = image.pixels + ((size_t)30 * image.width + 15) * 4;
    EXPECT_NEAR(pixel[0], 128, 5); EXPECT_LT(pixel[1], 5); EXPECT_GT(pixel[2], 250); image_free(image.pixels);
    const char* svg = "temp/render_output_parity/paged_preview_thumbnail.svg";
    ASSERT_TRUE(render_document_fixture(html_path, svg,
        "--paged --thumbnail-page 5 --scale .5 --pages 1 --book-page 1 --page-scale .25"));
    EXPECT_TRUE(file_contains_text(svg, "width=\"60\" height=\"80\" viewBox=\"0 0 120 160\""));
    if (command_exists("rsvg-convert")) {
        CommandResult rendered = run_command_capture("rsvg-convert temp/render_output_parity/paged_preview_thumbnail.svg -o temp/render_output_parity/paged_preview_thumbnail_rsvg.png");
        ASSERT_EQ(rendered.exit_code, 0) << rendered.output;
        ASSERT_TRUE(load_png_rgba("temp/render_output_parity/paged_preview_thumbnail_rsvg.png", &image));
        EXPECT_EQ(image.width, 60); EXPECT_EQ(image.height, 80); expect_preview_pixel(image, 15, 30, 128, 0, 255); image_free(image.pixels);
    }
}

TEST(RenderOutputParity, PagedPreviewColumnFillHorizontalGroupsAndRightBinding) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html = "temp/render_output_parity/paged_preview_alternate.html";
    const char* png = "temp/render_output_parity/paged_preview_horizontal.png";
    ASSERT_TRUE(render_html_fixture(html, png, paged_preview_fixture(),
        "--paged --page-grid 2x2 --page-fill column --page-groups horizontal --page-scale .5 --page-padding 8 --page-column-gap 6 --page-row-gap 5 --page-group-gap 14"));
    ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image));
    EXPECT_EQ(image.width, 216); EXPECT_EQ(image.height, 181);
    expect_preview_pixel(image, 40, 40, 255, 0, 0); expect_preview_pixel(image, 40, 115, 0, 255, 0);
    expect_preview_pixel(image, 100, 40, 0, 0, 255); expect_preview_pixel(image, 100, 115, 255, 128, 0);
    expect_preview_pixel(image, 170, 40, 128, 0, 255); image_free(image.pixels);
    const char* book = "temp/render_output_parity/paged_preview_right_binding.png";
    ASSERT_TRUE(render_document_fixture(html, book, "--paged --book-page 1 --right-binding --page-padding 8 --page-column-gap 12"));
    ASSERT_TRUE(load_png_rgba(book, &image)); EXPECT_EQ(image.width, 268); EXPECT_EQ(image.height, 176);
    expect_preview_pixel(image, 40, 70, 0, 255, 0); expect_preview_pixel(image, 170, 70, 255, 0, 0); image_free(image.pixels);
}

TEST(RenderOutputParity, PagedPdfSelectionIgnoresPreviewFilteringAndGeometry) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/paged_preview_export.html";
    const char* pdf = "temp/render_output_parity/paged_preview_export_all.pdf";
    ASSERT_TRUE(render_html_fixture(html_path, pdf, paged_preview_fixture(),
        "--paged --pages 1 --book --book-page 1 --page-scale .25 --page-padding 99 --scale 2"));
    EXPECT_EQ(pdf_page_count(pdf), 5);
    const char* selected = "temp/render_output_parity/paged_preview_export_selected.pdf";
    ASSERT_TRUE(render_document_fixture(html_path, selected,
        "--paged --pages 1 --page-grid 2x2 --export-pages 4-5,2,4 --page-scale .25"));
    EXPECT_EQ(pdf_page_count(selected), 3);
    CommandResult info = pdf_info(selected); ASSERT_EQ(info.exit_code, 0) << info.output;
    const char* size = strstr(info.output, "Page size:"); ASSERT_NE(size, nullptr);
    double width = 0.0, height = 0.0; ASSERT_EQ(sscanf(size, "Page size: %lf x %lf", &width, &height), 2);
    EXPECT_NEAR(width, 90.0, .01); EXPECT_NEAR(height, 120.0, .01);
    if (command_exists("pdftoppm")) {
        PdfFileInfo rendered = {}; snprintf(rendered.path, sizeof(rendered.path), "%s", selected);
        snprintf(rendered.base, sizeof(rendered.base), "paged_selected");
        ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
        const uint8_t colors[][3] = {{0, 255, 0}, {255, 128, 0}, {128, 0, 255}};
        for (int page = 1; page <= 3; page++) {
            char png_path[PATH_MAX]; ASSERT_TRUE(render_reference_page(&rendered, page, png_path, sizeof(png_path)));
            ImageData image = {}; ASSERT_TRUE(load_png_rgba(png_path, &image));
            // overlapping selections remain deduplicated in physical order; sample inside each colored body box.
            expect_preview_pixel(image, image.width / 3, image.height * 7 / 16,
                colors[page - 1][0], colors[page - 1][1], colors[page - 1][2]);
            image_free(image.pixels);
        }
    }
}

TEST(RenderOutputParity, PagedPreviewRejectsInvalidOptionsWithoutWritingAnOutput) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html = "temp/render_output_parity/paged_invalid_options.html";
    ASSERT_TRUE(write_file_all(html, paged_preview_fixture(), strlen(paged_preview_fixture())));
    const struct { const char* options; const char* extension; const char* reason; } invalid[] = {
        {"--pages 1", "png", "require --paged"}, {"--paged --pages 1,", "png", "expected all"},
        {"--paged --page-grid 2x0", "png", "expected positive rows"}, {"--paged --page-scale nan", "png", "finite"},
        {"--paged --scale nan", "png", "finite"}, {"--paged --pixel-ratio 1junk", "png", "finite"},
        {"--paged --pages 6", "png", "invalid page selection"}, {"--paged --book-page 6", "svg", "invalid page selection"},
        {"--paged --thumbnail-page 6", "png", "thumbnail page"}, {"--paged --thumbnail-page 1", "pdf", "requires PNG"},
        {"--paged --export-pages 1", "svg", "requires PDF"}, {"--paged --export-pages 6", "pdf", "encoding failed"},
        {"--paged --page-padding 1e30", "png", "too large"}, {"--paged --pages", "png", "expected all"}};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        SCOPED_TRACE(invalid[i].options);
        char output[PATH_MAX]; snprintf(output, sizeof(output), "temp/render_output_parity/paged_invalid_option_%zu.%s", i, invalid[i].extension);
        remove(output); EXPECT_FALSE(render_document_fixture(html, output, invalid[i].options)); EXPECT_FALSE(file_exists(output));
        char errors[PATH_MAX + 8]; snprintf(errors, sizeof(errors), "%s.err", output);
        char messages[PATH_MAX + 8]; snprintf(messages, sizeof(messages), "%s.out", output);
        EXPECT_TRUE(file_contains_text(errors, invalid[i].reason) || file_contains_text(messages, invalid[i].reason));
    }
}

TEST(RenderOutputParity, PagedLambdaPreviewUsesTheSharedFileExportPath) {
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html = "temp/render_output_parity/paged_lambda_input.html";
    ASSERT_TRUE(write_file_all(html, paged_preview_fixture(), strlen(paged_preview_fixture())));
    const char* script = "temp/render_output_parity/paged_lambda_input.ls";
    // the HTML parser returns #document with a doctype followed by the HTML element expected by the loader.
    const char* source = "(input(\"temp/render_output_parity/paged_lambda_input.html\", 'html')^)[1]";
    ASSERT_TRUE(write_file_all(script, source, strlen(source)));
    const char* png = "temp/render_output_parity/paged_lambda_preview.png";
    ASSERT_TRUE(render_document_fixture(script, png, "--paged --pages 2,5 --page-grid 1x2 --page-padding 8 --page-column-gap 12"));
    ImageData image = {}; ASSERT_TRUE(load_png_rgba(png, &image)); EXPECT_EQ(image.width, 268); EXPECT_EQ(image.height, 176);
    expect_preview_pixel(image, 40, 70, 0, 255, 0); expect_preview_pixel(image, 170, 70, 128, 0, 255); image_free(image.pixels);
    const char* pdf = "temp/render_output_parity/paged_lambda_selected.pdf";
    ASSERT_TRUE(render_document_fixture(script, pdf, "--paged --pages 2,5 --export-pages 2,5")); EXPECT_EQ(pdf_page_count(pdf), 2);
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

TEST(RenderOutputParity, PdfInlineSvgUsesVectorPaths) {
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
    EXPECT_FALSE(file_contains_text(pdf_path, "/Subtype /Image"));
    EXPECT_FALSE(file_contains_text(pdf_path, "BI\n/W"));
    if (!command_exists("pdftoppm")) GTEST_SKIP() << "Poppler is needed to inspect vector PDF output";
    ASSERT_TRUE(ensure_dir("temp/pdf_visual"));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    PdfFileInfo rendered = {};
    snprintf(rendered.path, sizeof(rendered.path), "%s", pdf_path);
    snprintf(rendered.base, sizeof(rendered.base), "inline_svg_vector");
    char png_path[PATH_MAX];
    ASSERT_TRUE(render_reference_page(&rendered, 1, png_path, sizeof(png_path)));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    size_t blue = 0, gold = 0;
    for (size_t i = 0; i < (size_t)image.width * image.height; i++) {
        const unsigned char* pixel = image.pixels + i * 4;
        if (pixel[2] > 180 && pixel[0] < 80 && pixel[1] < 140) blue++;
        if (pixel[0] > 235 && pixel[1] > 220 && pixel[2] > 170 && pixel[2] < 220) gold++;
    }
    image_free(image.pixels);
    EXPECT_GT(blue, 100u);
    EXPECT_GT(gold, 100u);
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
    // Semantic SVG primitives share the vector path lowering used by PDF.
    EXPECT_TRUE(file_contains_text(svg_path, "<path d=\"M30.00,15.00 C"));
    EXPECT_TRUE(file_contains_text(svg_path, "fill=\"rgb(22,163,74)\""))
        << "the vector path should carry inherited currentColor";
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
    // Missing alpha is zero at use time, exposing the white page (CSS Color 4 §4.4).
    for (int channel = 0; channel < 3; channel++) EXPECT_GT(missing[channel], 245);
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

TEST(RenderOutputParity, InsetShadowUsesConstrainedCornerRadii) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    struct RadiusCase { const char* name; const char* authored; const char* used; };
    const RadiusCase cases[] = {
        {"pill", "50px", "20px"},
        {"unequal", "50px 25px 75px 50px", "20px 10px 30px 20px"}
    };
    // CSS overlap reduction makes these authored and used radii paint identically.
    for (const RadiusCase& c : cases) {
        for (int scale = 1; scale <= 2; scale++) {
            SCOPED_TRACE(c.name);
            SCOPED_TRACE(scale);
            char png_paths[2][PATH_MAX];
            const char* radii[] = {c.authored, c.used};
            for (size_t i = 0; i < 2; i++) {
                char html_path[PATH_MAX], html[1024], options[96];
                snprintf(html_path, sizeof(html_path),
                    "temp/render_output_parity/inset_radius_%s_%d_%zu.html", c.name, scale, i);
                snprintf(png_paths[i], sizeof(png_paths[i]),
                    "temp/render_output_parity/inset_radius_%s_%d_%zu.png", c.name, scale, i);
                snprintf(html, sizeof(html),
                    "<!doctype html><style>html,body{margin:0;background:#f5f5f7}"
                    "div{margin:8px;width:100px;height:40px;background:#f37329;"
                    "border-radius:%s;box-shadow:inset 0 1px 1px rgba(255,255,255,.41)}"
                    "</style><div></div>", radii[i]);
                snprintf(options, sizeof(options), "-vw 128 -vh 64 --pixel-ratio %d", scale);
                ASSERT_TRUE(render_html_fixture(html_path, png_paths[i], html, options));
            }
            expect_pngs_exactly_equal(png_paths[1], png_paths[0]);
        }
    }
}

TEST(RenderOutputParity, ClippedInsetShadowMatchesFullViewportPixels) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/inset_viewport.html";
    const char* full_path = "temp/render_output_parity/inset_viewport_full.png";
    const char* clipped_path = "temp/render_output_parity/inset_viewport_clipped.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0;width:128px;height:64px;overflow:hidden}"
        "div{margin:8px;width:100px;height:40px;border-radius:50px;"
        "background:linear-gradient(90deg,red,blue);"
        "box-shadow:inset 3px 5px 15px rgba(0,0,0,.6)}</style><div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, full_path, html, "-vw 128 -vh 64"));
    ASSERT_TRUE(render_document_fixture(html_path, clipped_path, "-vw 64 -vh 32"));
    ImageData full = {}, clipped = {};
    ASSERT_TRUE(load_png_rgba(full_path, &full));
    ASSERT_TRUE(load_png_rgba(clipped_path, &clipped));
    ASSERT_EQ(full.width, 128); ASSERT_EQ(full.height, 64);
    ASSERT_EQ(clipped.width, 64); ASSERT_EQ(clipped.height, 32);
    for (int row = 0; row < clipped.height; row++) {
        EXPECT_EQ(memcmp(full.pixels + (size_t)row * (size_t)full.width * 4,
            clipped.pixels + (size_t)row * (size_t)clipped.width * 4,
            (size_t)clipped.width * 4), 0) << "clipped shadow differs on row " << row;
    }
    image_free(full.pixels); image_free(clipped.pixels);
}

TEST(RenderOutputParity, TransparentInsetShadowPreservesBackground) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/inset_background.html";
    const char* actual = "temp/render_output_parity/inset_background.png";
    const char* reference = "temp/render_output_parity/inset_background_reference.png";
    const char* html =
        "<!doctype html><style>html,body{margin:0}div{width:100px;height:40px;"
        "background:linear-gradient(90deg,red 50%,blue 50%);"
        "box-shadow:inset 0 0 15px transparent}</style><div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, actual, html));
    const char* no_shadow =
        "<!doctype html><style>html,body{margin:0}div{width:100px;height:40px;"
        "background:linear-gradient(90deg,red 50%,blue 50%)}"
        "</style><div></div>";
    ASSERT_TRUE(render_html_fixture(html_path, reference, no_shadow));
    expect_pngs_exactly_equal(reference, actual);
}

TEST(RenderOutputParity, CornerMathUsesFinalBoxAndComputedInheritedLengths) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    struct RadiusCase { const char* name; const char* actual; const char* reference; };
    const RadiusCase cases[] = {
        {"corner_math_logical", "border-start-start-radius:calc(10px + 20%)", "border-start-start-radius:30px 26px"},
        {"corner_math_quad", "border-radius:calc(10px + 20%) / calc(5px + 30%)", "border-radius:30px / 29px"},
        {"corner_math_min", "border-radius:min(60%,2em)", "border-radius:30px"},
        {"corner_math_clamp", "border-start-end-radius:clamp(5px,40%,35px)", "border-start-end-radius:35px 32px"},
        {"corner_math_negative", "border-end-end-radius:calc(-10px)", "border-end-end-radius:0"},
        {"corner_math_overlap", "border-radius:80px;border-start-start-radius:calc(50% + 30px)", "border-radius:80px;border-top-left-radius:80px 70px"},
        {"corner_math_outline", "border-start-start-radius:calc(10px + 20%);outline:2px solid red", "border-start-start-radius:30px 26px;outline:2px solid red"},
        {"corner_math_shadow", "border-start-start-radius:calc(10px + 20%);box-shadow:3px 3px 1px red", "border-start-start-radius:30px 26px;box-shadow:3px 3px 1px red"},
        {"corner_math_linear_shadow", "border-radius:calc(10px + 20%);background:linear-gradient(red,blue);box-shadow:3px 3px 1px red", "border-radius:30px 30px 30px 30px / 26px 26px 26px 26px;background:linear-gradient(red,blue);box-shadow:3px 3px 1px red"},
        {"corner_math_radial_shadow", "border-radius:calc(10px + 20%);background:radial-gradient(red,blue);box-shadow:3px 3px 1px red", "border-radius:30px 30px 30px 30px / 26px 26px 26px 26px;background:radial-gradient(red,blue);box-shadow:3px 3px 1px red"},
        {"corner_math_rtl", "direction:rtl;border-start-start-radius:calc(10px + 20%)", "direction:rtl;border-top-right-radius:30px 26px"}
    };
    for (const RadiusCase& test : cases) {
        SCOPED_TRACE(test.name);
        char actual[1024], reference[1024];
        const char* pattern = "<!doctype html><style>html,body{margin:0;background:white}"
            "#box{width:100px;height:80px;background:green;font-size:15px;%s}"
            "</style><div id='box'></div>";
        snprintf(actual, sizeof(actual), pattern, test.actual);
        snprintf(reference, sizeof(reference), pattern, test.reference);
        expect_html_pair_output_parity(test.name, actual, reference);
    }
    EXPECT_TRUE(file_contains_text("temp/render_output_parity/corner_math_logical.svg", "L0.00,26.00"));
    EXPECT_TRUE(file_contains_text("temp/render_output_parity/corner_math_clamp.svg", "100.00,32.00"));
    const char* actual = "<!doctype html><style>html,body{margin:0;background:white}"
        "#parent{width:200px;height:100px;font-size:20px;border-start-start-radius:calc(1em + 20%)}"
        "#child{width:100px;height:50px;background:green;font-size:40px;border-start-start-radius:inherit}"
        "</style><div id='parent'><div id='child'></div></div>";
    const char* reference = "<!doctype html><style>html,body{margin:0;background:white}"
        "#parent{width:200px;height:100px;font-size:20px;border-start-start-radius:60px 40px}"
        "#child{width:100px;height:50px;background:green;font-size:40px;border-start-start-radius:40px 30px}"
        "</style><div id='parent'><div id='child'></div></div>";
    expect_html_pair_output_parity("corner_math_inherit", actual, reference);

    // deferred effect groups must retain column stroke paths as well as rounded fills and gradients.
    const char* styles[] = {"solid", "double", "dashed"};
    for (const char* style : styles) {
        char name[80], column_actual[1024], column_reference[1024];
        snprintf(name, sizeof(name), "corner_math_columns_%s", style);
        const char* pattern = "<!doctype html><style>html,body{margin:0;background:white}"
            "#box{width:100px;height:80px;background:green;columns:2;column-fill:auto;column-gap:20px;"
            "column-rule:3px %s red;box-shadow:3px 3px 1px red;border-radius:%s}"
            "#box>div{height:80px}</style><div id='box'><div></div><div></div></div>";
        snprintf(column_actual, sizeof(column_actual), pattern, style, "calc(10px + 20%)");
        snprintf(column_reference, sizeof(column_reference), pattern, style, "30px / 26px");
        expect_html_pair_output_parity(name, column_actual, column_reference);
    }
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

TEST(RenderOutputParity, IndividualTransformsComposeBeforeTransformAndSurviveRestyle) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* individual =
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#box{position:absolute;left:80px;top:80px;width:60px;height:40px;"
        "background:red;transform-origin:0 0;transform:translateX(4px);"
        "scale:50% 200%;rotate:90deg;--move:25% 10px;translate:var(--move)}"
        "#box.before{translate:0;rotate:0deg;scale:1}"
        "</style><div id='box' class='before'></div><script>"
        "document.querySelector('#box').className='';</script>";
    const char* equivalent =
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#box{position:absolute;left:80px;top:80px;width:60px;height:40px;"
        "background:red;transform-origin:0 0;"
        "transform:translate(25%,10px) rotate(90deg) scale(.5,2) translateX(4px)}"
        "</style><div id='box'></div>";
    const char* actual = "temp/render_output_parity/individual_transforms.png";
    const char* expected = "temp/render_output_parity/individual_transforms_reference.png";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/individual_transforms.html", actual, individual));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/individual_transforms_reference.html", expected, equivalent));
    expect_pngs_exactly_equal(actual, expected);
}

TEST(RenderOutputParity, BackfacesRespectThreeDimensionalContextsInRasterAndSvg) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html = "test/ui/backface_visibility.html";
    const char* png = "temp/render_output_parity/backface_visibility.png";
    const char* svg = "temp/render_output_parity/backface_visibility.svg";
    ASSERT_TRUE(render_document_fixture(html, png));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png, &image));
    ASSERT_GT(image.width, 500);
    ASSERT_GT(image.height, 50);
    const uint8_t colors[][3] = {{0, 0, 255}, {255, 128, 0}, {255, 0, 0},
        {0, 0, 255}, {255, 0, 0}, {0, 0, 255}};
    for (unsigned i = 0; i < sizeof(colors) / sizeof(*colors); i++) {
        const uint8_t* pixel = image.pixels + ((size_t)50 * image.width + i * 90 + 50) * 4;
        for (unsigned channel = 0; channel < 3; channel++)
            EXPECT_EQ(pixel[channel], colors[i][channel]) << "face " << i;
    }
    image_free(image.pixels);
    ASSERT_TRUE(render_document_fixture(html, svg));
    EXPECT_FALSE(file_contains_text(svg, "rgb(255,0,255)"));
    EXPECT_TRUE(file_contains_text(svg, "rgb(255,128,0)"));
    if (command_exists("sips") || command_exists("pdftoppm")) {
        const char* pdf = "temp/render_output_parity/backface_visibility.pdf";
        const char* pdf_png = "temp/render_output_parity/backface_visibility_pdf.png";
        ASSERT_TRUE(render_document_fixture(html, pdf));
        ASSERT_TRUE(rasterize_fixture_pdf(pdf, pdf_png));
        const char* reference_pdf = "temp/render_output_parity/backface_reference.pdf";
        const char* reference_png = "temp/render_output_parity/backface_reference.png";
        const char* reference_html = "<!doctype html><style>html,body{margin:0;background:white}"
            ".face{position:absolute;top:20px;width:60px;height:60px}</style>"
            "<div class=face style='left:20px;background:blue'></div>"
            "<div class=face style='left:110px;background:#ff8000'></div>"
            "<div class=face style='left:200px;background:red'></div>"
            "<div class=face style='left:290px;background:blue'></div>"
            "<div class=face style='left:380px;background:red'></div>"
            "<div class=face style='left:470px;background:blue'></div>";
        ASSERT_TRUE(render_html_fixture("temp/render_output_parity/backface_reference.html", reference_pdf, reference_html));
        ASSERT_TRUE(rasterize_fixture_pdf(reference_pdf, reference_png));
        // compare like PDF rasterizations: platform color management changes raw sRGB samples.
        ImageData reference = {};
        ASSERT_TRUE(load_png_rgba(pdf_png, &image));
        ASSERT_TRUE(load_png_rgba(reference_png, &reference));
        ASSERT_GT(image.width, 500);
        ASSERT_GT(reference.width, 500);
        ASSERT_GT(image.height, 50);
        ASSERT_GT(reference.height, 50);
        for (unsigned i = 0; i < sizeof(colors) / sizeof(*colors); i++) {
            size_t x = i * 90 + 50;
            const uint8_t* actual_pixel = image.pixels + ((size_t)50 * image.width + x) * 4;
            const uint8_t* reference_pixel = reference.pixels + ((size_t)50 * reference.width + x) * 4;
            EXPECT_EQ(memcmp(actual_pixel, reference_pixel, 4), 0) << "PDF face " << i;
        }
        image_free(image.pixels);
        image_free(reference.pixels);
    }
}

TEST(RenderOutputParity, IndividualThreeDimensionalTransformsReuseFunctionMatrices) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* individual =
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#box{position:absolute;left:80px;top:80px;width:60px;height:40px;"
        "background:red;transform-origin:0 0;perspective:500px;"
        "scale:50% 200% 2;rotate:60deg 1 2 3;translate:25% 10px 20px}"
        "</style><div id='box'></div>";
    const char* equivalent =
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#box{position:absolute;left:80px;top:80px;width:60px;height:40px;"
        "background:red;transform-origin:0 0;perspective:500px;"
        "transform:translate3d(25%,10px,20px) rotate3d(1,2,3,60deg) scale3d(.5,2,2)}"
        "</style><div id='box'></div>";
    const char* actual = "temp/render_output_parity/individual_transforms_3d.png";
    const char* expected = "temp/render_output_parity/individual_transforms_3d_reference.png";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/individual_transforms_3d.html", actual, individual));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/individual_transforms_3d_reference.html", expected, equivalent));
    expect_pngs_exactly_equal(actual, expected);
}

TEST(RenderOutputParity, IndividualTransformsKeepIndependentResetAndInheritance) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>html,body{margin:0}"
        "#parent{translate:10px;rotate:20deg;scale:2}"
        "#box{width:40px;height:40px;background:red;transform-origin:0 0;"
        "translate:inherit;translate:2foo;scale:inherit;rotate:initial;transform:none}"
        "</style><div id='parent'><div id='box'></div></div>";
    const char* svg = "temp/render_output_parity/individual_transform_reset.svg";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/individual_transform_reset.html", svg, html));
    EXPECT_TRUE(file_contains_text(svg, "transform=\"matrix(2 0 0 2 10 0)\""));
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

TEST(RenderOutputParity, GridTrackMathMatchesLiteralSizesAcrossOutputs) {
    struct GridMathCase { const char* name; const char* actual; const char* reference; };
    const GridMathCase cases[] = {
        {"grid_math_gap", "grid-template-columns:calc(10px + 25%) 1fr;column-gap:20px",
            "grid-template-columns:60px 1fr;column-gap:20px"},
        {"grid_math_repeat", "grid-template-columns:repeat(2,calc(5px + 25%)) 1fr;column-gap:20px",
            "grid-template-columns:55px 55px 1fr;column-gap:20px"},
        {"grid_math_auto_repeat", "grid-template-columns:repeat(auto-fill,calc(10px + 25%));column-gap:20px",
            "grid-template-columns:60px 60px;column-gap:20px"},
        {"grid_math_auto_repeat_maximum", "grid-template-columns:repeat(auto-fill,minmax(10px,80px));column-gap:20px",
            "grid-template-columns:80px 80px;column-gap:20px"},
        {"grid_math_auto_repeat_neighbor", "grid-template-columns:calc(10px + 25%) repeat(auto-fill,40px);column-gap:10px",
            "grid-template-columns:60px 40px 40px;column-gap:10px"},
        {"grid_math_minmax", "grid-template-columns:minmax(calc(10px + 10%),calc(10px + 40%)) 1fr",
            "grid-template-columns:minmax(30px,90px) 1fr"},
        {"grid_math_comparison", "grid-template-columns:min(80px,calc(10px + 50%)) max(20px,calc(10px + 25%)) clamp(20px,calc(10px + 10%),40px)",
            "grid-template-columns:80px 60px 30px"},
        {"grid_math_row", "grid-template-columns:100px;grid-template-rows:calc(10px + 25%) 1fr;row-gap:10px",
            "grid-template-columns:100px;grid-template-rows:30px 1fr;row-gap:10px"},
        {"grid_math_implicit", "grid-template-columns:100px;grid-template-rows:none;grid-auto-rows:calc(5px + 20%)",
            "grid-template-columns:100px;grid-template-rows:none;grid-auto-rows:21px"},
        {"grid_math_negative", "grid-template-columns:calc(5px - 10%) 1fr;column-gap:20px",
            "grid-template-columns:0px 1fr;column-gap:20px"},
        {"grid_math_invalid", "grid-template-columns:60px 1fr;grid-template-columns:calc(1px + 1) 1fr",
            "grid-template-columns:60px 1fr"},
        {"grid_math_variable_invalid", "grid-template-columns:60px 1fr;grid-template-columns:var(--tracks);--tracks:calc(1px + 1) 1fr",
            "grid-template-columns:none"}
    };
    for (const GridMathCase& test : cases) {
        SCOPED_TRACE(test.name);
        char actual[4096], reference[4096];
        const char* format = "<!doctype html><style>html,body{margin:0}"
            "#grid{display:grid;width:200px;height:80px;font:10px monospace;"
            "grid-template-rows:20px;align-content:start;justify-content:start;%s}"
            "#grid>div{min-width:0;min-height:0}#grid>div:nth-child(3n+1){background:red}"
            "#grid>div:nth-child(3n+2){background:blue}#grid>div:nth-child(3n){background:lime}"
            "</style><div id='grid'><div></div><div></div><div></div><div></div><div></div><div></div></div>";
        snprintf(actual, sizeof(actual), format, test.actual);
        snprintf(reference, sizeof(reference), format, test.reference);
        expect_html_pair_output_parity(test.name, actual, reference);
    }
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

    ASSERT_TRUE(rasterize_fixture_pdf(pdf_path, png_path));
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

TEST(RenderOutputParity, ScopeRootsLimitsAndProximityReachRasterSvgAndPdf) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* geometry = "<!doctype html><style>html,body{margin:0;background:white}"
        ".scope{position:relative;width:100px;height:40px}"
        ".tile{position:absolute;top:0;width:20px;height:20px}"
        ".near,.limit{position:absolute;top:0;width:20px;height:20px}"
        ".near{left:30px}.limit{left:60px}</style>";
    const char* tree = "<div class=scope><div class=tile></div>"
        "<div class=near><div class=tile></div></div>"
        "<div class=limit><div class=tile></div></div></div>";
    const char* rules[] = {
        "<style>@scope (.scope) to (.limit){.tile{background:green}}"
        "@scope (.near){.tile{background:blue}}"
        "@scope (.scope) to (.limit){.tile{background:green}}"
        ".tile{background:red}</style>",
        "<style>.tile{background:green}.near .tile{background:blue}"
        ".limit .tile{background:red}</style>"
    };
    const char* html_paths[] = {"temp/render_output_parity/scopes.html",
        "temp/render_output_parity/scopes_reference.html"};
    const char* png_paths[] = {"temp/render_output_parity/scopes.png",
        "temp/render_output_parity/scopes_reference.png"};
    const char* svg_paths[] = {"temp/render_output_parity/scopes.svg",
        "temp/render_output_parity/scopes_reference.svg"};
    const char* pdf_paths[] = {"temp/render_output_parity/scopes.pdf",
        "temp/render_output_parity/scopes_reference.pdf"};
    const char* pdf_png_paths[] = {"temp/render_output_parity/scopes_pdf.png",
        "temp/render_output_parity/scopes_reference_pdf.png"};
    bool pdf_available = command_exists("sips") || command_exists("pdftoppm");
    StrBuf* source = strbuf_new();
    ASSERT_NE(source, nullptr);
    for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
        strbuf_reset(source);
        strbuf_append_str(source, geometry);
        strbuf_append_str(source, rules[i]);
        strbuf_append_str(source, tree);
        ASSERT_TRUE(render_html_fixture(html_paths[i], png_paths[i], source->str));
        ASSERT_TRUE(render_document_fixture(html_paths[i], svg_paths[i]));
        if (pdf_available) {
            ASSERT_TRUE(render_document_fixture(html_paths[i], pdf_paths[i]));
            ASSERT_TRUE(rasterize_fixture_pdf(pdf_paths[i], pdf_png_paths[i]));
        }
    }
    strbuf_free(source);
    expect_pngs_exactly_equal(png_paths[1], png_paths[0]);
    EXPECT_TRUE(file_contains_text(svg_paths[0], "rgb(0,128,0)"));
    EXPECT_TRUE(file_contains_text(svg_paths[0], "rgb(0,0,255)"));
    EXPECT_TRUE(file_contains_text(svg_paths[0], "rgb(255,0,0)"));
    if (pdf_available) expect_pngs_exactly_equal(pdf_png_paths[1], pdf_png_paths[0]);
}

TEST(RenderOutputParity, RegisteredPropertyDefaultsInheritanceAndComputedUnitsReachAllOutputs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    const char* sources[] = {
        "<!doctype html><style>html,body{margin:0;background:white}"
        "@property --size{syntax:'<length>';inherits:false;initial-value:20px}"
        "@property --ink{syntax:'<color>';inherits:true;initial-value:green}"
        "@property --basis{syntax:'<length>';inherits:true;initial-value:10px}"
        "#parent{font-size:10px;--size:70px;--ink:blue;--basis:2em}"
        ".box{height:20px;width:var(--size);background:var(--ink)}"
        "#invalid{--size:red}#initial{--ink:initial}#basis{font-size:20px;width:var(--basis)}"
        "</style><div id=parent><div class=box></div><div class=box id=invalid></div>"
        "<div class=box id=initial></div><div class=box id=basis></div></div>",
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#parent{font-size:10px}.box{height:20px;width:20px;background:blue}"
        "#initial{background:green}#basis{font-size:20px}</style>"
        "<div id=parent><div class=box></div><div class=box id=invalid></div>"
        "<div class=box id=initial></div><div class=box id=basis></div></div>"
    };
    expect_html_pair_output_parity("registered_properties", sources[0], sources[1]);
}

TEST(RenderOutputParity, RegisteredPropertyMathAndTransformsComputeBeforeInheritanceInAllOutputs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    const char* sources[] = {
        "<!doctype html><style>html,body{margin:0;background:white}"
        "@property --size{syntax:'<length>';inherits:true;initial-value:calc(1in + 4px)}"
        "@property --mix{syntax:'<length-percentage>';inherits:true;initial-value:0px}"
        "@property --motion{syntax:'<transform-list>';inherits:true;initial-value:rotate(0deg)}"
        "#parent{width:200px;font-size:calc(5px + 5px);--size:calc(2em + 5px);"
        "--mix:calc(2em + 10% + 3px);--motion:translate(calc(2em + 5px),0px)}"
        ".box{height:20px;background:red;font-size:30px;width:var(--size)}"
        "#mixed{width:var(--mix);background:blue}#moved{transform:var(--motion);background:green}"
        "</style><div class=box></div><div id=parent><div class=box></div>"
        "<div class=box id=mixed></div><div class=box id=moved></div></div>",
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#parent{width:200px;font-size:10px}.box{height:20px;background:red;font-size:30px;width:100px}"
        "#parent .box{width:25px}#parent #mixed{width:43px;background:blue}"
        "#moved{transform:translate(25px,0px);background:green}</style>"
        "<div class=box></div><div id=parent><div class=box></div>"
        "<div class=box id=mixed></div><div class=box id=moved></div></div>"
    };
    expect_html_pair_output_parity("registered_math", sources[0], sources[1]);
}

TEST(RenderOutputParity, ScriptRegistrationOverridesCssAndPreservesOwnerComputationInAllOutputs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    const char* sources[] = {
        "<!doctype html><style>html,body{margin:0;background:white}"
        "@property --size{syntax:'<length>';inherits:false;initial-value:5px}"
        "#parent{font-size:10px;--size:calc(2em + 5px)}"
        ".box{font-size:30px;height:20px;background:red;width:var(--size)}"
        "#invalid{--size:red;background:blue}</style>"
        "<div class=box></div><div id=parent><div class=box></div><div class=box id=invalid></div></div>"
        "<script>CSS.registerProperty({name:'--size',syntax:'<length>',inherits:true,initialValue:'35px'});</script>",
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#parent{font-size:10px}.box{font-size:30px;height:20px;background:red;width:35px}"
        "#parent .box{width:25px}#invalid{background:blue}</style>"
        "<div class=box></div><div id=parent><div class=box></div><div class=box id=invalid></div></div>"
    };
    expect_html_pair_output_parity("script_registration", sources[0], sources[1]);
}

TEST(RenderOutputParity, RegisteredColorsPreserveCurrentColorAndComputeMathInAllOutputs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    const char* sources[] = {
        "<!doctype html><style>html,body{margin:0;background:white}"
        "@property --ink{syntax:'<color>';inherits:true;initial-value:red}"
        "#parent{color:red;--ink:currentColor}.box{width:30px;height:20px;background:var(--ink)}"
        "#child{color:blue}#math{--ink:rgb(calc(100 + 20) 0 0 / calc(.5))}"
        "#hue{--ink:hsl(.5turn 100% 50%)}#white{--ink:hwb(60deg 0% 0%)}"
        "#missing{--ink:rgb(20 0 0 / none)}#invalid{--ink:rgb(1 2 3 4)}"
        "#space{--ink:color(srgb 100% 0 0 / 50%)}"
        "</style><div id=parent><div class=box id=child></div><div class=box id=math></div>"
        "<div class=box id=hue></div><div class=box id=white></div>"
        "<div class=box id=missing></div><div class=box id=invalid></div><div class=box id=space></div></div>",
        "<!doctype html><style>html,body{margin:0;background:white}"
        "#parent{color:red}.box{width:30px;height:20px;background:red}"
        "#child{color:blue;background:blue}#math{background:rgba(120,0,0,.5)}"
        "#hue{background:cyan}#white{background:yellow}#missing{background:transparent}"
        "#space{background:rgba(255,0,0,.5)}"
        "</style><div id=parent><div class=box id=child></div><div class=box id=math></div>"
        "<div class=box id=hue></div><div class=box id=white></div>"
        "<div class=box id=missing></div><div class=box id=invalid></div><div class=box id=space></div></div>"
    };
    expect_html_pair_output_parity("registered_colors", sources[0], sources[1]);
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

TEST(RenderOutputParity, AnimationShorthandProjectsCascadeAndVariableValues) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "div{width:20px;height:10px;background:red}"
        "#long{animation-name:grow;animation-duration:1s;"
        "animation-timing-function:linear;animation-delay:-.5s;animation-fill-mode:both}"
        "#short{animation:grow -.5s 1s linear both}"
        "#longWins{animation:grow 1s linear -.5s both;animation-duration:2s}"
        "#shortWins{animation-duration:2s;animation:grow 1s linear -.5s both}"
        "#variable{--motion:grow 1s linear -.5s both;animation:var(--motion)}"
        "#invalid{animation:grow 1s linear -.5s both;animation:grow 1s 2s 3s}"
        "#computedInvalid{--motion:grow 1px;animation:grow 1s linear -.5s both;"
        "animation:var(--motion)}"
        "#zero{animation:grow 0s linear forwards}"
        "</style><div id=long></div><div id=short></div><div id=longWins></div>"
        "<div id=shortWins></div><div id=variable></div><div id=invalid></div>"
        "<div id=computedInvalid></div><div id=zero></div>";
    const char* svg_path = "temp/render_output_parity/animation_shorthand.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/animation_shorthand.html", svg_path, html));
    const float widths[] = {60, 60, 40, 60, 60, 60, 20, 100};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        char expected[160];
        snprintf(expected, sizeof(expected),
            "y=\"%.2f\" width=\"%.2f\" height=\"10.00\" fill=\"rgb(255,0,0)\"",
            (double)i * 10.0, (double)widths[i]);
        EXPECT_TRUE(file_contains_text(svg_path, expected)) << expected;
    }
}

TEST(RenderOutputParity, AnimationListsSampleEveryNameAndRepeatShorterLonghandLists) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "@keyframes tint{from{background-color:green}to{background-color:green}}"
        "div{width:20px;height:10px;background:red}"
        "#short{animation:grow 1s linear -.5s both,tint 2s linear -.5s both}"
        "#long{animation-name:grow,tint;animation-duration:1s,2s;"
        "animation-timing-function:linear;animation-delay:-.5s;animation-fill-mode:both}"
        "</style><div id=short></div><div id=long></div>";
    const char* svg_path = "temp/render_output_parity/animation_lists.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/animation_lists.html", svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"0.00\" width=\"60.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"10.00\" width=\"60.00\" height=\"10.00\" fill=\"rgb(0,128,0)\""));
}

TEST(RenderOutputParity, AnimationNamesRetainCaseAndTimingEndpointsPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "@keyframes Reverse{from{width:20px}to{width:80px}}"
        "@keyframes reverse{from{width:20px}to{width:40px}}"
        "div{width:20px;height:10px;background:red}"
        "#upper{animation-name:Reverse;animation-duration:0s;animation-fill-mode:forwards}"
        "#lower{animation-name:reverse;animation-duration:0s;animation-fill-mode:forwards}"
        "#quoted{animation:\"Reverse\" 0s forwards}"
        "#paused{animation:grow 1s linear -.5s both paused}"
        "#fraction{animation:grow 1s linear -2s .5 forwards}"
        "#zero{animation:grow 1s linear 0 forwards}"
        "#back{animation:grow 1s linear 2s reverse backwards}"
        "#steps{animation:grow 1s steps(4,jump-start) -.25s both}"
        "</style><div id=upper></div><div id=lower></div><div id=quoted></div>"
        "<div id=paused></div><div id=fraction></div><div id=zero></div>"
        "<div id=back></div><div id=steps></div>";
    const char* path = "temp/render_output_parity/animation_endpoints.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/animation_endpoints.html", path, html));
    const float widths[] = {80, 40, 80, 60, 60, 20, 100, 60};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        char expected[160];
        snprintf(expected, sizeof(expected),
            "y=\"%.2f\" width=\"%.2f\" height=\"10.00\" fill=\"rgb(255,0,0)\"",
            (double)i * 10.0, (double)widths[i]);
        EXPECT_TRUE(file_contains_text(path, expected)) << expected;
    }
}

TEST(RenderOutputParity, AnimationEventsMutateStylesAfterLayoutAndKeepPayloads) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "#box{width:20px;height:10px;background:red;animation:grow 0s linear forwards}"
        "</style><div id=box></div><script>"
        "var box=document.getElementById('box');"
        "box.addEventListener('animationstart',function(event){"
        "box.setAttribute('data-start',event.animationName);"
        "box.style.backgroundColor='green';"
        "box.setAttribute('data-width',String(box.getBoundingClientRect().width));});"
        "box.addEventListener('animationend',function(event){"
        "box.setAttribute('data-end',String(event.elapsedTime));});"
        "</script>";
    const char* events =
        "{\"name\":\"animation event layout ownership\","
        "\"html\":\"temp/render_output_parity/animation_events.html\","
        "\"viewport\":{\"width\":200,\"height\":40},\"events\":["
        "{\"type\":\"wait\",\"ms\":30},"
        "{\"type\":\"assert_attribute\",\"target\":{\"selector\":\"#box\"},"
        "\"attribute\":\"data-start\",\"equals\":\"grow\"},"
        "{\"type\":\"assert_attribute\",\"target\":{\"selector\":\"#box\"},"
        "\"attribute\":\"data-width\",\"equals\":\"100\"},"
        "{\"type\":\"assert_attribute\",\"target\":{\"selector\":\"#box\"},"
        "\"attribute\":\"data-end\",\"equals\":\"0\"},"
        "{\"type\":\"assert_pixel\",\"x\":80,\"y\":5,\"min_g\":120,\"max_r\":20,\"max_b\":20}]}";
    ASSERT_TRUE(run_html_fixture_view(
        "temp/render_output_parity/animation_events.html",
        "temp/render_output_parity/animation_events.json", html, events));
}

TEST(RenderOutputParity, AnimationPausedListRestyleUpdatesAndCancelsPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "@keyframes tint{from{background-color:green}to{background-color:green}}"
        "#box{width:20px;height:10px;background:red;"
        "animation:grow 1s linear -.5s both paused,tint 1s both paused}"
        "button{position:absolute;top:30px;width:80px;height:20px}"
        "#update{left:0}#cancel{left:100px}</style><div id=box></div>"
        "<button id=update onclick=\"document.getElementById('box').style.animation="
        "'grow 2s linear -.5s both paused'\">update</button>"
        "<button id=cancel onclick=\"document.getElementById('box').style.animation='none'\">cancel</button>";
    const char* events =
        "{\"name\":\"paused animation restyle\","
        "\"html\":\"temp/render_output_parity/animation_restyle.html\","
        "\"viewport\":{\"width\":200,\"height\":80},\"events\":["
        "{\"type\":\"assert_pixel\",\"x\":50,\"y\":5,\"min_g\":120,\"max_r\":20,\"max_b\":20},"
        "{\"type\":\"click\",\"x\":40,\"y\":40},"
        "{\"type\":\"assert_pixel\",\"x\":30,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":50,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"x\":140,\"y\":40},"
        "{\"type\":\"assert_pixel\",\"x\":10,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":30,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    ASSERT_TRUE(run_html_fixture_view(
        "temp/render_output_parity/animation_restyle.html",
        "temp/render_output_parity/animation_restyle.json", html, events));
}

TEST(RenderOutputParity, TransitionListsProjectCascadeVariablesAndKeepEveryProperty) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "div{position:absolute;left:0;width:20px;height:10px;background:red}"
        ".changed div{width:100px}"
        "#a{top:0;transition:width 1s linear -.5s;transition-duration:2s}"
        "#b{top:20px;transition:all 1s linear -.5s,width 2s linear -.5s}"
        "#c{top:40px;transition-property:opacity,color,background-color,height,min-height,"
        "max-height,min-width,max-width,aspect-ratio,width;transition-duration:1s,2s;"
        "transition-delay:-.5s;transition-timing-function:linear}"
        "#d{top:60px;transition-property:height,unknown-target,width;"
        "transition-duration:1s,2s,4s;transition-delay:-.5s;transition-timing-function:linear}"
        "#e{top:80px;transition:none;transition-duration:1s;transition-delay:-.5s}"
        "#f{top:100px;--motion:width -.5s 2s linear;transition:var(--motion)}"
        "button{position:absolute;left:120px;top:130px;width:60px;height:20px}"
        "</style><div id=a></div><div id=b></div><div id=c></div><div id=d></div>"
        "<div id=e></div><div id=f></div><button onclick=\"document.body.className='changed';"
        "var box=document.getElementById('d');var s=getComputedStyle(box);"
        "box.setAttribute('data-duration',s.transitionDuration);"
        "box.setAttribute('data-properties',s.transitionProperty)\">change</button>";
    const char* events =
        "{\"name\":\"transition computed lists and paint\","
        "\"html\":\"temp/render_output_parity/transition_lists.html\","
        "\"viewport\":{\"width\":200,\"height\":170},\"events\":["
        "{\"type\":\"click\",\"x\":150,\"y\":140},"
        "{\"type\":\"assert_attribute\",\"target\":{\"selector\":\"#d\"},"
        "\"attribute\":\"data-duration\",\"equals\":\"1s, 2s, 4s\"},"
        "{\"type\":\"assert_attribute\",\"target\":{\"selector\":\"#d\"},"
        "\"attribute\":\"data-properties\",\"equals\":\"height, unknown-target, width\"},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":45,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":25,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":45,\"y\":25,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":45,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":45,\"y\":45,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"assert_pixel\",\"x\":25,\"y\":65,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":65,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"assert_pixel\",\"x\":80,\"y\":85,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":105,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":45,\"y\":105,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    ASSERT_TRUE(run_html_fixture_view(
        "temp/render_output_parity/transition_lists.html",
        "temp/render_output_parity/transition_lists.json", html, events));
}

TEST(RenderOutputParity, TransitionRetargetRestartAndCancellationReachPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "#box{width:20px;height:10px;background:red;transition:width 1s linear}"
        "button{position:absolute;top:30px;width:40px;height:20px}"
        "#start{left:0}#retarget{left:50px}#restart{left:100px}#cancel{left:150px}"
        "</style><div id=box></div>"
        "<button id=start onclick=\"document.getElementById('box').style.width='100px'\">start</button>"
        "<button id=retarget onclick=\"document.getElementById('box').style.width='60px'\">retarget</button>"
        "<button id=restart onclick=\"document.getElementById('box').style.width='80px'\">restart</button>"
        "<button id=cancel onclick=\"var b=document.getElementById('box');"
        "b.style.transition='none';b.style.width='20px'\">cancel</button>";
    // clicks drain their input frames, so samples include that elapsed time.
    const char* events =
        "{\"name\":\"transition retarget and completed restart paint\","
        "\"html\":\"temp/render_output_parity/transition_retarget.html\","
        "\"viewport\":{\"width\":200,\"height\":80},\"events\":["
        "{\"type\":\"click\",\"x\":20,\"y\":40},{\"type\":\"advance_time\",\"ms\":250},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":50,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"x\":70,\"y\":40},{\"type\":\"advance_time\",\"ms\":250},"
        "{\"type\":\"assert_pixel\",\"x\":43,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":55,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"advance_time\",\"ms\":1000},{\"type\":\"click\",\"x\":120,\"y\":40},"
        "{\"type\":\"advance_time\",\"ms\":500},"
        "{\"type\":\"assert_pixel\",\"x\":65,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":75,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"x\":170,\"y\":40},"
        "{\"type\":\"assert_pixel\",\"x\":15,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":25,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    ASSERT_TRUE(run_html_fixture_view(
        "temp/render_output_parity/transition_retarget.html",
        "temp/render_output_parity/transition_retarget.json", html, events));
}

static StrBuf* physical_side_effect_fixture(bool transition) {
    const char* sides[] = {"top", "right", "bottom", "left"};
    const char* properties[] = {"border-%s-color", "border-%s-width", "%s",
        "margin-%s", "padding-%s"};
    const char* from[] = {"red", "2px", "0px", "-10px", "0px"};
    const char* to[] = {"blue", "10px", "20px", "30px", "20px"};
    StrBuf* html = strbuf_new();
    strbuf_append_str(html, "<!doctype html><style>body{margin:0;background:white;height:350px}"
        ".row{position:absolute;width:120px;font-size:0;line-height:0}"
        ".cell{display:inline-block;position:relative;vertical-align:top;"
        "width:40px;height:20px;border:2px solid red;background:white}"
        ".marker{display:inline-block;vertical-align:top;margin-left:30px;"
        "width:5px;height:5px;background:blue}"
        ".below{display:block;width:5px;height:5px;background:blue}"
        "button{position:absolute;left:0;top:310px;width:60px;height:20px}");
    for (int family = 0; family < 5; family++) {
        for (int side = 0; side < 4; side++) {
            int index = family * 4 + side;
            char property[48];
            snprintf(property, sizeof(property), properties[family], sides[side]);
            strbuf_append_format(html, "#row%d{left:%dpx;top:%dpx}", index, side * 140, family * 60);
            if (transition) {
                strbuf_append_format(html,
                    "#b%d{%s:%s;transition:%s 1s linear -.5s}.changed #b%d{%s:%s}",
                    index, property, from[family], property, index, property, to[family]);
            } else {
                strbuf_append_format(html, "@keyframes k%d{from{%s:%s}to{%s:%s}}"
                    "#b%d{animation:k%d 1s linear -.5s both paused}",
                    index, property, from[family], property, to[family], index, index);
            }
        }
    }
    strbuf_append_str(html, "</style>");
    for (int index = 0; index < 20; index++) {
        strbuf_append_format(html, "<div class=row id=row%d><span class=cell id=b%d></span>"
            "<i class=marker></i><i class=below></i></div>", index, index);
    }
    if (transition) strbuf_append_str(html,
        "<button onclick=\"document.body.className='changed'\">change</button>");
    return html;
}

struct PhysicalSideGeometry {
    float x, y, width, height;
    float marker_x, marker_y, below_y;
};

static PhysicalSideGeometry physical_side_geometry(int family, int side, float progress) {
    float row_x = (float)side * 140.0f;
    float row_y = (float)family * 60.0f;
    float offset = progress * 20.0f;
    float margin = -10.0f + progress * 40.0f;
    PhysicalSideGeometry result = {row_x, row_y, 44.0f, 24.0f};
    if (family == 1 || family == 4) {
        float addition = family == 1 ? progress * 8.0f : offset;
        if (side == 1 || side == 3) result.width += addition;
        else result.height += addition;
    } else if (family == 2) {
        if (side == 0) result.y += offset;
        else if (side == 1) result.x -= offset;
        else if (side == 2) result.y -= offset;
        else result.x += offset;
    } else if (family == 3) {
        if (side == 0) result.y += margin;
        else if (side == 3) result.x += margin;
    }
    // keep the flow marker beyond a relatively positioned cell's painted overlap.
    result.marker_x = row_x + result.width + 30.0f;
    result.marker_y = row_y;
    result.below_y = row_y + result.height;
    if (family == 3) {
        if (side == 1 || side == 3) result.marker_x += margin;
        else result.below_y += margin;
    }
    return result;
}

TEST(RenderOutputParity, AnimationPhysicalBoxSidesReachLayoutAndPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    StrBuf* html = physical_side_effect_fixture(false);
    bool rendered = render_html_fixture("temp/render_output_parity/animation_physical_sides.html",
        "temp/render_output_parity/animation_physical_sides.svg", html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    const char* svg = "temp/render_output_parity/animation_physical_sides.svg";
    for (int family = 0; family < 5; family++) {
        for (int side = 0; side < 4; side++) {
            PhysicalSideGeometry box = physical_side_geometry(family, side, .5f);
            char expected[200];
            snprintf(expected, sizeof(expected),
                "x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"rgb(255,255,255)\"",
                (double)box.x, (double)box.y, (double)box.width, (double)box.height);
            EXPECT_TRUE(file_contains_text(svg, expected)) << family << "/" << side;
            snprintf(expected, sizeof(expected),
                "x=\"%.2f\" y=\"%.2f\" width=\"5.00\" height=\"5.00\" fill=\"rgb(0,0,255)\"",
                (double)box.marker_x, (double)box.marker_y);
            EXPECT_TRUE(file_contains_text(svg, expected)) << family << "/" << side;
            if (family == 3 && side == 2) {
                // the bottom margin must affect the following block's painted position.
                EXPECT_TRUE(file_contains_text(svg,
                    "x=\"280.00\" y=\"214.00\" width=\"5.00\" height=\"5.00\" fill=\"rgb(0,0,255)\""));
            }
        }
    }
    const char* colored_sides[] = {
        "points=\"0.00,0.00 44.00,0.00 42.00,2.00 2.00,2.00\" fill=\"rgb(128,0,128)\"",
        "points=\"182.00,2.00 184.00,0.00 184.00,24.00 182.00,22.00\" fill=\"rgb(128,0,128)\"",
        "points=\"282.00,22.00 322.00,22.00 324.00,24.00 280.00,24.00\" fill=\"rgb(128,0,128)\"",
        "points=\"420.00,0.00 422.00,2.00 422.00,22.00 420.00,24.00\" fill=\"rgb(128,0,128)\""
    };
    for (const char* color : colored_sides) EXPECT_TRUE(file_contains_text(svg, color));
}

TEST(RenderOutputParity, TransitionPhysicalBoxSidesReachGeometryAndRasterPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    StrBuf* html = physical_side_effect_fixture(true);
    StrBuf* events = strbuf_new();
    strbuf_append_str(events, "{\"name\":\"physical side transition consumers\","
        "\"html\":\"temp/render_output_parity/transition_physical_sides.html\","
        "\"viewport\":{\"width\":600,\"height\":350},\"events\":["
        "{\"type\":\"click\",\"x\":30,\"y\":320}");
    // the click drains five input frames after the negative-delay initial sample.
    float progress = .5f + 5.0f / 60.0f;
    for (int family = 0; family < 5; family++) {
        for (int side = 0; side < 4; side++) {
            int index = family * 4 + side;
            PhysicalSideGeometry box = physical_side_geometry(family, side, progress);
            strbuf_append_format(events, ",{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#b%d\"},"
                "\"x\":%.2f,\"y\":%.2f,\"width\":%.2f,\"height\":%.2f,\"tolerance\":1}",
                index, (double)box.x, (double)box.y, (double)box.width, (double)box.height);
            strbuf_append_format(events, ",{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%.2f,"
                "\"max_r\":20,\"max_g\":20,\"min_b\":240}",
                (double)box.marker_x + 2.0, (double)box.marker_y + 2.0);
            if (family == 3 && side == 2) {
                strbuf_append_format(events, ",{\"type\":\"assert_pixel\",\"x\":282,\"y\":%.2f,"
                    "\"max_r\":20,\"max_g\":20,\"min_b\":240}", (double)box.below_y + 2.0);
            }
        }
    }
    const float border_points[][2] = {{22, 1}, {183, 12}, {302, 23}, {421, 12}};
    for (const auto& point : border_points) {
        strbuf_append_format(events, ",{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%.2f,"
            "\"min_r\":70,\"max_r\":170,\"max_g\":20,\"min_b\":90,\"max_b\":190}",
            (double)point[0], (double)point[1]);
    }
    strbuf_append_str(events, "]}");
    bool passed = run_html_fixture_view("temp/render_output_parity/transition_physical_sides.html",
        "temp/render_output_parity/transition_physical_sides.json", html->str, events->str);
    strbuf_free(events);
    strbuf_free(html);
    EXPECT_TRUE(passed);
}

TEST(RenderOutputParity, AnimationMarginsOverrideHtmlDefaults) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{background:white;"
        "animation:move 1s linear -.5s both paused}"
        "@keyframes move{from{margin-top:0px;margin-left:0px}"
        "to{margin-top:20px;margin-left:20px}}"
        ".red{width:40px;height:10px;background:red}"
        "h1{width:40px;height:10px;background:blue;font-size:20px;"
        "margin-left:0;margin-right:0;margin-bottom:0;"
        "animation:gap 1s linear -.5s both paused}"
        "@keyframes gap{from{margin-top:0px}to{margin-top:20px}}"
        "</style><div class=red></div><h1></h1>";
    const char* svg = "temp/render_output_parity/animation_default_margins.svg";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/animation_default_margins.html",
        svg, html));
    EXPECT_TRUE(file_contains_text(svg,
        "x=\"10.00\" y=\"10.00\" width=\"40.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg,
        "x=\"10.00\" y=\"30.00\" width=\"40.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

static StrBuf* typed_keyframe_length_fixture() {
    StrBuf* html = strbuf_new();
    strbuf_append_str(html,
        "<!doctype html><style>html{font-size:12px}body{margin:0;background:white}"
        ".row{position:absolute;left:0;width:200px;height:30px;font-size:10px}"
        ".box{position:relative;width:20px;height:10px;background:red;"
        "animation:ems 1s linear -.5s both paused}"
        "@keyframes ems{from{width:2em}to{width:4em}}"
        "@keyframes percent{from{width:10%}to{width:30%}}"
        "@keyframes math{from{width:calc(10px + 10%)}to{width:calc(30px + 30%)}}"
        "@keyframes root{from{width:2rem}to{width:4rem}}"
        "@keyframes physical{from{width:.25in}to{width:.75in}}"
        "@keyframes inset{from{left:10%}to{left:30%}}"
        "@keyframes margin{from{margin-left:10%}to{margin-left:30%}}"
        "@keyframes padding{from{padding-left:10%}to{padding-left:30%}}"
        "@keyframes variable{from{width:var(--from)}to{width:var(--to)}}"
        "#b1,#b9{font-size:20px}#b2{animation-name:percent}"
        "#b3{animation-name:math}#b4{animation-name:root}#b5{animation-name:physical}"
        "#b6{animation-name:inset}#b7{animation-name:margin}#b8{animation-name:padding}"
        "#b9{animation-name:variable;--from:2em;--to:4em}"
        "button{position:absolute;left:0;top:420px;width:200px;height:20px}");
    for (int index = 0; index < 10; index++) {
        strbuf_append_format(html, "#r%d{top:%dpx}", index, index * 40);
    }
    strbuf_append_str(html, "</style>");
    for (int index = 0; index < 10; index++) {
        strbuf_append_format(html,
            "<div class=row id=r%d><div class=box id=b%d></div></div>", index, index);
    }
    strbuf_append_str(html,
        "<button onclick=\"document.getElementById('b0').style.fontSize='20px';"
        "document.getElementById('r2').style.width='400px';"
        "document.getElementById('r3').style.width='400px';"
        "document.getElementById('b9').style.setProperty('--to','6em')\">change</button>");
    return html;
}

struct TypedKeyframeGeometry {
    float x, width;
};

static TypedKeyframeGeometry typed_keyframe_geometry(int index, bool changed) {
    const float widths[] = {30, 60, 40, 60, 36, 48, 20, 20, 60, 60};
    TypedKeyframeGeometry box = {index == 6 || index == 7 ? 40.0f : 0.0f, widths[index]};
    if (changed) {
        if (index == 0) box.width = 60.0f;
        else if (index == 2) box.width = 80.0f;
        else if (index == 3) box.width = 100.0f;
        else if (index == 9) box.width = 80.0f;
    }
    return box;
}

TEST(RenderOutputParity, AnimationTypedKeyframeLengthsResolveIntoPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    StrBuf* html = typed_keyframe_length_fixture();
    const char* svg = "temp/render_output_parity/animation_typed_lengths.svg";
    bool rendered = render_html_fixture(
        "temp/render_output_parity/animation_typed_lengths.html", svg, html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    for (int index = 0; index < 10; index++) {
        TypedKeyframeGeometry box = typed_keyframe_geometry(index, false);
        char expected[200];
        snprintf(expected, sizeof(expected),
            "x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"10.00\" fill=\"rgb(255,0,0)\"",
            (double)box.x, (double)index * 40.0, (double)box.width);
        EXPECT_TRUE(file_contains_text(svg, expected)) << index;
    }
}

TEST(RenderOutputParity, AnimationTypedKeyframeLengthsResampleAfterStyleChanges) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    StrBuf* html = typed_keyframe_length_fixture();
    StrBuf* events = strbuf_new();
    strbuf_append_str(events, "{\"name\":\"keyframe length contexts refresh\","
        "\"html\":\"temp/render_output_parity/animation_typed_lengths_live.html\","
        "\"viewport\":{\"width\":450,\"height\":460},\"events\":[");
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) strbuf_append_str(events, ",{\"type\":\"click\",\"x\":30,\"y\":430}");
        for (int index = 0; index < 10; index++) {
            TypedKeyframeGeometry box = typed_keyframe_geometry(index, pass == 1);
            strbuf_append_format(events,
                "%s{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#b%d\"},"
                "\"x\":%.2f,\"y\":%.2f,\"width\":%.2f,\"height\":10,\"tolerance\":0.1},"
                "{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%.2f,"
                "\"min_r\":240,\"max_g\":20,\"max_b\":20},"
                "{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%.2f,"
                "\"min_r\":240,\"min_g\":240,\"min_b\":240}",
                pass == 0 && index == 0 ? "" : ",", index,
                (double)box.x, (double)index * 40.0, (double)box.width,
                (double)box.x + box.width - 2.0, (double)index * 40.0 + 5.0,
                (double)box.x + box.width + 2.0, (double)index * 40.0 + 5.0);
        }
    }
    strbuf_append_str(events, "]}");
    bool passed = run_html_fixture_view(
        "temp/render_output_parity/animation_typed_lengths_live.html",
        "temp/render_output_parity/animation_typed_lengths_live.json", html->str, events->str);
    strbuf_free(events);
    strbuf_free(html);
    EXPECT_TRUE(passed);
}

TEST(RenderOutputParity, AnimationInvalidKeyframeLengthsUseComputedFallback) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        ".row{width:200px;height:50px;font-size:10px}"
        ".box{width:40px;height:10px;background:red;animation:bad 1s linear -.5s both paused}"
        "@keyframes bad{from{width:10px}to{width:var(--end)}}"
        "@keyframes math{from{width:calc(-20px)}to{width:20px}}"
        "@keyframes pad{from{padding-left:calc(-20px)}to{padding-left:20px}}"
        "@keyframes invalid{from{width:10px}to{width:30px;width:20junk}}"
        "@keyframes duplicate{from{width:10px;width:20px}to{width:30px;width:40px}}"
        "#b5{animation-name:math}#b6{animation-name:pad}"
        "#b7{animation-name:invalid}#b8{animation-name:duplicate}</style>"
        "<div class=row><div class=box id=b0 style=\"--end:20junk\"></div></div>"
        "<div class=row><div class=box id=b1></div></div>"
        "<div class=row><div class=box id=b2 style=\"--end:auto\"></div></div>"
        "<div class=row><div class=box id=b3 style=\"--end:10px 20px\"></div></div>"
        "<div class=row><div class=box id=b4 style=\"--end:calc(2px + bad)\"></div></div>"
        "<div class=row><div class=box id=b5></div></div>"
        "<div class=row><div class=box id=b6></div></div>"
        "<div class=row><div class=box id=b7></div></div>"
        "<div class=row><div class=box id=b8></div></div>";
    const float widths[] = {200, 200, 200, 200, 200, 0, 40, 20, 30};
    StrBuf* events = strbuf_new();
    strbuf_append_str(events, "{\"name\":\"keyframe value fallback and final range\","
        "\"html\":\"temp/render_output_parity/animation_invalid_lengths.html\","
        "\"viewport\":{\"width\":250,\"height\":460},\"events\":[");
    for (int index = 0; index < 9; index++) {
        strbuf_append_format(events,
            "%s{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#b%d\"},"
            "\"x\":0,\"y\":%.2f,\"width\":%.2f,\"height\":10,\"tolerance\":0.1},"
            "{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%.2f,"
            "\"min_r\":240,\"min_g\":240,\"min_b\":240}",
            index == 0 ? "" : ",", index, (double)index * 50.0, (double)widths[index],
            (double)widths[index] + 2.0, (double)index * 50.0 + 5.0);
        if (widths[index] > 0) strbuf_append_format(events,
            ",{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%.2f,"
            "\"min_r\":240,\"max_g\":20,\"max_b\":20}",
            (double)widths[index] - 2.0, (double)index * 50.0 + 5.0);
    }
    strbuf_append_str(events, "]}");
    bool passed = run_html_fixture_view(
        "temp/render_output_parity/animation_invalid_lengths.html",
        "temp/render_output_parity/animation_invalid_lengths.json", html, events->str);
    strbuf_free(events);
    EXPECT_TRUE(passed);
}

static const char* web_length_effect_fixture() {
    return
        "<!doctype html><style>body{margin:0;background:white}"
        "#row{width:200px;font-size:10px}#box{width:40px;height:10px;background:red}"
        "button{position:absolute;top:30px;width:80px;height:20px}"
        "#start{left:0}#resize{left:90px}#seek{left:180px}</style><div id=row><div id=box></div></div>"
        "<button id=start onclick=\"var b=document.getElementById('box');"
        "window.motion=b.animate([{width:'2em'},{width:'calc(4em + 20%)'}],"
        "{duration:1000,fill:'both'});window.motion.pause();window.motion.currentTime=500\">start</button>"
        "<button id=resize onclick=\"document.getElementById('box').style.fontSize='20px'\">resize</button>"
        "<button id=seek onclick=\"window.motion.currentTime=750\">seek</button>";
}

TEST(RenderOutputParity, AnimationWebKeyframeLengthsResolveAndResample) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* events =
        "{\"name\":\"Web Animation length contexts refresh\","
        "\"html\":\"temp/render_output_parity/animation_web_lengths.html\","
        "\"viewport\":{\"width\":280,\"height\":70},\"events\":["
        "{\"type\":\"click\",\"x\":30,\"y\":40},"
        "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#box\"},"
        "\"x\":0,\"y\":0,\"width\":50,\"height\":10,\"tolerance\":0.1},"
        "{\"type\":\"assert_pixel\",\"x\":48,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":52,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"x\":120,\"y\":40},"
        "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#box\"},"
        "\"x\":0,\"y\":0,\"width\":80,\"height\":10,\"tolerance\":0.1},"
        "{\"type\":\"assert_pixel\",\"x\":78,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":82,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"x\":210,\"y\":40},"
        "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#box\"},"
        "\"x\":0,\"y\":0,\"width\":100,\"height\":10,\"tolerance\":0.1},"
        "{\"type\":\"assert_pixel\",\"x\":98,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":102,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    ASSERT_TRUE(run_html_fixture_view("temp/render_output_parity/animation_web_lengths.html",
        "temp/render_output_parity/animation_web_lengths.json", web_length_effect_fixture(), events));
}

TEST(RenderOutputParity, AnimationStandaloneWebSeeksCommitPendingGeometry) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_web_lengths_standalone.html";
    const char* js_path = "temp/render_output_parity/animation_web_lengths_standalone.js";
    const char* html = web_length_effect_fixture();
    const char* script =
        "var box=document.getElementById('box');\n"
        "console.log(box.getBoundingClientRect().width);\n"
        "var motion=box.animate([{width:'2em'},{width:'calc(4em + 20%)'}],"
        "{duration:1000,fill:'both'});\n"
        "motion.pause();motion.currentTime=500;\n"
        "console.log(box.getBoundingClientRect().width);\n"
        "box.style.fontSize='20px';\n"
        "console.log(box.getBoundingClientRect().width);\n"
        "motion.currentTime=750;\n"
        "console.log(box.getBoundingClientRect().width);\n";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    ASSERT_TRUE(write_file_all(js_path, script, strlen(script)));
    char qhtml[PATH_MAX + 8], qjs[PATH_MAX + 8], command[PATH_MAX * 2 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(js_path, qjs, sizeof(qjs));
    snprintf(command, sizeof(command), "%s js %s --document %s --no-log 2>&1",
        LAMBDA_EXE, qjs, qhtml);
    CommandResult result = run_command_capture(command);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "40\n50\n80\n100\n");
}

TEST(RenderOutputParity, AnimationWebKeyframesConvertListsOffsetsAndEffectSnapshots) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_web_keyframe_conversion.html";
    const char* js_path = "temp/render_output_parity/animation_web_keyframe_conversion.js";
    const char* html =
        "<!doctype html><style>body{margin:0}.box{position:absolute;top:0;left:0;"
        "width:40px;height:10px;background:red}</style><div class=box id=box></div>";
    // Chromium reference checks conversion ordering/error identity as well as used values.
    const char* script =
        "var box=document.getElementById('box');\n"
        "function reset(){box=document.createElement('div');box.className='box';document.body.appendChild(box);return box;}\n"
        "function sample(frames,time){reset();var a=box.animate(frames,{duration:1000,fill:'both',easing:'linear'});a.pause();a.currentTime=time;return box.getBoundingClientRect().width;}\n"
        "function check(name,run){try{console.log(name+':'+run());}catch(e){console.log(name+':THREW:'+e.name+':'+e.message);}}\n"
        "function rejects(frames){try{sample(frames,500);return false;}catch(e){return e.name==='TypeError';}}\n"
        "check('anchored',()=>Math.abs(sample([{width:'20px',offset:.2},{width:'40px'},{width:'60px',offset:.8},{width:'100px'}],500)-40)<.01);\n"
        "check('single',()=>Math.abs(sample([{width:'100px'}],500)-70)<.01);\n"
        "check('single_indexed',()=>Math.abs(sample({width:'100px'},500)-70)<.01);\n"
        "check('uneven_indexed',()=>Math.abs(sample({width:['20px','40px','100px'],opacity:[0,1]},500)-40)<.01&&Math.abs(Number(getComputedStyle(box).opacity)-.5)<.01);\n"
        "check('shared_offsets',()=>Math.abs(sample({width:['20px','60px','100px'],offset:[0,.25,1]},250)-60)<.01);\n"
        "check('frame_easing',()=>Math.abs(sample([{width:'20px',easing:'steps(2,start)'},{width:'100px'}],250)-60)<.01);\n"
        "check('indexed_easing',()=>Math.abs(sample({width:['20px','60px','100px'],easing:['steps(2,start)','linear']},125)-40)<.01);\n"
        "check('large',()=>{var f=[];for(var i=0;i<80;i++)f.push({width:(20+i)+'px'});return Math.abs(sample(f,1000)-99)<.01;});\n"
        "check('negative_offset',()=>rejects([{width:'20px',offset:-.1},{width:'100px'}]));\n"
        "check('descending_offset',()=>rejects([{width:'20px',offset:.8},{width:'100px',offset:.2}]));\n"
        "check('nonfinite_offset',()=>rejects([{width:'20px',offset:Infinity}]));\n"
        "check('offset_coercion',()=>Math.abs(sample([{width:'20px',offset:'0'},{width:'100px',offset:'1'}],500)-60)<.01);\n"
        "check('duplicate_offset',()=>Math.abs(sample([{width:'20px',offset:0},{width:'40px',offset:0},{width:'100px',offset:1}],500)-70)<.01);\n"
        "check('empty',()=>{reset();return !!box.animate([],{duration:1000})&&!!box.animate(null,{duration:1000})&&!!box.animate({},{duration:1000});});\n"
        "check('primitive_frame',()=>rejects([3]));\n"
        "check('invalid_easing',()=>rejects([{width:'20px',easing:'bogus'}]));\n"
        "check('unused_easing',()=>rejects({width:['20px'],easing:['linear','bogus']}));\n"
        "check('invalid_composite',()=>rejects([{width:'20px',composite:'ADD'}]));\n"
        "check('symbol_value',()=>rejects([{width:Symbol('bad')} ]));\n"
        "check('thrown_getter',()=>{var error={marker:1};try{sample([{get width(){throw error;}}],500);return false;}catch(e){return e===error;}});\n"
        "check('thrown_conversion',()=>{var error={marker:2};try{sample([{width:{toString(){throw error;}}}],500);return false;}catch(e){return e===error;}});\n"
        "check('numeric_opacity',()=>{reset();var a=box.animate([{opacity:0},{opacity:1}],{duration:1000,fill:'both'});a.pause();a.currentTime=500;return Math.abs(Number(getComputedStyle(box).opacity)-.5)<.01;});\n"
        "check('enumerable_names',()=>{var f={width:'100px'};Object.defineProperty(f,'height',{enumerable:false,get(){throw 4;}});return Math.abs(sample([f],500)-70)<.01;});\n"
        "check('name_order',()=>{var order='';var f={get width(){order+='w';return '100px';},get opacity(){order+='p';return 1;},get height(){order+='h';return '10px';},get offset(){order+='o';return 1;},get easing(){order+='e';return 'linear';},get composite(){order+='c';return 'replace';}};sample([f],500);return order==='ceohpw';});\n"
        "check('iterable_frames',()=>{var reads=0,steps=0;var frames={get [Symbol.iterator](){reads++;return function(){return {next(){return steps++<2?{value:{width:steps===1?'20px':'100px'},done:false}:{done:true};}};};}};return Math.abs(sample(frames,500)-60)<.01&&reads===1;});\n"
        "check('array_iterator',()=>{var frames=[{width:'bad'}];frames[Symbol.iterator]=function*(){yield {width:'20px'};yield {width:'100px'};};return Math.abs(sample(frames,500)-60)<.01;});\n"
        "check('indexed_iterable',()=>{var values=new Set(['20px','100px']);return Math.abs(sample({width:values},500)-60)<.01;});\n"
        "check('interleaved_conversion',()=>{var order='';var values={[Symbol.iterator](){var i=0;return {next(){order+='n';return i++<2?{value:{toString(){order+='s';return '20px';}},done:false}:{done:true};}};}};sample({width:values},500);return order==='nsnsn';});\n"
        "check('iterator_error',()=>{var closed=false,error={};var values={[Symbol.iterator](){return {next(){return {value:{toString(){throw error;}},done:false};},return(){closed=true;return {};}};}};try{sample({width:values},500);return false;}catch(e){return e===error&&!closed;}});\n"
        "check('effect_snapshot',()=>{reset();var reads=0,f=[{get width(){reads++;return '20px';}},{width:'100px'}];var effect=new KeyframeEffect(box,f,{duration:1000,fill:'both'});var immediate=reads===1;f[1].width='200px';var a=new Animation(effect);a.pause();a.currentTime=500;return immediate&&reads===1&&Math.abs(box.getBoundingClientRect().width-60)<.01;});\n"
        "\n"
        "check('effect_timing_snapshot',()=>{reset();var reads=0,o={get duration(){reads++;return 1000;},fill:'both'};var effect=new KeyframeEffect(box,[{width:'20px'},{width:'100px'}],o);var immediate=reads===1;var a=new Animation(effect);a.pause();a.currentTime=500;return immediate&&reads===1&&Math.abs(box.getBoundingClientRect().width-60)<.01;});\n"
        "check('effect_eager_error',()=>{reset();try{new KeyframeEffect(box,[{offset:-1,width:'20px'}],1000);return false;}catch(e){return e.name==='TypeError';}});\n"
        "check('timing_error',()=>{reset();try{box.animate([{width:'100px'}],{duration:-1});return false;}catch(e){return e.name==='TypeError';}});\n"
        "check('timing_getter_error',()=>{reset();var error={};try{box.animate([{width:'100px'}],{get duration(){throw error;}});return false;}catch(e){return e===error;}});\n"
        "check('nullable_offsets',()=>Math.abs(sample({width:['20px','60px','100px'],offset:[0,undefined,1]},500)-60)<.01);\n"
        "check('capture_next_once',()=>{var reads=0,i=0;var frames={[Symbol.iterator](){return {get next(){reads++;return function(){return i++<2?{value:{width:i===1?'20px':'100px'},done:false}:{done:true};};}};}};return Math.abs(sample(frames,500)-60)<.01&&reads===1;});\n"
        "\n"
        "function collect(){if(typeof gc==='function')gc();}\n"
        "check('getter_collection',()=>{var f=[{get width(){collect();return {toString(){collect();return '20px';}};},get opacity(){collect();return 0;}},{get width(){collect();return '100px';},get opacity(){collect();return 1;}}];return Math.abs(sample(f,500)-60)<.01&&Math.abs(Number(getComputedStyle(box).opacity)-.5)<.01;});\n"
        "check('iterator_collection',()=>{var i=0,frames={[Symbol.iterator](){collect();return {get next(){collect();return function(){collect();return i++<2?{value:{width:i===1?'20px':'100px'},done:false}:{done:true};};}};}};return Math.abs(sample(frames,500)-60)<.01;});\n"
        "check('snapshot_collection',()=>{reset();var effect=new KeyframeEffect(box,[{width:'20px'},{width:'100px'}],{duration:1000,fill:'both'});collect();var a=new Animation(effect);collect();a.pause();a.currentTime=500;collect();return Math.abs(box.getBoundingClientRect().width-60)<.01;});\n";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output,
        "anchored:true\n"
        "single:true\n"
        "single_indexed:true\n"
        "uneven_indexed:true\n"
        "shared_offsets:true\n"
        "frame_easing:true\n"
        "indexed_easing:true\n"
        "large:true\n"
        "negative_offset:true\n"
        "descending_offset:true\n"
        "nonfinite_offset:true\n"
        "offset_coercion:true\n"
        "duplicate_offset:true\n"
        "empty:true\n"
        "primitive_frame:true\n"
        "invalid_easing:true\n"
        "unused_easing:true\n"
        "invalid_composite:true\n"
        "symbol_value:true\n"
        "thrown_getter:true\n"
        "thrown_conversion:true\n"
        "numeric_opacity:true\n"
        "enumerable_names:true\n"
        "name_order:true\n"
        "iterable_frames:true\n"
        "array_iterator:true\n"
        "indexed_iterable:true\n"
        "interleaved_conversion:true\n"
        "iterator_error:true\n"
        "effect_snapshot:true\n"
        "effect_timing_snapshot:true\n"
        "effect_eager_error:true\n"
        "timing_error:true\n"
        "timing_getter_error:true\n"
        "nullable_offsets:true\n"
        "capture_next_once:true\n"
        "getter_collection:true\n"
        "iterator_collection:true\n"
        "snapshot_collection:true\n");
}

TEST(RenderOutputParity, AnimationWebMultiplePropertiesReachSizingTransformAndOpacityPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* frames[] = {
        "[{opacity:0,transform:'translateX(0px)',width:'20px',height:'10px'},"
        "{opacity:1,transform:'translateX(40px)',width:'100px',height:'30px'}]",
        "{opacity:[0,1],transform:['translateX(0px)','translateX(40px)'],"
        "width:['20px','100px'],height:['10px','30px']}",
    };
    for (int index = 0; index < 3; index++) {
        char html_path[256], events_path[256];
        snprintf(html_path, sizeof(html_path),
            "temp/render_output_parity/animation_web_multiple_%d.html", index);
        snprintf(events_path, sizeof(events_path),
            "temp/render_output_parity/animation_web_multiple_%d.json", index);
        StrBuf* html = strbuf_new();
        strbuf_append_str(html,
            "<!doctype html><style>body{margin:0;background:white}"
            "#box{position:absolute;width:40px;height:10px;background:red}"
            "button{position:absolute;top:50px;width:80px;height:20px}"
            "#start{left:0}#seek{left:90px}</style><div id=box></div>"
            "<button id=start onclick=\"var b=document.getElementById('box');window.motion=");
        strbuf_append_format(html, index == 2
            ? "new Animation(new KeyframeEffect(b,%s,{duration:1000,fill:'both'}))"
            : "b.animate(%s,{duration:1000,fill:'both'})", frames[index % 2]);
        strbuf_append_str(html,
            ";window.motion.pause();window.motion.currentTime=500\">start</button>"
            "<button id=seek onclick=\"window.motion.currentTime=750\">seek</button>");
        StrBuf* events = strbuf_new();
        strbuf_append_format(events,
            "{\"name\":\"Multiple Web keyframe properties\",\"html\":\"%s\","
            "\"viewport\":{\"width\":200,\"height\":80},\"events\":[", html_path);
        strbuf_append_str(events,
            "{\"type\":\"click\",\"x\":30,\"y\":60},"
            "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#box\"},"
            "\"x\":20,\"y\":0,\"width\":60,\"height\":20,\"tolerance\":0.1},"
            "{\"type\":\"assert_pixel\",\"x\":25,\"y\":5,\"min_r\":250,"
            "\"min_g\":120,\"max_g\":135,\"min_b\":120,\"max_b\":135},"
            "{\"type\":\"assert_pixel\",\"x\":85,\"y\":5,"
            "\"min_r\":250,\"min_g\":250,\"min_b\":250},"
            "{\"type\":\"click\",\"x\":120,\"y\":60},"
            "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#box\"},"
            "\"x\":30,\"y\":0,\"width\":80,\"height\":25,\"tolerance\":0.1},"
            "{\"type\":\"assert_pixel\",\"x\":35,\"y\":5,\"min_r\":250,"
            "\"min_g\":55,\"max_g\":70,\"min_b\":55,\"max_b\":70},"
            "{\"type\":\"assert_pixel\",\"x\":115,\"y\":5,"
            "\"min_r\":250,\"min_g\":250,\"min_b\":250}]}");
        bool passed = run_html_fixture_view(html_path, events_path, html->str, events->str);
        strbuf_free(events);
        strbuf_free(html);
        EXPECT_TRUE(passed) << "keyframe form " << index;
    }
}

TEST(RenderOutputParity, AnimationImportantDeclarationsReachGeometryAndPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const struct {
        const char* id;
        const char* base;
        const char* from;
        const char* to;
        float x, y, width, height;
        bool green;
    } cases[] = {
        {"width", "width:40px!important", "width:20px", "width:100px", 0,0,40,10,false},
        {"height", "height:10px!important", "height:0px", "height:40px", 0,50,40,10,false},
        {"margin", "margin:2px!important", "margin-left:0px", "margin-left:20px", 2,102,40,10,false},
        {"padding", "padding:2px!important", "padding-left:0px", "padding-left:20px", 0,152,44,14,false},
        {"border", "border:2px solid red!important", "border-left-width:2px", "border-left-width:10px", 0,202,44,14,false},
        {"background", "background:green!important", "background-color:red", "background-color:blue", 0,252,40,10,true},
        {"logical", "margin-inline-start:2px!important", "margin-left:0px", "margin-left:20px", 2,302,40,10,false},
        {"all", "all:initial!important;display:block!important;height:10px!important;background:red!important",
            "width:20px", "width:100px", 0,352,200,10,false},
        {"normal", "", "width:20px", "width:100px", 0,402,60,10,false},
        {"inherited", "width:inherit", "width:20px", "width:100px", 0,452,60,10,false},
        {"endpoint", "", "width:20px!important", "width:100px", 0,502,70,10,false}
    };
    StrBuf* html = strbuf_new();
    strbuf_append_str(html, "<!doctype html><style>body{margin:0;background:white}"
        ".row{width:200px;height:50px}.box{width:40px;height:10px;background:red}");
    for (const auto& box : cases) {
        strbuf_append_format(html, "@keyframes k_%s{from{%s}to{%s}}"
            "#%s{%s;animation:k_%s 1s linear -.5s both paused!important}",
            box.id, box.from, box.to, box.id, box.base, box.id);
    }
    strbuf_append_str(html, "#r_inherited{width:200px!important}</style>");
    for (const auto& box : cases)
        strbuf_append_format(html, "<div class=row id=r_%s><div class=box id=%s></div></div>", box.id, box.id);
    const char* svg = "temp/render_output_parity/animation_priority.svg";
    bool rendered = render_html_fixture("temp/render_output_parity/animation_priority.html", svg, html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    for (const auto& box : cases) {
        char expected[200];
        snprintf(expected, sizeof(expected),
            "x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"rgb(%s)\"",
            (double)box.x, (double)box.y, (double)box.width, (double)box.height,
            box.green ? "0,128,0" : "255,0,0");
        EXPECT_TRUE(file_contains_text(svg, expected)) << box.id;
    }
}

TEST(RenderOutputParity, AnimationNeutralEndpointsAndPropertyIntervalsReachPaintAndCssom) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const struct {
        const char* id;
        const char* base;
        const char* rule;
        const char* background;
        float width;
    } cases[] = {
        {"opacity_to", "opacity:.2", "to{opacity:1}", "rgb(0,128,0)", 40},
        {"opacity_from", "opacity:.8", "from{opacity:0}", "rgb(0,128,0)", 40},
        {"background_to", "", "to{background-color:blue}", "rgb(0,64,128)", 40},
        {"background_from", "", "from{background-color:red}", "rgb(128,64,0)", 40},
        {"separate", "opacity:.8", "from{opacity:0}to{background-color:blue}", "rgb(0,64,128)", 40},
        {"sparse", "opacity:.8", "0%{opacity:0}25%{width:40px}75%{width:60px}100%{opacity:1}", "rgb(0,128,0)", 50},
        {"interior", "", "25%{width:20px}", "rgb(0,128,0)", 26.67f},
        {"add_to", "", "to{width:20px;animation-composition:add}", "rgb(0,128,0)", 50},
        {"mixed", "", "from{width:10px;animation-composition:add}to{width:20px}", "rgb(0,128,0)", 35},
        {"transparent", "background:transparent", "to{background-color:blue}", "rgba(0,0,255,0.502)", 40},
        {"transform_none", "", "to{transform:translateX(20px)}", "rgb(0,128,0)", 40},
        {"transform_base", "transform:translateX(10px)", "to{transform:translateX(20px)}", "rgb(0,128,0)", 40},
        {"duplicate", "", "from{width:20px}50%{width:50px}50%{width:60px}to{width:100px}", "rgb(0,128,0)", 60},
        {"timing", "", "from{width:0px}50%{width:80px;animation-timing-function:ease-out}to{width:100px}", "rgb(0,128,0)", 93.69f},
        {"color_to", "", "to{color:blue}", "rgb(0,128,0)", 40},
        {"color_default", "color:initial", "to{color:blue}", "rgb(0,128,0)", 40}
    };
    StrBuf* html = strbuf_new();
    strbuf_append_str(html, "<!doctype html><style>body{margin:0;background:white}"
        ".row{width:200px;height:50px}.box{width:40px;height:10px;background:green;color:green}");
    for (const auto& box : cases) {
        bool interval_timing = strcmp(box.id, "timing") == 0;
        strbuf_append_format(html, "@keyframes k_%s{%s}#%s{%s;animation:k_%s 1s %s %s both paused}",
            box.id, box.rule, box.id, box.base, box.id,
            interval_timing ? "ease-in" : "linear", interval_timing ? "-.75s" : "-.5s");
    }
    strbuf_append_str(html, "</style>");
    for (const auto& box : cases)
        strbuf_append_format(html, "<div class=row><div class=box id=%s>X</div></div>", box.id);
    const char* html_path = "temp/render_output_parity/animation_neutral.html";
    const char* svg = "temp/render_output_parity/animation_neutral.svg";
    bool rendered = render_html_fixture(html_path, svg, html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char expected[200];
        snprintf(expected, sizeof(expected),
            "x=\"0.00\" y=\"%.2f\" width=\"%.2f\" height=\"10.00\" fill=\"%s\"",
            (double)index * 50.0, (double)cases[index].width, cases[index].background);
        EXPECT_TRUE(file_contains_text(svg, expected)) << cases[index].id;
    }
    EXPECT_TRUE(file_contains_text(svg, "opacity=\"0.6000\""));
    EXPECT_TRUE(file_contains_text(svg, "opacity=\"0.4000\""));
    EXPECT_TRUE(file_contains_text(svg, "opacity=\"0.5000\""));
    EXPECT_TRUE(file_contains_text(svg, "transform=\"matrix(1 0 0 1 10 0)\""));
    EXPECT_TRUE(file_contains_text(svg, "transform=\"matrix(1 0 0 1 15 0)\""));
    EXPECT_TRUE(file_contains_text(svg, "font-size=\"16.00\" fill=\"rgb(0,64,128)\">X</text>"));
    EXPECT_TRUE(file_contains_text(svg, "font-size=\"16.00\" fill=\"rgb(0,0,128)\">X</text>"));

    const char* js_path = "temp/render_output_parity/animation_neutral.js";
    const char* script =
        "for(const id of ['opacity_to','opacity_from','separate','sparse'])"
        "console.log(getComputedStyle(document.getElementById(id)).opacity);"
        "for(const id of ['background_to','background_from','color_to','color_default']){"
        "const s=getComputedStyle(document.getElementById(id));"
        "console.log(id.startsWith('background')?s.backgroundColor:s.color);}";
    ASSERT_TRUE(write_file_all(js_path, script, strlen(script)));
    char qhtml[PATH_MAX + 8], qjs[PATH_MAX + 8], command[PATH_MAX * 2 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(js_path, qjs, sizeof(qjs));
    snprintf(command, sizeof(command), "%s js %s --document %s --no-log 2>&1",
        LAMBDA_EXE, qjs, qhtml);
    CommandResult result = run_command_capture(command);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output,
        "0.6\n0.4\n0.4\n0.5\nrgb(0, 64, 128)\nrgb(128, 64, 0)\nrgb(0, 64, 128)\nrgb(0, 0, 128)\n");
}

TEST(RenderOutputParity, AnimationTypedOpacityAndColorReachPaintAndCssom) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const struct { const char* id; const char* base; const char* rule; } cases[] = {
        {"percent", "", "from{opacity:0%}to{opacity:100%}"},
        {"variable", "--low:.2;--high:.8", "from{opacity:var(--low)}to{opacity:var(--high)}"},
        {"math", "", "from{opacity:calc(.2 + .1)}to{opacity:calc(.8 - .1)}"},
        {"hint", "", "from{opacity:calc(10% + 20%)}to{opacity:clamp(0%,90%,70%)}"},
        {"clipped_low", "", "from{opacity:-1}to{opacity:1}"},
        {"clipped_high", "", "from{opacity:0}to{opacity:2}"},
        {"color_var", "--start:red;--end:blue", "from{color:var(--start)}to{color:var(--end)}"},
        {"color_current", "", "from{color:currentColor}to{color:blue}"},
        {"color_modern", "", "from{color:rgb(255 0 0 / .5)}to{color:rgb(0 0 255 / .5)}"},
        {"ordinary_math", "opacity:calc(20% + 30%)", ""},
        {"ordinary_invalid", "opacity:.4;opacity:.5 auto", ""}
    };
    StrBuf* html = strbuf_new();
    strbuf_append_str(html, "<!doctype html><style>body{margin:0;background:white}"
        ".row{width:200px;height:50px}.box{width:40px;height:10px;background:green;color:green}");
    for (const auto& box : cases) {
        strbuf_append_format(html, "#%s{%s}", box.id, box.base);
        if (*box.rule) strbuf_append_format(html,
            "@keyframes k_%s{%s}#%s{animation:k_%s 1s linear -.5s both paused}",
            box.id, box.rule, box.id, box.id);
    }
    strbuf_append_str(html, "</style>");
    for (const auto& box : cases)
        strbuf_append_format(html, "<div class=row><div class=box id=%s>X</div></div>", box.id);
    const char* html_path = "temp/render_output_parity/animation_typed_scalar_color.html";
    const char* svg = "temp/render_output_parity/animation_typed_scalar_color.svg";
    bool rendered = render_html_fixture(html_path, svg, html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    EXPECT_TRUE(file_contains_text(svg, "opacity=\"0.5000\""));
    EXPECT_TRUE(file_contains_text(svg, "opacity=\"0.4000\""));
    const char* colors[] = {"rgb(128,0,128)", "rgb(0,0,128)", "rgba(128,0,128,0.502)"};
    for (const char* color : colors) {
        char expected[160];
        snprintf(expected, sizeof(expected), "font-size=\"16.00\" fill=\"%s\">X</text>", color);
        EXPECT_TRUE(file_contains_text(svg, expected)) << color;
    }
    const char* js_path = "temp/render_output_parity/animation_typed_scalar_color.js";
    const char* script =
        "for(const id of ['percent','variable','math','hint','clipped_low','clipped_high',"
        "'ordinary_math','ordinary_invalid'])console.log(getComputedStyle(document.getElementById(id)).opacity);"
        "for(const id of ['color_var','color_current','color_modern'])"
        "console.log(getComputedStyle(document.getElementById(id)).color);";
    ASSERT_TRUE(write_file_all(js_path, script, strlen(script)));
    char qhtml[PATH_MAX + 8], qjs[PATH_MAX + 8], command[PATH_MAX * 2 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(js_path, qjs, sizeof(qjs));
    snprintf(command, sizeof(command), "%s js %s --document %s --no-log 2>&1",
        LAMBDA_EXE, qjs, qhtml);
    CommandResult result = run_command_capture(command);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "0.5\n0.5\n0.5\n0.5\n0.5\n0.5\n0.5\n0.4\n"
        "rgb(128, 0, 128)\nrgb(0, 0, 128)\nrgba(128, 0, 128, 0.5)\n");
}

TEST(RenderOutputParity, AnimationTypedTransformsReachPaintAndComputedMatrices) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const struct { const char* id; const char* base; const char* rule; float x; float scale = 1.0f; } cases[] = {
        {"percent", "", "from{transform:translateX(0%)}to{transform:translateX(100%)}", 20},
        {"em", "font-size:10px", "from{transform:translateX(0em)}to{transform:translateX(4em)}", 20},
        {"variable", "--end:40px", "from{transform:translateX(0px)}to{transform:translateX(var(--end))}", 20},
        {"scale", "", "from{transform:scale(50%)}to{transform:scale(150%)}", 0},
        {"mixed", "", "from{transform:translateX(10px)}to{transform:translateX(100%)}", 25},
        {"inches", "", "from{transform:translateX(1in)}to{transform:translateX(2in)}", 144},
        {"ordinary", "transform:translateX(50%);transform:translateX(1deg)", "", 20},
        {"chain", "transform:translateX(50%) scale(2);transform-origin:0 0", "", 20, 2},
        {"depth", "transform:translateZ(20px)", "", 0}
    };
    StrBuf* html = strbuf_new();
    strbuf_append_str(html, "<!doctype html><style>body{margin:0;background:white}"
        ".row{width:200px;height:50px}.box{width:40px;height:10px;background:red}");
    for (const auto& box : cases) {
        strbuf_append_format(html, "#%s{%s}", box.id, box.base);
        if (*box.rule) strbuf_append_format(html, "@keyframes k_%s{%s}"
            "#%s{animation:k_%s 1s linear -.5s both paused}", box.id, box.rule, box.id, box.id);
    }
    strbuf_append_str(html, "</style>");
    for (const auto& box : cases)
        strbuf_append_format(html, "<div class=row><div class=box id=%s></div></div>", box.id);
    const char* html_path = "temp/render_output_parity/animation_typed_transform.html";
    const char* svg = "temp/render_output_parity/animation_typed_transform.svg";
    bool rendered = render_html_fixture(html_path, svg, html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    float row_y = 0.0f;
    for (const auto& box : cases) {
        float paint_y = row_y * (1.0f - box.scale);
        row_y += 50.0f;
        if (box.x == 0.0f) continue;
        char expected[128];
        snprintf(expected, sizeof(expected), "transform=\"matrix(%.6g 0 0 %.6g %.6g %.6g)\"",
            (double)box.scale, (double)box.scale, (double)box.x, (double)paint_y);
        EXPECT_TRUE(file_contains_text(svg, expected)) << box.id;
    }
    const char* js_path = "temp/render_output_parity/animation_typed_transform.js";
    const char* script = "var ids=['percent','em','variable','scale','mixed','inches','ordinary','chain','depth'];"
        "for(const id of ids)console.log(getComputedStyle(document.getElementById(id)).transform);"
        "for(const id of ids){var r=document.getElementById(id).getBoundingClientRect();"
        "console.log(r.x+','+r.y+','+r.width+','+r.height);}";
    ASSERT_TRUE(write_file_all(js_path, script, strlen(script)));
    char qhtml[PATH_MAX + 8], qjs[PATH_MAX + 8], command[PATH_MAX * 2 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(js_path, qjs, sizeof(qjs));
    snprintf(command, sizeof(command), "%s js %s --document %s --no-log 2>&1", LAMBDA_EXE, qjs, qhtml);
    CommandResult result = run_command_capture(command);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output,
        "matrix(1, 0, 0, 1, 20, 0)\nmatrix(1, 0, 0, 1, 20, 0)\nmatrix(1, 0, 0, 1, 20, 0)\n"
        "matrix(1, 0, 0, 1, 0, 0)\nmatrix(1, 0, 0, 1, 25, 0)\nmatrix(1, 0, 0, 1, 144, 0)\n"
        "matrix(1, 0, 0, 1, 20, 0)\nmatrix(2, 0, 0, 2, 20, 0)\n"
        "matrix3d(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 20, 1)\n"
        "20,0,40,10\n20,50,40,10\n20,100,40,10\n0,150,40,10\n25,200,40,10\n"
        "144,250,40,10\n20,300,40,10\n20,350,80,20\n0,400,40,10\n");
}

struct TransformAnimationFixtureCase {
    const char* id; const char* from; const char* to; const char* matrix; const char* rect;
};

static void check_transform_animation_fixture(const TransformAnimationFixtureCase* cases, size_t count,
                                               const char* stem, const char* const* paint, size_t paint_count) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    StrBuf* html = strbuf_new();
    StrBuf* script = strbuf_new();
    StrBuf* expected = strbuf_new();
    strbuf_append_str(html, "<!doctype html><style>body{margin:0;background:white}.row{width:200px;height:50px}"
        ".box{width:40px;height:10px;background:red;font-size:10px;transform-origin:0 0}");
    strbuf_append_str(script, "function near(actual,expected){return actual.length===expected.length&&"
        "expected.every(function(value,index){return Math.abs(actual[index]-value)<.0001;});}var cases={");
    for (size_t index = 0; index < count; index++) {
        const TransformAnimationFixtureCase& box = cases[index];
        strbuf_append_format(html, "@keyframes k_%s{from{transform:%s}to{transform:%s}}"
            "#%s{animation:k_%s 1s linear -.5s both paused}", box.id, box.from, box.to, box.id, box.id);
        strbuf_append_format(script, "'%s':{matrix:%s,rect:%s},", box.id, box.matrix, box.rect);
        strbuf_append_format(expected, "%s:true:true\n", box.id);
    }
    strbuf_append_str(html, "</style>");
    for (size_t index = 0; index < count; index++)
        strbuf_append_format(html, "<div class=row><div class=box id=%s></div></div>", cases[index].id);
    strbuf_append_str(script, "};for(const id of Object.keys(cases)){var box=document.getElementById(id);"
        "var matrix=getComputedStyle(box).transform;var coefficients=matrix.slice(matrix.indexOf('(')+1,-1).split(',').map(Number);"
        "var rect=box.getBoundingClientRect();console.log(id+':'+near(coefficients,cases[id].matrix)+':'"
        "+near([rect.x,rect.y,rect.width,rect.height],cases[id].rect));}");
    char html_path[256], js_path[256], svg[256];
    snprintf(html_path, sizeof(html_path), "temp/render_output_parity/%s.html", stem);
    snprintf(js_path, sizeof(js_path), "temp/render_output_parity/%s.js", stem);
    snprintf(svg, sizeof(svg), "temp/render_output_parity/%s.svg", stem);
    bool rendered = render_html_fixture(html_path, svg, html->str);
    strbuf_free(html);
    ASSERT_TRUE(rendered);
    for (size_t index = 0; index < paint_count; index++) EXPECT_TRUE(file_contains_text(svg, paint[index])) << paint[index];
    CommandResult result = run_html_fixture_script(html_path, js_path, script->str);
    strbuf_free(script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, expected->str);
    strbuf_free(expected);
}

TEST(RenderOutputParity, AnimationTransformMathSpatialAndMatrixValuesReachPaintAndClientRects) {
    const TransformAnimationFixtureCase cases[] = {
        {"scale", "scale(calc(0%))", "scale(calc(100%))", "[.5,0,0,.5,0,0]", "[0,0,20,5]"},
        {"angle", "rotate(calc(0deg))", "rotate(calc(.25turn))", "[.7071068,.7071068,-.7071068,.7071068,0,0]", "[-7.0710678,50,35.3553391,35.3553391]"},
        {"translate3d", "translate3d(0px,0px,0px)", "translate3d(40px,0px,0px)", "[1,0,0,1,20,0]", "[20,100,40,10]"},
        {"scale3d", "scale3d(1,1,1)", "scale3d(3,3,3)", "[2,0,0,0,0,2,0,0,0,0,2,0,0,0,0,1]", "[0,150,80,20]"},
        {"rotatex", "rotateX(0deg)", "rotateX(90deg)", "[1,0,0,0,0,.7071068,.7071068,0,0,-.7071068,.7071068,0,0,0,0,1]", "[0,200,40,7.0710678]"},
        {"primitive", "translateX(10px)", "translateY(20px)", "[1,0,0,1,5,10]", "[5,260,40,10]"},
        {"matrix", "matrix(1,0,0,1,0,0)", "matrix(3,0,0,3,0,0)", "[2,0,0,2,0,0]", "[0,300,80,20]"},
        {"suffix", "rotate(0deg)", "translateX(40px)", "[1,0,0,1,20,0]", "[20,350,40,10]"},
        {"length", "translateX(calc(1em + 1in))", "translateX(calc(3em + 2in))", "[1,0,0,1,164,0]", "[164,400,40,10]"},
        {"axes", "scaleX(1)", "scaleY(3)", "[1,0,0,2,0,0]", "[0,450,40,20]"},
        {"prefix", "rotate(0deg) matrix(1,0,0,1,0,0)", "rotate(360deg) matrix(3,0,0,3,0,0)", "[-2,0,0,-2,0,0]", "[-80,480,80,20]"}
    };
    const char* paint[] = {"matrix(0.5 0 0 0.5 0 0)", "matrix(1 0 0 1 5 10)",
        "matrix(2 0 0 2 0 -150)", "matrix(1 0 0 0.707107 0 58.5786)", "matrix(1 0 0 1 164 0)"};
    check_transform_animation_fixture(cases, sizeof(cases) / sizeof(cases[0]), "animation_spatial_transform",
        paint, sizeof(paint) / sizeof(paint[0]));
}

TEST(RenderOutputParity, AnimationGeneral3dRotationPerspectiveAndMatrixValuesReachPaintAndClientRects) {
    const TransformAnimationFixtureCase cases[] = {
        {"axis", "rotate3d(1,1,0,0deg)", "rotate3d(1,1,0,90deg)", "[.85355339,.14644661,-.5,0,.14644661,.85355339,.5,0,.5,-.5,.70710678,0,0,0,0,1]", "[0,0,35.6066017,14.3933983]"},
        {"mixed_axes", "rotateX(90deg)", "rotateY(90deg)", "[.66666667,.33333333,-.66666667,0,.33333333,.66666667,.66666667,0,.66666667,-.66666667,.33333333,0,0,0,0,1]", "[0,50,30,20]"},
        {"normalized_axis", "rotate3d(1,0,0,30deg)", "rotate3d(2,0,0,90deg)", "[1,0,0,0,0,.5,.8660254,0,0,-.8660254,.5,0,0,0,0,1]", "[0,100,40,5]"},
        {"primitive_turn", "rotate(0deg)", "rotate3d(0,0,1,360deg)", "[-1,0,0,-1,0,0]", "[-40,140,40,10]"},
        {"perspective", "perspective(100px)", "perspective(200px)", "[1,0,0,0,0,1,0,0,0,0,1,-.0075,0,0,0,1]", "[0,200,40,10]"},
        {"perspective_none", "perspective(none)", "perspective(200px)", "[1,0,0,0,0,1,0,0,0,0,1,-.0025,0,0,0,1]", "[0,250,40,10]"},
        {"perspective_zero", "perspective(0px)", "perspective(200px)", "[1,0,0,0,0,1,0,0,0,0,1,-.5025,0,0,0,1]", "[0,300,40,10]"},
        {"matrix3d", "matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)", "matrix3d(1,0,0,0,0,1,0,0,0,0,3,0,0,0,0,1)", "[1,0,0,0,0,1,0,0,0,0,2,0,0,0,0,1]", "[0,350,40,10]"},
        {"spatial_suffix", "rotateX(0deg)", "translateZ(40px)", "[1,0,0,0,0,1,0,0,0,0,1,0,0,0,20,1]", "[0,400,40,10]"},
        {"matrix_tail", "matrix(1,0,0,1,0,0) rotate(0deg)", "matrix(3,0,0,3,0,0) rotate(360deg)", "[-2,0,0,-2,0,0]", "[-80,430,80,20]"},
        {"perspective_tail", "perspective(100px) rotateX(0deg)", "perspective(200px) rotateX(360deg)", "[1,0,0,0,0,-1,0,0,0,0,-1,.0075,0,0,0,1]", "[0,490,40,10]"},
        {"rotation_tail", "rotateX(90deg) rotate(0deg)", "rotateY(90deg) rotate(360deg)", "[-.66666667,-.33333333,.66666667,0,-.33333333,-.66666667,-.66666667,0,.66666667,-.66666667,.33333333,0,0,0,0,1]", "[-30,530,30,20]"},
        {"referencebox", "rotateX(0deg)", "translate3d(100%,50%,40px)", "[1,0,0,0,0,1,0,0,0,0,1,0,20,2.5,20,1]", "[20,602.5,40,10]"},
        {"perspective_depth", "perspective(100px) translateZ(40px)", "perspective(200px) translateZ(40px)", "[1,0,0,0,0,1,0,0,0,0,1,-.0075,0,0,40,.7]", "[0,650,57.1428571,14.2857143]"},
        {"collapsed", "matrix3d(-1,0,0,0,0,2,0,0,0,0,3,0,0,0,0,1)", "matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)", "[0,0,0,0,0,0,-.5,0,0,1,0,0,0,0,0,1]", "[0,700,0,0]"}
    };
    const char* paint[] = {"matrix(0.853553 0.146447 0.146447 0.853553 0 0)",
        "matrix(0.666667 0.333333 0.333333 0.666667 -16.6667 16.6667)",
        "matrix(1 0 0 0.5 0 50)", "matrix(1 0 0 -1 0 1000)", "matrix(1 0 0 1 20 2.5)",
        "matrix(1.42857 0 0 1.42857 0 -278.571)"};
    check_transform_animation_fixture(cases, sizeof(cases) / sizeof(cases[0]), "animation_general3d_transform",
        paint, sizeof(paint) / sizeof(paint[0]));
}

TEST(RenderOutputParity, AnimationPerspectiveDepthExportsTheAffineScaleAcrossBackends) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    if (!command_exists("sips") && !command_exists("pdftoppm"))
        GTEST_SKIP() << "a PDF rasterizer is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_perspective_depth.html";
    const char* svg_path = "temp/render_output_parity/animation_perspective_depth.svg";
    const char* pdf_path = "temp/render_output_parity/animation_perspective_depth.pdf";
    const char* png_path = "temp/render_output_parity/animation_perspective_depth.png";
    const char* pdf_png = "temp/render_output_parity/animation_perspective_depth_pdf.png";
    const char* html = "<!doctype html><style>body{margin:0;background:white;height:50px}"
        "#box{position:absolute;left:10px;top:10px;width:40px;height:10px;background:red;"
        "transform-origin:0 0;animation:depth 1s linear -.5s both paused}"
        "@keyframes depth{from{transform:perspective(100px) translateZ(40px)}"
        "to{transform:perspective(200px) translateZ(40px)}}</style><div id=box></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path, "matrix(1.42857 0 0 1.42857 -4.28571 -4.28571)"));
    ASSERT_TRUE(render_html_fixture(html_path, png_path, html));
    ASSERT_TRUE(render_html_fixture(html_path, pdf_path, html));
    ASSERT_TRUE(rasterize_fixture_pdf(pdf_path, pdf_png));
    const char* images[] = {png_path, pdf_png};
    const struct { int x, y; bool red; } samples[] = {{20,12,true}, {60,12,true}, {20,22,true}, {70,12,false}, {20,26,false}};
    for (const char* path : images) {
        SCOPED_TRACE(path);
        ImageData image = {};
        ASSERT_TRUE(load_png_rgba(path, &image));
        ASSERT_GT(image.width, 70);
        ASSERT_GT(image.height, 26);
        for (const auto& sample : samples) {
            const unsigned char* pixel = image.pixels + ((size_t)sample.y * image.width + sample.x) * 4;
            // pdf rasterizers color-manage device RGB; geometry depends on the occupied color region.
            if (sample.red) { EXPECT_GT(pixel[0], pixel[1] * 3); EXPECT_GT(pixel[0], pixel[2] * 3); }
            else { EXPECT_GT(pixel[0], 240); EXPECT_GT(pixel[1], 240); EXPECT_GT(pixel[2], 240); }
        }
        image_free(image.pixels);
    }
}

TEST(RenderOutputParity, AnimationWebSpatialSuffixesAndPerspectiveRefreshAfterSeekingAndSizing) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_web_general3d_transform.html";
    const char* js_path = "temp/render_output_parity/animation_web_general3d_transform.js";
    const char* html = "<!doctype html><style>body{margin:0}.box{width:40px;height:10px;font-size:10px;"
        "transform-origin:0 0;--end:calc(100% - 1em)}</style><div class=box id=box></div>"
        "<div class=box id=perspective></div>";
    const char* script = "var box=document.getElementById('box');"
        "var motion=box.animate([{transform:'rotateX(0deg)'},{transform:'translate3d(var(--end),50%,40px)'}],"
        "{duration:1000,fill:'both',easing:'linear'});motion.pause();motion.currentTime=500;"
        "function values(element){var value=getComputedStyle(element).transform;"
        "return value.slice(value.indexOf('(')+1,-1).split(',').map(Number);}"
        "function check(x,y,z,width,height){var matrix=values(box);var rect=box.getBoundingClientRect();"
        "console.log('suffix:'+([matrix[12]-x,matrix[13]-y,matrix[14]-z,rect.x-x,rect.y-y,"
        "rect.width-width,rect.height-height].every(function(error){return Math.abs(error)<.0001;})));}"
        "check(15,2.5,20,40,10);box.style.cssText+='width:80px;height:40px';check(35,10,20,80,40);"
        "box.style.fontSize='20px';check(30,10,20,80,40);box.style.setProperty('--end','min(100%,30px)');"
        "check(15,10,20,80,40);motion.currentTime=750;check(22.5,15,30,80,40);"
        "var depth=document.getElementById('perspective');var effect=depth.animate("
        "[{transform:'perspective(100px) translateZ(40px)'},{transform:'perspective(200px) translateZ(40px)'}],"
        "{duration:1000,fill:'both',easing:'linear'});effect.pause();effect.currentTime=500;"
        "function checkDepth(coefficient,width,height){var matrix=values(depth);var rect=depth.getBoundingClientRect();"
        "console.log('perspective:'+([matrix[11]-coefficient,rect.width-width,rect.height-height]"
        ".every(function(error){return Math.abs(error)<.0001;})));}"
        "checkDepth(-.0075,57.1428571,14.2857143);depth.style.width='80px';"
        "checkDepth(-.0075,114.2857143,14.2857143);effect.currentTime=750;"
        "checkDepth(-.00625,106.6666667,13.3333333);";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "suffix:true\nsuffix:true\nsuffix:true\nsuffix:true\nsuffix:true\n"
        "perspective:true\nperspective:true\nperspective:true\n");
}

TEST(RenderOutputParity, CollapsedTransformsRetainZeroVisualDimensionsInBothRectApis) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/collapsed_transform_rect.html";
    const char* js_path = "temp/render_output_parity/collapsed_transform_rect.js";
    const char* html = "<!doctype html><style>body{margin:0}.row{width:200px;height:50px}"
        ".box{width:40px;height:10px;transform-origin:0 0}#x{transform:scaleX(0)}"
        "#both{transform:scale(0)}#edge{transform:rotateY(90deg)}"
        "#matrix{transform:matrix3d(0,0,0,0,0,0,-.5,0,0,1,0,0,0,0,0,1)}</style>"
        "<div class=row><div class=box id=x></div></div><div class=row><div class=box id=both></div></div>"
        "<div class=row><div class=box id=edge></div></div><div class=row><div class=box id=matrix></div></div>";
    const char* script = "var cases={x:[0,0,0,10],both:[0,50,0,0],edge:[0,100,0,10],matrix:[0,150,0,0]};"
        "function near(rect,expected){var values=[rect.x,rect.y,rect.width,rect.height];"
        "return expected.every(function(value,index){return Math.abs(value-values[index])<.0001;});}"
        "for(const id of Object.keys(cases)){var box=document.getElementById(id);var rect=box.getBoundingClientRect();"
        "var rects=box.getClientRects();console.log(id+':'+near(rect,cases[id])+':'"
        "+(rects.length===1&&near(rects[0],cases[id])));}";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "x:true:true\nboth:true:true\nedge:true:true\nmatrix:true:true\n");
}

TEST(RenderOutputParity, AnimationPercentageTransformMathAndMatrixSuffixesReachUsedGeometry) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_referencebox_transform.html";
    const char* js_path = "temp/render_output_parity/animation_referencebox_transform.js";
    const char* svg = "temp/render_output_parity/animation_referencebox_transform.svg";
    const char* html = "<!doctype html><style>body{margin:0}.row{width:200px;height:50px}"
        ".box{width:40px;height:10px;background:red;font-size:10px;transform-origin:0 0}"
        "#ordinary{transform:translate(calc(10px + 50%),calc(5px + 50%))}"
        "@keyframes calc{from{transform:translateX(calc(10px + 25%))}to{transform:translateX(calc(30px + 75%))}}"
        "@keyframes nonlinear{from{transform:translateX(min(100%,30px))}to{transform:translateX(max(50%,10px))}}"
        "@keyframes spatial{from{transform:translate3d(0px,0px,0px)}to{transform:translate3d(calc(50% + 1em),calc(50% + 1em),0px)}}"
        "@keyframes suffix{from{transform:rotate(0deg)}to{transform:translateX(100%)}}"
        "@keyframes product{from{transform:translateX(calc(100% - 1em)) rotate(0deg)}to{transform:scale(2) translateX(50%)}}"
        "@keyframes grow{from{transform:rotate(0deg);width:20px}to{transform:translateX(100%);width:100px}}"
        "#calc{animation:calc 1s linear -.5s both paused}#nonlinear{animation:nonlinear 1s linear -.5s both paused}"
        "#spatial{animation:spatial 1s linear -.5s both paused}#suffix{animation:suffix 1s linear -.5s both paused}"
        "#product{animation:product 1s linear -.5s both paused}#grow{animation:grow 1s linear -.5s both paused}"
        "#zero{width:0;height:0;transform:translate(calc(1em + 100%),calc(5px + 100%))}</style>"
        "<div class=row><div class=box id=ordinary></div></div><div class=row><div class=box id=calc></div></div>"
        "<div class=row><div class=box id=nonlinear></div></div><div class=row><div class=box id=spatial></div></div>"
        "<div class=row><div class=box id=suffix></div></div><div class=row><div class=box id=product></div></div>"
        "<div class=row><div class=box id=grow></div></div><div class=row><div class=box id=zero></div></div>";
    const char* script = "var cases={ordinary:[30,10,40,10],calc:[40,50,40,10],nonlinear:[25,100,40,10],"
        "spatial:[15,157.5,40,10],suffix:[20,200,40,10],product:[35,250,60,15],grow:[30,300,60,10],zero:[10,355,0,0]};"
        "function check(id,expected){var box=document.getElementById(id);var matrix=getComputedStyle(box).transform;"
        "var values=matrix.slice(matrix.indexOf('(')+1,-1).split(',').map(Number);var rect=box.getBoundingClientRect();"
        "var actual=[rect.x,rect.y,rect.width,rect.height];var y=expected[1]-box.parentElement.offsetTop;"
        "console.log(id+':'+(Math.abs(values[4]-expected[0])<.0001&&Math.abs(values[5]-y)<.0001)+':'"
        "+expected.every(function(value,index){return Math.abs(value-actual[index])<.0001;}));}"
        "for(const id of Object.keys(cases))check(id,cases[id]);"
        "document.getElementById('ordinary').style.cssText+='width:80px;height:40px';check('ordinary',[50,25,80,40]);"
        "document.getElementById('product').style.cssText+='width:80px;font-size:20px';check('product',[70,250,120,15]);"
        "document.getElementById('calc').style.width='80px';check('calc',[60,50,80,10]);";
    ASSERT_TRUE(render_html_fixture(html_path, svg, html));
    const char* matrices[] = {"matrix(1 0 0 1 30 10)", "matrix(1 0 0 1 40 0)",
        "matrix(1 0 0 1 25 0)", "matrix(1.5 0 0 1.5 35 -125)", "matrix(1 0 0 1 30 0)"};
    for (const char* matrix : matrices) EXPECT_TRUE(file_contains_text(svg, matrix)) << matrix;
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "ordinary:true:true\ncalc:true:true\nnonlinear:true:true\nspatial:true:true\n"
        "suffix:true:true\nproduct:true:true\ngrow:true:true\nzero:true:true\nordinary:true:true\nproduct:true:true\ncalc:true:true\n");
}

TEST(RenderOutputParity, AnimationWebPercentageMathSuffixesRefreshAfterSizingAndVariableChanges) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_web_referencebox_transform.html";
    const char* js_path = "temp/render_output_parity/animation_web_referencebox_transform.js";
    const char* html = "<!doctype html><style>body{margin:0}#box{width:40px;height:10px;font-size:10px;"
        "--end:calc(100% - 1em)}</style><div id=box></div>";
    const char* script = "var box=document.getElementById('box');"
        "var motion=box.animate([{transform:'rotate(0deg)'},{transform:'translateX(var(--end))'}],"
        "{duration:1000,fill:'both',easing:'linear'});motion.pause();motion.currentTime=500;"
        "console.log(getComputedStyle(box).transform);box.style.width='80px';"
        "console.log(getComputedStyle(box).transform);box.style.fontSize='20px';"
        "console.log(getComputedStyle(box).transform);box.style.setProperty('--end','clamp(10px,50%,50px)');"
        "console.log(getComputedStyle(box).transform);motion.currentTime=750;"
        "console.log(getComputedStyle(box).transform);";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "matrix(1, 0, 0, 1, 15, 0)\nmatrix(1, 0, 0, 1, 35, 0)\n"
        "matrix(1, 0, 0, 1, 30, 0)\nmatrix(1, 0, 0, 1, 20, 0)\nmatrix(1, 0, 0, 1, 30, 0)\n");
}

TEST(RenderOutputParity, AnimationStandaloneWebTransformsRefreshUnitsVariablesAndReferenceBox) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_web_transform.html";
    const char* js_path = "temp/render_output_parity/animation_web_transform.js";
    const char* html = "<!doctype html><style>body{margin:0}#box{width:40px;height:10px;"
        "font-size:10px;--end:100%}</style><div id=box></div>";
    const char* script = "var box=document.getElementById('box');"
        "var motion=box.animate([{transform:'translateX(1em)'},{transform:'translateX(var(--end))'}],"
        "{duration:1000,fill:'both',easing:'linear'});motion.pause();motion.currentTime=500;"
        "console.log(getComputedStyle(box).transform);box.style.width='80px';"
        "console.log(getComputedStyle(box).transform);box.style.fontSize='20px';"
        "console.log(getComputedStyle(box).transform);box.style.setProperty('--end','50%');"
        "console.log(getComputedStyle(box).transform);motion.currentTime=750;"
        "console.log(getComputedStyle(box).transform);";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "matrix(1, 0, 0, 1, 25, 0)\nmatrix(1, 0, 0, 1, 45, 0)\n"
        "matrix(1, 0, 0, 1, 50, 0)\nmatrix(1, 0, 0, 1, 30, 0)\nmatrix(1, 0, 0, 1, 35, 0)\n");
}

TEST(RenderOutputParity, AnimationStandaloneWebTypedValuesRefreshOnStyleChanges) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/animation_web_scalar_color.html";
    const char* js_path = "temp/render_output_parity/animation_web_scalar_color.js";
    const char* html = "<!doctype html><style>body{margin:0}#box{width:40px;height:10px;"
        "opacity:.2;color:green;--low:20%;--high:80%;--start:red;--end:blue}</style><div id=box>X</div>";
    const char* script =
        "var box=document.getElementById('box');"
        "var fade=box.animate([{opacity:'var(--low)'},{opacity:'calc(var(--high) - 10%)'}],"
        "{duration:1000,fill:'both',easing:'linear'});fade.pause();fade.currentTime=500;"
        "var color=box.animate([{color:'var(--start)'},{color:'var(--end)'}],"
        "{duration:1000,fill:'both',easing:'linear'});color.pause();color.currentTime=500;"
        "console.log(getComputedStyle(box).opacity);console.log(getComputedStyle(box).color);"
        "box.style.setProperty('--low','40%');box.style.setProperty('--high','100%');"
        "box.style.setProperty('--start','black');"
        "console.log(getComputedStyle(box).opacity);console.log(getComputedStyle(box).color);"
        "fade.currentTime=750;color.currentTime=750;"
        "console.log(getComputedStyle(box).opacity);console.log(getComputedStyle(box).color);";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    CommandResult result = run_html_fixture_script(html_path, js_path, script);
    ASSERT_EQ(result.exit_code, 0) << result.output;
    EXPECT_STREQ(result.output, "0.45\nrgb(128, 0, 128)\n0.65\nrgb(0, 0, 128)\n"
        "0.775\nrgb(0, 0, 191)\n");
}

TEST(RenderOutputParity, AnimationLiveImportantChangesRefreshCssAndWebSampling) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}.row{width:200px;height:50px}"
        ".box{width:40px;height:10px;background:red}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "#css{animation:grow 1s linear -.5s both paused}"
        "button{position:absolute;left:0;width:100px;height:30px}"
        "#start{top:150px}#normal{top:210px}#important{top:270px}#seek{top:330px}</style>"
        "<div class=row><div class=box id=css style='width:40px!important'></div></div>"
        "<div class=row><div class=box id=web style='width:40px!important'></div></div>"
        "<button id=start>start</button><button id=normal>normal</button>"
        "<button id=important>important</button><button id=seek>seek</button><script>"
        "var css=document.getElementById('css'),web=document.getElementById('web'),motion;"
        "document.getElementById('start').onclick=function(){motion=web.animate("
        "[{width:'20px'},{width:'100px'}],{duration:1000,fill:'both'});"
        "motion.pause();motion.currentTime=500;};"
        "document.getElementById('normal').onclick=function(){"
        "css.style.setProperty('width','40px','');web.style.setProperty('width','40px','');};"
        "document.getElementById('important').onclick=function(){"
        "css.style.setProperty('width','80px','important');web.style.setProperty('width','80px','important');};"
        "document.getElementById('seek').onclick=function(){motion.currentTime=750;"
        "css.style.setProperty('width','40px','');web.style.setProperty('width','40px','');};</script>";
    StrBuf* events = strbuf_new();
    strbuf_append_str(events, "{\"name\":\"animation priority changes reach paint\","
        "\"html\":\"temp/render_output_parity/animation_priority_live.html\","
        "\"viewport\":{\"width\":300,\"height\":400},\"events\":[");
    const float widths[4][2] = {{40,40},{60,60},{80,80},{60,80}};
    for (int phase = 0; phase < 4; phase++) {
        strbuf_append_format(events, "%s{\"type\":\"click\",\"x\":30,\"y\":%d}",
            phase ? "," : "", 160 + phase * 60);
        for (int box = 0; box < 2; box++) {
            strbuf_append_format(events, ",{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#%s\"},"
                "\"x\":0,\"y\":%d,\"width\":%.2f,\"height\":10,\"tolerance\":0.1},"
                "{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%d,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
                "{\"type\":\"assert_pixel\",\"x\":%.2f,\"y\":%d,\"min_r\":240,\"min_g\":240,\"min_b\":240}",
                box ? "web" : "css", box * 50, (double)widths[phase][box],
                (double)widths[phase][box] - 2.0, box * 50 + 5,
                (double)widths[phase][box] + 2.0, box * 50 + 5);
        }
    }
    strbuf_append_str(events, "]}");
    bool passed = run_html_fixture_view("temp/render_output_parity/animation_priority_live.html",
        "temp/render_output_parity/animation_priority_live.json", html, events->str);
    strbuf_free(events);
    EXPECT_TRUE(passed);
}

TEST(RenderOutputParity, AnimationFrameTicksReflowAnimatedSizes) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "@keyframes grow{from{width:20px}to{width:100px}}"
        "#box{width:20px;height:10px;background:red;animation:grow 1s linear forwards}"
        "</style><div id=box></div>";
    const char* events =
        "{\"name\":\"animation frame geometry reaches paint\","
        "\"html\":\"temp/render_output_parity/animation_frame_geometry.html\","
        "\"viewport\":{\"width\":150,\"height\":40},\"events\":["
        "{\"type\":\"advance_time\",\"ms\":250},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":50,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"advance_time\",\"ms\":250},"
        "{\"type\":\"assert_pixel\",\"x\":55,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":70,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"advance_time\",\"ms\":500},"
        "{\"type\":\"assert_pixel\",\"x\":95,\"y\":5,\"min_r\":240,\"max_g\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":105,\"y\":5,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    ASSERT_TRUE(run_html_fixture_view(
        "temp/render_output_parity/animation_frame_geometry.html",
        "temp/render_output_parity/animation_frame_geometry.json", html, events));
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

TEST(RenderOutputParity, CustomPropertyCommaAndEmptyFallbackTokensReachPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/var_comma_empty.html";
    const char* svg_path = "temp/render_output_parity/var_comma_empty.svg";
    const char* html =
        "<!doctype html><style>body{margin:0}div{width:20px;height:10px}"
        "#a{--rgb:255,0,0;background:rgb(var(--rgb))}"
        "#b{--rg:0,255;background:rgb(var(--rg),0)}"
        "#c{background:rgb(var(--missing,0,0,255))}"
        "#d{background:purple;margin:5px var(--missing,) 10px}"
        "#e{--empty:;background:orange;margin:5px var(--empty,99px) 10px}"
        "</style><div id=a></div><div id=b></div><div id=c></div>"
        "<div id=d></div><div id=e></div>";
    ASSERT_TRUE(render_html_fixture(html_path, svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"0.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"10.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,255,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"20.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"10.00\" y=\"35.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(128,0,128)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "x=\"10.00\" y=\"50.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,165,0)\""));
}

TEST(RenderOutputParity, CustomPropertyCommasJoinSurroundingGradientStopTokens) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* literal_html = "temp/render_output_parity/var_gradient_literal.html";
    const char* variable_html = "temp/render_output_parity/var_gradient_comma.html";
    const char* literal_png = "temp/render_output_parity/var_gradient_literal.png";
    const char* variable_png = "temp/render_output_parity/var_gradient_comma.png";
    ASSERT_TRUE(render_html_fixture(literal_html, literal_png,
        "<!doctype html><style>body{margin:0}div{width:80px;height:40px;"
        "background:linear-gradient(to right,red 0%,blue 100%)}"
        "</style><div></div>"));
    ASSERT_TRUE(render_html_fixture(variable_html, variable_png,
        "<!doctype html><style>body{margin:0}div{width:80px;height:40px;"
        "--stops:0%,blue;background:linear-gradient(to right,"
        "red var(--stops) 100%)}"
        "</style><div></div>"));
    expect_pngs_exactly_equal(literal_png, variable_png);
}

TEST(RenderOutputParity, ComputedCustomPropertyNamesReachHtmlAndSvgPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* literal_png = "temp/render_output_parity/var_name_literal.png";
    const char* variable_png = "temp/render_output_parity/var_name_computed.png";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/var_name_literal.html", literal_png,
        "<!doctype html><style>body{margin:0}div{width:40px;height:20px;"
        "margin-left:6px;padding:2px;border:3px solid blue;background:red}"
        "svg{display:block}</style><div></div><svg width='32' height='18' "
        "xmlns='http://www.w3.org/2000/svg'><rect width='32' height='18' fill='lime'/></svg>"));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/var_name_computed.html", variable_png,
        "<!doctype html><style>body{margin:0}div{--width:40px;--width-name:--width;"
        "--height:20px;--height-name:--height;--color:red;--color-name:--color;"
        "--border:3px solid blue;--border-name:--border;--space:2px;--space-name:--space;"
        "width:var(var(--width-name));height:var(var(--height-name));"
        "margin-left:var(var(--absent,--missing),6px);padding:var(var(--space-name));"
        "border:var(var(--border-name));background:var(var(--color-name))}"
        "svg{display:block}g{--paint:lime;--paint-name:--paint;--alias:var(var(--paint-name))}"
        "rect{--paint:red;fill:var(--alias)}"
        "</style><div></div><svg width='32' height='18' xmlns='http://www.w3.org/2000/svg'>"
        "<g><rect width='32' height='18'/></g></svg>"));
    // identical pixels require both shorthand/layout substitution and declaration-owner SVG lookup.
    expect_pngs_exactly_equal(literal_png, variable_png);
}

TEST(RenderOutputParity, SvgInvalidPaintAndWidthSubstitutionsUseInheritedComputedValues) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* reference = "temp/render_output_parity/svg_computed_literal.png";
    const char* computed = "temp/render_output_parity/svg_computed_css.png";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/svg_computed_literal.html", reference,
        "<!doctype html><style>body{margin:0}svg{display:block}</style>"
        "<svg width='160' height='22' xmlns='http://www.w3.org/2000/svg'>"
        "<rect x='4' y='4' width='24' height='14' fill='green' stroke='blue' stroke-width='4'/>"
        "<rect x='44' y='4' width='24' height='14' fill='purple' stroke='red' stroke-width='4'/>"
        "<rect x='84' y='4' width='24' height='14' fill='red' stroke='none'/>"
        "<rect x='124' y='4' width='24' height='14' fill='none' stroke='lime' stroke-width='6'/></svg>"));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/svg_computed_css.html", computed,
        "<!doctype html><style>body{margin:0}svg{display:block}"
        "g{fill:green;stroke:currentColor;stroke-width:2em;font-size:2px;color:red}"
        ".invalid{fill:blue;fill:var(--paint);--paint:url(#missing) potato;color:blue;font-size:1px}"
        ".width{fill:purple;fill:url(#missing) potato;stroke-width:10px;"
        "stroke-width:var(--width);--width:-2}"
        ".fallback{fill:URL(#missing) RGB(255,0,0);stroke:NONE}"
        ".number{fill:none;stroke:lime;stroke-width:calc(2 * 3)}"
        "</style><svg width='160' height='22' xmlns='http://www.w3.org/2000/svg'><g>"
        "<rect class='invalid' x='4' y='4' width='24' height='14'/>"
        "<rect class='width' x='44' y='4' width='24' height='14'/>"
        "<rect class='fallback' x='84' y='4' width='24' height='14'/>"
        "<rect class='number' x='124' y='4' width='24' height='14'/></g></svg>"));
    // invalid computed winners inherit; parse-time invalid paint leaves the valid declaration in place.
    expect_pngs_exactly_equal(reference, computed);
}

TEST(RenderOutputParity, SvgPresentationMathKeywordsAndInheritedLengthsReachPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* literal_css =
        "body{fill-opacity:.5}.dash{stroke-dasharray:6px 4px;stroke-dashoffset:-2px;"
        "stroke-linecap:round;stroke-opacity:.5}.join{stroke-linejoin:bevel;stroke-miterlimit:0}"
        ".order{paint-order:stroke}.hole{fill-rule:evenodd}.clip{clip-rule:evenodd}"
        "stop{stop-color:green;stop-opacity:.5}feFlood{flood-color:blue;flood-opacity:.5}"
        "feDiffuseLighting{lighting-color:lime}g{font-size:2px;stroke-dasharray:4px 6px;stroke-dashoffset:4px}";
    const char* computed_css =
        "body{fill-opacity:calc(25% + 25%)}.dash{stroke-dasharray:calc(2 * 3) min(4px,5px);"
        "stroke-dashoffset:calc(2px - 4px);stroke-linecap:RoUnD;stroke-opacity:calc(25% + 25%)}"
        ".join{stroke-linejoin:MiTeR;stroke-miterlimit:calc(0 * 4)}"
        ".order{paint-order:StRoKe}.hole{fill-rule:EvEnOdD}.clip{clip-rule:EvEnOdD}"
        "stop{stop-color:rgb(0,128,0);stop-opacity:calc(25% + 25%)}"
        "feFlood{flood-color:rgb(0,0,255);flood-opacity:calc(25% + 25%)}"
        "feDiffuseLighting{lighting-color:rgb(0,255,0)}"
        "g{font-size:2px;stroke-dasharray:2em calc(2 * 3);stroke-dashoffset:2em}"
        ".inherited{font-size:1px;stroke-dasharray:1 1;stroke-dasharray:var(--bad);--bad:2 -1}";
    const char* geometry =
        "</style><svg width='240' height='140' xmlns='http://www.w3.org/2000/svg'><defs>"
        "<linearGradient id='grad'><stop offset='0'/><stop offset='1'/></linearGradient>"
        "<clipPath id='clip'><path class='clip' d='M100 50h40v40h-40z M110 60h20v20h-20z'/></clipPath>"
        "<filter id='flood'><feFlood/></filter><filter id='light'><feDiffuseLighting diffuseConstant='1'>"
        "<feDistantLight azimuth='90' elevation='90'/></feDiffuseLighting></filter></defs>"
        "<path class='dash' d='M10 12H90' fill='none' stroke='blue' stroke-width='6'/>"
        "<path class='join' d='M110 25L120 5L130 25' fill='none' stroke='red' stroke-width='8'/>"
        "<rect class='order' x='155' y='5' width='30' height='25' fill='red' stroke='lime' stroke-width='8'/>"
        "<path class='hole' d='M5 50h40v40h-40z M15 60h20v20h-20z' fill='purple'/>"
        "<rect x='100' y='50' width='40' height='40' fill='blue' clip-path='url(#clip)'/>"
        "<rect x='55' y='50' width='30' height='40' fill='url(#grad)'/>"
        "<rect x='150' y='50' width='30' height='40' filter='url(#flood)'/>"
        "<rect x='195' y='50' width='30' height='40' filter='url(#light)'/>"
        "<g><path class='inherited' d='M10 120H200' fill='none' stroke='green' stroke-width='4'/></g></svg>";
    const char* html_paths[] = {"temp/render_output_parity/svg_properties_literal.html",
        "temp/render_output_parity/svg_properties_css.html"};
    const char* png_paths[] = {"temp/render_output_parity/svg_properties_literal.png",
        "temp/render_output_parity/svg_properties_css.png"};
    const char* styles[] = {literal_css, computed_css};
    for (int index = 0; index < 2; index++) {
        StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
        strbuf_append_str(html, "<!doctype html><style>body{margin:0}svg{display:block}");
        strbuf_append_str(html, styles[index]); strbuf_append_str(html, geometry);
        bool rendered = render_html_fixture(html_paths[index], png_paths[index], html->str);
        strbuf_free(html); ASSERT_TRUE(rendered);
    }
    // exact pixels require computed math, canonical keywords and declaration-font inheritance to reach existing paint consumers.
    expect_pngs_exactly_equal(png_paths[0], png_paths[1]);
}

TEST(RenderOutputParity, SvgDashLengthsUseEachInstanceFontBeforeInheritance) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/svg_properties_use_literal.html",
        "temp/render_output_parity/svg_properties_use_literal.png",
        "<!doctype html><style>body{margin:0}svg{display:block}</style><svg width='220' height='70'>"
        "<path d='M10 20H200' fill='none' stroke='blue' stroke-width='4' stroke-dasharray='4 2' stroke-dashoffset='2'/>"
        "<path d='M10 50H200' fill='none' stroke='blue' stroke-width='4' stroke-dasharray='8 4' stroke-dashoffset='4'/></svg>"));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/svg_properties_use_css.html",
        "temp/render_output_parity/svg_properties_use_css.png",
        "<!doctype html><style>body{margin:0}svg{display:block}</style><svg width='220' height='70'>"
        "<defs><g id='units' stroke-dasharray='calc(2 * 1em) 1em' stroke-dashoffset='1em'>"
        "<path d='M10 0H200' fill='none' stroke='blue' stroke-width='4' font-size='1'/></g></defs>"
        "<use href='#units' y='20' font-size='2'/><use href='#units' y='50' font-size='4'/></svg>"));
    // the shared definition computes against each use host, and its child inherits lengths without using its own font.
    expect_pngs_exactly_equal("temp/render_output_parity/svg_properties_use_literal.png",
        "temp/render_output_parity/svg_properties_use_css.png");
}

TEST(RenderOutputParity, DeepInheritedComputedValuesDriveTextAndBoxPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "#source:lang(en){color:red;font-size:10px;line-height:2}"
        "#source:lang(fr){color:blue;font-size:20px;line-height:2}"
        "#swatch{width:17px;height:20px;background:red}</style>"
        "<div id=swatch></div><button id=update>update</button><div id=source lang=en></div>"
        "<script>document.getElementById('update').addEventListener('click',function(){"
        "var source=document.getElementById('source'),leaf=source;"
        "for(var depth=0;depth<128;depth++){var child=document.createElement('div');"
        "leaf.appendChild(child);leaf=child;}leaf.style.fontSize='2em';source.lang='fr';"
        "var live=getComputedStyle(leaf),swatch=document.getElementById('swatch');"
        "swatch.style.backgroundColor=live.color;swatch.style.width=live.fontSize;"
        "swatch.style.height=live.lineHeight;source.textContent='';});</script>";
    const char* events =
        "{\"name\":\"deep computed values drive paint\","
        "\"html\":\"temp/render_output_parity/cssom_deep_paint.html\","
        "\"viewport\":{\"width\":200,\"height\":140},\"events\":["
        "{\"type\":\"click\",\"target\":{\"selector\":\"#update\"}},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#swatch\"},"
        "\"property\":\"width\",\"equals\":\"40px\"},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#swatch\"},"
        "\"property\":\"height\",\"equals\":\"80px\"},"
        "{\"type\":\"assert_pixel\",\"x\":35,\"y\":75,\"min_b\":240,\"max_r\":20,\"max_g\":20},"
        "{\"type\":\"assert_pixel\",\"x\":45,\"y\":75,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    // inherited color, font and line-height independently determine the swatch's paint and bounds.
    ASSERT_TRUE(run_html_fixture_view("temp/render_output_parity/cssom_deep_paint.html",
        "temp/render_output_parity/cssom_deep_paint.json", html, events));
}

TEST(RenderOutputParity, LineHeightMathKeepsInheritedTypesAndAppliesZoomOnce) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        ".frame{position:absolute;top:0;width:40px;font-size:10px}"
        "#numbers{left:0;line-height:calc(2)}#lengths{left:60px;line-height:calc(2 * 1em)}"
        "#zoom_frame{position:absolute;left:120px;top:0}"
        "#zoom_owner{font-size:10px;line-height:calc(2 * 1em);zoom:2}"
        ".leaf{width:40px;font-size:20px;background:blue;text-indent:9999px;overflow:hidden}"
        "#length_leaf{background:green}#update{position:absolute;left:0;top:110px}</style>"
        "<div id=numbers class=frame><div id=number_leaf class=leaf>X</div></div>"
        "<div id=lengths class=frame><div id=length_leaf class=leaf>X</div></div>"
        "<div id=zoom_frame><div id=zoom_owner><div id=zoom_leaf class=leaf>X</div></div></div>"
        "<button id=update>update</button><script>"
        "document.getElementById('update').addEventListener('click',function(){"
        "document.getElementById('lengths').style.fontSize='20px';});</script>";
    const char* events =
        "{\"name\":\"line-height numeric types reach layout and paint\","
        "\"html\":\"temp/render_output_parity/cssom_leading_paint.html\","
        "\"viewport\":{\"width\":240,\"height\":150},\"events\":["
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#number_leaf\"},\"property\":\"height\",\"equals\":\"40px\"},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#length_leaf\"},\"property\":\"height\",\"equals\":\"20px\"},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#zoom_leaf\"},\"property\":\"line-height\",\"equals\":\"20px\"},"
        "{\"type\":\"assert_pixel\",\"x\":5,\"y\":35,\"min_b\":240,\"max_r\":20,\"max_g\":20},"
        "{\"type\":\"assert_pixel\",\"x\":65,\"y\":15,\"min_g\":110,\"max_r\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":65,\"y\":25,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"assert_pixel\",\"x\":125,\"y\":35,\"min_b\":240,\"max_r\":20,\"max_g\":20},"
        "{\"type\":\"assert_pixel\",\"x\":125,\"y\":45,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"target\":{\"selector\":\"#update\"}},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#length_leaf\"},\"property\":\"height\",\"equals\":\"40px\"},"
        "{\"type\":\"assert_pixel\",\"x\":65,\"y\":35,\"min_g\":110,\"max_r\":20,\"max_b\":20},"
        "{\"type\":\"assert_pixel\",\"x\":65,\"y\":45,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    // inherited math numbers scale with the child font; computed lengths retain their owner's basis.
    ASSERT_TRUE(run_html_fixture_view("temp/render_output_parity/cssom_leading_paint.html",
        "temp/render_output_parity/cssom_leading_paint.json", html, events));
}

TEST(RenderOutputParity, InheritedLineHeightMathMatchesComputedLengthsAcrossOutputs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    const char* rules[] = {
        "#numbers{line-height:calc(2)}#lengths{line-height:calc(2 * 1em)}"
        "#percentages{line-height:calc(150% + 5px)}#zoom_owner{line-height:calc(2 * 1em)}",
        "#numbers{line-height:2}#lengths,#percentages,#zoom_owner{line-height:20px}",
    };
    StrBuf* html[2] = {strbuf_new(), strbuf_new()};
    ASSERT_NE(html[0], nullptr); ASSERT_NE(html[1], nullptr);
    for (size_t index = 0; index < 2; index++) {
        strbuf_append_str(html[index], "<!doctype html><style>body{margin:0;background:white}"
            ".owner{position:absolute;top:0;width:40px;font:10px Arial}"
            "#numbers{left:0}#lengths{left:50px}#percentages{left:100px}#zoom{left:150px}"
            "#zoom_owner{font-size:10px;zoom:2}.leaf{font-size:20px;background:blue}</style><style>");
        strbuf_append_str(html[index], rules[index]);
        strbuf_append_str(html[index], "</style><div id=numbers class=owner><div class=leaf>A<br>B</div></div>"
            "<div id=lengths class=owner><div class=leaf>C<br>D</div></div>"
            "<div id=percentages class=owner><div class=leaf>E<br>F</div></div>"
            "<div id=zoom class=owner><div id=zoom_owner><div class=leaf>G<br>H</div></div></div>");
    }
    // line boxes position real glyphs and backgrounds in every export, including inherited zoom.
    expect_html_pair_output_parity("inherited_line_height_math", html[0]->str, html[1]->str);
    for (StrBuf* source : html) strbuf_free(source);
}

TEST(RenderOutputParity, FontMetricAndLeadingUnitsMatchExplicitLengthsAcrossOutputs) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    const char* rules[] = {
        "#ex{line-height:2ex}#ch{line-height:2ch}#cap{line-height:2cap}"
        "#ic{line-height:2ic}#lh{line-height:2lh}#rlh{line-height:2rlh}"
        "#size{font-size:2ex;line-height:1em}#zoom{line-height:2cap;zoom:2}",
        "#ex{line-height:10.56640625px}#ch{line-height:11.123046875px}"
        "#cap,#zoom{line-height:13.759765625px}#ic{line-height:20px}"
        "#lh,#rlh{line-height:80px}#size{font-size:21.1328125px;line-height:21.1328125px}#zoom{zoom:2}",
    };
    const char* ids[] = {"ex", "ch", "cap", "ic", "lh", "rlh", "size", "zoom"};
    StrBuf* html[2] = {strbuf_new(), strbuf_new()};
    ASSERT_NE(html[0], nullptr); ASSERT_NE(html[1], nullptr);
    for (size_t index = 0; index < 2; index++) {
        strbuf_append_str(html[index], "<!doctype html><style>"
            "@font-face{font-family:MetricSans;src:url(../../test/layout/data/font/LiberationSans-Regular.ttf)}"
            "html{font:20px/2 MetricSans}body{margin:0;background:white}"
            ".frame{position:absolute;left:0;width:70px}.owner{font-size:10px}"
            ".leaf{font-size:20px;background:blue}#size{font-size:inherit}");
        strbuf_append_str(html[index], rules[index]);
        strbuf_append_str(html[index], "</style>");
        for (size_t row = 0; row < 8; row++) {
            strbuf_append_format(html[index], "<div class=frame style='top:%upx'>"
                "<div class=owner id=%s><div class=leaf>A<br>B</div></div></div>",
                (unsigned)(row * 180), ids[row]);
        }
    }
    // independently calculated font-table lengths must position glyphs and backgrounds in every export.
    expect_html_pair_output_parity("font_metric_units", html[0]->str, html[1]->str);
    for (StrBuf* source : html) strbuf_free(source);
}

TEST(RenderOutputParity, LiveFontMetricAndParentLeadingChangesReachLayoutAndPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>"
        "@font-face{font-family:MetricSans;src:url(../../test/layout/data/font/LiberationSans-Regular.ttf)}"
        "@font-face{font-family:MetricMono;src:url(../../test/layout/data/font/LiberationMono-Regular.ttf)}"
        "html{font:20px/2 MetricSans}body{margin:0;background:white}"
        "#source{position:absolute;left:0;top:0;font-size:10px;line-height:2lh;width:20ch}"
        "#leaf{font-size:20px;background:blue;text-indent:9999px;overflow:hidden}"
        "#update{position:absolute;left:0;top:140px}</style>"
        "<div id=source><div id=leaf>X</div></div><button id=update>update</button><script>"
        "document.getElementById('update').addEventListener('click',function(){"
        "document.documentElement.style.lineHeight='3';"
        "document.getElementById('source').style.fontFamily='MetricMono';});</script>";
    const char* events =
        "{\"name\":\"font metrics and parent leading reach live paint\","
        "\"html\":\"temp/render_output_parity/font_metric_live.html\","
        "\"viewport\":{\"width\":200,\"height\":180},\"events\":["
        "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#leaf\"},\"width\":111.2305,\"height\":80,\"tolerance\":0.03},"
        "{\"type\":\"assert_pixel\",\"x\":105,\"y\":75,\"min_b\":240,\"max_r\":20,\"max_g\":20},"
        "{\"type\":\"assert_pixel\",\"x\":115,\"y\":100,\"min_r\":240,\"min_g\":240,\"min_b\":240},"
        "{\"type\":\"click\",\"target\":{\"selector\":\"#update\"}},"
        "{\"type\":\"assert_rect\",\"target\":{\"selector\":\"#leaf\"},\"width\":120.0195,\"height\":120,\"tolerance\":0.03},"
        "{\"type\":\"assert_pixel\",\"x\":115,\"y\":115,\"min_b\":240,\"max_r\":20,\"max_g\":20},"
        "{\"type\":\"assert_pixel\",\"x\":125,\"y\":125,\"min_r\":240,\"min_g\":240,\"min_b\":240}]}";
    ASSERT_TRUE(run_html_fixture_view("temp/render_output_parity/font_metric_live.html",
        "temp/render_output_parity/font_metric_live.json", html, events));
}

TEST(RenderOutputParity, LiveInheritedComputedValuesReachPaintAfterAncestorSelectorMutation) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0;background:white}"
        "#parent:lang(en){color:red}#parent:lang(fr){color:blue}"
        "#swatch{width:40px;height:20px;background:red}</style>"
        "<div id=swatch></div><button id=update>update</button>"
        "<div id=parent lang=en><span id=child>inherited text</span></div><script>"
        "var child=document.getElementById('child'),live=getComputedStyle(child);"
        "document.getElementById('update').addEventListener('click',function(){"
        "document.getElementById('parent').lang='fr';"
        "document.getElementById('swatch').style.backgroundColor=live.color;});</script>";
    const char* events =
        "{\"name\":\"inherited CSSOM value drives paint\","
        "\"html\":\"temp/render_output_parity/cssom_inherited_paint.html\","
        "\"viewport\":{\"width\":200,\"height\":100},\"events\":["
        "{\"type\":\"click\",\"target\":{\"selector\":\"#update\"}},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#child\"},"
        "\"property\":\"color\",\"equals\":\"rgb(0, 0, 255)\"},"
        "{\"type\":\"assert_style\",\"target\":{\"selector\":\"#swatch\"},"
        "\"property\":\"background-color\",\"equals\":\"rgb(0, 0, 255)\"},"
        "{\"type\":\"assert_pixel\",\"x\":10,\"y\":10,\"min_b\":240,\"max_r\":20,\"max_g\":20}]}";
    // the first inherited read in the handler determines a separately painted consumer.
    ASSERT_TRUE(run_html_fixture_view("temp/render_output_parity/cssom_inherited_paint.html",
        "temp/render_output_parity/cssom_inherited_paint.json", html, events));
}

TEST(RenderOutputParity, LinguisticSelectorsAndFirstStrongDirectionReachPaintAndInlineLayout) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    const struct {const char* direction; const char* reference; const char* text;} rows[] = {
        {"auto", "ltr", "<bdi>\xD7\x90</bdi>" "abc"},
        {"auto", "rtl", "<span style='display:none'>\xF0\x9E\xA4\x80</span>"},
        {"auto", "ltr", "\xD9\xA1\xD6\xB0\xD4\xB1"},
        {"auto", "ltr", "123"},
    };
    StrBuf* html[2] = {strbuf_new(), strbuf_new()};
    ASSERT_NE(html[0], nullptr); ASSERT_NE(html[1], nullptr);
    for (size_t index = 0; index < 2; index++) {
        strbuf_append_str(html[index], "<!doctype html><style>body{margin:0}.row{width:120px;height:20px;"
            "font-size:0;text-align:start;margin-bottom:4px}.box{display:inline-block;width:20px;height:20px}"
            ".first{background:red}.second{background:blue}.selected{width:120px;height:20px;background:red}");
        strbuf_append_str(html[index], index == 0
            ? ".selected:lang(de-DE){background:green}"
            : ".selected{background:green}");
        strbuf_append_str(html[index], "</style><body dir='rtl'><div lang='de-Latn-DE'><div class='selected'></div></div>");
        for (const auto& row : rows) {
            strbuf_append_str(html[index], "<div class='row' dir='");
            strbuf_append_str(html[index], index == 0 ? row.direction : row.reference);
            strbuf_append_str(html[index], "'>");
            strbuf_append_str(html[index], row.text);
            strbuf_append_str(html[index], "<span class='box first'></span><span class='box second'></span></div>");
        }
        strbuf_append_str(html[index], "</body>");
    }
    // compare real paint and inline ordering with explicit direction and selector references.
    expect_html_pair_output_parity("linguistic_selectors", html[0]->str, html[1]->str);
    for (StrBuf* source : html) strbuf_free(source);
}

TEST(RenderOutputParity, SvgReferencesAndColorSpacesReachExistingPaintConsumers) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    const char* styles[] = {
        "body{marker:url(#m)}.marked{marker:url(#n);marker:var(--bad);--bad:url(#n) red}"
        ".scaled{vector-effect:NoN-ScAlInG-StRoKe}.label{text-anchor:MiDdLe}"
        "mask{color-interpolation:LiNeArRgB}filter{color-interpolation-filters:SrGb}",
        "body{marker-start:url(#m);marker-mid:url(#m);marker-end:url(#m)}"
        ".scaled{vector-effect:non-scaling-stroke}.label{text-anchor:middle}"
        "mask{color-interpolation:linearRGB}filter{color-interpolation-filters:sRGB}",
    };
    const char* geometry =
        "</style><svg width='220' height='120'><defs>"
        "<marker id='m' markerWidth='8' markerHeight='8' refX='4' refY='4' markerUnits='userSpaceOnUse'>"
        "<rect width='8' height='8' fill='red'/></marker>"
        "<marker id='n' markerWidth='8' markerHeight='8' refX='4' refY='4' markerUnits='userSpaceOnUse'>"
        "<rect width='8' height='8' fill='blue'/></marker>"
        "<mask id='mask'><rect width='220' height='120' fill='#808080'/></mask>"
        "<filter id='filter'><feColorMatrix type='matrix' values='.5 0 0 0 0 0 .5 0 0 0 "
        "0 0 .5 0 0 0 0 0 1 0'/></filter></defs>"
        "<path class='marked' d='M10 12L40 12L70 12' fill='none' stroke='green' stroke-width='2'/>"
        "<path class='scaled' transform='translate(80 0) scale(2)' d='M5 6H60' fill='none' stroke='purple' stroke-width='6'/>"
        "<text class='label' x='110' y='48' font-size='16'>anchor</text>"
        "<rect x='10' y='60' width='80' height='40' fill='green' mask='url(#mask)'/>"
        "<rect x='110' y='60' width='80' height='40' fill='#808080' filter='url(#filter)'/></svg>";
    StrBuf* html[2] = {strbuf_new(), strbuf_new()};
    ASSERT_NE(html[0], nullptr); ASSERT_NE(html[1], nullptr);
    for (int index = 0; index < 2; index++) {
        strbuf_append_str(html[index], "<!doctype html><style>body{margin:0}svg{display:block}");
        strbuf_append_str(html[index], styles[index]); strbuf_append_str(html[index], geometry);
    }
    // canonical CSS tokens, invalid substitution fallback and root inheritance must reach real paint.
    expect_html_pair_output_parity("svg_references", html[0]->str, html[1]->str);
    for (StrBuf* source : html) strbuf_free(source);
}

TEST(RenderOutputParity, SvgPaintResourceUrlsUseStylesheetAndVariableConsumerBases) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity/svg_css_url"));
    ASSERT_TRUE(ensure_dir("temp/render_output_parity/svg_css_url/styles"));
    const struct {const char* path; const char* text;} resources[] = {
        {"temp/render_output_parity/svg_css_url/styles/server.svg",
         "<svg xmlns='http://www.w3.org/2000/svg'><defs><linearGradient id='g'>"
         "<stop offset='0' stop-color='green'/><stop offset='1' stop-color='green'/>"
         "</linearGradient></defs></svg>"},
        {"temp/render_output_parity/svg_css_url/styles/paint.css",
         "@import url(import.css);#literal{fill:url(server.svg#g) red}#variable{fill:var(--paint) blue}"},
        {"temp/render_output_parity/svg_css_url/styles/import.css", "#imported{fill:url(server.svg#g) purple}"},
    };
    for (const auto& resource : resources)
        ASSERT_TRUE(write_file_all(resource.path, resource.text, strlen(resource.text)));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/svg_css_url/reference.html",
        "temp/render_output_parity/svg_css_url/reference.png",
        "<!doctype html><style>body{margin:0}svg{display:block}</style><svg width='90' height='20'>"
        "<rect width='30' height='20' fill='green'/><rect x='30' width='30' height='20' fill='green'/>"
        "<rect x='60' width='30' height='20' fill='green'/></svg>"));
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/svg_css_url/actual.html",
        "temp/render_output_parity/svg_css_url/actual.png",
        "<!doctype html><link rel='stylesheet' href='styles/paint.css'>"
        "<style>body{margin:0;--paint:url(server.svg#g)}svg{display:block}</style>"
        "<svg width='90' height='20'><rect id='literal' width='30' height='20'/>"
        "<rect id='imported' x='30' width='30' height='20'/>"
        "<rect id='variable' x='60' width='30' height='20'/></svg>"));
    // inherited custom tokens acquire the using declaration's stylesheet base, not the defining document's.
    expect_pngs_exactly_equal("temp/render_output_parity/svg_css_url/reference.png",
        "temp/render_output_parity/svg_css_url/actual.png");
}

TEST(RenderOutputParity, OversizedCustomPropertiesUseConsumerFallbacksAcrossHtmlAndSvg) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* literal_png = "temp/render_output_parity/var_limit_literal.png";
    const char* expanded_png = "temp/render_output_parity/var_limit_expanded.png";
    ASSERT_TRUE(render_html_fixture("temp/render_output_parity/var_limit_literal.html", literal_png,
        "<!doctype html><style>body{margin:0}div{width:40px;height:20px;"
        "border:3px solid blue;background:red}svg{display:block}</style><div></div>"
        "<svg width='32' height='18' xmlns='http://www.w3.org/2000/svg'>"
        "<rect width='32' height='18' fill='lime'/></svg>"));
    StrBuf* html = strbuf_new(); ASSERT_NE(html, nullptr);
    strbuf_append_str(html, "<!doctype html><style>:root{--expand0:x;");
    for (int index = 1; index <= 31; index++)
        strbuf_append_format(html, "--expand%d:var(--expand%d)var(--expand%d);", index, index - 1, index - 1);
    strbuf_append_str(html,
        "}body{margin:0}div{width:var(--expand31,40px);height:20px;"
        "padding:5px;padding:var(--expand31);border:var(--expand31,3px solid blue);"
        "background:var(--expand31,red)}svg{display:block}rect{fill:var(--expand31,lime)}"
        "</style><div></div><svg width='32' height='18' xmlns='http://www.w3.org/2000/svg'>"
        "<rect width='32' height='18'/></svg>");
    bool rendered = render_html_fixture("temp/render_output_parity/var_limit_expanded.html", expanded_png, html->str);
    strbuf_free(html); ASSERT_TRUE(rendered);
    // an overflowing winner resets padding rather than exposing the discarded 5px declaration.
    expect_pngs_exactly_equal(literal_png, expanded_png);
}

TEST(RenderOutputParity, CustomPropertyDefaultingAndCyclesReachPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}section{--tone:red}"
        "div{position:absolute;left:0;width:20px;height:10px;"
        "background:var(--tone,lime)}"
        "#initial{top:0;--tone:initial}"
        "#inherit{top:10px;--tone:inherit}"
        "#unset{top:20px;--tone:unset}"
        "#self{top:30px;--tone:var(--tone,red)}"
        "#mutual{top:40px;--tone:var(--other,red);--other:var(--tone,blue)}"
        "#outside{top:50px;--tone:var(--a,blue);--a:var(--b,red);--b:var(--a)}"
        "#unused{top:60px;--solid:red;--tone:var(--solid,var(--tone))}"
        "#parent{--a:red;--tone:var(--a)}"
        "#child{top:70px;--a:var(--tone);background:var(--a,lime)}"
        "</style><section><div id=initial></div><div id=inherit></div>"
        "<div id=unset></div><div id=self></div><div id=mutual></div>"
        "<div id=outside></div><div id=unused></div></section>"
        "<section id=parent><div id=child></div></section>";
    const char* svg_path = "temp/render_output_parity/var_defaulting_cycles.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/var_defaulting_cycles.html", svg_path, html));
    const char* colors[] = {"0,255,0", "255,0,0", "255,0,0", "0,255,0",
                            "0,255,0", "0,0,255", "255,0,0", "255,0,0"};
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        char expected[160];
        snprintf(expected, sizeof(expected),
            "y=\"%.2f\" width=\"20.00\" height=\"10.00\" fill=\"rgb(%s)\"",
            (double)i * 10.0, colors[i]);
        EXPECT_TRUE(file_contains_text(svg_path, expected)) << expected;
    }
}

TEST(RenderOutputParity, SvgCustomPropertyReferencesUseDeclarationOwner) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}svg{display:block}"
        "g{--base:red;--alias:var(--base)}"
        "rect{--base:blue;fill:var(--alias,lime)}"
        "</style><svg width='20' height='10' viewBox='0 0 20 10' "
        "xmlns='http://www.w3.org/2000/svg'>"
        "<g><rect width='20' height='10'/></g></svg>";
    const char* png_path = "temp/render_output_parity/svg_var_owner.png";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/svg_var_owner.html", png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GT(image.width, 10);
    ASSERT_GT(image.height, 5);
    const unsigned char* pixel = image.pixels + (5 * image.width + 10) * 4;
    EXPECT_EQ(pixel[0], 255);
    EXPECT_EQ(pixel[1], 0);
    EXPECT_EQ(pixel[2], 0);
    EXPECT_EQ(pixel[3], 255);
    image_free(image.pixels);
}

TEST(RenderOutputParity, CustomPropertyRollbackRespectsOriginLayerAndInlinePriority) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}section{--tone:red}"
        "div{width:20px;height:10px;background:var(--tone,lime)}"
        "@layer first,second;"
        "@layer first{#normal{--tone:red}#important{--tone:revert-layer!important}"
        "#inline{--tone:blue!important}}"
        "@layer second{#normal{--tone:blue;--tone:revert-layer}"
        "#important{--tone:blue!important}}"
        "#origin{--tone:blue;--tone:revert}"
        "</style><section><div id=normal></div><div id=important></div>"
        "<div id=origin></div><div id=inline style='--tone:revert-layer!important'></div>"
        "</section>";
    const char* svg_path = "temp/render_output_parity/var_rollback.svg";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/var_rollback.html", svg_path, html));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"0.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"10.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"20.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(255,0,0)\""));
    EXPECT_TRUE(file_contains_text(svg_path,
        "y=\"30.00\" width=\"20.00\" height=\"10.00\" fill=\"rgb(0,0,255)\""));
}

TEST(RenderOutputParity, SvgCustomPropertyDefaultingRollbackAndCaseReachPaint) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0) {
        GTEST_SKIP() << "lambda.exe is unavailable";
    }
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html =
        "<!doctype html><style>body{margin:0}svg{display:block}g{--tone:red}"
        "rect{fill:var(--tone,lime)}"
        "#inherit{--tone:inherit}#initial{--tone:initial}"
        "#case{--Tone:red;--tone:blue;fill:var(--Tone,lime)}"
        "@layer first,second;@layer first{#layer{--tone:red}}"
        "@layer second{#layer{--tone:blue;--tone:revert-layer}}"
        "#origin{--tone:blue;--tone:revert}"
        "</style><svg width='100' height='10' viewBox='0 0 100 10' "
        "xmlns='http://www.w3.org/2000/svg'><g>"
        "<rect id='inherit' x='0' width='20' height='10'/>"
        "<rect id='initial' x='20' width='20' height='10'/>"
        "<rect id='case' x='40' width='20' height='10'/>"
        "<rect id='layer' x='60' width='20' height='10'/>"
        "<rect id='origin' x='80' width='20' height='10'/>"
        "</g></svg>";
    const char* png_path = "temp/render_output_parity/svg_var_defaulting.png";
    ASSERT_TRUE(render_html_fixture(
        "temp/render_output_parity/svg_var_defaulting.html", png_path, html));
    ImageData image = {};
    ASSERT_TRUE(load_png_rgba(png_path, &image));
    ASSERT_GT(image.width, 90);
    ASSERT_GT(image.height, 5);
    const unsigned char colors[][3] = {{255,0,0}, {0,255,0}, {255,0,0}, {255,0,0}, {255,0,0}};
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        const unsigned char* pixel = image.pixels + ((size_t)5 * image.width + i * 20 + 10) * 4;
        for (int channel = 0; channel < 3; channel++) {
            EXPECT_EQ(pixel[channel], colors[i][channel]) << "box " << i;
        }
    }
    image_free(image.pixels);
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

TEST(RenderOutputParity, PdfUnicodeTextUsesFontOutlines) {
    if (!file_exists(LAMBDA_EXE) || access(LAMBDA_EXE, X_OK) != 0)
        GTEST_SKIP() << "lambda.exe is unavailable";
    ASSERT_TRUE(ensure_dir("temp/render_output_parity"));
    const char* html_path = "temp/render_output_parity/pdf_unicode.html";
    const char* pdf_path = "temp/render_output_parity/pdf_unicode.pdf";
    const char* html = "<!doctype html><meta charset='utf-8'>"
        "<style>body{font:24px serif}</style><p>Café αβγ Привет</p>";
    ASSERT_TRUE(write_file_all(html_path, html, strlen(html)));
    char qhtml[PATH_MAX + 8], qpdf[PATH_MAX + 8], cmd[PATH_MAX * 3 + 256];
    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(pdf_path, qpdf, sizeof(qpdf));
    snprintf(cmd, sizeof(cmd), "%s render %s%s -o %s > temp/render_output_parity/pdf_unicode.log 2>&1",
        LAMBDA_EXE, lambda_no_log_arg(), qhtml, qpdf);
    int status = system(cmd);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    EXPECT_TRUE(file_contains_text(pdf_path, " c\n"));
    EXPECT_TRUE(file_contains_text(pdf_path, "f\n"));
    EXPECT_FALSE(file_contains_text(pdf_path, " Tj\n"));
    EXPECT_FALSE(file_contains_text(pdf_path, "Café"));
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

TEST(PdfRenderVisual, EmbeddedType1MathMatchesPoppler) {
    if (!command_exists("pdftoppm")) GTEST_SKIP() << "Poppler is required for the PDF reference";
    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir(PDF_TEMP_DIR));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    ASSERT_TRUE(ensure_dir(PDF_DIFF_DIR));
    PdfFileInfo pdf = {};
    snprintf(pdf.path, sizeof(pdf.path), "test/input/math_intensive_test.pdf");
    snprintf(pdf.base, sizeof(pdf.base), "math_intensive_type1");
    const int pages = pdf_page_count(pdf.path);
    ASSERT_GT(pages, 1);
    // page 2 exercises display sums/integrals, size-specific delimiters, accents and braces.
    for (int page = 1; page <= pages; page++) {
        char reference[PATH_MAX], actual[PATH_MAX], diff[PATH_MAX];
        ASSERT_TRUE(render_reference_page(&pdf, page, reference, sizeof(reference)));
        ImageData ref = {};
        ASSERT_TRUE(load_png_rgba(reference, &ref));
        int height = ref.height;
        image_free(ref.pixels);
        // Poppler rounds up the bitmap height; keep the SVG's intrinsic ratio to avoid a subpixel y inset.
        ASSERT_TRUE(render_lambda_png_page(&pdf, page - 1, height, actual, sizeof(actual), true));
        snprintf(diff, sizeof(diff), "%s/type1_math_page_%d.png", PDF_DIFF_DIR, page);
        double mismatch = 100.0, delta = 255.0;
        // compare ink geometry with one-pixel antialias smoothing: Poppler hints text, SVG paints outlines.
        compare_pngs(reference, actual, diff, &mismatch, &delta, 1);
        char property[64]; snprintf(property, sizeof(property), "page_%d_mismatch_percent", page);
        RecordProperty(property, mismatch);
        EXPECT_LT(mismatch, 0.75) << "page " << page << "; inspect " << diff;
    }
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
