import dom

view <fullscreen_probe> { ~.tree }
on probe(evt) {
    let owner = evt.currentTarget
    dom.set_attribute(owner, "data-available", string(dom.window_fullscreen_enabled(owner)))
    dom.set_attribute(owner, "data-actual", string(dom.window_fullscreen(owner)))
    dom.set_attribute(owner, "data-enter", string(dom.set_window_fullscreen(owner, true)))
    dom.set_attribute(owner, "data-exit", string(dom.set_window_fullscreen(owner, false)))
    return 'handled'
}

<html apply(<fullscreen_probe tree:<body id:"result">>)>
