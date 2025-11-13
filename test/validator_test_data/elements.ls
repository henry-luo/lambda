// Lambda Validator Test Data: Elements
// Tests element type validation with attributes and content

// ========== Type Definitions ==========

// Simple element
type Paragraph = <p>

// Element with attributes
type Link = <a href: string>

// Element with attributes and content
type Button = <button type: string, disabled: bool>

// Element with optional attributes
type Image = <img src: string, alt: string?>

// Element with nested content types
type Article = <article> {
    title: string,
    content: [Paragraph]+
}

// Complex nested elements
type Page = <html> {
    head: <head> {
        title: <title>,
        meta: [<meta name: string, content: string>]*
    },
    body: <body>
}

// ========== Valid Test Cases ==========

// Simple element (no attributes or content)
let valid_paragraph: Paragraph = <p>"This is a paragraph"</p>

// Element with attributes
let valid_link: Link = <a href: "https://example.com">"Click here"</a>

// Element with multiple attributes
let valid_button: Button = <button type: "submit", disabled: false>"Submit"</button>

// Element with optional attribute present
let valid_image_with_alt: Image = <img src: "photo.jpg", alt: "A photo">

// Element with optional attribute absent
let valid_image_no_alt: Image = <img src: "photo.jpg">

// Element with nested content
let valid_article: Article = <article>{
    title: "My Article",
    content: [
        <p>"First paragraph"</p>,
        <p>"Second paragraph"</p>
    ]
}</article>

// Complex nested structure
let valid_page: Page = <html>{
    head: <head>{
        title: <title>"My Page"</title>,
        meta: [
            <meta name: "description", content: "A test page">,
            <meta name: "author", content: "Alice">
        ]
    },
    body: <body>"Page content"</body>
}</html>

// Element with empty content
let valid_empty: Paragraph = <p></p>

// ========== Invalid Test Cases ==========

// Wrong tag name
let invalid_tag: Paragraph = <div>"Not a paragraph"</div>  // Error: Expected <p>, got <div>

// Missing required attribute
let invalid_link_no_href: Link = <a>"No href"</a>  // Error: Missing required attribute 'href'

// Wrong attribute type
let invalid_button_type: Button = <button type: 123, disabled: false>"Submit"</button>  
// Error: Attribute 'type' should be string, got int

// Extra unexpected attributes (if strict)
let invalid_button_extra: Button = <button type: "submit", disabled: false, id: "btn1">"Submit"</button>
// Error: Unexpected attribute 'id'

// Missing required nested content
let invalid_article_no_title: Article = <article>{
    content: [<p>"Paragraph"</p>]
}</article>  // Error: Missing required field 'title'

// Wrong nested element type
let invalid_article_wrong_content: Article = <article>{
    title: "Title",
    content: [<div>"Not a paragraph"</div>]  // Error: Expected <p>, got <div>
}</article>

// Wrong content type (not an element)
let invalid_not_element: Paragraph = "Just text"  // Error: Expected element, got string

// ========== Edge Cases ==========

// Self-closing element
let edge_self_closing: Image = <img src: "test.jpg" />

// Element with mixed content (text and elements)
type Div = <div>
let edge_mixed_content: Div = <div>
    "Some text"
    <p>"Nested paragraph"</p>
    "More text"
</div>

// Deeply nested elements
type NestedDiv = <div> {inner: NestedDiv?}
let edge_deep_nesting: NestedDiv = <div>{
    inner: <div>{
        inner: <div>{
            inner: null
        }</div>
    }</div>
}</div>

// Element with special characters in content
let edge_special_chars: Paragraph = <p>"Special: <>&\"'"</p>

// Element with number content
let edge_number_content: Paragraph = <p>42</p>  // Content can be number?

// ========== HTML-specific Elements ==========

type Html5Elements = 
    | <div>
    | <span>
    | <section>
    | <article>
    | <header>
    | <footer>
    | <nav>
    | <aside>
    | <main>

let valid_html5_div: Html5Elements = <div>"Div content"</div>
let valid_html5_section: Html5Elements = <section>"Section content"</section>
let invalid_html5: Html5Elements = <table>"Wrong element"</table>  // Error: Not in union
