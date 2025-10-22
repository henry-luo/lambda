#ifndef CSS_TOKENIZER_H
#define CSS_TOKENIZER_H

// Include new CSS system headers
#include "css_parser.h"
#include "css_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// Note: This file is now a compatibility header.
// The main tokenizer types and functions are defined in css_parser.h

/**
 * CSS Tokenizer Compatibility Header
 * 
 * This file provides backward compatibility for code that included
 * the old css_tokenizer_enhanced.h. All tokenizer types and functions
 * are now defined in css_parser.h.
 */

// Type aliases for backward compatibility
typedef CssToken css_token_t;
typedef CssTokenType CSSTokenType;
typedef CssTokenStream CSSTokenStream;
typedef CssTokenizer CSSTokenizer;
typedef CssToken CSSToken;

// Function aliases for backward compatibility
#define css_tokenizer_enhanced_create css_tokenizer_create
#define css_tokenizer_enhanced_destroy css_tokenizer_destroy
#define css_tokenizer_enhanced_tokenize css_tokenizer_tokenize
#define css_tokenize_enhanced css_tokenize

#ifdef __cplusplus
}
#endif

#endif // CSS_TOKENIZER_H
