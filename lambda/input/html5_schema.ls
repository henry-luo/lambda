// HTML5 Schema Definition in Lambda Script
// Comprehensive schema for valid HTML5 documents according to W3C HTML5 specification
// Defines all standard HTML5 elements with their allowed attributes and content models

// Basic data types
type HTMLString = string
type URL = string
type MIME = string
type Language = string
type Color = string
type Number = string | int
type Boolean = bool | 'true' | 'false' | ''

// Common attribute groups
type GlobalAttrs = {
    id: string?,
    class: string?,
    style: string?,
    title: string?,
    lang: Language?,
    dir: 'ltr' | 'rtl' | 'auto'?,
    hidden: Boolean?,
    tabindex: int?,
    accesskey: string?,
    contenteditable: 'true' | 'false' | ''?,
    draggable: 'true' | 'false' | 'auto'?,
    dropzone: string?,
    spellcheck: 'true' | 'false' | ''?,
    translate: 'yes' | 'no'?,
    'data-*': string?
}

type EventAttrs = {
    onabort: string?,
    onblur: string?,
    oncancel: string?,
    oncanplay: string?,
    oncanplaythrough: string?,
    onchange: string?,
    onclick: string?,
    onclose: string?,
    oncontextmenu: string?,
    oncuechange: string?,
    ondblclick: string?,
    ondrag: string?,
    ondragend: string?,
    ondragenter: string?,
    ondragexit: string?,
    ondragleave: string?,
    ondragover: string?,
    ondragstart: string?,
    ondrop: string?,
    ondurationchange: string?,
    onemptied: string?,
    onended: string?,
    onerror: string?,
    onfocus: string?,
    onformchange: string?,
    onforminput: string?,
    oninput: string?,
    oninvalid: string?,
    onkeydown: string?,
    onkeypress: string?,
    onkeyup: string?,
    onload: string?,
    onloadeddata: string?,
    onloadedmetadata: string?,
    onloadstart: string?,
    onmousedown: string?,
    onmousemove: string?,
    onmouseout: string?,
    onmouseover: string?,
    onmouseup: string?,
    onmousewheel: string?,
    onpause: string?,
    onplay: string?,
    onplaying: string?,
    onprogress: string?,
    onratechange: string?,
    onreadystatechange: string?,
    onreset: string?,
    onresize: string?,
    onscroll: string?,
    onseeked: string?,
    onseeking: string?,
    onselect: string?,
    onshow: string?,
    onstalled: string?,
    onsubmit: string?,
    onsuspend: string?,
    ontimeupdate: string?,
    onvolumechange: string?,
    onwaiting: string?
}

type CommonAttrs = GlobalAttrs & EventAttrs

// Microdata attributes
type MicrodataAttrs = {
    itemid: URL?,
    itemprop: string?,
    itemref: string?,
    itemscope: Boolean?,
    itemtype: URL?
}

type AllAttrs = CommonAttrs & MicrodataAttrs

// Content model types
type Flow = element
type Phrasing = element
type Embedded = element
type Interactive = element
type Heading = element
type Sectioning = element
type FormAssociated = element
type ListedFormAssociated = element
type SubmittableFormAssociated = element
type ResettableFormAssociated = element
type LabelableFormAssociated = element

// Document structure
type HTMLDocument <
    version: '5',
    <html: HtmlAttrs, <head>, <body>>
>

type HtmlAttrs = AllAttrs & {
    manifest: URL?,
    xmlns: 'http://www.w3.org/1999/xhtml'?
}

// Head elements
type Head < AllAttrs, HeadContent* >

type HeadContent = title | base | link | meta | style | script | noscript | template

type Title < AllAttrs, HTMLString >

type Base < AllAttrs & {
    href: URL?,
    target: '_blank' | '_self' | '_parent' | '_top' | string?
} >

type Link < AllAttrs & {
    href: URL?,
    crossorigin: 'anonymous' | 'use-credentials' | ''?,
    rel: string?,
    media: string?,
    hreflang: Language?,
    type: MIME?,
    sizes: string?,
    as: 'audio' | 'document' | 'embed' | 'fetch' | 'font' | 'image' | 'object' | 'script' | 'style' | 'track' | 'video' | 'worker'?,
    color: Color?,
    disabled: Boolean?,
    integrity: string?,
    referrerpolicy: 'no-referrer' | 'no-referrer-when-downgrade' | 'origin' | 'origin-when-cross-origin' | 'same-origin' | 'strict-origin' | 'strict-origin-when-cross-origin' | 'unsafe-url'?
} >

