// Run: ./lambda.exe demo archive
// A personal web archive in the spirit of ArchiveBox (vibe/Lambda_Pkg_Archive.md
// §9): paste a URL or a path and press Archive; the job row shows its progress
// while the window keeps painting; the snapshot opens in the viewer, and the
// library can be filtered, searched, re-derived and pruned. The library lives
// in temp/archive/ (ARCHIVE_DEMO_ROOT overrides it); a first run archives a
// small local sample.
import dom
import archive: lambda.archive
import store: lambda.archive.store
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "archive.css"), 'text')^
let CONFIG = input(url_resolve(BASE, "archive.yaml"), 'yaml')^
let CWD = sys.proc.self.cwd#
let ROOT_SETTING = sys.proc.self.env.ARCHIVE_DEMO_ROOT#
let ROOT = if (ROOT_SETTING != null and ROOT_SETTING != "") ROOT_SETTING else "temp/archive"
let ROOT_ABS = if (starts_with(ROOT, "/")) ROOT else CWD ++ "/" ++ ROOT
let ENV = {root_url: "file://" ++ ROOT_ABS}

// Each service message becomes `on job(evt)` on the app: dom.dispatch from the
// job's task, looking the element up each time since a rebuild may replace it
// (AR-O13, resolved to dom.dispatch: the window loop runs tasks inside the
// document's evaluator and state batch, so the handler runs and re-renders).
pn report(root, message) {
    let owner = dom.get_element_by_id(root, "archive")
    if (owner != null) dom.dispatch(owner, {type: "job", bubbles: false, detail: message})
}

// The viewer reads the selected snapshot from a cache unpacked out of
// snapshot.zip (AR-E8 will read the members in place); media/ is copied beside it.
pn unpack_view(record) {
    if (record == null) { return null }
    let snapshot = ROOT_ABS ++ "/snapshots/" ++ record.id
    let dir = ROOT_ABS ++ "/.view/" ++ record.id
    if (not exists(dir ++ "/index.html")) {
        if (exists(dir)) io.delete(dir) ^ { null }
        io.mkdir(dir) ^ { null }
        store.unpack(snapshot ++ "/snapshot.zip", dir) ^ { null }
        if (exists(snapshot ++ "/media")) store.copy_tree(snapshot ++ "/media", dir ++ "/media") ^ { null }
    }
    var markdown = null
    if (exists(dir ++ "/article.md")) { input(dir ++ "/article.md", 'text') ^ { null } ~ { markdown = ~ } }
    return {id: record.id, url: "file://" ++ dir, readable: exists(dir ++ "/readable.html"), markdown: markdown}
}

// Archive pressed: the request goes to the service; the new state is returned
pn submit(st, service) {
    let next = model.request_add(st, CWD)
    var failed = service == null
    if (next.request != null and service != null) { send(service, next.request) ^ { failed = true } }
    if (failed and next.request != null) {*: next.state, note: "The archive service is not running."} else next.state
}

// the search hits for a query, over the library's text.txt files (AR8)
pn searched(st, library, query) {
    var hits = []
    if (trim(query) != "" and library != null) { archive.search(library, query) ^ { hits = [] } ~ { hits = ~ } }
    model.with_hits(st, query, hits)
}

view <archive_app> state st: model.initial(), service: null, library: null, shown: null {
    <body ui.page(st, ENV, shown)>
}

on load(evt) {
    // the snapshot frame's own load reaches this handler too: open the library once
    if (library != null or st.loaded) { return 'pass' }
    var lib = null
    var failure = null
    archive.library(ROOT, {config: CONFIG}) ^ { failure = ^ } ~ { lib = ~ }
    if (failure is error) {
        st = {*: model.loaded(st, []), note: "Cannot open the library: " ++ failure.message}
        return 'handled'
    }
    library = lib
    st = model.loaded(st, lib.records)
    shown = unpack_view(model.selected(st))
    let root = evt.target
    // owned by the document, so it outlives this handler; the handle in state reaches it later (S13.3.1v2)
    service = start(archive.service, [lib, pn (m) => { report(root, m) }], {owner: 'document'})
    if (len(lib.records) == 0) {
        for (r in model.sample_requests("file://" ++ CWD ++ "/")) { send(service, {op: 'add', *: r}) ^ { null } }
    }
    return 'handled'
}

on job(evt) {
    let before = st.selected
    st = model.apply_job(st, evt.detail)
    if (st.selected != before or (evt.detail.type == "done" and evt.detail.snapshot.id == st.selected)) {
        // a re-derived snapshot replaces its cache
        if (evt.detail.type == "done") io.delete(ROOT_ABS ++ "/.view/" ++ evt.detail.snapshot.id) ^ { null }
        shown = unpack_view(model.selected(st))
    }
    return 'handled'
}

on click(evt) {
    let button = dom.closest(evt.target, "[data-action]")
    if (button == null) { return 'pass' }
    let name = dom.get_attribute(button, "data-action")
    let value = dom.get_attribute(button, "data-value")
    if (name == "archive") {
        st = submit(st, service)
    } else if (name == "cancel") {
        if (service != null) { send(service, {op: 'cancel', tag: value}) ^ { null } }
    } else if (name == "rerun") {
        let next = model.request_refresh(st, value)
        st = next.state
        if (next.request != null and service != null) { send(service, next.request) ^ { null } }
    } else if (name == "confirm-delete") {
        let id = st.confirm
        if (id != null and library != null) {
            archive.remove(library, id) ^ { null }
            io.delete(ROOT_ABS ++ "/.view/" ++ id) ^ { null }
            st = model.removed(st, id)
            shown = unpack_view(model.selected(st))
        }
    } else if (name == "clear-search") {
        st = model.action(st, name, value)
        dom.set_state(dom.get_element_by_id(evt.target, "search"), "value", "") ^ { null }
    } else {
        let before = st.selected
        st = model.action(st, name, value)
        if (st.selected != before) { shown = unpack_view(model.selected(st)) }
    }
    return 'handled'
}

on input(evt) {
    let id = dom.get_attribute(evt.target, "id")
    if (id == "url") { st = {*: st, url: dom.get_state(evt.target, "value") or "", note: null} }
    else if (id == "search") { st = searched(st, library, dom.get_state(evt.target, "value") or "") }
    else { return 'pass' }
    return 'handled'
}

on change(evt) {
    if (dom.get_attribute(evt.target, "id") != "force") { return 'pass' }
    st = model.action(st, "force", dom.get_state(evt.target, "value"))
    return 'handled'
}

on keydown(evt) {
    if (evt.key == "Enter" and dom.get_attribute(evt.target, "id") == "url") {
        st = submit({*: st, url: dom.get_state(evt.target, "value") or st.url}, service)
        return 'prevent-default'
    }
    return 'pass'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Archive — Lambda"><style CSS>>
    apply(<archive_app>)
>
