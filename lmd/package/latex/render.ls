// latex/render.ls — Pass 2: Render AST to HTML elements
// Uses pre-computed DocInfo from analyze.ls (read-only, no state threading).
// render_node(node, info) returns element|string|null directly.

import util: .util
import sym: .symbols
import math_bridge: .math_bridge
import spacing: .elements.spacing
import macros: .macros
import boxes: .elements.boxes
import font_decl: .elements.font_decl
import color: .elements.color
import picture: .elements.picture
import registry: .packages.registry
import graphicx: .packages.graphicx
import enumitem: .packages.enumitem
import siunitx: .packages.siunitx
import booktabs: .packages.booktabs
import biblatex: .packages.biblatex
import amsmath: .packages.amsmath
import amssymb: .packages.amssymb
import tikz: .packages.tikz
import bussproofs: .packages.bussproofs
import semantic: .packages.semantic
import language: .packages.language
import listings: .packages.listings
import tcolorbox: .packages.tcolorbox
import caption: .packages.caption
import dingbats: .packages.dingbats
import algorithmic: .packages.algorithmic
import algorithm2e: .packages.algorithm2e
import floats: .floats
import hyperref: .packages.hyperref
import page_style: .packages.page_style
import decorations: .packages.decorations
import paragraph: .packages.paragraph
import filler: .packages.filler
import paralist: .packages.paralist
import codeblocks: .packages.codeblocks
import frames: .packages.frames
import footmisc: .packages.footmisc
import appendix: .packages.appendix
import colspec: .packages.array
import hhline: .packages.hhline
import titles: .packages.titles
import contents: .packages.contents
import frontmatter: .packages.frontmatter
import varioref: .packages.varioref
import adjustbox: .packages.adjustbox
import pdfpages: .packages.pdfpages

// ============================================================
// Main dispatcher — called recursively on every AST node
// ============================================================

pub fn render_node(node, info) {
    if (node == null) null
    else if (node is string) render_text(node, info)
    else if (node is int or node is float) string(node)
    else if (node is symbol) render_symbol(node, info)
    else if (node is element) render_element(node, info)
    else string(node)
}

// render children of a node — used by postprocess for footnote bodies
// render a footnote's text for the footnote list; an explicit [n] is its number, not text
pub fn render_note_body(node, info) {
    render_children_skip_brack(node, info)
}

fn render_text(t, info) {
    if (t == " " or t == "\n") " "
    else if (len(trim(t)) == 0) null
    else {
        // convert ligatures that tree-sitter didn't handle (e.g., in command arguments)
        let t1 = replace(t, "---", "\u2014")
        let t2 = replace(t1, "--", "\u2013")
        // LGR maps ASCII in Greek language runs to Greek glyphs.
        let visible = if (info.text_language == "greek")
            language.lgr_to_unicode(t2, info.greek_polytonic)
            // Babel LGR uses a tilde as an accent; ordinary text uses it as a tie.
            else replace(t2, "~", " ")
        let localized = info.text_language != null and info.text_language != info.language
        if (localized or info.font_size != null)
            <span lang: if (localized) language.code(info.text_language) else null,
                style: font_size_style(info), visible>
        else visible
    }
}

fn font_size_style(info) {
    if (info.font_size == null) null
    else "font-size:" ++ info.font_size ++ ";line-height:" ++
        (if (info.line_height == null) "normal" else info.line_height)
}

fn render_symbol(s, info) {
    let v = string(s)
    match v {
        case "parbreak": null
        case "nbsp": "\u00A0"
        case "row_sep": <br>
        case "alignment_tab": null
        default: v
    }
}

// ============================================================
// Element dispatcher
// ============================================================

// Handlers for commands a package owns (registry CATALOG `commands`), consulted
// only while that package is active and before the core arms, so a package can
// redefine a core command. An owned command without a handler here falls
// through to the core arms.
pub let PACKAGE_RENDER = {
    amsmath: {DeclareMathOperator: render_nothing, 'DeclareMathOperator*': render_nothing,
        eqref: (el, info) => render_number_ref(el, info, true)},
    polyglossia: {textenglish: render_textenglish, otherlanguage: render_otherlanguage},
    gfsporson: {textporson: render_textporson},
    csquotes: {enquote: render_enquote},
    listings: {lstinline: (el, info) => listings.render_inline(el), lstset: render_nothing,
        lstlisting: (el, info) => listings.render_environment(el, info.listing_options,
            util.lookup(info.listing_nums, string(el.source_offset)))},
    tcolorbox: {tcolorbox: (el, info) =>
        tcolorbox.render(el, info.custom_colors, render_children(el, 0, info))},
    bussproofs: {prooftree: render_prooftree},
    makecell: {makecell: render_makecell},
    slashbox: {backslashbox: (el, info) => render_diagonal_cell(el, "backslash"),
        slashbox: (el, info) => render_diagonal_cell(el, "slash")},
    diagbox: {diagbox: (el, info) => render_diagonal_cell(el, "backslash")},
    fancyhdr: {fancyplain: (el, info) => render_children(el, 1, info)},
    cleveref: {cref: (el, info) => render_cref(el, info, false),
        Cref: (el, info) => render_cref(el, info, true)},
    doi: {doi: (el, info) => render_doi(el)},
    hyperref: {hypersetup: render_nothing, hypertarget: render_hypertarget,
        hyperlink: render_hyperlink,
        // \addcontentsline anchors itself, so the phantom anchor adds nothing
        phantomsection: render_nothing,
        texorpdfstring: (el, info) => render_node(util.command_args(el)[0], info)},
    caption: {captionsetup: render_captionsetup},
    graphicx: {resizebox: render_graphicx_box, scalebox: render_graphicx_box,
        rotatebox: render_graphicx_box, reflectbox: render_graphicx_box},
    placeins: {FloatBarrier: render_nothing},
    // the float package's definitions act through analysis (float kinds)
    float: {newfloat: render_nothing, floatname: render_nothing, floatstyle: render_nothing,
        restylefloat: render_nothing, floatplacement: render_nothing, listof: render_listof},
    bookmark: {bookmark: (el, info) => hyperref.bookmark_command(el, info.hyperref_settings)},
    pifont: {ding: render_ding,
        dinglist: (el, info) => render_generic_list(el, info, (groups, i) => ding_label(groups, 0)),
        dingautolist: (el, info) => render_generic_list(el, info, (groups, i) => ding_label(groups, i))},
    marvosym: map([for (k, v at dingbats.MARVOSYM, x in [string(k), render_marvosym]) x]),
    // label macros are definitions (analysis) when they carry an argument
    algpseudocode: map(["algorithmic", render_algorithmic, "Comment", render_alg_comment,
        "Call", render_alg_call, "Return", render_alg_inline,
        "algrenewcommand", render_nothing, "algnewcommand", render_nothing] ++
        [for (macro in algorithmic.LABEL_MACROS, x in [macro, render_alg_label_macro]) x]),
    // statements and blocks fold inside the algorithm float (render_alg2e_body); these
    // appear inside statements, and settings act through analysis
    subfig: {subfloat: render_subfig},
    subfigure: {subfigure: render_subfig},
    wrapfig: {wrapfigure: render_wrapfloat, wraptable: render_wrapfloat},
    // sideways floats render upright (registry target diagnostics)
    rotating: {sidewaysfigure: (el, info) => render_float(el, info, "figure"),
        sidewaystable: render_table_env, turn: render_turn, rotate: render_turn,
        sideways: render_turn},
    pdflscape: {landscape: render_landscape},
    lscape: {landscape: render_landscape},
    algorithm2e: map(["KwTo", (el, info) => alg_word("to", info),
        "KwRet", (el, info) => <span class: "latex-alg-return", alg_word("return", info) " "
            for c in render_children(el, 0, info) { c }>,
        "tcp", render_alg2e_comment, "tcc", render_alg2e_comment,
        "tcp*", render_alg2e_comment, "tcc*", render_alg2e_comment] ++
        [for (setting in algorithm2e.SETTINGS, x in [setting, render_nothing]) x]),
    algorithmic: map(["algorithmic", render_algorithmic, "COMMENT", render_alg_comment] ++
        [for (k, v at algorithmic.INLINE_WORDS,
            x in (if (string(k) != "Return") [string(k), render_alg_inline] else [])) x]),
    siunitx: {num: render_siunitx, si: render_siunitx, unit: render_siunitx,
        SI: render_siunitx, qty: render_siunitx},
    // tag forms act through the analysis pass (amsmath.tag_text)
    mathtools: {newtagform: render_nothing, renewtagform: render_nothing, usetagform: render_nothing},
    soul: map([for (command in decorations.SOUL_COMMANDS, x in [command, (el, info) =>
        decorations.render_soul(el, render_children(el, 0, info), info.settings, info.custom_colors)]) x] ++
        [for (setting in decorations.SOUL_SETTINGS, x in [setting, render_nothing]) x]),
    ulem: map([for (command in decorations.ULEM_COMMANDS, x in [command, (el, info) =>
        decorations.render_ulem(el, render_children(el, 0, info))]) x] ++
        ["emph", render_ulem_emph, "normalem", render_nothing, "ULforem", render_nothing]),
    // paragraph declarations act through find_leading_decl and next_render_info
    setspace: map([for (tag in paragraph.SETSPACE_DECLARATIONS, x in [tag, render_nothing]) x] ++
        [for (tag in paragraph.SETSPACE_ENVIRONMENTS, x in [tag, render_spacing_env]) x]),
    ragged2e: map([for (tag in paragraph.RAGGED_DECLARATIONS, x in [tag, render_nothing]) x] ++
        [for (tag in paragraph.RAGGED_ENVIRONMENT_NAMES, x in [tag, (el, info) =>
            render_env_div(el, info, "latex-" ++ string(name(el)), paragraph.ragged_css(string(name(el))))]) x]),
    lipsum: map([for (tag in filler.COMMANDS.lipsum, x in [tag, (el, info) => filler.render(el)]) x]),
    blindtext: map([for (tag in filler.COMMANDS.blindtext, x in [tag, (el, info) => filler.render(el)]) x]),
    paralist: map([for (tag in paralist.ENVIRONMENT_NAMES, x in [tag, render_paralist]) x]),
    fancybox: map([for (tag in boxes.FANCYBOX_COMMANDS, x in [tag, render_fancy_frame]) x]),
    boxedminipage: {boxedminipage: render_boxed_minipage},
    // framed blocks on the frame-box model (frames.ls)
    framed: map([for (tag in frames.FRAMED, x in [tag, render_framed]) x]),
    mdframed: {mdframed: (el, info) => render_mdframed(el, info, {}),
        newmdenv: render_nothing, mdfdefinestyle: render_nothing},
    // code blocks through the listings renderer (codeblocks.ls)
    fancyvrb: map([for (tag in codeblocks.FANCYVRB_ENVIRONMENTS, x in [tag, render_fancyvrb]) x] ++
        ["Verb", (el, info) => listings.render_inline(el), "fvset", render_nothing]),
    minted: {minted: render_minted, mintinline: (el, info) => <code class: "latex-code language-" ++ (el.language or "text"),
            style: "font-family:monospace;", util.text_of(el)>,
        mint: (el, info) => listings.render_code(util.text_of(el), {}, null, el.language),
        setminted: render_nothing},
    footmisc: {footref: render_footref},
    // colortbl acts in the table builder; \cellcolor marks its cell
    colortbl: {cellcolor: render_cellcolor, arrayrulecolor: render_nothing, doublerulesepcolor: render_nothing},
    array: {newcolumntype: render_nothing},
    adjustbox: {adjustbox: render_adjustbox},
    pdfpages: {includepdf: render_includepdf, includepdfset: render_nothing},
    tocloft: map([for (command in contents.TOCLOFT_COMMANDS, x in [command, render_nothing]) x]),
    titling: map([for (command in frontmatter.TITLING_COMMANDS, x in [command,
        if (starts_with(command, "the")) render_title_part else render_nothing]) x]),
    authblk: {affil: render_nothing},
    varioref: map([for (command in varioref.COMMANDS, x in [command, render_vref]) x]),
    minitoc: map([for (command in contents.MINITOC_COMMANDS, x in [command,
        if (contents.mini_kind(command) != null) render_minitoc else render_nothing]) x]),
    threeparttable: {threeparttable: render_threeparttable, measuredfigure: render_threeparttable,
        tablenotes: render_tablenotes, tnote: (el, info) => tnote_label(trim(util.text_of(el)), info)},
    appendix: {appendices: render_appendices,
        subappendices: (el, info) => render_env_div(el, info, "latex-subappendices", null),
        appendixpage: (el, info) => render_appendix_page(), addappheadtotoc: render_contents_anchor},
    nicefrac: {nicefrac: render_math_command},
    xfrac: {sfrac: render_math_command},
    mhchem: {ce: render_math_command, pu: render_math_command}
}

fn render_nothing(el, info) => null

// \cellcolor[model]{color}: a marker the table builder turns into the cell's
// background (cell_decoration)
fn render_cellcolor(el, info) {
    let css = color.command_color(el, 0, info.custom_colors)
    if (info.table_cell != true)
        util.unsupported_element("colortbl", "\\cellcolor belongs in a table cell", el.source_offset)
    else if (css == null) color.invalid_color(el)
    else {type: "cellcolor", color: css}
}

// ulem underlines \emph unless normalem is in effect
fn render_ulem_emph(el, info) {
    let options = registry.options_for(info.packages, "ulem")
    if (decorations.emph_underlined(info.settings, el.source_offset,
        options != null and util.option_enabled(options.normalem)))
        <span class: "latex-uline", style: "text-decoration:underline", for c in render_children(el, 0, info) { c }>
    else render_styled(el, info, "em")
}

fn render_ding(el, info) {
    let glyph = dingbats.ding(util.raw_argument(el, "required", 0))
    if (glyph == null) util.unsupported_element("pifont", "Unsupported ding code", el.source_offset)
    else <span class: "latex-ding", glyph>
}

// a dinglist label, or dingautolist's label for item `step`: \ding{n + step}
fn ding_label(groups, step) {
    let start = if (len(groups) == 0) null else int(trim(util.text_of(groups[0]))) ^ { null }
    let glyph = if (start == null) null else dingbats.ding(string(start + step))
    if (glyph == null) null else <span class: "latex-item-label", glyph>
}

fn render_marvosym(el, info) => <span class: "latex-marvosym", dingbats.MARVOSYM[string(name(el))]>

// ============================================================
// Pseudocode (algpseudocode, algorithmic): one line per statement
// ============================================================

fn render_algorithmic(el, info) {
    <div class: "latex-algorithmic", for (line in algorithmic.lines(el)) render_alg_line(line, info)>
}

fn render_alg_line(line, info) {
    let desc = line.desc
    let args = util.command_args(line.node)
    let words = if (desc.words == null) [] else
        [for (i, word in desc.words, item in (if (i > 0) [" ", alg_word(word, info)] else [alg_word(word, info)])) item]
    let condition = if (desc.call == true and len(args) > 0)
        [<span class: "latex-alg-name", render_node(args[0], info)>, "(",
            *[for (arg in slice(args, 1, len(args))) render_node(arg, info)], ")"]
        else [for (arg in args, let r = render_node(arg, info) where r != null) r]
    let after = if (desc.after == null) [] else [alg_word(desc.after, info)]
    let groups = [for (group in [words, condition, after] where len(group) > 0) group]
    let parts = [for (i, group in groups, item in (if (i > 0) [" ", *group] else group)) item]
    let body = [for (node in line.items, let r = render_node(node, info) where r != null) r];
    <div class: "latex-alg-line", id: if (line.label != null) util.slugify(line.label) else null,
        style: "padding-left:" ++ string(line.depth * 1.5) ++ "em",
        <span class: "latex-alg-number", if (line.shown != null) string(line.shown) ++ ":" else "">
        for c in parts { c }
        for c in body { c }>
}

// a keyword's label: the document's \algorithmic<word> definition, else its default
fn alg_word(word, info) {
    let defined = util.lookup(info.alg_labels, word)
    let macro = macros.find_macro(info.macros, "algorithmic" ++ word)
    if (defined != null) <span class: "latex-alg-keyword", for c in render_children(defined, 0, info) { c }>
    else if (macro != null and macro.body != null)
        <span class: "latex-alg-keyword", for c in render_children(macro.body, 0, info) { c }>
    else <strong class: "latex-alg-keyword", algorithmic.word_text(word)>
}

fn render_alg_inline(el, info) {
    let tag = string(name(el))
    alg_word(if (tag == "Return") "return" else lower(algorithmic.INLINE_WORDS[tag]), info)
}

fn render_alg_comment(el, info) =>
    <span class: "latex-alg-comment", "▷ " for c in render_children(el, 0, info) { c }>

// \Call{name}{arguments}
fn render_alg_call(el, info) {
    let args = util.command_args(el);
    <span class: "latex-alg-call",
        <span class: "latex-alg-name", if (len(args) > 0) render_node(args[0], info) else null>
        "(" for (arg in slice(args, 1, len(args))) render_node(arg, info) ")">
}

// ============================================================
// subfig, wrapfig, rotating, landscape
// ============================================================

// \subfloat[list][caption]{body} (subfig) and \subfigure[caption]{body}: a lettered
// subfloat of the enclosing float, captioned from its last optional argument
fn render_subfig(el, info) {
    let kind = if (info.caption_type == "table") "subtable" else "subfigure"
    let letter = util.lookup(info.subfloat_nums, string(el.source_offset))
    let args = util.command_args(el)
    let start = util.last_argument_start(el)
    let body = slice(args, start, len(args))
    let options = [for (node in slice(args, 0, start)
        where node is element and string(name(node)) == "brack_group") node]
    let caption_text = if (len(options) == 0) [] else render_children(options[len(options) - 1], 0, info)
    let items = collect_children(body, 0, len(body), [], {*:info, caption_type: kind, caption_number: letter})
    if (info.caption_type == null or letter == null)
        util.unsupported_element(string(name(el)), "A subfloat needs a parent figure or table", el.source_offset)
    else <div class: "latex-subfloat", style: "display:inline-block;vertical-align:top;margin:0 0.5em;",
        for item in items { item }
        caption.render(caption_text, kind, letter, info.caption_options, false, el.source_offset, info.float_kinds)>
}

