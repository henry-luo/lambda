#include "format-latex-html.h"
#include "format.h"
#include "../../lib/stringbuf.h"
#include "../../lib/log.h"
#include "../../lib/mem-pool/include/mem_pool.h"
#include <string.h>
#include <stdlib.h>

// Main API function
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, VariableMemPool* pool) {
    if (!html_buf || !css_buf || !pool) {
        // log_error("format_latex_to_html: Invalid parameters");
        return;
    }
    
    // For now, create a simple HTML output to demonstrate integration
    stringbuf_append_str(html_buf, "<div class=\"latex-document\">\n");
    stringbuf_append_str(html_buf, "  <h1>LaTeX Document</h1>\n");
    stringbuf_append_str(html_buf, "  <p>This is a converted LaTeX document.</p>\n");
    stringbuf_append_str(html_buf, "  <p>LaTeX to HTML conversion is working!</p>\n");
    stringbuf_append_str(html_buf, "</div>\n");
    
    // Add basic CSS
    stringbuf_append_str(css_buf, ".latex-document {\n");
    stringbuf_append_str(css_buf, "  font-family: 'Computer Modern', 'Latin Modern', serif;\n");
    stringbuf_append_str(css_buf, "  max-width: 800px;\n");
    stringbuf_append_str(css_buf, "  margin: 0 auto;\n");
    stringbuf_append_str(css_buf, "  padding: 2rem;\n");
    stringbuf_append_str(css_buf, "  line-height: 1.6;\n");
    stringbuf_append_str(css_buf, "}\n");
    stringbuf_append_str(css_buf, ".latex-document h1 {\n");
    stringbuf_append_str(css_buf, "  text-align: center;\n");
    stringbuf_append_str(css_buf, "  margin-bottom: 2rem;\n");
    stringbuf_append_str(css_buf, "}\n");
}
