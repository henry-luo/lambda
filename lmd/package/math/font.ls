// Immutable, per-render font facts. Resource reads stay in Lambda IO (D7.1.2v2).
import radiant
import sym: .symbols
import alphabet: .alphabet
import util: .util
import fallback: .fallback
import bundled: .bundled
import tex_metrics: .tex_metrics
import embedded_fonts: lambda.doc.font

pub let UNITS = 1000.0

// Keep the compatibility surface while conversion imports the pure mapping.
pub fn variant(ch, style) => alphabet.variant(ch, style)
pub fn command_variant(cmd) => alphabet.command_variant(cmd)

fn source_items(node) {
    let attrs = ["value", "name", "cmd", "text", "base", "sub", "sup", "numer", "denom",
        "radicand", "index", "arg", "content", "body", "left", "right", "above", "below",
        "label", "over", "under", "delim", "annotation", "upper", "lower", "target", "display", "script", "scriptscript"];
    if (node is array) node
    else if (node is element) [*[for (attr in attrs) node[attr]], *content(node)] else []
}

fn collect(node) {
    if (node is string or node is symbol) {
        let text = string(node)
        let command = if (slice(text, 0, 1) == "\\") slice(text, 1, len(text)) else text
        text ++ (sym.lookup_symbol(text) or "") ++ (sym.get_accent(command) or "") ++
            (if (command == "KaTeX") "KATEX" else "")
    } else util.str_join([for (child in source_items(node)) collect(child)], "") ++
            (if (name(node) == 'verbatim' and node.starred) "␣" else "")
}

fn collect_styles(node) {
    // Literal command spellings inside verbatim/text are not font declarations.
    let style = if (node is element and name(node) == 'verbatim') "mono"
        else if (node is element) command_variant(string(node.cmd or node.name or "")) else null;
    [*if (style != null) [style] else [], *[for (child in source_items(node), request in collect_styles(child)) request]]
}

// Keep the CSS alias and style alongside measured geometry so SVG text selects the same face.
fn text_facts(facts, family) {
    if (facts == null) null
    else {*:facts, glyphs: [for (g in facts.glyphs) if (g == null) null else
        {*:g, text_family: family or g.font_family, text_weight: g.font_weight, text_style: g.font_style,
            text_size: facts.font_size}]}
}

fn measure(style, points, faces, family = null) => text_facts(radiant.math_metrics(style, points, faces), family)

