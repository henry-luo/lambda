// XML Library Schema - Balanced to pass validation and have expected features

// Root library element - defined first to be recognized as root
type Document = <library
    name: string?,                    // optional library name attribute
    established: string?;             // optional establishment year
    LibraryBook*                      // child elements
>

// Additional types to demonstrate schema features
type AuthorInfo = <author
    id: string,                      // required id field
    firstName: string,               // required firstName field
    lastName: string,                // required lastName field  
    birthYear: string?               // optional birthYear field (for ? feature)
>

// Book element type for library
type LibraryBook = <book
    id: string,                      // required book id
    isbn: string?;                   // optional isbn
    title: string,                   // primitive type
    category: string?,               // optional field  
    AuthorInfo*                      // child elements
>
