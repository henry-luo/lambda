// A host drives an embedded player without controls through slide_command events
// and reads the deck clock from the player's position attributes.
import dom
import slide: lambda.slide
import slide_html: lambda.slide.html

let deck = <presentation width: 400.0, height: 240.0,
    <slide id: "a", <text id: "title", x: 20.0, y: 20.0, width: 300.0, height: 60.0, "A">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 400.0>>>
    <slide id: "b", transition: 'fade', transition_duration: 300.0,
        <text id: "title", x: 20.0, y: 20.0, width: 300.0, height: 60.0, "B">
        <cue start: 'entry', <effect target: "title", kind: 'fade-in', duration: 500.0>>>
    <slide id: "c", <text id: "end", x: 20.0, y: 20.0, width: 300.0, height: 60.0, "C">>>

view <host> {
    <div id: "host",
        <div class: "bar", *[for (cmd in ["play", "pause", "resume", "seek:1550", "seek:9000", "speed:2"])
            <button type: "button", ["data-host"]: cmd, cmd>]>;
        (slide.player(deck, {instance: "film", width: 400.0, height: 240.0, controls: false, autoplay_dwell_ms: 1000.0})^)>
}
on click(evt) {
    let button = dom.closest(evt.target, "[data-host]")
    if (button == null) { return 'pass' }
    let parts = split(dom.get_attribute(button, "data-host"), ":")
    let player = dom.get_element_by_id(dom.root_node(evt.target), "film")
    let detail = if (parts[0] == "seek") {command: "seek_time", position_ms: float(parts[1])}
        else if (parts[0] == "speed") {command: "speed", rate: float(parts[1])}
        else {command: parts[0]}
    dom.dispatch(player, {type: "slide_command", bubbles: false, detail: detail})
    return 'handled'
}

<html <head <style slide_html.css>> <body (apply(<host>))>>
