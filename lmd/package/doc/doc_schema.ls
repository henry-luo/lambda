// Mark Doc Schema Types in Lambda Script
// Based on the unified Mark schema for representing document structures
// from Markdown, wiki, HTML, and other formats via Pandoc's AST

// Core attribute types used throughout the schema
type Identifier = string?
type CssClass = string | string[]
type DataAttrs = {string: string}

// Base attributes used by most elements
type BaseAttrs = {
    id: Identifier,
    class: CssClass?,
    sourcepos: string?,
    'data-*': string?
}

// Mark data type mappings for metadata
type MetaString = string
type MetaInlines = string  // text with inline markup
type MetaBlocks = element  // block content
type MetaList = string[]
type MetaMap = {string: string}
type MetaBool = bool
type MetaValue = string

// Author information structure
type Author = {
    name: MetaInlines,
    orcid: string?,
    email: string?,
    affiliation: MetaInlines?,
    roles: string[]?,
    address: MetaInlines?,
    website: string?,
    identifier: string?,
    bio: MetaBlocks?,
    initials: string?,
    contributed: bool?
}

// Journal metadata
type Journal = {
    name: MetaInlines,
    volume: string?,
    issue: string?,
    pages: string?,
    issn: string?
}

// Conference metadata  
type Conference = {
    name: MetaInlines,
    location: MetaInlines?,
    date: string?
}

// Reference/citation entry
type Reference = {
    id: string,
    type: string?,
    title: string?,
    author: string[]?,
    publisher: string?,
    year: int?,
    isbn: string?,
    journal: string?,
    volume: int?,
    issue: int?,
    pages: string?,
    doi: string?
}

// Complete metadata schema
type MetaAttrs = {
    // Core document metadata
    title: MetaInlines?,
    subtitle: MetaInlines?,
    author: Author[]?,
    
    // Date and version fields
    created: string?,
    modified: string?,
    published: string?,
    version: string?,
    revision: string?,
    
    // Language and status
    language: string?,
    lang: string?,
    status: string?,
    
    // Content description
    description: MetaInlines?,
    keywords: string[]?,
    subject: string[]?,
    tags: string[]?,
    abstract: MetaBlocks?,
    
    // Rights and legal
    license: string?,
    copyright: MetaInlines?,
    doi: string?,
    source: string?,
    
    // Bibliography and citations
    bibliography: string[]?,
    csl: string?,
    'link-citations': bool?,
    references: Reference[]?,
    
    // Document structure
    toc: bool?,
    'toc-depth': string?,
    'document-class': string?,
    'class-option': string[]?,
    geometry: string?,
    
    // Publication metadata
    publisher: MetaInlines?,
    institution: MetaInlines?,
    location: MetaInlines?,
    department: MetaInlines?,
    funding: MetaInlines?,
    sponsor: MetaInlines?,
    'conflict-of-interest': MetaInlines?,
    
    // Academic metadata
    journal: Journal?,
    conference: Conference?,
    
    // Custom metadata
    custom: {string: MetaValue}?
}

// Text alignment
type Alignment = "left" | "center" | "right" | "justify"

// Citation modes
type CitationMode = "NormalCitation" | "AuthorInText" | "SuppressAuthor"

// List numbering styles
type ListStyle = "decimal" | "lower-alpha" | "upper-alpha" | "lower-roman" | "upper-roman"
type ListDelimiter = "period" | "paren" | "two-paren"

// Math display types
type MathType = "inline" | "block" | "display"

// Quote types
type QuoteType = "single" | "double"

// Raw content formats
type RawFormat = "html" | "latex" | "tex" | "markdown" | "rst" | "docx" | "wiki" |
    "mediawiki" | "dokuwiki" | "tikiwiki" | "twiki" | "vimwiki" | "xwiki" | "zimwiki" |
    "creole" | "jira" | "confluence"

// Occurrence modifiers for types
type Occurrence = '?' | '+' | '*'

// Shared task projection and richer workflow state; conditional invariants are
// checked by lambda.doc.wiki.tasks (Input_Wiki section 4.1).
type TaskState = "todo" | "in-progress" | "done" | "waiting" | "blocked" |
    "cancelled" | "rejected" | "migrated" | "custom"
type TaskAttrs = {
    'data-checked': "true" | "false"?,
    'data-task-state': TaskState?,
    'data-task-label': string?,
    'data-task-progress-min': string?,
    'data-task-progress-max': string?,
    'data-task-marker': string?,
    'data-task-flavor': string?
}
// Element-specific attribute types
type HeaderAttrs = BaseAttrs & TaskAttrs & {
    level: string | int?  // parser projection is a string from 1 to 6
}

type LinkAttrs = BaseAttrs & {
    href: string,
    title: string?
}

type ImageAttrs = BaseAttrs & {
    src: string,
    alt: string?,
    title: string?,
    width: string | int?,
    height: string | int?
}

type CodeAttrs = BaseAttrs & {
    type: "inline" | "block"?,
    language: string?,
    'data-executable': bool?
}

type RawAttrs = BaseAttrs & {
    format: RawFormat
}

type QuoteAttrs = BaseAttrs & {
    type: QuoteType
}

type SpanAttrs = BaseAttrs & {
    style: string | {string: string}?
}

type ListAttrs = BaseAttrs & {
    start: string | int?,
    type: string?,
    delim: ListDelimiter?,
    style: ListStyle?
}

type TableCellAttrs = BaseAttrs & {
    align: Alignment?,
    rowspan: string | int?,
    colspan: string | int?
}

type ColAttrs = {
    align: Alignment?,
    width: string?
}

