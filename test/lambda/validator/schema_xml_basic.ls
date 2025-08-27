// Basic XML schema for simple XML documents
// Tests fundamental XML structure based on actual parser output

// Processing instruction like <?xml version="1.0"?>
type XmlProcessingInstruction = <?xml
    version: string                   // version attribute
>

// Book element with id and category attributes and child elements
type BookElement = <book
    id: string,                       // book id attribute
    category: string;                 // book category attribute
    BookTitle,                        // title child element
    BookAuthor,                       // author child element  
    BookPrice                         // price child element
>

// Child element types
type BookTitle = <title string>
type BookAuthor = <author string>
type BookPrice = <price string>

// Root element containing simple elements  
type Document = <root;
    SimpleElement*             // zero or more simple elements
>

// Simple element with name and text attributes
type SimpleElement = <element
    name: string,              // element name attribute
    text: string               // element text attribute
>
