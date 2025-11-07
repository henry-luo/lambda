#ifndef CSS_TOKENIZER_H
#define CSS_TOKENIZER_H

// Include new CSS system headers
#include "css_parser.hpp"
#include "css_style.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// Note: This file is now a compatibility header.
// The main tokenizer types and functions are defined in css_parser.h

/**
 * CSS Tokenizer Compatibility Header
 * 
 * This file provides backward compatibility for code that included
 * the old css_tokenizer.h. All tokenizer types and functions
 * are now defined in css_parser.h.
 */

// Type aliases for backward compatibility
typedef CssToken css_token_t;
typedef CssTokenType CSSTokenType;
typedef CssTokenStream CSSTokenStream;
typedef CssTokenizer CSSTokenizer;
typedef CssToken CSSToken;

#ifdef __cplusplus
}
#endif

#endif // CSS_TOKENIZER_H
