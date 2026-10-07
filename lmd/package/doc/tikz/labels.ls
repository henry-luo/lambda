// Text and math labels share one positioned HTML representation.
import math: lambda.doc.math.math
import opts: .options
import util: lambda.latex.util

fn plain_text(source) {
    // Common TeX accent commands in figure labels have plain Unicode equivalents.
    let acute_i = replace(source, "\\'{i}", "í")
    let acute_o = replace(acute_i, "\\'{o}", "ó")
    let acute_a = replace(acute_o, "\\'{a}", "á")
    let tilde_a = replace(acute_a, "\\~{a}", "ã")
    replace(tilde_a, "\\c{c}", "ç")
}

fn mixed_parts(source, offset, acc) any^ {
    let opening = index_of(slice(source, offset, len(source)), "$")
    if (opening == null) [*acc, <span plain_text(slice(source, offset, len(source)))>]
    else {
        let start = offset + opening
        let after = start + 1
        let closing = index_of(slice(source, after, len(source)), "$")
        if (closing == null) raise error("unclosed math span in TikZ label")
        else {
            let ast = parse(slice(source, after, after + closing), {type: "math"})^
            let rendered = math.render_box_element(math.render_box(ast, {display: false}))
            mixed_parts(source, after + closing + 1,
                [*acc, <span plain_text(slice(source, offset, start))>, rendered])^
        }
    }
}

let FONT_SIZES = [{command: "\\scriptsize", size: "0.72em"},
    {command: "\\footnotesize", size: "0.8em"}, {command: "\\small", size: "0.9em"},
    {command: "\\large", size: "1.2em"}]

// CSS font size for a supported TeX size command, or null.
pub fn font_size(command) {
    let matches = [for (entry in FONT_SIZES where entry.command == trim(command)) entry.size]
    if (len(matches) == 0) null else matches[0]
}

pub fn prepare(source) map^ {
    let value = trim(source)
    let font_matches = [for (entry in FONT_SIZES where starts_with(value, entry.command))
        entry.command]
    let font_command = if (len(font_matches) == 0) null else font_matches[0]
    if (starts_with(value, "\\textbf{")) {
        let group = util.read_balanced(value, len("\\textbf"), "{", "}")^
        let valid_tail = if (trim(slice(value, group.next, len(value))) != "")
            raise error("unsupported text after PGFPlots bold label") else true
        let prepared = prepare(group.raw)^;
        {element: <span style: "font-weight:bold", prepared.element>,
         width_em: prepared.width_em, height_em: prepared.height_em,
         depth_em: prepared.depth_em}
    } else if (starts_with(value, "\\color{")) {
        let group = util.read_balanced(value, len("\\color"), "{", "}")^
        let color = opts.color_value(group.raw)^
        let prepared = prepare(slice(value, group.next, len(value)))^;
        {element: <span style: "color:" ++ color, prepared.element>,
         width_em: prepared.width_em, height_em: prepared.height_em,
         depth_em: prepared.depth_em}
    } else if (font_command != null) {
        let remaining = trim(slice(value, len(font_command), len(value)))
        let body = if (starts_with(remaining, "{") and ends_with(remaining, "}"))
            slice(remaining, 1, len(remaining) - 1) else remaining
        let prepared = prepare(body)^
        let size = font_size(font_command);
        {element: <span style: "font-size:" ++ size ++ ";", prepared.element>,
         width_em: prepared.width_em, height_em: prepared.height_em,
         depth_em: prepared.depth_em}
    } else {
    let textcolor_prefix = "\\textcolor{"
    if (starts_with(value, textcolor_prefix)) {
        let color_start = len(textcolor_prefix)
        let color_end = index_of(slice(value, color_start, len(value)), "}")
        let body_open = if (color_end == null) null else color_start + color_end + 1
        let valid = body_open != null and slice(value, body_open, body_open + 1) == "{" and
            ends_with(value, "}")
        if (not valid) raise error("malformed TikZ textcolor label")
        else {
            let color = slice(value, color_start, body_open - 1)
            let body = slice(value, body_open + 1, len(value) - 1)
            let supported_color = len([for (candidate in ["black", "blue", "red", "green",
                "darkgreen", "orange", "purple", "gray"] where candidate == color) candidate]) > 0
            if (not supported_color) raise error("unsupported TikZ textcolor: " ++ color)
            else {
                let prepared = prepare(body)^;
                {element: <span style: "color:" ++ color, prepared.element>,
                    width_em: prepared.width_em, height_em: prepared.height_em,
                    depth_em: prepared.depth_em}
            }
        }
    } else if (starts_with(value, "$") and ends_with(value, "$") and len(value) >= 2) {
        let ast = parse(slice(value, 1, len(value) - 1), {type: "math"})^
        let measured = math.render_box(ast, {display: false})
        {element: math.render_box_element(measured),
            width_em: measured.width, height_em: measured.height,
            depth_em: measured.depth}
    } else if (contains(value, "$")) {
        let parts = mixed_parts(value, 0, [])^;
        {element: <span for (part in parts) part>,
            width_em: null, height_em: null, depth_em: null}
    } else {
        // CSS measures ordinary text at paint time; the geometry caller must
        // reserve space independently before it admits long labels.
        {element: <span plain_text(value)>,
            width_em: null, height_em: null, depth_em: null}
    }
    }
}

pub fn positioned(prepared, x, y, extra_style = "") {
    let style = "position:absolute;left:" ++ string(x) ++ "px;top:" ++ string(y) ++
        "px;white-space:nowrap;transform:translate(-50%,-50%);" ++ extra_style;
    <span class: "tikz-label", style: style, prepared.element>
}

pub fn plain_title(source) string^ {
    // PGFPlots title font declarations affect style, not the displayed words.
    let text = replace(replace(trim(source), "\\large", ""), "\\bfseries", "")
    if (contains(text, "\\")) raise error("unsupported PGFPlots title command")
    else replace(replace(text, "{", ""), "}", "")
}
