# Pandoc Mark Schema Documentation

This document provides a comprehensive Mark schema for representing document structures from Markdown, wiki, and HTML inputThe schema uses HTML elements where possible (e.g., `<p>`, `<h1>`) and custom Mark tags for Pandoc-specific features (e.g., `<cite>`, `<math>`). Attributes reflect Pandoc's AST `Attr` type (identifier, classes, key-value pairs) and HTML attributes. Belo- **Attribute Structure**: Reflects Pandoc's `Attr` type (identifier, classes, key-value pairs) and HTML attributes for flexibility. Class attributes can be single strings for one class or arrays for multiple classes (e.g., `class:['section', 'main']`). Below is a detailed list of all elements and their attributes., based on Pandoc's Abstract Syntax Tree (AST) as described in the [Pandoc API documentation](https://pandoc.org/using-the-pandoc-api.html). The schema prioritizes HTML elements where possible and uses custom Mark tags for Pandoc-specific features, supporting document transformation and validation. It includes all Pandoc formatting options, such as citations, math, figures, and code blocks, with detailed attribute documentation and examples of all block and inline elements.

## Mark Schema

Below is the complete Mark schema sample, illustrating all Pandoc AST block and inline elements, including code, citations, math, and figures.

```mark
// Mark Schema for Pandoc AST with HTML elements, illustrating all block and inline elements
<doc version:'1.0'
  // Metadata section for document properties
  <meta
    // Field for key-value metadata
    <field name:title, type:string
      <inlines
        <str "Comprehensive Pandoc Document">
      >
    >
    <field name:author, type:string
      <inlines
        <str "John Doe">
      >
    >
    <field name:date, type:string
      <inlines
        <str "2025-07-25">
      >
    >
    // References for citations
    <field name:references, type:list
      <inlines
        <reference id:smith2020, type:book
          <str "Smith, J. (2020). Understanding Pandoc. Academic Press.">
        >
        <reference id:jones2021, type:article
          <str "Jones, A. (2021). Document Transformation. Journal of Docs, 12(3), 45-60.">
        >
      >
    >
  >
  // Document body with block-level elements
  <body
    // Header (level 1 to 6)
    <h1 id:intro, class:['section', 'main'], level:1, data-custom:value "Introduction">
    <h2 id:subintro, class:subsection, level:2 "Subsection">
    // Paragraph (Para) with various inline elements
    <p id:p1, class:text
      "This paragraph includes " <em "emphasized"> ", " <strong "strong"> ", " <s "strikethrough"> ", "
      <sup "superscript"> ", " <sub "subscript"> ", and " <span style:"font-variant: small-caps;" "small caps"> " text. "
      "It also has a " <q type:double "double-quoted"> " and " <q type:single "single-quoted"> " phrase, "
      "an inline " <code language:python "print(\"Hello\")"> ", and a citation "
      <cite
        <citations
          <citation id:smith2020, prefix:"see ", suffix:", p. 15", mode:NormalCitation, note-num:1, hash:0>
          <citation id:jones2021, prefix:"", suffix:"", mode:AuthorInText, note-num:2, hash:1>
        >
      >
      ". A " <a href:"http://example.com", title:Example "link"> " and " <img src:"inline.jpg", alt:"Inline image", width:50> " are included, "
      "with a " <br> " line break and " <note id:note2 <p "Inline footnote">> "."
    >
    // Plain (similar to Para, without paragraph styling)
    <p id:p2, class:plain "Plain text with " <em "minimal"> " formatting.">
    // LineBlock for poetry or addresses
    <line-block id:poem1
      <line <str "Roses are red"> <em " with emphasis">>
      <line <str "Violets are blue">>
    >
    // CodeBlock
    <pre id:code1, class:sourceCode
      <code language:python, data-executable:true
        "def greet(name):\n    return f\"Hello, {name}!\"\nprint(greet(\"World\"))"
      >
    >
    // RawBlock
    <raw format:html, id:raw1 "<div class=\"custom\">Raw HTML content</div>">
    // BlockQuote
    <blockquote id:quote1, class:quote
      <p "Quoted text with " <strong "emphasis"> ".">
    >
    // OrderedList with different attributes
    <ol id:list1, start:1, type:1, class:numbered, delim:period, style:decimal
      <li <p "Item 1">>
      <li <p "Item 2 with " <strong "strong"> " text">>
    >
    <ol id:list2, start:2, type:A, class:lettered, delim:paren, style:upper-alpha
      <li <p "Item A">>
      <li <p "Item B">>
    >
    // BulletList
    <ul id:list3, class:bulleted
      <li <p "Bullet item A">>
      <li <p "Bullet item B with " <code "code">>>
    >
    // DefinitionList
    <dl id:deflist1
      <dt <str "Term 1">>
      <dd <p "Definition with " <a href:"http://example.com" "link"> ".">>
      <dt <str "Term 2">>
      <dd <p "Another definition.">>
    >
    // HorizontalRule
    <hr id:hr1, class:separator>
    // Table with alignment and width
    <table id:table1, class:data
      <caption <str "Sample Table">>
      <colgroup
        <col align:left, width:"50%">
        <col align:center, width:"50%">
      >
      <thead
        <tr
          <th align:left <p "Header 1">>
          <th align:center <p "Header 2">>
        >
      >
      <tbody
        <tr
          <td align:left <p "Cell 1 with " <em "emphasis">>>
          <td align:center <p "Cell 2">>
        >
      >
    >
    // Div with nested content
    <div id:section1, class:['section', 'container'], data-role:container
      <p "Content in a div with " <sub "subscript"> " and a " <raw format:latex "\\textbf{bold}"> " LaTeX element.">
    >
    // Figure with image and caption
    <figure id:fig1, class:image
      <img src:"image.jpg", alt:"Sample image", title:Image, width:300, height:200>
      <figcaption <str "Figure 1: Sample image with "> <em "caption">>
    >
    // Footnote
    <note id:note1
      <p "Footnote content with " <strong "strong"> " text.">
    >
    // Math (display and inline)
    <math type:display, id:math1
      "\\[ E = mc^2 \\]"
    >
    <p "Inline math: " <math type:inline, id:math2 "\\( x^2 + y^2 = z^2 \\)"> ".">
  >
>
```

