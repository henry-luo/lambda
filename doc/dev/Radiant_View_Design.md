# Radiant View Command Design

This note documents the `lambda view` document-loading architecture and the current per-format flows in Radiant.

The view command is intentionally DOM-backed. Each supported input type is normalized into a `DomDocument` with a Lambda element root, a Radiant DOM tree, resolved CSS, and enough retained runtime state for interactive or generated documents. After that point, layout and rendering are shared: the document is laid out into a view tree and painted through the Radiant renderer.

## Shared Architecture

`lambda view <document>` enters the same high-level loading path as Radiant layout:

1. Parse the input URL relative to the current document/base URL.
2. Dispatch by scheme and file extension in `load_html_doc()`.
3. Build or generate a Lambda element tree.
4. Create a `DomDocument` with `dom_document_create()`.
5. Build the Radiant DOM tree with `build_dom_tree_from_element()`.
6. Parse and apply stylesheets plus inline `style=""` attributes.
7. Store document roots, stylesheets, URL, scale, and retained runtime state on `DomDocument`.
8. Run layout and render through the normal Radiant view pipeline.

The important design rule is that format-specific code should stop at producing a coherent `DomDocument`. Layout and rendering should not care whether the source was HTML, an image wrapper, XML, PDF-generated HTML, Lambda script output, LaTeX output, markdown, or source text.

Generated-node flows use retained ownership rather than serialize/reparse when possible. For `.ls`, `.tex`, and markdown math, generated Lambda elements are allocated in a retained arena and the owning `Runtime` is stored on `dom_doc->lambda_runtime` when those nodes must stay live after loading.

## 1. HTML And SVG

### HTML

HTML is the default loader for unknown extensions, `.html`, `.htm`, and HTTP/HTTPS URLs. The flow is implemented by `load_lambda_html_doc()`:

1. Load source from memory, local file, or HTTP.
2. Detect and convert non-UTF-8 charset when possible.
3. Parse with the HTML5 parser into a Lambda element tree.
4. Create a `DomDocument` and build the Radiant DOM tree.
5. Extract viewport and `<base href>`.
6. Collect external `<link rel="stylesheet">`, inline `<style>`, and `@import` stylesheets in document order.
7. Apply inline `style=""` attributes before scripts.
8. Execute document scripts and body load handlers.
9. Re-scan inline styles when scripts mutate style elements.
10. Compile inline event handlers.
11. Apply CSS cascade.
12. Store roots, stylesheet cache, document URL, scale, and HTML version.

HTML is the richest path. It supports script execution, stylesheet order, charset handling, event handlers, `getComputedStyle`, pseudo-state recascade support, and runtime DOM mutations.

### SVG Files

External `.svg` files are currently loaded as DOM-backed image documents through `load_svg_doc()`, which delegates to the shared image wrapper path. The SVG file becomes the `src` of a generated HTML `<img>` element.

This keeps top-level SVG viewing on the normal HTML/image interaction path: scrolling, selection behavior around the element, future context menus, and image-level zoom can be implemented against a DOM document rather than a special root renderer.

Inline SVG inside HTML or generated script output is different: it remains inline DOM content and is rendered through the inline SVG rendering path.

## 2. Image With Generated `<img>` Element

Raster image inputs (`.png`, `.jpg`, `.jpeg`, `.gif`) and top-level `.svg` use the DOM image document flow:

1. Resolve the image URL/path.
2. Escape the image URL and filename for HTML attributes.
3. Generate a small HTML document:
   - `html`
   - `head`
   - minimal page CSS
   - `body data-rdt-document="image"`
   - `img.rdt-image-document src="..." alt="..."`
4. Pass that in-memory HTML string to `load_lambda_html_doc()`.

The generated wrapper makes image viewing a normal DOM-backed page. This is the right shape for future work such as zoom controls, copy image, context menu commands, selection affordances, drag/drop, and image metadata overlays.

The wrapper CSS currently keeps the image unscaled by default:

```css
img.rdt-image-document {
  display: block;
  max-width: none;
  height: auto;
  user-select: auto;
}
```

## 3. XML With Stylesheet

XML uses `load_xml_doc()`.

The XML loader only renders XML as styled content when the parsed XML declares an `<?xml-stylesheet?>` directive. The flow is:

1. Read the XML file.
2. Parse with `parse_xml()` into a Lambda XML document tree.
3. If no XML stylesheet href is found, fall back to the source-text viewer.
4. Find the first real XML root element, skipping processing instructions and comments.
5. Load and parse the referenced CSS stylesheet.
6. Create a `DomDocument`.
7. Wrap XML content in synthetic `html > body`.
8. Build Radiant DOM for the XML root under the synthetic body.
9. Store the stylesheet on `DomDocument`.
10. Apply CSS cascade and inline `style=""` attributes.

The synthetic HTML/body wrapper lets XML reuse the normal block layout root assumptions while preserving XML element names as custom DOM tags for selector matching.

## 4. Source Documents Like `*.yaml`

Source-like text inputs include `.json`, `.yaml`, `.yml`, `.toml`, `.txt`, `.csv`, `.ini`, `.conf`, `.cfg`, and `.log`. These use `load_text_doc()`.

The source viewer flow is:

1. Read the source file as text.
2. Escape HTML-sensitive characters.
3. Generate a small HTML document containing the escaped source in `<pre>`.
4. Add inline CSS for a monospace source view.
5. Parse the generated HTML.
6. Build `DomDocument` and Radiant DOM.
7. Extract and apply the inline stylesheet.

