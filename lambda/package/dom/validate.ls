// Constraint validation for form controls (HTML 4.10.21.3), in Lambda.
//
// This is the half of validation that is spec bookkeeping rather than engine
// mechanism: read the control's value and its constraint attributes, decide
// validity, write :valid/:invalid. The value buffer, the UTF-16 IDL offsets and
// the restyle all stay native.
//
// It covers ground the native v1 pass never did — minlength, maxlength, min,
// max and step (RAD_19 known-issue 6). `pattern` is still not covered: Lambda
// exposes no regex system function today, so there is nothing to compile the
// pattern with (ESO29). Native had the same gap for the same reason.
import radiant

// --- helpers ---------------------------------------------------------------

// An attribute that is absent reads as null; treat blank as absent too.
fn attr_or_null(elem, name) {
    let raw = radiant.attr(elem, name);
    if (raw == null or raw == "") null else raw
}

fn as_number(text) {
    if (text == null) null
    else {
        let n = float(text);
        // float() yields an error value for non-numeric text; keep it null so
        // callers can treat "not a number" and "absent" the same way
        if (type(n) == 'error') null else n
    }
}

// A number input's value must parse; an empty value is checked by `required`.
pub fn value_is_number(text) {
    // float() accepts a leading numeric prefix ("12abc" parses as 12), so the
    // whole string has to be checked, not just whether a parse succeeds
    if (as_number(text) == null) { false }
    else {
        all(for (i in 0 to len(text) - 1) numeric_char(slice(text, i, i + 1), i))
    }
}

// Digits anywhere; sign only in the leading position; one dot or exponent mark
// is accepted here and rejected by the float() parse above if malformed.
fn numeric_char(c, i) {
    let o = ord(c);
    (o >= 48 and o <= 57) or c == "." or c == "e" or c == "E" or
        ((c == "-" or c == "+") and i == 0)
}

// Structural checks, deliberately the same approximation the native pass used:
// a full grammar belongs in a regex engine, not in hand-rolled string tests.
pub fn value_is_email(text) {
    let at = index_of(text, "@");
    // index_of yields null when the needle is absent, and null propagates
    // through comparisons — so decide the "no @" case before comparing
    if (at == null or at < 1) { false }
    else {
        let tail = slice(text, at, len(text));
        let dot = index_of(tail, ".");
        last_index_of(text, "@") == at and dot != null and dot > 1
    }
}

pub fn value_is_url(text) {
    starts_with(text, "http://") or starts_with(text, "https://")
}

// --- the constraint pass ---------------------------------------------------

// Returns true when every constraint the control declares is satisfied.
pub fn is_valid(elem, text, input_type) {
    // A custom validity message set through the IDL overrides everything.
    if (radiant.custom_validity(elem) != "") { false }
    else if (radiant.get_state(elem, "required") and len(text) == 0) { false }
    else if (len(text) == 0) {
        // an empty, non-required control is valid regardless of the rest
        true
    }
    else {
        let minlen = as_number(attr_or_null(elem, "minlength"));
        let maxlen = as_number(attr_or_null(elem, "maxlength"));
        let min_v = as_number(attr_or_null(elem, "min"));
        let max_v = as_number(attr_or_null(elem, "max"));
        let step_v = as_number(attr_or_null(elem, "step"));
        let num = as_number(text);

        if (minlen != null and len(text) < minlen) { false }
        else if (maxlen != null and len(text) > maxlen) { false }
        else if (input_type == "number" and not value_is_number(text)) { false }
        else if (input_type == "email" and not value_is_email(text)) { false }
        else if (input_type == "url" and not value_is_url(text)) { false }
        else if (min_v != null and num != null and num < min_v) { false }
        else if (max_v != null and num != null and num > max_v) { false }
        else if (step_v != null and num != null and step_v > 0 and ((num - (if (min_v != null) min_v else 0)) % step_v) != 0) { false }
        else { true }
    }
}

// Recompute and publish :valid/:invalid for one control.
pub pn revalidate(elem) {
    // Reflect the declared constraints into pseudo-state first, exactly as the
    // native pass did: :required/:optional are complements, and :read-only
    // covers disabled too. Claiming validation means owning all of this, not
    // just the :valid/:invalid pair.
    let required = radiant.get_state(elem, "required");
    radiant.set_state(elem, "optional", not required);
    radiant.set_state(elem, "readonly",
        radiant.get_state(elem, "readonly") or radiant.get_state(elem, "disabled"));

    let text = radiant.get_state(elem, "value");
    let raw_type = radiant.attr(elem, "type");
    let input_type = if (raw_type == null) "text" else lower(raw_type);
    let ok = is_valid(elem, text, input_type);
    radiant.set_state(elem, "valid", ok)
    radiant.set_state(elem, "invalid", not ok)
}
