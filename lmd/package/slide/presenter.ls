// presenter tools use wall time, independently of the deck's animation clock.
import c: .common

pub fn initial_state() => {console: false, overview: false, toolbar: true, blackout: false, pointer: false,
    elapsed_ms: 0.0, anchor_ms: 0.0, running: false, started: false}
pub fn elapsed(ui, now_ms) => ui.elapsed_ms + if (ui.running) max(0.0, now_ms - ui.anchor_ms) else 0.0
fn two(n) => if (n < 10) "0" ++ string(n) else string(n)
pub fn clock(ui, now_ms) {
    let seconds = int(floor(elapsed(ui, now_ms) / 1000.0))
    two(seconds div 3600) ++ ":" ++ two((seconds div 60) % 60) ++ ":" ++ two(seconds % 60)
}
pub fn reduce(ui, command, now_ms) map^ {
    let checked_time = if (not c.finite(now_ms)) raise c.fail("presenter", "invalid timer time")
    if (command == "console") {*: ui, console: not ui.console,
        started: true, running: if (not ui.started) true else ui.running,
        anchor_ms: if (not ui.started) now_ms else ui.anchor_ms}
    else if (command == "timer-toggle") {*: ui, elapsed_ms: elapsed(ui, now_ms), anchor_ms: now_ms, running: not ui.running, started: true}
    else if (command == "timer-reset") {*: ui, elapsed_ms: 0.0, anchor_ms: now_ms}
    else if (command == "escape") {*: ui, overview: false, blackout: false, pointer: false}
    else if (contains(["overview", "toolbar", "blackout", "pointer"], command)) {*: ui, [command]: not ui[command]}
    else raise c.fail("presenter", "unknown tool " ++ command)
}
pub fn destination(plan, value) int^ {
    let numeric = int(value)
    if (numeric != null and c.as_text(numeric) == c.as_text(value) and numeric >= 1 and numeric <= len(plan.slides)) numeric - 1
    else {
        let matches = [for (i, scene in plan.slides where scene.id == value) i]
        if (len(matches) == 1) matches[0] else raise c.fail("picker", "enter a slide number or ID")
    }
}
pub fn title(scene) => c.as_text(c.value(scene.source.title, scene.id))
pub fn button(command, label) => <button type: "button", ["data-slide-tool"]: command, label>

pub fn toolbar(plan, key) => [
    button("console", "Presenter"), button("overview", "Overview"),
    <label class: "slide-picker-label", ["for"]: key ++ "-picker", "Slide">,
    <input id: key ++ "-picker", class: "slide-picker", type: "text", value: "1", ["aria-label"]: "Slide number or ID">,
    button("go", "Go"), button("blackout", "Blackout"), button("pointer", "Pointer")]

pub fn console_tree(key) => <aside class: "slide-console", ["aria-label"]: "Presenter console", ["aria-hidden"]: "true", inert: "",
    <h2 "Presenter console">
    <div class: "slide-timer-controls", <output class: "slide-timer", ["aria-label"]: "Elapsed presentation time", "00:00:00">
        button("timer-toggle", "Pause timer")
        button("timer-reset", "Reset timer")>
    <h3 "Speaker notes">
    <div class: "slide-notes">
    <h3 class: "slide-preview-title", "Next slide">
    <div class: "slide-preview-stage", ["aria-label"]: "Next slide preview", inert: "",
        <div class: "slide-preview-canvas">>
    <p class: "slide-preview-end", "End of presentation">
>

pub fn overview_tree(plan) => <nav class: "slide-overview", ["aria-label"]: "Slide overview", ["aria-hidden"]: "true", inert: "",
    *[<h2 "Choose a slide">,
    button("overview", "Close overview"),
    <div class: "slide-overview-list", for (i, scene in plan.slides)
        <button type: "button", ["data-slide-tool"]: "go", ["data-slide-destination"]: string(i + 1),
            ["aria-current"]: if (i == 0) "true" else "false", string(i + 1) ++ ". " ++ title(scene)>>]
>