// \begin{wrapfigure}[lines]{placement}[overhang]{width}: a float the text wraps around
fn render_wrapfloat(el, info) {
    let kind = if (string(name(el)) == "wraptable") "table" else "figure"
    let header = util.environment_header(el, 2)
    let placement = if (len(header.groups) > 0) trim(util.text_of(header.groups[0])) else ""
    let side = if (contains("lLiI", placement) and placement != "") "left" else "right"
    let width_source = if (len(header.groups) > 1) trim(util.group_source(header.groups[1])) else ""
    let width = if (width_source == "" or width_source == "0pt") null else util.css_dimension(width_source)
    let items = collect_children(header.body, 0, len(header.body), [], {*:info, caption_type: kind,
        caption_number: string(get_float_num(el, info, kind))})
    if (len(header.groups) < 2 or (width_source != "0pt" and width == null))
        util.unsupported_element("wrapfig", "wrapfigure needs a placement and a width", el.source_offset)
    else <figure class: "latex-wrapfloat latex-wrap" ++ kind,
        style: "float:" ++ side ++ ";" ++ (if (width == null) "" else "width:" ++ width ++ ";") ++
            (if (side == "left") "margin:0.3em 1em 0.5em 0;" else "margin:0.3em 0 0.5em 1em;"),
        for c in items { c }>
}

// paralist's lists: compact, run-in or inline forms of itemize, enumerate and
// description; an enumerate form takes a label template such as [(a)]
fn render_paralist(el, info) {
    let spec = paralist.ENVIRONMENTS[string(name(el))]
    let lead = util.leading_option(el)
    let flat = util.flatten_paragraphs(<paralist for c in lead.children { c }>)
    let tpl = if (spec.kind == "enumerate") paralist.template(lead.option) else null
    if (spec.form == "compact" and tpl == null) compact_list(flat, info, spec.kind)
    else (let items = labelled_items(flat, info, spec.kind, tpl),
        if (spec.form == "inline")
            <span class: "latex-inpara", for (i, item in items)
                <span class: "latex-inpara-item", (if (i > 0) " " else null); item.label; for c in item.body { c }>>
        else if (spec.form == "runin")
            <div class: "latex-runin", for (item in items)
                <p class: "latex-runin-item", item.label; for c in item.body { c }>>
        else <ul class: "latex-list", style: enumitem.list_style({nosep: "true"}, "itemize"),
            for (item in items) <li item.label; for c in item.body { c }>>)
}

// a compact list: the ordinary list with enumitem's nosep spacing
fn compact_list(flat, info, kind) {
    let style = enumitem.list_style({nosep: "true"}, kind)
    match kind {
        case "enumerate": <ol class: "latex-enumerate", style: style, for li in split_list_items(flat, info) { li }>
        case "description": <dl class: "latex-description", style: style, for item in split_desc_items(flat, info) { item }>
        default: <ul class: "latex-itemize", style: style, for li in split_list_items(flat, info) { li }>
    }
}

// each item as {label, body}: a description item's term in bold, otherwise the
// template's label or the default one
fn labelled_items(flat, info, kind, tpl) {
    if (kind == "description") (let parts = split_desc_items(flat, info),
        [for (i, part in parts where name(part) == 'dt')
            {label: <strong for c in content(part) { c }>,
             body: if (i + 1 < len(parts) and name(parts[i + 1]) == 'dd') content(parts[i + 1]) else []}])
    else [for (li in split_list_items(flat, info, (i) => <span class: "latex-item-label",
            if (tpl != null) paralist.label(tpl, i + 1) else paralist.default_label(kind, i + 1)>))
        (let children = content(li), {label: children[0], body: slice(children, 1, len(children))})]
}

fn render_fancy_frame(el, info) {
    let tag = string(name(el));
    <span class: "latex-" ++ tag, style: boxes.fancybox_css(tag), for c in render_children(el, 0, info) { c }>
}

// \begin{boxedminipage}[position]{width}: the width is the header group
fn render_boxed_minipage(el, info) {
    let lead = util.leading_option(el)
    let header = util.environment_header(<boxedminipage for c in lead.children { c }>, 1)
    let width = if (len(header.groups) > 0) util.css_dimension(trim(util.group_source(header.groups[0]))) else null
    if (width == null) util.unsupported_element("boxedminipage", "boxedminipage needs a width", el.source_offset)
    else <div class: "latex-boxedminipage", style: boxes.boxed_minipage_css(width),
        for c in collect_children(header.body, 0, len(header.body), [], info) { c }>
}

// framed's framed, shaded and leftbar blocks
fn render_framed(el, info) {
    let tag = string(name(el))
    let css = frames.framed_css(tag, info.custom_colors)
    if (css == null) util.unsupported_element("framed", tag ++ " needs a defined shadecolor", el.source_offset)
    else <div class: "latex-" ++ replace(tag, "*", ""), style: css, for c in render_children(el, 0, info) { c }>
}

// an mdframed block: a \newmdenv's keys, then a style= style's, then its own
fn render_mdframed(el, info, defaults) {
    let lead = util.leading_option(el)
    let own = util.parse_kv_options(lead.option)
    let style = own.style or defaults.style
    let styled = if (style == null) {} else util.parse_kv_options(
        util.setting_at(info.settings, "mdf-style:" ++ trim(string(style)), el.source_offset))
    let frame = frames.mdframed_frame({*:defaults, *:styled, *:own}, info.custom_colors)
    if (frame == null) util.unsupported_element("mdframed", "Unknown mdframed color", el.source_offset)
    else <div class: "latex-mdframed", style: frame.css,
        if (frame.title != null) <div class: "latex-mdframed-title", string(frame.title)>;
        for c in collect_children(lead.children, 0, len(lead.children), [], info) { c }>
}

// fancyvrb's Verbatim family and minted's environment: the raw body's head holds the
// options (and minted's language); the preamble's \fvset or \setminted sets defaults
fn render_fancyvrb(el, info) {
    let parsed = codeblocks.head(el.source, false)
    listings.render_code(parsed.code, codeblocks.listing_options(
        codeblocks.keys_of(parsed.options, info.code_defaults.fancyvrb), false), null)
}

fn render_minted(el, info) {
    let parsed = codeblocks.head(el.source, true)
    listings.render_code(parsed.code, codeblocks.listing_options(
        codeblocks.keys_of(parsed.options, info.code_defaults.minted), true), null, parsed.language)
}

// setspace's environments; spacing{f} reads its stretch from the header group
fn render_spacing_env(el, info) {
    let tag = string(name(el))
    let header = util.environment_header(el, if (tag == "spacing") 1 else 0)
    let value = if (len(header.groups) > 0) trim(util.text_of(header.groups[0])) else null
    let stretch = paragraph.environment_stretch(tag, value, info.class_options)
    if (stretch == null) util.unsupported_element("setspace", "spacing needs a positive stretch", el.source_offset)
    else <div class: "latex-" ++ tag, style: paragraph.stretch_css(stretch),
        for c in collect_children(header.body, 0, len(header.body), [], info) { c }>
}

// turn and rotate turn their content by an angle; sideways by a quarter turn
fn render_turn(el, info) {
    let sideways = string(name(el)) == "sideways"
    let header = util.environment_header(el, if (sideways) 0 else 1)
    let angle = if (sideways) "90" else if (len(header.groups) > 0) trim(util.text_of(header.groups[0])) else ""
    let items = collect_children(header.body, 0, len(header.body), [], info)
    if ((float(angle) ^ { null }) == null)
        util.unsupported_element("rotating", "turn and rotate need a numeric angle", el.source_offset)
    else graphicx.transformed_box("latex-turn", "transform:" ++ graphicx.css_rotation(angle) ++
        ";transform-origin:center center", items)
}

// landscape content stays on the continuous page (registry target diagnostics for PDF)
fn render_landscape(el, info) => <div class: "latex-landscape", for c in render_children(el, 0, info) { c }>

// ============================================================
// algorithm2e: the algorithm body itself is the pseudocode
// ============================================================

// numbering, semicolons and end lines, from the package options and the
// document's settings commands (the last of a pair wins)
fn alg2e_settings(info) {
    let opts = registry.options_for(info.packages, "algorithm2e")
    let given = if (info.alg2e_settings == null) [] else info.alg2e_settings
    let numbering = last_setting(given, "LinesNumbered", "LinesNotNumbered")
    let semicolons = last_setting(given, "PrintSemicolon", "DontPrintSemicolon");
    {numbered: if (numbering != null) numbering == "LinesNumbered" else opts != null and opts.linesnumbered != null,
     semicolons: semicolons != "DontPrintSemicolon",
     noend: (opts != null and opts.noend != null) or (contains(given, "SetAlgoNoEnd") or false)}
}

// whichever of two opposing settings the document gave last, or null
fn last_setting(given, a, b) {
    let hits = [for (s in given where s == a or s == b) s]
    if (len(hits) == 0) null else hits[len(hits) - 1]
}

// the lines, then the captions as direct children of the float so its style can place them
fn render_alg2e_body(body, info) {
    let settings = alg2e_settings(info)
    let folded = algorithm2e.fold(util.flatten_paragraphs(body), 0, settings)
    let lines = algorithm2e.numbered(folded.lines, settings.numbered);
    [<div class: "latex-algorithmic latex-algorithm2e",
        for (line in lines) render_alg2e_line(line, info, settings)>,
     *[for (block in folded.blocks) render_node(block, info)]]
}

fn render_alg2e_line(line, info, settings) {
    let words = if (line.words == null) [] else
        [for (i, word in line.words, item in (if (i > 0) [" ", alg_word(word, info)] else [alg_word(word, info)])) item]
    let rendered = [for (node in (if (line.nodes != null) line.nodes else line.condition),
        let r = render_node(node, info) where r != null) r]
    let parts = if (line.kind == "io") [<strong class: "latex-alg-keyword", line.label>, " ", *rendered]
        else if (line.kind == "statement")
            rendered ++ (if (line.semicolon and settings.semicolons) [";"] else [])
        else if (line.kind == "blank") [" "]
        else words ++ (if (len(rendered) > 0) [" ", *rendered] else []) ++
            (if (line.after != null) [" ", alg_word(line.after, info)] else []);
    <div class: "latex-alg-line", style: "padding-left:" ++ string(line.depth * 1.5) ++ "em",
        <span class: "latex-alg-number", if (line.number != null) string(line.number) else "">
        for c in parts { c }>
}

// algorithm2e's comments: \tcp{…} as //, \tcc{…} as /* … */
fn render_alg2e_comment(el, info) {
    let block = starts_with(string(name(el)), "tcc")
    let parts = [if (block) "/* " else "// ", *render_children(el, 0, info), if (block) " */" else ""];
    <span class: "latex-alg-comment", for c in parts { c }>
}

// a label macro with an argument is a definition; without one it shows the label
fn render_alg_label_macro(el, info) {
    let macro = string(name(el))
    if (len(content(el)) > 0) null else alg_word(slice(macro, 11, len(macro)), info)
}

// graphicx box commands: the content is the last argument; a lone image is
// sized through its own keys
fn render_graphicx_box(el, info) {
    graphicx.render_box(el, render_children(el, util.last_argument_start(el), info),
        graphicx.lone_image(el), info.base_uri)
}

// \adjustbox{keys}{content}, or the environment with its keys as the header group
fn render_adjustbox(el, info) {
    let box = adjustbox.parts(el)
    let items = if (box.body == null) render_children(el, box.start, info)
        else collect_children(box.body, 0, len(box.body), [], info)
    adjustbox.render(adjustbox.keys(box.raw), items, box.image, info.base_uri, info.custom_colors, el.source_offset,
        box.body != null)
}

// pdfpages' \includepdf: each selected page a block of its own, on \includepdfset's
// defaults; {} in the page list is a blank page
fn render_includepdf(el, info) {
    let opts = {*:util.parse_kv_options(util.setting_at(info.settings, "includepdfset", el.source_offset)),
        *:util.parse_kv_options(util.optional_raw(el))}
    let unknown = pdfpages.unknown_keys(opts)
    let src = graphicx.resolved_source(trim(util.text_of_skip_brack(el)), info.base_uri)
    let parsed = if (len(unknown) > 0) null else graphicx.pdf_document(src)
    if (len(unknown) > 0)
        util.unsupported_element("pdfpages", "Unsupported includepdf key: " ++ join(unknown, ", "), el.source_offset)
    else if (parsed == null) util.unsupported_element("pdfpages", "Cannot read PDF " ++ src, el.source_offset)
    else {
        let keys = pdfpages.page_keys(opts)
        let framed = util.option_enabled(opts.frame);
        <div class: "latex-includepdf",
            for (k, page in pdfpages.page_list(opts.pages, graphicx.pdf_page_count(parsed)))
                <div class: "latex-pdfpage", style: if (framed) "border:0.4pt solid" else null,
                    if (page == null) null else graphicx.render_pdf_page(parsed, src, page, keys,
                        "latex-pdfpage-" ++ string(el.source_offset) ++ "-" ++ string(k), el.source_offset)>>
    }
}

// \hypertarget{name}{text} and \hyperlink{name}{text}; names slug like labels
fn render_hypertarget(el, info) {
    let args = util.command_args(el);
    <span id: util.slugify(trim(util.text_of(args[0]))),
        for (arg in slice(args, 1, len(args))) render_node(arg, info)>
}

fn render_hyperlink(el, info) {
    let args = util.command_args(el);
    <a href: "#" ++ util.slugify(trim(util.text_of(args[0]))),
        for (arg in slice(args, 1, len(args))) render_node(arg, info)>
}

fn render_ensuremath(el, info) =>
    render_inline_ast(math_bridge.ensuremath_ast(el), el, info, "Cannot parse the ensuremath argument")

// \nicefrac, \sfrac, \ce and \pu set math in any mode
fn render_math_command(el, info) =>
    render_inline_ast(math_bridge.command_ast(el), el, info, "Cannot parse \\" ++ string(name(el)))

fn render_inline_ast(ast, el, info, message) {
    if (ast == null) util.unsupported_element("latex", message, el.source_offset)
    else bold_math(math_bridge.render_inline_el({ast: ast}), info, false)
}

fn render_siunitx(el, info) => siunitx.render(el)

fn render_prooftree(el, info) {
    bussproofs.render(el, info.macros) ^ {
        util.unsupported_element("bussproofs",
            "Proof tree could not be rendered: " ++ ^.message, el.source_offset)
    }
}

// the gallery needs the demo option; without it the command is reported either way
fn render_alltikzpeople(el, info) {
    if (registry.active(info.packages, "tikzpeople") and
        registry.options_for(info.packages, "tikzpeople").demo != null)
        tikz.render_gallery(el) ^ {
            util.unsupported_element("tikzpeople", ^.message, el.source_offset)
        }
    else util.unsupported_element("tikzpeople",
        "alltikzpeople requires the tikzpeople demo option", el.source_offset)
}

fn render_captionsetup(el, info) {
    if (len(caption.invalid_options(caption.setup(el))) == 0 and
        (util.optional_raw(el) == null or info.caption_options[trim(util.optional_raw(el))] != null)) null
    else util.unsupported_element("caption", "Unsupported caption setup", el.source_offset)
}

