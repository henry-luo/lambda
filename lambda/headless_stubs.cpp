// headless_stubs.cpp
// Stub implementations for radiant functions used by CSS DOM code and main.cpp
// Only compiled into lambda-cli.exe (headless build without Radiant engine)

#ifdef LAMBDA_HEADLESS

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include "../radiant/view.hpp"
#include "../lambda/lambda-data.hpp"

// Forward declarations (avoid including heavy radiant headers)
struct CounterContext;
struct DomDocument;
struct DomElement;

extern "C" void log_mem_stage(const char*) {}

// --- Symbol Resolution Stubs ---

extern "C" SymbolResolution resolve_symbol(const char* name, size_t len) {
    (void)name; (void)len;
    SymbolResolution result = {};
    result.type = SYMBOL_UNKNOWN;
    result.utf8 = nullptr;
    result.utf8_len = 0;
    result.codepoint = 0;
    return result;
}

extern "C" SymbolResolution resolve_symbol_string(const void* string_ptr) {
    (void)string_ptr;
    SymbolResolution result = {};
    result.type = SYMBOL_UNKNOWN;
    result.utf8 = nullptr;
    result.utf8_len = 0;
    result.codepoint = 0;
    return result;
}

// --- Counter System Stubs (C++ linkage - matches layout_counters.hpp) ---

int counter_format(CounterContext* ctx, const char* name, uint32_t style,
                   char* buffer, size_t buffer_size) {
    (void)ctx; (void)name; (void)style;
    if (buffer && buffer_size > 0) buffer[0] = '\0';
    return 0;
}

int counters_format(CounterContext* ctx, const char* name, const char* separator,
                    uint32_t style, char* buffer, size_t buffer_size) {
    (void)ctx; (void)name; (void)separator; (void)style;
    if (buffer && buffer_size > 0) buffer[0] = '\0';
    return 0;
}

// --- Radiant Layout/Render Stubs (C++ linkage - matches main.cpp declarations) ---

int cmd_layout(int argc, char** argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "Error: layout command not available in headless CLI build\n");
    return 1;
}

int cmd_webdriver(int argc, char** argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "Error: webdriver command not available in headless CLI build\n");
    return 1;
}

int run_layout(const char* html_file) {
    (void)html_file;
    fprintf(stderr, "Error: layout command not available in headless CLI build\n");
    return 1;
}

int render_html_to_svg(const char* html_file, const char* svg_file,
                       int viewport_width, int viewport_height, float scale) {
    (void)html_file; (void)svg_file; (void)viewport_width; (void)viewport_height; (void)scale;
    fprintf(stderr, "Error: render command not available in headless CLI build\n");
    return 1;
}

int render_html_to_pdf(const char* html_file, const char* pdf_file,
                       int viewport_width, int viewport_height, float scale) {
    (void)html_file; (void)pdf_file; (void)viewport_width; (void)viewport_height; (void)scale;
    fprintf(stderr, "Error: render command not available in headless CLI build\n");
    return 1;
}

int render_html_to_png(const char* html_file, const char* png_file,
                       int viewport_width, int viewport_height, float scale, float pixel_ratio) {
    (void)html_file; (void)png_file; (void)viewport_width; (void)viewport_height; (void)scale; (void)pixel_ratio;
    fprintf(stderr, "Error: render command not available in headless CLI build\n");
    return 1;
}

int render_html_to_jpeg(const char* html_file, const char* jpeg_file, int quality,
                        int viewport_width, int viewport_height, float scale, float pixel_ratio) {
    (void)html_file; (void)jpeg_file; (void)quality; (void)viewport_width; (void)viewport_height; (void)scale; (void)pixel_ratio;
    fprintf(stderr, "Error: render command not available in headless CLI build\n");
    return 1;
}

int render_html_to_output_target(const char* html_file, const char* output_file,
                                 int viewport_width, int viewport_height,
                                 float scale, float pixel_ratio, int jpeg_quality) {
    (void)html_file; (void)output_file; (void)viewport_width; (void)viewport_height;
    (void)scale; (void)pixel_ratio; (void)jpeg_quality;
    fprintf(stderr, "Error: render command not available in headless CLI build\n");
    return 1;
}

int view_doc_in_window(const char* doc_file) {
    (void)doc_file;
    fprintf(stderr, "Error: view command not available in headless CLI build\n");
    return 1;
}

int view_doc_in_window_with_events(const char* doc_file, const char* event_file, bool headless,
                                    const char** font_dirs, int font_dir_count,
                                    bool enable_event_log, bool enable_state_dump) {
    (void)doc_file; (void)event_file; (void)headless;
    (void)font_dirs; (void)font_dir_count; (void)enable_event_log; (void)enable_state_dump;
    fprintf(stderr, "Error: view command not available in headless CLI build\n");
    return 1;
}

int view_lambda_script_source_in_window_with_events(const char* script_name, const char* script_source,
                                                    const char* event_file, bool headless,
                                                    const char** font_dirs, int font_dir_count,
                                                    bool enable_event_log, bool enable_state_dump) {
    (void)script_name; (void)script_source; (void)event_file; (void)headless;
    (void)font_dirs; (void)font_dir_count; (void)enable_event_log; (void)enable_state_dump;
    fprintf(stderr, "Error: view command not available in headless CLI build\n");
    return 1;
}

extern "C" Item fn_pdf_register_svg_image_resolver(Item svg_item, Item pdf_item) {
    (void)pdf_item;
    return svg_item;
}

extern "C" void svg_unregister_image_resolvers_for_tree(Element* root) {
    (void)root;
}

Element* get_html_root_element(Input* input) {
    (void)input;
    return nullptr;
}

#endif // LAMBDA_HEADLESS
