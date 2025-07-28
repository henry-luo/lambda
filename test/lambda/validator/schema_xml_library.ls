// XML Schema (XSD) converted to Lambda schema
// Based on library.xsd - comprehensive library management schema

// Author complex type with required ID attribute
type AuthorType = <author
    id: string,                       // required ID attribute
    firstName: string,                // required first name
    lastName: string,                 // required last name  
    birthYear: int?                   // optional birth year
>

// Book complex type with category and stock attributes
type BookType = <book
    category: string,                 // required category (fiction, non-fiction, etc.)
    inStock: bool?,                   // optional in-stock flag (default true)
    title: string,                    // book title
    authors: AuthorType+,             // one or more authors (maxOccurs="unbounded")
    isbn: string,                     // ISBN with pattern validation
    publishedYear: int,               // publication year
    price: float,                     // book price (decimal)
    description: string?              // optional description
>

// Library root element with establishment year
type LibraryType = <library
    established: int,                 // required establishment year
    name: string,                     // library name
    address: string,                  // library address
    books: BookType+                  // one or more books (maxOccurs="unbounded")
>

// Document structure matching XML parser output
type Document = <document
    xmlDeclaration: XmlProcessingInstruction?, // optional <?xml> declaration
    library: LibraryType              // library root element
>

// Processing instruction for XML declaration
type XmlProcessingInstruction = <?xml
    version: string,                  // XML version
    encoding: string?                 // optional encoding
>