fn render_element(el, info) {
    let tag = name(el)
    let owned = registry.handler(PACKAGE_RENDER, info.packages, string(tag))
    if (info.biblatex_context != null and
        biblatex.is_citation_command(string(tag)))
        biblatex.render_citation(el, info.biblatex_context, info.footnotes)
    else if (owned != null) owned(el, info)
    else match tag {
        // ---- document structure ----
        case 'latex_document': render_document(el, info)
        case 'document': render_body(el, info)

        // ---- preamble (skip — already extracted in pass 1) ----
        case 'documentclass': null
        case 'usepackage': null
        case 'title': null
        case 'author': null
        case 'date': null
        case 'filecontents': null
        case 'filecontents*': null
        case 'begingroup': null
        case 'endgroup': null
        case 'selectfont': null
        case 'pagestyle': render_page_style(el, info)
        case 'thispagestyle': render_page_style(el, info)
        case 'fontsize': if (font_size_arguments(el) != null) null
            else util.unsupported_element("latex", "Invalid fontsize dimensions", el.source_offset)
        case 'selectlanguage': null
        case 'boldmath': null
        case 'unboldmath': null
        // \mathversion{bold} threads like \boldmath (next_render_info)
        case 'mathversion': null
        // NFSS declarations act as a group's leading declaration (font_decl.decl_css)
        case 'fontfamily': null
        case 'fontseries': null
        case 'fontshape': null
        case 'usefont': null
        case 'linespread': null
        case 'normalfont': null
        case 'languageattribute': null
        case 'spanishdecimal': null
        case 'foreignlanguage': render_foreignlanguage(el, info)
        case 'setdefaultlanguage': null
        case 'setotherlanguage': null
        case 'newfontfamily': null
        case 'renewfontfamily': null
        case 'XeTeXlinebreaklocale': null
        case 'directlua': util.unsupported_element("lua",
            "Embedded Lua is outside the LaTeX package", el.source_offset)
        case 'luacode': util.unsupported_element("lua",
            "Embedded Lua is outside the LaTeX package", el.source_offset)
        case 'luacode*': util.unsupported_element("lua",
            "Embedded Lua is outside the LaTeX package", el.source_offset)

        // ---- title page ----
        case 'maketitle': render_maketitle(info)
        case 'thanks': render_thanks_ref(el, info)
        case 'and': <span class: "latex-and">
        case 'titlepage': render_env_div(el, info, "latex-titlepage", null)
        case 'tableofcontents': render_toc(info)
        case 'addcontentsline': render_contents_anchor(el, info)

        // ---- sections ----
        case 'part': render_heading(el, info, 1)
        case 'chapter': render_heading(el, info, 1)
        case 'section': render_heading(el, info, 2)
        case 'subsection': render_heading(el, info, 3)
        case 'subsubsection': render_heading(el, info, 4)
        case 'paragraph_command': render_heading(el, info, 5)
        case 'subparagraph': render_heading(el, info, 6)

        // ---- paragraphs ----
        case 'paragraph': render_paragraph(el, info)

        // ---- text styling ----
        case 'textbf': render_styled(el, info, "b")
        case 'textit': render_styled(el, info, "i")
        case 'emph': render_styled(el, info, "em")
        case 'underline': render_styled(el, info, "u")
        case 'texttt': render_code(el, info)
        case 'textup': render_font(el, info, "latex-up")
        case 'textmd': render_font(el, info, "latex-md")
        case 'textnormal': render_font(el, info, "latex-normal")
        case 'textsuperscript': <sup for c in render_children(el, 0, info) { c }>
        case 'textsubscript': <sub for c in render_children(el, 0, info) { c }>
        case 'ensuremath': render_ensuremath(el, info)
        case 'verb': render_verb(el)
        case 'verb_command': render_verb_command(el)

        // ---- font families ----
        case 'textsf': if (info.polyglossia != null and info.language == "thai")
            <span class: "latex-sf", style: language.font_style(info.polyglossia,
                "thaifontsf", "sans-serif"), for c in render_children(el, 0, info) { c }>
            else render_font(el, info, "latex-sf")
        case 'textsc': render_font(el, info, "latex-sc")
        case 'textsl': render_font(el, info, "latex-sl")
        case 'textrm': render_font(el, info, "latex-rm")

        // ---- font declarations (handled in render_group, standalone = no-op) ----
        case 'itshape': null
        case 'bfseries': null
        case 'bf': null
        case 'it': null
        case 'rm': null
        case 'tt': null
        case 'ttfamily': null
        case 'rmfamily': null
        case 'sffamily': null
        case 'scshape': null
        case 'slshape': null
        case 'upshape': null
        case 'mdseries': null

        // ---- alignment declarations (handled in render_group, standalone = no-op) ----
        case 'centering': null
        case 'raggedright': null
        case 'raggedleft': null

        // ---- appendix marker (handled in analyze pass) ----
        case 'appendix': null

        // ---- color commands ----
        case 'textcolor': color.render_textcolor(el, render_children(el, color.content_start(el, 1), info), info.custom_colors)
        case 'colorbox': color.render_colorbox(el, render_children(el, color.content_start(el, 1), info), info.custom_colors)
        case 'fcolorbox': color.render_fcolorbox(el, render_children(el, color.content_start(el, 2), info), info.custom_colors)
        case 'definecolor': null
        case 'pagecolor': if (color.page_color(el, info.custom_colors) == null)
            color.invalid_color(el) else null
        case 'color': null

        // ---- font sizes ----
        case 'tiny': render_size(el, info, "tiny")
        case 'scriptsize': render_size(el, info, "scriptsize")
        case 'footnotesize': render_size(el, info, "footnotesize")
        case 'small': render_size(el, info, "small")
        case 'normalsize': render_size(el, info, "normalsize")
        case 'large': render_size(el, info, "large")
        case 'Large': render_size(el, info, "Large")
        case 'LARGE': render_size(el, info, "LARGE")
        case 'huge': render_size(el, info, "huge")
        case 'Huge': render_size(el, info, "Huge")

        // ---- diacritics ----
        case 'accent': render_accent(el, info)

        // ---- math ----
        case 'inline_math': bold_math(math_bridge.render_inline_el(el), info, false)
        case 'display_math': bold_math(if (registry.active(info.packages, "semantic") and
            semantic.contains_inference(el))
            semantic.render_inference_display(el) ^ {
                util.unsupported_element("semantic", "Unsupported semantic inference rule",
                    el.source_offset)
            } else math_bridge.render_display_el(el), info, true)
        case 'math_environment': render_math_env(el, info)
        case 'equation': render_numbered_equation(el, info)

        // ---- lists ----
        case 'itemize': render_itemize(el, info)
        case 'enumerate': render_enumerate(el, info)
        case 'description': render_description(el, info)
        case 'item': null
        case 'list': render_generic_list(el, info, (groups, i) => list_label(groups, info))
        case 'trivlist': render_generic_list(el, info, null)
        case 'tabbing': render_tabbing(el, info)

        // ---- environments ----
        case 'quote': render_env_block(el, info, "blockquote", "latex-quote")
        case 'quotation': render_env_block(el, info, "blockquote", "latex-quote latex-quotation")
        case 'verse': render_env_block(el, info, "blockquote", "latex-quote latex-verse")
        case 'center': render_env_div(el, info, "latex-center", "text-align:center")
        case 'flushleft': render_env_div(el, info, "latex-flushleft", "text-align:left")
        case 'flushright': render_env_div(el, info, "latex-flushright", "text-align:right")
        case 'verbatim': render_verbatim_env(el, info)
        // without listings, lstlisting stays a plain verbatim block
        case 'lstlisting': render_verbatim_env(el, info)
        case 'abstract': render_abstract(el, info)
        case 'figure': render_figure(el, info)
        case 'figure*': render_figure(el, info)
        case 'table*': render_table_env(el, info)
        case 'picture': if (registry.active(info.packages, "semantic") and
            semantic.contains_diagram(el))
            semantic.render_tdiagram_picture(el) ^ {
                util.unsupported_element("semantic", "Unsupported semantic T-diagram",
                    el.source_offset)
            } else picture.render_picture(el, info.unitlength)
        case 'tikzpicture': tikz.render_picture(el, info.macros,
            info.tikz_declarations, info.tikz_math_declarations,
            info.custom_colors, registry.active(info.packages, "tikzpeople"),
            info.base_uri)
        case 'alltikzpeople': render_alltikzpeople(el, info)
        case 'alltikzpeople*': render_alltikzpeople(el, info)
        case 'minipage': render_env_div(el, info, "latex-minipage", null)
        case 'multicols': render_multicols(el, info)

        // ---- theorem-like environments ----
        // check custom \newtheorem defs first (for shared counters, starred variants)
        case 'theorem': render_theorem_like(el, info, "Theorem", "theorem")
        case 'lemma': render_theorem_like(el, info, "Lemma", "lemma")
        case 'corollary': render_theorem_like(el, info, "Corollary", "corollary")
        case 'proposition': render_theorem_like(el, info, "Proposition", "proposition")
        case 'definition': render_theorem_like(el, info, "Definition", "definition")
        case 'example': render_theorem_like(el, info, "Example", "example")
        case 'remark': render_theorem_like(el, info, "Remark", "remark")
        case 'proof': render_proof_env(el, info)

        // ---- boxes ----
        case 'fbox': boxes.render_fbox(render_children(el, 0, info))
        case 'mbox': boxes.render_mbox(render_children(el, 0, info))
        case 'frame': boxes.render_frame(render_children(el, 0, info))
        case 'makebox': boxes.render_makebox(el, render_children_skip_brack(el, info))
        case 'framebox': boxes.render_framebox(el, render_children_skip_brack(el, info))
        case 'parbox': boxes.render_parbox(el, render_children_skip_brack(el, info))
        case 'raisebox': boxes.render_raisebox(el, render_children_skip_brack(el, info))
        case 'llap': boxes.render_llap(render_children(el, 0, info))
        case 'rlap': boxes.render_rlap(render_children(el, 0, info))
        case 'smash': boxes.render_smash(render_children(el, 0, info))
        case 'phantom': boxes.render_phantom(render_children(el, 0, info))
        case 'hphantom': boxes.render_hphantom(render_children(el, 0, info))
        case 'vphantom': boxes.render_vphantom(render_children(el, 0, info))

        // ---- tables ----
        case 'table': render_table_env(el, info)
        case 'tabular': render_tabular(el, info)
        case 'longtable': render_tabular(el, info)
        case 'tabularx': render_tabular(el, info)
        case 'xltabular': render_tabular(el, info)

        // ---- spacing ----
        case 'linebreak': <br>
        case 'newline': <br>
        case 'hspace': spacing.render_hspace_el(el)
        case 'vspace': spacing.render_vspace_el(el)
        case 'hrule': <hr>
        case 'newpage': <hr class: "latex-pagebreak">
        case 'clearpage': <hr class: "latex-pagebreak">
        case 'pagebreak': <hr class: "latex-pagebreak">
        case 'hfill': <span class: "latex-hfill">
        case 'hfil': <span class: "latex-hfill">
        case 'dotfill': <span class: "latex-dotfill">
        case 'hrulefill': <span class: "latex-hrulefill">
        case 'rule': boxes.render_rule(el)
        case 'centerline': <div class: "latex-centerline", for c in render_children(el, 0, info) { c }>
        case 'indent': <span class: "latex-indent">
        case 'cleardoublepage': <hr class: "latex-pagebreak">
        // with a bibliography, the citation handler takes \nocite; without one it has no effect
        case 'nocite': null
        case 'twocolumn': render_column_switch(el, info)
        case 'onecolumn': render_column_switch(el, info)
        case 'markboth': render_marks(el)
        case 'markright': render_marks(el)
        case 'bigskip': spacing.render_bigskip_el()
        case 'medskip': spacing.render_medskip_el()
        case 'smallskip': spacing.render_smallskip_el()
        case 'bigbreak': spacing.render_bigbreak_el()
        case 'medbreak': spacing.render_medbreak_el()
        case 'smallbreak': spacing.render_smallbreak_el()
        case 'quad': spacing.render_quad_el()
        case 'qquad': spacing.render_qquad_el()
        case 'enspace': spacing.render_enspace_el()
        case '/': spacing.render_italcorr_el()
        case 'slash': spacing.render_slash_el()
        case 'thinspace': spacing.render_thinspace_el()
        case 'negthinspace': spacing.render_negthinspace_el()
        case 'noindent': spacing.render_noindent_el()

        // ---- cross references ----
        case 'label': render_label(el, info)
        case 'ref': render_ref(el, info)
        case 'pageref': render_pageref(el, info)
        case 'thepage': <span class: "latex-page-number">
        case 'leftmark': <span class: "latex-chapter-mark">
        case 'rightmark': <span class: "latex-section-mark">
        case 'autoref': render_autoref(el, info)
        case 'nameref': render_nameref(el, info)
        case 'href': render_href(el, info)
        case 'url': render_url(el)
        case 'cite': render_cite(el, info)

        // ---- bibliography ----
        case 'thebibliography': render_bibliography(el, info)
        case 'bibitem': null
        case 'addbibresource': if (info.biblatex_context.processor == "csl" or registry.active(info.packages, "biblatex")) null else render_generic_default(el, info)
        case 'printbibliography': if (info.biblatex_context.processor == "csl" or registry.active(info.packages, "biblatex"))
            biblatex.render_bibliography(el, info.biblatex_context) else render_generic_default(el, info)
        case 'bibliography': if (info.biblatex_context != null)
            biblatex.render_bibliography(el, info.biblatex_context) else render_generic_default(el, info)
        case 'bibliographystyle': if (info.biblatex_context != null) null
            else render_generic_default(el, info)
        case 'refsection': if (info.biblatex_context.processor == "csl" or registry.active(info.packages, "biblatex"))
            render_children(el, 0, info) else render_generic_default(el, info)
        case 'refsegment': if (info.biblatex_context.processor == "csl" or registry.active(info.packages, "biblatex"))
            render_children(el, 0, info) else render_generic_default(el, info)

        // ---- footnotes ----
        case 'footnote': render_footnote(el, info)
        case 'footnotemark': render_footnotemark(el, info)
        // the text joins the footnote list (analysis); nothing renders in place
        case 'footnotetext': null

        // Captions render at their source position, preserving nested subfloat ownership.
        case 'caption': render_caption(el, info)
        case 'caption*': render_caption(el, info)
        case 'captionof': render_captionof(el, info)
        case 'subfigure': render_subfloat(el, info, "subfigure")
        case 'subtable': render_subfloat(el, info, "subtable")
        case 'subref': render_subref(el, info)
        case 'pdfbookmark': hyperref.explicit_bookmark(el, info.hyperref_settings)

        // ---- special characters ----
        case 'control_symbol': render_control_symbol(el)
        case 'controlspace_command': " "
        case 'space_cmd': " "
        case 'char_command': render_char_command(el)
        case 'caret_char': render_caret_char(el)

        // ---- groups ----
        case 'curly_group': render_group(el, info)
        case 'brack_group': render_group(el, info)
        case 'group': render_group(el, info)
        case 'text_group': render_group(el, info)

        // ---- symbol commands ----
        case 'LaTeX': render_latex_logo()
        case 'TeX': render_tex_logo()
        case 'LaTeXe': render_latex2e_logo()
        case 'today': if (info.date != null) info.date else "today"
        case 'ldots': "\u2026"
        case 'dots': "\u2026"
        case 'dag': "\u2020"
        case 'ddag': "\u2021"
        case 'copyright': "\u00A9"
        case 'pounds': "\u00A3"
        case 'textbackslash': "\\"
        case 'textasciitilde': "~"
        case 'textasciicircum': "^"
        case 'textbar': "|"
        case 'textless': "<"
        case 'textgreater': ">"
        case 'SS': "\u00A7"
        case 'P': "\u00B6"
        case 'ss': "\u00DF"
        case 'aa': "\u00E5"
        case 'AA': "\u00C5"
        case 'ae': "\u00E6"
        case 'AE': "\u00C6"
        case 'oe': "\u0153"
        case 'OE': "\u0152"
        case 'o': "\u00F8"
        case 'O': "\u00D8"
        case 'l': "\u0142"
        case 'L': "\u0141"
        case 'i': "\u0131"
        case 'j': "\u0237"

        // ---- character code commands ----
        case 'symbol': render_symbol_command(el)

        // ---- extended symbols (textgreek, textcomp, gensymb) ----
        default: render_extended_or_generic(el, info)
    }
}

// skip commands that produce null output
let SKIP_COMMANDS = {
    'newcommand': true, 'renewcommand': true, 'providecommand': true,
    'pgfmathsetmacro': true, 'pgfmathdeclarefunction': true,
    'newenvironment': true, 'renewenvironment': true,
    'setcounter': true, 'newcounter': true, 'addtocounter': true,
    'stepcounter': true, 'refstepcounter': true, 'comment': true,
    // page-builder hints with no effect on continuous or Radiant-paged output
    'nopagebreak': true, 'samepage': true, 'enlargethispage': true,
    'raggedbottom': true, 'flushbottom': true, 'sloppy': true, 'fussy': true,
    'vfill': true, 'vfil': true, 'null': true,
    // read by page_style.prepare
    'pagenumbering': true,
    // arbitrary material written into the .toc; only formatting is lost
    'addtocontents': true,
    'setlength': true, 'newlength': true,
    // picture sub-commands handled inside picture.render_picture
    'put': true, 'line': true, 'vector': true,
    'circle': true, 'oval': true, 'bezier': true, 'qbezier': true,
    'unitlength': true, 'linethickness': true, 'thinlines': true,
    'thicklines': true, 'multiput': true, 'circle*': true
}

// Fallback: check skip commands, extended symbol map, then macro, then generic
fn render_extended_or_generic(el, info) {
    let tag_str = string(name(el))
    // skip commands that are no-ops
    if (SKIP_COMMANDS[tag_str] == true) { null }
    // other specific commands
    else if (tag_str == "includegraphics") { render_includegraphics(el, info) }
    else if (amsmath.handles(tag_str, info.packages))
        { bold_math(amsmath.render_environment(el,
            util.lookup(info.amsmath_numbers, string(el.source_offset)), info.math_operators),
            info, true) }
    else if (tag_str == "marginpar") { render_marginpar(el, info) }
    else if (floats.kind_for_list(info.float_kinds, tag_str) != null) {
        render_float_list(info, floats.kind_for_list(info.float_kinds, tag_str)) }
    // a package's or \newfloat's float environment
    else if (floats.kind_of(info.float_kinds, tag_str) != null) {
        render_float(el, info, floats.kind_of(info.float_kinds, tag_str)) }
    // an environment \newmdenv defined
    else if (registry.active(info.packages, "mdframed") and
        util.setting_at(info.settings, "mdf-env:" ++ tag_str, el.source_offset) != null) {
        render_mdframed(el, info, util.parse_kv_options(
            util.setting_at(info.settings, "mdf-env:" ++ tag_str, el.source_offset))) }
    else { resolve_extended_or_generic(tag_str, el, info) }
}

fn resolve_extended_or_generic(tag_str, el, info) {
    let ext = sym.resolve_extended(tag_str)
    if (ext != null) { ext }
    else if (registry.active(info.packages, "amssymb")) {
        let glyph = amssymb.render_symbol(tag_str)
        if (glyph != null) glyph else render_generic(el, info)
    }
    else { render_generic(el, info) }
}

// ============================================================
// Children rendering — no state threading
// ============================================================

fn render_children(el, from_idx, info) {
    let args = util.command_args(el)
    let children = if (registry.active(info.packages, "paralist")) rejoin_inline_lists(args, 0, []) else args
    let n = len(children)
    if (from_idx >= n) { [] }
    else { collect_children(children, from_idx, n, [], info) }
}

// paralist's inline lists run into the text around them, but the parser closes the
// paragraph at every environment: text, list and text with no blank line between
// them become one paragraph again
fn rejoin_inline_lists(children, i, acc) {
    if (i >= len(children)) acc
    else {
        let child = children[i]
        let previous = if (len(acc) > 0) acc[len(acc) - 1] else null
        let inline = child is element and (contains(paralist.INLINE_NAMES, string(name(child))) or false)
        if (inline and joins(previous, true)) (
            let next = if (i + 1 < len(children)) children[i + 1] else null,
            let tail = if (joins(next, false)) content(next) else [],
            rejoin_inline_lists(children, i + (if (joins(next, false)) 2 else 1),
                [*slice(acc, 0, len(acc) - 1), <paragraph for c in [*content(previous), child, *tail] { c }>]))
        else rejoin_inline_lists(children, i + 1, acc ++ [child])
    }
}

// a paragraph the list continues: the text before it does not end, or the text after
// it does not start, with a blank line
fn joins(node, before) {
    if (not (node is element) or name(node) != 'paragraph' or len(node) == 0) false
    else (let edge = if (before) node[len(node) - 1] else node[0],
        not (edge is string) or index_of(if (before) trim_start(edge) else edge, "\n\n") == null)
}

// render children but skip brack_group elements (used by box commands)
fn render_children_skip_brack(el, info) {
    let children = util.command_args(el)
    collect_children_skip_brack(children, 0, len(children), [], info)
}

fn collect_children_skip_brack(el, i, n, acc, info) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == "brack_group") {
            collect_children_skip_brack(el, i + 1, n, acc, info)
        } else {
            let rendered = render_node(child, info)
            let new_acc = if (rendered != null) acc ++ [rendered] else acc
            collect_children_skip_brack(el, i + 1, n, new_acc, info)
        }
    }
}

