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
    amalg: "⨿", land: "∧", lor: "∨"
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
    "in": "∈", ni: "∋", notin: "∉",
    vdash: "⊢", dashv: "⊣", models: "⊨",
    mid: "∣", parallel: "∥", perp: "⊥",
    propto: "∝", asymp: "≍", bowtie: "⋈",
    ll: "≪", gg: "≫", doteq: "≐",
    trianglelefteq: "⊴", trianglerighteq: "⊵"
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
    to: "→", gets: "←", implies: "⟹", iff: "⟺"
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
    checkmark: "✓", maltese: "✠",
    degree: "°", copyright: "©",
    dots: "…", ldots: "…", cdots: "⋯",
    vdots: "⋮", ddots: "⋱",
    colon: ":", vert: "|", Vert: "‖",
    langle: "⟨", rangle: "⟩",
    lceil: "⌈", rceil: "⌉", lfloor: "⌊", rfloor: "⌋",
    lbrace: "{", rbrace: "}",
    lvert: "|", rvert: "|", lVert: "‖", rVert: "‖"
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
    "inf": "inf", ker: "ker", lg: "lg",
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
    big: 1.2, Big: 1.8, bigg: 2.4, Bigg: 3.0
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

// look up an accent character by key
pub fn get_accent(name) {
    accents[name]
}

// look up a delimiter size by name
pub fn get_delim_size(name) {
    delim_sizes[name]
}
