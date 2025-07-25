# Pandoc Mark Schema Documentation

This document provides a comprehensive Mark schema for representing document structures from Markdown, wiki, and HTML inputs, based on Pandoc's Abstract Syntax Tree (AST) as described in the [Pandoc API documentation](https://pandoc.org/using-the-pandoc-api.html). The schema uses Mark notation, a unified format for both object and markup data that combines the best of JSON, HTML, and XML with clean syntax. This version demonstrates extensive use of Mark elements for document transformation and validation, including all Pandoc formatting options such as citations, math, figures, and code blocks.

## Mark Schema

Below is the complete Mark schema sample, including citations and a variety of block and inline elements using Mark notation syntax.

```mark
<doc version:1.0
  // Metadata section for document properties
  <meta
    // Field for key-value metadata
    <field name:title, type:string
      <inlines
        <mark id:'title-mark', class:'highlight important', data-type:title
          "Sample Document"
        >
      >
    >
    <field name:author, type:string
      <inlines
        <mark id:'author-mark', class:'highlight author', data-role:creator
          "John Doe"
        >
      >
    >
    <field name:date, type:string
      <inlines
        <mark id:'date-mark', class:'highlight date', data-format:iso
          "2025-07-25"
        >
      >
    >
    // References for citations
    <field name:references, type:list
      <inlines
        <reference id:smith2020, type:book
          <mark class:'citation-text', data-type:book
            "Smith, J. (2020). Understanding Pandoc. Academic Press."
          >
        >
        <reference id:jones2021, type:article
          <mark class:'citation-text', data-type:article
            "Jones, A. (2021). Document Transformation. Journal of Docs, 12(3), 45-60."
          >
        >
      >
    >
  >
  // Document body with block-level elements
  <body
    // Header with identifier, classes, and key-value attributes
    <h1 id:intro, class:'section main', level:1, data-custom:value
      <mark id:'header-mark', class:'highlight header', data-level:1
        "Introduction"
      >
    >
    // Paragraph with inline elements and citation
    <p id:p1, class:text
      "This document demonstrates "
      <mark id:'feature-mark', class:'highlight feature', data-importance:high
        "Pandoc's features"
      >
      ", including citations "
      <cite
        <citations
          <citation id:smith2020, prefix:"see ", suffix:", p. 15", mode:NormalCitation, note-num:1, hash:0>
          <citation id:jones2021, prefix:"", suffix:"", mode:AuthorInText, note-num:2, hash:1>
        >
      >
      "."
    >
    // LineBlock for poetry or addresses
    <line-block id:poem1
      <line
        <mark id:'poem-line1', class:'highlight poetry', data-stanza:1
          "First line with "
          <em "emphasis">
        >
      >
      <line
        <mark id:'poem-line2', class:'highlight poetry', data-stanza:1
          "Second line"
        >
      >
    >
    // CodeBlock with language and attributes
    <pre id:code1, class:sourceCode
      <code language:python, data-executable:true
        <mark id:'code-mark', class:'highlight code', data-language:python
          "print(\"Hello, world!\")"
        >
      >
    >
    // RawBlock for format-specific content
    <raw format:html, id:raw1
      <mark id:'raw-mark', class:'highlight raw', data-format:html
        "<div>Raw HTML content</div>"
      >
    >
    // BlockQuote
    <blockquote id:quote1, class:quote
      <p
        <mark id:'quote-mark', class:'highlight quote', data-type:blockquote
          "Quoted text here."
        >
      >
    >
    // OrderedList with list attributes
    <ol id:list1, start:1, type:1, class:numbered, delim:period, style:decimal
      <li
        <p
          <mark id:'list-item1', class:'highlight list-item', data-number:1
            "Item 1"
          >
        >
      >
      <li
        <p
          <mark id:'list-item2', class:'highlight list-item', data-number:2
            "Item 2 with "
            <strong "strong">
            " text"
          >
        >
      >
    >
    // BulletList
    <ul id:list2, class:bulleted
      <li
        <p
          <mark id:'bullet-item1', class:'highlight bullet-item', data-type:bullet
            "Item A"
          >
        >
      >
      <li
        <p
          <mark id:'bullet-item2', class:'highlight bullet-item', data-type:bullet
            "Item B"
          >
        >
      >
    >
    // DefinitionList
    <dl id:deflist1
      <dt
        <mark id:'term-mark', class:'highlight term', data-type:'definition-term'
          "Term"
        >
      >
      <dd
        <p
          <mark id:'def-mark', class:'highlight definition', data-type:'definition-desc'
            "Definition with "
            <a href:"http://example.com" "link">
            "."
          >
        >
      >
    >
    // HorizontalRule
    <hr id:hr1>
    // Table with alignment and width attributes
    <table id:table1, class:data
      <caption
        <mark id:'table-caption', class:'highlight caption', data-type:table
          "Sample Table"
        >
      >
      <colgroup
        <col align:left, width:"50%">
        <col align:center, width:"50%">
      >
      <thead
        <tr
          <th align:left
            <p
              <mark id:'header1-mark', class:'highlight table-header', data-col:1
                "Header 1"
              >
            >
          >
          <th align:center
            <p
              <mark id:'header2-mark', class:'highlight table-header', data-col:2
                "Header 2"
              >
            >
          >
        >
      >
      <tbody
        <tr
          <td align:left
            <p
              <mark id:'cell1-mark', class:'highlight table-cell', data-row:1, data-col:1
                "Cell 1"
              >
            >
          >
          <td align:center
            <p
              <mark id:'cell2-mark', class:'highlight table-cell', data-row:1, data-col:2
                "Cell 2"
              >
            >
          >
        >
      >
    >
    // Div with attributes
    <div id:section1, class:section, data-role:container
      <p
        <mark id:'div-content', class:'highlight content', data-container:div
          "Content in a div with "
          <sub "subscript">
          "."
        >
      >
    >
    // Figure with image and caption
    <figure id:fig1, class:image
      <img src:"image.jpg", alt:"Sample image", title:"Image", width:"300", height:"200">
      <figcaption
        <mark id:'caption-mark', class:'highlight caption', data-type:figure
          "Figure 1: Sample image description"
        >
      >
    >
    // Footnote
    <note id:note1
      <p
        <mark id:'footnote-mark', class:'highlight footnote', data-type:note
          "Footnote content with "
          <em "emphasis">
          "."
        >
      >
    >
    // Math (display)
    <math type:display, id:math1
      <mark id:'math-mark', class:'highlight math', data-type:display
        "\\[ E = mc^2 \\]"
      >
    >
  >
>
```

