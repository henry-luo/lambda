// radiant/pdf/pages.hpp
// PDF Page Tree Navigation
//
// This module handles parsing the PDF Pages dictionary tree structure
// to extract individual pages and their content streams.

#ifndef PDF_PAGES_HPP
#define PDF_PAGES_HPP

#include "../../lambda/input/input.hpp"
#include "../view.hpp"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * PDF Page Information
 * Contains all data needed to render a specific page
 */
typedef struct {
    Array* content_streams;     // Array of content stream objects for this page
    Map* resources;             // Resources dictionary (fonts, images, etc.)
    double media_box[4];        // [x, y, width, height] - page dimensions
    double crop_box[4];         // Optional crop box
    int page_number;            // 1-based page number
    bool has_crop_box;          // Whether crop_box is valid
} PDFPageInfo;

/**
 * Get the number of pages in a PDF document
 *
 * @param pdf_data The parsed PDF data structure
 * @return Number of pages, or 0 on error
 */
int pdf_get_page_count_from_data(Map* pdf_data);

/**
 * Extract information for a specific page
 *
 * @param pdf_data The parsed PDF data structure
 * @param page_index Zero-based page index
 * @param pool Memory pool for allocations
 * @return PDFPageInfo structure, or NULL on error
 */
PDFPageInfo* pdf_get_page_info(Map* pdf_data, int page_index, Pool* pool);

/**
 * Resolve an indirect reference to an actual object
 *
 * @param pdf_data The parsed PDF data structure
 * @param ref_obj The indirect reference object (e.g., "5 0 R")
 * @param pool Memory pool for temporary allocations
 * @return The resolved object, or ITEM_NULL if not found
 */
Item pdf_resolve_reference(Map* pdf_data, Item ref_obj, Pool* pool);

/**
 * Extract MediaBox from page dictionary or inherited from parent
 *
 * @param page_dict The page dictionary
 * @param pdf_data The parsed PDF data (for resolving references)
 * @param media_box Output array [x, y, width, height]
 * @return true if MediaBox was found, false otherwise
 */
bool pdf_extract_media_box(Map* page_dict, Map* pdf_data, double* media_box);

#ifdef __cplusplus
}
#endif

#endif // PDF_PAGES_HPP
