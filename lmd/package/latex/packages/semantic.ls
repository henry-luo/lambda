// Bounded semantic inference rules and T-diagram source model (D7.2.1–D7.2.4).
import util: ~~.util
import math: lambda.doc.math.math

fn skip_space(source, at) =>
    if (at >= len(source)) at
    else if (slice(source, at, at + 1) == " " or
             slice(source, at, at + 1) == "\n" or
             slice(source, at, at + 1) == "\t" or
             slice(source, at, at + 1) == "\r")
        skip_space(source, at + 1)
    else at

fn math_fragment(source) any^ {
    let clean = replace(trim(source), "$", "")
    if (clean == "") <span>
    else math.render_inline(parse(clean, {type: "math"})^)
}

fn predicate_formula(source) any^ {
    let turnstile = index_of(source, "|-")
    let arrow = index_of(source, "=>")
    if (turnstile == null or arrow == null or arrow <= turnstile)
        raise error("unsupported semantic predicate expression")
    else {
        let value_start = skip_space(source, arrow + 2)
        let value = util.read_balanced(source, value_start, "{", "}")^
        let context = slice(source, 0, turnstile)
        let expression = replace(trim(slice(source, turnstile + 2, arrow)), "$", "")
        let remainder = slice(source, value.next, len(source))
        let context_math = math_fragment(context)^
        let value_math = math_fragment(value.raw)^
        let remainder_math = math_fragment(remainder)^;
        <span class: "latex-semantic-predicate",
            context_math;
            <span class: "latex-semantic-turnstile", "⊢">;
            <code class: "latex-semantic-expression", expression>;
            <span class: "latex-semantic-arrow",
                <sup value_math>; "⇒"; <sub "S">>;
            remainder_math>
    }
}

fn formula(source, predicate_mode) any^ {
    let clean = trim(source)
    if (starts_with(clean, "\\inference")) {
        let parsed = parse_rule(clean, 0, predicate_mode)^
        if (trim(slice(clean, parsed.next, len(clean))) != "")
            raise error("unsupported content after nested semantic inference")
        else parsed.graphic
    } else if (predicate_mode) predicate_formula(clean)^
    else math_fragment(clean)^
}

fn render_rule(premises, conclusion, rule_name, narrow, predicate_mode) any^ {
    let entries = [for (raw in util.split_top_level(premises, "&")
        where trim(raw) != "") formula(raw, predicate_mode)^]
    let result = formula(conclusion, predicate_mode)^
    let label = if (rule_name == null) null else math_fragment(rule_name)^;
    <span class: if (narrow) "latex-semantic-rule latex-semantic-rule-narrow"
            else "latex-semantic-rule",
        if (len(entries) > 0)
            <span class: "latex-semantic-premises",
                for (entry in entries) <span class: "latex-semantic-premise", entry>>;
        <span class: "latex-semantic-conclusion", result>;
        if (label != null) <span class: "latex-semantic-rule-name", label>
    >
}

fn parse_rule(source, at, predicate_mode) map^ {
    let token = "\\inference"
    if (slice(source, at, at + len(token)) != token)
        raise error("expected semantic inference")
    else {
        let suffix = at + len(token)
        let narrow = slice(source, suffix, suffix + 1) == "*"
        let head = skip_space(source, if (narrow) suffix + 1 else suffix)
        let prefix_label = if (slice(source, head, head + 1) == "[")
            util.read_balanced(source, head, "[", "]")^ else null
        let premise_at = skip_space(source,
            if (prefix_label == null) head else prefix_label.next)
        let premises = util.read_balanced(source, premise_at, "{", "}")^
        let conclusion_at = skip_space(source, premises.next)
        let conclusion = util.read_balanced(source, conclusion_at, "{", "}")^
        let tail = skip_space(source, conclusion.next)
        let suffix_label = if (slice(source, tail, tail + 1) == "[")
            util.read_balanced(source, tail, "[", "]")^ else null
        if (prefix_label != null and suffix_label != null)
            raise error("semantic inference has two rule names")
        else {
            let label = if (prefix_label != null) prefix_label.raw
                else if (suffix_label != null) suffix_label.raw else null
            let next = if (suffix_label == null) conclusion.next else suffix_label.next
            {graphic: render_rule(premises.raw, conclusion.raw, label,
                narrow, predicate_mode)^, next: next}
        }
    }
}