fn collect_children(el, i, n, acc, info) {
    if (i >= n) { acc }
    else {
        let child = el[i]
        // track \setlength{\unitlength}{value} updates for picture rendering
        let next_info = next_render_info(child, info)
        let rendered = render_node(child, next_info)
        let punct = if (i + 1 < n) autopunct_mark(child, el[i + 1], next_info) else null
        let later_info = after_render_info(child, next_info)
        if (punct != null) {
            // the punctuation moves in front of the note mark; the rest of the text follows it
            let rest = render_node(slice(el[i + 1], 1, len(el[i + 1])), later_info)
            let moved = acc ++ [punct] ++ (if (rendered != null) [rendered] else []) ++
                (if (rest != null) [rest] else [])
            collect_children(el, i + 2, n, moved, later_info)
        } else {
            let new_acc = if (rendered != null) acc ++ [rendered] else acc
            collect_children(el, i + 1, n, new_acc, later_info)
        }
    }
}

// biblatex's autopunct, on by default: in a note style, \autocite moves the
// punctuation that follows it in front of the note mark ("First.¹"), as the
// pinned biblatex references show (Lambda_Design_BibTeX_CSL §7.4)
fn autopunct_mark(child, next, info) {
    if (not (child is element) or not (next is string) or len(next) == 0) null
    else {
        let tag = string(name(child))
        let context = info.biblatex_context
        let note_style = context != null and context.processor == "csl" and
            context.compiled != null and context.compiled.style.class == "note"
        let mark = slice(next, 0, 1)
        if ((tag == "autocite" or tag == "autocites") and note_style and
            contains(".,;:!?", mark)) mark else null
    }
}

// Transparent paragraphs carry declarations into later block siblings.
fn after_render_info(child, info) {
    if (child is element and string(name(child)) == "paragraph")
        fold_paragraph_declarations(child, 0, info)
    else info
}

fn fold_paragraph_declarations(node, index, info) {
    if (index >= len(node)) info
    else fold_paragraph_declarations(node, index + 1,
        next_render_info(node[index], info))
}

fn next_render_info(child, info) {
    if (child is element and string(name(child)) == "captionsetup" and
        registry.active(info.packages, "caption")) {
        {*:info, caption_options: caption.update(info.caption_options, child, info.caption_type)}
    } else if (child is element and string(name(child)) == "definecolor") {
        let definition = color.definition(child)
        if (definition.color_name == "") info
        else {*:info, custom_colors: info.custom_colors ++
            [{key: definition.color_name, val: color.definition_value(definition)}]}
    } else if (child is element and string(name(child)) == "begingroup") {
        {*:info, font_groups: info.font_groups ++
            [{size: info.font_size, height: info.line_height,
              language: info.text_language}]}
    } else if (child is element and string(name(child)) == "endgroup") {
        let count = len(info.font_groups)
        if (count == 0) info
        else {
            let saved = info.font_groups[count - 1]
            {*:info, font_size: saved.size, line_height: saved.height,
                text_language: saved.language,
                font_groups: slice(info.font_groups, 0, count - 1)}
        }
    } else if (child is element and string(name(child)) == "fontsize") {
        let dimensions = font_size_arguments(child)
        if (dimensions == null) info
        else {*:info, font_size: dimensions.size, line_height: dimensions.height}
    } else if (child is element and string(name(child)) == "selectlanguage" and
                            registry.active(info.packages, "babel")) {
        let selected = trim(util.text_of(child))
        if (language.supported(selected)) {*:info, text_language: selected} else info
    } else if (child is element and string(name(child)) == "lstset" and
                            registry.active(info.packages, "listings")) {
        {*:info, listing_options: listings.merged(info.listing_options,
            listings.setting_options(child))}
    } else if (child is element and registry.active(info.packages, "setspace") and
        paragraph.declared_stretch(child, info.class_options) != null) {
        {*:info, line_stretch: paragraph.declared_stretch(child, info.class_options)}
    } else if (child is element and string(name(child)) == "boldmath") {
        {*:info, math_bold: true}
    } else if (child is element and string(name(child)) == "unboldmath") {
        {*:info, math_bold: false}
    } else if (child is element and string(name(child)) == "mathversion") {
        let version = trim(util.text_of(child))
        if (version == "bold") {*:info, math_bold: true}
        else if (version == "normal") {*:info, math_bold: false} else info
    } else if (child is element and string(name(child)) == "setlength"
                            and child.length_name == "unitlength"
                            and child.length_value != null) {
        {*:info, unitlength: trim(child.length_value)}
    } else info
}

fn font_size_arguments(el) {
    let args = util.command_args(el)
    if (len(args) < 2) null
    else {
        let size = util.css_dimension(util.text_of(args[0]))
        let height = util.css_dimension(util.text_of(args[1]))
        if (size == null or height == null) null
        else {size: size, height: height}
    }
}

fn render_page_style(el, info) {
    let style = trim(util.text_of(el))
    if (style == "empty" or style == "plain") null
    else if (registry.active(info.packages, "fancyhdr") and
        style == info.running_style.style) null
    else util.unsupported_element("latex", "Unsupported page style " ++ style,
        el.source_offset)
}

// ============================================================
// Document structure
// ============================================================

fn render_document(el, info) {
    let doc_body = util.find_child(el, 'document')
    if (doc_body != null) { render_body(doc_body, info) }
    else { document_article(render_children(el, 0, info), info) }
}

fn render_body(el, info) => document_article(render_children(el, 0, info), info)

fn document_article(items, info) {
    <article class: document_class(info),
        lang: language.code(info.language),
        style: document_style(info),
        if (info.running_style.header != null)
            render_running_band(info.running_style.header, "header", info)
        for c in items { c }
        if (info.running_style.footer != null)
            render_running_band(info.running_style.footer, "footer", info)
    >
}

fn running_slot(raw, info) {
    if (raw == null or trim(raw) == "") []
    else {
        let ast = parse(raw, "latex") ^ { null }
        if (ast == null) [util.unsupported_element("fancyhdr",
            "Header/footer content could not be parsed", info.running_style.offset)]
        else render_children(ast, 0, info)
    }
}

pub fn running_stylesheet(info) =>
    page_style.stylesheet(info.running_style, (raw) => running_slot(raw, info))

pub fn running_diagnostics(info, target) =>
    page_style.target_issues(info.running_style, (raw) => running_slot(raw, info), target) ^ {
        [util.diagnostic("running-content-error", "fancyhdr", null, ^.message,
            info.running_style.offset)]
    }

fn render_running_band(slots, band, info) {
    let tag = if (band == "header") "latex-running-header"
        else "latex-running-footer";
    <div class: tag,
        for (slot in ["left", "center", "right"])
            <div class: "latex-running-" ++ slot ++ " latex-running-" ++ band ++ "-" ++ slot,
                for (item in running_slot(slots[slot], info)) item>
    >
}

fn document_style(info) {
    let background = if (info.page_color == null) ""
        else "background-color:" ++ info.page_color ++ ";"
    let selected_family = macros.find_macro(info.macros, "familydefault")
    let sans_default = selected_family != null and selected_family.source != null and
        trim(selected_family.source) == "\\sfdefault"
    // a font package replaces the main family, or the sans family when it is the default
    let main_css = registry.font_css(info.packages, "main")
    let sans_css = if (sans_default) registry.font_css(info.packages, "sans") else null
    let font = if (info.polyglossia != null and info.language == "thai")
        language.font_style(info.polyglossia, "thaifont", "serif") ++
            "word-break:normal;overflow-wrap:break-word;"
        else if (main_css != null) "font-family:" ++ main_css ++ ";"
        else if (sans_css != null) "font-family:" ++ sans_css ++ ";"
        else ""
    let spread = if (info.line_spread == null) ""
        else "line-height:" ++ string(font_decl.line_height_for(info.line_spread)) ++ ";"
    let style = background ++ font ++ spread
    if (style == "") null else style
}

// the article's classes; the twocolumn class option sets the document in two columns
fn document_class(info) {
    "latex-document latex-" ++ info.docclass ++
        (if (two_column(info)) " latex-twocolumn" else "")
}

fn two_column(info) => info.class_options != null and (contains(info.class_options, "twocolumn") or false)

// \twocolumn[header]: the header spans both columns of a twocolumn document;
// switching column count mid-document is not represented
fn render_column_switch(el, info) {
    let tag = string(name(el))
    if (tag == "twocolumn" and two_column(info)) {
        let header = util.find_child(el, 'brack_group')
        if (header == null) null
        else <div class: "latex-span-all", for c in render_children(header, 0, info) { c }>
    }
    else util.unsupported_element("latex",
        "Changing the column count mid-document is not supported; use the twocolumn class option",
        el.source_offset)
}

fn render_foreignlanguage(el, info) {
    let args = util.command_args(el)
    let selected = if (len(args) > 0) trim(util.text_of(args[0])) else ""
    if (not registry.active(info.packages, "babel") or not language.supported(selected))
        render_generic_default(el, info)
    else <span lang: language.code(selected),
        for (arg in slice(args, 1, len(args))) {
            render_node(arg, {*:info, text_language: selected})
        }>
}

// Polyglossia's environment argument is the first group inside its opening paragraph.
fn render_otherlanguage(el, info) {
    let opening = if (len(el) > 0) el[0] else null
    let first = if (opening is element and string(name(opening)) == "paragraph" and
        len(opening) > 0) opening[0] else opening
    let selected = if (first is element and string(name(first)) == "curly_group")
        trim(util.text_of(first)) else ""
    if (not language.supported(selected))
        util.unsupported_element("polyglossia", "Unsupported otherlanguage selection",
            el.source_offset)
    else {
        let local_info = {*:info, text_language: selected}
        let opening_items = if (opening is element and string(name(opening)) == "paragraph")
            render_children(opening, 1, local_info) else []
        let rest = render_children(el, 1, local_info)
        let font = if (selected == "english")
            language.font_style(info.polyglossia, "englishfont", "serif") else null;
        <div lang: language.code(selected), style: font,
            if (len(opening_items) > 0) { <p for c in opening_items { c }> }
            for c in rest { c }
        >
    }
}

fn render_textenglish(el, info) {
    let args = util.command_args(el)
    let sans = len(args) > 0 and args[0] is element and
        string(name(args[0])) == "sffamily"
    let selected = "english"
    let local_info = {*:info, text_language: selected}
    let items = render_children(el, if (sans) 1 else 0, local_info)
    let font = language.font_style(info.polyglossia,
        if (sans) "sffamilylatin" else "englishfont",
        if (sans) "sans-serif" else "serif");
    <span lang: language.code(selected), style: font, for c in items { c }>
}

fn render_textporson(el, info) {
    let args = util.command_args(el)
    if (not all([for (arg in args) arg is string]))
        util.unsupported_element("gfsporson",
            "textporson currently requires plain LGR text", el.source_offset)
    else <span class: "latex-gfsporson", lang: "el",
        style: "font-family:" ++ registry.font_css(info.packages, "greek"),
        language.lgr_to_unicode(join(args, ""), info.greek_polytonic)>
}

fn render_maketitle(info) {
    // \thanks notes in the title block are marked with footnote symbols, in source order
    let thanks = [for (el in [info.title_el, info.author_el, info.date_el],
        note in thanks_notes(el)) note]
    let title_info = {*:info, thanks_marks: [for (i, note in thanks)
        {key: string(note.source_offset), val: util.fnsymbol(i + 1)}]}
    // titling styles each part from its \pre…/\post… code and fills the hook slots
    let titling = registry.active(info.packages, "titling")
    let styles = [for (part in frontmatter.TITLING_PARTS)
        if (titling) frontmatter.part_style(info.settings, part, info.custom_colors) else null]
    let title_div = render_maketitle_div("title", info.title_el, info.title, title_info, styles[0])
    let author_div = if (registry.active(info.packages, "authblk") and len(info.authblk or []) > 0)
        render_authblk(title_info)
        else render_maketitle_div("author", info.author_el, info.author, title_info, styles[1])
    let date_div = render_maketitle_div("date", info.date_el, info.date, title_info, styles[2])
    let hooks = [for (slot in ["a", "b", "c", "d"])
        if (titling) title_hook(frontmatter.hook(info, slot), info) else null]
    let parts = [hooks[0], title_div, hooks[1], author_div, hooks[2], date_div, hooks[3]] |: (~ != null)
    let drop = if (titling) util.setting_at(info.settings, "length:droptitle", null) else null;
    <header class: "latex-title", style: if (drop == null) null else "margin-top:" ++ drop,
        for c in parts { c }
        if (len(thanks) > 0) {
            <div class: "latex-thanks",
                for (i, note in thanks)
                    <p <sup util.fnsymbol(i + 1)> " " for c in render_children(note, 0, info) { c }>>
        }
    >
}

fn thanks_notes(node) {
    if (not (node is element)) []
    else if (string(name(node)) == "thanks") [node]
    else [for (child in node, note in thanks_notes(child)) note]
}

fn render_thanks_ref(el, info) {
    let mark = if (info.thanks_marks == null) null
        else util.lookup(info.thanks_marks, string(el.source_offset))
    if (mark == null) util.unsupported_element("latex",
        "\\thanks belongs in \\title, \\author or \\date", el.source_offset)
    else <sup class: "latex-thanks-ref", mark>
}

// render a preamble field div (title/author/date) using element children if available
// `style` is titling's {css, before, after} for the part, or null
fn render_maketitle_div(css_class, el, fallback_text, info, style = null) {
    let rendered = if (el != null and el is element) render_children(el, 0, info) else []
    let items = if (len(rendered) > 0) rendered else if (fallback_text != null) [fallback_text] else []
    if (len(items) == 0) null
    else if (style == null) <div class: css_class, for c in items { c }>
    else <div class: css_class, style: if (style.css == "") null else style.css,
        if (style.before == "") null else style.before; for c in items { c }
        if (style.after == "") null else style.after>
}

// a \maketitlehook… slot the document filled
fn title_hook(body, info) {
    if (body == null or len(body) == 0) null
    // a group, so a leading declaration (\centering) applies to the hook's text
    else <div class: "latex-title-hook", render_node(body, info)>
}

// authblk's authors and affiliations. With marks, the names are joined as authblk
// joins them and each affiliation follows on its own line with its mark; without
// them, the authors before each run of affiliations form a block with them.
fn render_authblk(info) {
    let entries = info.authblk
    let fonts = frontmatter.fonts(info, registry.options_for(info.packages, "authblk"))
    let seps = frontmatter.separators(info)
    let marked = any([for (e in entries) len(frontmatter.marks(e.el)) > 0])
    if (marked) {
        let names = [for (e in entries where e.kind == "author")
            [*render_children_skip_brack(e.el, info), mark_sup(frontmatter.marks(e.el))]];
        <div class: "author latex-authblk",
            <div class: "latex-authors", style: if (fonts.author == "") null else fonts.author,
                for c in frontmatter.joined(names, seps) { c }>
            <div class: "latex-affiliations", style: if (fonts.affil == "") null else fonts.affil,
                for (e in entries where e.kind == "affil")
                    <div class: "latex-affil", mark_sup(frontmatter.marks(e.el)); " ";
                        for c in render_children_skip_brack(e.el, info) { c }>>>
    }
    else <div class: "author latex-authblk",
        for (block in frontmatter.blocks(entries))
            <div class: "latex-authblock",
                <div class: "latex-authors", style: if (fonts.author == "") null else fonts.author,
                    for c in frontmatter.joined([for (e in block.authors) render_children_skip_brack(e.el, info)], seps) { c }>
                for (e in block.affils)
                    <div class: "latex-affil", style: if (fonts.affil == "") null else fonts.affil,
                        for c in render_children_skip_brack(e.el, info) { c }>>>
}

fn mark_sup(marks) => if (len(marks) == 0) null else <sup class: "latex-affil-mark", join(marks, ",")>

// titling's \thetitle, \theauthor and \thedate print what \title, \author and \date gave
fn render_title_part(el, info) {
    let part = slice(string(name(el)), 3, len(string(name(el))))
    let source = if (part == "title") info.title_el else if (part == "author") info.author_el else info.date_el;
    <span class: "latex-the" ++ part, for c in (if (source == null) [] else render_children(source, 0, info)) { c }>
}

// appendix's environment: the anchor of its toc option's contents head, the page
// option's title, then the appendices
fn render_appendices(el, info) {
    let options = registry.options_for(info.packages, "appendix")
    let page = if (options != null and util.option_enabled(options.page)) render_appendix_page() else null;
    <div class: "latex-appendices", render_contents_anchor(el, info); page;
        for c in render_children(el, 0, info) { c }>
}

// \appendixpage: the appendices' title, set like a part
fn render_appendix_page() => <h1 class: "latex-appendix-page", appendix.HEAD>

// "Appendix " before a heading's or contents entry's number when appendix's title
// or titletoc option named it
fn appendix_name(names, place) {
    if (names != null and names[place]) appendix.NAME ++ " " else ""
}

// the anchor a ToC or list entry from \addcontentsline links to
fn render_contents_anchor(el, info) {
    let id = util.lookup(info.contents_anchors, string(el.source_offset))
    if (id == null) null else <a id: id>
}

fn render_toc(info) {
    if (len(info.headings) == 0) { null }
    else {
        let items = [for (h in info.headings) toc_item(h, info)];
        <nav class: "latex-toc",
            list_title(contents.title(info, "contentsname", "Contents", "toc"), info, null);
            <ul for item in items { item }>
        >
    }
}

// a contents entry; tocloft's macros for its level style it
fn toc_item(h, info) {
    let style = if (registry.active(info.packages, "tocloft"))
        contents.entry_style(info, contents.entry_name(h.level)) else null
    let shown = if (h.number == null) null
        else appendix_name(util.lookup(info.heading_names, h.id), "toc") ++
            language.display_number(h.number, info.polyglossia);
    <li class: "toc-l" ++ (h.level), style: if (style == null or style.css == "") null else style.css,
        <a href: "#" ++ h.id,
            if (shown == null) null
            else if (style == null) <span class: "sec-num", shown>
            else (<span class: "sec-num", style: style.number_css, style.before ++ shown ++ style.after>,
                if (style.after_box == "") null else style.after_box);
            h.text
        >
    >
}

