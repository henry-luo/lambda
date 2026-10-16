import dom

view <frame_host> { ~.rendered }
on click(evt) {
    if (dom.get_attribute(evt.target,"id") != "replace") { return 'pass' }
    let owner = dom.get_element_by_id(dom.root_node(evt.target),"frame-host")
    dom.set_inner_html(dom.get_element_by_id(owner,"pane"),"<strong id='replacement'>Replaced safely</strong>")
    dom.set_attribute(owner,"data-alive","true")
    return 'prevent-default'
}

<html <body apply(<frame_host rendered:<main id:"frame-host",
    <button id:"replace",type:"button","Replace embedded document">;
    <section id:"pane",<iframe id:"child",src:"../demo/finder/tests/fixtures/Preview%20%23%20%C3%BC.md",
        style:"width:400px;height:200px">>>>)>;
    // exercise a Lambda callback while the parent also owns a JS realm.
    <script "void 0;">>
