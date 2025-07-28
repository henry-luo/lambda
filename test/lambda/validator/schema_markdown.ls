// Basic Markdown schema with all required features
type Document = {
    content: [BlockElement+]  // one-or-more occurrences
}

type BlockElement = Header | Paragraph | List  // union types

type Header = {
    level: int,
    text: string,
    id: string?  // optional fields
}

type Paragraph = {
    text: string,
    formatting: [InlineElement*]?  // zero-or-more occurrences, optional fields
}

type List = {
    items: [string*]  // zero-or-more occurrences
}

type InlineElement = {
    type: "bold" | "italic" | "link",
    text: string
}