## Element and Attribute Documentation

The schema uses Mark notation, which combines the best features of JSON, HTML, and XML. Mark elements use clean angle bracket syntax with attributes as key-value pairs and support mixed content. This version extensively uses Mark elements for text highlighting and semantic annotation. Below is a detailed list of elements and their attributes.

### Root Element
- **`<pandoc>`**
  - **Attributes**:
    - `version`: Number, schema version (e.g., 1.0).
  - **Purpose**: Root element containing metadata and body.

### Metadata Elements
- **`<meta>`**
  - **Attributes**: None.
  - **Purpose**: Contains `<field>` elements for document metadata.
  - **Content**: Zero or more `<field>` elements.
- **`<field>`**
  - **Attributes**:
    - `name`: Symbol, required, metadata key (e.g., title, author, references).
    - `type`: Symbol, optional, data type (e.g., string, list, map).
  - **Purpose**: Represents a metadata key-value pair.
  - **Content**: `<inlines>` or `<blocks>` (typically `<inlines>` for simple metadata).
- **`<reference>`** (within `<field name:references>`)
  - **Attributes**:
    - `id`: Symbol, required, unique citation identifier (e.g., smith2020).
    - `type`: Symbol, optional, citation type (e.g., book, article).
  - **Purpose**: Stores citation details for use in `<cite>`.
  - **Content**: Text or Mark elements.

### Block-Level Elements
- **`<p>`** (Para, Plain)
  - **Attributes**:
    - `id`: Symbol, optional, unique identifier.
    - `class`: Symbol or string, optional, CSS classes.
    - `data-*`: Custom key-value pairs, optional, for metadata.
  - **Purpose**: Paragraphs or plain text blocks.
  - **Content**: Mixed content (text and inline elements).
