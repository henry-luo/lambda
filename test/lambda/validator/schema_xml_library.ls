// XML Schema (XSD) converted to Lambda schema
// Based on library.xsd - comprehensive library management schema

// Library root element - defined first to be recognized as root
type Document = <library
    established: string,              // established attribute
    xmlns: string,                   // namespace attribute
    name: string,                     // text content from name child element
    address: string,                  // text content from address child element
    BookType*                         // zero or more book child elements
>

// Book element with attributes and text content from child elements
type BookType = <book
    category: string,                 // category attribute
    inStock: string?,                 // optional inStock attribute
    title: string,                    // text from title child element
    author: string,                   // simplified author as string
    isbn: string,                     // text from isbn child element
    publishedYear: string,            // text from publishedYear child element
    price: string,                    // text from price child element
    description: string?              // optional text from description child element
>
