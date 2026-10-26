// The Books window: library state, its event handlers, and the document both
// the demo and its UI fixture open (the fixture keeps no reading state).
import dom
import reader: lambda.doc.ebook.reader
import lb: .mod_library
import ui: .mod_ui

pn close_book(lib, st, persist) map {
    let e = lb.find(lib, st.open)
    return {*: st, open: null, saved: null, places: lb.refresh(st.places, e, persist)}
}

view <books_app> state st: {section: "home", query: "", sort: "recent", open: null, saved: null,
        places: {}, opened: [], ready: false} {
    ui.page(~.library, st)
}
// saved places live on disk, so they are read once the window is up
on load(evt) {
    if (st.ready) { return 'pass' }
    st = {*: st, ready: true, places: lb.places(~.library, ~.persist), opened: lb.opened(~.persist)}
    return 'handled'
}
on click(evt) {
    let book = dom.closest(evt.target, "[data-book]")
    if (book != null and st.open == null) {
        let id = dom.get_attribute(book, "data-book")
        st = {*: st, open: id, saved: lb.saved(lb.find(~.library, id), ~.persist),
            opened: lb.mark_opened(st.opened, id, ~.persist)}
        return 'handled'
    }
    if (dom.closest(evt.target, "[data-act=close]") != null and st.open != null) {
        st = close_book(~.library, st, ~.persist)
        return 'handled'
    }
    let section = dom.closest(evt.target, "[data-section]")
    if (section != null) {
        // a section leaves any search; the field keeps its own typed value
        let field = dom.get_element_by_id(dom.root_node(evt.target), "search")
        if (field != null) dom.set_state(field, "value", "")
        st = {*: st, section: dom.get_attribute(section, "data-section"), query: ""}
        return 'handled'
    }
    let sort = dom.closest(evt.target, "[data-sort]")
    if (sort != null) {
        st = {*: st, sort: dom.get_attribute(sort, "data-sort")}
        return 'handled'
    }
    return 'pass'
}
on input(evt) {
    if (dom.get_attribute(evt.target, "id") != "search") { return 'pass' }
    st = {*: st, query: dom.get_state(evt.target, "value")}
    return 'handled'
}
on keydown(evt) {
    let typing = dom.closest(evt.target, "input,textarea,select,[contenteditable]") != null
    if (st.open != null and evt.key == "Escape" and not typing) {
        st = close_book(~.library, st, ~.persist)
        return 'prevent-default'
    }
    if (st.open == null and (evt.metaKey or evt.ctrlKey) and lower(string(evt.key)) == "f") {
        dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target), "search"), false)
        return 'prevent-default'
    }
    return 'pass'
}

// base is the demo folder; persist: false neither reads nor saves reading state
pub fn document(base, persist) => (let css = input(url_resolve(base, "book.css"), 'text')^,
    <html lang:"en",
        <head <meta charset:"UTF-8"><title "Books — Lambda"><style (reader.stylesheet ++ css)>>
        apply(<books_app library:lb.load_all(base), persist:persist>)>)
