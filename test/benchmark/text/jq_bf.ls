// Text benchmark jq_bf: jq/bf.jq (jaq's Brainfuck interpreter written in jq)
// as a Lambda query. It keeps bf.jq's state record and algorithm: one
// character per step, `[`/`]` matched by scanning (skip_loop/backward_loop,
// no precomputed jump table), memory padded with nulls by assign(i; f)
// (vibe/impl/Lambda_Impl_Jq_Tests.md §5.6).
//   . as $prog | reduce range(rounds) as $r (0;
//     (. * 31 + ($prog | bf | explode | add) + $r) % 1000000007)
import ~~.jq_query_common

let rounds = 10

// .input[.cursor:.cursor+1]
fn char_at(s) => slice(s.input, s.cursor, s.cursor + 1)

// .memory[.pointer], null past the end as in jq
fn cell_at(memory, i) => if (i < len(memory)) memory[i] else null

// (. + delta) % 256 with jq's null + n == n
fn cell_add(v, delta) => ((v or 0) + delta) % 256

// jq orders null below every number, so `.memory[.pointer] > 0` is false for null
fn is_positive(v) => v != null and v > 0

// assign(i; f): .[i] |= f inside the array, else pad with nulls and append f(null)
pn assign_cell(memory, i, delta) {
    var out = memory
    if (i >= len(out)) {
        while (len(out) <= i) { out.push(null) }
    }
    out[i] = cell_add(out[i], delta)
    out
}

// skip_loop: last(recurse(step)) | .cursor += 1 | .depth -= 1, where step
// stops at the `]` that drops depth below saved_depth
pn skip_loop(s) map^ {
    var t = s
    while (true) {
        let c = char_at(t)
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

// backward_loop: last(recurse(step)) | .depth -= 1, where step stops at the
// `[` whose decrement no longer leaves depth above saved_depth
pn backward_loop(s) map^ {
    var t = s
    while (true) {
        let c = char_at(t)
        if (c == "[" and not (t.saved_depth < t.depth - 1)) { break }
        t.cursor = t.cursor - 1
        if (c == "[") { t.depth = t.depth - 1 }
        if (c == "]") { t.depth = t.depth + 1 }
        if (c != "[" and c != "]" and t.cursor < 0) { raise error("unmatching loop") }
    }
    t.depth = t.depth - 1
    t
}

pn bf(program) string^ {
    var s = {input: program, cursor: 0, memory: [], pointer: 0, depth: 0, output: [], saved_depth: null}
    while (not (s.cursor >= len(s.input))) {
        let c = char_at(s)
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
    let program = input("test/benchmark/text/jq/fib.bf", 'text')^
    let t0 = clock()
    var checksum = 0
    var r = 0
    while (r < rounds) {
        // a pn's `^ { … }` handler is statement-only (S7.6.7v4), so propagate
        let text = bf(program)^
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
