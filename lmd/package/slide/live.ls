// The cached rendered tree keeps reactive state writes from regenerating scenes.
// Frame handlers patch live wrappers only (S12.1.3, D4.5.1v4).
import dom
import c: .common
import html: .html
import player: .player
import sampler: .sample
import transitions: .transitions
import morph: .morph

fn instance(options) => c.as_text(c.value(options.instance, "slide"))

fn tree(plan, options) {
    let key = instance(options);
    <div class: "slide-player", id: key, tabindex: 0, role: "region", ["aria-label"]: plan.title,
        *[html.stage(plan, {slide: 0, cue: -1, time_ms: 0.0}, options),
        <div class: "slide-controls",
            <button type: "button", ["data-slide-command"]: "previous", "Previous">
            <button type: "button", ["data-slide-command"]: "play", "Play">
            <button type: "button", ["data-slide-command"]: "pause", "Pause">
            <button type: "button", ["data-slide-command"]: "resume", "Resume">
            <button type: "button", ["data-slide-command"]: "next", "Next">
            <button type: "button", ["data-slide-command"]: "restart", "Restart">
            <label class: "slide-speed", ["for"]: key ++ "-speed",
                <span "Speed">
                <input id: key ++ "-speed", class: "slide-speed-input", type: "range",
                    min: "0.5", max: "4", step: "0.25", value: "1", ["aria-label"]: "Playback speed">
                <span class: "slide-speed-value", "1×">
            >
            <span class: "slide-status", ["aria-live"]: "polite", "1 / " ++ string(len(plan.slides))>
        >]
    >
}

pn patch_layer(owner, id, visual, previous = null) {
    if (visual == previous) { return null }
    let node = dom.get_element_by_id(owner, id)
    if (node != null) {
        if (previous == null or visual.opacity != previous.opacity)
            set_style(node, "opacity", c.fmt(visual.opacity))^
        if (previous == null or visual.tx != previous.tx or visual.ty != previous.ty)
            set_style(node, "transform", "translate(" ++ c.px(visual.tx) ++ "," ++ c.px(visual.ty) ++ ")")^
    }
}

// Control text is state: rewrite its text node rather than the element's child
// list, which stages a new Mark child array on every replacement.
pn set_text(node, text) {
    let child = dom.first_child(node)
    if (child != null and dom.node_type(child) == 3) dom.set_node_value(child, text)
    else dom.set_text_content(node, text)
}

pn set_style(node, property, value) bool^ {
    if (not dom.presentation_style_set_property(node, property, value)) {
        raise c.fail("player.style", "host rejected " ++ property)
    }
    return true
}

pn patch_visual(owner, v, key, previous = null) {
        if (v == previous) { return null }
        let node = dom.get_element_by_id(owner, key ++ "-" ++ v.dom_key)
        if (node != null) {
            for (property, value in html.effect_properties(v, previous))
                set_style(node, string(property), value)^
            let hidden = v.visible < 0.5 or v.opacity <= 0.0
            let was_hidden = previous != null and (previous.visible < 0.5 or previous.opacity <= 0.0)
            if (previous == null or hidden != was_hidden) {
                dom.set_attribute(node, "aria-hidden", if (hidden) "true" else "false")
                if (hidden) dom.set_attribute(node, "inert", "")
                else if (dom.has_attribute(node, "inert")) dom.remove_attribute(node, "inert")
            }
            if (previous == null or previous.paint != v.paint) {
                let text = dom.query_selector(node, ".slide-text")
                let shape_node = dom.get_element_by_id(node, key ++ "-paint-" ++ v.dom_key)
                if (text != null and v.paint != null) set_style(text, "color", v.paint)^
                if (shape_node != null and v.paint != null)
                    set_style(shape_node, if (lower(dom.node_name(shape_node)) == "line") "stroke" else "fill", v.paint)^
            }
        }
}

pn patch_objects(owner, visuals, previous, key) {
    for (i, v in visuals) patch_visual(owner, v, key, previous[i])^
}

