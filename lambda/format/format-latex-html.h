#ifndef FORMAT_LATEX_HTML_H
#define FORMAT_LATEX_HTML_H

#include "../lambda-data.hpp"
#include "../../lib/stringbuf.h"
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Main API function for LaTeX to HTML conversion
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast, Pool* pool);

#ifdef __cplusplus
}
#endif

#endif // FORMAT_LATEX_HTML_H
