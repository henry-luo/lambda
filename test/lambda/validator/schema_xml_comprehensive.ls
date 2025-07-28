// Comprehensive XML schema covering common XML features
// Tests complex XML with nested structures, attributes, and union types

type XmlAttribute = {
    name: string,                     // attribute name
    value: string                     // attribute value
}

// Nested structure with braces
type NestedMetadata = {
    creator: string,                  // content creator
    timestamp: string,                // creation timestamp
    tags: {                           // nested structure for tags
        category: string,
        priority: int
    }
}

type XmlElement = <element
    name: string,                     // element name
    text: string?,                    // optional text content
    attributes: XmlAttribute*,        // element attributes
    metadata: NestedMetadata?,        // nested structures
    children: XmlElement*             // nested child elements
>

type NestedElement = <nested
    level: int,                       // nesting level
    data: string?,                    // optional data attribute
    text: string?,                    // optional text content
    children: NestedElement*          // nested children (nested structures)
>

// Union type for flexible content
type ContentElement = XmlElement | NestedElement

type RootElement = <root
    elements: ContentElement*         // mixed content elements with nested structures
>

type Document = <document
    version: string,                  // document version
    standalone: bool?,                // optional standalone declaration
    root: RootElement                 // root element
>