pub fn prepare(ast, options) map | error {
    let use_bundled = options.font_family == null and options.fonts == null
    let family = options.font_family or bundled.FAMILY
    let faces = if (options.fonts != null) options.fonts else if (options.font_family != null) null
        else bundled.faces()^
    let variant_families = if (use_bundled) bundled.VARIANT_FAMILIES else {sans: "sans-serif", mono: "monospace"}
    // One batch owns all glyphs used by this formula; no mutable last-font state.
    let source = collect(ast)
    let closed_composites = if (use_bundled) [for (ch in unique(split(source,""))
        where sym.closed_integral_base(ch) != null) ch] else []
    let components = util.str_join([for (ch in closed_composites) sym.closed_integral_base(ch) ++ "◯"],"")
    let chars = unique(split(source ++ components ++ "()[]{}|‖⌈⌉⌊⌋⟨⟩√̂̃̄⃗̇̈⏞⏟←→↔⇀↽− /", ""))
    let styles = ["normal", "auto", "italic", "bold", "bolditalic", "script", "fraktur", "double", "sans", "sansitalic", "mono"]
    let points = unique([for (ch in chars, style in styles) variant(ch, style)])
    let large_points = [for (ch in sym.large_symbols() where contains(chars, ch)) ord(ch)]
    let delimiter_data = if (use_bundled) tex_metrics.delimiters()^ else null
    let delimiter_small_points = unique([*[for (r in delimiter_data.recipes where len(r.small) > 0) r.codepoint],
        *[for (r in delimiter_data.arrow_symbols) r.codepoint]])
    let native = measure({font_family: family, font_size: UNITS}, points, faces, family)
    if (native == null) error("math: cannot read selected font: " ++ family)
    else {
    let facts = {*:native, constants: if (native.has_math) native.constants else fallback.constants(native.font_metrics)}
    // Ordinary fonts put italic/bold letters in separate faces, not Unicode math alphabets.
    // Only request style faces used by this formula; each query owns its font resources.
    let needed_styles = unique(["italic",*collect_styles(ast)])
    let style_faces = if (native.has_math) [] else [for (style in needed_styles where style != "normal")
        (let weight = if (style == "bold" or style == "bolditalic") 700 else 400,
         let slant = if (contains(["italic","bolditalic","sansitalic"],style)) "italic" else "normal",
         let style_family = variant_families[if (style == "sansitalic") "sans" else style] or family,
         {style: style, facts: measure({font_family: style_family, font_size: UNITS,
            font_weight: weight, font_style: slant}, points, faces, style_family)})]
    // Resolve only absent source characters; never replace a Latin variable with
    // a different font's mathematical-alphabet glyph just to obtain italics.
    // Composed symbols acquire only their bundled components, never a platform glyph.
    let fallback_points = [for (ch in chars where not contains(closed_composites, ch) and
        lookup({points: points}, native, ord(ch)) == null) ord(ch)]
    let fallback_facts = if (len(fallback_points) == 0) null else
        measure({font_family: if (use_bundled) bundled.SYMBOL_FAMILIES else family,
            font_size: UNITS, fallback: true}, fallback_points, faces)
    // Keep paired harpoons in one bundled face instead of unrelated system fallbacks.
    let reaction_points = if (use_bundled) [ord("⇀"), ord("↽"), ord("↼"), ord("⇁")] else []
    if (facts.constants.script_percent_scale_down <= 0 or facts.constants.script_script_percent_scale_down <= 0)
        error("math: selected font has invalid script scale constants")
    else {facts: facts, points: points, family: facts.font_family,
        font_metrics: facts.font_metrics, style_faces: style_faces,
        fallback_points: fallback_points, fallback_facts: fallback_facts, faces: faces,
        reaction_points: reaction_points,
        reaction_facts: if (use_bundled) measure({font_family:"KaTeX_Main", font_size:UNITS}, reaction_points, faces, "KaTeX_Main") else null,
        closed_composites: closed_composites,
        delimiter_data:delimiter_data,
        delimiter_faces:[for (family in unique([for (p in delimiter_data.pieces) p.family]))
            (let points = unique([for (p in delimiter_data.pieces where p.family == family) p.codepoint]),
            {family:family,points:points,facts:measure({font_family:family,font_size:UNITS},points,faces,family)})],
        delimiter_small_points:delimiter_small_points,
        delimiter_small_facts:if (delimiter_data != null) measure({font_family:"KaTeX_Main",font_size:UNITS},
            delimiter_small_points,faces,"KaTeX_Main") else null,
        tex: if (use_bundled) tex_metrics.load()^ else null,
        large_points: large_points,
        large_facts: if (use_bundled and len(large_points) > 0)
            measure({font_family:"KaTeX_Size2", font_size:UNITS}, large_points, faces, "KaTeX_Size2") else null}
    }
}

pub fn stylesheet(profile, body) => embedded_fonts.stylesheet(profile.faces, body)

fn lookup(profile, facts, cp) {
    let index = index_of(profile.points, cp)
    if (index != null) facts.glyphs[index] else null
}

pub fn glyph(profile, cp) map | error {
    let result = lookup(profile, profile.facts, cp) or
        lookup({points: profile.reaction_points}, profile.reaction_facts, cp) or
        lookup({points: profile.fallback_points}, profile.fallback_facts, cp)
    if (result == null) error("math: selected font lacks glyph U+" ++ string(cp) ++ " (" ++ profile.family ++ ")")
    else result
}

// Matched TFM supplies the selected glyph's logical box and italic correction.
fn with_tex_metrics(g, m) => if (m == null) g else
    {*:g, advance:m.width, height:m.height, depth:m.depth, italic:m.italic}

