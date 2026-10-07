// Preserve the existing TikZ script engine as the sole drawing implementation.
import bridge: ~~.tikz_bridge
import util: ~~.util
import people: lambda.doc.tikz.people

fn declarations_in(node) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        if (tag == "tikzset") {
            let raw = util.raw_argument(node, "required", 0)
            if (raw == null) [] else [raw]
        } else [for (child in node, declaration in declarations_in(child))
            declaration]
    }
}

pub fn preamble_declarations(ast) => [for (child in ast,
    declaration in if (child is element and string(name(child)) != "document")
        declarations_in(child) else []) declaration]

fn math_in(node) {
    if (not (node is element)) []
    else {
        let tag = string(name(node))
        if (tag == "pgfmathsetmacro")
            [{kind: "setmacro", target: util.raw_argument(node, "required", 0),
              source: util.raw_argument(node, "required", 1),
              offset: node.source_offset}]
        else if (tag == "pgfmathdeclarefunction")
            [{kind: "function", name: util.raw_argument(node, "required", 0),
              arity: util.raw_argument(node, "required", 1),
              source: util.raw_argument(node, "required", 2),
              offset: node.source_offset}]
        else [for (child in node, declaration in math_in(child)) declaration]
    }
}

pub fn math_declarations(ast) => [for (child in ast,
    declaration in math_in(child)) declaration]

pub fn render_picture(island, definitions = [], declarations = [],
                      math_declarations = [], custom_colors = [],
                      people_active = false) =>
    bridge.render_picture(island, definitions, declarations,
        math_declarations, custom_colors, people_active)

pub fn render_gallery(command) any^ {
    let size = util.raw_argument(command, "required", 0)
    let options = util.raw_argument(command, "required", 1)
    if (size == null or options == null)
        raise error("alltikzpeople needs size and options")
    else people.gallery(size, options,
        string(name(command)) != "alltikzpeople*")^
}
