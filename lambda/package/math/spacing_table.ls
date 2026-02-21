// math/spacing_table.ls — TeX inter-atom spacing rules
// Based on Appendix G of The TeXBook
//
// Atom types: ord(0), op(1), bin(2), rel(3), open(4), close(5), punct(6), inner(7)
// Spacing values: 0=none, 1=thin(3mu), 2=medium(4mu), 3=thick(5mu)
// Negative values = only in display/text style (not script/scriptscript)

// The 8×8 spacing table
// Row = left atom type, Column = right atom type
// -1 = thin (display/text only), -2 = medium (display/text only), -3 = thick (display/text only)
// 99 = impossible combination (should not occur)

let SPACING_TABLE = [
    //          ord  op  bin  rel  open close punct inner
    /* ord   */ [0,   1,  -2,  -3,  0,   0,    0,   -1],
    /* op    */ [1,   1,   99, -3,  0,   0,    0,    1],
    /* bin   */ [-2, -2,   99,  99, -2,  99,   99,  -2],
    /* rel   */ [-3, -3,   99,  0,  -3,  0,    0,   -3],
    /* open  */ [0,   0,   99,  0,   0,  0,    0,    0],
    /* close */ [0,   1,  -2,  -3,  0,   0,    0,   -1],
    /* punct */ [-1, -1,   99, -1,  -1, -1,   -1,   -1],
    /* inner */ [-1,  1,  -2,  -3,  -1,  0,   -1,   -1]
]

// Spacing values in em (mu = 1/18 em)
let THIN_SPACE = 0.16667       // 3mu = 3/18 em
let MEDIUM_SPACE = 0.22222     // 4mu = 4/18 em
let THICK_SPACE = 0.27778      // 5mu = 5/18 em

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

// Get spacing between two atom types
// Returns spacing in em, or 0.0 if no spacing needed
// style: "display" | "text" | "script" | "scriptscript"
pub fn get_spacing(left_type, right_type, style) {
    let li = atom_type_index(left_type)
    let ri = atom_type_index(right_type)
    let code = SPACING_TABLE[li][ri]

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
    else if (spacing_em < 0.0) "ML__negativethinspace"
    else if (spacing_em <= THIN_SPACE) "ML__thinspace"
    else if (spacing_em <= MEDIUM_SPACE) "ML__mediumspace"
    else if (spacing_em <= THICK_SPACE) "ML__thickspace"
    else null
}
