// latex/symbols.ls — LaTeX command to Unicode/HTML entity mappings

// ============================================================
// Diacritics: \'{e} → é, \"{o} → ö, etc.
// ============================================================

// map diacritic command name → combining Unicode character
let DIACRITICS = {
    "'":  "\u0301",   // acute accent
    "`":  "\u0300",   // grave accent
    "^":  "\u0302",   // circumflex
    "\"": "\u0308",   // diaeresis/umlaut
    "~":  "\u0303",   // tilde
    "=":  "\u0304",   // macron
    ".":  "\u0307",   // dot above
    "u":  "\u0306",   // breve
    "v":  "\u030C",   // caron/háček
    "H":  "\u030B",   // double acute
    "c":  "\u0327",   // cedilla
    "d":  "\u0323",   // dot below
    "b":  "\u0331",   // macron below
    "t":  "\u0361",   // tie
    "k":  "\u0328"    // ogonek
}

// common pre-composed diacritic results
let DIACRITIC_COMPOSED = {
    "acute_a": "\u00E1", "acute_e": "\u00E9", "acute_i": "\u00ED",
    "acute_o": "\u00F3", "acute_u": "\u00FA", "acute_y": "\u00FD",
    "acute_A": "\u00C1", "acute_E": "\u00C9", "acute_I": "\u00CD",
    "acute_O": "\u00D3", "acute_U": "\u00DA",
    "grave_a": "\u00E0", "grave_e": "\u00E8", "grave_i": "\u00EC",
    "grave_o": "\u00F2", "grave_u": "\u00F9",
    "grave_A": "\u00C0", "grave_E": "\u00C8", "grave_I": "\u00CC",
    "grave_O": "\u00D2", "grave_U": "\u00D9",
    "circ_a": "\u00E2", "circ_e": "\u00EA", "circ_i": "\u00EE",
    "circ_o": "\u00F4", "circ_u": "\u00FB",
    "circ_A": "\u00C2", "circ_E": "\u00CA", "circ_I": "\u00CE",
    "circ_O": "\u00D4", "circ_U": "\u00DB",
    "uml_a": "\u00E4", "uml_e": "\u00EB", "uml_i": "\u00EF",
    "uml_o": "\u00F6", "uml_u": "\u00FC", "uml_y": "\u00FF",
    "uml_A": "\u00C4", "uml_E": "\u00CB", "uml_I": "\u00CF",
    "uml_O": "\u00D6", "uml_U": "\u00DC",
    "tilde_a": "\u00E3", "tilde_n": "\u00F1", "tilde_o": "\u00F5",
    "tilde_A": "\u00C3", "tilde_N": "\u00D1", "tilde_O": "\u00D5",
    "caron_c": "\u010D", "caron_s": "\u0161", "caron_z": "\u017E",
    "caron_C": "\u010C", "caron_S": "\u0160", "caron_Z": "\u017D",
    "cedilla_c": "\u00E7", "cedilla_C": "\u00C7"
}

// resolve a diacritic command to a character
pub fn resolve_diacritic(cmd, base_char) {
    // try pre-composed lookup first
    let key_prefix = match cmd {
        case "'":  "acute_"
        case "`":  "grave_"
        case "^":  "circ_"
        case "\"": "uml_"
        case "~":  "tilde_"
        case "v":  "caron_"
        case "c":  "cedilla_"
        default:   null
    }
    let composed = if (key_prefix != null) DIACRITIC_COMPOSED[key_prefix ++ base_char] else null
    if (composed != null) { composed }
    else {
        // fallback: base char + combining diacritic
        let combining = DIACRITICS[cmd]
        if (combining != null) base_char ++ combining
        else base_char
    }
}

// ============================================================
// Special characters: \# → #, \& → &, etc.
// ============================================================

let SPECIAL_CHARS = {
    "#": "#",
    "$": "$",
    "%": "%",
    "&": "&",
    "_": "_",
    "{": "{",
    "}": "}",
    "~": "\u00A0",
    "\\": "\n",
    "textbackslash": "\\",
    "textasciitilde": "~",
    "textasciicircum": "^",
    "textbar": "|",
    "textless": "<",
    "textgreater": ">",
    "textunderscore": "_",
    "textemdash": "\u2014",
    "textendash": "\u2013",
    "textquoteleft": "\u2018",
    "textquoteright": "\u2019",
    "textquotedblleft": "\u201C",
    "textquotedblright": "\u201D",
    "ldots": "\u2026",
    "dots": "\u2026",
    "textellipsis": "\u2026",
    "LaTeX": "LaTeX",
    "TeX": "TeX",
    "dag": "\u2020",
    "ddag": "\u2021",
    "S": "\u00A7",
    "P": "\u00B6",
    "copyright": "\u00A9",
    "pounds": "\u00A3",
    "ss": "\u00DF",
    "o": "\u00F8",
    "O": "\u00D8",
    "l": "\u0142",
    "L": "\u0141",
    "i": "\u0131",
    "j": "\u0237",
    "aa": "\u00E5",
    "AA": "\u00C5",
    "ae": "\u00E6",
    "AE": "\u00C6",
    "oe": "\u0153",
    "OE": "\u0152"
}

pub fn resolve_special(cmd_name) {
    SPECIAL_CHARS[cmd_name]
}

// ============================================================
// Ligatures
// ============================================================

let LIGATURES = {
    "---": "\u2014",     // em dash
    "--":  "\u2013",     // en dash
    "``": "\u201C",      // left double quote
    "''": "\u201D",      // right double quote
    "<<": "\u00AB",      // left guillemet
    ">>": "\u00BB"       // right guillemet
}

pub fn resolve_ligature(text) {
    let result = LIGATURES[text]
    if (result != null) result
    else text
}

// ============================================================
// Font size commands
// ============================================================

let FONT_SIZES = {
    "tiny":         "0.5em",
    "scriptsize":   "0.6em",
    "footnotesize": "0.7em",
    "small":        "0.85em",
    "normalsize":   "1.0em",
    "large":        "1.1em",
    "Large":        "1.3em",
    "LARGE":        "1.5em",
    "huge":         "1.8em",
    "Huge":         "2.2em"
}

pub fn get_font_size(cmd_name) {
    FONT_SIZES[cmd_name]
}
