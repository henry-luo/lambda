// Test schema with duplicate type definitions
// This should trigger duplicate detection warnings but not crash

// First definition of Document
type Document = <root
    name: string,
    value: string
>

// Duplicate definition of Document (should be ignored with warning)
type Document = <document
    title: string,
    content: string
>

// Another type to ensure parsing continues
type SimpleType = <simple
    id: string
>

// Another duplicate (should also be ignored)
type SimpleType = <other
    data: string
>