type Meta < AllAttrs & {
    name: string?,
    'http-equiv': 'content-type' | 'default-style' | 'refresh' | 'x-ua-compatible' | 'content-security-policy'?,
    content: string?,
    charset: string?
} >

type Style < AllAttrs & {
    media: string?,
    type: MIME?,
    disabled: Boolean?
}, HTMLString >

// Body and sectioning elements
type Body < AllAttrs, Flow* >

type Article < AllAttrs, Flow* >
type Section < AllAttrs, Flow* >
type Nav < AllAttrs, Flow* >
type Aside < AllAttrs, Flow* >
type Header < AllAttrs, Flow* >
type Footer < AllAttrs, Flow* >
type Main < AllAttrs, Flow* >

// Heading elements
type H1 < AllAttrs, Phrasing* >
type H2 < AllAttrs, Phrasing* >
type H3 < AllAttrs, Phrasing* >
type H4 < AllAttrs, Phrasing* >
type H5 < AllAttrs, Phrasing* >
type H6 < AllAttrs, Phrasing* >
type Hgroup < AllAttrs, (H1 | H2 | H3 | H4 | H5 | H6 | Template | Script)* >

// Flow content elements  
type P < AllAttrs, Phrasing* >
type Hr < AllAttrs >
type Pre < AllAttrs, Phrasing* >
type Blockquote < AllAttrs & {
    cite: URL?
}, Flow* >

type Ol < AllAttrs & {
    reversed: Boolean?,
    start: int?,
    type: '1' | 'a' | 'A' | 'i' | 'I'?
}, Li* >

type Ul < AllAttrs, Li* >

type Li < AllAttrs & {
    value: int?
}, Flow* >

type Dl < AllAttrs, (Dt | Dd | Div | Template | Script)* >
type Dt < AllAttrs, Phrasing* >
type Dd < AllAttrs, Flow* >

type Figure < AllAttrs, (Flow | Figcaption)* >
type Figcaption < AllAttrs, Flow* >
type Div < AllAttrs, Flow* >

// Text-level semantic elements
type A < AllAttrs & {
    href: URL?,
    target: '_blank' | '_self' | '_parent' | '_top' | string?,
    download: string?,
    ping: string?,
    rel: string?,
    hreflang: Language?,
    type: MIME?,
    referrerpolicy: 'no-referrer' | 'no-referrer-when-downgrade' | 'origin' | 'origin-when-cross-origin' | 'same-origin' | 'strict-origin' | 'strict-origin-when-cross-origin' | 'unsafe-url'?
}, Phrasing* >

type Em < AllAttrs, Phrasing* >
type Strong < AllAttrs, Phrasing* >
type Small < AllAttrs, Phrasing* >
type S < AllAttrs, Phrasing* >
type Cite < AllAttrs, Phrasing* >
type Q < AllAttrs & {
    cite: URL?
}, Phrasing* >

type Dfn < AllAttrs, Phrasing* >
type Abbr < AllAttrs, Phrasing* >
type Ruby < AllAttrs, (Phrasing | Rt | Rp)* >
type Rt < AllAttrs, Phrasing* >
type Rp < AllAttrs, Phrasing* >
type Data < AllAttrs & {
    value: string
}, Phrasing* >

type Time < AllAttrs & {
    datetime: string?
}, Phrasing* >

type Code < AllAttrs, Phrasing* >
type Var < AllAttrs, Phrasing* >
type Samp < AllAttrs, Phrasing* >
type Kbd < AllAttrs, Phrasing* >
type Sub < AllAttrs, Phrasing* >
type Sup < AllAttrs, Phrasing* >
type I < AllAttrs, Phrasing* >
type B < AllAttrs, Phrasing* >
type U < AllAttrs, Phrasing* >
type Mark < AllAttrs, Phrasing* >
type Bdi < AllAttrs, Phrasing* >
type Bdo < AllAttrs, Phrasing* >
type Span < AllAttrs, Phrasing* >
type Br < AllAttrs >
type Wbr < AllAttrs >

