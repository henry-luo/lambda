// XML Library Schema - Following working basic pattern
// The schema syntax field:type works for both attributes and child element text content

// Library root element - this is the Document type (MUST BE FIRST)
type Document = <library
    established: string,             // established attribute
    xmlns: string?,                  // optional namespace attribute
    name: string,                    // name child element text content
    address: string,                 // address child element text content
    BookType*                        // zero or more book child elements
>

// Author element with attributes and child elements
type AuthorType = <author
    id: string,                      // id attribute
    firstName: string,               // firstName child element text content
    lastName: string,                // lastName child element text content
    birthYear: string?               // optional birthYear child element text content
>

// Book element with attributes and child elements
type BookType = <book
    category: string,                // category attribute
    inStock: string?,                // optional inStock attribute (can be true/false)
    title: string,                   // title child element text content
    author: AuthorType,              // author child element (complex structure)
    isbn: string,                    // isbn child element text content
    publishedYear: string,           // publishedYear child element text content
    price: string,                   // price child element text content
    description: string?             // optional description child element text content
>
