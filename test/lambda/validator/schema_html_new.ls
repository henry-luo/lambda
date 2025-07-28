// Strict HTML schema that will fail for malformed HTML
type Document = {
    doctype: string,  // Required DOCTYPE
    html: HtmlElement
}

type HtmlElement = {
    tag: "html",
    lang: string?,  // optional fields
    children: [HeadBody+]  // Must have head and body, one-or-more occurrences
}

type HeadBody = Head | Body

type Head = {
    tag: "head",
    children: [HeadElement*]  // zero-or-more occurrences
}

type HeadElement = {
    tag: "title" | "meta" | "link",
    text: string?,
    attributes: {string: string}?
}

type Body = {
    tag: "body",
    children: [BodyElement*]
}

type BodyElement = {
    tag: "h1" | "h2" | "h3" | "h4" | "h5" | "h6" | "p" | "div" | "span" | "a" | "img" | "ul" | "ol" | "li",
    text: string?,
    attributes: {string: string}?,
    children: [BodyElement*]?
}