// Edits
type Ins < AllAttrs & {
    cite: URL?,
    datetime: string?
}, Phrasing* >

type Del < AllAttrs & {
    cite: URL?,
    datetime: string?
}, Phrasing* >

// Embedded content
type Picture < AllAttrs, (Source* & Img & Script*) >

type Source < AllAttrs & {
    src: URL?,
    type: MIME?,
    srcset: string?,
    sizes: string?,
    media: string?,
    width: Number?,
    height: Number?
} >

type Img < AllAttrs & {
    alt: string?,
    src: URL?,
    srcset: string?,
    sizes: string?,
    crossorigin: 'anonymous' | 'use-credentials' | ''?,
    usemap: string?,
    ismap: Boolean?,
    width: Number?,
    height: Number?,
    decoding: 'sync' | 'async' | 'auto'?,
    loading: 'eager' | 'lazy'?,
    referrerpolicy: 'no-referrer' | 'no-referrer-when-downgrade' | 'origin' | 'origin-when-cross-origin' | 'same-origin' | 'strict-origin' | 'strict-origin-when-cross-origin' | 'unsafe-url'?
} >

type Iframe < AllAttrs & {
    src: URL?,
    srcdoc: string?,
    name: string?,
    sandbox: string?,
    allow: string?,
    allowfullscreen: Boolean?,
    width: Number?,
    height: Number?,
    referrerpolicy: 'no-referrer' | 'no-referrer-when-downgrade' | 'origin' | 'origin-when-cross-origin' | 'same-origin' | 'strict-origin' | 'strict-origin-when-cross-origin' | 'unsafe-url'?,
    loading: 'eager' | 'lazy'?
} >

type Embed < AllAttrs & {
    src: URL?,
    type: MIME?,
    width: Number?,
    height: Number?
} >

type Object < AllAttrs & {
    data: URL?,
    type: MIME?,
    name: string?,
    usemap: string?,
    form: string?,
    width: Number?,
    height: Number?
}, (Param* & Flow*) >

type Param < AllAttrs & {
    name: string,
    value: string
} >

// Media elements
type Video < AllAttrs & {
    src: URL?,
    crossorigin: 'anonymous' | 'use-credentials' | ''?,
    poster: URL?,
    preload: 'none' | 'metadata' | 'auto' | ''?,
    autoplay: Boolean?,
    playsinline: Boolean?,
    loop: Boolean?,
    muted: Boolean?,
    controls: Boolean?,
    width: Number?,
    height: Number?
}, (Source* & Track* & Flow*) >

type Audio < AllAttrs & {
    src: URL?,
    crossorigin: 'anonymous' | 'use-credentials' | ''?,
    preload: 'none' | 'metadata' | 'auto' | ''?,
    autoplay: Boolean?,
    loop: Boolean?,
    muted: Boolean?,
    controls: Boolean?
}, (Source* & Track* & Flow*) >

type Track < AllAttrs & {
    default: Boolean?,
    kind: 'subtitles' | 'captions' | 'descriptions' | 'chapters' | 'metadata'?,
    label: string?,
    src: URL,
    srclang: Language?
} >

// Canvas and SVG
type Canvas < AllAttrs & {
    width: Number?,
    height: Number?
}, Flow* >

type Svg < AllAttrs & {
    width: Number?,
    height: Number?,
    viewBox: string?,
    xmlns: 'http://www.w3.org/2000/svg'?
}, any* >

// Map
type Map < AllAttrs & {
    name: string
}, Flow* >

type Area < AllAttrs & {
    alt: string?,
    coords: string?,
    shape: 'rect' | 'circle' | 'poly' | 'default'?,
    href: URL?,
    target: '_blank' | '_self' | '_parent' | '_top' | string?,
    download: string?,
    ping: string?,
    rel: string?,
    referrerpolicy: 'no-referrer' | 'no-referrer-when-downgrade' | 'origin' | 'origin-when-cross-origin' | 'same-origin' | 'strict-origin' | 'strict-origin-when-cross-origin' | 'unsafe-url'?
} >

// Table elements
type Table < AllAttrs, (Caption? & Colgroup* & Thead? & Tbody* & Tfoot? & Tr*) >
type Caption < AllAttrs, Flow* >
type Colgroup < AllAttrs & {
    span: int?
}, Col* >

