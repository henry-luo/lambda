// math/spacing_table.ls — TeX inter-atom spacing rules
// Based on Appendix G of The TeXBook
//
// Atom types: ord(0), op(1), bin(2), rel(3), open(4), close(5), punct(6), inner(7)
// Spacing values: 0=none, 1=thin(3mu), 2=medium(4mu), 3=thick(5mu)
// Negative values = only in display/text style (not script/scriptscript)

// The 8×8 spacing table.
// Row = left atom type, Column = right atom type.
// -1 = thin (display/text only), -2 = medium (display/text only), -3 = thick (display/text only).
// 99 = impossible combination (should not occur).

// Spacing values in em (mu = 1/18 em)
let THIN_SPACE = 0.17          // MathLive snapshot value for 3mu
let MEDIUM_SPACE = 0.23        // MathLive snapshot value for 4mu
let THICK_SPACE = 0.28         // MathLive snapshot value for 5mu

// atom type name → index
pub fn atom_type_index(atom_type) {
    if (atom_type == "mord") 0
    else if (atom_type == "mop") 1
    else if (atom_type == "mbin") 2
    else if (atom_type == "mrel") 3
    else if (atom_type == "mopen") 4
    else if (atom_type == "mclose") 5
    else if (atom_type == "mpunct") 6
    else if (atom_type == "minner") 7
    else 0
}

fn spacing_code_for_indices(li, ri) {
    // Long single-process corpus runs exposed module-level nested array state
    // going stale after prior renders; keep this immutable TeX table as branch
    // logic so each lookup is derived from scalar indices only.
    if (li == 0) spacing_code_ord(ri)
    else if (li == 1) spacing_code_op(ri)
    else if (li == 2) spacing_code_bin(ri)
    else if (li == 3) spacing_code_rel(ri)
    else if (li == 4) spacing_code_open(ri)
    else if (li == 5) spacing_code_close(ri)
    else if (li == 6) spacing_code_punct(ri)
    else spacing_code_inner(ri)
}

fn spacing_code_ord(ri) {
    if (ri == 1) 1
    else if (ri == 2) -2
    else if (ri == 3) -3
    else if (ri == 7) -1
    else 0
}

fn spacing_code_op(ri) {
    if (ri == 0 or ri == 1 or ri == 7) 1
    else if (ri == 2) 99
    else if (ri == 3) -3
    else 0
}

fn spacing_code_bin(ri) {
    if (ri == 2 or ri == 3 or ri == 5 or ri == 6) 99
    else -2
}

fn spacing_code_rel(ri) {
    if (ri == 3 or ri == 5 or ri == 6) 0
    else -3
}

fn spacing_code_open(ri) {
    if (ri == 2) 99
    else 0
}

fn spacing_code_close(ri) {
    if (ri == 1) 1
    else if (ri == 2) -2
    else if (ri == 3) -3
    else if (ri == 7) -1
    else 0
}

fn spacing_code_punct(ri) {
    if (ri == 2) 99
    else -1
}

fn spacing_code_inner(ri) {
    if (ri == 1) 1
    else if (ri == 2) -2
    else if (ri == 3) -3
    else if (ri == 5) 0
    else -1
}

// Get spacing between two atom types
// Returns spacing in em, or 0.0 if no spacing needed
// style: "display" | "text" | "script" | "scriptscript"
pub fn get_spacing(left_type, right_type, style) {
    let li = atom_type_index(left_type)
    let ri = atom_type_index(right_type)
    let code = spacing_code_for_indices(li, ri)

    if (code == 99) 0.0          // impossible combination
    else if (code == 0) 0.0      // no space
    else if (code > 0)
        (if (code == 1) THIN_SPACE
         else if (code == 2) MEDIUM_SPACE
         else if (code == 3) THICK_SPACE
         else 0.0)
    else
        // conditional on style (display/text only)
        if (style == "script" or style == "scriptscript") 0.0
        else
            (let abs_code = 0 - code,
             if (abs_code == 1) THIN_SPACE
             else if (abs_code == 2) MEDIUM_SPACE
             else if (abs_code == 3) THICK_SPACE
             else 0.0)
}

// CSS class for a given spacing value
pub fn spacing_class(spacing_em) {
    if (spacing_em == 0.0) null
    else if (spacing_em < 0.0) "lm_negativethinspace"
    else if (spacing_em <= THIN_SPACE) "lm_thinspace"
    else if (spacing_em <= MEDIUM_SPACE) "lm_mediumspace"
    else if (spacing_em <= THICK_SPACE) "lm_thickspace"
    else null
}