fn tex_piece(profile, slot) map | error {
    let piece = [for (p in profile.delimiter_data.pieces where p.slot == slot) p][0]
    let face = [for (f in profile.delimiter_faces where f.family == piece.family) f][0]
    let g = lookup(face,face.facts,piece.codepoint);
    if (piece == null or g == null) error("math: bundled CMEX delimiter piece is unavailable")
    // Reverse makeFF's authored glyph translation to recover the original CMEX baseline.
    else {*:with_tex_metrics(g,piece.metrics), advance:piece.metrics.width + piece.metrics.italic,
        source_baseline:piece.shift}
}

pub fn tex_delimiter(profile, cp) {
    let canonical = if (cp == 124) ord("∣") else if (cp == ord("‖")) ord("∥")
        else if (cp == 60) ord("⟨") else if (cp == 62) ord("⟩")
        else if (cp == ord("∖")) ord("\\") else cp
    let recipe = [for (r in profile.delimiter_data.recipes where r.codepoint == canonical) r][0]
    if (recipe == null) null
    else {
        let base = lookup({points:profile.delimiter_small_points},profile.delimiter_small_facts,canonical)
        let extension = recipe.chain[len(recipe.chain) - 1].metrics.extension
        let parts = [for (i,slot in extension)
            if (i < 3 and slot == 0) null else tex_piece(profile,slot)^];
        if (base == null and len(recipe.small) > 0) error("math: bundled small delimiter is unavailable")
        else {small:[for (m in recipe.small) {*:with_tex_metrics(base,m),advance:m.width + m.italic,
                source_baseline:recipe.small_shift}],
            variants:[for (v in recipe.chain where v.metrics.extension == null) tex_piece(profile,v.slot)^],
            top:parts[0], middle:parts[1], bottom:parts[2], repeat:parts[3]}
    }
}

pub fn tex_accent(profile, command) {
    let entry = [for (a in profile.delimiter_data.accents where a.cmd == command) a][0];
    if (entry == null) null else {variants:[for (v in entry.chain) tex_piece(profile,v.slot)^],
        x_height:profile.delimiter_data.accent_x_height}
}

// CMSY10/7/5 and scaled CMR10 share these unchanged KaTeX_Main encodings.
pub fn tex_arrow_symbol(profile, ch, style) {
    let entry = [for (s in profile.delimiter_data.arrow_symbols where s.codepoint == ord(ch)) s][0];
    if (entry == null) null else with_tex_metrics(
        lookup({points:profile.delimiter_small_points},profile.delimiter_small_facts,entry.codepoint),
        entry.metrics[if (style == "script") 1 else if (style == "scriptscript") 2 else 0])
}

fn style_facts(profile, style) => [for (entry in profile.style_faces where entry.style == style) entry.facts][0]

pub fn text_metrics(profile, style) => (style_facts(profile,style) or profile.facts).font_metrics

pub fn character(profile, ch, style, literal = false) map | error {
    let cp = if (literal) ord(ch) else variant(ch, style)
    let preferred = lookup(profile, profile.facts, cp)
    let actual = alphabet.effective_style(ch, style)
    let styled = style_facts(profile,actual)
    let ordinary = lookup(profile, styled or profile.facts, ord(ch))
    // Text punctuation must follow its text face, not the base math face's unchanged codepoint.
    if (literal and ordinary != null) ordinary
    else if (preferred != null) preferred
    else if (ordinary != null) ordinary
    else glyph(profile, ord(ch))^
}

pub fn large_operator(profile, cp) map | error {
    let base = glyph(profile, cp)^
    let larger = lookup({points:profile.large_points}, profile.large_facts, cp);
    if (larger != null and larger.ink.bottom - larger.ink.top > base.ink.bottom - base.ink.top) larger else base
}

pub fn scale(profile, style) {
    if (profile.tex != null and profile.tex.scales[style] != null) profile.tex.scales[style]
    else if (style == "script") profile.facts.constants.script_percent_scale_down / 100.0
    else if (style == "scriptscript") profile.facts.constants.script_script_percent_scale_down / 100.0
    else 1.0
}
