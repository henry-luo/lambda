// Per-document package state for UA editing (D7.2.5, S9.1.7). Native code
// stores only this opaque immutable value's GC root; policy stays in Lambda.
import dom

fn initial() {
    {
        schema: 1,
        settings: {
            style_with_css: false,
            default_paragraph_separator: "div"
        },
        typing_state: null,
        composition: null,
        history: { undo: [], redo: [], open_group: null },
        next_group: 1,
        active_group: null,
        dispatch_depth: 0,
        generation: 0
    }
}

// Allocation happens only on an executing edit path. Query callers receive a
// live context without creating package state or changing observable document
// behavior.
pub pn ensure(host) {
    let current = dom.edit_session(host);
    if (current != null) current
    else {
        let created = initial();
        dom.set_edit_session(host, created)
    }
}

pub fn read(host) {
    dom.edit_session(host)
}

pub fn style_with_css(host) {
    let current = read(host);
    if (current == null) false else current.settings.style_with_css
}

pub fn default_paragraph_separator(host) {
    let current = read(host);
    if (current == null) "div" else current.settings.default_paragraph_separator
}

pub fn typing_format_tag(host) {
    let current = read(host);
    if (current == null or current.typing_state == null) null
    else current.typing_state.format_tag
}

pub fn typing_format(host) {
    let current = read(host);
    if (current == null) null else current.typing_state
}

pub pn set_style_with_css(host, enabled) {
    let current = ensure(host);
    if (current == null) null
    else if (current.settings.style_with_css == enabled) current
    else dom.set_edit_session(host, {
        *: current,
        settings: { *: current.settings, style_with_css: enabled },
        generation: current.generation + 1
    })
}

pub pn set_default_paragraph_separator(host, tag) {
    let current = ensure(host);
    if (current == null) null
    else if (current.settings.default_paragraph_separator == tag) current
    else dom.set_edit_session(host, {
        *: current,
        settings: { *: current.settings, default_paragraph_separator: tag },
        generation: current.generation + 1
    })
}

// Collapsed inline commands update package typing state. The next ordinary
// insertion planner consumes this single mark; it is not native editor state.
pub pn toggle_typing_format(host, tag) {
    let current = ensure(host);
    if (current == null) null
    else {
        let next = if (current.typing_state != null and
                       current.typing_state.format_tag == tag) null
                   else { format_tag: tag };
        dom.set_edit_session(host, {
            *: current,
            typing_state: next,
            generation: current.generation + 1
        })
    }
}

// Formatting descriptors become a single immutable future-typing mark.  The
// package—not native editor state—keeps the wrapper and attribute/style choice
// that the next ordinary insert plan must apply (D7.2.5).
pub pn set_typing_format(host, descriptor, use_css) {
    let current = ensure(host);
    if (current == null) null
    else {
        let css = use_css and descriptor.css_property != null;
        let tag = if (css) "span" else descriptor.format_tag;
        let value = if (css) descriptor.css_value
                    else descriptor.format_value;
        let next = {
            format_tag: tag,
            attribute_name: if (css) "style" else descriptor.attribute_name,
            attribute_value: if (css) descriptor.css_property ++ ":" ++ value ++ ";"
                             else value
        };
        let unchanged = current.typing_state != null and
                        current.typing_state.format_tag == next.format_tag and
                        current.typing_state.attribute_name == next.attribute_name and
                        current.typing_state.attribute_value == next.attribute_value;
        dom.set_edit_session(host, {
            *: current,
            typing_state: if (unchanged) null else next,
            generation: current.generation + 1
        })
    }
}

pub pn clear_typing_format(host) {
    let current = ensure(host);
    if (current == null) null
    else if (current.typing_state == null) current
    else dom.set_edit_session(host, {
        *: current,
        typing_state: null,
        generation: current.generation + 1
    })
}

// History is an opaque package value to native code. This single write keeps
// every retained-delta update document-owned and transaction-reversible.
pub pn set_history(host, history) {
    let current = ensure(host);
    if (current == null) null
    else dom.set_edit_session(host, {
        *: current,
        history: history,
        generation: current.generation + 1
    })
}

// Replaying history restores the package-visible typing state alongside the
// opaque stack move. Native code roots this whole value but never reads it
// (D7.2.5, D5.3.3).
pub pn set_history_and_typing(host, history, typing_state) {
    let current = ensure(host);
    if (current == null) null
    else dom.set_edit_session(host, {
        *: current,
        history: history,
        typing_state: typing_state,
        generation: current.generation + 1
    })
}
