// math/symbols.ls — LaTeX command → Unicode character mapping tables
// Used to convert \alpha, \infty, etc. to their Unicode equivalents

// ============================================================
// Greek letters (lowercase)
// ============================================================

let greek_lower = {
    alpha: "α", beta: "β", gamma: "γ", delta: "δ",
    epsilon: "ϵ", varepsilon: "ε", zeta: "ζ", eta: "η",
    theta: "θ", vartheta: "ϑ", iota: "ι", kappa: "κ",
    lambda: "λ", mu: "μ", nu: "ν", xi: "ξ",
    omicron: "ο", pi: "π", varpi: "ϖ", rho: "ρ",
    varrho: "ϱ", sigma: "σ", varsigma: "ς", tau: "τ",
    upsilon: "υ", phi: "ϕ", varphi: "φ", chi: "χ",
    psi: "ψ", omega: "ω"
}

// ============================================================
// Greek letters (uppercase)
// ============================================================

let greek_upper = {
    Gamma: "Γ", Delta: "Δ", Theta: "Θ", Lambda: "Λ",
    Xi: "Ξ", Pi: "Π", Sigma: "Σ", Upsilon: "Υ",
    Phi: "Φ", Psi: "Ψ", Omega: "Ω"
}

// ============================================================
// Binary operators
// ============================================================

let bin_operators = {
    pm: "±", mp: "∓", times: "×", div: "÷",
    cdot: "⋅", ast: "∗", star: "⋆", circ: "∘",
    bullet: "∙", oplus: "⊕", ominus: "⊖", otimes: "⊗",
    oslash: "⊘", odot: "⊙", dagger: "†", ddagger: "‡",
    cap: "∩", cup: "∪", sqcap: "⊓", sqcup: "⊔",
    vee: "∨", wedge: "∧", setminus: "∖", wr: "≀",
    amalg: "⨿", land: "∧", lor: "∨",
    // AMS binary operators (boxed family)
    boxplus: "⊞", boxtimes: "⊠", boxminus: "⊟", boxdot: "⊡"
}

// ============================================================
// Relations
// ============================================================

let relations = {
    leq: "≤", le: "≤", geq: "≥", ge: "≥",
    neq: "≠", ne: "≠", equiv: "≡", prec: "≺",
    succ: "≻", sim: "∼", simeq: "≃", approx: "≈",
    cong: "≅", subset: "⊂", supset: "⊃", subseteq: "⊆",
    supseteq: "⊇", sqsubseteq: "⊑", sqsupseteq: "⊒",
    'in': "∈", ni: "∋", notin: "∉",
    vdash: "⊢", dashv: "⊣", models: "⊨",
    mid: "∣", parallel: "∥", perp: "⊥",
    propto: "∝", asymp: "≍", bowtie: "⋈",
    ll: "≪", gg: "≫", doteq: "≐",
    trianglelefteq: "⊴", trianglerighteq: "⊵",
    // AMS relations
    preceq: "⪯", succeq: "⪰", nmid: "∤",
    nleq: "≰", ngeq: "≱"
}

// ============================================================
// Arrows
// ============================================================

let arrows = {
    leftarrow: "←", rightarrow: "→", uparrow: "↑", downarrow: "↓",
    leftrightarrow: "↔", updownarrow: "↕",
    Leftarrow: "⇐", Rightarrow: "⇒", Uparrow: "⇑", Downarrow: "⇓",
    Leftrightarrow: "⇔", Updownarrow: "⇕",
    longleftarrow: "⟵", longrightarrow: "⟶", longleftrightarrow: "⟷",
    Longleftarrow: "⟸", Longrightarrow: "⟹", Longleftrightarrow: "⟺",
    hookrightarrow: "↪", hookleftarrow: "↩",
    mapsto: "↦", longmapsto: "⟼",
    nearrow: "↗", nwarrow: "↖", searrow: "↘", swarrow: "↙",
    to: "→", gets: "←", implies: "⟹", iff: "⟺",
    // AMS arrows
    twoheadleftarrow: "↞", twoheadrightarrow: "↠",
    leftleftarrows: "⇇", rightrightarrows: "⇉",
    leftrightarrows: "⇆", rightleftarrows: "⇄",
    upuparrows: "⇈", downdownarrows: "⇊",
    Lleftarrow: "⇚", Rrightarrow: "⇛",
    looparrowleft: "↫", looparrowright: "↬",
    curvearrowleft: "↶", curvearrowright: "↷",
    leftharpoonup: "↼", rightharpoonup: "⇀",
    leftharpoondown: "↽", rightharpoondown: "⇁",
    rightsquigarrow: "⇝", leftrightsquigarrow: "↭"
}