pub fn render_inference_display(node) any^ {
    let source = if (node.source == null) "" else node.source
    let found = index_of(source, "\\inference")
    if (found == null) raise error("semantic display has no inference")
    else {
        let prefix = trim(slice(source, 0, found))
        // The supported declaration formats predicates as context, code, arrow, value.
        let predicate_mode = starts_with(prefix,
            "\\def\\predicatebegin#1|-#2=>#3#4\\predicateend") and
            (contains(prefix, "\\texttt{#2}") ^ { false }) and
            (contains(prefix, "\\stackrel{#3}{\\Rightarrow}_S") ^ { false })
        if (prefix != "" and not predicate_mode)
            raise error("unsupported semantic predicate declaration")
        else {
            let parsed = parse_rule(source, found, predicate_mode)^
            if (trim(slice(source, parsed.next, len(source))) != "")
                raise error("unsupported content after semantic inference")
            else <div class: "math-display-container latex-semantic-display",
                parsed.graphic>
        }
    }
}

pub fn contains_inference(node) {
    node.source != null and (contains(node.source, "\\inference") ^ { false })
}

fn superscript_char(ch) string^ {
    match ch {
        case "-": "⁻"
        case "+": "⁺"
        case "0": "⁰"
        case "1": "¹"
        case "2": "²"
        case "3": "³"
        case "4": "⁴"
        case "5": "⁵"
        case "6": "⁶"
        case "7": "⁷"
        case "8": "⁸"
        case "9": "⁹"
        default: raise error("unsupported semantic diagram superscript")
    }
}

fn superscript_text(raw, at, output) string^ {
    if (at >= len(raw)) output
    else superscript_text(raw, at + 1, output ++
        superscript_char(slice(raw, at, at + 1))^)^
}

fn label_text(raw) string^ {
    let source = trim(raw)
    let math_at = index_of(source, "$^{")
    if (math_at == null) {
        if (contains(source, "\\") or contains(source, "$"))
            raise error("unsupported semantic diagram label")
        else source
    } else {
        let exponent = util.read_balanced(source, math_at + 2, "{", "}")^
        if (slice(source, exponent.next, exponent.next + 1) != "$")
            raise error("unsupported semantic diagram math label")
        else {
            let base = slice(source, 0, math_at)
            let rest = slice(source, exponent.next + 1, len(source))
            base ++ superscript_text(exponent.raw, 0, "")^ ++ label_text(rest)^
        }
    }
}

fn diagram_command(source, token, parts) map^ {
    let args_at = skip_space(source, len(token))
    let args = util.read_balanced(source, args_at, "{", "}")^
    if (trim(slice(source, args.next, len(source))) != "")
        raise error("content after semantic diagram command")
    else {
        let raw_parts = util.split_top_level(args.raw, ",")
        if (len(raw_parts) != parts)
            raise error("semantic diagram command has incorrect arity")
        else {kind: slice(token, 1, len(token)),
            children: [for (part in raw_parts) parse_diagram(part)^]}
    }
}

fn parse_diagram(raw) map^ {
    let source = trim(raw)
    if (starts_with(source, "\\compiler"))
        diagram_command(source, "\\compiler", 3)^
    else if (starts_with(source, "\\interpreter"))
        diagram_command(source, "\\interpreter", 2)^
    else if (starts_with(source, "\\program"))
        diagram_command(source, "\\program", 2)^
    else if (starts_with(source, "\\machine")) {
        let opt_at = skip_space(source, len("\\machine"))
        let optional = if (slice(source, opt_at, opt_at + 1) == "[")
            util.read_balanced(source, opt_at, "[", "]")^ else null
        let body_at = skip_space(source,
            if (optional == null) opt_at else optional.next)
        let body = util.read_balanced(source, body_at, "{", "}")^
        if (trim(slice(source, body.next, len(source))) != "")
            raise error("content after semantic machine")
        else {kind: "machine", label: label_text(if (optional == null)
            body.raw else optional.raw)^, language: label_text(body.raw)^,
            children: []}
    } else {kind: "text", label: label_text(source)^, children: []}
}

