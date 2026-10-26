// Books views: a sidebar of sections and collections, the Home shelves, cover
// grids, and the reader, which is lambda.doc.ebook's own reader element.
import ebook: lambda.doc.ebook
import lb: .mod_library

let ICONS = {home: "M3 11l9-7 9 7 M5 10v10h5v-6h4v6h5V10",
    books: "M4 4h4v16H4z M10 4h4v16h-4z M15.6 5.2l3.9-1 3.9 15.4-3.9 1z",
    reading: "M2 5.5c3.2-1.3 6.5-1 10 1.2 3.5-2.2 6.8-2.5 10-1.2v13.2c-3.2-1.3-6.5-1-10 1.2-3.5-2.2-6.8-2.5-10-1.2z M12 6.7v13.2",
    unread: "M6 3h12v18l-6-4-6 4z",
    collection: "M5 4h14 M4 7.5h16 M3 11h18v9H3z",
    search: "M21 21l-5-5M18 10a8 8 0 1 1-16 0 8 8 0 0 1 16 0",
    back: "M15 5l-7 7 7 7"}
fn icon(name) => <svg class:"icon", viewBox:"0 0 24 24", fill:"none", stroke:"currentColor", 'stroke-width':"1.7",
    'stroke-linecap':"round", 'stroke-linejoin':"round", 'aria-hidden':"true", <path d:ICONS[name]>>

fn count_label(n) => string(n) ++ (if (n == 1) " book" else " books")

// a book is started once it has been opened or the reader has saved a place in it
fn started(st, e) => st.places[e.id] != null or e.id in st.opened
fn matches(e, query) => (let q = lower(trim(query)),
    contains(lower(e.title ++ " " ++ e.subtitle ++ " " ++ e.author ++ " " ++ join(e.genres, " ")), q) ^ { false })

pub fn shelf(lib, st, section) => if (section == "home" or section == "all") lib
    else if (section == "reading") [for (e in lib where started(st, e)) e]
    else if (section == "unread") [for (e in lib where not started(st, e)) e]
    else [for (e in lib where section in e.genres) e]

// most recently opened first; never-opened books keep catalog order after them
fn recent(st, books) => [for (id in st.opened, e in books where e.id == id) e] ++
    [for (e in books where not (e.id in st.opened)) e]
// titles sort past a leading article, as a library shelves them
fn title_key(title) => (let l = lower(title),
    if (starts_with(l, "the ")) slice(l, 4, len(l)) else if (starts_with(l, "a ")) slice(l, 2, len(l)) else l)
fn ordered(st, books) => if (st.sort == "title") sort(books, (e) => title_key(e.title))
    else if (st.sort == "author") sort(books, (e) => lower(e.author))
    else recent(st, books)

fn cover(e) => if (e.cover != null) <img class:"cover", src:e.cover, alt:"">
    else <span class:"cover cover-plain", style:"background:" ++ e.tint,
        <span class:"cover-plain-title", e.title>; <span class:"cover-plain-author", e.author>>

fn meter(place) => <span class:"meter", <span class:"meter-fill",
    style:"width:" ++ (if (place == null) "0" else string(round(place.fraction * 1000) / 10)) ++ "%">>

fn card(st, e) => <button type:"button", class:"book", 'data-book':e.id, title:e.title,
    *:(if (e.book == null) {disabled:""} else {}),
    <span class:"cover-wrap", (cover(e))>
    <span class:"book-title", e.title>
    <span class:"book-author", if (e.book == null) "Cannot read this book" else e.author>
    if (started(st, e)) <span class:"book-state", (meter(st.places[e.id])); <span class:"book-percent", lb.percent(st.places[e.id])>>
    else <span class:"book-state", <span class:"badge", "NEW">; <span class:"book-year", string(e.year)>>>

fn grid(st, books) => <div class:"grid", for (e in books) card(st, e)>

fn continue_card(st, e) => (let place = st.places[e.id],
    <button type:"button", class:"continue", 'data-book':e.id, title:e.title,
        <span class:"continue-cover", (cover(e))>
        <span class:"continue-text",
            <span class:"continue-kicker", if (place == null) "Just opened" else lb.percent(place) ++ " read">
            <span class:"continue-title", e.title>
            <span class:"continue-author", e.author>
            <span class:"continue-chapter", if (place == null) "From the beginning" else place.chapter>;
            (meter(place))>>)

