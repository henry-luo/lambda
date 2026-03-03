// Test: Document Processing
// Layer: 4 | Category: integration | Covers: elements, query, namespace, transform

// ===== Build document tree =====
let doc = <html>
    <head>
        <title> "My Page"
        <meta charset: "utf-8">
    <body>
        <div class: "header">
            <h1> "Welcome"
            <nav>
                <a href: "/home"> "Home"
                <a href: "/about"> "About"
                <a href: "/contact"> "Contact"
        <div class: "content">
            <article>
                <h2> "Article 1"
                <p> "First paragraph"
                <p> "Second paragraph"
            <article>
                <h2> "Article 2"
                <p> "Another paragraph"
        <div class: "footer">
            <p> "Copyright 2024"

// ===== Query for all paragraphs =====
let paragraphs = doc?p
len(paragraphs)

// ===== Query for all links =====
let links = doc?a
len(links)
links | map((a) => a.href)

// ===== Query for articles =====
let articles = doc?article
len(articles)

// ===== Extract headings =====
let h2s = doc?h2
h2s | map((h) => str(h[0]))

// ===== Query specific div =====
let divs = doc?div
len(divs)
divs | map((d) => d.class)

// ===== Nested element construction =====
let table = <table>
    <thead>
        <tr>
            <th> "Name"
            <th> "Value"
    <tbody>
        for (i in 1 to 3)
            <tr>
                <th> "Item " & str(i)
                <td> str(i * 10)

// ===== Query table cells =====
len(table?td)
len(table?th)

// ===== Transform document =====
let link_list = doc?a | map((a) => {
    url: a.href,
    text: str(a[0])
})
link_list

// ===== Element attributes =====
let meta = doc?meta
meta[0].charset
