// LaTeX owns figure context; the TikZ package owns picture semantics.
import tikz: lambda.doc.tikz.tikz
import tikz_opts: lambda.doc.tikz.options
import util: .util
import macros: .macros

fn command_end(source, at) {
    if (at >= len(source)) at
    else if (index_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
        slice(source, at, at + 1)) != null)
        command_end(source, at + 1)
    else at
}

fn skip_space(source, at) =>
    if (at >= len(source)) at
    else if (slice(source, at, at + 1) == " " or
             slice(source, at, at + 1) == "\n" or
             slice(source, at, at + 1) == "\t")
        skip_space(source, at + 1)
    else at

fn macro_arguments(source, at, remaining, acc) any^ {
    if (remaining == 0) {next: at, values: acc}
    else {
        let open_at = skip_space(source, at)
        let group = util.read_balanced(source, open_at, "{", "}")^
        macro_arguments(source, group.next, remaining - 1,
            [*acc, group.raw])^
    }
}

fn substitute_arguments(source, values, at) {
    if (at >= len(values)) source
    else substitute_arguments(replace(source, "#" ++ string(at + 1),
        values[at]), values, at + 1)
}

fn handler_of(key) {
    let found = [for (suffix in tikz_opts.STYLE_HANDLERS where ends_with(key, suffix)) suffix]
    if (len(found) == 0) null else found[0]
}

// Preamble key handlers; other handlers (.code, .cd, .store in) program PGF
// internals and stay diagnosed.
fn declaration_styles(raw) any^ {
    let values = util.parse_kv_options(util.strip_tex_comments(raw))
    let invalid = [for (key, value at values
        where handler_of(string(key)) == null) string(key)]
    if (len(invalid) > 0)
        raise error("unsupported TikZ style declaration: " ++ invalid[0])
    else [for (key, value at values)
        (let handler = handler_of(string(key)),
         {name: trim(slice(string(key), 0, len(string(key)) - len(handler))),
          handler: handler, source: value})]
}

fn collect_styles(declarations) any^ {
    let groups = [for (raw in declarations) declaration_styles(raw)^];
    [for (group in groups, item in group) item]
}

// Plain styles expand textually so picture and axis options see them too.
// Parameterized, appended or defaulted styles, inherited `every ...` styles and
// pics are left to the TikZ parser.
fn textual(style, styles) =>
    style.handler == "/.style" and not contains(style.source, "#") and
    not starts_with(style.name, "every ") and
    not any([for (other in styles) other.name == style.name and other.handler != "/.style"])

// Declaration groups the parser must define, as `\tikzset` text on one line so
// that line numbers in picture diagnostics are unchanged.
fn parser_preamble(declarations, styles) any^ {
    let needed = [for (raw in declarations
        where any([for (style in declaration_styles(raw)^) not textual(style, styles)])) raw];
    util.str_join([for (raw in needed) "\\tikzset{" ++
        replace(replace(util.strip_tex_comments(raw), "\r", " "), "\n", " ") ++ "}"], "")
}

fn expand_style_part(raw, styles, depth) any^ {
    if (depth > 8) raise error("TikZ style expansion exceeds 8 levels")
    else {
        let part = trim(raw)
        let matches = [for (style in styles where style.name == part) style.source]
        if (len(matches) == 0) raw
        else expand_style_options(matches[len(matches) - 1], styles, depth + 1)^
    }
}

fn expand_style_options(raw, styles, depth) any^ {
    util.str_join([for (part in util.split_top_level(raw, ","))
        expand_style_part(part, styles, depth)^], ",")
}

fn expand_style_brackets(source, styles, at, output) any^ {
    if (at >= len(source)) output
    else {
        let suffix = slice(source, at, len(source))
        let found = index_of(suffix, "[")
        if (found == null) output ++ suffix
        else {
            let open_at = at + found
            let group = util.read_balanced(source, open_at, "[", "]")^
            let expanded = expand_style_options(group.raw, styles, 0)^
            expand_style_brackets(source, styles, group.next,
                output ++ slice(source, at, open_at) ++ "[" ++ expanded ++ "]")^
        }
    }
}

fn expand_math_declarations(declarations, definitions) any^ =>
    [for (declaration in declarations)
        {*:declaration, source: if (declaration.source == null) null
            else expand_fragment(declaration.source, definitions, 0, 0, "")^}]

// Expand only declared TeX macros before the neutral TikZ parser sees their body.
fn expand_fragment(source, definitions, depth, at, output) any^ {
    if (depth > 16) raise error("TikZ macro expansion exceeds 16 levels")
    else if (at >= len(source)) output
    else {
        let suffix = slice(source, at, len(source))
        let found = index_of(suffix, "\\")
        if (found == null) output ++ suffix
        else {
            let slash = at + found
            let word_end = command_end(source, slash + 1)
            let end_at = if (word_end == slash + 1) word_end + 1 else word_end
            let command = slice(source, slash + 1, end_at)
            let definition = macros.find_macro(definitions, command)
            let before = output ++ slice(source, at, slash)
            if (definition == null)
                expand_fragment(source, definitions, depth, end_at,
                    before ++ slice(source, slash, end_at))^
            else if (definition.source == null or definition.params > 9 or
                     definition.default_arg != null)
                raise error("unsupported TikZ macro definition: " ++ command)
            else {
                let arguments = macro_arguments(source, end_at,
                    definition.params, [])^
                let substituted = substitute_arguments(definition.source,
                    arguments.values, 0)
                let expanded = expand_fragment(substituted, definitions,
                    depth + 1, 0, "")^
                expand_fragment(source, definitions, depth, arguments.next,
                    before ++ expanded)^
            }
        }
    }
}

fn render_expanded(island, definitions, declarations,
                   math_declarations, custom_colors, people_active, base_uri) any^ {
    if (island.raw_source == null)
        raise error("TikZ graphics island has no preserved source")
    else {
        let expanded = expand_fragment(island.raw_source,
            definitions, 0, 0, "")^
        let styles = collect_styles(declarations)^
        let source = expand_style_brackets(expanded,
            [for (style in styles where textual(style, styles)) style], 0, "")^
        let preamble = parser_preamble(declarations, styles)^
        let math_program = expand_math_declarations(math_declarations,
            definitions)^
        // `base_uri` resolves `\addplot table {file}` beside the document.
        tikz.render_with_program(preamble ++ source, math_program, custom_colors,
            island.source_offset, people_active, {base_uri: base_uri})^
    }
}

pub fn render_picture(island, definitions = [], declarations = [],
                      math_declarations = [], custom_colors = [],
                      people_active = false, base_uri = null) {
    render_expanded(island, definitions, declarations,
        math_declarations, custom_colors, people_active, base_uri) ^ {
        let offset = island.source_offset;
        <div class: "latex-tikz-unsupported",
            role: "img", 'aria-label': "Unsupported TikZ picture",
            util.unsupported_element("tikz", "TikZ picture could not be rendered: " ++ ^.message,
                offset);
            <pre island.raw_source>
        >
    }
}
