// Schema for Markdown document validation  
// The Document type should match what the Markdown parser produces: a doc element
type Document = <doc
    version: string?,
    children: Document*
>