// a list's title: a renamed \contentsname or \list…name, a \listof{type}{title}
// title, tocloft's title font and after-title text
fn list_title(named, info, given) {
    let text = if (given != null) [given] else if (named.body != null) render_children(named.body, 0, info)
        else [named.text];
    <div class: "toc-title", style: named.css, for c in text { c }
        if (named.after == "") null else named.after>
}

// ============================================================
// Headings
// ============================================================

fn render_heading(el, info, html_level) {
    let title_el = el.title
    let title_text = if (title_el != null) util.text_of(title_el) else ""
    let title_id = util.slugify(title_text)

    // look up pre-computed number
    let sec_num = util.lookup(info.heading_nums, title_id)

    // render title children
    let title_children =
        if (title_el != null and title_el is element) render_children(title_el, 0, info)
        else if (title_text != "") [title_text]
        else []

    let names = util.lookup(info.heading_names, title_id)
    let num_span = if (sec_num != null) <span class: "sec-num",
        appendix_name(names, "title") ++ language.display_number(sec_num, info.polyglossia) ++ " "> else null
    // \texorpdfstring gives outlines its PDF branch
    let outline_text = if (title_el != null and util.find_descendant(title_el, 'texorpdfstring') != null)
        util.pdf_text_of(title_el) else util.text_of(title_children)
    let outline_title = if (not hyperref.outlines_enabled(info.hyperref_settings)) null
        else (if (info.hyperref_settings.bookmarksnumbered == "true" and sec_num != null)
            string(sec_num) ++ " " else "") ++ outline_text
    let running = info.running_style.header != null or info.running_style.footer != null
    // titlesec and sectsty restyle the heading, its label and its title
    let titlesec = registry.options_for(info.packages, "titlesec")
    let sectsty = registry.active(info.packages, "sectsty")
    let style = if (titlesec == null and not sectsty) null
        else titles.heading_style(info, titlesec, sectsty, titles.level_of(string(name(el))),
            if (sec_num == null) null else language.display_number(sec_num, info.polyglossia),
            contains(info.appendix_headings or [], title_id) or false, el.source_offset)
    if (style == null) build_heading_el(html_level, title_id, num_span, title_children, outline_title, running)
    else {
        let label_span = if (style.label == null) num_span
            else <span class: "sec-num", style: style.label.css ++ (if (style.label.block) "display:block;" else "") ++
                (if (style.label.sep == null) "" else (if (style.label.block) "margin-bottom:" else "margin-right:") ++
                    style.label.sep ++ ";"), style.label.text>
        let titled = if (style.title_css == "") title_children
            else [<span class: "latex-title-text", style: style.title_css, for c in title_children { c }>]
        build_heading_el(html_level, title_id, label_span, titled, outline_title, running,
            if (style.css == "") null else style.css, style.indent_after)
    }
}

// `css` is a titlesec or sectsty style; `indent_after` marks a heading whose next
// paragraph is indented
fn build_heading_el(level, id, num_span, children, outline_title, running_marks, css = null, indent_after = false) {
    let marks = if (not running_marks) null
        else if (level == 1) "string-set:latex-chapter content(text);"
        else if (level == 2) "string-set:latex-section content(text);" else null
    let attributes = {id: id, 'data-pdf-outline-title': outline_title,
        'data-pdf-outline-level': if (outline_title != null) level - 1 else null,
        class: if (indent_after) "latex-indent-after" else null,
        style: if (marks == null) css else if (css == null) marks else marks ++ css}
    let items = [if (num_span != null) num_span, for (child in children) child]
    match level {
        case 1: <h1 *:attributes, for (item in items) item>
        case 2: <h2 *:attributes, for (item in items) item>
        case 3: <h3 *:attributes, for (item in items) item>
        case 4: <h4 *:attributes, for (item in items) item>
        case 5: <h5 *:attributes, for (item in items) item>
        default: <h6 *:attributes, for (item in items) item>
    }
}

// ============================================================
// Paragraphs
// ============================================================

// a setspace declaration earlier in the group spaces the paragraphs after it
fn render_paragraph(el, info) {
    let rendered = render_paragraph_content(el, info)
    if (info.line_stretch == null or rendered == null) rendered
    else <div class: "latex-spacing", style: paragraph.stretch_css(info.line_stretch),
        for c in (if (rendered is array) rendered else [rendered]) { c }>
}

fn render_paragraph_content(el, info) {
    // check for parbreak symbols inside paragraph — split into multiple <p> tags
    if (has_parbreak(el, 0, len(el))) {
        let groups = split_at_parbreaks(el, info)
        if (len(groups) == 0) null
        else if (len(groups) == 1) groups[0]
        else groups
    }
    else {
        // check for leading font/alignment declaration
        let decl = find_leading_decl(el, 0, len(el), info)
        if (decl != null) { render_paragraph_with_decl(el, info, decl) }
        else {
            let items = render_children_with_color(el, info)
            if (len(util.trim_children(items)) == 0) { null }
            else if (has_block_child(items)) {
                let parts = split_around_blocks(items)
                if (len(parts) == 1) parts[0]
                else parts
            }
            else if (starts_with_noindent(el)) {
                <p class: fill_class(items), style: "text-indent:0", for c in items { c }> }
            else { <p class: fill_class(items), for c in items { c }> }
        }
    }
}

// \noindent at the start of a paragraph removes its indentation
fn starts_with_noindent(el) {
    let leading = [for (item in util.command_args(el)
        where not (item is string and trim(item) == "")) item]
    len(leading) > 0 and leading[0] is element and string(name(leading[0])) == "noindent"
}

fn has_parbreak(el, i, n) {
    if (i >= n) false
    else if (el[i] is symbol and string(el[i]) == "parbreak") true
    else has_parbreak(el, i + 1, n)
}

fn split_at_parbreaks(el, info) {
    split_parbreaks_rec(el, 0, len(el), [], [], info)
}

fn split_parbreaks_rec(el, i, n, current_children, acc, info) {
    if (i >= n) {
        // flush remaining children as a paragraph
        if (len(current_children) > 0) {
            let items = current_children |: (~ != null)
            if (len(items) > 0) acc ++ flush_as_paragraph(items)
            else acc
        }
        else acc
    }
    else if (el[i] is symbol and string(el[i]) == "parbreak") {
        // flush current group and start new one
        if (len(current_children) > 0) {
            let items = current_children |: (~ != null)
            if (len(items) > 0) {
                split_parbreaks_rec(el, i + 1, n, [], acc ++ flush_as_paragraph(items), info)
            }
            else { split_parbreaks_rec(el, i + 1, n, [], acc, info) }
        }
        else { split_parbreaks_rec(el, i + 1, n, [], acc, info) }
    }
    else {
        let child = el[i]
        // track \setlength{\unitlength}{value} updates
        let next_info = next_render_info(child, info)
        let rendered = render_node(child, next_info)
        split_parbreaks_rec(el, i + 1, n, current_children ++ [rendered], acc,
            after_render_info(child, next_info))
    }
}

// CSS has no inline fill, so a line holding \hfill, \dotfill or \hrulefill lays out
// as a flex row in which the fills grow
fn fill_class(items) =>
    if (any([for (item in items) item is element and
        (item.class == "latex-hfill" or item.class == "latex-dotfill" or
         item.class == "latex-hrulefill")])) "latex-fill-line" else null

// \markboth{left}{right}, \markright{right}: set the running-head strings that
// headings set, on a hidden element (Radiant's paged path reads hidden assignments)
fn render_marks(el) {
    let args = util.command_args(el)
    let texts = [for (arg in args) trim(util.text_of(arg))]
    let assignments = if (string(name(el)) == "markboth" and len(texts) >= 2)
        ["latex-chapter " ++ page_style.quoted_content(texts[0]),
         "latex-section " ++ page_style.quoted_content(texts[1])]
        else if (len(texts) >= 1) ["latex-section " ++ page_style.quoted_content(texts[len(texts) - 1])]
        else [];
    <span class: "latex-mark", style: "display:none;string-set:" ++ join(assignments, ",")>
}

fn render_paragraph_with_decl(el, info, decl) {
    let decl_tag = string(name(decl))
    let items = render_children_with_color(el, info)
    if (len(items) == 0) { null }
    else if (font_decl.is_font_decl(decl_tag)) {
        let style = font_decl.decl_css(decl);
        if (style == null) <p for c in font_decl.wrap_font_decl(decl, items) { c }>
        else <p style: style, for c in items { c }>
    }
    else if (color.is_color_decl(decl_tag)) {
        <p for c in items { c }>
    }
    else <p style: paragraph_decl_css(decl, info), for c in items { c }>
}

fn split_around_blocks(items) {
    split_blocks_rec(items, 0, len(items), [], [])
}

fn split_blocks_rec(items, i, n, current, acc) {
    if (i >= n) { flush_inline(current, acc) }
    else {
        let item = items[i]
        if (is_block_element(item)) {
            let flushed = flush_inline(current, acc)
            split_blocks_rec(items, i + 1, n, [], flushed ++ [item])
        } else {
            split_blocks_rec(items, i + 1, n, current ++ [item], acc)
        }
    }
}

// flush items as paragraph, but extract block elements (like equation divs)
// so they don't get wrapped in <p> tags (which is invalid HTML)
fn flush_as_paragraph(items) {
    if (len(util.trim_children(items)) == 0) []
    else if (has_block_child(items)) split_around_blocks(items)
    else [<p for c in items { c }>]
}

fn flush_inline(items, acc) {
    // whitespace around block output must not create paragraph boxes between subfloats.
    if (len(util.trim_children(items)) == 0) acc
    else acc ++ [<p for c in items { c }>]
}

fn has_block_child(items) {
    check_block_rec(items, 0, len(items))
}

fn check_block_rec(items, i, n) {
    if (i >= n) false
    else if (is_block_element(items[i])) true
    else check_block_rec(items, i + 1, n)
}

fn is_block_element(item) {
    if (item is element) is_block_tag(string(name(item)))
    else false
}

fn is_block_tag(tag) {
    match tag {
        case "header": true
        case "div": true
        case "section": true
        case "article": true
        case "nav": true
        case "aside": true
        case "main": true
        case "ul": true
        case "ol": true
        case "dl": true
        case "table": true
        case "figure": true
        case "blockquote": true
        case "pre": true
        case "h1": true
        case "h2": true
        case "h3": true
        case "h4": true
        case "h5": true
        case "h6": true
        default: false
    }
}

// ============================================================
// Text styling
// ============================================================

fn render_styled(el, info, html_tag) {
    let items = render_children(el, 0, info)
    match html_tag {
        case "b": <strong for c in items { c }>
        case "i": <em for c in items { c }>
        case "em": <em for c in items { c }>
        case "u": <u for c in items { c }>
        default: <span for c in items { c }>
    }
}

fn render_code(el, info) {
    let items = render_children(el, 0, info);
    <code class: "latex-code", for c in items { c }>
}

fn render_verb(el) {
    let t = util.text_of(el);
    <code class: "latex-code", t>
}

// verb_command: raw token text like "\verb|hello|" — extract content between delimiters
fn render_verb_command(el) {
    let raw = util.text_of(el)
    // format: \verb<delim><content><delim> — skip "\verb" (5 chars), then delimiter
    let body = slice(raw, 5, len(raw))
    if (len(body) >= 2) {
        let content = slice(body, 1, len(body) - 1);
        <code class: "latex-code", content>
    }
    else { <code class: "latex-code", body> }
}

fn render_font(el, info, css_class) {
    let items = render_children(el, 0, info);
    <span class: css_class, for c in items { c }>
}

fn render_size(el, info, cmd_name) {
    let size = sym.get_font_size(cmd_name)
    let items = render_children(el, 0, info)
    if (size != null) { <span style: "font-size:" ++ size, for c in items { c }> }
    else { <span for c in items { c }> }
}

fn render_accent(el, info) {
    let cmd = el.command
    let cmd_name = if (cmd != null) cmd else ""
    let base = if (len(el) > 0) util.text_of(render_node(el[0], info)) else ""
    sym.resolve_diacritic(cmd_name, base)
}

fn render_math_env(el, info) {
    let env_name = util.attr_or(el, "name", "equation")
    bold_math(math_bridge.render_math_env_el(el, env_name), info, true)
}

fn render_numbered_equation(el, info) {
    let equation_number = util.lookup(info.equation_nums, string(el.source_offset))
    let rendered = if (registry.active(info.packages, "amsmath"))
        amsmath.render_environment(el,
            util.lookup(info.amsmath_numbers, string(el.source_offset)), info.math_operators)
    else if (equation_number == null) math_bridge.render_equation_el(el)
    else <div class: "latex-equation",
        math_bridge.render_equation_math(el);
        <span class: "latex-eq-number", amsmath.tag_text(string(equation_number), false, null)>
    >;
    bold_math(rendered, info, true)
}

fn bold_math(rendered, info, block) {
    if (not info.math_bold) rendered
    else if (block) <div class: "latex-boldmath", rendered>
    else <span class: "latex-boldmath", rendered>
}

// ============================================================
// Lists
// ============================================================

fn render_itemize(el, info) {
    let flat = util.flatten_paragraphs(el)
    let items = split_list_items(flat, info)
    let opts = if (registry.active(info.packages, "enumitem")) enumitem.options(el) else {};
    let list_style = enumitem.list_style(opts, "itemize");
    <ul class: "latex-itemize", style: if (list_style != "") list_style else null,
        for li in items { li }>
}

fn render_enumerate(el, info) {
    let flat = util.flatten_paragraphs(el)
    // the enumerate and paralist packages read [(a)] as a label template
    let tpl = if (el.options_raw == null or registry.active(info.packages, "enumitem") or
        not (registry.active(info.packages, "enumerate") or registry.active(info.packages, "paralist"))) null
        else paralist.template(el.options_raw)
    if (tpl != null) <ul class: "latex-list latex-enumerate",
        for (item in labelled_items(flat, info, "enumerate", tpl)) <li item.label; for c in item.body { c }>>
    else render_plain_enumerate(el, flat, info)
}

fn render_plain_enumerate(el, flat, info) {
    let items = split_list_items(flat, info)
    let opts = if (registry.active(info.packages, "enumitem")) enumitem.options(el) else {};
    let first = if (registry.active(info.packages, "enumitem") and el.source_offset != null)
        util.lookup(info.enumerate_starts, string(el.source_offset)) else enumitem.start(opts);
    let list_style = enumitem.list_style(opts, "enumerate");
    <ol class: if (enumitem.parenthesized_alpha(opts))
            "latex-enumerate latex-enumerate-alpha-paren" else "latex-enumerate",
        start: if (first != null) string(first) else null,
        style: if (list_style != "") list_style else null, for li in items { li }>
}

fn render_description(el, info) {
    let flat = util.flatten_paragraphs(el)
    let items = split_desc_items(flat, info)
    let opts = if (registry.active(info.packages, "enumitem")) enumitem.options(el) else {};
    let list_style = enumitem.list_style(opts, "description");
    <dl class: "latex-description", style: if (list_style != "") list_style else null,
        for item in items { item }>
}


fn is_item_node(child) {
    if (child is element) {
        if (string(name(child)) == "item") true else false
    } else { false }
}

// default_label(i) labels item i when it has no \item[label] of its own (the
// list environments); null leaves such items unlabelled
fn split_list_items(flat, info, default_label = null) {
    split_items_rec(flat, 0, len(flat), [], false, [], info, default_label)
}

fn has_list_content(items) {
    any([for (item in items) not (item is string and trim(item) == "")])
}

fn flush_list_item(current, custom, acc) {
    if (not has_list_content(current)) acc
    else if (custom) acc ++ [<li style: "list-style-type:none", for c in current { c }>]
    else acc ++ [<li for c in current { c }>]
}

fn split_items_rec(flat, i, n, current, custom, acc, info, default_label) {
    if (i >= n) flush_list_item(current, custom, acc)
    else if (is_item_node(flat[i])) {
        let flushed = flush_list_item(current, custom, acc)
        let own = get_item_custom_label(flat[i])
        let label = if (own != null) own else if (default_label != null) default_label(len(flushed)) else null
        split_items_rec(flat, i + 1, n, if (label != null) [label] else [],
            label != null, flushed, info, default_label)
    } else {
        let rendered = render_node(flat[i], info)
        let new_current = if (rendered != null) current ++ [rendered] else current
        split_items_rec(flat, i + 1, n, new_current, custom, acc, info, default_label)
    }
}

// tabbing as a grid: rows end at \\, and \= and \> separate cells, so the stops
// set in the first row become columns. A \kill row sets stops without printing.
// The other tab commands (\+ \- \< \' \`) reach the fallback and are reported.
fn render_tabbing(el, info) {
    let rows = split_nodes(util.flatten_paragraphs(el), 0, [], [],
        (node) => node is symbol and string(node) == "row_sep");
    <table class: "latex-tabbing",
        <tbody for (row in rows where not is_blank_row(row) and
            not any([for (node in row) node is element and string(name(node)) == "kill"]))
            <tr for (cell in split_nodes(row, 0, [], [], (node) => node is element and
                (string(name(node)) == "=" or string(name(node)) == ">")))
                <td for (node in cell, let item = render_node(node, info) where item != null) item>>>>
}

fn is_blank_row(row) => all([for (node in row) node is string and len(trim(node)) == 0])

// split a node list at separator nodes, which are dropped
fn split_nodes(nodes, i, current, acc, is_separator) {
    if (i >= len(nodes)) acc ++ [current]
    else if (is_separator(nodes[i])) split_nodes(nodes, i + 1, [], acc ++ [current], is_separator)
    else split_nodes(nodes, i + 1, current ++ [nodes[i]], acc, is_separator)
}

// Lists whose argument groups precede the first \item: list{label}{declarations},
// pifont's dinglist{n} and dingautolist{n}. label_for(groups, i) labels item i
// unless \item[x] gives one; trivlist passes null. Spacing declarations are not modelled.
fn render_generic_list(el, info, label_for) {
    let flat = util.flatten_paragraphs(el)
    let item_positions = [for (i, node in flat where is_item_node(node)) i]
    let start = if (len(item_positions) == 0) len(flat) else item_positions[0]
    let groups = [for (node in slice(flat, 0, start)
        where node is element and string(name(node)) == "curly_group") node];
    <ul class: "latex-list", for li in split_list_items(slice(flat, start, len(flat)), info,
        if (label_for == null) null else (i) => label_for(groups, i)) { li }>
}

