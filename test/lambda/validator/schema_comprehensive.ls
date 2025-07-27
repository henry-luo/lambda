// Comprehensive schema covering all Lambda validator features
// This schema tests all types, occurrences, elements, unions, and complex nesting

// Basic type definitions
type DocumentHeader = {
    title: string,
    version: string,
    created: datetime,
    modified: datetime?,
    published: bool
}

// Element types for structured content
type HeadingElement = <h1 level: int, id: string?, class: string*, text: string>
type ParagraphElement = <p id: string?, class: string*, content: string>
type LinkElement = <a href: string, title: string?, target: string?, text: string>
type ImageElement = <img src: string, alt: string, width: int?, height: int?, caption: string?>
type CodeElement = <code lang: string?, class: string*, content: string>
type ListElement = <ul type: string?, class: string*, items: string+>

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
    authors: Author+,              // one or more
    categories: Category*,         // zero or more
    tags: Tag*,                   // zero or more
    keywords: string*,            // zero or more strings
    custom_fields: {              // nested map with optional fields
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
    content: ContentElement*,      // zero or more content elements
    subsections: Section*,         // recursive structure
    metadata: {                    // inline nested structure
        word_count: int?,
        reading_time: float?,
        last_updated: datetime?
    }?
}

// Main document type with all features
type Document = {
    header: DocumentHeader,
    metadata: Metadata,
    body: {
        introduction: string?,
        sections: Section+,         // at least one section
        conclusion: string?
    },
    appendix: {
        references: {
            title: string,
            url: string,
            accessed: datetime?
        }*,
        glossary: {
            term: string,
            definition: string,
            see_also: string*
        }*
    }?,
    footer: {
        copyright: string,
        license: string?,
        contact: string?
    }?
}
