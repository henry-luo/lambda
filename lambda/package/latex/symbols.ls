// latex/symbols.ls — LaTeX command to Unicode/HTML entity mappings

// ============================================================
// Diacritics: \'{e} → é, \"{o} → ö, etc.
// ============================================================

// map diacritic command name → combining Unicode character
let DIACRITICS = {
    "'": "\u0301",   // acute accent
    '`':  "\u0300",   // grave accent
    '^':  "\u0302",   // circumflex
    "\"": "\u0308",   // diaeresis/umlaut
    '~':  "\u0303",   // tilde
    '=':  "\u0304",   // macron
    '.':  "\u0307",   // dot above
    'u':  "\u0306",   // breve
    'v':  "\u030C",   // caron/háček
    'H':  "\u030B",   // double acute
    'c':  "\u0327",   // cedilla
    'd':  "\u0323",   // dot below
    'b':  "\u0331",   // macron below
    't':  "\u0361",   // tie
    'k':  "\u0328"    // ogonek
}

// common pre-composed diacritic results
let DIACRITIC_COMPOSED = {
    'acute_a': "\u00E1", 'acute_e': "\u00E9", 'acute_i': "\u00ED",
    'acute_o': "\u00F3", 'acute_u': "\u00FA", 'acute_y': "\u00FD",
    'acute_A': "\u00C1", 'acute_E': "\u00C9", 'acute_I': "\u00CD",
    'acute_O': "\u00D3", 'acute_U': "\u00DA",
    'grave_a': "\u00E0", 'grave_e': "\u00E8", 'grave_i': "\u00EC",
    'grave_o': "\u00F2", 'grave_u': "\u00F9",
    'grave_A': "\u00C0", 'grave_E': "\u00C8", 'grave_I': "\u00CC",
    'grave_O': "\u00D2", 'grave_U': "\u00D9",
    'circ_a': "\u00E2", 'circ_e': "\u00EA", 'circ_i': "\u00EE",
    'circ_o': "\u00F4", 'circ_u': "\u00FB",
    'circ_A': "\u00C2", 'circ_E': "\u00CA", 'circ_I': "\u00CE",
    'circ_O': "\u00D4", 'circ_U': "\u00DB",
    'uml_a': "\u00E4", 'uml_e': "\u00EB", 'uml_i': "\u00EF",
    'uml_o': "\u00F6", 'uml_u': "\u00FC", 'uml_y': "\u00FF",
    'uml_A': "\u00C4", 'uml_E': "\u00CB", 'uml_I': "\u00CF",
    'uml_O': "\u00D6", 'uml_U': "\u00DC",
    'tilde_a': "\u00E3", 'tilde_n': "\u00F1", 'tilde_o': "\u00F5",
    'tilde_A': "\u00C3", 'tilde_N': "\u00D1", 'tilde_O': "\u00D5",
    'caron_c': "\u010D", 'caron_s': "\u0161", 'caron_z': "\u017E",
    'caron_C': "\u010C", 'caron_S': "\u0160", 'caron_Z': "\u017D",
    'cedilla_c': "\u00E7", 'cedilla_C': "\u00C7"
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
    '#': "#",
    '$': "$",
    '%': "%",
    '&': "&",
    '_': "_",
    '{': "{",
    '}': "}",
    '~': "\u00A0",
    '\\': "\n",
    'textbackslash': "\\",
    'textasciitilde': "~",
    'textasciicircum': "^",
    'textbar': "|",
    'textless': "<",
    'textgreater': ">",
    'textunderscore': "_",
    'textemdash': "\u2014",
    'textendash': "\u2013",
    'textquoteleft': "\u2018",
    'textquoteright': "\u2019",
    'textquotedblleft': "\u201C",
    'textquotedblright': "\u201D",
    'ldots': "\u2026",
    'dots': "\u2026",
    'textellipsis': "\u2026",
    'LaTeX': "LaTeX",
    'TeX': "TeX",
    'dag': "\u2020",
    'ddag': "\u2021",
    'S': "\u00A7",
    'P': "\u00B6",
    'copyright': "\u00A9",
    'pounds': "\u00A3",
    'ss': "\u00DF",
    'o': "\u00F8",
    'O': "\u00D8",
    'l': "\u0142",
    'L': "\u0141",
    'i': "\u0131",
    'j': "\u0237",
    'aa': "\u00E5",
    'AA': "\u00C5",
    'ae': "\u00E6",
    'AE': "\u00C6",
    'oe': "\u0153",
    'OE': "\u0152"
}

pub fn resolve_special(cmd_name) {
    SPECIAL_CHARS[cmd_name]
}

// ============================================================
// Ligatures
// ============================================================

let LIGATURES = {
    '---': "\u2014",     // em dash
    '--':  "\u2013",     // en dash
    '``': "\u201C",      // left double quote
    "''": "\u201D",      // right double quote
    '<<': "\u00AB",      // left guillemet
    '>>': "\u00BB"       // right guillemet
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
    'tiny':         "0.5em",
    'scriptsize':   "0.6em",
    'footnotesize': "0.7em",
    'small':        "0.85em",
    'normalsize':   "1.0em",
    'large':        "1.1em",
    'Large':        "1.3em",
    'LARGE':        "1.5em",
    'huge':         "1.8em",
    'Huge':         "2.2em"
}

pub fn get_font_size(cmd_name) {
    FONT_SIZES[cmd_name]
}

