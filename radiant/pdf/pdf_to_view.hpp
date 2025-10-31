// radiant/pdf/pdf_to_view.hpp
// Main API for converting PDF documents to Radiant View Trees

#ifndef PDF_TO_VIEW_HPP
#define PDF_TO_VIEW_HPP

#include "../view.hpp"
#include "../../lambda/lambda-data.hpp"
#include "operators.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert PDF data to Radiant View Tree
 *
 * @param input Input context with parsed PDF data
 * @param pdf_root Root item from PDF parser (should be a Map)
 * @return ViewTree* or nullptr on error
 */
ViewTree* pdf_to_view_tree(Input* input, Item pdf_root);

/**
 * Convert a specific page from PDF to View Tree
 *
 * @param input Input context with parsed PDF data
 * @param pdf_root Root item from PDF parser
 * @param page_index Zero-based page index
 * @return ViewTree* or nullptr on error
 */
ViewTree* pdf_page_to_view_tree(Input* input, Item pdf_root, int page_index);

/**
 * Get number of pages in PDF
 *
 * @param pdf_root Root item from PDF parser
 * @return Number of pages or 0 on error
 */
int pdf_get_page_count(Item pdf_root);

#ifdef __cplusplus
}
#endif

#endif // PDF_TO_VIEW_HPP