- **`<line-block>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Groups lines for poetry or addresses.
  - **Content**: One or more `<line>` elements.
- **`<line>`**
  - **Attributes**: None.
  - **Purpose**: A single line within `<line-block>`.
  - **Content**: Mixed content (text and inline elements).
- **`<pre>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Wraps `<code>` for code blocks.
  - **Content**: `<code>` element.
- **`<code>`** (within `<pre>`)
  - **Attributes**:
    - `language`: Symbol, optional, programming language (e.g., python).
    - `data-executable`: Boolean, optional, indicates executable code.
  - **Purpose**: Contains code with syntax highlighting.
  - **Content**: Text or Mark elements for highlighted code.
- **`<raw>`**
  - **Attributes**:
    - `format`: Symbol, required, content format (e.g., html, latex).
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
    - `start`: Number, optional, starting number (default: 1).
    - `type`: Number, optional, numbering style.
    - `delim`: Symbol, optional, delimiter (e.g., period, paren).
    - `style`: Symbol, optional, list style (e.g., decimal).
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
  - **Content**: Mixed content (text and inline elements).
- **`<dd>`**
  - **Attributes**: None.
  - **Purpose**: Definition description.
  - **Content**: Block elements.
- **`<h1>` to `<h6>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `level`: Number, required, header level (1 to 6).
  - **Purpose**: Headers with level-specific tags.
  - **Content**: Mixed content (text and inline elements).
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
  - **Content**: Mixed content (text and inline elements).
- **`<colgroup>`**
  - **Attributes**: None.
  - **Purpose**: Groups column specifications.
  - **Content**: `<col>` elements.
- **`<col>`**
  - **Attributes**:
    - `align`: Symbol, optional, column alignment (e.g., left, center, right).
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
    - `align`: Symbol, optional, cell alignment (e.g., left, center, right).
    - `rowspan`, `colspan`: Number, optional, cell spanning.
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
    - `width`, `height`: String, optional, dimensions.
  - **Purpose**: Embeds images.
  - **Content**: None (empty element).
- **`<figcaption>`**
  - **Attributes**: None.
  - **Purpose**: Figure caption.
  - **Content**: Mixed content (text and inline elements).
- **`<note>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Footnote content.
  - **Content**: Block elements.
- **`<math>`**
  - **Attributes**:
    - `type`: Symbol, required, math type (inline or display).
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Contains LaTeX math expressions.
  - **Content**: Text (LaTeX code) or Mark elements.

### Inline-Level Elements
- **`<mark>`**
  - **Attributes**:
    - `id`: Symbol, optional, unique identifier.
    - `class`: Symbol or string, optional, CSS classes for categorization.
    - `data-*`: Custom key-value pairs, optional, for metadata.
  - **Purpose**: Marked or highlighted text. Used extensively throughout this schema to demonstrate text highlighting, annotation, and semantic markup capabilities. Can wrap any content to indicate importance, categorization, or special meaning.
  - **Content**: Mixed content (text, other inline elements, or nested marks).
- **`<em>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Emphasized text.
  - **Content**: Mixed content (text and inline elements).
- **`<strong>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Strong text.
  - **Content**: Mixed content (text and inline elements).
- **`<s>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Strikethrough text.
  - **Content**: Mixed content (text and inline elements).
- **`<sup>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Superscript text.
  - **Content**: Mixed content (text and inline elements).
- **`<sub>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Subscript text.
  - **Content**: Mixed content (text and inline elements).
- **`<span>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `style`: String, optional, inline CSS.
  - **Purpose**: Generic inline container.
  - **Content**: Mixed content (text and inline elements).
- **`<q>`**
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `type`: Symbol, required, quote type (single or double).
  - **Purpose**: Quoted text.
  - **Content**: Mixed content (text and inline elements).
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
    - `id`: Symbol, required, citation identifier.
    - `prefix`: String, optional, text before citation.
    - `suffix`: String, optional, text after citation.
    - `mode`: Symbol, required, citation mode (NormalCitation, AuthorInText, SuppressAuthor).
    - `note-num`: Number, optional, footnote number for citations in notes.
    - `hash`: Number, optional, unique hash for citation instance.
  - **Purpose**: Represents a single citation with Pandoc's attributes.
  - **Content**: None (empty element).
- **`<code>`** (inline)
  - **Attributes**:
    - `id`, `class`, `data-*` (same as `<p>`).
    - `language`: Symbol, optional, programming language.
  - **Purpose**: Inline code.
  - **Content**: Text or Mark elements.
