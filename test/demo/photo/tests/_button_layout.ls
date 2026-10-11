import dom
view <button_layout_probe> { ~.rendered }
on load(evt) {
    for (id in ["flex","grid"]) {
        let button = dom.get_element_by_id(evt.target,id)
        let icon = dom.query_selector(button,"span")
        let label = dom.query_selector(button,"small")
        let selected = dom.document_create_range(dom.root_node(button))
        dom.range_select_node_contents(selected,icon)
        let glyph = dom.range_get_bounding_client_rect(selected)
        let box = dom.bounding_box(icon)
        let caption = dom.bounding_box(label)
        // Text must stay inside its item after the container aligns its children.
        dom.set_attribute(button,"data-aligned",string(glyph.top >= box.top and glyph.bottom <= caption.top))
    }
    return 'handled'
}
<html <head <style "body{margin:0}button{box-sizing:border-box;width:80px;height:100px;padding:10px;border:0;gap:8px}span{display:block;height:28px;line-height:28px;font-size:20px}small{display:block;height:12px;line-height:12px;font-size:10px}#flex{display:flex;flex-direction:column;align-items:center;justify-content:center}#grid{display:grid;grid-template-rows:28px 12px;place-content:center;place-items:center}">>
    apply(<button_layout_probe rendered:<body
        <button id:"flex",<span "A"> <small "Caption">>
        <button id:"grid",<span "A"> <small "Caption">>>>)
>
