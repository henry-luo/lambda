// XML Library Schema - Following working basic schema pattern

// Book element with text content fields
type BookElement = <book
    title: string,                    // title text content
    author: string,                   // author text content
    isbn: string?                     // optional isbn text content
>

// Library root element
type Document = <library
    established: string,              // established attribute
    xmlns: string,                   // xmlns attribute  
    name: string,                     // name text content
    address: string,                 // address text content
    BookElement*                     // zero or more book elements
>