type CitationAttrs = {
    id: string,
    prefix: string?,
    suffix: string?,
    mode: CitationMode,
    'note-num': int?,
    hash: int?
}

type MathAttrs = BaseAttrs & {
    type: MathType
}

// Map item for attribute definitions
type MapItem = {
    name: string | symbol,
    as: string
}

type AttrType = {
    name: string | symbol | string,
    as: string
}

// Element declarations describe the actual Mark Doc tree, not {tag,content} records.
// Child runs remain extensible for document transformations (S2.6.1v2).
type InlineRun = string | element

type Emphasis = <em BaseAttrs, InlineRun*>
type Strong = <strong BaseAttrs, InlineRun*>
type Strikeout = <del BaseAttrs, InlineRun*>
type Superscript = <sup BaseAttrs, InlineRun*>
type Subscript = <sub BaseAttrs, InlineRun*>
type Underline = <u BaseAttrs, InlineRun*>
type SmallCaps = <span SpanAttrs, InlineRun*>
type Quoted = <q QuoteAttrs, InlineRun*>
type Link = <a LinkAttrs, InlineRun*>
type Image = <img ImageAttrs>
type Code = <code CodeAttrs, string?>
type RawInline = <raw RawAttrs, string?>
type RawBlock = RawInline
type LineBreak = <br BaseAttrs>
type Space = <space BaseAttrs>
type SoftBreak = <softbreak BaseAttrs>
type Cite = <cite BaseAttrs, InlineRun*>
type Citation = <citation BaseAttrs & {key: string?}, InlineRun*>
type Note = <note BaseAttrs, InlineRun*>
type Math = <math MathAttrs, string?>
type TaskCheckbox = <input type: "checkbox", disabled: "disabled", checked: "checked"?>
type WikiVariable = <var BaseAttrs & {source: string?, name: string?, args: string?, default: string?}, InlineRun*>

type InlineContent = string | Emphasis | Strong | Strikeout | Superscript |
    Subscript | Underline | SmallCaps | Quoted | Link | Image | Code | RawInline |
    LineBreak | Space | SoftBreak | Cite | Citation | Note | Math | WikiVariable |
    <b InlineRun*> | <i InlineRun*> | <s InlineRun*> | <ins InlineRun*> | <'footnote-ref' InlineRun*>

type Para = <p BaseAttrs, InlineContent*>
type Header = <h1 HeaderAttrs, InlineContent*> | <h2 HeaderAttrs, InlineContent*> |
    <h3 HeaderAttrs, InlineContent*> | <h4 HeaderAttrs, InlineContent*> |
    <h5 HeaderAttrs, InlineContent*> | <h6 HeaderAttrs, InlineContent*>
type LineBlock = <line_block BaseAttrs, <line InlineContent*>*>
type CodeBlock = Code | <pre CodeAttrs, string?>
type BlockQuote = <blockquote BaseAttrs, InlineRun*>
type HorizontalRule = <hr BaseAttrs>
type Div = <div BaseAttrs, InlineRun*>
type Figure = <figure BaseAttrs, Image, <figcaption InlineContent*>?>

// Lists keep the existing disabled checkbox projection alongside task attributes.
type TaskListItem = <li BaseAttrs & TaskAttrs, (TaskCheckbox | InlineRun)*>
type ListItem = TaskListItem
type OrderedList = <ol ListAttrs, ListItem*>
type BulletList = <ul BaseAttrs, ListItem*>
type List = OrderedList | BulletList
type DefinitionList = <dl BaseAttrs, (<dt InlineContent*> | <dd InlineRun*>)*>

type TableCell = <th TableCellAttrs, InlineRun*> | <td TableCellAttrs, InlineRun*>
type TableRow = <tr BaseAttrs, TableCell*>
type Table = <table BaseAttrs,
    <caption BaseAttrs, InlineContent*>?,
    <colgroup <col ColAttrs>*>?,
    (<thead TableRow*> | <tbody TableRow*> | TableRow)*
>
type Footnotes = <footnotes BaseAttrs, <footnote BaseAttrs, InlineRun*>*>

type BlockContent = Para | Header | LineBlock | CodeBlock | BlockQuote | List |
    DefinitionList | Table | HorizontalRule | Div | Figure | RawBlock | Math |
    Footnotes | SmallCaps | <'html-dom' InlineRun*>
type BlockElement = BlockContent
type InlineElement = InlineContent
type SafeInline = string | Emphasis | Strong | Code | Link
type StrictPara = <p BaseAttrs, SafeInline+>
type StrictBlock = StrictPara | Header | CodeBlock

// Common HTML names remain available as schema aliases.
type p = Para
type h1 = <h1 HeaderAttrs, InlineContent*>
type h2 = <h2 HeaderAttrs, InlineContent*>
type h3 = <h3 HeaderAttrs, InlineContent*>
type h4 = <h4 HeaderAttrs, InlineContent*>
type h5 = <h5 HeaderAttrs, InlineContent*>
type h6 = <h6 HeaderAttrs, InlineContent*>
type em = Emphasis
type strong = Strong
type s = Strikeout
type sup = Superscript
type sub = Subscript
type span = SmallCaps
type q = Quoted
type a = Link
type img = Image
type code = Code
type raw = RawInline
type br = LineBreak
type hr = HorizontalRule
type blockquote = BlockQuote
type ol = OrderedList
type ul = BulletList
type dl = DefinitionList
type table = Table
type div = Div
type figure = Figure
type cite = Cite
type note = Note
type math = Math
type line_block = LineBlock

type Meta = <meta MetaAttrs>
type Body = <body BlockContent*>
// Keep Document last: the CLI chooses the last declaration as the default root.
type Document = <doc version: string?, Meta?, Body>
