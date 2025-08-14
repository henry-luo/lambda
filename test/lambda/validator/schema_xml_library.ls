// XML Library Schema - Following working basic pattern
// The schema syntax field:type works for both attributes and child element text content

// Library root element - this is the Document type (MUST BE FIRST)
type Document = <library
    established: string,             // established attribute
    xmlns: string?,                  // optional namespace attribute
    name: string,                    // name child element text content
    address: string,                 // address child element text content
    BookType+                        // one or more book child elements
>

// Author element with attributes and child elements
type AuthorType = <author
    id: string,                      // id attribute
    firstName: string,               // firstName child element text content
    lastName: string,                // lastName child element text content
    birthYear: string?               // optional birthYear child element text content
>

// Book element definition with nested AuthorType
type BookType = <book
    title: string,                    // title attribute or text content
    isbn: string?,                   // optional isbn attribute or text content
    AuthorType+                      // one or more authors (nested type)
>
