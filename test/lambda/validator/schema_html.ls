// Schema specifically designed for HTML input validation
type HtmlAttribute = {
    name: string,
    value: string
}

type HtmlElement = <html 
    tag: string,
    attributes: HtmlAttribute*,
    content: string?,
    children: HtmlElement*
>

type Document = {
    doctype: string?,
    html: HtmlElement
}
