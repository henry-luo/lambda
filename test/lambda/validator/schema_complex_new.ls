// Complex nested schema
type Author = {
    name: string,
    email: string?
}

type Document = {
    title: string,
    authors: [Author+],
    content: <section
        id: string?,
        children: Document*
    >
}
