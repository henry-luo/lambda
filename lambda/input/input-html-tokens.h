/**
 * @file input-html-tokens.h
 * @brief HTML tokenization data structures and element classification
 *
 * This header contains element type arrays and character classification
 * functions extracted from the main HTML parser for modularization.
 */

#ifndef INPUT_HTML_TOKENS_H
#define INPUT_HTML_TOKENS_H

#ifdef __cplusplus
extern "C" {
#endif

// HTML5 void elements (self-closing tags)
extern const char* HTML5_VOID_ELEMENTS[];

// HTML5 semantic elements that should be parsed as containers
extern const char* HTML5_SEMANTIC_ELEMENTS[];

// HTML5 elements that contain raw text (like script, style)
extern const char* HTML5_RAW_TEXT_ELEMENTS[];

// HTML5 elements that should preserve whitespace
extern const char* HTML5_PREFORMATTED_ELEMENTS[];

// HTML5 block-level elements
extern const char* HTML5_BLOCK_ELEMENTS[];

// HTML5 inline elements
extern const char* HTML5_INLINE_ELEMENTS[];

// Element classification functions
bool html_is_semantic_element(const char* tag_name);
bool html_is_void_element(const char* tag_name);
bool html_is_raw_text_element(const char* tag_name);
bool html_is_preformatted_element(const char* tag_name);
bool html_is_block_element(const char* tag_name);
bool html_is_inline_element(const char* tag_name);

// HTML5 custom element validation
bool html_is_valid_custom_element_name(const char* name);

// Data attribute and ARIA checks
bool html_is_data_attribute(const char* attr_name);
bool html_is_aria_attribute(const char* attr_name);

// Optional end tag / auto-close support per HTML spec
// Returns true if opening <new_tag> should implicitly close <current_tag>
// Per HTML spec: DT/DD close each other, LI closes LI, P closes P, etc.
bool html_tag_closes_parent(const char* current_tag, const char* new_tag);

#ifdef __cplusplus
}
#endif

#endif // INPUT_HTML_TOKENS_H