## Element and Attribute Documentation

The schema uses HTML elements where possible (e.g., `<p>`, `<h1>`) and custom element for Pandoc-specific features (e.g., `<cite>`, `<math>`). Attributes reflect Pandoc’s AST `Attr` type (identifier, classes, key-value pairs) and HTML attributes. Below is a detailed list of all elements and their attributes.

### Root Element
- **`<doc>`**
  - **Attributes**:
    - `version`: String, schema version (e.g., "1.0").
  - **Purpose**: Root element containing metadata and body.
  - **Content**: `<meta>` and `<body>`.

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
  - **Content**: `<inlines>` or `<blocks>` (typically `<inlines>`).
- **`<reference>`** (within `<field name="references">`)
  - **Attributes**:
    - `id`: String, required, unique citation identifier (e.g., "smith2020").
    - `type`: String, optional, citation type (e.g., "book", "article").
  - **Purpose**: Stores citation details for `<cite>`.
  - **Content**: Inline elements (e.g., `<str>`).

### Block-Level Elements
- **`<p>`** (Para, Plain)
  - **Attributes**:
    - `id`: String, optional, unique identifier.
    - `class`: Array of strings, optional, CSS classes (e.g., `['text', 'content']`).
    - `data-*`: Custom key-value pairs, optional, for metadata.
  - **Purpose**: Paragraphs or plain text blocks.
  - **Content**: Inline elements.
- **`<line-block>`**
  - **Attributes**: `id`, `class` (array format for multiple classes), `data-*` (same as `<p>`).
  - **Purpose**: Groups lines for poetry or addresses.
  - **Content**: One or more `<line>` elements.
- **`<line>`**
  - **Attributes**: None.
  - **Purpose**: A single line within `<line-block>`.
  - **Content**: Inline elements.
- **`<pre>`**
  - **Attributes**: `id`, `class` (array format for multiple classes), `data-*` (same as `<p>`).
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
The Mark schema is designed to represent document structures from Markdown, wiki, and HTML inputs, leveraging Pandoc’s AST for semantic richness and HTML elements for web compatibility. It supports document transformation (e.g., Markdown to HTML) and validation, ensuring structural integrity across formats.

### Design Principles
- **HTML Priority**: Uses HTML elements (e.g., `<p>`, `<h1>`, `<a>`) for familiar structures, enhancing compatibility with web-based workflows.
- **Pandoc AST Fallback**: Custom Mark tags (e.g., `<cite>`, `<math>`) are used for Pandoc-specific features without direct HTML equivalents.
- **Comprehensive Coverage**: Includes all Pandoc formatting options (citations, math, figures, code blocks) with detailed attributes, illustrating every block and inline element.
- **Attribute Structure**: Reflects Pandoc’s `Attr` type (identifier, classes, key-value pairs) and HTML attributes for flexibility.

### Citation System
- **Structure**: Citations use `<cite <citations <citation>...> >`, with each `<citation>` referencing a `<reference>` in `<meta <field name:references>` via `id`.
- **Attributes**:
  - `id`: Links to a reference.
  - `prefix`, `suffix`: Contextual text (e.g., "see [Smith, 2020, p. 15]").
  - `mode`: Citation style ("NormalCitation", "AuthorInText", "SuppressAuthor").
  - `note-num`: Footnote number for note-based citations.
  - `hash`: Unique identifier for citation instances.
- **Example**: The schema includes citations for "smith2020" (NormalCitation) and "jones2021" (AuthorInText), demonstrating varied usage.

### Usage
- **Transformation**: Facilitates conversion between formats (e.g., Markdown to LaTeX) by preserving semantic structure.
- **Validation**: Ensures documents conform to Pandoc’s AST with valid element nesting and attributes.
- **Compatibility**: Supports Markdown (e.g., `#`, `*`, `[@ref]`), wiki (headings, links), and HTML structures.
- **Element Completeness**: The schema illustrates all Pandoc AST elements, including inline (`Str`, `Emph`, `Strong`, etc.) and block elements (`Para`, `CodeBlock`, etc.), with code examples.

### References
- [Pandoc API Documentation](https://pandoc.org/using-the-pandoc-api.html)
- [Pandoc Types Documentation](https://hackage.haskell.org/package/pandoc-types/docs/Text-Pandoc-Definition.html)
- [Mark Notation](https://github.com/henry-luo/mark) - The unified notation for both object and markup data used in this schema

This Markdown document can be downloaded and rendered using tools like Pandoc to generate HTML, PDF, or other formats.