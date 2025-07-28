// XML Edge Cases Schema
// Tests XML edge cases: empty elements, CDATA, processing instructions, etc.

type ProcessingInstruction = <pi
    target: string,
    data: string?
>

type Comment = {
    content: string
}

type CDataSection = {
    content: string
}

type EmptyElement = <empty
    id: string?,
    class: string*
>

type SelfClosingElement = <selfclosing
    name: string,
    value: string?
>

type MixedContentElement = <mixed
    id: string,
    content: (string | NestedElement | Comment | CDataSection)*
>

type NestedElement = <nested
    level: int,
    data: string?
>

type Document = <document
    version: string,
    standalone: bool?,
    processingInstructions: ProcessingInstruction*,
    root: RootElement
>

type RootElement = <root
    xmlns: string?,
    empty: EmptyElement*,
    selfclosing: SelfClosingElement*,
    mixed: MixedContentElement*,
    comments: Comment*
>