// the list environment's label: its first argument, rendered
fn list_label(groups, info) =>
    if (len(groups) == 0) null
    else <span class: "latex-item-label", for c in render_children(groups[0], 0, info) { c }>

// get custom label from \item[label] as a rendered span, or null
fn get_item_custom_label(item_node) {
    let opt = util.find_child(item_node, 'brack_group')
    if (opt != null) { <span class: "latex-item-label", util.text_of(opt) ++ " "> }
    else { null }
}

fn split_desc_items(flat, info) {
    split_desc_rec(flat, 0, len(flat), [], null, [], info)
}

fn split_desc_rec(flat, i, n, acc, term, content, info) {
    if (i >= n) { flush_desc(acc, term, content) }
    else if (is_item_node(flat[i])) {
        let flushed = flush_desc(acc, term, content)
        let label_text = get_item_label(flat[i])
        let new_term = if (label_text != null) <dt label_text> else <dt>
        split_desc_rec(flat, i + 1, n, flushed, new_term, [], info)
    } else {
        let rendered = render_node(flat[i], info)
        let new_content = if (rendered != null) content ++ [rendered] else content
        split_desc_rec(flat, i + 1, n, acc, term, new_content, info)
    }
}

fn flush_desc(acc, term, content) {
    if (term != null) {
        let dd = if (has_list_content(content)) <dd for c in content { c }> else <dd>
        acc ++ [term, dd]
    } else if (has_list_content(content)) {
        acc ++ [<dd for c in content { c }>]
    } else { acc }
}

fn get_item_label(item_node) {
    let opt = util.find_child(item_node, 'brack_group')
    if (opt != null) { util.text_of(opt) }
    else {
        if (len(item_node) > 0) util.text_of(item_node) else null
    }
}

// ============================================================
// Environments
// ============================================================

fn render_env_block(el, info, html_tag, css_class) {
    let items = render_children(el, 0, info)
    match html_tag {
        case "blockquote": <blockquote class: css_class, for c in items { c }>
        default: <div class: css_class, for c in items { c }>
    }
}

fn render_env_div(el, info, css_class, style) {
    let items = render_children(el, 0, info)
    if (style != null) {
        <div class: css_class, style: style, for c in items { c }>
    } else {
        <div class: css_class, for c in items { c }>
    }
}

fn render_verbatim_env(el, info) {
    let t = util.text_of(el);
    <pre class: "latex-verbatim", style: font_size_style(info), <code t>>
}

fn render_abstract(el, info) {
    let items = render_children(el, 0, info);
    <div class: "latex-abstract",
        <div class: "abstract-title", "Abstract">
        for c in items { c }
    >
}

fn render_figure(el, info) => render_float(el, info, "figure")

// A float environment: its leading [placement] is dropped, its caption is
// numbered and named from the kind, and a non-plain float style shapes the frame.
fn render_float(el, info, kind) {
    let body = util.leading_option(el).children
    let classes = (if (kind == "figure") "latex-figure" else "latex-float latex-float-" ++ kind) ++
        float_style_class(info, kind) ++ spanning_class(el)
    let float_info = {*:info, caption_type: kind, caption_number: string(get_float_num(el, info, kind))}
    let items = if (kind == "algorithm" and registry.active(info.packages, "algorithm2e"))
        render_alg2e_body(body, float_info)
        else collect_children(body, 0, len(body), [], float_info);
    <figure class: classes, for c in items { c }>
}

// \listof{type}{title}
fn render_listof(el, info) {
    let args = [for (arg in util.command_args(el)) trim(util.text_of(arg))]
    if (len(args) < 2 or info.float_kinds[args[0]] == null)
        util.unsupported_element("float", "listof needs a float type and a title", el.source_offset)
    else render_float_list(info, args[0], args[1])
}

// figure* and table* span both columns of a twocolumn document
fn spanning_class(el) => if (ends_with(string(name(el)), "*")) " latex-span-all" else ""

fn render_caption(el, info) {
    if (info.caption_type == null) util.unsupported_element("caption",
        "Caption needs a figure or table; use captionof outside a float", el.source_offset)
    else caption.render(render_children_skip_brack(el, info), info.caption_type,
        info.caption_number, info.caption_options, el.starred == true, el.source_offset,
        info.float_kinds)
}

fn render_captionof(el, info) {
    let args = util.command_args(el)
    let kind = trim(util.raw_argument(el, "required", 0))
    if (not registry.active(info.packages, "caption") or len(args) < 2 or
        (kind != "figure" and kind != "table"))
        util.unsupported_element("caption", "captionof supports figure or table", el.source_offset)
    else caption.render([render_node(args[1], info)], kind,
        util.lookup(info.captionof_nums, string(el.source_offset)), info.caption_options,
        el.starred == true, el.source_offset)
}

fn render_subfloat(el, info, kind) {
    let width = util.raw_argument(el, "required", 0)
    let dimension = util.css_dimension(width)
    if (not registry.active(info.packages, "subcaption") or dimension == null or
        info.caption_type == null)
        util.unsupported_element("subcaption", "Subfloat needs a parent float and valid width", el.source_offset)
    else {
        let ordinal_text = util.lookup(info.subfloat_nums, string(el.source_offset))
        let children = content(el)
        let items = collect_children(children, 0, len(children), [], {*:info,
            caption_type: kind, caption_number: ordinal_text});
        <div class: "latex-subfloat", style: "display:inline-block;vertical-align:top;width:" ++ dimension ++ ";",
            for (item in items) item>
    }
}

fn render_subref(el, info) {
    let entry = util.lookup(info.labels, trim(util.text_of(el)))
    let subfloats = registry.active(info.packages, "subcaption") or
        registry.active(info.packages, "subfig") or registry.active(info.packages, "subfigure")
    if (not subfloats or entry == null or not starts_with(entry.type, "sub"))
        util.unsupported_element("subcaption", "Unresolved subfloat reference", el.source_offset)
    else <a href: "#" ++ entry.id, entry.subnumber>
}

// a float's number from analysis: by source offset, else by its text when offsets are absent
fn get_float_num(el, info, kind) {
    let exact = [for (entry in info.floats where entry.kind == kind and
        entry.offset == el.source_offset) entry.number]
    let text = trim(util.text_of(el))
    let by_text = [for (entry in info.floats where entry.kind == kind and
        entry.content == text) entry.number]
    if (len(exact) > 0) exact[0] else if (len(by_text) > 0) by_text[0] else 0
}

// \listoffigures, \listoftables: the captions of one float kind, linked when labelled
fn render_float_list(info, kind, title = null) {
    let desc = info.float_kinds[kind]
    let style = if (registry.active(info.packages, "tocloft")) contents.entry_style(info, desc.entry or kind) else null
    let entries = [for (entry in info.floats where entry.kind == kind and entry.caption != null,
        let parts = [if (entry.number == null) null
            else if (style == null) <span class: "sec-num", string(entry.number)>
            else <span class: "sec-num", style: style.number_css, style.before ++ string(entry.number) ++ style.after>,
            entry.caption])
        <li class: "toc-l2", style: if (style == null or style.css == "") null else style.css,
            if (entry.id != null) <a href: "#" ++ entry.id, for c in parts { c }>
            else parts
        >];
    <nav class: "latex-toc latex-list-of latex-" ++ (if (desc.list != null) desc.list else "listof-" ++ kind),
        list_title(contents.title(info, desc.name_macro, desc.list_title, desc.ext), info, title);
        <ul for item in entries { item }>
    >
}

// minitoc's \minitoc, \secttoc and \parttoc: the headings under the enclosing
// chapter, section or part, titled by \mtcsettitle or the title macro
fn render_minitoc(el, info) {
    let kind = contents.mini_kind(string(name(el)))
    let depth = util.setting_at(info.settings, "counter:" ++ kind.depth, el.source_offset)
    let entries = contents.mini_entries(info.headings, kind, if (depth == null) 2 else depth, el.source_offset)
    let set_title = util.setting_at(info.settings, "mtctitle:" ++ kind.key, el.source_offset)
    let renamed = macros.last_macro(info.macros, kind.title)
    let heading = if (set_title != null) [set_title]
        else if (renamed != null and renamed.body != null) render_children(renamed.body, 0, info)
        else [if (kind.key == "parttoc") "Table of Contents" else "Contents"]
    if (len(entries) == 0) null
    else <nav class: "latex-toc latex-minitoc",
        <div class: "toc-title", for c in heading { c }>
        <ul for (h in entries) toc_item(h, info)>>
}

// The parser keeps \begin{multicols}{n}[preface] as a `columns` attribute and a
// leading multicols_preface child; the preface spans all columns.
fn render_multicols(el, info) {
    let cols = if (el.columns != null) trim(el.columns) else "2"
    let preface = util.find_child(el, 'multicols_preface')
    let columns = <div class: "latex-multicols", style: "column-count:" ++ cols,
        for c in render_children(el, if (preface == null) 0 else 1, info) { c }
    >
    if (preface == null) columns
    else <div class: "latex-multicols-block",
        <div class: "latex-multicols-preface", for c in render_children(preface, 0, info) { c }>
        columns
    >
}

// ============================================================
// Theorem-like environments
// ============================================================

fn render_theorem_env(el, info, display_name, env_type) {
    let items = render_children(el, 0, info)
    let env_num = get_theorem_num(el, info, env_type)
    let heading = display_name ++ " " ++ (env_num) ++ ".";
    <div class: "latex-theorem latex-" ++ env_type,
        <strong class: "latex-theorem-head", heading>
        " "
        for c in items { c }
    >
}

// route theorem-like env through custom defs if available, else default numbered
fn render_theorem_like(el, info, display_name, env_type) {
    let thm_def = util.lookup(info.theorem_defs, env_type)
    if (thm_def != null) render_custom_theorem(el, info, thm_def)
    else render_theorem_env(el, info, display_name, env_type)
}

fn render_proof_env(el, info) {
    let items = render_children(el, 0, info);
    <div class: "latex-proof",
        <em class: "latex-proof-head", "Proof.">
        " "
        for c in items { c }
        <span class: "latex-qed", "\u25A1">
    >
}

fn get_theorem_num(el, info, env_type) {
    let thm_text = trim(util.text_of(el))
    find_theorem_num(info.theorems, thm_text, env_type, 0)
}

fn find_theorem_num(theorems, text, env_type, i) {
    if (i >= len(theorems)) 0
    else check_theorem_match(theorems, text, env_type, i)
}

fn check_theorem_match(theorems, text, env_type, i) {
    let entry = theorems[i]
    if (entry.kind == env_type and entry.content == text) entry.number
    else find_theorem_num(theorems, text, env_type, i + 1)
}

// ============================================================
// Tables
// ============================================================

// threeparttable (and measuredfigure): the caption and the notes are held to the
// table's width as table captions of the wrapper (css.ls)
fn render_threeparttable(el, info) {
    <div class: "latex-threeparttable", for c in render_children(el, 0, info) { c }>
}

// threeparttable's options, from the package and then the environment
fn tpt_option(info, local, key) {
    let package = registry.options_for(info.packages, "threeparttable")
    util.option_enabled(local[key]) or (package != null and util.option_enabled(package[key]))
}

// a note label: italic, superscript unless the online option sets it on the line
fn tnote_label(label, info, local = {}) {
    if (tpt_option(info, local, "online")) <span class: "latex-tnote", label>
    else <sup class: "latex-tnote", label>
}

// tablenotes: each \item[label] a note with a hanging label, or with para all in
// one paragraph; flushleft drops the hang and normal the smaller type
fn render_tablenotes(el, info) {
    let lead = util.leading_option(el)
    let local = util.parse_kv_options(lead.option)
    let parts = split_desc_items(util.flatten_paragraphs(<tablenotes for c in lead.children { c }>), info)
    let notes = [for (i, part in parts where name(part) == 'dt')
        {label: util.text_of(part),
         body: if (i + 1 < len(parts) and name(parts[i + 1]) == 'dd') trim_leading(content(parts[i + 1])) else []}]
    let style = (if (tpt_option(info, local, "normal")) "font-size:1em;" else "") ++
        (if (tpt_option(info, local, "flushleft")) "--latex-tnote-hang:0;" else "")
    let rows = if (tpt_option(info, local, "para"))
        [<p for c in [for (k, note in notes, item in [if (k > 0) "\u2003" else null,
            tnote_label(note.label, info, local), " ", *note.body]) item] { c }>]
        else [for (note in notes)
            <div class: "latex-tablenote", tnote_label(note.label, info, local); " "; for c in note.body { c }>];
    <div class: "latex-tablenotes", style: if (style == "") null else style, for row in rows { row }>
}

// items without the space before their first text
fn trim_leading(items) {
    if (len(items) == 0 or not (items[0] is string)) items
    else [trim_start(items[0]), *slice(items, 1, len(items))]
}

fn render_table_env(el, info) {
    let tab_num = get_float_num(el, info, "table")
    let items = render_children(el, 0, {*:info, caption_type: "table",
        caption_number: string(tab_num)});
    <div class: "latex-table-wrapper" ++ float_style_class(info, "table") ++ spanning_class(el),
        for c in items { c }
    >
}

// a non-plain float style (\floatstyle with \restylefloat or \newfloat) as a class
fn float_style_class(info, kind) {
    let style = info.float_kinds[kind].style
    if (style == null or style == "plain") "" else " latex-float-style-" ++ style
}


fn render_tabular(el, info) {
    let content = table_content(el)
    let colored = registry.active(info.packages, "colortbl")
    let ctx = {si: registry.active(info.packages, "siunitx"), dcolumn: registry.active(info.packages, "dcolumn"),
        colors: info.custom_colors, settings: info.settings, offset: el.source_offset}
    let col_spec = siunitx.measure_columns(content, parse_col_spec(el, ctx))
    // cells see the table: multicolumn preambles, \cellcolor, and colortbl's rule
    // color, which for vertical rules is the one in effect where the table starts
    let sections = table_sections(content, col_spec, {*:info, table_cell: true, table_columns: ctx,
        colortbl: colored,
        rule_color: if (colored) util.setting_at(info.settings, "arrayrulecolor", el.source_offset) else null})
    let caption = util.find_descendant(el, 'caption');
    <table class: "latex-tabular", style: table_width_style(el),
        if (caption != null) {
            <caption render_caption(caption, {*:info, caption_type: "table",
                caption_number: string(get_float_num(el, info, "table"))})>
        }
        if (len(sections.head) > 0) {
            <thead for row in sections.head { row }>
        }
        <tbody
            for row in sections.body { row }
        >
        if (len(sections.foot) > 0) {
            <tfoot for row in sections.foot { row }>
        }
    >
}

fn table_width_style(el) {
    if (el.width == null) null
    else if (trim(el.width) == "\\textwidth" or trim(el.width) == "\\linewidth")
        "width:100%;table-layout:fixed"
    else "width:" ++ trim(el.width) ++ ";table-layout:fixed"
}

fn render_makecell(el, info) {
    let args = [for (arg in util.command_args(el)
        where not (arg is element and name(arg) == 'brack_group')) arg];
    <span class: "latex-makecell",
        for (arg in args) {
            if (arg is symbol and string(arg) == "row_sep") { <br> }
            else render_node(arg, info)
        }
    >
}

fn render_diagonal_cell(el, direction) {
    let args = [for (arg in util.command_args(el)
        where not (arg is element and name(arg) == 'brack_group')) arg]
    let first = if (len(args) > 0) util.rich_text_of(args[0]) else ""
    let second = if (len(args) > 1) util.rich_text_of(args[1]) else ""
    let width = util.optional_raw(el)
    let css_width = if (width == null) "min-width:6em" else "min-width:" ++ width;
    <span class: "latex-diagonal-cell " ++ direction, style: css_width,
        <span class: "latex-diagonal-upper", second>
        <span class: "latex-diagonal-lower", first>
    >
}

fn table_marker(content, marker, index) {
    if (index >= len(content)) null
    else if (content[index] is element and
        string(name(content[index])) == marker) index
    else table_marker(content, marker, index + 1)
}

fn table_sections(content, col_spec, info) {
    let first_head = table_marker(content, "endfirsthead", 0)
    let head = table_marker(content, "endhead", 0)
    let foot = table_marker(content, "endfoot", 0)
    let last_foot = table_marker(content, "endlastfoot", 0)
    let head_end = if (first_head != null) first_head else head
    let body_begin = if (last_foot != null) last_foot + 1
        else if (foot != null) foot + 1
        else if (head != null) head + 1
        else if (first_head != null) first_head + 1 else 0
    let foot_begin = if (foot != null) foot + 1
        else if (head != null) head + 1
        else if (first_head != null) first_head + 1 else 0
    let foot_end = if (last_foot != null) last_foot
        else if (foot != null) foot else null
    {head: if (head_end == null) []
        else split_rows(slice(content, 0, head_end), col_spec, info),
     body: split_rows(slice(content, body_begin, len(content)), col_spec, info),
     foot: if (foot_end == null) []
        else split_rows(slice(content, foot_begin, foot_end), col_spec, info)}
}

// the column preamble: the direct parser stores it in `columns`, other tables
// keep it as the first group (array.ls reads it)
fn parse_col_spec(el, ctx) {
    let cg = find_curly_group(el, 0)
    if (el.columns != null) colspec.columns(el.columns, ctx)
    else if (cg != null) colspec.columns(util.text_of(cg), ctx)
    else []
}

fn find_curly_group(el, i) {
    if (i >= len(el)) null
    else check_curly(el, i)
}

fn check_curly(el, i) {
    let child = el[i]
    if ((child is element) and name(child) == 'curly_group') child
    else find_curly_group(el, i + 1)
}

// The document parser may split one table into paragraphs. Keep all table
// tokens in source order so later rows and head/foot markers are not dropped.
fn table_content(el) => [for (child in content(el), token in table_tokens(child)) token]

fn table_tokens(child) {
    if (child is element and name(child) == 'paragraph')
        [for (token in child) token]
    else [child]
}

fn split_rows(content, col_spec, info) {
    collect_rows(content, 0, len(content), [], [], col_spec, info)
}

fn collect_rows(el, i, n, current_cells, acc_rows, col_spec, info) {
    if (i >= n) finalize_rows(current_cells, acc_rows, col_spec, info)
    else route_child(el, i, n, current_cells, acc_rows, col_spec, info)
}

fn finalize_rows(current_cells, acc_rows, col_spec, info) {
    let final_row = make_row(current_cells, col_spec, info)
    if (final_row != null) {
        append_row(acc_rows, final_row)
    } else {
        acc_rows
    }
}

