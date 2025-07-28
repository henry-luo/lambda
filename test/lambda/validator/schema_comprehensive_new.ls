// Comprehensive schema with all features
type Document = {
    meta: Metadata?,
    content: [BlockElement+]
}

type Metadata = {
    title: string,
    author: string | [string*],
    tags: [string*]?
}

type BlockElement = Header | Paragraph | List | CodeBlock

type Header = {
    level: int,
    text: string,
    id: string?
}

type Paragraph = {
    text: string,
    formatting: [InlineElement*]?
}

type List = {
    type: "ordered" | "unordered",
    items: [ListItem+]
}

type ListItem = {
    text: string,
    nested: List?
}

type CodeBlock = {
    language: string?,
    code: string
}

type InlineElement = {
    type: "bold" | "italic" | "link",
    text: string,
    href: string?
}
