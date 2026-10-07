// The cached rendered tree keeps reactive state writes from regenerating scenes.
// Frame handlers patch live wrappers only (S12.1.3, D4.5.1v4).
import dom
import c: .common
import html: .html
import player: .player
import sampler: .sample
import transitions: .transitions
import morph: .morph
import presenter: .presenter

fn instance(options) => c.as_text(c.value(options.instance, "slide"))
fn event_owner(target, options) {
    let owner = dom.closest(target, ".slide-player")
    if (owner != null and dom.get_attribute(owner, "id") == instance(options)) owner else null
}

fn tree(plan, options) {
    let key = instance(options);
    // focus eligibility reads HTML attributes as text, including tabindex.
    <div class: "slide-player", id: key, tabindex: "0", role: "region", ["aria-label"]: plan.title,
        style: if (c.value(options.fit_viewport, false)) "" else "width:" ++ c.px(c.value(options.width, plan.width)) ++ ";",
        *[<div class: "slide-workspace", *[
            <div class: "slide-audience", *[html.stage(plan, {slide: 0, cue: -1, time_ms: 0.0}, options),
                <div class: "slide-blackout", ["data-slide-tool"]: "blackout", ["aria-hidden"]: "true">,
                <div class: "slide-pointer", ["aria-hidden"]: "true">,
                presenter.overview_tree(plan)]>, presenter.console_tree(key)]>,
        <button class: "slide-toolbar-toggle", type: "button", ["data-slide-tool"]: "toolbar", ["aria-label"]: "Toggle toolbar (H)", "Toolbar">,
        <div class: "slide-controls",
            *[<button type: "button", ["data-slide-command"]: "previous", "Previous">,
            <button type: "button", ["data-slide-command"]: "play", "Play">,
            <button type: "button", ["data-slide-command"]: "pause", "Pause">,
            <button type: "button", ["data-slide-command"]: "resume", "Resume">,
            <button type: "button", ["data-slide-command"]: "next", "Next">,
            <button type: "button", ["data-slide-command"]: "restart", "Restart">,
            <label class: "slide-speed", ["for"]: key ++ "-speed",
                <span "Speed">
                <input id: key ++ "-speed", class: "slide-speed-input", type: "range",
                    min: "0.5", max: "4", step: "0.25", value: "1", ["aria-label"]: "Playback speed">
                <span class: "slide-speed-value", "1×">
            >,
            <span class: "slide-status", ["aria-live"]: "polite", "1 / " ++ string(len(plan.slides))>,
            *presenter.toolbar(plan, key)]
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
pn ensure_layer(owner, plan, index, key, parent = null, final = false) {
    if (dom.get_element_by_id(owner, layer_id(key, index)) != null) { return null }
    let scene = plan.slides[index]
    let canvas = if (parent != null) parent else dom.query_selector(owner, ".slide-canvas")
    let fragment = dom.parse_fragment(canvas,
        format([html.layer(plan, scene, if (final) final_visuals(scene)^ else sampler.scene(scene, -1, 0.0)^, key)], 'html'))
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

pn present(owner, plan, before, ps, options, painted, tools) {
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
    if (before.slide != ps.slide) {
        sync_navigation(owner, plan, before.slide, ps.slide)
        if (tools.console) sync_console(owner, plan, ps.slide, key)^
    }
    if (before.playback_rate != ps.playback_rate)
        set_text(dom.query_selector(owner, ".slide-speed-value"), c.fmt(ps.playback_rate) ++ "×")
    // retain only the last successful ordinary sample; Morph has separate placement writes.
    return if (is_morph) null else visuals
}

pn commit_event(owner, plan, before, event, options, token, painted, tools) {
    if (token > 0) { dom.cancel_frame(owner, token) }
    let result = player.reduce(plan, before, event) ^ { {ps: {*: before, paused: true}, problem: ^.message} } ~
        { {ps: ~, problem: null} }
    // procedure handlers publish through locals; their statement result is null (S7.6.7v4).
    var displayed = {problem: null, painted: null}
    present(owner, plan, before, result.ps, options, painted, tools) ^ { displayed = {problem: ^.message, painted: null} } ~
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

pn show(node, visible, display = "block", interactive = true) {
    dom.style_set_property(node, "display", if (visible) display else "none")
    dom.set_attribute(node, "aria-hidden", if (visible) "false" else "true")
    if (visible and interactive) dom.remove_attribute(node, "inert") else dom.set_attribute(node, "inert", "")
}

pn sync_navigation(owner, plan, previous, index) {
    let picker = dom.query_selector(owner, ".slide-picker")
    dom.set_attribute(picker, "value", string(index + 1))
    dom.set_state(picker, "value", string(index + 1))
    for (i in [previous, index]) {
        let item = dom.query_selector(owner, "[data-slide-destination='" ++ string(i + 1) ++ "']")
        dom.set_attribute(item, "aria-current", if (i == index) "true" else "false")
    }
}

// notes and preview layers are mounted once on first use, independently of audience IDs.
pn sync_console(owner, plan, index, key) {
    let notes = dom.query_selector(owner, ".slide-notes")
    let note_id = key ++ "-notes-s" ++ string(index)
    if (dom.get_element_by_id(owner, note_id) == null) {
        let scene = plan.slides[index]
        let contents = [for (note in scene.notes) for (child in content(note)) child]
        let fragment = dom.parse_fragment(notes, format([<section id: note_id,
            *[<h4 presenter.title(scene)>, *(if (len(contents) > 0) contents else [<p "No speaker notes.">]) ]>], 'html'))
        for (node in dom.child_nodes(fragment)) dom.append_child(notes, node)
    }
    for (node in dom.child_nodes(notes)) show(node, dom.get_attribute(node, "id") == note_id)
    let canvas = dom.query_selector(owner, ".slide-preview-canvas")
    let next = index + 1
    if (next < len(plan.slides)) ensure_layer(owner, plan, next, key ++ "-preview", canvas, true)^
    for (node in dom.child_nodes(canvas)) show(node, dom.get_attribute(node, "id") == layer_id(key ++ "-preview", next))
    show(dom.query_selector(owner, ".slide-preview-stage"), next < len(plan.slides), "block", false)
    show(dom.query_selector(owner, ".slide-preview-end"), next >= len(plan.slides))
    set_text(dom.query_selector(owner, ".slide-preview-title"),
        if (next < len(plan.slides)) "Next: " ++ string(next + 1) ++ ". " ++ presenter.title(plan.slides[next]) else "Next slide")
}

pn update_timer(owner, tools, now_ms) {
    let node = dom.query_selector(owner, ".slide-timer")
    let text = presenter.clock(tools, now_ms)
    if (dom.text_content(node) != text) set_text(node, text)
}

pn tool_event(owner, plan, ps, tools, command, now_ms, options, clock_token) {
    if (clock_token > 0) dom.cancel_frame(owner, clock_token)
    let next = presenter.reduce(tools, command, now_ms)^
    for (name in ["console", "overview", "toolbar", "blackout", "pointer"])
        dom.set_attribute(owner, "data-slide-" ++ name, string(next[name]))
    show(dom.query_selector(owner, ".slide-console"), next.console)
    show(dom.query_selector(owner, ".slide-overview"), next.overview)
    show(dom.query_selector(owner, ".slide-controls"), next.toolbar, "flex")
    show(dom.query_selector(owner, ".slide-blackout"), next.blackout)
    let canvas = dom.query_selector(owner, ".slide-canvas")
    if (next.blackout or next.overview) {
        dom.set_attribute(canvas, "inert", "")
        dom.set_attribute(canvas, "aria-hidden", "true")
    } else {
        dom.remove_attribute(canvas, "inert")
        dom.remove_attribute(canvas, "aria-hidden")
    }
    // a laser appears only over the audience canvas, never over speaker controls.
    show(dom.query_selector(owner, ".slide-pointer"), false)
    for (name in ["console", "overview", "blackout", "pointer"])
        for (button in dom.query_selector_all(owner, "button[data-slide-tool='" ++ name ++ "']"))
            dom.set_attribute(button, "aria-pressed", string(next[name]))
    dom.set_attribute(owner, "data-slide-timer-running", string(next.running))
    set_text(dom.query_selector(owner, "[data-slide-tool=timer-toggle]"), if (next.running) "Pause timer" else "Start timer")
    update_timer(owner, next, now_ms)
    if (next.console) sync_console(owner, plan, ps.slide, instance(options))^
    // geometry reads use the previous committed layout; measure restored controls next frame.
    if (next.toolbar != tools.toolbar or next.console != tools.console)
        dom.request_frame(owner, "slide_fit")
    dom.focus_set(owner, false)
    return {tools: next, token: if (next.console and next.running) dom.request_frame(owner, "slide_presenter_frame") else 0}
}

// one session value publishes playback and presenter state together after each action.
pn action(owner, plan, session, command, value, now_ms, options) {
    if (contains(["console", "overview", "toolbar", "blackout", "pointer", "escape", "timer-toggle", "timer-reset"], command)) {
        let changed = tool_event(owner, plan, session.ps, session.tools, command, now_ms, options, session.clock_token)^;
        return {*: session, tools: changed.tools, clock_token: changed.token}
    }
    var tools = session.tools
    var clock_token = session.clock_token
    var event = {command: command, time_ms: now_ms}
    if (command == "go") {
        let picked = presenter.destination(plan, value) ^ { {problem: ^.message, index: null} } ~ { {problem: null, index: ~} }
        if (picked.problem != null) {
            set_text(dom.query_selector(owner, ".slide-status"), picked.problem)
            return session
        }
        event = {command: 'jump', slide: picked.index, time_ms: now_ms}
        if (tools.overview) {
            let changed = tool_event(owner, plan, session.ps, tools, "overview", now_ms, options, clock_token)^;
            tools = changed.tools
            clock_token = changed.token
        }
    }
    let next = commit_event(owner, plan, session.ps, event, options, session.token, session.painted, tools)
    if (command == "go") dom.focus_set(owner, false)
    return {*: next, tools: tools, clock_token: clock_token}
}

view <slide_player> state session: {ps: player.initial_state(~.plan, ~.options), token: 0, painted: null,
    tools: presenter.initial_state(), clock_token: 0} {
    ~.rendered
}
on slide_fit(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    fit_player(evt.target, ~.plan, ~.options)
    return 'handled'
}
on slide_frame(evt) {
    if (evt.detail != session.token or evt.event_phase != 2) { return 'pass' }
    let next = commit_event(evt.target, ~.plan, session.ps, {command: 'frame', time_ms: evt.time_stamp},
        ~.options, 0, session.painted, session.tools)
    session = {*: session, *: next}
    return 'handled'
}
on slide_presenter_frame(evt) {
    if (evt.detail != session.clock_token or evt.event_phase != 2) { return 'pass' }
    update_timer(evt.target, session.tools, evt.time_stamp)
    session = {*: session, clock_token: if (session.tools.console and session.tools.running)
        dom.request_frame(evt.target, "slide_presenter_frame") else 0}
    return 'handled'
}
on slide_activate(evt) {
    if (session.ps.generation != 0 or evt.event_phase != 2) { return 'pass' }
    session = action(evt.target, ~.plan, session, "activate", null, evt.time_stamp, ~.options)^
    return 'handled'
}
on input(evt) {
    let owner = event_owner(evt.target, ~.options)
    if (owner == null or not dom.matches(evt.target, ".slide-speed-input")) { return 'pass' }
    let next = commit_event(owner, ~.plan, session.ps,
        {command: 'speed', rate: dom.range_value(evt.target), time_ms: evt.time_stamp},
        ~.options, session.token, session.painted, session.tools)
    session = {*: session, *: next}
    return 'handled'
}
on click(evt) {
    let owner = event_owner(evt.target, ~.options)
    if (owner == null) { return 'pass' }
    let tool = dom.closest(evt.target, "[data-slide-tool]")
    let button = dom.closest(evt.target, "[data-slide-command]")
    let interactive = dom.closest(evt.target, "a, input, button, select, textarea, [contenteditable], [role=button], [role=link]")
    if (tool == null and button == null and (interactive != null or dom.closest(evt.target, ".slide-stage") == null)) { return 'pass' }
    let command = if (tool != null) dom.get_attribute(tool, "data-slide-tool")
        else if (button != null) dom.get_attribute(button, "data-slide-command") else "next"
    let value = if (tool != null and dom.has_attribute(tool, "data-slide-destination")) dom.get_attribute(tool, "data-slide-destination")
        else dom.get_state(dom.query_selector(owner, ".slide-picker"), "value")
    session = action(owner, ~.plan, session, command, value, evt.time_stamp, ~.options)^
    return 'handled'
}
on keydown(evt) {
    let owner = event_owner(evt.target, ~.options)
    if (owner == null or evt.ctrlKey or evt.altKey or evt.metaKey or evt.shiftKey) { return 'pass' }
    if (evt.key == "Enter" and dom.matches(evt.target, ".slide-picker")) {
        session = action(owner, ~.plan, session, "go", dom.get_state(evt.target, "value"), evt.time_stamp, ~.options)^
        return 'prevent-default'
    }
    let tool = if (lower(evt.key) == "p") "console" else if (lower(evt.key) == "o") "overview"
        else if (lower(evt.key) == "h") "toolbar" else if (lower(evt.key) == "b" or evt.key == ".") "blackout"
        else if (lower(evt.key) == "l") "pointer" else if (evt.key == "Escape") "escape" else null
    let editing = dom.closest(evt.target, "input, select, textarea, [contenteditable]") != null
    if (tool != null and (not editing or evt.key == "Escape")) {
        session = action(owner, ~.plan, session, tool, null, evt.time_stamp, ~.options)^
        return 'prevent-default'
    }
    if (not dom.same_node(evt.target, owner)) { return 'pass' }
    let cmd = if (evt.key == "ArrowRight" or evt.key == " " or evt.key == "PageDown") "next"
        else if (evt.key == "ArrowLeft" or evt.key == "PageUp") "previous"
        else if (evt.key == "Home") "home" else if (evt.key == "End") "end" else null
    if (cmd == null) { return 'pass' }
    session = action(owner, ~.plan, session, cmd, null, evt.time_stamp, ~.options)^
    return 'prevent-default'
}
on mousemove(evt) {
    if (not session.tools.pointer or session.tools.blackout or session.tools.overview) { return 'pass' }
    let owner = event_owner(evt.target, ~.options)
    if (owner == null) { return 'pass' }
    track_pointer(owner, evt)^
    return 'handled'
}
on mouseout(evt) {
    if (not session.tools.pointer or session.tools.blackout or session.tools.overview) { return 'pass' }
    let owner = event_owner(evt.target, ~.options)
    if (owner == null) { return 'pass' }
    track_pointer(owner, evt)^
    return 'pass'
}

pn track_pointer(owner, evt) {
    let box = dom.bounding_box(dom.query_selector(owner, ".slide-stage"))
    let inside = evt.x >= box.left and evt.y >= box.top and evt.x < box.left + box.width and evt.y < box.top + box.height
    let dot = dom.query_selector(owner, ".slide-pointer")
    dom.style_set_property(dot, "display", if (inside) "block" else "none")
    if (inside) set_style(dot, "transform", "translate(" ++ c.px(evt.x - box.left - 7.0) ++ "," ++ c.px(evt.y - box.top - 7.0) ++ ")")^
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
    fit_document(evt.target, ~.plan, ~.options, true)
    return 'handled'
}

pn fit_document(target, plan, options, after_layout = false) {
    let owner = dom.get_element_by_id(target, instance(options))
    if (owner != null) {
        // toolbar wrapping also changes during a resize's layout commit.
        if (after_layout) dom.request_frame(owner, "slide_fit")
        else fit_player(owner, plan, {*: options, fit_viewport: true})
    }
}

pn fit_player(owner, plan, options) {
    let viewport = dom.viewport_size(owner)
    if (viewport == null) { return null }
    let standalone = c.value(options.fit_viewport, false)
    let controls = dom.bounding_box(dom.query_selector(owner, ".slide-controls"))
    let toolbar = dom.get_attribute(owner, "data-slide-toolbar") != "false"
    let total_width = c.value(options.width, if (standalone) viewport.width else plan.width)
    let height = c.value(options.height, if (standalone) max(1.0, viewport.height - (if (toolbar) c.value(controls.height, 0.0) else 0.0)) else plan.height)
    let console_open = dom.get_attribute(owner, "data-slide-console") == "true"
    // narrow embeds stack the console so notes and timer retain readable bounds.
    let stacked = total_width < 640.0
    let panel_width = if (stacked) total_width else min(360.0, total_width * 0.4)
    let panel_height = if (stacked) max(320.0, height) else height
    let width = max(1.0, total_width - if (console_open and not stacked) panel_width else 0.0)
    let workspace = dom.query_selector(owner, ".slide-workspace")
    dom.style_set_property(workspace, "flex-direction", if (stacked) "column" else "row")
    dom.style_set_property(workspace, "width", c.px(total_width))
    dom.style_set_property(workspace, "height", c.px(height + if (stacked and console_open) panel_height else 0.0))
    let audience = dom.query_selector(owner, ".slide-audience")
    dom.style_set_property(audience, "width", c.px(width))
    dom.style_set_property(audience, "height", c.px(height))
    let fit = html.fit(plan, width, height)
    let stage = dom.query_selector(owner, ".slide-stage")
    dom.style_set_property(stage, "width", c.px(width))
    dom.style_set_property(stage, "height", c.px(height))
    dom.style_set_property(dom.query_selector(owner, ".slide-canvas"), "transform", fit.transform)
    let console = dom.query_selector(owner, ".slide-console")
    dom.style_set_property(console, "width", c.px(panel_width))
    dom.style_set_property(console, "height", c.px(panel_height))
    let preview_width = max(1.0, panel_width - 32.0)
    let preview_height = preview_width * plan.height / plan.width
    let preview = dom.query_selector(owner, ".slide-preview-stage")
    dom.style_set_property(preview, "width", c.px(preview_width))
    dom.style_set_property(preview, "height", c.px(preview_height))
    let canvas = dom.query_selector(owner, ".slide-preview-canvas")
    dom.style_set_property(canvas, "width", c.px(plan.width))
    dom.style_set_property(canvas, "height", c.px(plan.height))
    dom.style_set_property(canvas, "transform", html.fit(plan, preview_width, preview_height).transform)
}

pub fn document_tree(plan, options = {}) => apply(<slide_document plan: plan, options: options,
    rendered: html.document(plan, player_tree(plan, {*: options, fit_viewport: true}))>)
