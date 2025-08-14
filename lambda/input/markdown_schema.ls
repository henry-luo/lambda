// Markdown Schema Definition in Lambda Script
// Schema for markdown documents parsed into element-based structures
// Designed to match the output of the input-markup.cpp parser

// Root document - doc element to match markdown output
type Document = <doc
    element*     // flexible content - any number of elements
>
