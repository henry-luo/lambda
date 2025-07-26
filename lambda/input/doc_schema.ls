// Mark Doc Schema Types in Lambda Script
// Based on the unified Mark schema for representing document structures
// from Markdown, wiki, HTML, and other formats via Pandoc's AST

// Core attribute types used throughout the schema
type Identifier = string?
type CssClass = string | [string*]
type DataAttrs = {string: any}

// Base attributes used by most elements
type BaseAttrs = {
    id: Identifier,
    class: CssClass,
    'data-*': DataAttrs
}

// Mark data type mappings for metadata
type MetaString = string
type MetaInlines = string  // text with inline markup
type MetaBlocks = element  // block content
type MetaList = [any*]
type MetaMap = {string: any}
type MetaBool = bool
type MetaValue = any

// Author information structure
type Author = {
    name: MetaInlines,
    orcid: string?,
    email: string?,
    affiliation: MetaInlines?,
    roles: [string*]?,
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
    author: [string*]?,
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
type Meta = {
    // Core document metadata
    title: MetaInlines?,
    subtitle: MetaInlines?,
    author: [Author*]?,
    
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
    keywords: [string*]?,
    subject: [string*]?,
    tags: [string*]?,
    abstract: MetaBlocks?,
    
    // Rights and legal
    license: string?,
    copyright: MetaInlines?,
    doi: string?,
    source: string?,
    
    // Bibliography and citations
    bibliography: [string*]?,
    csl: string?,
    'link-citations': bool?,
    references: [Reference*]?,
    
    // Document structure
    toc: bool?,
    'toc-depth': string?,
    'document-class': string?,
    'class-option': [string*]?,
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

// Inline element types
type InlineContent = string | element

// Block element base type
type BlockContent = element

// Text alignment
type Alignment = left | center | right | justify

// Citation modes
type CitationMode = NormalCitation | AuthorInText | SuppressAuthor

// List numbering styles
type ListStyle = decimal | 'lower-alpha' | 'upper-alpha' | 'lower-roman' | 'upper-roman'
type ListDelimiter = period | paren | 'two-paren'

// Math display types
type MathType = inline | display

// Quote types
type QuoteType = single | double

// Raw content formats
type RawFormat = html | latex | tex | markdown | rst | docx

// Occurrence modifiers for types
type Occurrence = '?' | '+' | '*'

// Element-specific attribute types
type HeaderAttrs = BaseAttrs & {
    level: int  // 1-6
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
    style: {string: string}?
}

type ListAttrs = BaseAttrs & {
    start: int?,
    type: string?,
    delim: ListDelimiter?,
    style: ListStyle?
}

type TableCellAttrs = {
    align: Alignment?,
    rowspan: int?,
    colspan: int?
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
    as: any
}

type AttrType = {
    name: string | symbol | string,
    as: any
}

// Core document structure types
type Document <
    version: string,
    <meta: Meta?>,
    <body>
>

type Meta <
    // All metadata fields as defined above
>

type Body < BlockContent* >

// Block-level elements
type Para < id: string?, class: CssClass?, 'data-*': DataAttrs?, InlineContent* >
type LineBlock < BaseAttrs, <line: InlineContent*>* >
type CodeBlock < CodeAttrs, string >
type RawBlock < RawAttrs, string >
type BlockQuote < BaseAttrs, BlockContent* >
type Header < HeaderAttrs, InlineContent* >
type HorizontalRule < BaseAttrs >
type Div < BaseAttrs, BlockContent* >
type Figure < BaseAttrs, <img>, <figcaption: InlineContent*>? >

// List elements
type OrderedList < ListAttrs, <li: BlockContent*>* >
type BulletList < BaseAttrs, <li: BlockContent*>* >
type DefinitionList < 
    BaseAttrs,
    (<dt: InlineContent*>, <dd: BlockContent*>)*
>

// Table elements
type Table <
    BaseAttrs,
    <caption: InlineContent*>?,
    <colgroup: <col: ColAttrs>*>?,
    <thead: <tr: (<th: TableCellAttrs, BlockContent*> | <td: TableCellAttrs, BlockContent*>)*>*>?,
    <tbody: <tr: (<th: TableCellAttrs, BlockContent*> | <td: TableCellAttrs, BlockContent*>)*>*>?
>

// Inline elements
type Emphasis < BaseAttrs, InlineContent* >
type Strong < BaseAttrs, InlineContent* >
type Strikeout < BaseAttrs, InlineContent* >
type Superscript < BaseAttrs, InlineContent* >
type Subscript < BaseAttrs, InlineContent* >
type SmallCaps < SpanAttrs, InlineContent* >
type Quoted < QuoteAttrs, InlineContent* >
type Link < LinkAttrs, InlineContent* >
type Image < ImageAttrs >
type Code < CodeAttrs, string >
type RawInline < RawAttrs, string >
type LineBreak < BaseAttrs >
type Space < BaseAttrs >
type SoftBreak < BaseAttrs >

// Citation elements
type Cite < BaseAttrs, <citation: CitationAttrs>* >
type Note < BaseAttrs, BlockContent* >
type Math < MathAttrs, string >

// HTML element aliases for common elements
type p = Para
type h1 = Header
type h2 = Header  
type h3 = Header
type h4 = Header
type h5 = Header
type h6 = Header
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
type raw = RawInline | RawBlock
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

// Complete document type
type doc = Document