// ============================================================
// Miscellaneous symbols
// ============================================================

let misc_symbols = {
    infty: "∞", nabla: "∇", partial: "∂",
    forall: "∀", exists: "∃", nexists: "∄",
    emptyset: "∅", varnothing: "∅",
    neg: "¬", lnot: "¬", surd: "√",
    top: "⊤", bot: "⊥", angle: "∠",
    triangle: "△", backslash: "∖",
    ell: "ℓ", wp: "℘", Re: "ℜ", Im: "ℑ",
    aleph: "ℵ", beth: "ℶ", gimel: "ℷ",
    hbar: "ℏ", imath: "ı", jmath: "ȷ",
    prime: "′", dprime: "″",
    flat: "♭", natural: "♮", sharp: "♯",
    clubsuit: "♣", diamondsuit: "♢",
    heartsuit: "♡", spadesuit: "♠",
    varheartsuit: "♥",
    blacktriangle: "▲", blacksquare: "■",
    checkmark: "✓", maltese: "✠",
    degree: "°", copyright: "©",
    dots: "…", ldots: "…", cdots: "⋯",
    vdots: "⋮", ddots: "⋱",
    colon: ":", vert: "∣", Vert: "∥",
    langle: "⟨", rangle: "⟩",
    lceil: "⌈", rceil: "⌉", lfloor: "⌊", rfloor: "⌋",
    lbrace: "{", rbrace: "}",
    lvert: "∣", rvert: "∣", lVert: "∥", rVert: "∥"
}

// ============================================================
// Big operators (also used as symbols in text mode)
// ============================================================

let big_operators = {
    sum: "∑", prod: "∏", coprod: "∐",
    int: "∫", iint: "∬", iiint: "∭", oint: "∮",
    bigcup: "⋃", bigcap: "⋂", bigsqcup: "⊔",
    bigvee: "⋁", bigwedge: "⋀",
    bigoplus: "⨁", bigotimes: "⨂", bigodot: "⨀",
    biguplus: "⨄"
}

// ============================================================
// Accent commands → combining character or description
// ============================================================

let accents = {
    hat: "̂", widehat: "̂",
    tilde: "̃", widetilde: "̃",
    bar: "̄", overline: "‾",
    vec: "⃗", dot: "̇", ddot: "̈",
    acute: "́", grave: "̀", breve: "̆",
    check: "̌", mathring: "̊"
}

// ============================================================
// Text-mode operator names (rendered upright)
// ============================================================

let operator_names = {
    arccos: "arccos", arcsin: "arcsin", arctan: "arctan",
    arg: "arg", cos: "cos", cosh: "cosh",
    cot: "cot", coth: "coth", csc: "csc",
    deg: "deg", det: "det", dim: "dim",
    exp: "exp", gcd: "gcd", hom: "hom",
    'inf': "inf", ker: "ker", lg: "lg",
    lim: "lim", liminf: "lim inf", limsup: "lim sup",
    ln: "ln", log: "log", max: "max",
    min: "min", Pr: "Pr", sec: "sec",
    sin: "sin", sinh: "sinh", sup: "sup",
    tan: "tan", tanh: "tanh"
}

// ============================================================
// Delimiter sizing commands → scale factor
// ============================================================

let delim_sizes = {
    big: 1.2, bigl: 1.2, bigr: 1.2, bigm: 1.2,
    Big: 1.8, Bigl: 1.8, Bigr: 1.8, Bigm: 1.8,
    bigg: 2.4, biggl: 2.4, biggr: 2.4, biggm: 2.4,
    Bigg: 3.0, Biggl: 3.0, Biggr: 3.0, Biggm: 3.0
}

