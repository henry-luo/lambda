// Keep the card mounted across studio redraws. Like the photo album, turn the two-sided
// leaf through presentation transforms; changing clip paths would relayout every frame.
import dom
import updates: ~~.mod_publish
import A: .mod_art
import P: .mod_popup
import ui: .mod_ui

pub let TURN_MS = 550.0
pub fn progress(pose) => if (pose == "open") 1.0 else if (pose == "half") .5 else 0.0
fn read_number(owner, key, fallback = 0.0) => float(dom.get_attribute(owner, key) or string(fallback)) ^ { fallback }
fn book(owner) => dom.query_selector(owner, ".book")

pub pn cancel_turn(owner) {
    let token = read_number(owner, "data-frame")
    if (token > 0) dom.cancel_frame(owner, int(token))
    updates.attr(owner, "data-frame", "")
    updates.attr(owner, "data-turning", "false")
    return null
}
pn settle(owner, pose) {
    cancel_turn(owner)
    let el = book(owner)
    if (el == null) { return null }
    updates.attr(el, "data-turning", "false")
    paint(owner, progress(pose))
    dom.presentation_style_clear(el)
    dom.presentation_style_clear(dom.query_selector(el, ".cover"))
    for (plane in dom.query_selector_all(el, ".popup-live .popup-plane")) dom.presentation_style_clear(plane)
    updates.attr(owner, "data-progress", A.n(progress(pose)))
    return null
}
pn paint(owner, p) {
    let el = book(owner)
    dom.presentation_style_set_property(dom.query_selector(el, ".cover"), "transform", "rotateY(" ++ A.n(-180.0 * p) ++ "deg)")
    // Separate shade planes preserve each face's retained raster while its lighting changes.
    let shade = .42 * math.sin(math.pi * p)
    for (side in ["front", "inside"]) dom.presentation_style_set_property(dom.query_selector(el, ".turn-shade-" ++ side), "opacity", A.n(shade))
    let popup = dom.matches(el, ".popup-book")
    let yaw = P.CAMERA.yaw + (P.CAMERA.half_yaw - P.CAMERA.yaw) * math.sin(math.pi * p)
    dom.presentation_style_set_property(el, "transform", "translateX(" ++ A.n(p * 50.0) ++ "%)"
        ++ (if (popup) " rotateY(" ++ A.n(yaw) ++ "deg) rotateX(" ++ A.n(P.CAMERA.pitch) ++ "deg)" else ""))
    let folded = P.fold_pose(p)
    for (plane in dom.query_selector_all(el, ".popup-live .popup-plane")) {
        dom.presentation_style_set_property(plane, "transform", "translateZ(" ++ A.n(read_number(plane, "data-lift") * folded.lift)
            ++ "px) rotateX(" ++ A.n(folded.angle) ++ "deg)")
        dom.presentation_style_set_property(plane, "opacity", string(folded.opacity))
    }
    updates.attr(owner, "data-progress", p)
    return null
}
pn schedule(owner) {
    updates.attr(owner, "data-frame", dom.request_frame(owner, "greetingturnframe"))
    return null
}
pn begin(owner, pose, wall_ms) {
    let from = read_number(owner, "data-progress")
    let to = progress(pose)
    cancel_turn(owner)
    if (pose == "back" or from == to or dom.computed_style(owner, "--greetings-turn-motion") == "0") { return settle(owner, pose) }
    updates.attr(owner, "data-target", pose)
    updates.attr(owner, "data-from", from)
    updates.attr(owner, "data-to", to)
    updates.attr(owner, "data-at", wall_ms)
    updates.attr(owner, "data-duration", TURN_MS * max(.25, abs(to - from)))
    updates.attr(owner, "data-turning", "true")
    updates.attr(book(owner), "data-turning", "true")
    paint(owner, from)
    schedule(owner)
    return null
}
pub pn frame(owner, wall_ms) {
    if (dom.get_attribute(owner, "data-turning") != "true" or book(owner) == null) { return null }
    // Use document time directly: a delayed frame must not stretch a short turn indefinitely.
    let elapsed = max(0.0, wall_ms - read_number(owner, "data-at", wall_ms))
    let u = min(1.0, elapsed / read_number(owner, "data-duration", TURN_MS))
    if (u >= 1.0) { return settle(owner, dom.get_attribute(owner, "data-target")) }
    let eased = 1.0 - math.pow(1.0 - u, 3.0)
    paint(owner, read_number(owner, "data-from") + (read_number(owner, "data-to") - read_number(owner, "data-from")) * eased)
    schedule(owner)
    return null
}
pub pn close(owner) {
    cancel_turn(owner)
    updates.markup(owner, "")
    return null
}
pub pn follow(owner, before, after, wall_ms) {
    if (after.preview == null) { return close(owner) }
    if (before.preview == null or before.card != after.card or before.vp != after.vp or book(owner) == null) {
        cancel_turn(owner)
        updates.markup(owner, format([ui.preview(after)], 'html'))
        settle(owner, after.preview)
        return null
    }
    updates.attr(dom.query_selector(owner, ".preview"), "class", "preview" ++ (if (after.motion_paused) " effects-paused" else ""))
    updates.markup(dom.query_selector(owner, ".preview-effects-host"), format([ui.preview_effects(after)], 'html'))
    updates.markup(dom.query_selector(owner, ".preview-bar-host"), format([ui.preview_bar(after)], 'html'))
    if (before.preview != after.preview) {
        updates.attr(book(owner), "class", "book " ++ after.preview ++ (if (P.of(after.card).design != "none") " popup-book" else ""))
        begin(owner, after.preview, wall_ms)
    }
    return null
}