fn route_child(el, i, n, current_cells, acc_rows, col_spec, info) {
    let child = el[i]
    if (child is string) handle_string_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child is symbol) handle_symbol_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child is element) handle_element_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
}

fn handle_symbol_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let sv = string(child)
    if (sv == "alignment_tab") collect_rows(el, i + 1, n, current_cells ++ [null], acc_rows, col_spec, info)
    else if (sv == "row_sep") handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info)
    else collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
}

fn handle_string_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    handle_text_content(el, i, n, current_cells, acc_rows, col_spec, info, child)
}

fn handle_text_content(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let trimmed = trim(child)
    if (trimmed == null or len(trimmed) == 0) collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
    else add_text_to_cell(el, i, n, current_cells, acc_rows, col_spec, info, trimmed)
}

fn add_text_to_cell(el, i, n, current_cells, acc_rows, col_spec, info, trimmed) {
    let new_cells = append_to_last_cell(current_cells, trimmed)
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn handle_element_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let child_name = name(child)
    // The table owns its caption; it is not cell content.
    if (child_name == 'caption') collect_rows(el, i + 1, n, current_cells, acc_rows, col_spec, info)
    else if (child_name == 'linebreak_command') handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info)
    else if (child_name == 'hline' or child_name == 'toprule' or child_name == 'midrule' or child_name == 'bottomrule')
        handle_rule_row(el, i, n, current_cells, acc_rows, col_spec, info,
            rule_row(string(child_name), rule_color_at(info, child), len(col_spec)))
    else if (child_name == 'hhline' and registry.active(info.packages, "hhline"))
        handle_rule_row(el, i, n, current_cells, acc_rows, col_spec, info,
            hhline.rule_row(util.raw_argument(child, "required", 0) or "", len(col_spec), rule_color_at(info, child)))
    else if (child_name == 'cmidrule' and registry.active(info.packages, "booktabs"))
        handle_rule_row(el, i, n, current_cells, acc_rows, col_spec, info,
            booktabs.cmidrule(child, len(col_spec)))
    else if (child_name == 'addlinespace' and registry.active(info.packages, "booktabs"))
        handle_rule_row(el, i, n, current_cells, acc_rows, col_spec, info,
            booktabs.addlinespace(child, len(col_spec)))
    else if (child_name == 'rowcolor' and info.colortbl)
        handle_row_color(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child_name == 'multicolumn') handle_multicol_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else if (child_name == 'multirow') handle_multirow_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
    else handle_rendered_child(el, i, n, current_cells, acc_rows, col_spec, info, child)
}

// Keep the color directive with its row so the shared table builder applies it
// after column spans and rule rows have been resolved.
fn handle_row_color(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let css = color.command_color(child, 0, info.custom_colors)
    let marker = if (css == null) color.invalid_color(child)
        else {type: "rowcolor", color: css}
    collect_rows(el, i + 1, n, [marker] ++
        (if (len(current_cells) == 0) [null] else current_cells), acc_rows, col_spec, info)
}

fn handle_row_break(el, i, n, current_cells, acc_rows, col_spec, info) {
    let row = make_row(current_cells, col_spec, info)
    if (row != null) {
        let new_rows = append_row(acc_rows, row)
        collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
    } else {
        collect_rows(el, i + 1, n, [], acc_rows, col_spec, info)
    }
}

fn append_row(rows, row) {
    let wrapped = [row]
    rows ++ wrapped
}

// a horizontal rule's row; a colortbl rule color draws it as a colored line across
// the columns instead of the plain rule row's border on the next row
fn rule_row(kind, rule_color, columns) {
    if (rule_color == null) <tr class: "latex-" ++ kind>
    else <tr class: "latex-colored-rule",
        <td colspan: string(max([1, columns])), style: "border-top:" ++
            (if (kind == "toprule" or kind == "bottomrule") "2px" else "1px") ++ " solid " ++ rule_color>>
}

// the \arrayrulecolor in effect at a rule, when colortbl is loaded
fn rule_color_at(info, child) =>
    if (info.colortbl) util.setting_at(info.settings, "arrayrulecolor", child.source_offset) else null

// flush cells before each horizontal rule so partial rules retain their column span.
fn handle_rule_row(el, i, n, current_cells, acc_rows, col_spec, info, rule) {
    // flush current cells if any, then add rule row
    let row = make_row(current_cells, col_spec, info)
    if (row != null) {
        let flushed = append_row(acc_rows, row)
        let new_rows = append_row(flushed, rule)
        collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
    } else {
        let new_rows = append_row(acc_rows, rule)
        collect_rows(el, i + 1, n, [], new_rows, col_spec, info)
    }
}

fn handle_rendered_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let rendered = render_node(child, info)
    let new_cells = append_to_last_cell(current_cells, rendered)
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

// --- multicolumn / multirow cell handling ---

fn handle_multicol_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let args = util.command_args(child)
    let colspan = int(args[0])
    // the span's own preamble sets its alignment and its rules
    let spanned = colspec.columns(util.text_of(args[1]), info.table_columns or {})
    let column = if (len(spanned) > 0) spanned[0] else "left"
    let align = if (column is map) column.align else column
    let content = render_span_content(args, 2, info)
    let cell_desc = {type: "multicol", colspan: colspan, align: align, column: column, content: content}
    let new_cells = current_cells ++ [cell_desc]
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn handle_multirow_child(el, i, n, current_cells, acc_rows, col_spec, info, child) {
    let args = util.command_args(child)
    let rowspan = int(args[0])
    let content = render_span_content(args, 2, info)
    let cell_desc = {type: "multirow", rowspan: rowspan, content: content}
    let new_cells = current_cells ++ [cell_desc]
    collect_rows(el, i + 1, n, new_cells, acc_rows, col_spec, info)
}

fn render_span_content(child, start, info) {
    if (start >= len(child)) null
    else if (start == len(child) - 1) render_one_span(child[start], info)
    else render_span_seq(child, start, len(child), info, [])
}

fn render_one_span(c, info) {
    if (c is element) render_node(c, info)
    else c
}

fn render_span_seq(el, i, n, info, acc) {
    if (i >= n) acc
    else render_next_span(el, i, n, info, acc)
}

fn render_next_span(el, i, n, info, acc) {
    let c = el[i]
    let r = render_one_span(c, info)
    render_span_seq(el, i + 1, n, info, acc ++ [r])
}

fn get_align(col_spec, idx) {
    if (idx < len(col_spec)) col_spec[idx]
    else "left"
}

// a cell under its column: alignment, then the column's rules and color and the
// cell's own \cellcolor
fn cell_to_td_aligned(c, align, info) {
    let body = without_cell_colors(c)
    let extra = cell_decoration(c, align, info)
    if (align is map and align.kind == "si-decimal")
        siunitx.render_table_cell(body, align, if (extra == "") null else extra)
    else if (align is map) cell_to_td_styled(body, table_align_style(align) ++ joined_style(extra))
    else if (align == "left" and extra == "") cell_to_td_plain(body)
    else cell_to_td_styled(body, "text-align: " ++ align ++ joined_style(extra))
}

fn joined_style(extra) => if (extra == "") "" else ";" ++ extra

// colortbl's order: a \cellcolor over the row's color over the column's
fn cell_decoration(c, column, info) {
    let own = [for (item in (if (c is array) c else [c]) where is_span_cell(item, "cellcolor")) item.color]
    colspec.decoration_css(column, info.rule_color, info.row_colored or len(own) > 0) ++
        (if (len(own) == 0) "" else "background-color:" ++ own[len(own) - 1] ++ ";")
}

fn without_cell_colors(c) {
    if (c is array) [for (item in c where not is_span_cell(item, "cellcolor")) item]
    else if (is_span_cell(c, "cellcolor")) null else c
}

fn table_align_style(align) {
    let width = if (align.kind == "fixed" and align.width != null)
        ";width:" ++ align.width else ""
    "text-align: " ++ align.align ++ width
}

fn cell_to_td_plain(c) {
    if (c is array) { <td for item in c { item }> }
    else if (c != null) { <td c> }
    else { <td> }
}

fn cell_to_td_styled(c, style) {
    if (c is array) { <td style: style, for item in c { item }> }
    else if (c != null) { <td style: style, c> }
    else { <td style: style> }
}

fn make_row(cells, col_spec, info) {
    let marker = if (len(cells) > 0 and is_span_cell(cells[0], "rowcolor")) cells[0] else null
    let body = if (marker != null) slice(cells, 1, len(cells)) else cells
    if (len(body) == 0) null
    else if (all_cells_empty(body, 0)) null
    else make_row_inner(body, col_spec, marker, info)
}

fn all_cells_empty(cells, i) {
    if (i >= len(cells)) true
    else if (cells[i] != null) false
    else all_cells_empty(cells, i + 1)
}

fn make_row_inner(cells, col_spec, marker, info) {
    let tds = build_tds(cells, col_spec, 0, 0, [], {*:info, row_colored: marker != null});
    <tr style: if (marker == null) null else "background-color:" ++ marker.color,
        for td in tds { td }>
}

fn build_tds(cells, col_spec, ci, col, acc, info) {
    if (ci >= len(cells)) acc
    else dispatch_td(cells, col_spec, ci, col, acc, info)
}

fn dispatch_td(cells, col_spec, ci, col, acc, info) {
    let cell = cells[ci]
    if (is_span_cell(cell, "multicol")) emit_multicol_td(cells, col_spec, ci, col, acc, cell, info)
    else if (is_span_cell(cell, "multirow")) emit_multirow_td(cells, col_spec, ci, col, acc, cell, info)
    else emit_regular_td(cells, col_spec, ci, col, acc, cell, info)
}

fn is_span_cell(cell, t) {
    (cell is map) and (cell.type == t)
}

fn emit_multicol_td(cells, col_spec, ci, col, acc, cell, info) {
    let td = multicol_td(cell, info)
    build_tds(cells, col_spec, ci + 1, col + cell.colspan, acc ++ [td], info)
}

fn multicol_td(cell, info) {
    let c = without_cell_colors(cell.content);
    <td colspan: string(cell.colspan), style: "text-align: " ++ cell.align ++
        joined_style(cell_decoration(cell.content, cell.column, info)), c>
}

fn emit_multirow_td(cells, col_spec, ci, col, acc, cell, info) {
    let align = get_align(col_spec, col)
    let td = multirow_td(cell, align, info)
    build_tds(cells, col_spec, ci + 1, col + 1, acc ++ [td], info)
}

fn multirow_td(cell, align, info) {
    let c = without_cell_colors(cell.content)
    let css_align = if (align is map and align.kind == "si-decimal") "right"
        else if (align is map) align.align else align
    let extra = cell_decoration(cell.content, align, info)
    if (css_align == "left" and extra == "") { <td rowspan: string(cell.rowspan), c> }
    else { <td rowspan: string(cell.rowspan), style: "text-align: " ++ css_align ++ joined_style(extra), c> }
}

fn emit_regular_td(cells, col_spec, ci, col, acc, cell, info) {
    let td = cell_to_td_aligned(cell, get_align(col_spec, col), info)
    build_tds(cells, col_spec, ci + 1, col + 1, acc ++ [td], info)
}

fn append_to_last_cell(cells, content) {
    if (content == null) cells
    else if (len(cells) == 0) [[content]]
    else set_last_cell(cells, content)
}

fn set_last_cell(cells, content) {
    let last_idx = len(cells) - 1
    let last_cell = cells[last_idx]
    let new_val = compute_new_cell(last_cell, content)
    slice(cells, 0, last_idx) ++ [new_val]
}

fn compute_new_cell(last_cell, content) {
    if (last_cell == null) wrap_content(content)
    else if (last_cell is array) last_cell ++ [content]
    else [last_cell, content]
}

fn wrap_content(content) {
    [content]
}

fn replace_last(arr, new_val) {
    slice(arr, 0, len(arr) - 1) ++ [new_val]
}

// ============================================================
// Cross references
// ============================================================

fn render_label(el, info) {
    let label_name = trim(util.text_of(el));
    <a id: util.slugify(label_name)>
}

fn render_ref(el, info) {
    if (trim(util.text_of(el)) == "LastPage" and registry.active(info.packages, "lastpage"))
        <span class: "latex-page-count">
    else render_number_ref(el, info)
}

fn render_pageref(el, info) => page_ref(trim(util.text_of(el)), info, el.source_offset)

// the page of a label; paged output fills the anchor with the page number
fn page_ref(key, info, offset) {
    let entry = util.lookup(info.labels, key)
    if (key == "LastPage" and registry.active(info.packages, "lastpage"))
        <span class: "latex-page-count">
    else if (entry == null) util.unsupported_element("latex", "Unresolved page reference " ++ key, offset)
    else <a class: "latex-target-page", href: "#" ++ entry.id>
}

fn render_number_ref(el, info, parenthesized = false) => number_ref(trim(util.text_of(el)), info, parenthesized)

fn number_ref(ref_name, info, parenthesized = false) {
    let label_info = util.lookup(info.labels, ref_name)
    let value = if (label_info != null) label_info.number else "??";
    // eqref shares reference resolution, framing the number as its equation's tag
    <a class: if (label_info != null) "latex-ref" else "latex-ref latex-unresolved",
        href: "#" ++ (if (label_info != null) label_info.id else util.slugify(ref_name)),
        if (parenthesized) amsmath.tag_text(string(value), false,
            if (label_info != null) label_info.tag_form else null) else value>
}

fn render_autoref(el, info) {
    let ref_name = trim(util.text_of(el))
    let label_info = util.lookup(info.labels, ref_name)
    if (label_info != null) {
        let prefix = autoref_prefix(label_info.type)
        let display = prefix ++ label_info.number;
        <a class: "latex-ref latex-autoref", href: "#" ++ label_info.id,
            display
        >
    } else {
        <a class: "latex-ref latex-unresolved", href: "#" ++ util.slugify(ref_name),
            "??"
        >
    }
}

fn reference_type_name(label_type, plural, capital) {
    let base = if (label_type == "subsection" or label_type == "subsubsection")
        "section" else label_type
    let many = if (plural) base ++ "s" else base
    if (capital) upper(slice(many, 0, 1)) ++ slice(many, 1, len(many)) else many
}

fn autoref_prefix(label_type) =>
    reference_type_name(label_type, false, true) ++ "\u00A0"

fn cref_types(entries, at, found) {
    if (at >= len(entries)) found
    else {
        let type_name = entries[at].type
        cref_types(entries, at + 1,
            if (any([for (prior in found where prior == type_name) prior])) found
            else [*found, type_name])
    }
}

fn cref_consecutive(entries, at) {
    if (at >= len(entries)) true
    else {
        let prior = int(entries[at - 1].number) ^ { null }
        let current = int(entries[at].number) ^ { null }
        prior != null and current != null and current == prior + 1 and
            cref_consecutive(entries, at + 1)
    }
}

fn cref_number(entry) =>
    <a class: "latex-ref latex-cref", href: "#" ++ entry.id, entry.number>

fn cref_group(entries, label_type, capital) {
    let n = len(entries)
    let collapse = n >= 3 and cref_consecutive(entries, 1);
    <span class: "latex-cref-group",
        reference_type_name(label_type, n > 1, capital) ++ "\u00A0"
        if (collapse) {
            cref_number(entries[0])
            "–"
            cref_number(entries[n - 1])
        } else {
            for (at, entry in entries) {
                if (at > 0) { if (at == n - 1) " and " else ", " }
                cref_number(entry)
            }
        }
    >
}

fn render_cref(el, info, capital) =>
    cref_keys([for (key in util.split_top_level(trim(util.text_of(el)), ",")) trim(key)], info, capital)

fn cref_keys(keys, info, capital) {
    let entries = [for (key in keys) util.lookup(info.labels, key)]
    if (len(entries) == 0 or any([for (entry in entries where entry == null) true]))
        <span class: "latex-ref latex-unresolved", "??">
    else {
        let types = cref_types(entries, 0, []);
        <span class: "latex-cref",
            for (at, label_type in types) {
                if (at > 0) "; "
                cref_group([for (entry in entries where entry.type == label_type) entry],
                    label_type, capital and at == 0)
            }
        >
    }
}

// varioref: a reference with its page phrase, or the phrase alone. The phrase
// carries \pageref's page anchor, which paged output fills; cleveref, when loaded,
// names the reference's type as it does for \vref.
fn render_vref(el, info) {
    let tag = string(name(el))
    let first = trim(util.raw_argument(el, "required", 0) or "")
    let second = trim(util.raw_argument(el, "required", 1) or "")
    let capital = starts_with(tag, "V")
    let reference = (key) => if (registry.active(info.packages, "cleveref")) cref_keys([key], info, capital)
        else number_ref(key, info)
    let parts = if (tag == "vpageref" or tag == "Vpageref")
            [if (capital) "On page " else "on page ", page_ref(first, info, el.source_offset)]
        else if (tag == "vpagerefrange")
            ["on pages ", page_ref(first, info, el.source_offset), "\u2013", page_ref(second, info, el.source_offset)]
        else if (tag == "vrefrange")
            [reference(first), " to ", reference(second), " on pages ", page_ref(first, info, el.source_offset),
             "\u2013", page_ref(second, info, el.source_offset)]
        else [reference(first), " on page ", page_ref(first, info, el.source_offset)];
    <span class: "latex-vref", for c in parts { c }>
}

fn render_nameref(el, info) {
    let ref_name = trim(util.text_of(el))
    let label_info = util.lookup(info.labels, ref_name)
    if (label_info != null) {
        let title_val = label_info.title
        let display = get_nameref_display(title_val, label_info.number);
        <a class: "latex-ref latex-nameref", href: "#" ++ label_info.id,
            display
        >
    } else {
        <a class: "latex-ref latex-unresolved", href: "#" ++ util.slugify(ref_name),
            "??"
        >
    }
}

fn get_nameref_display(title_val, number_val) {
    if (title_val != null and title_val is string and len(title_val) > 0) title_val
    else if (number_val != null) string(number_val)
    else "??"
}

fn render_href(el, info) {
    let args = util.command_args(el)
    let n = len(args)
    let url = if (n > 0) util.text_of(args[0]) else "#"
    let display = if (n > 1) render_children(el, 1, info) else [url];
    <a href: url, for d in display { d }>
}