// ============================================================
// Lookup function: command → Unicode
// ============================================================

pub fn lookup_symbol(cmd) {
    // strip leading backslash if present
    let name = if (len(cmd) > 0 and slice(cmd, 0, 1) == "\\") slice(cmd, 1, len(cmd)) else cmd

    greek_lower[name] or greek_upper[name] or
    bin_operators[name] or relations[name] or
    arrows[name] or misc_symbols[name] or
    big_operators[name] or null
}

// classify a symbol command into atom type
pub fn classify_symbol(cmd) {
    let name = if (len(cmd) > 0 and slice(cmd, 0, 1) == "\\") slice(cmd, 1, len(cmd)) else cmd

    if (bin_operators[name]) "mbin"
    else if (relations[name]) "mrel"
    else if (arrows[name]) "mrel"
    else if (big_operators[name]) "mop"
    else if (operator_names[name]) "mop"
    else "mord"
}

// look up an operator name by key
pub fn get_operator_name(name) {
    operator_names[name]
}

// check if a command name is a limit-style operator (limits above/below in display)
pub fn is_limit_op(cmd) {
    let name = if (len(cmd) > 0 and slice(cmd, 0, 1) == "\\") slice(cmd, 1, len(cmd)) else cmd
    big_operators[name] != null or
    name == "lim" or name == "limsup" or name == "liminf" or
    name == "sup" or name == "inf" or
    name == "min" or name == "max" or
    name == "det" or name == "gcd" or name == "Pr"
}

// look up an accent character by key
pub fn get_accent(name) {
    accents[name]
}

// ============================================================
// Font-variant table — command name → class string
//
// MathLive selects an HTML class per symbol (lm_cmr upright, lm_mathit italic,
// lm_ams). Lambda previously hard-coded ~12 entries inline; this table is the
// canonical source. Lookup falls back to the rendering context's font when a
// command is absent.
// ============================================================

