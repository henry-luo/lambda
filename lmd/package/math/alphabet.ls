// Font-independent mathematical alphabets shared by SVG and MathML (D7.2.8).
// Unicode mathematical alphabets describe semantic variants, not a font's cmap.
let alphabets = {
    italic: [0x1D434, 0x1D44E, 0], bold: [0x1D400, 0x1D41A, 0x1D7CE],
    bolditalic: [0x1D468, 0x1D482, 0], script: [0x1D49C, 0x1D4B6, 0],
    fraktur: [0x1D504, 0x1D51E, 0], double: [0x1D538, 0x1D552, 0x1D7D8],
    sans: [0x1D5A0, 0x1D5BA, 0x1D7E2], sansitalic:[0x1D608,0x1D622,0], mono: [0x1D670, 0x1D68A, 0x1D7F6]
}
// Unicode preserves these alphabet members in the Letterlike Symbols block.
let exceptions = {
    italic: {h: 0x210E},
    script: {B: 0x212C, E: 0x2130, F: 0x2131, H: 0x210B, I: 0x2110,
        L: 0x2112, M: 0x2133, R: 0x211B, e: 0x212F, g: 0x210A, o: 0x2134},
    fraktur: {C: 0x212D, H: 0x210C, I: 0x2111, R: 0x211C, Z: 0x2128},
    double: {C: 0x2102, H: 0x210D, N: 0x2115, P: 0x2119, Q: 0x211A, R: 0x211D, Z: 0x2124}
}

pub fn effective_style(ch, style) => if (style == "auto")
    (if (ord(ch) >= 0x391 and ord(ch) <= 0x3A9) "normal" else "italic") else style

pub fn variant(ch, style) {
    let cp = ord(ch)
    let actual = effective_style(ch, style)
    let alphabet = alphabets[if (actual == "cal") "script" else actual]
    let special = exceptions[if (actual == "cal") "script" else actual][ch]
    let greek = if (actual == "italic") 0x1D6E2 else if (actual == "bold") 0x1D6A8
        else if (actual == "bolditalic") 0x1D71C else null
    let greek_special = index_of([0x2207, 0x2202, 0x3F5, 0x3D1, 0x3F0, 0x3D5, 0x3F1, 0x3D6], cp)
    if (special != null) special
    else if (alphabet != null and cp >= 65 and cp <= 90) alphabet[0] + cp - 65
    else if (alphabet != null and cp >= 97 and cp <= 122) alphabet[1] + cp - 97
    else if (alphabet != null and alphabet[2] > 0 and cp >= 48 and cp <= 57) alphabet[2] + cp - 48
    else if (greek != null and cp >= 0x391 and cp <= 0x3A9) greek + cp - 0x391
    else if (greek != null and cp >= 0x3B1 and cp <= 0x3C9) greek + 26 + cp - 0x3B1
    else if (greek != null and cp == 0x3F4) greek + 17
    else if (greek != null and greek_special != null) greek + (if (greek_special == 0) 25 else 50 + greek_special)
    else cp
}

// Share command spellings between rendering and style-face acquisition.
let styled_commands = [
    {cmd:"\\rm", style:"normal"}, {cmd:"\\it", style:"italic"}, {cmd:"\\bf", style:"bold"},
    {cmd:"\\sf", style:"sans"}, {cmd:"\\tt", style:"mono"},
    {cmd: "\\mathrm", style: "normal"},
    {cmd: "\\mathbf", style: "bold"},
    {cmd: "\\boldsymbol", style: "bolditalic"},
    {cmd: "\\mathit", style: "italic"},
    {cmd: "\\mathcal", style: "cal"},
    {cmd: "\\mathscr", style: "script"},
    {cmd: "\\mathfrak", style: "fraktur"},
    {cmd: "\\mathbb", style: "double"},
    {cmd: "\\mathsf", style: "sans"},
    {cmd: "\\mathsfit", style: "sansitalic"},
    {cmd: "\\mathtt", style: "mono"},
    {cmd: "\\operatorname", style: "normal"},
    {cmd:"\\textbf",style:"bold"}, {cmd:"\\textit",style:"italic"}, {cmd:"\\emph",style:"italic"},
    {cmd:"\\textsf",style:"sans"}, {cmd:"\\texttt",style:"mono"}
]

pub fn command_variant(cmd) => [for (entry in styled_commands where entry.cmd == cmd) entry.style][0]

// Ordinary bundled faces encode alphabet variants as styled base characters.
pub fn decode(ch) {
    let cp=ord(ch)
    if (cp<0x2100 or cp>0x1D7FF) { {character:ch,style:null} }
    else {
    let special=[for (style, entries in exceptions, base, value in entries where value==cp)
        {character:string(base),style:string(style)}][0]
    let latin=[for (style, starts in alphabets, i, start in starts
        where start>0 and cp>=start and cp<start+(if (i==2) 10 else 26))
        {character:chr(cp-start+[65,97,48][i]),style:string(style)}][0]
    let greek=if (cp<0x1D6A8) null else [for (style in ["italic","bold","bolditalic"],
        base in [*[for (i in 0x391 to 0x3A9) chr(i)],*[for (i in 0x3B1 to 0x3C9) chr(i)],"ϴ","∇","∂","ϵ","ϑ","ϰ","ϕ","ϱ","ϖ"]
        where variant(base,style)==cp and ord(base)!=cp) {character:base,style:style}][0];
    special or latin or greek or {character:ch,style:null}
    }
}