This path is intentionally a viewer, not a semantic document renderer. For example, YAML is displayed as source text rather than converted into a rich table or tree. That keeps arbitrary data files safe and predictable while still going through the normal layout/render stack.

## 5. Dynamic Documents: `*.pdf`, `*.ls`, `*.tex`

Dynamic documents generate a Lambda element tree before DOM construction. The design goal is retained generated nodes: generate once, keep the produced tree and runtime ownership live, and avoid deep copy or serialize/reparse roundtrips.

### Lambda Script Documents (`*.ls`)

`.ls` files use `load_lambda_script_doc()`, which delegates to `load_lambda_script_source_doc()`.

Flow:

1. Allocate a heap `Runtime`.
2. Create a retained result `Input` with `ui_mode=true`.
3. Set `runtime->ui_mode=true` and `runtime->result_arena=result_input->arena`.
4. Run the Lambda script with MIR.
5. Interpret the script result:
   - full `<html>` element: use directly
   - other element: wrap in `html > body`
   - scalar/text value: stringify and wrap in a generated `<div>`
   - SVG element/string: wrap in an in-memory HTML document so inline SVG rendering handles it
   - HTML string: parse as in-memory HTML
6. Build `DomDocument` from the retained result input.
7. Apply `script.css` for non-HTML results, or collect inline styles for full HTML results.
8. Store stylesheets and `dom_doc->lambda_runtime`.

The retained runtime keeps heap, JIT code, name pool, generated nodes, and event handler state live for interactive script documents.

### PDF Documents (`*.pdf`)

PDF view uses an in-memory Lambda bridge script rather than a temporary `.ls` file.

Flow:

1. Resolve the PDF path.
2. Build a small Lambda script in memory:
   - import the PDF package
   - load the PDF with `input(path, 'pdf')`
   - call `pdf.pdf_to_html(doc, {max_pages: 48})`
3. Pass that script source to `load_lambda_script_source_doc()`.
4. Continue through the retained `.ls` generated-node path.

This means PDF rendering is a dynamic HTML-generation path. It should not write bridge scripts into `./temp`; the bridge source is held in memory.

### LaTeX Documents (`*.tex`, `*.latex`)

LaTeX uses `load_latex_doc()`.

Flow:

1. Build an in-memory Lambda script that imports `lambda.package.latex.latex`.
2. Load the `.tex` file through `input(path, {type: "latex"})`.
3. Render to standalone HTML with `latex.render(ast, {standalone: true})`.
4. Allocate generated nodes in a retained result input arena.
5. Require the script result to be an HTML element tree.
6. Build Radiant DOM directly from that generated tree.
7. Apply default LaTeX CSS, KaTeX CSS, inline `<style>` elements, and inline `style=""`.
8. Store stylesheets and `dom_doc->lambda_runtime`.

The current design intentionally avoids the older stringify-to-HTML and reparse fallback. LaTeX package output is retained and passed directly to DOM construction.

## 6. Markdown, Wiki, And Markdown With Math

### Markdown

Markdown uses `load_markdown_doc()`.

Flow:

1. Read the markdown file.
2. Parse through `input_from_source(..., "markdown")`.
3. Find the root element from the parser result.
4. Optionally run the markdown math subflow described below.
5. Build `DomDocument` and Radiant DOM from the parsed element tree.
6. Load `input/markdown.css`.
7. Load math CSS and KaTeX CSS when present.
8. Apply CSS cascade and inline styles.
9. Store stylesheets and, when math generated nodes were retained, `dom_doc->lambda_runtime`.

The parsed markdown input remains a normal parsed input. It is not globally switched to `ui_mode`; generated math nodes are retained separately.

### Markdown With Math

Markdown math is handled as a subflow inside `load_markdown_doc()`.

The markdown parser emits `<math>` elements for inline and block math. The view loader then:

1. Walks the parsed markdown element tree.
2. Collects math nodes with parent pointer, child index, source text, and display/inline mode.
3. Builds one in-memory Lambda script that imports `lambda.package.math.math` and renders every math expression in a batch.
4. Allocates a heap `Runtime` with `ui_mode=true`.
5. Sets `math_runtime->result_arena=input->arena` so generated nodes are allocated into the retained markdown document arena.
6. Runs the script with MIR.
7. Splices returned rendered elements directly into the markdown tree.
8. Stores the math runtime on `dom_doc->lambda_runtime` if any generated math node is used.

There is no deep copy and no HTML serialization/reparse in this subflow. The runtime is retained because the generated nodes may depend on runtime-owned structures.

### Wiki

Wiki uses `load_wiki_doc()`.

Flow:

1. Read the wiki file.
2. Parse through `input_from_source(..., "wiki")`.
3. Find the root element from the parser result.
4. Build `DomDocument` and Radiant DOM.
5. Load `input/wiki.css`.
6. Apply CSS cascade.
7. Store document roots and URL.

Wiki is currently simpler than HTML and markdown: it does not run scripts or a math-generation pass.

## Design Invariants

- Every loader returns a `DomDocument` ready for shared layout and rendering.
- Format-specific parsing/generation should happen before DOM construction.
- Generated document flows should prefer retained Lambda element trees over deep copy or serialize/reparse.
- Dynamic flows that need runtime-owned nodes or event execution must retain their `Runtime` on `dom_doc->lambda_runtime`.
- Source viewers and image viewers should still be DOM-backed HTML documents so future interaction features reuse normal view infrastructure.
- Top-level XML without an XML stylesheet is source text, not an unstyled custom-element page.