let font_class_map = {
    // -- lowercase Greek: italic with lcGreek marker class --
    alpha: "lcGreek lm_mathit", beta: "lcGreek lm_mathit",
    gamma: "lcGreek lm_mathit", delta: "lcGreek lm_mathit",
    epsilon: "lcGreek lm_mathit", varepsilon: "lcGreek lm_mathit",
    zeta: "lcGreek lm_mathit", eta: "lcGreek lm_mathit",
    theta: "lcGreek lm_mathit", vartheta: "lcGreek lm_mathit",
    iota: "lcGreek lm_mathit", kappa: "lcGreek lm_mathit",
    lambda: "lcGreek lm_mathit", mu: "lcGreek lm_mathit",
    nu: "lcGreek lm_mathit", xi: "lcGreek lm_mathit",
    omicron: "lcGreek lm_mathit", pi: "lcGreek lm_mathit",
    varpi: "lcGreek lm_mathit", rho: "lcGreek lm_mathit",
    varrho: "lcGreek lm_mathit", sigma: "lcGreek lm_mathit",
    varsigma: "lcGreek lm_mathit", tau: "lcGreek lm_mathit",
    upsilon: "lcGreek lm_mathit", phi: "lcGreek lm_mathit",
    varphi: "lcGreek lm_mathit", chi: "lcGreek lm_mathit",
    psi: "lcGreek lm_mathit", omega: "lcGreek lm_mathit",

    // -- uppercase Greek: upright (CMR) --
    Gamma: "lm_cmr", Delta: "lm_cmr", Theta: "lm_cmr", Lambda: "lm_cmr",
    Xi: "lm_cmr", Pi: "lm_cmr", Sigma: "lm_cmr", Upsilon: "lm_cmr",
    Phi: "lm_cmr", Psi: "lm_cmr", Omega: "lm_cmr",

    // -- upright math symbols (CMR) --
    infty: "lm_cmr", nabla: "lm_cmr", partial: "lm_cmr",
    mid: "lm_cmr", parallel: "lm_cmr", perp: "lm_cmr",
    ell: "lm_cmr", hbar: "lm_cmr",
    imath: "lm_cmr", jmath: "lm_cmr", aleph: "lm_cmr",
    cdot: "lm_cmr", times: "lm_cmr", div: "lm_cmr",
    vert: "lm_cmr", Vert: "lm_cmr",
    lvert: "lm_cmr", rvert: "lm_cmr", lVert: "lm_cmr", rVert: "lm_cmr",
    dots: "lm_cmr", ldots: "lm_cmr", cdots: "lm_cmr",
    vdots: "lm_cmr", ddots: "lm_cmr",
    forall: "lm_cmr", exists: "lm_cmr",
    angle: "lm_cmr",
    // Relations rendered upright in MathLive
    approx: "lm_cmr", sim: "lm_cmr", simeq: "lm_cmr", cong: "lm_cmr",
    propto: "lm_cmr", asymp: "lm_cmr", equiv: "lm_cmr",
    leq: "lm_cmr", le: "lm_cmr", geq: "lm_cmr", ge: "lm_cmr",
    neq: "lm_cmr", ne: "lm_cmr",
    subset: "lm_cmr", supset: "lm_cmr",
    subseteq: "lm_cmr", supseteq: "lm_cmr",
    'in': "lm_cmr", ni: "lm_cmr", notin: "lm_cmr",
    odot: "lm_cmr", oplus: "lm_cmr", otimes: "lm_cmr",
    // Arrows rendered upright in MathLive
    rightarrow: "lm_cmr", leftarrow: "lm_cmr",
    Rightarrow: "lm_cmr", Leftarrow: "lm_cmr",
    leftrightarrow: "lm_cmr", Leftrightarrow: "lm_cmr",
    longrightarrow: "lm_cmr", longleftarrow: "lm_cmr",
    Longrightarrow: "lm_cmr", Longleftarrow: "lm_cmr",
    Longleftrightarrow: "lm_cmr", longleftrightarrow: "lm_cmr",
    to: "lm_cmr", gets: "lm_cmr", implies: "lm_cmr", iff: "lm_cmr",
    hookrightarrow: "lm_cmr", hookleftarrow: "lm_cmr",
    mapsto: "lm_cmr", longmapsto: "lm_cmr",
    uparrow: "lm_cmr", downarrow: "lm_cmr", updownarrow: "lm_cmr",
    Uparrow: "lm_cmr", Downarrow: "lm_cmr", Updownarrow: "lm_cmr",
    // AMS triangles
    triangle: "lm_ams",
    // More CMR symbols
    cup: "lm_cmr", cap: "lm_cmr", setminus: "lm_cmr",
    pm: "lm_cmr", mp: "lm_cmr",
    // More AMS symbols
    emptyset: "lm_ams", varnothing: "lm_ams",
    beth: "lm_ams", gimel: "lm_ams", daleth: "lm_ams",
    neg: "lm_ams", lnot: "lm_ams",

    // -- AMS symbols --
    blacksquare: "lm_ams", blacktriangle: "lm_ams",
    twoheadleftarrow: "lm_ams", twoheadrightarrow: "lm_ams",
    boxplus: "lm_ams", boxtimes: "lm_ams",
    boxminus: "lm_ams", boxdot: "lm_ams",
    preceq: "lm_ams", succeq: "lm_ams",
    nmid: "lm_ams", rightsquigarrow: "lm_ams",
    leftleftarrows: "lm_ams", rightrightarrows: "lm_ams",
    leftrightarrows: "lm_ams", rightleftarrows: "lm_ams",
    upuparrows: "lm_ams", downdownarrows: "lm_ams",
    leftharpoonup: "lm_ams", rightharpoonup: "lm_ams",
    leftharpoondown: "lm_ams", rightharpoondown: "lm_ams",
    Lleftarrow: "lm_ams", Rrightarrow: "lm_ams",
    looparrowleft: "lm_ams", looparrowright: "lm_ams",
    curvearrowleft: "lm_ams", curvearrowright: "lm_ams"
}

// Look up the font class for a symbol command. Returns null if the command
// is not in the table — caller should fall back to the context's font.
pub fn font_class_of(name) {
    font_class_map[name]
}

// look up a delimiter size by name
pub fn get_delim_size(name) {
    delim_sizes[name]
}
