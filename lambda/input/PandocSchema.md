# Pandoc XML Schema Documentation

This document provides a comprehensive XML schema for representing document structures from Markdown, wiki, and HTML inputs, based on Pandoc’s Abstract Syntax Tree (AST) as described in the [Pandoc API docum## Element and Attribute Documentation

The schema uses HTML elements where possible (e.g., `<p>`, `<h1>`) and custom XML tags for Pandoc-specific features (e.g., `<cite>`, `<math>`, `<mark>`). This refactored version extensively uses `<mark>` elements to dem### Design Principles
- **HTML Priority**: Uses HTML elements (e.g., `<p>`, `<h1>`, `<a>`) for familiar structures, enhancing compatibility with web-based workflows.
- **Pandoc AST Fallback**: Custom XML tags (e.g., `<cite>`, `<math>`, `<mark>`) are used for Pandoc-specific features without direct HTML equivalents.
- **Mark Element Integration**: This refactored version extensively uses `<mark>` elements to demonstrate text highlighting, semantic annotation, and content categorization throughout the document structure.
- **Comprehensive Coverage**: Supports all Pandoc formatting options, including citations, math, figures, and code blocks, with detailed attributes.
- **Attribute Structure**: Reflects Pandoc's `Attr` type (identifier, classes, key-value pairs) and HTML attributes, ensuring flexibility.ate text highlighting and annotation capabilities. Attributes reflect Pandoc's AST `Attr` type (identifier, classes, key-value pairs) and HTML attributes. Below is a detailed list of elements and their attributes.ation](https://pandoc.org/using-the-pandoc-api.html). The schema prioritizes HTML elements where possible and uses custom XML tags for Pandoc-specific features, supporting document transformation and validation. It includes all Pandoc formatting options, such as citations, math, figures, and code blocks, with detailed attribute documentation.

## XML Schema

Below is the complete XML schema sample, including citations and a variety of block and inline elements.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- XML Schema for Pandoc AST with Mark elements and comprehensive attribute documentation -->
<pandoc version="1.0">
  <!-- Metadata section for document properties -->
  <meta>
    <!-- Field for key-value metadata -->
    <field name="title" type="string">
      <inlines>
        <mark id="title-mark" class="highlight important" data-type="title">
          <str>Sample Document</str>
        </mark>
      </inlines>
    </field>
    <field name="author" type="string">
      <inlines>
        <mark id="author-mark" class="highlight author" data-role="creator">
          <str>John Doe</str>
        </mark>
      </inlines>
    </field>
    <field name="date" type="string">
      <inlines>
        <mark id="date-mark" class="highlight date" data-format="iso">
          <str>2025-07-25</str>
        </mark>
      </inlines>
    </field>
    <!-- References for citations -->
    <field name="references" type="list">
      <inlines>
        <reference id="smith2020" type="book">
          <mark class="citation-text" data-type="book">
            <str>Smith, J. (2020). Understanding Pandoc. Academic Press.</str>
          </mark>
        </reference>
        <reference id="jones2021" type="article">
          <mark class="citation-text" data-type="article">
            <str>Jones, A. (2021). Document Transformation. Journal of Docs, 12(3), 45-60.</str>
          </mark>
        </reference>
      </inlines>
    </field>
  </meta>
  <!-- Document body with block-level elements -->
  <body>
    <!-- Header with identifier, classes, and key-value attributes -->
    <h1 id="intro" class="section main" level="1" data-custom="value">
      <mark id="header-mark" class="highlight header" data-level="1">
        Introduction
      </mark>
    </h1>
    <!-- Paragraph with inline elements and citation -->
    <p id="p1" class="text">
      This document demonstrates 
      <mark id="feature-mark" class="highlight feature" data-importance="high">
        Pandoc's features
      </mark>, including citations 
      <cite>
        <citations>
          <citation id="smith2020" prefix="see " suffix=", p. 15" mode="NormalCitation" note-num="1" hash="0"/>
          <citation id="jones2021" prefix="" suffix="" mode="AuthorInText" note-num="2" hash="1"/>
        </citations>
      </cite>.
    </p>
    <!-- LineBlock for poetry or addresses -->
    <line-block id="poem1">
      <line>
        <mark id="poem-line1" class="highlight poetry" data-stanza="1">
          <str>First line with </str><em>emphasis</em>
        </mark>
      </line>
      <line>
        <mark id="poem-line2" class="highlight poetry" data-stanza="1">
          <str>Second line</str>
        </mark>
      </line>
    </line-block>
    <!-- CodeBlock with language and attributes -->
    <pre id="code1" class="sourceCode">
      <code language="python" data-executable="true">
        <mark id="code-mark" class="highlight code" data-language="python">
          print("Hello, world!")
        </mark>
      </code>
    </pre>
    <!-- RawBlock for format-specific content -->
    <raw format="html" id="raw1">
      <mark id="raw-mark" class="highlight raw" data-format="html">
        <div>Raw HTML content</div>
      </mark>
    </raw>
    <!-- BlockQuote -->
    <blockquote id="quote1" class="quote">
      <p>
        <mark id="quote-mark" class="highlight quote" data-type="blockquote">
          Quoted text here.
        </mark>
      </p>
    </blockquote>
    <!-- OrderedList with list attributes -->
    <ol id="list1" start="1" type="1" class="numbered" delim="period" style="decimal">
      <li>
        <p>
          <mark id="list-item1" class="highlight list-item" data-number="1">
            Item 1
          </mark>
        </p>
      </li>
      <li>
        <p>
          <mark id="list-item2" class="highlight list-item" data-number="2">
            Item 2 with <strong>strong</strong> text
          </mark>
        </p>
      </li>
    </ol>
    <!-- BulletList -->
    <ul id="list2" class="bulleted">
      <li>
        <p>
          <mark id="bullet-item1" class="highlight bullet-item" data-type="bullet">
            Item A
          </mark>
        </p>
      </li>
      <li>
        <p>
          <mark id="bullet-item2" class="highlight bullet-item" data-type="bullet">
            Item B
          </mark>
        </p>
      </li>
    </ul>
    <!-- DefinitionList -->
    <dl id="deflist1">
      <dt>
        <mark id="term-mark" class="highlight term" data-type="definition-term">
          <str>Term</str>
        </mark>
      </dt>
      <dd>
        <p>
          <mark id="def-mark" class="highlight definition" data-type="definition-desc">
            Definition with <a href="http://example.com">link</a>.
          </mark>
        </p>
      </dd>
    </dl>
    <!-- HorizontalRule -->
    <hr id="hr1"/>
    <!-- Table with alignment and width attributes -->
    <table id="table1" class="data">
      <caption>
        <mark id="table-caption" class="highlight caption" data-type="table">
          <str>Sample Table</str>
        </mark>
      </caption>
      <colgroup>
        <col align="left" width="50%"/>
        <col align="center" width="50%"/>
      </colgroup>
      <thead>
        <tr>
          <th align="left">
            <p>
              <mark id="header1-mark" class="highlight table-header" data-col="1">
                Header 1
              </mark>
            </p>
          </th>
          <th align="center">
            <p>
              <mark id="header2-mark" class="highlight table-header" data-col="2">
                Header 2
              </mark>
            </p>
          </th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td align="left">
            <p>
              <mark id="cell1-mark" class="highlight table-cell" data-row="1" data-col="1">
                Cell 1
              </mark>
            </p>
          </td>
          <td align="center">
            <p>
              <mark id="cell2-mark" class="highlight table-cell" data-row="1" data-col="2">
                Cell 2
              </mark>
            </p>
          </td>
        </tr>
      </tbody>
    </table>
    <!-- Div with attributes -->
    <div id="section1" class="section" data-role="container">
      <p>
        <mark id="div-content" class="highlight content" data-container="div">
          Content in a div with <sub>subscript</sub>.
        </mark>
      </p>
    </div>
    <!-- Figure with image and caption -->
    <figure id="fig1" class="image">
      <img src="image.jpg" alt="Sample image" title="Image" width="300" height="200"/>
      <figcaption>
        <mark id="caption-mark" class="highlight caption" data-type="figure">
          <str>Figure 1: Sample image description</str>
        </mark>
      </figcaption>
    </figure>
    <!-- Footnote -->
    <note id="note1">
      <p>
        <mark id="footnote-mark" class="highlight footnote" data-type="note">
          Footnote content with <em>emphasis</em>.
        </mark>
      </p>
    </note>
    <!-- Math (display) -->
    <math type="display" id="math1">
      <mark id="math-mark" class="highlight math" data-type="display">
        \[ E = mc^2 \]
      </mark>
    </math>
  </body>
</pandoc>
```

## Element and Attribute Documentation

The schema uses HTML elements where possible (e.g., `<p>`, `<h1>`) and custom XML tags for Pandoc-specific features (e.g., `<cite>`, `<math>`). Attributes reflect Pandoc’s AST `Attr` type (identifier, classes, key-value pairs) and HTML attributes. Below is a detailed list of elements and their attributes.

### Root Element
- **`<pandoc>`**
  - **Attributes**:
    - `version`: String, schema version (e.g., "1.0").
  - **Purpose**: Root element containing metadata and body.

### Metadata Elements
- **`<meta>`**
  - **Attributes**: None.
  - **Purpose**: Contains `<field>` elements for document metadata.
  - **Content**: Zero or more `<field>` elements.
- **`<field>`**
  - **Attributes**:
    - `name`: String, required, metadata key (e.g., "title", "author", "references").
    - `type`: String, optional, data type (e.g., "string", "list", "map").
  - **Purpose**: Represents a metadata key-value pair.
  - **Content**: `<inlines>` or `<blocks>` (typically `<inlines>` for simple metadata).
- **`<reference>`** (within `<field name="references">`)
  - **Attributes**:
    - `id`: String, required, unique citation identifier (e.g., "smith2020").
    - `type`: String, optional, citation type (e.g., "book", "article").
  - **Purpose**: Stores citation details for use in `<cite>`.
  - **Content**: Inline elements (e.g., `<str>`).

### Block-Level Elements
- **`<p>`** (Para, Plain)
  - **Attributes**:
    - `id`: String, optional, unique identifier.
    - `class`: Space-separated strings, optional, CSS classes (e.g., "text").
    - `data-*`: Custom key-value pairs, optional, for metadata.
  - **Purpose**: Paragraphs or plain text blocks.
  - **Content**: Inline elements.
- **`<line-block>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Groups lines for poetry or addresses.
  - **Content**: One or more `<line>` elements.
- **`<line>`**
  - **Attributes**: None.
  - **Purpose**: A single line within `<line-block>`.
  - **Content**: Inline elements.
- **`<pre>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Wraps `<code>` for code blocks.
  - **Content**: `<code>` element.
- **`<code>`** (within `<pre>`)
  - **Attributes**:
    - `language`: String, optional, programming language (e.g., "python").
    - `data-executable`: Boolean, optional, indicates executable code ("true", "false").
  - **Purpose**: Contains code with syntax highlighting.
  - **Content**: Raw code text.
- **`<raw>`**
  - **Attributes**:
    - `format`: String, required, content format (e.g., "html", "latex").
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Embeds format-specific content.
  - **Content**: Raw text in specified format.
- **`<blockquote>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Quoted content.
  - **Content**: Block elements.
- **`<ol>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `start`: Integer, optional, starting number (default: 1).
    - `type`: String, optional, numbering style (e.g., "1", "A", "i").
    - `delim`: String, optional, delimiter (e.g., "period", "paren").
    - `style`: String, optional, list style (e.g., "decimal", "upper-alpha").
  - **Purpose**: Ordered list with customizable numbering.
  - **Content**: `<li>` elements.
- **`<ul>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Unordered list.
  - **Content**: `<li>` elements.
- **`<li>`**
  - **Attributes**: None.
  - **Purpose**: List item.
  - **Content**: Block elements.
- **`<dl>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Definition list.
  - **Content**: `<dt>` and `<dd>` elements.
- **`<dt>`**
  - **Attributes**: None.
  - **Purpose**: Definition term.
  - **Content**: Inline elements.
- **`<dd>`**
  - **Attributes**: None.
  - **Purpose**: Definition description.
  - **Content**: Block elements.
- **`<h1>` to `<h6>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `level`: Integer, required, header level (1 to 6).
  - **Purpose**: Headers with level-specific tags.
  - **Content**: Inline elements.
- **`<hr>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Horizontal rule.
  - **Content**: None (empty element).
- **`<table>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Table container.
  - **Content**: `<caption>`, `<colgroup>`, `<thead>`, `<tbody>`.
- **`<caption>`**
  - **Attributes**: None.
  - **Purpose**: Table caption.
  - **Content**: Inline elements.
- **`<colgroup>`**
  - **Attributes**: None.
  - **Purpose**: Groups column specifications.
  - **Content**: `<col>` elements.
- **`<col>`**
  - **Attributes**:
    - `align`: String, optional, column alignment (e.g., "left", "center", "right").
    - `width`: String, optional, column width (e.g., "50%").
  - **Purpose**: Specifies column properties.
  - **Content**: None (empty element).
- **`<thead>`**, **`<tbody>`**
  - **Attributes**: None.
  - **Purpose**: Table header and body.
  - **Content**: `<tr>` elements.
- **`<tr>`**
  - **Attributes**: None.
  - **Purpose**: Table row.
  - **Content**: `<th>` or `<td>` elements.
- **`<th>`**, **`<td>`**
  - **Attributes**:
    - `align`: String, optional, cell alignment (e.g., "left", "center", "right").
    - `rowspan`, `colspan`: Integer, optional, cell spanning.
  - **Purpose**: Table header or data cell.
  - **Content**: Block elements.
- **`<div>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Generic block container.
  - **Content**: Block elements.
- **`<figure>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Contains image and caption.
  - **Content**: `<img>`, `<figcaption>`.
- **`<img>`**
  - **Attributes**:
    - `src`: String, required, image URL.
    - `alt`: String, optional, alternative text.
    - `title`: String, optional, image title.
    - `width`, `height`: String, optional, dimensions (e.g., "300", "200px").
  - **Purpose**: Embeds images.
  - **Content**: None (empty element).
- **`<figcaption>`**
  - **Attributes**: None.
  - **Purpose**: Figure caption.
  - **Content**: Inline elements.
- **`<note>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Footnote content.
  - **Content**: Block elements.
- **`<math>`**
  - **Attributes**:
    - `type`: String, required, math type ("inline" or "display").
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Contains LaTeX math expressions.
  - **Content**: LaTeX code.

### Inline-Level Elements
- **`<str>`**
  - **Attributes**: None.
  - **Purpose**: Unformatted text.
  - **Content**: Plain text.
- **`<mark>`**
  - **Attributes**:
    - `id`: String, optional, unique identifier.
    - `class`: Space-separated strings, optional, CSS classes (e.g., "highlight", "important").
    - `data-*`: Custom key-value pairs, optional, for metadata (e.g., "data-type", "data-importance").
  - **Purpose**: Marked or highlighted text. Used extensively throughout this schema to demonstrate text highlighting, annotation, and semantic markup capabilities. Can wrap any inline content to indicate importance, categorization, or special meaning.
  - **Content**: Inline elements (including nested marks, text, emphasis, etc.).
- **`<em>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Emphasized text.
  - **Content**: Inline elements.
- **`<strong>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Strong text.
  - **Content**: Inline elements.
- **`<s>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Strikethrough text.
  - **Content**: Inline elements.
- **`<sup>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Superscript text.
  - **Content**: Inline elements.
- **`<sub>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Subscript text.
  - **Content**: Inline elements.
- **`<span>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `style`: String, optional, inline CSS (e.g., "font-variant: small-caps").
  - **Purpose**: Generic inline container (e.g., for SmallCaps).
  - **Content**: Inline elements.
- **`<q>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `type`: String, required, quote type ("single" or "double").
  - **Purpose**: Quoted text.
  - **Content**: Inline elements.
- **`<cite>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Wraps `<citations>` for citation content.
  - **Content**: `<citations>` element.
- **`<citations>`**
  - **Attributes**: None.
  - **Purpose**: Groups multiple `<citation>` elements.
  - **Content**: One or more `<citation>` elements.
- **`<citation>`**
  - **Attributes**:
    - `id`: String, required, citation identifier (e.g., "smith2020").
    - `prefix`: String, optional, text before citation (e.g., "see ").
    - `suffix`: String, optional, text after citation (e.g., ", p. 15").
    - `mode`: String, required, citation mode ("NormalCitation", "AuthorInText", "SuppressAuthor").
    - `note-num`: Integer, optional, footnote number for citations in notes.
    - `hash`: Integer, optional, unique hash for citation instance.
  - **Purpose**: Represents a single citation with Pandoc’s attributes.
  - **Content**: None (empty element).
- **`<code>`** (inline)
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `language`: String, optional, programming language.
  - **Purpose**: Inline code.
  - **Content**: Code text.
- **`<raw>`** (inline)
  - **Attributes**:
    - `format`: String, required, content format (e.g., "html", "latex").
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Raw inline content.
  - **Content**: Raw text.
- **`<a>`**
  - **Attributes**:
    - `href`: String, required, link URL.
    - `title`: String, optional, link title.
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Hyperlink.
  - **Content**: Inline elements.
- **`<br>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Line break.
  - **Content**: None (empty element).

## Overall Notes

### Purpose
The XML schema is designed to represent document structures from Markdown, wiki, and HTML inputs, leveraging Pandoc’s AST for semantic richness and HTML elements for web compatibility. It supports document transformation (e.g., Markdown to HTML) and validation, ensuring structural integrity across formats.

### Design Principles
- **HTML Priority**: Uses HTML elements (e.g., `<p>`, `<h1>`, `<a>`) for familiar structures, enhancing compatibility with web-based workflows.
- **Pandoc AST Fallback**: Custom XML tags (e.g., `<cite>`, `<math>`) are used for Pandoc-specific features without direct HTML equivalents.
- **Comprehensive Coverage**: Supports all Pandoc formatting options, including citations, math, figures, and code blocks, with detailed attributes.
- **Attribute Structure**: Reflects Pandoc’s `Attr` type (identifier, classes, key-value pairs) and HTML attributes, ensuring flexibility.

### Citation System
- **Structure**: Citations are represented with `<cite><citations><citation>...</citation></citations></cite>`. Each `<citation>` references a `<reference>` in `<meta><field name="references">` via the `id` attribute.
- **Attributes**: The `<citation>` element includes:
  - `id`: Links to a reference.
  - `prefix`, `suffix`: Contextual text (e.g., "see [Smith, 2020, p. 15]").
  - `mode`: Citation style ("NormalCitation", "AuthorInText", "SuppressAuthor").
  - `note-num`: Footnote number for note-based citations.
  - `hash`: Unique identifier for citation instances.
- **Example**: The sample includes citations for "smith2020" and "jones2021", demonstrating different modes and attributes.

### Mark System
- **Structure**: This refactored version uses `<mark>` elements extensively to wrap content throughout the document, demonstrating text highlighting and semantic annotation capabilities.
- **Attributes**: The `<mark>` element includes:
  - `id`: Unique identifier for the marked content (e.g., "title-mark", "feature-mark").
  - `class`: CSS classes for styling and categorization (e.g., "highlight", "important", "feature").
  - `data-*`: Custom metadata for semantic meaning (e.g., "data-type", "data-importance", "data-role").
- **Usage Examples**:
  - **Metadata highlighting**: Title, author, and date fields wrapped in semantic marks
  - **Content categorization**: Different mark classes for headers, lists, tables, code, etc.
  - **Importance indicators**: Using `data-importance="high"` for critical content
  - **Type classification**: Using `data-type` attributes to categorize different content types
- **Benefits**: Enables fine-grained content analysis, styling control, and semantic processing while maintaining document structure.

### Usage
- **Transformation**: The schema facilitates conversion between formats (e.g., Markdown to LaTeX) by preserving semantic structure.
- **Validation**: Ensures documents conform to Pandoc’s AST with valid element nesting and attribute values.
- **Compatibility**: Supports Markdown (e.g., `#`, `*`, `[@ref]`), wiki (headings, links), and HTML structures.

### References
- [Pandoc API Documentation](https://pandoc.org/using-the-pandoc-api.html)
- [Pandoc Types Documentation](https://hackage.haskell.org/package/pandoc-types/docs/Text-Pandoc-Definition.html)

This Markdown document can be downloaded and rendered using tools like Pandoc to generate HTML, PDF, or other formats.