type Col < AllAttrs & {
    span: int?
} >

type Tbody < AllAttrs, Tr* >
type Thead < AllAttrs, Tr* >
type Tfoot < AllAttrs, Tr* >
type Tr < AllAttrs, (Td | Th)* >

type Td < AllAttrs & {
    colspan: int?,
    rowspan: int?,
    headers: string?
}, Flow* >

type Th < AllAttrs & {
    colspan: int?,
    rowspan: int?,
    headers: string?,
    scope: 'row' | 'col' | 'rowgroup' | 'colgroup' | 'auto'?,
    abbr: string?
}, Flow* >

// Forms
type Form < AllAttrs & {
    'accept-charset': string?,
    action: URL?,
    autocomplete: 'on' | 'off'?,
    enctype: 'application/x-www-form-urlencoded' | 'multipart/form-data' | 'text/plain'?,
    method: 'get' | 'post' | 'dialog'?,
    name: string?,
    novalidate: Boolean?,
    target: '_blank' | '_self' | '_parent' | '_top' | string?
}, Flow* >

type Label < AllAttrs & {
    for: string?
}, Phrasing* >

type Input < AllAttrs & {
    accept: string?,
    alt: string?,
    autocomplete: string?,
    checked: Boolean?,
    dirname: string?,
    disabled: Boolean?,
    form: string?,
    formaction: URL?,
    formenctype: 'application/x-www-form-urlencoded' | 'multipart/form-data' | 'text/plain'?,
    formmethod: 'get' | 'post'?,
    formnovalidate: Boolean?,
    formtarget: '_blank' | '_self' | '_parent' | '_top' | string?,
    height: Number?,
    list: string?,
    max: string?,
    maxlength: int?,
    min: string?,
    minlength: int?,
    multiple: Boolean?,
    name: string?,
    pattern: string?,
    placeholder: string?,
    readonly: Boolean?,
    required: Boolean?,
    size: int?,
    src: URL?,
    step: string?,
    type: 'hidden' | 'text' | 'search' | 'tel' | 'url' | 'email' | 'password' | 'date' | 'month' | 'week' | 'time' | 'datetime-local' | 'number' | 'range' | 'color' | 'checkbox' | 'radio' | 'file' | 'submit' | 'image' | 'reset' | 'button'?,
    value: string?,
    width: Number?
} >

type Button < AllAttrs & {
    disabled: Boolean?,
    form: string?,
    formaction: URL?,
    formenctype: 'application/x-www-form-urlencoded' | 'multipart/form-data' | 'text/plain'?,
    formmethod: 'get' | 'post'?,
    formnovalidate: Boolean?,
    formtarget: '_blank' | '_self' | '_parent' | '_top' | string?,
    name: string?,
    type: 'submit' | 'reset' | 'button'?,
    value: string?
}, Phrasing* >

type Select < AllAttrs & {
    autocomplete: string?,
    disabled: Boolean?,
    form: string?,
    multiple: Boolean?,
    name: string?,
    required: Boolean?,
    size: int?
}, (Option | Optgroup | Template | Script)* >

type Datalist < AllAttrs, (Phrasing | Option)* >

type Optgroup < AllAttrs & {
    disabled: Boolean?,
    label: string
}, (Option | Template | Script)* >

type Option < AllAttrs & {
    disabled: Boolean?,
    label: string?,
    selected: Boolean?,
    value: string?
}, HTMLString >

type Textarea < AllAttrs & {
    autocomplete: string?,
    cols: int?,
    dirname: string?,
    disabled: Boolean?,
    form: string?,
    maxlength: int?,
    minlength: int?,
    name: string?,
    placeholder: string?,
    readonly: Boolean?,
    required: Boolean?,
    rows: int?,
    wrap: 'hard' | 'soft'?
}, HTMLString >

type Output < AllAttrs & {
    for: string?,
    form: string?,
    name: string?
}, Phrasing* >

type Progress < AllAttrs & {
    value: Number?,
    max: Number?
}, Phrasing* >

type Meter < AllAttrs & {
    value: Number,
    min: Number?,
    max: Number?,
    low: Number?,
    high: Number?,
    optimum: Number?
}, Phrasing* >

