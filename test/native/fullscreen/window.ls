import dom

// S12.1.5v2: the template owns status that must survive viewport regeneration.
view <fullscreen_window> state available:false,mode:false,changes:0,accepted:false,failed:false,
    metrics:false,restored:false,normal_width:0,normal_height:0 {
    <body id:"result", ["data-available"]:string(available), ["data-mode"]:string(mode),
        ["data-changes"]:string(changes), ["data-accepted"]:string(accepted),
        ["data-error"]:string(failed), ["data-metrics"]:string(metrics),
        ["data-restored"]:string(restored), style:"font:20px sans-serif;padding:24px",
        <div id:"viewport-check",style:"position:fixed;left:0;top:0;width:100vw;height:100vh;pointer-events:none">
        <h1 "Radiant fullscreen">
        <button id:"enter", "Enter fullscreen">
        <button id:"exit", "Exit fullscreen">
        <p "F11 toggles fullscreen. Escape returns to the window.">
    >
}
on load(evt) {
    available = dom.window_fullscreen_enabled(evt.currentTarget)
    mode = dom.window_fullscreen(evt.currentTarget)
    let viewport = dom.viewport_size(evt.currentTarget)
    normal_width = viewport.width
    normal_height = viewport.height
}
on windowfullscreenchange(evt) {
    mode = dom.window_fullscreen(evt.currentTarget)
    let viewport = dom.viewport_size(evt.currentTarget)
    let marker = dom.get_element_by_id(evt.currentTarget, "viewport-check")
    let bounds = dom.bounding_box(marker)
    metrics = bounds.width == viewport.width and bounds.height == viewport.height
    if (not mode) {
        restored = viewport.width == normal_width and viewport.height == normal_height
    }
    changes = changes + 1
}
on windowfullscreenerror(evt) { failed = true }
on click(evt) {
    let action = dom.get_attribute(evt.target, "id")
    if (action == "enter" or action == "exit") {
        accepted = dom.set_window_fullscreen(evt.currentTarget, action == "enter")
    }
    'pass'
}

<html apply(<fullscreen_window>)>
