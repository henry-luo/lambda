// Pages and reader state for the Readability demo. Transitions return new
// state values (S1.4); the view renders them and handlers assign them (S12.1.3).
import readability: lambda.doc.readability

// one browser tab per page: its file under articles/, the site's favicon and the address shown
let CATALOG = [
    {id: "trees", file: "meridian-trees.html", site: "meridian", mark: "M", label: "Shade by design",
     address: "meridianledger.example/science/2026/07/shade-by-design"},
    {id: "focaccia", file: "crumb-focaccia.html", site: "crumb", mark: "C", label: "Overnight focaccia",
     address: "crumbandhearth.example/bread/overnight-focaccia"},
    {id: "reader", file: "fieldnotes-reader-mode.html", site: "fieldnotes", mark: "F", label: "How reader mode works",
     address: "fieldnotes.example/engineering/how-reader-mode-decides"},
    {id: "home", file: "meridian-home.html", site: "meridian", mark: "M", label: "The Meridian Ledger",
     address: "meridianledger.example"}
]
pub let THEMES = ["light", "sepia", "dark"]
// reader text sizes in px; state keeps an index into them
pub let SIZES = [15, 17, 19, 21, 24]
let MONTHS = ["January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"]

// a catalog page loaded from `base`, the demo directory URL. Like Firefox, only a page that
// passes isProbablyReaderable is offered reader view, so only those are extracted.
fn load_page(base, entry) map^ {
    let url = url_resolve(base, "articles/" ++ entry.file)
    let doc = input(url, 'html')^
    let readerable = readability.readerable(doc)^;
    {*: entry, url: url, readerable: readerable,
     article: if (readerable) readability.parse(doc, {url: url})^ else null}
}
pub fn load_pages(base) array^ => [for (entry in CATALOG) load_page(base, entry)^]

// `reader` lists the ids of the tabs showing reader view: each tab keeps its own mode
pub fn initial() => {at: 0, reader: [], theme: "light", size: 2}
pub fn page(st, pages) => pages[st.at]
pub fn reading(st, pages) => pages[st.at].id in st.reader

pub fn select_tab(st, pages, at) => if (at >= 0 and at < len(pages)) {*: st, at: at} else st
pub fn toggle(st, pages) {
    let id = pages[st.at].id;
    if (not pages[st.at].readerable) st
    else if (contains(st.reader, id)) {*: st, reader: [for (other in st.reader where other != id) other]}
    else {*: st, reader: [*st.reader, id]}
}
pub fn theme(st, choice) => if (contains(THEMES, choice)) {*: st, theme: choice} else st
pub fn resize_text(st, step) => {*: st, size: min(max(st.size + step, 0), len(SIZES) - 1)}

// a toolbar or reader-settings button press: `command` and `value` come from its data attributes
pub fn action(st, pages, command, value) =>
    if (command == "tab") select_tab(st, pages, int(value))
    else if (command == "reader") toggle(st, pages)
    else if (command == "theme") theme(st, value)
    else if (command == "smaller") resize_text(st, -1)
    else if (command == "larger") resize_text(st, 1)
    else st

// browser keys: Cmd/Ctrl+Shift+R toggles reader view (as in Safari), Escape leaves it,
// Cmd/Ctrl+1-9 picks a tab; any other key returns `st` unchanged
pub fn shortcut(st, pages, key, primary, shift) =>
    if (primary and shift and lower(key) == "r") toggle(st, pages)
    else if (key == "Escape" and reading(st, pages)) toggle(st, pages)
    else if (primary and not shift and key is \("1" to "9")) select_tab(st, pages, int(key) - 1)
    else st

// "14 July 2026" from an ISO 8601 timestamp, or null
pub fn date_label(stamp) => if (stamp is \("\d"{4} "-" "\d"{2} "-" "\d"{2} ...))
    string(int(slice(stamp, 8, 10))) ++ " " ++ MONTHS[int(slice(stamp, 5, 7)) - 1] ++ " " ++ slice(stamp, 0, 4)
    else null
pub fn words(text) int => len([for (word in split(text, \("\s"+)) where word != "") word])
// reading time at 220 words a minute, never under one minute
pub fn minutes(article) int => max(1, int(ceil(words(article.text) / 220)))
