// Schema specifically designed for HTML input validation
type HtmlAttribute = {
    name: string,
    value: string
}

// The Document type should match what the HTML parser produces: an HTML element
type Document = <html 
    lang: string?,                    // optional lang attribute
    class: string*,                   // optional class attributes  
    id: string?,                      // optional id
    children: Document*               // children elements
>