pn patch_morph(owner, plan, ps, key) {
    let sampled = morph.sample(plan, ps)
    for (patch in [*sampled.outgoing, *sampled.incoming]) {
        let place = dom.get_element_by_id(owner, key ++ "-place-" ++ patch.geometry.dom_key)
        if (place != null) for (property, value in html.placement_properties(patch.geometry))
            set_style(place, string(property), value)^
        patch_visual(owner, patch.visual, key)^
    }
    set_style(dom.query_selector(owner, ".slide-canvas"), "background", sampled.background)^
    for (index in [ps.outgoing, ps.slide]) {
        set_style(dom.get_element_by_id(owner, key ++ "-s" ++ string(index)), "background", "transparent")^
    }
}

fn layer_id(key, index) => key ++ "-s" ++ string(index)
fn shown_layers(ps) => if (ps.outgoing >= 0) [ps.outgoing, ps.slide] else [ps.slide]
fn final_visuals(scene) array^ => sampler.scene(scene, len(scene.cues) - 1,
    if (len(scene.cues) > 0) scene.cues[len(scene.cues) - 1].duration_ms else 0.0)^

// A slide renders the first time it is needed and stays mounted; later visits
// change state only, never markup (user ruling 2026-10-07).
pn ensure_layer(owner, plan, index, key) {
    if (dom.get_element_by_id(owner, layer_id(key, index)) != null) { return null }
    let scene = plan.slides[index]
    let canvas = dom.query_selector(owner, ".slide-canvas")
    let fragment = dom.parse_fragment(canvas,
        format([html.layer(plan, scene, sampler.scene(scene, -1, 0.0)^, key)], 'html'))
    for (node in dom.child_nodes(fragment)) dom.append_child(canvas, node)
}

// Show the active slide (above any transition partner) and hide every other
// layer the previous state showed. A shown layer gets its authored placement
// and background back, which Morph samples overwrite.
pn show_layers(owner, plan, before, ps, key) {
    let shown = shown_layers(ps)
    for (index in [*shown_layers(before), *shown]) {
        let layer = dom.get_element_by_id(owner, layer_id(key, index))
        let visible = contains(shown, index)
        set_style(layer, "display", if (visible) "block" else "none")^
        set_style(layer, "z-index", if (index == ps.slide) "2" else "1")^
        if (index == ps.slide) dom.set_attribute(layer, "data-slide-active", "true")
        else if (dom.has_attribute(layer, "data-slide-active")) dom.remove_attribute(layer, "data-slide-active")
        if (visible) {
            let scene = plan.slides[index]
            set_style(layer, "background", html.layer_background(plan, scene))^
            for (obj in scene.objects) {
                let place = dom.get_element_by_id(owner, key ++ "-place-" ++ obj.dom_key)
                for (property, value in html.placement_properties(obj)) set_style(place, string(property), value)^
            }
        }
    }
}

