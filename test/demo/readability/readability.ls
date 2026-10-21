// Run: ./lambda.exe demo readability
// A browser window with a few cluttered pages; the reader icon in the address bar
// swaps a page for the article lambda.doc.readability extracts from it.
import dom
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "readability.css"), 'text')^
// every page is checked and extracted once, up front: the toggle then only re-renders
let PAGES = model.load_pages(BASE)^

// the view renders <body> so that keys reach it after focus falls back to the body
view <readability_app> state st:model.initial() {
    <body ui.page(st, PAGES)>
}
on click(evt) {
    let button = dom.closest(evt.target, "[data-action]")
    if (button == null or dom.get_attribute(button, "disabled") != null) { return 'pass' }
    st = model.action(st, PAGES, dom.get_attribute(button, "data-action"), dom.get_attribute(button, "data-value"))
    return 'handled'
}
on keydown(evt) {
    let next = model.shortcut(st, PAGES, string(evt.key), evt.metaKey or evt.ctrlKey, evt.shiftKey)
    if (next == st) { return 'pass' }
    st = next
    return 'prevent-default'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Reader View — Lambda Readability"><style CSS>>
    apply(<readability_app>)
>