- **`<raw>`** (inline)
  - **Attributes**:
    - `format`: Symbol, required, content format.
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Raw inline content.
  - **Content**: Raw text.
- **`<a>`**
  - **Attributes**:
    - `href`: String, required, link URL.
    - `title`: String, optional, link title.
    - `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Hyperlink.
  - **Content**: Mixed content (text and inline elements).
- **`<br>`**
  - **Attributes**: `id`, `class`, `data-*` (same as `<p>`).
  - **Purpose**: Line break.
  - **Content**: None (empty element).

## Overall Notes

### Purpose
The Mark schema is designed to represent document structures from Markdown, wiki, and HTML inputs, leveraging Pandoc's AST for semantic richness and Mark notation for clean, unified syntax. It supports document transformation and validation, ensuring structural integrity across formats.

### Design Principles
- **Mark Notation**: Uses Mark's unified notation that combines the best of JSON, HTML, and XML with clean syntax and fully-typed data model.
- **Mixed Content Support**: Mark's built-in mixed content support makes it ideal for document markup, similar to HTML but with cleaner syntax.
- **Semantic Annotation**: Extensive use of Mark elements for text highlighting, semantic annotation, and content categorization throughout the document structure.
- **Comprehensive Coverage**: Supports all Pandoc formatting options, including citations, math, figures, and code blocks, with detailed attributes.
- **Attribute Structure**: Reflects Pandoc's `Attr` type using Mark's flexible attribute system with symbols, strings, and complex values.

### Citation System
- **Structure**: Citations are represented with `<cite><citations><citation>...</citation></citations></cite>`. Each `<citation>` references a `<reference>` in `<meta><field name:references>` via the `id` attribute.
- **Attributes**: The `<citation>` element includes:
  - `id`: Links to a reference using symbol notation.
  - `prefix`, `suffix`: Contextual text using string values.
  - `mode`: Citation style using symbol notation.
  - `note-num`: Footnote number using number type.
  - `hash`: Unique identifier using number type.
- **Example**: The sample includes citations for smith2020 and jones2021, demonstrating different modes and attributes.

### Mark System
- **Structure**: This version uses Mark notation extensively to demonstrate clean syntax for document markup with built-in mixed content support.
- **Attributes**: Mark elements include:
  - `id`: Unique identifier using symbol notation.
  - `class`: CSS classes for styling and categorization using symbols or strings.
  - `data-*`: Custom metadata for semantic meaning using various data types.
- **Usage Examples**:
  - **Metadata highlighting**: Title, author, and date fields wrapped in semantic marks
  - **Content categorization**: Different mark classes for headers, lists, tables, code, etc.
  - **Importance indicators**: Using `data-importance:high` for critical content
  - **Type classification**: Using `data-type` attributes to categorize different content types
- **Benefits**: Enables fine-grained content analysis, styling control, and semantic processing while maintaining clean, readable syntax.

### Mark Notation Advantages
- **Clean Syntax**: No verbose closing tags, unquoted symbols, flexible attribute syntax
- **Fully Typed**: All data types supported (symbols, numbers, strings, objects, arrays)
- **Mixed Content**: Natural support for combining text and markup
- **JavaScript Compatible**: Mark objects are plain JavaScript objects (POJOs)
- **No Whitespace Ambiguity**: Text content is explicitly quoted

### Usage
- **Transformation**: The schema facilitates conversion between formats while preserving semantic structure using Mark's clean notation.
- **Validation**: Ensures documents conform to Pandoc's AST with valid element nesting and attribute values.
- **Compatibility**: Supports Markdown, wiki, and HTML structures with Mark's unified approach.
- **Processing**: Easy to process using Mark.js library or standard JavaScript object manipulation.

### References
- [Pandoc API Documentation](https://pandoc.org/using-the-pandoc-api.html)
- [Pandoc Types Documentation](https://hackage.haskell.org/package/pandoc-types/docs/Text-Pandoc-Definition.html)
- [Mark Notation](https://marknotation.org/)
- [Mark.js Library](https://github.com/henry-luo/mark)

This Markdown document demonstrates how Pandoc's document structures can be represented using Mark notation, providing a clean, unified format for both object and markup data.