fn max_height(nodes, at, current) {
    if (at >= len(nodes)) current
    else {
        let height = dimensions(nodes[at]).height
        max_height(nodes, at + 1, if (height > current) height else current)
    }
}

fn sum_width(nodes, at, total) {
    if (at >= len(nodes)) total
    else sum_width(nodes, at + 1, total + dimensions(nodes[at]).width)
}

fn dimensions(node) {
    if (node.kind == "text") {width: 48.0, height: 24.0}
    else if (node.kind == "machine") {width: 48.0, height: 46.0}
    else {
        let columns = len(node.children)
        let width = sum_width(node.children, 0, 0.0) +
            float(columns - 1) * 16.0 + 24.0
        let high = max_height(node.children, 0, 0.0)
        {width: if (width < 112.0) 112.0 else width,
         height: high + 78.0}
    }
}

fn svg_text(x, y, content) {
    <text x: string(x), y: string(y), 'text-anchor': "middle",
        'font-family': "serif", 'font-size': "16", fill: "black", content>
}

fn draw_children(nodes, at, x, y, acc) {
    if (at >= len(nodes)) acc
    else {
        let child = nodes[at]
        let child_width = dimensions(child).width
        let graphic = draw_diagram(child, x, y)
        draw_children(nodes, at + 1, x + child_width + 16.0,
            y, acc ++ [graphic])
    }
}

fn draw_diagram(node, x, y) {
    let size = dimensions(node)
    let mid = x + size.width / 2.0
    if (node.kind == "text") svg_text(mid, y + 18.0, node.label)
    else if (node.kind == "machine")
        <g
            <line x1: string(mid), y1: string(y + 2.0),
                x2: string(mid), y2: string(y + 25.0), stroke: "black">;
            svg_text(mid, y + 43.0, node.label)
        >
    else {
        let left = x + 12.0
        let row = y + 72.0
        let child_graphics = draw_children(node.children, 0, left, row, [])
        let tip = if (node.kind == "compiler") "→"
            else if (node.kind == "interpreter") "↓" else "▣";
        <g
            <line x1: string(x + 8.0), y1: string(y + 29.0),
                x2: string(x + size.width - 8.0), y2: string(y + 29.0),
                stroke: "black", 'stroke-width': "1.2">;
            <line x1: string(mid), y1: string(y + 29.0),
                x2: string(mid), y2: string(y + 62.0),
                stroke: "black", 'stroke-width': "1.2">;
            svg_text(mid, y + 21.0, tip);
            for (graphic in child_graphics) graphic
        >
    }
}

pub fn contains_diagram(node) {
    util.find_descendant(node, "compiler") != null or
    util.find_descendant(node, "program") != null or
    util.find_descendant(node, "interpreter") != null
}

pub fn render_tdiagram_picture(node) any^ {
    let paragraph = if (len(node) > 0 and node[0] is element) node[0] else node
    let puts = [for (child in paragraph where child is element and
        string(name(child)) == "put") child]
    if (len(puts) != 1)
        raise error("semantic T-diagram profile expects one picture put")
    else {
        let source = util.raw_argument(puts[0], "required", 0)
        if (source == null) raise error("semantic T-diagram put has no body")
        else {
            let diagram = parse_diagram(source)^
            let size = dimensions(diagram)
            let graphic = draw_diagram(diagram, 0.0, 0.0);
            <svg class: "latex-picture latex-semantic-tdiagram",
                xmlns: "http://www.w3.org/2000/svg",
                viewBox: "0 0 " ++ string(size.width) ++ " " ++ string(size.height),
                width: string(size.width) ++ "px",
                height: string(size.height) ++ "px",
                style: "max-width:100%;height:auto", graphic>
        }
    }
}
