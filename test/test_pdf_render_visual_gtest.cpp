/**
 * GTest PDF visual regression coverage.
 *
 * Reference rendering uses Poppler (`pdfinfo` + `pdftoppm`), the most
 * widely deployed and stable open-source PDF rasterizer available from the
 * command line. Each PDF page is rendered to a 600px-wide PNG under
 * test/pdf/reference. Lambda renders the same page through the PDF package
 * (`pdf.pdf_to_svg`) and Radiant's PNG renderer, then the test compares pixels.
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
#define PDF_REF_DIR "test/pdf/reference"
#define PDF_DIFF_DIR "test/pdf/diff"
#define PDF_TEMP_DIR "temp/pdf_visual"
#define LAMBDA_EXE "./lambda.exe"
#define RENDER_WIDTH 600
#define MAX_PAGES_PER_PDF 4
#define MAX_PDFS 64
#define MAX_PAGE_MISMATCH_PERCENT 35.0
#define MAX_MEAN_ABS_DELTA 32.0
#define PIXEL_DELTA_THRESHOLD 32

struct PdfFileInfo {
    char path[PATH_MAX];
    char base[256];
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

static bool ensure_dir(const char* path) {
    if (mkdir(path, 0755) == 0) return true;
    return errno == EEXIST;
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

static bool read_file_all(const char* path, char** out_data, size_t* out_len) {
    *out_data = NULL;
    *out_len = 0;
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return false; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return false; }
    char* data = (char*)malloc((size_t)size + 1);
    if (!data) { fclose(fp); return false; }
    size_t nread = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    data[nread] = '\0';
    *out_data = data;
    *out_len = nread;
    return true;
}

static bool write_file_all(const char* path, const char* data, size_t len) {
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;
    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    return written == len;
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

static bool render_lambda_svg_page(const PdfFileInfo* pdf, int page_index, const char* svg_path) {
    char script_path[PATH_MAX];
    char out_path_escaped[PATH_MAX * 2];
    char pdf_path_escaped[PATH_MAX * 2];
    char script[4096];
    char qscript[PATH_MAX + 8];
    char cmd[PATH_MAX * 3];

    snprintf(script_path, sizeof(script_path), "%s/%s_page_%02d.ls", PDF_TEMP_DIR, pdf->base, page_index + 1);
    lambda_string_escape(pdf->path, pdf_path_escaped, sizeof(pdf_path_escaped));
    lambda_string_escape(svg_path, out_path_escaped, sizeof(out_path_escaped));
    snprintf(script, sizeof(script),
             "import pdf: lambda.package.pdf.pdf\n"
             "\n"
             "pn main() {\n"
             "    let doc^err = input(\"%s\", 'pdf')\n"
             "    let svg = pdf.pdf_to_svg(doc, %d, {show_label: false})\n"
             "    let out = format(svg, 'xml')\n"
             "    output(out, \"%s\", 'text')^\n"
             "}\n",
             pdf_path_escaped, page_index, out_path_escaped);

    if (!write_file_all(script_path, script, strlen(script))) return false;

    shell_quote(script_path, qscript, sizeof(qscript));
    snprintf(cmd, sizeof(cmd), "%s run %s > %s/%s_page_%02d_run.out 2> %s/%s_page_%02d_run.err",
             LAMBDA_EXE, qscript, PDF_TEMP_DIR, pdf->base, page_index + 1,
             PDF_TEMP_DIR, pdf->base, page_index + 1);
    int status = system(cmd);
    return status == 0 && file_exists(svg_path);
}

static bool write_svg_html_wrapper(const char* svg_path, const char* html_path, int height) {
    char* svg = NULL;
    size_t svg_len = 0;
    if (!read_file_all(svg_path, &svg, &svg_len)) return false;

    const char* head = "<!doctype html><html><head><meta charset=\"utf-8\"><style>html,body{margin:0;padding:0;background:white;overflow:hidden;}svg{display:block;width:600px;height:";
    const char* mid = "px;}</style></head><body>";
    const char* tail = "</body></html>";
    char height_buf[32];
    snprintf(height_buf, sizeof(height_buf), "%d", height);

    size_t total = strlen(head) + strlen(height_buf) + strlen(mid) + svg_len + strlen(tail);
    char* html = (char*)malloc(total + 1);
    if (!html) { free(svg); return false; }
    html[0] = '\0';
    strcat(html, head);
    strcat(html, height_buf);
    strcat(html, mid);
    size_t pos = strlen(html);
    memcpy(html + pos, svg, svg_len);
    pos += svg_len;
    memcpy(html + pos, tail, strlen(tail));
    pos += strlen(tail);
    html[pos] = '\0';

    bool ok = write_file_all(html_path, html, pos);
    free(svg);
    free(html);
    return ok;
}

static bool render_lambda_png_page(const PdfFileInfo* pdf, int page_index, int height, char* out_png, size_t out_size) {
    char svg_path[PATH_MAX];
    char html_path[PATH_MAX];
    char qhtml[PATH_MAX + 8];
    char qpng[PATH_MAX + 8];
    char cmd[PATH_MAX * 3 + 256];

    snprintf(svg_path, sizeof(svg_path), "%s/%s_page_%02d.svg", PDF_TEMP_DIR, pdf->base, page_index + 1);
    snprintf(html_path, sizeof(html_path), "%s/%s_page_%02d.html", PDF_TEMP_DIR, pdf->base, page_index + 1);
    snprintf(out_png, out_size, "%s/%s_page_%02d_lambda.png", PDF_TEMP_DIR, pdf->base, page_index + 1);
    unlink(out_png);

    if (!render_lambda_svg_page(pdf, page_index, svg_path)) return false;
    if (!write_svg_html_wrapper(svg_path, html_path, height)) return false;

    shell_quote(html_path, qhtml, sizeof(qhtml));
    shell_quote(out_png, qpng, sizeof(qpng));
    snprintf(cmd, sizeof(cmd),
             "%s render %s -o %s -vw %d -vh %d --pixel-ratio 1 > %s/%s_page_%02d_render.out 2> %s/%s_page_%02d_render.err",
             LAMBDA_EXE, qhtml, qpng, RENDER_WIDTH, height,
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

    ASSERT_TRUE(ensure_dir("temp"));
    ASSERT_TRUE(ensure_dir(PDF_TEMP_DIR));
    ASSERT_TRUE(ensure_dir(PDF_REF_DIR));
    ASSERT_TRUE(ensure_dir(PDF_DIFF_DIR));

    PdfFileInfo files[MAX_PDFS];
    int file_count = discover_pdfs(files, MAX_PDFS);
    ASSERT_GT(file_count, 0) << "no PDF fixtures found under " << PDF_DIR;

    int compared_pages = 0;
    for (int i = 0; i < file_count; i++) {
        int pages = pdf_page_count(files[i].path);
        ASSERT_GT(pages, 0) << "pdfinfo did not report pages for " << files[i].path;
        int render_pages = pages > MAX_PAGES_PER_PDF ? MAX_PAGES_PER_PDF : pages;

        for (int page = 1; page <= render_pages; page++) {
            char ref_png[PATH_MAX];
            char lambda_png[PATH_MAX];
            char diff_png[PATH_MAX];
            snprintf(diff_png, sizeof(diff_png), "%s/%s_page_%02d_diff.png",
                     PDF_DIFF_DIR, files[i].base, page);
            ASSERT_TRUE(render_reference_page(&files[i], page, ref_png, sizeof(ref_png)))
                << "reference render failed for " << files[i].path << " page " << page;

            int ref_width = 0;
            int ref_height = 0;
            ASSERT_EQ(image_get_dimensions(ref_png, &ref_width, &ref_height), 1)
                << "failed to read reference dimensions: " << ref_png;
            ASSERT_EQ(ref_width, RENDER_WIDTH) << "reference renderer did not produce requested width for " << ref_png;
            ASSERT_GT(ref_height, 0);

            ASSERT_TRUE(render_lambda_png_page(&files[i], page - 1, ref_height, lambda_png, sizeof(lambda_png)))
                << "Lambda render failed for " << files[i].path << " page " << page;

            double mismatch_percent = 100.0;
            double mean_abs_delta = 255.0;
            compare_pngs(ref_png, lambda_png, diff_png, &mismatch_percent, &mean_abs_delta);

            EXPECT_LE(mismatch_percent, MAX_PAGE_MISMATCH_PERCENT)
                << "PDF visual mismatch too high for " << files[i].path
                << " page " << page << " (reference: " << ref_png
                << ", lambda: " << lambda_png
                << ", diff: " << diff_png << ")";
            EXPECT_LE(mean_abs_delta, MAX_MEAN_ABS_DELTA)
                << "PDF visual mean absolute delta too high for " << files[i].path
                << " page " << page << " (reference: " << ref_png
                << ", lambda: " << lambda_png
                << ", diff: " << diff_png << ")";
            compared_pages++;
        }
    }

    EXPECT_GT(compared_pages, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
