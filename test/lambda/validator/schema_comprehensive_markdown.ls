// Comprehensive schema covering all Lambda validator features - for Markdown format
// This schema tests all types, occurrences, and complex nested structures with Markdown elements

// Basic element types matching Markdown parser output
// Element syntax: <name attr1: type, attr2: type; content_type>
type HeadingElement = <h1 level: int?; string>
type ParagraphElement = <p id: string?, class: string*; string>
type LinkElement = <a href: string, title: string?, target: string?; string>
type ImageElement = <img src: string, alt: string, width: int?, height: int?, caption: string?; string?>
type CodeElement = <code lang: string?, class: string*; string>
type ListElement = <ul listType: string?, class: string*; string+>

// Union types for flexible content
type ContentElement = HeadingElement | ParagraphElement | LinkElement | ImageElement | CodeElement | ListElement

// Complex nested structures
type Author = {
    name: string,
    email: string?,
    url: string?,
    bio: string?
}

type Category = {
    name: string,
    slug: string,
    description: string?,
    parent: string?
}

type Tag = {
    name: string,
    color: string?
}

// Array and occurrence testing
type Metadata = {
    authors: Author+,
    categories: Category*,
    tags: Tag*,
    keywords: string*,
    custom_fields: {
        priority: int?,
        status: string?,
        flags: string*
    }?
}

// Complex section structure
type Section = {
    id: string,
    level: int,
    title: string,
    content: ContentElement*,
    subsections: Section*,
    metadata: {
        word_count: int?,
        reading_time: float?,
        last_updated: datetime?
    }?
}

// Main document type with all features - now as doc element to match Markdown parser output
type Document = <doc version: string?, title: string?; ContentElement*>
