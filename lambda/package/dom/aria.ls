// ARIA reflection for form controls (F7, ES18).
//
// A pure declarative mapping from engine state to aria-* attributes. It was
// native (te_aria_reflect), but nothing in it is mechanism — it reads state and
// writes attributes, and the one interesting rule in it is policy that reads
// better spelled out than buried in C.
//
// Timing is the reason this moved. Native called the reflection from the
// *value-mutation* points, which run before the validator produces a verdict,
// so aria-invalid trailed the truth by one edit (ESO37). Here it is called from
// the same handlers as revalidate and after it, so the verdict it mirrors is
// the one just computed.
import radiant

// Write only on an actual change. This matters more here than it did natively:
// native poked the attribute table directly, while a write from the package
// goes through the DOM operation path and carries mutation notices and a
// repaint with it. reflect() runs on every keystroke, so writing unconditionally
// would mean four DOM mutations per character — and it showed up immediately as
// an extra repaint rect in the state dump.
fn set_if_changed(elem, name, want) {
    if (radiant.attr(elem, name) != want) { radiant.set_attr(elem, name, want) }
    else { false }
}

// Present-or-absent mirrors: the attribute carries meaning only when true.
fn flag(elem, name, present) {
    set_if_changed(elem, name, if (present) "true" else null)
}

pub pn reflect(elem) {
    flag(elem, "aria-disabled", radiant.get_state(elem, "disabled"))
    flag(elem, "aria-readonly", radiant.get_state(elem, "readonly"))
    flag(elem, "aria-required", radiant.get_state(elem, "required"))

    // aria-invalid is deliberately written "false" rather than removed:
    // assistive technology treats an explicit false as "validation has run and
    // this control is currently OK", which is not the same as saying nothing.
    set_if_changed(elem, "aria-invalid",
        if (radiant.get_state(elem, "invalid")) "true" else "false")
}

// <input type=range> also reports its position. valuenow is the computed value,
// not the normalized fraction the engine stores.
pub pn reflect_range(elem) {
    reflect(elem)
    let v = radiant.range_value(elem);
    if (v != null) {
        set_if_changed(elem, "aria-valuenow", string(v))
        set_if_changed(elem, "aria-valuemin", string(radiant.range_min(elem)))
        set_if_changed(elem, "aria-valuemax", string(radiant.range_max(elem)))
    }
}
