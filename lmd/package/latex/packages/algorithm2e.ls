// algorithm2e: the algorithm environment's body is the pseudocode itself. \;
// ends a statement; block commands carry their bodies as brace arguments,
// which the parser flattens, so each argument is read back from its raw source.

import util: ~~.util

// block commands: words of the head line, whether the condition is an argument,
// the words after it, whether the block closes with an end line
let BLOCKS = {
    If: {head: ["if"], condition: true, after: "then", closes: true},
    uIf: {head: ["if"], condition: true, after: "then", closes: false},
    eIf: {head: ["if"], condition: true, after: "then", closes: true, otherwise: true},
    ElseIf: {head: ["else", "if"], condition: true, after: "then", closes: true},
    uElseIf: {head: ["else", "if"], condition: true, after: "then", closes: false},
    Else: {head: ["else"], condition: false, closes: true},
    uElse: {head: ["else"], condition: false, closes: false},
    For: {head: ["for"], condition: true, after: "do", closes: true},
    ForEach: {head: ["foreach"], condition: true, after: "do", closes: true},
    ForAll: {head: ["forall"], condition: true, after: "do", closes: true},
    While: {head: ["while"], condition: true, after: "do", closes: true},
    Repeat: {head: ["repeat"], condition: true, closes: false, until: true}
}

// input/output lines: \KwIn{x} prints "Input: x"
pub let IO_LABELS = {KwIn: "Input:", KwOut: "Output:", KwData: "Data:", KwResult: "Result:"}

// settings that shape every algorithm; scoping to one algorithm is not modelled
pub let SETTINGS = ["SetAlgoLined", "SetAlgoNoLine", "SetAlgoVlined", "LinesNumbered",
    "LinesNotNumbered", "DontPrintSemicolon", "PrintSemicolon", "SetAlgoNoEnd", "SetAlgoLongEnd",
    "SetAlgoShortEnd", "BlankLine", "IncMargin", "DecMargin", "SetKwInOut", "SetKwInput"]

pub let COMMANDS = [for (k, v at BLOCKS) string(k)] ++ [for (k, v at IO_LABELS) string(k)] ++
    ["KwRet", "KwTo", "tcp", "tcc", "tcp*", "tcc*", "SetKwFunction", "SetKwProg", "SetKw",
     "SetKwData", "ResetInOut", "nl", "lnl"] ++ SETTINGS

// The lines of an algorithm body: statement, head, end, io and blank records with
// a depth; other block elements (captions, labels) come back separately.
pub fn fold(nodes, depth, settings) {
    fold_rec(nodes, 0, depth, settings, [], [], [])
}

fn is_blank(nodes) => all([for (node in nodes) node is string and len(trim(node)) == 0])

fn flush(current, acc, depth, semicolon) =>
    if (is_blank(current)) acc else acc ++ [{kind: "statement", nodes: current, depth: depth, semicolon: semicolon}]

fn fold_rec(nodes, i, depth, settings, current, acc, blocks) {
    if (i >= len(nodes)) {lines: flush(current, acc, depth, false), blocks: blocks}
    else {
        let node = nodes[i]
        let tag = if (node is element) string(name(node)) else null
        if (node is symbol and string(node) == ";")
            fold_rec(nodes, i + 1, depth, settings, [], flush(current, acc, depth, true), blocks)
        else if (tag != null and BLOCKS[tag] != null) {
            let block = block_lines(node, BLOCKS[tag], depth, settings)
            fold_rec(nodes, i + 1, depth, settings, [], flush(current, acc, depth, false) ++ block.lines,
                blocks ++ block.blocks)
        }
        else if (tag != null and IO_LABELS[tag] != null)
            fold_rec(nodes, i + 1, depth, settings, [], flush(current, acc, depth, false) ++
                [{kind: "io", label: IO_LABELS[tag], nodes: util.command_args(node), depth: depth}], blocks)
        else if (tag == "BlankLine")
            fold_rec(nodes, i + 1, depth, settings, [], flush(current, acc, depth, false) ++
                [{kind: "blank", depth: depth}], blocks)
        else if (tag != null and (contains(SETTINGS, tag) or false))
            fold_rec(nodes, i + 1, depth, settings, current, acc, blocks)
        // a label names the algorithm, so it is an anchor, not a line
        else if (tag == "caption" or tag == "caption*" or tag == "label")
            fold_rec(nodes, i + 1, depth, settings, current, acc, blocks ++ [node])
        else fold_rec(nodes, i + 1, depth, settings, current ++ [node], acc, blocks)
    }
}

// a block command's argument i as nodes, read back from its raw source
pub fn argument(el, i) {
    let raw = util.raw_argument(el, "required", i)
    let parsed = if (raw == null) null else parse(raw, {type: "latex"}) ^ { null }
    if (parsed == null) [] else util.flatten_paragraphs(content(parsed))
}

fn block_lines(el, desc, depth, settings) {
    let condition = if (desc.condition) argument(el, 0) else []
    let body_index = if (desc.condition) 1 else 0
    let body = fold(argument(el, body_index), depth + 1, settings)
    let otherwise = if (desc.otherwise == true) fold(argument(el, 2), depth + 1, settings) else null
    let head = {kind: "head", words: desc.head, condition: if (desc.until == true) [] else condition,
        after: desc.after, depth: depth}
    let closing = if (desc.until == true) [{kind: "head", words: ["until"], condition: condition, depth: depth}]
        else if (desc.closes and not settings.noend) [{kind: "end", words: ["end"], depth: depth}]
        else []
    let else_lines = if (otherwise == null) []
        else [{kind: "head", words: ["else"], condition: [], depth: depth}] ++ otherwise.lines;
    {lines: [head] ++ body.lines ++ else_lines ++ closing,
     blocks: body.blocks ++ (if (otherwise == null) [] else otherwise.blocks)}
}

// numbers every line but blanks when lines are numbered
pub fn numbered(lines, on) => number_rec(lines, 0, 0, on, [])

fn number_rec(lines, i, count, on, acc) {
    if (i >= len(lines)) acc
    else {
        let counts = on and lines[i].kind != "blank"
        let next = if (counts) count + 1 else count
        number_rec(lines, i + 1, next, on, acc ++ [{*:lines[i], number: if (counts) next else null}])
    }
}
