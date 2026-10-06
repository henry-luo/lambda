// Text benchmark jq_bf, typed: jq/bf.jq (jaq's Brainfuck interpreter written
// in jq) as a typed Lambda query. Same algorithm as jq_bf.ls: bf.jq's state
// record, one character per step, `[`/`]` matched by scanning, memory padded
// with nulls (vibe/impl/Lambda_Impl_Jq_Tests.md §5.6).
import ~~.jq_query_typed

let rounds: int = 10

// bf.jq's state object; memory cells may be null, as jq's padding leaves them
type BfState = {input: string, cursor: int, memory: array, pointer: int, depth: int,
    output: int[], saved_depth: int?}

fn char_at(s: BfState) string => slice(s.input, s.cursor, s.cursor + 1) or ""

// .memory[.pointer], null past the end as in jq
fn cell_at(memory: array, i: int) => if (i < len(memory)) memory[i] else null

// (. + delta) % 256 with jq's null + n == n
fn cell_add(v, delta: int) int => ((v or 0) + delta) % 256

// jq orders null below every number, so `.memory[.pointer] > 0` is false for null
fn is_positive(v) bool => v != null and v > 0

// assign(i; f): .[i] |= f inside the array, else pad with nulls and append f(null)
pn assign_cell(memory: array, i: int, delta: int) array {
    var out: array = memory
    if (i >= len(out)) {
        while (len(out) <= i) { out.push(null) }
    }
    out[i] = cell_add(out[i], delta)
    out
}

// skip_loop: last(recurse(step)) | .cursor += 1 | .depth -= 1
pn skip_loop(s: BfState) BfState^ {
    var t: BfState = s
    while (true) {
        let c: string = char_at(t)
        if (c == "") { raise error("unmatching loop") }
        if (c == "]" and t.depth - 1 < t.saved_depth) { break }
        t.cursor = t.cursor + 1
        if (c == "[") { t.depth = t.depth + 1 }
        if (c == "]") { t.depth = t.depth - 1 }
    }
    t.cursor = t.cursor + 1
    t.depth = t.depth - 1
    t
}

// backward_loop: last(recurse(step)) | .depth -= 1
pn backward_loop(s: BfState) BfState^ {
    var t: BfState = s
    while (true) {
        let c: string = char_at(t)
        if (c == "[" and not (t.saved_depth < t.depth - 1)) { break }
        t.cursor = t.cursor - 1
        if (c == "[") { t.depth = t.depth - 1 }
        if (c == "]") { t.depth = t.depth + 1 }
        if (c != "[" and c != "]" and t.cursor < 0) { raise error("unmatching loop") }
    }
    t.depth = t.depth - 1
    t
}

pn bf(program: string) string^ {
    var s: BfState = {input: program, cursor: 0, memory: [], pointer: 0, depth: 0,
        output: [], saved_depth: null}
    while (not (s.cursor >= len(s.input))) {
        let c: string = char_at(s)
        s.cursor = s.cursor + 1
        if (c == ">") { s.pointer = s.pointer + 1 }
        else if (c == "<") {
            s.pointer = s.pointer - 1
            if (s.pointer < 0) { raise error("negative pointer") }
        }
        else if (c == "+") { s.memory = assign_cell(s.memory, s.pointer, 1) }
        else if (c == "-") { s.memory = assign_cell(s.memory, s.pointer, 255) }
        else if (c == ".") { s.output = s.output ++ [cell_at(s.memory, s.pointer)] }
        else if (c == ",") { raise error(", is not implemented") }
        else if (c == "[") {
            s.depth = s.depth + 1
            if (not is_positive(cell_at(s.memory, s.pointer))) {
                s.saved_depth = s.depth
                s = skip_loop(s)^
            }
        }
        else if (c == "]") {
            s.depth = s.depth - 1
            s.cursor = s.cursor - 1
            s.saved_depth = s.depth
            s = backward_loop(s)^
        }
    }
    // .output | implode
    join([for (cp in s.output) chr(cp)], "")
}

pn main() {
    let program: string = input("test/benchmark/text/jq/fib.bf", 'text')^
    let t0 = clock()
    var checksum: int = 0
    var r: int = 0
    while (r < rounds) {
        // a pn's `^ { … }` handler is statement-only (S7.6.7v4), so propagate
        let text: string = bf(program)^
        checksum = (checksum * 31 + jq_codepoint_sum(text) + r) % jq_modulus
        r = r + 1
    }
    let t1 = clock()
    if (checksum == 478890292) {
        print("jq_bf: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("jq_bf: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((t1 - t0) * 1000.0) ++ "\n")
}
