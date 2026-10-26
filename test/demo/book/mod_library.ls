// The Books library: the bundled EPUBs, parsed once by lambda.doc.ebook, and
// each book's saved place, kept by the ebook package's reading-state store.
import ebook: lambda.doc.ebook
import chapters: lambda.doc.ebook.chapter
import store: lambda.doc.ebook.state
import paths: lambda.io.path

// showcase order; books/ holds the Project Gutenberg EPUBs (README)
let CATALOG = [
    {id: "alice", file: "alice-in-wonderland.epub", year: 1865, genres: ["Fantasy", "Children's Classics"], tint: "#c8553d"},
    {id: "sherlock", file: "sherlock-holmes.epub", year: 1892, genres: ["Mystery"], tint: "#2f4858"},
    {id: "frankenstein", file: "frankenstein.epub", year: 1818, genres: ["Gothic", "Science Fiction"], tint: "#4a5d23"},
    {id: "time-machine", file: "the-time-machine.epub", year: 1895, genres: ["Science Fiction"], tint: "#33658a"},
    {id: "moby-dick", file: "moby-dick.epub", year: 1851, genres: ["Adventure"], tint: "#1b4965"},
    {id: "peter-pan", file: "peter-pan.epub", year: 1911, genres: ["Fantasy", "Adventure", "Children's Classics"], tint: "#5a8f29"},
    {id: "dracula", file: "dracula.epub", year: 1897, genres: ["Gothic"], tint: "#6d1a36"}]

pub let COLLECTIONS = ["Fantasy", "Science Fiction", "Gothic", "Mystery", "Adventure", "Children's Classics"]

fn load(path) map^ => ebook.parse(input(path, "binary")^)^

// "Frankenstein; or, The modern Prometheus" shelves as a title and a subtitle
fn title_parts(book) => (let full = if (book == null or len(book.metadata.titles) == 0) "" else book.metadata.titles[0],
    let pieces = split(full, ";"),
    {title: trim(pieces[0]), subtitle: if (len(pieces) > 1) trim(join(drop(pieces, 1), ";")) else ""})

// Each catalog item with its publication, cover and chapter sizes; a book that
// cannot be read stays on the shelf with the reason.
fn entry(base, item) {
    let path = url_resolve(base, "books/" ++ item.file)
    let book = load(path) ^ { null }
    let names = title_parts(book)
    let cover = if (book == null or book.metadata.cover == null) null
        else (chapters.resource(book, book.metadata.cover.href) ^ { null })
    let sizes = if (book == null) [] else [for (e in book.reading_order) book.resources[e.href].size];
    {*: item, path: path, book: book, title: if (names.title == "") item.file else names.title, subtitle: names.subtitle,
     author: if (book == null) "" else join(book.metadata.creators, ", "),
     cover: if (cover == null or cover.value == "") null else cover.value,
     sizes: sizes, total: max(1, sum(sizes)), chapters: len(sizes)}
}
pub fn load_all(base) => [for (item in CATALOG) entry(base, item)]

pub fn find(lib, id) => [for (e in lib where e.id == id) e][0]

fn flatten(nodes) => [for (node in nodes, found in [node, *flatten(node.children)]) found]

// the contents label of a saved chapter, or its spine position when the book has none
fn chapter_label(e, occurrence) => (let href = e.book.reading_order[occurrence].href,
    let labels = [for (node in flatten(e.book.navigation.toc) where node.target != null and node.target.href == href) node.label],
    if (len(labels) > 0) labels[0] else "Chapter " ++ string(occurrence + 1) ++ " of " ++ string(e.chapters))

// A saved place as a share of the book: chapters before it count by their
// packaged size, and the current chapter by the text read so far.
pub fn place(e, location) => if (e.book == null or location == null) null else (
    let occurrence = location.spine_occurrence,
    let chapter = ebook.chapter(e.book, occurrence) ^ { null },
    let within = if (chapter == null or len(chapter.text) == 0) 0.0
        else float(location.source_text_position) / float(len(chapter.text)),
    {fraction: min(1.0, (float(sum(take(e.sizes, occurrence))) + float(e.sizes[occurrence]) * within) / float(e.total)),
     chapter: chapter_label(e, occurrence)})

pub fn percent(place) => if (place == null) "0%" else string(int(round(place.fraction * 100))) ++ "%"

// the reader's saved place, when the reader keeps one (persist: false keeps none)
pub pn saved(e, persist) map {
    if (e.book == null) { return null }
    return store.restore(e.book, {persist: persist})
}
// places maps a book id to its saved place; refresh re-reads one book's
pub pn refresh(places, e, persist) map {
    let kept = saved(e, persist)
    return if (kept == null or kept.location == null) places else {*: places, [e.id]: place(e, kept.location)}
}
pub pn places(lib, persist) map {
    var found = {}
    for (e in lib) { found = refresh(found, e, persist) }
    return found
}

// The ids of opened books, most recent first, beside the reader's own reading
// state; an order rather than timestamps, so books opened together never tie.
fn record_path() => store.directory() ++ "/books-library.json"
pub pn opened(persist) array {
    if (not persist) { return [] }
    let path = record_path()
    let record = if (exists(path)) (input(path, "json") ^ { null }) else null
    if (not (record is map and record.version == 1 and record.recent is array)) { return [] }
    return [for (id in record.recent where id is string) id]
}
pub pn mark_opened(recent, id, persist) array {
    let next = [id, *[for (other in recent where other != id) other]]
    if (persist) {
        let path = record_path()
        io.mkdir(paths.dirname(path)) ^ { return next }
        output({version: 1, recent: next}, path, {format: "json", atomic: true}) ^ { return next }
    }
    return next
}
