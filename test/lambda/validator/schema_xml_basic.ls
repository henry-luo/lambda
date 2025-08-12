// Basic XML schema for simple XML documents
// Tests fundamental XML structure based on actual parser output

// Processing instruction like <?xml version="1.0"?>
type XmlProcessingInstruction = <?xml
    version: string                   // version attribute
>

// Book element with id and category attributes
type BookElement = <book
    id: string,                       // book id attribute
    category: string,                 // book category attribute
    title: string,                    // title text content
    author: string,                   // author text content  
    price: string                     // price text content
>

// Bookstore root element containing books
type BookstoreElement = <bookstore
    BookElement*               // zero or more book elements
>

// Document structure matching XML parser output
type Document = <document
    xmlDeclaration: XmlProcessingInstruction, // <?xml> declaration
    BookstoreElement       // bookstore root element
>
