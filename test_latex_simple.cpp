#include <iostream>
#include <string>
#include "lambda/format/format-latex-html.h"
#include "lambda/input/input.h"
#include "lib/stringbuf.h"
#include "lib/mempool.h"

int main() {
    // Initialize memory pool
    Pool* pool = pool_create();
    if (!pool) {
        std::cerr << "Failed to create memory pool" << std::endl;
        return 1;
    }

    // Create string buffers
    StringBuf* html_buf = stringbuf_new(pool);
    StringBuf* css_buf = stringbuf_new(pool);

    // Simple LaTeX input
    const char* latex_input = R"(\textbf{Bold text} and \textit{italic text})";

    try {
        // Create String for type specification
        String* type_str = (String*)malloc(sizeof(String) + strlen("latex") + 1);
        type_str->len = strlen("latex");
        type_str->ref_cnt = 0;
        strcpy(type_str->chars, "latex");

        // Parse LaTeX source
        Input* input = input_from_source(latex_input, nullptr, type_str, nullptr);
        if (!input) {
            std::cerr << "Failed to parse LaTeX input" << std::endl;
            free(type_str);
            pool_destroy(pool);
            return 1;
        }

        // Generate HTML
        format_latex_to_html(html_buf, css_buf, input->root, pool);

        // Get results
        String* html_result = stringbuf_to_string(html_buf);
        String* css_result = stringbuf_to_string(css_buf);

        std::cout << "=== HTML Output ===" << std::endl;
        if (html_result) {
            std::cout << std::string(html_result->chars, html_result->len) << std::endl;
        } else {
            std::cout << "No HTML output" << std::endl;
        }

        std::cout << "\n=== CSS Output ===" << std::endl;
        if (css_result) {
            std::cout << std::string(css_result->chars, css_result->len) << std::endl;
        } else {
            std::cout << "No CSS output" << std::endl;
        }

        free(type_str);
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    pool_destroy(pool);
    return 0;
}
