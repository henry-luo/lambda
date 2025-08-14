// XML Library Schema - Document type first

// Library root element
type Document = <library
    established: string,              // established attribute
    xmlns: string,                   // xmlns attribute  
    name: string,                     // name text content
    address: string,                 // address text content
    BookType*                        // zero or more book elements
>

// Book element with text content fields 
type BookType = <book
    title: string,                    // title text content
    author: string,                   // author text content
    isbn: string?                     // optional isbn text content
>