fn collection_tile(lib, name) => (let books = shelf(lib, {}, name),
    <button type:"button", class:"collection", 'data-section':name,
        <span class:"collection-covers", for (e in take(books, 3)) <span class:"mini-cover", (cover(e))>>
        <span class:"collection-name", name>
        <span class:"collection-count", count_label(len(books))>>)

fn heading(title, books, st, sortable) => <header class:"shelf-head",
    <div class:"shelf-heading", <h1 title>; <p class:"shelf-count", count_label(len(books))>>
    if (sortable) <div class:"sort", role:"group", 'aria-label':"Sort books",
        for (key in ["recent", "title", "author"]) <button type:"button", class:"sort-option", 'data-sort':key,
            'aria-pressed':string(st.sort == key), {recent: "Recent", title: "Title", author: "Author"}[key]>>>

fn home(lib, st) => (let current = take([for (e in recent(st, lib) where started(st, e)) e], 3),
    let next = [for (e in lib where not started(st, e)) e],
    <div class:"home",
        <header class:"shelf-head", <div class:"shelf-heading", <h1 "Home">;
            <p class:"shelf-count", "Classics from Project Gutenberg">>>
        <section class:"row", <h2 "Continue Reading">;
            if (len(current) == 0) <p class:"row-empty", "Open a book and it waits here, at the page where you left off.">
            else <div class:"continue-row", for (e in current) continue_card(st, e)>>
        if (len(next) > 0) <section class:"row", <h2 "Up Next">; (grid(st, next))>;
        <section class:"row", <h2 "Collections">;
            <div class:"collections", for (name in lb.COLLECTIONS) collection_tile(lib, name)>>>)

fn section_title(section) => {home: "Home", all: "All Books", reading: "In Progress", unread: "Unread"}[section] or section

fn content(lib, st) => if (trim(st.query) != "") (let found = [for (e in lib where matches(e, st.query)) e],
        <div class:"results", (heading("Results for “" ++ trim(st.query) ++ "”", found, st, false));
            if (len(found) == 0) <p class:"row-empty", "No books match your search."> else grid(st, found)>)
    else if (st.section == "home") home(lib, st)
    else (let books = ordered(st, shelf(lib, st, st.section)),
        <div class:"collection-page", (heading(section_title(st.section), books, st, true));
            if (len(books) == 0) <p class:"row-empty", "Nothing here yet."> else grid(st, books)>)

fn nav_item(lib, st, key, icon_name) => (let n = len(shelf(lib, st, key)),
    <button type:"button", class:"nav-item", 'data-section':key,
        *:(if (st.section == key and trim(st.query) == "") {'aria-current':"page"} else {}),
        (icon(icon_name)); <span class:"nav-label", section_title(key)>; <span class:"nav-count", string(n)>>)

fn sidebar(lib, st) => <aside class:"sidebar",
    <label class:"search", (icon("search"));
        <input id:"search", class:"search-input", type:"search", placeholder:"Search", value:st.query, 'aria-label':"Search books">>
    <nav class:"nav", 'aria-label':"Library",
        <h2 "Library">
        nav_item(lib, st, "home", "home"); nav_item(lib, st, "all", "books");
        nav_item(lib, st, "reading", "reading"); nav_item(lib, st, "unread", "unread");
        <h2 "Collections">
        for (name in lb.COLLECTIONS) nav_item(lib, st, name, "collection")>;
    <footer class:"sidebar-foot", "EPUB books read by lambda.doc.ebook">>

fn reading(lib, st) => (let e = lb.find(lib, st.open),
    <body class:"books-reading",
        <button type:"button", class:"books-back", 'data-act':"close", title:"Back to Library (Esc)", (icon("back")); "Library">;
        (ebook.reader(e.book, {saved: st.saved}) ^ { <p class:"read-error", "This book cannot be opened."> })>)

pub fn page(lib, st) => if (st.open != null) reading(lib, st)
    else <body class:"books", (sidebar(lib, st)); <main class:"shelf", (content(lib, st))>>
