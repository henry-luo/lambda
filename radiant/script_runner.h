/**
 * Script Runner for Radiant Layout Engine
 *
 * Extracts and executes <script> elements (both inline and external) and
 * onload handlers from HTML documents during the layout pipeline. Scripts
 * run after DOM tree construction but before CSS cascade and layout.
 *
 * External scripts (<script src="..."> ) are downloaded or read from disk
 * using the same URL resolution and HTTP infrastructure as CSS/image loading.
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
struct Url;

/**
 * Execute all <script> elements (inline and external) and the body onload handler.
 *
 * Walks the original Element* tree (Lambda data model) in document order,
 * loads external script sources and extracts inline script text,
 * concatenates with the body onload attribute value, and JIT-compiles
 * the combined source via the JS transpiler.
 *
 * The DomDocument's DomElement* tree is the DOM context — all DOM mutations
 * from JavaScript (getElementById, appendChild, style changes, etc.) operate
 * on the DomElement* tree directly.
 *
 * @param html_root    Root Element* from the Lambda HTML parse tree
 * @param dom_doc      DomDocument with constructed DomElement* tree
 * @param pool         Memory pool for allocations
 * @param base_url     Base URL for resolving relative script src paths (nullable)
 */
void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool, Url* base_url);

/**
 * Control whether execute_document_scripts() keeps the compiled JS runtime alive
 * for later event dispatch. Static headless renders can release it immediately
 * after load-time scripts finish mutating the DOM.
 */
void script_runner_set_retain_js_state(bool retain);

/**
 * Control whether external <script src="..."> files are loaded and executed.
 * Static headless smoke renders keep inline scripts but skip external browser
 * libraries that do not affect the initial parsed HTML/CSS view.
 */
void script_runner_set_execute_external_scripts(bool execute);

/**
 * Collect and compile inline event handler attributes (onclick, onmouseover, etc.)
 * from the DomElement* tree. Uses the retained MIR context from execute_document_scripts()
 * so that handler code can reference functions defined in <script> blocks.
 *
 * Must be called AFTER execute_document_scripts() and only if dom_doc->js_mir_ctx is set.
 *
 * @param dom_doc  DomDocument with retained JS compilation state
 */
void collect_and_compile_event_handlers(DomDocument* dom_doc);

/**
 * Clean up retained JS compilation state on a DomDocument.
 * Destroys the MIR context, runtime pools, and event handler registry.
 * Called during document teardown.
 *
 * @param dom_doc  DomDocument to clean up
 */
void script_runner_cleanup_js_state(DomDocument* dom_doc);

#ifdef __cplusplus
}
#endif
