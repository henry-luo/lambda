// Package-owned designMode canonicalization and host transition policy
// (D7.2.5). Native retains only generic document Boolean storage.
import dom

fn canonical_enabled(value) {
    lower(trim(if (value == null) "" else string(value))) == "on"
}

pub pn set_mode(host, value) {
    let enabled = canonical_enabled(value);
    if (not dom.set_design_mode(enabled)) false
    else {
        // Deactivating a document host cannot leave it as the keyboard-input
        // fallback. The engine only clears the identified host; policy stays
        // in this mode transition (D7.2.5).
        if (enabled) true else dom.clear_editing_focus(host)
    }
}