type Fieldset < AllAttrs & {
    disabled: Boolean?,
    form: string?,
    name: string?
}, (Legend? & Flow*) >

type Legend < AllAttrs, Phrasing* >

// Interactive elements
type Details < AllAttrs & {
    open: Boolean?
}, (Summary? & Flow*) >

type Summary < AllAttrs, Phrasing* >

type Dialog < AllAttrs & {
    open: Boolean?
}, Flow* >

// Scripting
type Script < AllAttrs & {
    src: URL?,
    type: MIME?,
    charset: string?,
    async: Boolean?,
    defer: Boolean?,
    crossorigin: 'anonymous' | 'use-credentials' | ''?,
    integrity: string?,
    nomodule: Boolean?,
    referrerpolicy: 'no-referrer' | 'no-referrer-when-downgrade' | 'origin' | 'origin-when-cross-origin' | 'same-origin' | 'strict-origin' | 'strict-origin-when-cross-origin' | 'unsafe-url'?
}, HTMLString >

type Noscript < AllAttrs, (Phrasing | Flow)* >

type Template < AllAttrs, any* >

type Slot < AllAttrs & {
    name: string?
}, Flow* >

// Custom elements placeholder
type CustomElement < AllAttrs, any* >

// Complete HTML5 document structure
type HTML5Document = {
    doctype: '<!DOCTYPE html>',
    html: {
        lang?: Language,
        dir?: 'ltr' | 'rtl' | 'auto',
        head: {
            title: string,
            meta: [MetaElement*]?,
            link: [LinkElement*]?, 
            style: [StyleElement*]?,
            script: [ScriptElement*]?,
            base?: BaseElement
        },
        body: {
            content: [FlowElement*]
        }
    }
}

// Content categories for validation
type FlowElement = Article | Section | Nav | Aside | Header | Footer | Main | 
                   H1 | H2 | H3 | H4 | H5 | H6 | Hgroup |
                   P | Hr | Pre | Blockquote | Ol | Ul | Dl | Figure | Div |
                   A | Em | Strong | Small | S | Cite | Q | Dfn | Abbr | Ruby | Data | Time |
                   Code | Var | Samp | Kbd | Sub | Sup | I | B | U | Mark | Bdi | Bdo | Span |
                   Ins | Del | Picture | Img | Iframe | Embed | Object | Video | Audio | Canvas | Svg | Map |
                   Table | Form | Fieldset | Details | Dialog | Script | Noscript | Template | Slot

type PhrasingElement = Em | Strong | Small | S | Cite | Q | Dfn | Abbr | Ruby | Data | Time |
                       Code | Var | Samp | Kbd | Sub | Sup | I | B | U | Mark | Bdi | Bdo | Span | Br | Wbr |
                       A | Ins | Del | Img | Embed | Object | Video | Audio | Canvas | Svg | Map |
                       Label | Input | Button | Select | Datalist | Textarea | Output | Progress | Meter |
                       Script | Noscript | Template | Slot

type EmbeddedElement = Picture | Img | Iframe | Embed | Object | Video | Audio | Canvas | Svg | Math

type InteractiveElement = A | Audio | Button | Details | Embed | Iframe | Input | Label | Select | Textarea | Video

type HeadingElement = H1 | H2 | H3 | H4 | H5 | H6

type SectioningElement = Article | Section | Nav | Aside

// Validation rules for HTML5 compliance
type HTML5Rules = {
    // Document must have a single html element as root
    root: 'html',
    
    // Required elements in head
    required_head_elements: ['title'],
    
    // Void elements (self-closing)
    void_elements: ['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr'],
    
    // Elements that cannot contain interactive content
    no_interactive_content: ['button', 'a', 'audio', 'video', 'embed', 'iframe', 'img', 'input', 'label', 'select', 'textarea'],
    
    // Elements with specific content model restrictions
    content_restrictions: {
        p: 'no_block_content',
        h1: 'phrasing_only',
        h2: 'phrasing_only', 
        h3: 'phrasing_only',
        h4: 'phrasing_only',
        h5: 'phrasing_only',
        h6: 'phrasing_only',
        dt: 'phrasing_only',
        label: 'phrasing_only',
        legend: 'phrasing_only',
        caption: 'flow_content',
        figcaption: 'flow_content'
    }
}
