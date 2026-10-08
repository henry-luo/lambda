// Test: Document Processing
// Layer: 4 | Category: integration | Covers: elements, query, namespace, transform

// ===== Build document tree =====
let doc = <html
    <head
        <title "My Page">
        <meta charset: "utf-8">
    >
    <body
        <div class: "header",
            <h1 "Welcome">
            <nav
                <a href: "/home", "Home">
                <a href: "/about", "About">
                <a href: "/contact", "Contact">
            >
        >
        <div class: "content",
            <article
                <h2 "Article 1">
                <p "First paragraph">
                <p "Second paragraph">
            >
            <article
                <h2 "Article 2">
                <p "Another paragraph">
            >
        >
        <div class: "footer",
            <p "Copyright 2024">
        >
    >
>

// ===== Query for all paragraphs =====
let paragraphs = doc?<p>
count(paragraphs)

// ===== Query for all links =====
let links = doc?<a>
count(links)
links |> ~.href

// ===== Query for articles =====
let articles = doc?<article>
count(articles)

// ===== Extract headings =====
let h2s = doc?<h2>
h2s |> string(~[0])

// ===== Query specific div =====
let divs = doc?<div>
count(divs)
divs |> ~.class

// ===== Nested element construction =====
let table = <table
    <thead
        <tr
            <th "Name">
            <th "Value">
        >
    >
    <tbody
        for (i in 1 to 3)
            <tr
                <th "Item " ++ string(i)>
                <td string(i * 10)>
            >
    >
>

// ===== Query table cells =====
count(table?<td>)
count(table?<th>)

// ===== Transform document =====
let link_list = doc?<a> |> {
    url: ~.href,
    text: string(~[0])
}
link_list

// ===== Element attributes =====
let meta = doc?<meta>
meta.charset