pn present(owner, plan, before, ps, options, painted) {
    let key = instance(options)
    let switched = before.slide != ps.slide or before.outgoing != ps.outgoing
    let is_morph = ps.phase == 'transition' and plan.slides[ps.slide].transition == 'morph'
    let visuals = if (switched or not is_morph) player.sample(plan, ps)^ else null
    if (switched) {
        for (index in shown_layers(ps)) ensure_layer(owner, plan, index, key)^
        show_layers(owner, plan, before, ps, key)^
        // a slide leaving through a transition shows its final build
        if (ps.outgoing >= 0) patch_objects(owner, final_visuals(plan.slides[ps.outgoing])^, null, key)^
    }
    if (is_morph) patch_morph(owner, plan, ps, key)^
    else patch_objects(owner, visuals, if (switched) null else painted, key)^
    let layers = transitions.sample(plan, ps)
    let old_layers = if (switched) null else transitions.sample(plan, before)
    patch_layer(owner, key ++ "-s" ++ string(ps.slide), layers.incoming, old_layers.incoming)^
    if (ps.outgoing >= 0) patch_layer(owner, key ++ "-s" ++ string(ps.outgoing), layers.outgoing, old_layers.outgoing)^
    if (switched or (before.phase == 'transition') != (ps.phase == 'transition'))
      for (index in (if (ps.outgoing >= 0) [ps.outgoing, ps.slide] else [ps.slide])) {
        let layer = dom.get_element_by_id(owner, key ++ "-s" ++ string(index))
        set_style(layer, "pointer-events", if (ps.phase == 'transition') "none" else "auto")^
        if (ps.phase == 'transition') { dom.set_attribute(layer, "inert", ""); dom.set_attribute(layer, "aria-hidden", "true") }
        else {
            if (dom.has_attribute(layer, "inert")) dom.remove_attribute(layer, "inert")
            if (dom.has_attribute(layer, "aria-hidden")) dom.remove_attribute(layer, "aria-hidden")
        }
    }
    let diagnostics = {index: ps.slide, cue: ps.cue, phase: c.as_text(ps.phase), paused: ps.paused, autoplay: ps.autoplay, rate: ps.playback_rate}
    let old_diagnostics = {index: before.slide, cue: before.cue, phase: c.as_text(before.phase), paused: before.paused, autoplay: before.autoplay, rate: before.playback_rate}
    for (property, value in diagnostics where value != old_diagnostics[property] or not dom.has_attribute(owner, "data-slide-" ++ string(property)))
        dom.set_attribute(owner, "data-slide-" ++ string(property), string(value))
    // Publish parked/command time without invalidating attribute selectors every frame.
    if (not player.needs_frame(ps) or before.generation != ps.generation or before.paused != ps.paused)
        dom.set_attribute(owner, "data-slide-time", c.fmt(ps.time_ms))
    if (before.slide != ps.slide)
        set_text(dom.query_selector(owner, ".slide-status"), string(ps.slide + 1) ++ " / " ++ string(len(plan.slides)))
    if (before.playback_rate != ps.playback_rate)
        set_text(dom.query_selector(owner, ".slide-speed-value"), c.fmt(ps.playback_rate) ++ "×")
    // retain only the last successful ordinary sample; Morph has separate placement writes.
    return if (is_morph) null else visuals
}

pn commit_event(owner, plan, before, event, options, token, painted) {
    if (token > 0) { dom.cancel_frame(owner, token) }
    let result = player.reduce(plan, before, event) ^ { {ps: {*: before, paused: true}, problem: ^.message} } ~
        { {ps: ~, problem: null} }
    // procedure handlers publish through locals; their statement result is null (S7.6.7v4).
    var displayed = {problem: null, painted: null}
    present(owner, plan, before, result.ps, options, painted) ^ { displayed = {problem: ^.message, painted: null} } ~
        { displayed = {problem: null, painted: ~} }
    let scheduled = if (result.problem == null and displayed.problem == null and player.needs_frame(result.ps)) dom.request_frame(owner, "slide_frame") else 0
    let problem = if (result.problem != null) result.problem else if (displayed.problem != null) displayed.problem
        else if (player.needs_frame(result.ps) and scheduled == 0) "slide: player.frame: host refused frame delivery" else null
    if (problem != null) {
        dom.set_attribute(owner, "data-slide-error", problem)
        dom.set_attribute(owner, "data-slide-paused", "true")
        set_text(dom.query_selector(owner, ".slide-status"), problem)
    } else if (dom.has_attribute(owner, "data-slide-error")) {
        dom.remove_attribute(owner, "data-slide-error")
        set_text(dom.query_selector(owner, ".slide-status"), string(result.ps.slide + 1) ++ " / " ++ string(len(plan.slides)))
    }
    return {ps: if (problem != null) {*: result.ps, paused: true} else result.ps, token: scheduled, painted: if (problem == null) displayed.painted else null}
}

