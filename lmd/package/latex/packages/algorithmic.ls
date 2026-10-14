// Pseudocode of algorithmicx's algpseudocode and the older algorithmic package.
// Statement commands open lines; block commands indent the lines between them.
// Analysis numbers the lines for \label; render draws them.

import util: ~~.util

// Statement keywords. role: line (a numbered statement), unnumbered, open (indents
// what follows), middle (outdents itself, indents what follows) or close
// (outdents). words name the \algorithmic<word> labels the line shows; after
// follows the condition. call shows its first two arguments as Name(args).
let PSEUDOCODE = {
    State: {role: "line"}, Statex: {role: "unnumbered"},
    Require: {role: "unnumbered", words: ["require"]}, Ensure: {role: "unnumbered", words: ["ensure"]},
    If: {role: "open", words: ["if"], after: "then"}, ElsIf: {role: "middle", words: ["else", "if"], after: "then"},
    Else: {role: "middle", words: ["else"]}, EndIf: {role: "close", words: ["end", "if"]},
    For: {role: "open", words: ["for"], after: "do"}, ForAll: {role: "open", words: ["forall"], after: "do"},
    EndFor: {role: "close", words: ["end", "for"]},
    While: {role: "open", words: ["while"], after: "do"}, EndWhile: {role: "close", words: ["end", "while"]},
    Repeat: {role: "open", words: ["repeat"]}, Until: {role: "close", words: ["until"]},
    Loop: {role: "open", words: ["loop"]}, EndLoop: {role: "close", words: ["end", "loop"]},
    Procedure: {role: "open", words: ["procedure"], call: true},
    EndProcedure: {role: "close", words: ["end", "procedure"]},
    Function: {role: "open", words: ["function"], call: true},
    EndFunction: {role: "close", words: ["end", "function"]}
}

// the older algorithmic package spells its statements in capitals
let ALGORITHMIC = {
    STATE: {role: "line"}, REQUIRE: {role: "unnumbered", words: ["require"]},
    ENSURE: {role: "unnumbered", words: ["ensure"]},
    IF: {role: "open", words: ["if"], after: "then"}, ELSIF: {role: "middle", words: ["else", "if"], after: "then"},
    ELSE: {role: "middle", words: ["else"]}, ENDIF: {role: "close", words: ["end", "if"]},
    FOR: {role: "open", words: ["for"], after: "do"}, FORALL: {role: "open", words: ["forall"], after: "do"},
    ENDFOR: {role: "close", words: ["end", "for"]},
    WHILE: {role: "open", words: ["while"], after: "do"}, ENDWHILE: {role: "close", words: ["end", "while"]},
    REPEAT: {role: "open", words: ["repeat"]}, UNTIL: {role: "close", words: ["until"]},
    LOOP: {role: "open", words: ["loop"]}, ENDLOOP: {role: "close", words: ["end", "loop"]},
    RETURN: {role: "line", words: ["return"]}, PRINT: {role: "line", words: ["print"]}
}

// inline keywords: algpseudocode's \Return, the older package's logical words
pub let INLINE_WORDS = {Return: "return", AND: "and", OR: "or", XOR: "xor", NOT: "not",
    TO: "to", TRUE: "true", FALSE: "false"}

// label words whose default text differs from the word
let DEFAULT_WORDS = {require: "Require:", ensure: "Ensure:", forall: "for all"}

pub fn word_text(word) => if (DEFAULT_WORDS[word] != null) DEFAULT_WORDS[word] else word

// the \algorithmic<word> label macros a document may redefine
pub let LABEL_MACROS = [for (word in ["require", "ensure", "if", "then", "else", "end", "for", "forall",
    "do", "while", "repeat", "until", "loop", "procedure", "function", "return", "print"])
    "algorithmic" ++ word]

pub let PSEUDOCODE_COMMANDS = [for (k, v at PSEUDOCODE) string(k)] ++ ["Return", "Comment", "Call",
    "algnewcommand", "algrenewcommand", "algdef", "algblock", "algblockdefx", "algloop",
    "algcblock", "algcloop"] ++ LABEL_MACROS
pub let ALGORITHMIC_COMMANDS = [for (k, v at ALGORITHMIC) string(k)] ++
    ["COMMENT", "AND", "OR", "XOR", "NOT", "TO", "TRUE", "FALSE"]

fn keyword_of(node) {
    if (not (node is element)) null
    else {
        let tag = string(name(node))
        if (PSEUDOCODE[tag] != null) PSEUDOCODE[tag] else ALGORITHMIC[tag]
    }
}

// The lines of an algorithmic environment: {node, desc, items, depth, number,
// shown, label}. [n] numbers every line and shows every n-th number.
pub fn lines(el) {
    let opened = util.leading_option(el)
    let flat = util.flatten_paragraphs(opened.children)
    let every = if (opened.option == null) 0 else (int(trim(opened.option)) ^ { 0 })
    let starts = [for (i, node in flat where keyword_of(node) != null) i]
    let raw = [for (k, start in starts,
        let stop = if (k + 1 < len(starts)) starts[k + 1] else len(flat))
        {node: flat[start], desc: keyword_of(flat[start]),
         rest: slice(flat, start + 1, stop)}]
    number_lines(raw, 0, 0, 0, every, [])
}

fn number_lines(raw, i, depth, count, every, acc) {
    if (i >= len(raw)) acc
    else {
        let line = raw[i]
        let role = line.desc.role
        let own_depth = if (role == "middle" or role == "close") max(depth - 1, 0) else depth
        let next_depth = if (role == "open" or role == "middle") own_depth + 1 else own_depth
        let numbered = role != "unnumbered"
        let next_count = if (numbered) count + 1 else count
        let labels = [for (node in line.rest where node is element and string(name(node)) == "label")
            trim(util.text_of(node))]
        number_lines(raw, i + 1, next_depth, next_count, every, acc ++ [{
            node: line.node, desc: line.desc, depth: own_depth,
            items: [for (node in line.rest where not (node is element and string(name(node)) == "label")) node],
            number: if (numbered) next_count else null,
            shown: if (numbered and every > 0 and next_count % every == 0) next_count else null,
            label: if (len(labels) == 0) null else labels[0]}])
    }
}
