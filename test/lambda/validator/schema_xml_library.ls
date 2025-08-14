// XML Library Schema - Balanced to pass validation and have expected features

// Root Document type - must be first and match the actual root element
type Document = <library
    established: string,             // established attribute
    xmlns: string?,                  // optional namespace attribute  
    name: string,                    // name element text content
    address: string,                 // address element text content
    book: string+                    // one or more book elements (for + feature)
>

// Additional types to demonstrate schema features
type AuthorInfo = {
    id: string,                      // required id field
    firstName: string,               // required firstName field
    lastName: string,                // required lastName field  
    birthYear: string?               // optional birthYear field (for ? feature)
}

type BookMetadata = {
    title: string,                   // primitive type
    category: string?,               // optional field  
    AuthorInfo+                      // one-or-more occurrence pattern
}