view <slide_player> state ps: player.initial_state(~.plan, ~.options), frame_token: 0, painted: null {
    ~.rendered
}
on slide_frame(evt) {
    if (evt.detail != frame_token or evt.event_phase != 2) { return 'pass' }
    frame_token = 0
    let owner = evt.target
    let next = commit_event(owner, ~.plan, ps, {command: 'frame', time_ms: evt.time_stamp}, ~.options, 0, painted)
    ps = next.ps
    frame_token = next.token
    painted = next.painted
    return 'handled'
}
on slide_activate(evt) {
    if (ps.generation != 0 or evt.event_phase != 2) { return 'pass' }
    let next = commit_event(evt.target, ~.plan, ps, {command: 'activate', time_ms: evt.time_stamp}, ~.options, frame_token, painted)
    ps = next.ps
    frame_token = next.token
    painted = next.painted
    return 'handled'
}
on input(evt) {
    let owner = dom.closest(evt.target, ".slide-player")
    if (owner == null or dom.get_attribute(owner, "id") != instance(~.options) or
        not dom.matches(evt.target, ".slide-speed-input")) { return 'pass' }
    let next = commit_event(owner, ~.plan, ps,
        {command: 'speed', rate: dom.range_value(evt.target), time_ms: evt.time_stamp}, ~.options, frame_token, painted)
    ps = next.ps
    frame_token = next.token
    painted = next.painted
    return 'handled'
}
on click(evt) {
    let owner = dom.closest(evt.target, ".slide-player")
    let button = dom.closest(evt.target, "[data-slide-command]")
    if (owner == null or dom.get_attribute(owner, "id") != instance(~.options)) { return 'pass' }
    let interactive = dom.closest(evt.target, "a, input, button, select, textarea, [contenteditable], [role=button], [role=link]")
    if (button == null and (interactive != null or dom.closest(evt.target, ".slide-stage") == null)) { return 'pass' }
    let command = if (button == null) 'next' else dom.get_attribute(button, "data-slide-command")
    let next = commit_event(owner, ~.plan, ps, {command: command, time_ms: evt.time_stamp}, ~.options, frame_token, painted)
    ps = next.ps
    frame_token = next.token
    painted = next.painted
    return 'handled'
}
on keydown(evt) {
    let owner = dom.closest(evt.target, ".slide-player")
    if (owner == null or not dom.same_node(evt.target, owner) or evt.ctrlKey or evt.altKey or evt.metaKey or evt.shiftKey) { return 'pass' }
    let cmd = if (evt.key == "ArrowRight" or evt.key == " " or evt.key == "PageDown") 'next'
        else if (evt.key == "ArrowLeft" or evt.key == "PageUp") 'previous'
        else if (evt.key == "Home") 'home' else if (evt.key == "End") 'end' else null
    if (cmd == null) { return 'pass' }
    let next = commit_event(owner, ~.plan, ps, {command: cmd, time_ms: evt.time_stamp}, ~.options, frame_token, painted)
    ps = next.ps
    frame_token = next.token
    painted = next.painted
    return 'prevent-default'
}

pub fn player_tree(plan, options = {}) => apply(<slide_player plan: plan, options: options, rendered: tree(plan, options)>)

// The existing body load event runs after layout; it requests a later activation frame.
view <slide_document> {
    ~.rendered
}
on load(evt) {
    fit_document(evt.target, ~.plan, ~.options)
    if (c.value(~.options.autostart, false)) {
        let owner = dom.get_element_by_id(evt.target, instance(~.options))
        if (owner != null) dom.request_frame(owner, "slide_activate")
    }
    return 'handled'
}
on resize(evt) {
    fit_document(evt.target, ~.plan, ~.options)
    return 'handled'
}

pn fit_document(target, plan, options) {
    let owner = dom.get_element_by_id(target, instance(options))
    let viewport = dom.viewport_size(target)
    if (owner == null or viewport == null) { return null }
    let controls = dom.bounding_box(dom.query_selector(owner, ".slide-controls"))
    let width = c.value(options.width, viewport.width)
    let height = c.value(options.height, max(1.0, viewport.height - c.value(controls.height, 0.0)))
    let fit = html.fit(plan, width, height)
    let stage = dom.query_selector(owner, ".slide-stage")
    dom.style_set_property(stage, "width", c.px(width))
    dom.style_set_property(stage, "height", c.px(height))
    dom.style_set_property(dom.query_selector(owner, ".slide-canvas"), "transform", fit.transform)
}
pub fn document_tree(plan, options = {}) => apply(<slide_document plan: plan, options: options,
    rendered: html.document(plan, player_tree(plan, options))>)
