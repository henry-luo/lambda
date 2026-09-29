# Mark Doc Schema

This document describes the unified element vocabulary — the **Mark Doc schema** — that Lambda's prose parsers produce for Markdown, reStructuredText, AsciiDoc, MediaWiki/DokuWiki, Org-mode, Textile and man pages, and that `lambda validate` checks those documents against. It is modelled on Pandoc's Abstract Syntax Tree ([Pandoc API documentation](https://pandoc.org/using-the-pandoc-api.html)): HTML element names are used wherever HTML has one (`<p>`, `<h1>`, `<a>`), and custom elements cover what HTML lacks (`<math>`, `<citation>`, `<footnote>`).

> **Where it lives.** The schema is `lambda/input/doc_schema.ls`, shipped with the runtime and used as the default validator schema for the formats above (`lambda validate README.md`). See [Markup_Formats_Support.md](Markup_Formats_Support.md) for what each parser emits and [Lambda_Validator.md](Lambda_Validator.md) for running the validator.
>
> **What the parsers emit today.** The tree is `<doc version: "1.0", <body …>>`. A `<meta>` element is part of the schema but not yet produced — Markdown front matter currently parses as a thematic break and a heading. Inline runs inside `<p>` are wrapped in a `<span>`; headings carry `level` as a string (`level: "1"`); code carries `type: "inline"` or `type: "block"` and math `type: "inline"` or `type: "block"`; strikethrough is `<del>`; footnotes are `<footnote-ref ref: "1">` at the reference and a `<footnote>` block for the text; citations are `<citation key: …>`. The sample and element list below show the schema's target shape, and note these differences where they matter.

## Mark Schema

Below is a complete Mark schema sample, illustrating the Pandoc AST block and inline elements, including code, citations, math and figures.

```mark
// Mark Schema for Pandoc AST with HTML elements, illustrating all block and inline elements
<doc version:'1.0'
  // Metadata section for document properties using unified metadata schema
  <meta
    // Core document metadata
    title:"Comprehensive Pandoc Document",
    subtitle:"A Complete Mark Schema Example",
    author:[
      {
        name:"John Doe",
        orcid:"0000-0002-1825-0097",
        email:"john.doe@example.com",
        affiliation:"University of Examples",
        roles:[author, 'corresponding-author'],
        address:"123 Academic Lane, Research City, RC 12345",
        website:"https://johndoe.example.com",
        identifier:"author-001",
        bio:<p "John Doe is a researcher in document processing and markup languages.">,
        initials:"J.D.",
        contributed:true
      },
      {
        name:"Jane Smith",
        orcid:"0000-0002-1825-0098",
        email:"jane.smith@example.com",
        affiliation:"Institute of Technology",
        roles:[author],
        website:"https://janesmith.example.com",
        identifier:"author-002",
        initials:"J.S.",
        contributed:true
      }
    ]
    
    // Date metadata
    created:t'2025-07-25T10:30:00Z',
    modified:t'2025-07-25T15:45:00Z',
    published:t'2025-07-25',
    version:"1.2.0",
    revision:"rev-003",
    
    // Language and status
    language:en,
    lang:en,
    status:draft,
    
    // Content description
    description:"A comprehensive Mark schema example demonstrating all Pandoc AST elements with unified metadata structure",
    keywords:[mark, schema, pandoc, ast, document, metadata],
    subject:['document-processing', 'markup-languages', 'technical-documentation'],
    tags:[mark, schema, pandoc, ast, document],
    
    // Rights and licensing
    license:"CC-BY-4.0",
    copyright:"This work is licensed under Creative Commons Attribution 4.0 International License",
    doi:"10.1000/xyz123",
    source:"original.md",
    
    // Bibliography and citations
    bibliography:["references.bib", "additional.json"],
    csl:"chicago-author-date.csl",
    'link-citations':true,
    
    // Document structure
    abstract:<div
      <p "This document provides a comprehensive Mark schema for representing document structures from Markdown, wiki, and HTML inputs, based on Pandoc's Abstract Syntax Tree (AST).">
      <p "The schema prioritizes HTML elements where possible and uses custom Mark tags for 'Pandoc-specific' features.">
    >,
    toc:true,
    'toc-depth':3,
    'document-class':article,
    'class-option':["12pt", "a4paper"],
    geometry:"margin=1in",
    
    // Publication metadata
    publisher:"Academic Press International",
    institution:"University of Examples",
    location:"Research City, Country",
    department:"Department of Computer Science",
    funding:"Grant XYZ-2025-001 from Research Foundation",
    sponsor:"National Science Foundation",
    'conflict-of-interest':"The authors declare no conflicts of interest",
    
    // Academic journal metadata
    journal:{
      name:"Journal of Document Processing",
      volume:"15",
      issue:"3",
      pages:"123-145",
      issn:"1234-5678"
    },
    
    // Conference metadata
    conference:{
      name:"International Conference on Document Technologies",
      location:"Tech City, Country",
      date:t'2025-09-15'
    },
    
    // Custom metadata
    custom:{
      "review-round":"second",
      "editor":"Dr. Sarah Wilson",
      "submission-date":t'2025-06-01'
    },
    
    // References for citations
    references:[
      {
        id:smith2020,
        type:book,
        title:"Understanding Pandoc",
        author:["Smith, J."],
        publisher:"Academic Press",
        year:2020,
        isbn:"978-0-123456-78-9"
      },
      {
        id:jones2021,
        type:article,
        title:"Document Transformation",
        author:["Jones, A."],
        journal:"Journal of Docs",
        volume:12,
        issue:3,
        pages:"45-60",
        year:2021,
        doi:"10.1000/abc123"
      }
    ]
  >
  // Document body with 'block-level' elements
  <body
    // Header (level 1 to 6)
    <h1 id:intro, class:['section', 'main'], level:1, 'data-custom':value "Introduction">
    <h2 id:subintro, class:subsection, level:2 "Subsection">
    // Paragraph (Para) with various inline elements
    <p id:p1, class:text
      "This paragraph includes " <em "emphasized"> ", " <strong "strong"> ", " <del "strikethrough"> ", "
      <sup "superscript"> ", " <sub "subscript"> ", and " <span style:{'font-variant': 'small-caps'} "small caps"> " text. "
      "It also has a " <q type:double "double-quoted"> " and " <q type:single "single-quoted"> " phrase, "
      "an inline " <code type:inline, language:python "print(\"Hello\")"> ", and a citation "
      <cite
        <citation id:smith2020, prefix:"see ", suffix:", p. 15", mode:NormalCitation, 'note-num':1, hash:0>
        <citation id:jones2021, prefix:"", suffix:"", mode:AuthorInText, 'note-num':2, hash:1>
      >
      ". A " <a href:"http://example.com", title:Example "link"> " and " <img src:"inline.jpg", alt:"Inline image", width:50> " are included, "
      "with a " <br> " line break and a footnote reference" <footnote-ref ref:"2"> "."
    >
    // Plain (similar to Para, without paragraph styling)
    <p id:p2, class:plain "Plain text with " <em "minimal"> " formatting.">
    // LineBlock for poetry or addresses
    <line_block id:poem1
      <line "Roses are red" <em " with emphasis">>
      <line "Violets are blue">
    >
    // CodeBlock
    <code type:block, language:python, 'data-executable':true
        "def greet(name):\n    return f\"Hello, {name}!\"\nprint(greet(\"World\"))"
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
    <ol id:list2, start:2, type:A, class:lettered, delim:paren, style:'upper-alpha'
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
      <dt "Term 1">
      <dd <p "Definition with " <a href:"http://example.com" "link"> ".">>
      <dt "Term 2">
      <dd <p "Another definition.">>
    >
    // HorizontalRule
    <hr id:hr1, class:separator>
    // Table with alignment and width
    <table id:table1, class:data
      <caption "Sample Table">
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
    <div id:section1, class:['section', 'container'], 'data-role':container
      <p "Content in a div with " <sub "subscript"> " and a " <raw format:latex "\\textbf{bold}"> " LaTeX element.">
    >
    // Figure with image and caption
    <figure id:fig1, class:image
      <img src:"image.jpg", alt:"Sample image", title:Image, width:300, height:200>
      <figcaption "Figure 1: Sample image with " <em "caption">>
    >
    // Footnote text (referenced by <footnote-ref ref:"1">)
    <footnote id:"1"
      <p "Footnote content with " <strong "strong"> " text.">
    >
    // Math (block and inline); the content is the TeX source without delimiters
    <math type:block, id:math1
      "E = mc^2"
    >
    <p "Inline math: " <math type:inline, id:math2 "x^2 + y^2 = z^2"> ".">
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

> **Target design.** The `<meta>` element and the mapping table below describe the intended unified metadata; no parser emits `<meta>` yet.

The Meta schema is designed aiming to unify all the common metadata elements across formats like Markdown (YAML), Docx, JATS, TEI, LaTeX, and Org-mode. It uses Mark/Lambda data types directly without wrapper elements for cleaner and more efficient representation.

- **`<meta>`**
  - **Purpose**: Contains unified metadata using Mark data types directly.
  - **Meta Type Mapping**:
    - `MetaString` → Mark string values: `"text"`
    - `MetaInlines` → Mark inline content: `"text with <em>markup</em>"`
    - `MetaBlocks` → Mark block content: `<div><p>content</p></div>`
    - `MetaList` → Mark arrays: `["item1", "item2"]`
    - `MetaMap` → Mark map: `{key:value, ...}`
    - `MetaBool` → Mark boolean values: `true` or `false`
    - `MetaValue` → Any Mark meta type (used in custom fields)
  
  - **Core Document Fields**:
    - `title`: Mark inlines, document title
    - `subtitle`: Mark inlines, document subtitle
    - `author`: Mark list of objects with detailed author information
      - `name`: Mark inlines, author full name
      - `orcid`: Mark string, ORCID identifier
      - `email`: Mark string, contact email
      - `affiliation`: Mark inlines, institutional affiliation
      - `roles`: Mark list, author roles (e.g., [author, 'corresponding-author'])
      - `address`: Mark inlines, postal address
      - `website`: Mark string, personal/professional website
      - `identifier`: Mark string, unique author identifier
      - `bio`: Mark blocks, author biography
      - `initials`: Mark string, author initials
      - `contributed`: Mark boolean, contribution status
  
  - **Date and Version Fields**:
    - `created`: Mark string, creation date (ISO 8601)
    - `modified`: Mark string, last modification date (ISO 8601)
    - `published`: Mark string, publication date (ISO 8601)
    - `version`: Mark string, document version
    - `revision`: Mark string, revision identifier
  
  - **Language and Status Fields**:
    - `language`: Mark string, ISO 639-1 language code
    - `lang`: Mark string, alias for language compatibility
    - `status`: Mark string, document status (e.g., draft, final, 'peer-reviewed')
  
  - **Content Description Fields**:
    - `description`: Mark inlines, abstract or summary
    - `keywords`: Mark list, searchable keywords
    - `subject`: Mark list, topical categories
    - `tags`: Mark list, synonym/alias for keywords
    - `abstract`: Mark blocks, formal abstract section
  
  - **Rights and Legal Fields**:
    - `license`: Mark string, license identifier (e.g., "CC-BY-4.0")
    - `copyright`: Mark inlines, legal/copyright notice
    - `doi`: Mark string, DOI, ISBN, or URI
    - `source`: Mark string, source file or document
  
  - **Bibliography and Citation Fields**:
    - `bibliography`: Mark list, bibliography file paths
    - `csl`: Mark string, CSL (Citation Style Language) file path
    - `link-citations`: Mark boolean, enable citation linking
    - `references`: Mark list, inline bibliography entries
  
  - **Document Structure Fields**:
    - `toc`: Mark boolean, table of contents generation
    - `toc-depth`: Mark string, table of contents depth
    - `document-class`: Mark string, document class (e.g., article)
    - `class-option`: Mark list, document class options
    - `geometry`: Mark string, page layout configuration
  
  - **Publication Fields**:
    - `publisher`: Mark inlines, publishing organization
    - `institution`: Mark inlines, affiliated institution
    - `location`: Mark inlines, publication location
    - `department`: Mark inlines, departmental affiliation
    - `funding`: Mark inlines, grant or funding information
    - `sponsor`: Mark inlines, sponsoring organization
    - `conflict-of-interest`: Mark inlines, conflict of interest statement
  
  - **Academic Journal Fields** (nested object):
    - `journal`: Mark map containing:
      - `name`: Mark inlines, journal name
      - `volume`: Mark string, volume number
      - `issue`: Mark string, issue number
      - `pages`: Mark string, page range
      - `issn`: Mark string, ISSN identifier
  
  - **Conference Fields** (nested object):
    - `conference`: Mark map containing:
      - `name`: Mark inlines, conference name
      - `location`: Mark inlines, conference location
      - `date`: Mark string, conference date
  
  - **Custom Metadata Fields**:
    - `custom`: Mark map of custom fields with arbitrary 'key-value' pairs

#### Compatibility Mapping Summary

| **Field**        | **Markdown (YAML)** | **Docx**        | **JATS**                         | **TEI**             | **LaTeX**                  | **Org-mode**      |
| ---------------- | ------------------- | --------------- | -------------------------------- | ------------------- | -------------------------- | ----------------- |
| **title**        | `title`             | `dc:title`      | `<article-title>`                | `<title>`           | `\title{}`                 | `#+TITLE:`        |
| **author**       | `author`            | `dc:creator`    | `<contrib>`                      | `<author>`          | `\author{}`                | `#+AUTHOR:`       |
| **created**      | `date`              | `dc:date`       | `<pub-date>`                     | `<date>`            | `\date{}`                  | `#+DATE:`         |
| **keywords**     | `keywords`          | `cp:keywords`   | `<kwd-group>`                    | `<keywords>`        | `\keywords{}`              | `#+KEYWORDS:`     |
| **language**     | `lang`              | `dc:language`   | `@xml:lang`                      | `@xml:lang`         | `\usepackage[lang]{babel}` | `#+LANGUAGE:`     |
| **publisher**    | `publisher`         | `dc:publisher`  | `<publisher-name>`               | `<publisher>`       | `\publisher{}`             | `#+PUBLISHER:`    |
| **subject**      | `tags`              | `dc:subject`    | `<subject>`                      | `<keywords>`        | `\subject{}`               | `#+TAGS:`         |
| **copyright**    | `copyright`         | `dc:rights`     | `<copyright>`                    | `<availability>`    | `\copyright{}`             | `#+COPYRIGHT:`    |
| **version**      | `version`           | `dc:identifier` | `<article-version>`              | `<edition>`         | `\version{}`               | `#+VERSION:`      |
| **doi**          | `doi`               | Custom property | `<article-id 'pub-id-type'="doi">` | `<idno type="DOI">` | `\doi{}`                   | `#+DOI:`          |
| **abstract**     | `abstract`          | Custom property | `<abstract>`                     | `<abstract>`        | `\begin{abstract}`         | `#+ABSTRACT:`     |
| **bibliography** | `bibliography`      | Custom property | `<ref-list>`                     | `<listBibl>`        | `\bibliography{}`          | `#+BIBLIOGRAPHY:` |
| **status**       | `status`            | Custom property | `<article-type>`                 | `<revisionDesc>`    | Custom command             | `#+STATUS:`       |

### Block-Level Elements
- **`<p>`** (Para, Plain)
  - **Attributes**:
    - `id`: String, optional, unique identifier.
    - `class`: Array of strings, optional, CSS classes (e.g., `['text', 'content']`).
    - `'data-*'`: Custom 'key-value' pairs, optional, for metadata.
  - **Purpose**: Paragraphs or plain text blocks.
  - **Content**: Inline elements. The Markdown parser wraps a paragraph's inline run in a single `<span>` child.
- **`<line_block>`**
  - **Attributes**: `id`, `class` (array format for multiple classes), `'data-*'` (same as `<p>`).
  - **Purpose**: Groups lines for poetry or addresses.
  - **Content**: One or more `<line>` elements.
- **`<line>`**
  - **Attributes**: None.
  - **Purpose**: A single line within `<line_block>`.
  - **Content**: Inline elements.
- **`<code>`**
  - **Attributes**:
    - `type`: Symbol, `block` for a code block (`inline` for inline code, below).
    - `language`: String, optional, programming language (e.g., python).
    - `'data-executable'`: Boolean, optional, indicates executable code ("true", "false").
  - **Purpose**: Contains code with syntax highlighting.
  - **Content**: Raw code text.
- **`<raw>`**
  - **Attributes**:
    - `format`: String, required, content format (e.g., "html", "latex").
    - `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Embeds 'format-specific' content.
  - **Content**: Raw text in specified format.
- **`<blockquote>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Quoted content.
  - **Content**: Block elements.
- **`<ol>`**
  - **Attributes**:
    - `id`, `class`, `'data-*'` (same as `<p>`).
    - `start`: Integer, optional, starting number (default: 1).
    - `type`: String, optional, numbering style (e.g., '1', 'A', 'i').
    - `delim`: Symbol, optional, delimiter (e.g., 'period', 'paren').
    - `style`: Symbol, optional, list style (e.g., decimal, 'upper-alpha').
  - **Purpose**: Ordered list with customizable numbering.
  - **Content**: `<li>` elements.
- **`<ul>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Unordered list.
  - **Content**: `<li>` elements.
- **`<li>`**
  - **Attributes**: None.
  - **Purpose**: List item.
  - **Content**: Block elements.
- **`<dl>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
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
    - `id`, `class`, `'data-*'` (same as `<p>`).
    - `level`: header level (1 to 6). Declared as an integer in the schema; the parsers currently emit it as a string (`level: "1"`).
  - **Purpose**: Headers with 'level-specific' tags.
  - **Content**: Inline elements.
- **`<hr>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Horizontal rule.
  - **Content**: None (empty element).
- **`<table>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
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
    - `rowspan`, `colspan`: Integer, optional, cell spanning.
  - **Purpose**: Table header or data cell.
  - **Content**: Block elements.
- **`<div>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Generic block container.
  - **Content**: Block elements.
- **`<figure>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Contains image and caption.
  - **Content**: `<img>`, `<figcaption>`.
- **`<img>`**
  - **Attributes**:
    - `src`: String, required, image URL.
    - `alt`: String, optional, alternative text.
    - `title`: String, optional, image title.
    - `width`, `height`: String, optional, dimensions (e.g., 300, "200px").
  - **Purpose**: Embeds images.
  - **Content**: None (empty element).
- **`<figcaption>`**
  - **Attributes**: None.
  - **Purpose**: Figure caption.
  - **Content**: Inline elements.
- **`<footnote>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Footnote content, referenced from the text by `<footnote-ref ref: id>`. (The schema file still names this element `note`.)
  - **Content**: Block elements.
- **`<math>`**
  - **Attributes**:
    - `type`: Symbol, required, `inline` or `block`.
    - `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Contains a LaTeX math expression, without its `$` delimiters. See [Math_Support.md](Math_Support.md).
  - **Content**: LaTeX code.

### Inline-Level Elements
- **`<em>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Emphasized text.
  - **Content**: Inline elements.
- **`<strong>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Strong text.
  - **Content**: Inline elements.
- **`<del>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Strikethrough text (the schema file still names it `s`).
  - **Content**: Inline elements.
- **`<sup>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Superscript text.
  - **Content**: Inline elements.
- **`<sub>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Subscript text.
  - **Content**: Inline elements.
- **`<span>`**
  - **Attributes**:
    - `id`, `class`, `'data-*'` (same as `<p>`).
    - `style`: Map, optional, inline CSS (e.g., `{'font-variant': 'small-caps'}`).
  - **Purpose**: Generic inline container (e.g., for SmallCaps).
  - **Content**: Inline elements.
- **`<q>`**
  - **Attributes**:
    - `id`, `class`, `'data-*'` (same as `<p>`).
    - `type`: Symbol, required, quote type (single or double).
  - **Purpose**: Quoted text.
  - **Content**: Inline elements.
- **`<cite>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Contains one or more `<citation>` elements for citation content.
  - **Content**: One or more `<citation>` elements.
- **`<citation>`**
  - **Attributes**:
    - `id`: String, required, citation identifier (e.g., "smith2020"). The Markdown parser emits a bare `<citation key: "smith2020">` for `[@smith2020]`.
    - `prefix`: String, optional, text before citation (e.g., "see ").
    - `suffix`: String, optional, text after citation (e.g., ", p. 15").
    - `mode`: Symbol, required, citation mode (NormalCitation, AuthorInText, SuppressAuthor).
    - `'note-num'`: Integer, optional, footnote number for citations in notes.
    - `hash`: Integer, optional, unique hash for citation instance.
  - **Purpose**: Represents a single citation with Pandoc’s attributes.
  - **Content**: None (empty element).
- **`<code>`** (inline)
  - **Attributes**:
    - `type`: Symbol, `inline`.
    - `id`, `class`, `'data-*'` (same as `<p>`).
    - `language`: Symbol, optional, programming language.
  - **Purpose**: Inline code.
  - **Content**: Code text.
- **`<raw>`** (inline)
  - **Attributes**:
    - `format`: Symbol, required, content format (e.g., "html", "latex").
    - `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Raw inline content.
  - **Content**: Raw text.
- **`<a>`**
  - **Attributes**:
    - `href`: String, required, link URL.
    - `title`: String, optional, link title.
    - `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Hyperlink.
  - **Content**: Inline elements.
- **`<footnote-ref>`**
  - **Attributes**: `ref`: String, required, the id of the `<footnote>` holding the text.
  - **Purpose**: The in-text reference to a footnote (`[^1]` in Markdown).
  - **Content**: None (empty element).
- **`<br>`**
  - **Attributes**: `id`, `class`, `'data-*'` (same as `<p>`).
  - **Purpose**: Line break.
  - **Content**: None (empty element).

## Overall Notes

### Purpose
The Mark schema is designed to represent document structures from Markdown, wiki, and HTML inputs, leveraging Pandoc’s AST for semantic richness and HTML elements for web compatibility. It supports document transformation (e.g., Markdown to HTML) and validation, ensuring structural integrity across formats.

### Design Principles
- **HTML Priority**: Uses HTML elements (e.g., `<p>`, `<h1>`, `<a>`) for familiar structures, enhancing compatibility with 'web-based' workflows.
- **Pandoc AST Fallback**: Custom Mark tags (e.g., `<cite>`, `<math>`) are used for Pandoc-specific features without direct HTML equivalents.
- **Comprehensive Coverage**: Includes all Pandoc formatting options (citations, math, figures, code blocks) with detailed attributes, illustrating every block and inline element.
- **Attribute Structure**: Reflects Pandoc’s `Attr` type (identifier, classes, key-value pairs) and HTML attributes for flexibility.

### Citation System
- **Structure**: Citations use `<cite <citation>... <citation>... >`, with each `<citation>` referencing an entry in the `references` metadata array via `id`.
- **Attributes**:
  - `id`: Links to a reference object in the metadata `references` array.
  - `prefix`, `suffix`: Contextual text (e.g., "see [Smith, 2020, p. 15]").
  - `mode`: Citation style (NormalCitation, AuthorInText, SuppressAuthor).
  - `'note-num'`: Footnote number for 'note-based' citations.
  - `hash`: Unique identifier for citation instances.
- **Example**: The schema includes citations for "smith2020" (NormalCitation) and "jones2021" (AuthorInText), demonstrating varied usage with references stored as objects in the metadata.

### Emoji Shortcodes
A GitHub-style shortcode such as `:smile:` is recognized by the Markdown parser and currently emitted as a bare symbol (`'smile'`) in the inline run; it is not converted to the Unicode character and there is no `<emoji>` element in the schema.

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