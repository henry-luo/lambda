#ifndef LAMBDA_CSS_RESOLVE_H
#define LAMBDA_CSS_RESOLVE_H

#include "layout.hpp"
#include "../lambda/input/css/dom_element.h"
#include "../lambda/input/css/css_style.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lambda CSS Style Resolution for Radiant Layout Engine
 *
 * Parallel implementation to resolve_element_style() that works with
 * Lambda CSS DomElement structures instead of Lexbor elements.
 *
 * Key Design:
 * - Separate function for clean separation of Lexbor vs Lambda CSS paths
 * - Maps Lambda CSS keyword strings to Lexbor enum values
 * - Converts Lambda CssValue structures to Radiant property structures
 * - Maintains same behavior as Lexbor resolution for layout compatibility
 */

// ============================================================================
// Keyword Mapping: Lambda CSS strings → Lexbor enum values
// ============================================================================

/**
 * Map CSS keyword string to Lexbor enum value
 *
 * @param keyword CSS keyword string (e.g., "block", "inline", "flex")
 * @return Lexbor LXB_CSS_VALUE_* constant, or 0 if unknown
 */
int map_css_keyword_to_lexbor(const char* keyword);

/**
 * Map Lambda color keyword to RGBA value
 * @param keyword const char* keyword string (e.g., "red", "blue")
 * @return uint32_t RGBA color (0xRRGGBBAA format)
 */
uint32_t map_lambda_color_keyword(const char* keyword);

/**
 * Map Lambda font-size keyword to pixel value
 * @param keyword const char* keyword string (e.g., "small", "large")
 * @return float font size in pixels
 */
float map_lambda_font_size_keyword(const char* keyword);

/**
 * Map Lambda font-weight keyword to numeric value
 * @param keyword const char* keyword string (e.g., "normal", "bold")
 * @return int font weight (100-900)
 */
int map_lambda_font_weight_keyword(const char* keyword);

/**
 * Map Lambda font-family keyword to font name
 * @param keyword const char* keyword string (e.g., "serif", "sans-serif")
 * @return const char* font family name
 */
const char* map_lambda_font_family_keyword(const char* keyword);

// ============================================================================
// Value Conversion: Lambda CSS → Radiant Property Structures
// ============================================================================

/**
 * Convert Lambda CSS length/percentage to pixels
 *
 * @param value CssValue with length or percentage type
 * @param lycon Layout context for font size, viewport calculations
 * @param prop_id Property ID for context-specific resolution
 * @return Float value in pixels
 */
float convert_lambda_length_to_px(const CssValue* value, LayoutContext* lycon,
                                   CssPropertyId prop_id);

/**
 * Convert Lambda CSS color to Radiant Color type
 *
 * @param value CssValue with color type
 * @return Color struct (RGBA format)
 */
Color convert_lambda_color(const CssValue* value);

// /**
//  * Convert Lambda CSS spacing value to Radiant SpacingProp
//  * Handles margin and padding properties
//  *
//  * @param prop_id Property ID (margin, padding, etc.)
//  * @param value CssValue with spacing value
//  * @param target Target SpacingProp structure to populate
//  * @param lycon Layout context
//  * @param specificity CSS specificity for cascade
//  */
// void convert_lambda_spacing_property(CssPropertyId prop_id, const CssValue* value,
//                                       SpacingProp* target, LayoutContext* lycon,
//                                       int32_t specificity);

/**
 * Get specificity value from Lambda CSS declaration
 * Converts Lambda CssSpecificity to Lexbor-compatible int32_t
 *
 * @param decl CSS declaration with specificity info
 * @return int32_t specificity value
 */
int32_t get_lambda_specificity(const CssDeclaration* decl);

// ============================================================================
// Main Style Resolution
// ============================================================================

/**
 * Resolve Lambda CSS styles for a DomElement
 * Parallel function to resolve_element_style() for Lexbor
 *
 * Iterates through DomElement->specified_style AVL tree and applies
 * each CSS declaration to the LayoutContext view properties.
 *
 * @param dom_elem DomElement with specified_style tree
 * @param lycon Layout context with current view
 */
void resolve_lambda_css_styles(DomElement* dom_elem, LayoutContext* lycon);

/**
 * Process single Lambda CSS property declaration
 * Called for each property in DomElement->specified_style
 *
 * @param prop_id CSS property ID
 * @param decl CSS declaration with value
 * @param lycon Layout context
 */
void resolve_lambda_css_property(CssPropertyId prop_id, const CssDeclaration* decl,
                                  LayoutContext* lycon);

#ifdef __cplusplus
}
#endif

#endif // LAMBDA_CSS_RESOLVE_H
