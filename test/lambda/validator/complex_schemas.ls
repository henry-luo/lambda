// Complex schema definitions for advanced testing
// Author: Henry Luo

// Nested map structures
type Address = {
    street: string,
    city: string,
    country: string,
    postal_code: string?
}

type Contact = {
    name: string,
    email: string,
    phone: string?,
    address: Address
}

// Complex array structures
type ContactList = [Contact+]
type NestedArrays = [[string*]*]

// Advanced union types
type ContactMethod = email: string | phone: string | address: Address
type MultiUnion = string | int | bool | Contact

// Complex element structures (document-like)
type DocumentElement = <doc 
    title: string?,
    author: string?
    [ParagraphElement | HeaderElement | ListElement]*
>

type HeaderElement = <h level: int, [string]>
type ParagraphElement = <p [string | EmphasisElement | LinkElement]*>
type EmphasisElement = <em [string]>
type LinkElement = <a href: string, [string]>
type ListElement = <ul [ListItemElement+]>
type ListItemElement = <li [string | ParagraphElement]*>

// Function types
type StringProcessor = (input: string) => string
type ContactValidator = (contact: Contact) => bool
type MapFunction = (input: [string*]) => [int*]

// Recursive structures
type TreeNode = {
    value: string,
    children: [TreeNode*]?
}

type LinkedList = {
    data: string,
    next: LinkedList?
}

// Advanced constraints (conceptual - would need validation rules)
type ConstrainedString = string    // min: 1, max: 100
type PositiveInt = int             // min: 1
type EmailAddress = string         // pattern: email regex
type Range = {
    min: int,                      // constraint: min <= max
    max: int
}