// ============================================================
// Extended symbol commands (textgreek, textcomp, gensymb)
// ============================================================

let EXTENDED_SYMBOLS = {
    // ---- textgreek package: lowercase ----
    'textalpha':   "\u03B1", 'textbeta':    "\u03B2", 'textgamma':   "\u03B3",
    'textdelta':   "\u03B4", 'textepsilon': "\u03B5", 'textzeta':    "\u03B6",
    'texteta':     "\u03B7", 'texttheta':   "\u03B8", 'textiota':    "\u03B9",
    'textkappa':   "\u03BA", 'textlambda':  "\u03BB", 'textmu':      "\u03BC",
    'textnu':      "\u03BD", 'textxi':      "\u03BE", 'textomicron': "\u03BF",
    'textpi':      "\u03C0", 'textrho':     "\u03C1", 'textsigma':   "\u03C3",
    'texttau':     "\u03C4", 'textupsilon': "\u03C5", 'textphi':     "\u03C6",
    'textchi':     "\u03C7", 'textpsi':     "\u03C8", 'textomega':   "\u03C9",

    // ---- textgreek package: uppercase ----
    'textAlpha':   "\u0391", 'textBeta':    "\u0392", 'textGamma':   "\u0393",
    'textDelta':   "\u0394", 'textEpsilon': "\u0395", 'textZeta':    "\u0396",
    'textEta':     "\u0397", 'textTheta':   "\u0398", 'textIota':    "\u0399",
    'textKappa':   "\u039A", 'textLambda':  "\u039B", 'textMu':      "\u039C",
    'textNu':      "\u039D", 'textXi':      "\u039E", 'textOmicron': "\u039F",
    'textPi':      "\u03A0", 'textRho':     "\u03A1", 'textSigma':   "\u03A3",
    'textTau':     "\u03A4", 'textUpsilon': "\u03A5", 'textPhi':     "\u03A6",
    'textChi':     "\u03A7", 'textPsi':     "\u03A8", 'textOmega':   "\u03A9",

    // ---- textcomp: currency ----
    'texteuro':           "\u20AC", 'textcent':        "\u00A2",
    'textsterling':       "\u00A3", 'textbaht':        "\u0E3F",
    'textcolonmonetary':  "\u20A1", 'textcurrency':    "\u00A4",
    'textdong':           "\u20AB", 'textflorin':      "\u0192",
    'textlira':           "\u20A4", 'textnaira':       "\u20A6",
    'textpeso':           "\u20B1", 'textwon':         "\u20A9",
    'textyen':            "\u00A5",

    // ---- textcomp: math/science ----
    'textperthousand':     "\u2030", 'textpertenthousand': "\u2031",
    'textonehalf':         "\u00BD", 'textthreequarters':  "\u00BE",
    'textonequarter':      "\u00BC", 'textfractionsolidus': "\u2044",
    'textdiv':             "\u00F7", 'texttimes':           "\u00D7",
    'textminus':           "\u2212", 'textpm':              "\u00B1",
    'textsurd':            "\u221A", 'textlnot':            "\u00AC",
    'textasteriskcentered':"\u2217",
    'textonesuperior':     "\u00B9", 'texttwosuperior':     "\u00B2",
    'textthreesuperior':   "\u00B3",

    // ---- textcomp: arrows ----
    'textleftarrow':  "\u2190", 'textuparrow':    "\u2191",
    'textrightarrow': "\u2192", 'textdownarrow':  "\u2193",

    // ---- gensymb / misc ----
    'textcelsius':       "\u2103", 'textdegree':        "\u00B0",
    'textohm':           "\u2126", 'textmicro':         "\u00B5",
    'checkmark':         "\u2713", 'textreferencemark':  "\u203B",
    'textordfeminine':   "\u00AA", 'textordmasculine':   "\u00BA",
    'textmarried':       "\u26AD", 'textdivorced':       "\u26AE",
    'textbardbl':        "\u2016", 'textbrokenbar':      "\u00A6",
    'textbigcircle':     "\u25EF", 'textcircledP':       "\u24C5",
    'textregistered':    "\u00AE", 'textservicemark':    "\u2120",
    'texttrademark':     "\u2122", 'textnumero':         "\u2116",
    'textrecipe':        "\u211E", 'textestimated':      "\u212E",
    'textmusicalnote':   "\u266A", 'textdiscount':       "%",
    'textinterrobang':   "\u203D", 'textpercentoldstyle': "%",

    // ---- old-style numerals ----
    'textzerooldstyle':  "0", 'textoneoldstyle':   "1",
    'texttwooldstyle':   "2", 'textthreeoldstyle': "3",
    'textfouroldstyle':  "4", 'textfiveoldstyle':  "5",
    'textsixoldstyle':   "6", 'textsevenoldstyle': "7",
    'texteightoldstyle': "8", 'textnineoldstyle':  "9",

    // ---- additional non-ASCII character commands ----
    'IJ': "\u0132", 'ij': "\u0133",
    'TH': "\u00DE", 'th': "\u00FE",
    'DH': "\u00D0", 'dh': "\u00F0",
    'DJ': "\u0110", 'dj': "\u0111",
    'NG': "\u014A", 'ng': "\u014B"
}

pub fn resolve_extended(cmd_name) {
    EXTENDED_SYMBOLS[cmd_name]
}
