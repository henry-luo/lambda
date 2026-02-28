/**
 * Script Runner for Radiant Layout Engine
 *
 * Extracts and executes inline <script> elements and onload handlers
 * from HTML documents during the layout pipeline. Scripts run after
 * DOM tree construction but before CSS cascade and layout.
 *
 * Integration point: called from load_lambda_html_doc() in cmd_layout.cpp
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations (avoid including heavy headers)
struct Element;
struct DomDocument;
struct Pool;

/**
 * Execute all inline <script> elements and the body onload handler.
 *
 * Walks the original Element* tree (Lambda data model) in document order,
 * extracts inline <script> source text, concatenates with the body onload
 * attribute value, and JIT-compiles the combined source via the JS transpiler.
 *
 * The DomDocument's DomElement* tree is the DOM context — all DOM mutations
 * from JavaScript (getElementById, appendChild, style changes, etc.) operate
 * on the DomElement* tree directly.
 *
 * @param html_root    Root Element* from the Lambda HTML parse tree
 * @param dom_doc      DomDocument with constructed DomElement* tree
 * @param pool         Memory pool for allocations
 */
void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool);

#ifdef __cplusplus
}
#endif