fn render_url(el) {
    let url = util.text_of(el);
    <a class: "latex-url", href: url, url>
}

fn render_doi(el) {
    let value = trim(util.text_of(el));
    <a class: "latex-url", href: "https://doi.org/" ++ value, value>
}

fn render_enquote(el, info) {
    let french = info.language == "french" or info.language == "fr"
    let content = render_children(el, 0, info);
    <span class: "latex-quote",
        if (french) "« " else "“"
        for c in content { c }
        if (french) " »" else "”"
    >
}

fn render_cite(el, info) {
    let keys = util.split_top_level(trim(util.text_of_skip_brack(el)), ",")
    if (len(keys) == 1) render_cite_key(trim(keys[0]), info)
    else <span class: "latex-cites",
        "["
        for (i, key in keys)
            <a class: "latex-cite", href: "#bib-" ++ trim(key),
                (if (i > 0) ", " else "") ++ render_cite_number(trim(key), info)>
        "]">
}

fn render_cite_number(cite_key, info) {
    let bib_num = find_bib_num(info.bibitems, cite_key, 0)
    if (bib_num > 0) string(bib_num) else "?"
}

fn render_cite_key(cite_key, info) {
    let bib_num = find_bib_num(info.bibitems, cite_key, 0)
    let display_num = if (bib_num > 0) string(bib_num) else "?";
    <a class: "latex-cite", href: "#bib-" ++ cite_key, "[" ++ display_num ++ "]">
}

fn find_bib_num(bibitems, key, i) {
    if (i >= len(bibitems)) 0
    else check_bib_match(bibitems, key, i)
}

fn check_bib_match(bibitems, key, i) {
    let entry = bibitems[i]
    if (entry.key == key) entry.number
    else find_bib_num(bibitems, key, i + 1)
}

fn render_bibliography(el, info) {
    let items = render_bib_items(el, info);
    <section class: "latex-bibliography",
        <h2 "References">
        <ol class: "latex-bib-list",
            for item in items { item }
        >
    >
}

fn render_bib_items(el, info) {
    collect_bib_entries(el, 0, len(el), info, [], null)
}

// walk children of thebibliography: each bibitem starts a new entry,
// content between bibitems is the entry text
fn collect_bib_entries(el, i, n, info, acc, current_key) {
    if (i >= n) finalize_bib_entries(acc, current_key, [])
    else collect_bib_entry_at(el, i, n, info, acc, current_key)
}

fn collect_bib_entry_at(el, i, n, info, acc, current_key) {
    let child = el[i]
    if ((child is element) and name(child) == 'bibitem') start_bib_entry(el, i, n, info, acc, child)
    else if ((child is element) and name(child) == 'paragraph') collect_bib_in_paragraph(el, i, n, info, acc, current_key, child)
    else collect_bib_entries(el, i + 1, n, info, acc, current_key)
}

fn start_bib_entry(el, i, n, info, acc, bibitem) {
    let key = trim(util.text_of(bibitem))
    collect_bib_entries(el, i + 1, n, info, acc, key)
}

fn collect_bib_in_paragraph(el, i, n, info, acc, current_key, para) {
    // process paragraph children to split by bibitem
    let result = collect_bib_from_para(para, 0, len(para), info, acc, current_key)
    collect_bib_entries(el, i + 1, n, info, result.acc, result.key)
}

fn collect_bib_from_para(para, i, n, info, acc, current_key) {
    if (i >= n) make_bib_state(acc, current_key)
    else collect_bib_from_para_at(para, i, n, info, acc, current_key)
}

fn collect_bib_from_para_at(para, i, n, info, acc, current_key) {
    let child = para[i]
    if ((child is element) and name(child) == 'bibitem') start_bib_from_para(para, i, n, info, acc, child)
    else accumulate_bib_content(para, i, n, info, acc, current_key, child)
}

fn start_bib_from_para(para, i, n, info, acc, bibitem) {
    let key = trim(util.text_of(bibitem))
    collect_bib_from_para(para, i + 1, n, info, acc, key)
}

fn accumulate_bib_content(para, i, n, info, acc, current_key, child) {
    if (current_key == null) collect_bib_from_para(para, i + 1, n, info, acc, current_key)
    else add_bib_content(para, i, n, info, acc, current_key, child)
}

fn add_bib_content(para, i, n, info, acc, current_key, child) {
    let rendered = render_node(child, info)
    let new_entry = make_bib_item_entry(current_key, rendered)
    let new_acc = acc ++ [new_entry]
    collect_bib_from_para(para, i + 1, n, info, new_acc, current_key)
}

fn make_bib_state(acc, key) {
    {acc: acc, key: key}
}

fn make_bib_item_entry(key, content) {
    {key: key, content: content}
}

fn finalize_bib_entries(entries, last_key, acc) {
    if (len(entries) == 0) acc
    else group_bib_entries(entries, acc)
}

fn group_bib_entries(entries, acc) {
    // group entries by key into <li> elements
    build_bib_list(entries, 0, len(entries), null, [], [])
}

fn build_bib_list(entries, i, n, cur_key, cur_content, acc) {
    if (i >= n) finish_bib_list(cur_key, cur_content, acc)
    else process_bib_entry(entries, i, n, cur_key, cur_content, acc)
}

fn process_bib_entry(entries, i, n, cur_key, cur_content, acc) {
    let e = entries[i]
    if (cur_key == null or cur_key == e.key) build_bib_list(entries, i + 1, n, e.key, cur_content ++ [e.content], acc)
    else flush_and_continue(entries, i, n, cur_key, cur_content, acc)
}

fn flush_and_continue(entries, i, n, cur_key, cur_content, acc) {
    let li = make_bib_li(cur_key, cur_content)
    let e = entries[i]
    build_bib_list(entries, i + 1, n, e.key, [e.content], acc ++ [li])
}

fn finish_bib_list(cur_key, cur_content, acc) {
    if (cur_key == null) acc
    else acc ++ [make_bib_li(cur_key, cur_content)]
}

fn make_bib_li(key, content) {
    <li id: "bib-" ++ key,
        for c in content { c }
    >
}

// ============================================================
// Footnotes
// ============================================================

// a footnote's number by source offset, else by its text when offsets are absent
fn render_footnote(el, info) {
    let exact = [for (entry in info.footnotes where entry.offset != null and
        entry.offset == el.source_offset) entry.number]
    let key = util.slugify(trim(util.text_of(el)))
    let by_text = [for (entry in info.footnotes
        where util.slugify(trim(util.text_of(entry.node))) == key) entry.number]
    footnote_ref(if (len(exact) > 0) exact[0] else if (len(by_text) > 0) by_text[0] else 0, info)
}

fn render_footnotemark(el, info) {
    let mark = util.lookup(info.footnote_marks, string(el.source_offset))
    if (mark == null) util.unsupported_element("latex", "Unnumbered footnote mark",
        el.source_offset)
    else footnote_ref(mark, info)
}

fn footnote_ref(fn_num, info) {
    <sup class: "latex-footnote-ref",
        <a href: "#fn-" ++ (fn_num), id: "fnref-" ++ (fn_num),
            footmisc.mark(registry.options_for(info.packages, "footmisc"), fn_num)
        >
    >
}

// footmisc's \footref{label} repeats the mark of the note holding \label, linking
// to the label's anchor inside that note
fn render_footref(el, info) {
    let entry = util.lookup(info.labels, trim(util.text_of(el)));
    <sup class: if (entry == null) "latex-footnote-ref latex-unresolved" else "latex-footnote-ref",
        <a href: "#" ++ (if (entry == null) util.slugify(trim(util.text_of(el))) else entry.id),
            if (entry == null) "??" else entry.number>>
}

// ============================================================
// Logo rendering (LaTeX, TeX, LaTeX2e)
// ============================================================

fn render_latex_logo() {
    <span class: "latex-logo",
        "L"
        <sup "A">
        "T"
        <sub "E">
        "X"
    >
}

fn render_tex_logo() {
    <span class: "tex-logo",
        "T"
        <sub "E">
        "X"
    >
}

fn render_latex2e_logo() {
    <span class: "latex-logo",
        "L"
        <sup "A">
        "T"
        <sub "E">
        "X"
        <span class: "latex-2e", "2">
        <i "\u03B5">
    >
}

// ============================================================
// Character code commands (\symbol, \char, ^^)
// ============================================================

// \symbol{"00A9} or \symbol{169}
fn render_symbol_command(el) {
    let arg_text = util.text_of(el)
    let trimmed = trim(arg_text)
    let code = parse_char_code(trimmed)
    if (code > 0) chr(code) else ""
}

// \char"A9 or \char169 or \char'251
fn render_char_command(el) {
    let text = util.text_of(el)
    // text is the full token, e.g. \char"A9
    let after = if (starts_with(text, "\\char")) slice(text, 5, len(text)) else text
    let code = parse_char_code(trim(after))
    if (code > 0) chr(code) else ""
}

// ^^A9 or ^^^^00A9
fn render_caret_char(el) {
    let text = util.text_of(el)
    // strip leading ^^ or ^^^^
    let hex_part = if (starts_with(text, "^^^^")) slice(text, 4, len(text))
                   else if (starts_with(text, "^^")) slice(text, 2, len(text))
                   else text
    let code = hex_to_int(hex_part, 0, len(hex_part), 0)
    if (code > 0) chr(code) else ""
}

// parse a char code string: "A9 (hex), 169 (decimal), '251 (octal)
fn parse_char_code(s) {
    if (len(s) == 0) 0
    else if (starts_with(s, "\"")) hex_to_int(s, 1, len(s), 0)
    else if (starts_with(s, "'")) oct_to_int(s, 1, len(s), 0)
    else int(s)
}

fn hex_to_int(s, i, n, acc) {
    if (i >= n) acc
    else {
        let c = s[i]
        let digit = hex_digit(c)
        if (digit < 0) acc
        else hex_to_int(s, i + 1, n, acc * 16 + digit)
    }
}

fn hex_digit(c) {
    if (c >= "0" and c <= "9") ord(c) - ord("0")
    else if (c >= "a" and c <= "f") ord(c) - ord("a") + 10
    else if (c >= "A" and c <= "F") ord(c) - ord("A") + 10
    else -1
}

fn oct_to_int(s, i, n, acc) {
    if (i >= n) acc
    else {
        let c = s[i]
        if (c >= "0" and c <= "7") oct_to_int(s, i + 1, n, acc * 8 + (ord(c) - ord("0")))
        else acc
    }
}

// ============================================================
// Control symbols, groups, misc
// ============================================================

fn render_control_symbol(el) {
    let t = util.text_of(el)
    let resolved = sym.resolve_special(t)
    if (resolved != null) resolved else t
}

fn render_group(el, info) {
    // check for leading font/alignment declaration in this group
    let decl = find_leading_decl(el, 0, len(el), info)
    if (decl != null) { render_group_with_decl(el, info, decl) }
    else {
        let items = render_children_with_color(el, info)
        if (len(items) == 0) null
        else if (len(items) == 1) items[0]
        else <span for c in items { c }>
    }
}

// find the first font/paragraph/color declaration among children (skipping whitespace)
fn find_leading_decl(el, i, n, info) {
    if (i >= n) { null }
    else {
        let child = el[i]
        if (child is string and len(trim(child)) == 0) {
            find_leading_decl(el, i + 1, n, info)
        } else if (child is element) {
            let tag_str = string(name(child))
            if (font_decl.is_font_decl(tag_str) or paragraph_decl_css(child, info) != null or
                color.is_color_decl(tag_str)) child
            else null
        }
        else { null }
    }
}

// The CSS of a paragraph declaration: LaTeX's alignment ones, and ragged2e's and
// setspace's while their package is active; null for anything else
fn paragraph_decl_css(el, info) {
    let tag = string(name(el))
    if (font_decl.is_align_decl(tag)) font_decl.align_decl_style(tag)
    else if (registry.active(info.packages, "ragged2e") and
        (contains(paragraph.RAGGED_DECLARATIONS, tag) or false)) paragraph.ragged_css(tag)
    else if (registry.active(info.packages, "setspace") and
        (contains(paragraph.SETSPACE_DECLARATIONS, tag) or false)) (
        let stretch = paragraph.declared_stretch(el, info.class_options),
        if (stretch == null) null else paragraph.stretch_css(stretch))
    else null
}

// render a group whose first significant child is a font/alignment/color declaration
fn render_group_with_decl(el, info, decl) {
    let decl_tag = string(name(decl))
    // render children after the declaration (the decl itself returns null)
    let items = render_children_with_color(el, info)
    if (font_decl.is_font_decl(decl_tag))
        font_decl.wrap_font_decl(decl, items)
    else if (color.is_color_decl(decl_tag))
        if (len(items) == 1) items[0] else <span for c in items { c }>
    else font_decl.wrap_align_decl(decl_tag, items, paragraph_decl_css(decl, info))
}

fn has_color_decl(el) {
    any([for (child in el) child is element and string(name(child)) == "color"])
}

fn render_children_with_color(el, info) {
    if (has_color_decl(el)) color_segments(el, 0, len(el), info, null, [], [])
    else render_children(el, 0, info)
}

fn flush_color_segment(items, style, acc) {
    if (len(items) == 0) acc
    else if (style == null) acc ++ items
    else acc ++ [<span class: "latex-color", style: style, for c in items { c }>]
}

// a declaration changes only following siblings; recursive rendering keeps group scope.
fn color_segments(el, i, n, info, style, current, acc) {
    if (i >= n) flush_color_segment(current, style, acc)
    else {
        let child = el[i]
        let next_info = next_render_info(child, info)
        if (child is element and string(name(child)) == "color") {
            let flushed = flush_color_segment(current, style, acc)
            let next_style = color.color_decl_style(child, next_info.custom_colors)
            let next_acc = if (next_style == null) flushed ++ [color.invalid_color(child)] else flushed
            color_segments(el, i + 1, n, next_info, next_style, [], next_acc)
        } else {
            let rendered = render_node(child, next_info)
            color_segments(el, i + 1, n, next_info, style,
                if (rendered == null) current else current ++ [rendered], acc)
        }
    }
}

// find the declaration element node by tag name within a parent
fn find_decl_el(el, tag_str) {
    find_decl_el_rec(el, 0, len(el), tag_str)
}

fn find_decl_el_rec(el, i, n, tag_str) {
    if (i >= n) { el }
    else {
        let child = el[i]
        if (child is element and string(name(child)) == tag_str) child
        else find_decl_el_rec(el, i + 1, n, tag_str)
    }
}

fn render_includegraphics(el, info) {
    if (registry.active(info.packages, "graphicx")) graphicx.render(el, info.base_uri)
    else render_legacy_includegraphics(el)
}

fn render_legacy_includegraphics(el) {
    // extract optional arguments [key=val,...] from brack_group
    let brack = util.find_child(el, 'brack_group')
    let opts = util.parse_kv_options(if (brack != null) util.text_of(brack) else null)

    // extract src from non-brack children
    let src = trim(util.text_of_skip_brack(el))

    // build style string from options
    let style = build_img_style(opts)

    if (style != "") {
        <img class: "latex-image", src: src, alt: src, style: style>
    } else {
        <img class: "latex-image", src: src, alt: src>
    }
}

fn build_img_style(opts) {
    let parts0 = []
    // width
    let parts1 = if (opts.width != null) { parts0 ++ ["width:" ++ opts.width] } else parts0
    // height
    let parts2 = if (opts.height != null) { parts1 ++ ["height:" ++ opts.height] } else parts1
    // keepaspectratio (only meaningful with width or height)
    let parts3 = if (opts.keepaspectratio != null) { parts2 ++ ["object-fit:contain"] } else parts2
    // build transform: may combine scale and angle
    let tx0 = []
    let tx1 = if (opts.scale != null) { tx0 ++ ["scale(" ++ opts.scale ++ ")"] } else tx0
    let tx2 = if (opts.angle != null) { tx1 ++ ["rotate(" ++ opts.angle ++ "deg)"] } else tx1
    let parts4 = if (len(tx2) > 0) { parts3 ++ ["transform:" ++ join(tx2, " ")] } else parts3
    // trim + clip → clip-path:inset(top right bottom left)
    // LaTeX trim order: left bottom right top
    let trim_vals = if (opts.trim != null) split(trim(opts.trim), null) else []
    let parts5 = if (opts.trim != null and opts.clip != null and len(trim_vals) == 4) {
        // LaTeX: trim=left bottom right top → CSS: inset(top right bottom left)
        parts4 ++ ["clip-path:inset(" ++ trim_vals[3] ++ " " ++ trim_vals[2] ++ " " ++ trim_vals[1] ++ " " ++ trim_vals[0] ++ ")"]
    } else parts4

    join(parts5, ";")
}

fn render_marginpar(el, info) {
    let items = render_children(el, 0, info);
    <span class: "latex-cmd-marginpar", for c in items { c }>
}

// D7.2.7: the TeX engine has already expanded every user macro, so an element
// reaching here is a command it passed through on purpose: a custom theorem
// environment, or an unsupported command that the fallback reports.
fn render_generic(el, info) {
    let thm_def = util.lookup(info.theorem_defs, string(name(el)))
    if (thm_def != null) { render_custom_theorem(el, info, thm_def) }
    else { render_generic_default(el, info) }
}

fn render_custom_theorem(el, info, thm_def) {
    let items = render_children(el, 0, info)
    let display_name = thm_def.label
    let env_type = thm_def.env_name
    let env_num = if (thm_def.numbered) get_theorem_num(el, info, env_type) else null
    let heading = display_name ++
        (if (env_num == null) "" else " " ++ string(env_num)) ++ ".";
    <div class: "latex-theorem latex-" ++ env_type ++
            " latex-theorem-style-" ++ thm_def.style,
        <strong class: "latex-theorem-head", heading>
        " "
        for c in items { c }
    >
}

fn render_items(items, i, n, info, acc) {
    if (i >= n) { acc }
    else {
        let item = items[i]
        let rendered = render_node(item, info)
        let new_acc = if (rendered != null) acc ++ [rendered] else acc
        render_items(items, i + 1, n, info, new_acc)
    }
}

fn render_generic_default(el, info) {
    let command = string(name(el))
    let message = "Unsupported LaTeX command or environment: " ++ command
    util.unsupported_element(registry.package_for_command(command, info.packages),
        message, el.source_offset)
}
