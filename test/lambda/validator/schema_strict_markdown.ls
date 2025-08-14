// Ultra-strict markdown schema that only allows plain text and basic formatting
// This schema is designed to fail on any HTML elements or complex structures

type Document = {
    body: [StrictPara+]              // Must have at least one paragraph
}

// Only allow paragraphs with very limited inline content  
type StrictPara = {
    tag: "p",
    content: [PlainContent+]         // Must have content
}

// Extremely restrictive content - only plain strings allowed
type PlainContent = string           // Only plain text, no HTML elements
