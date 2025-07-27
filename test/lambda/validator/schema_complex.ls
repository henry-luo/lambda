// Schema for complex nested structures - demonstrates all type features together
type DocumentMeta = {
    title: string,
    version: string,
    authors: string+,
    tags: string*
}

type SectionAttributes = {
    id: string?,
    class: string?
}

type TextFormatting = {
    bold: bool,
    italic: bool,
    font_size: int?
}

type ListItem = {
    text: string,
    priority: int?
}

type Link = {
    url: string,
    text: string
}

type Section = {
    section_type: string,
    level: int?,
    content: string?,
    attributes: SectionAttributes?,
    formatting: TextFormatting?,
    items: ListItem*,
    ordered: bool?
}

type DocumentFooter = {
    copyright: string,
    links: Link*
}

type Document = {
    meta: DocumentMeta,
    sections: Section+,
    footer: DocumentFooter?
}

type ComplexTypes = {
    document: Document
}
