// Schema for Markdown document validation
type MarkdownBlock = <block 
    type: string,
    level: int?,
    content: string?,
    language: string?,
    attributes: {
        id: string?,
        class: string*
    }?
>

type Document = {
    frontmatter: {
        title: string?,
        author: string?,
        date: datetime?,
        tags: string*
    }?,
    blocks: MarkdownBlock+
}
