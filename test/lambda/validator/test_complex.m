// Test data for complex nested structures combining multiple type features
{
    document: {
        meta: {
            title: "Complex Document",
            version: "2.1",
            authors: ["John Doe", "Jane Smith"],
            tags: ["test", "validation", "complex"]
        },
        sections: [
            {
                section_type: "header",
                level: 1,
                content: "Introduction",
                attributes: {
                    id: "intro",
                    class: "section-header"
                }
            },
            {
                section_type: "paragraph",
                content: "This is a complex document structure.",
                formatting: {
                    bold: false,
                    italic: true,
                    font_size: 12
                }
            },
            {
                section_type: "list",
                items: [
                    {text: "First item", priority: 1},
                    {text: "Second item", priority: 2},
                    {text: "Third item", priority: 3}
                ],
                ordered: true
            }
        ],
        footer: {
            copyright: "2024 Test Corp",
            links: [
                {url: "https://example.com", text: "Home"},
                {url: "https://example.com/about", text: "About"}
            ]
        }
    }
}
