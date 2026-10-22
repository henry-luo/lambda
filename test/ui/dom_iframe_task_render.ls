// A frame a running task renders: the task's dom.dispatch re-renders the view
// inside the task's evaluator, where layout may not load a frame; the host
// pump's next quiescent turn loads it (layout_ensure_iframe_document).
import dom

pn later(root) {
    sleep(50)^
    let owner = dom.get_element_by_id(root, "app")
    if (owner != null) dom.dispatch(owner, {type: "ready", bubbles: false, detail: "<p id='x'>from task</p>"})
}

view <task_frame_app> state html: null, started: false {
    <body <main id:"app", 'data-ready':string(html != null),
        if (html != null) <iframe id:"frame", srcdoc:html> else null>>
}
on load(evt) {
    if (started) { return 'pass' }
    started = true
    start(later, [dom.root_node(evt.target)], {owner: 'document'})
    return 'handled'
}
on ready(evt) {
    html = evt.detail
    return 'handled'
}

<html <head <title "task frame">> apply(<task_frame_app>)>
