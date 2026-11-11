// Experimental TeX macro expansion. Unsupported primitives remain outside this API.
import stream: .stream
import reader: .reader
import primitives: .primitives

fn result(input, definitions, groups, output, count, issue = null) =>
    {input: input, definitions: definitions, groups: groups, output: output,
        conditions: [], counts: {}, toks: {}, lowercase: {}, uppercase: {},
        parameters: {endlinechar: input.reader.endline, escapechar: 92}, after_assignment: null, token_text_scan: false,
        messages: [], expansions: count, issue: issue}
fn fail(st, code) => {*:st, issue: code}
fn unsupported(st, name) => {*:st, issue: "tex-unsupported-primitive", unsupported: name}
fn charcat(t, cat) => t != null and t.kind == "char" and t.cat == cat
fn is_control(t) => t != null and (t.kind == "control" or t.kind == "active")
fn key(t) => if (t.kind == "active") "active:" ++ t.value else "control:" ++ t.value
fn conditional_name(name) => contains(["if", "ifcat", "ifnum", "ifdim", "ifodd", "ifvmode", "ifhmode", "ifmmode",
    "ifinner", "ifvoid", "ifhbox", "ifvbox", "ifx", "ifeof", "iftrue", "iffalse", "ifcase", "ifdefined",
    "ifcsname", "iffontchar", "ifpdfprimitive", "ifincsname"], name)
fn builtin(name) => primitives.exists(name)
fn meaning(st, t) any^ => if (not is_control(t)) {kind: "char", value: t.value, cat: t.cat}
    else if (st.definitions[key(t)] != null) st.definitions[key(t)]
    else if (t.kind == "control" and builtin(t.value)) {kind: "primitive", name: t.value}
    else {kind: "undefined"}
fn primitive(st, t) any^ => if (is_control(t) and meaning(st, t).kind == "primitive") meaning(st, t).name else null
fn nonspace(input) any^ {
    let read = stream.next(input);
    if (charcat(read.token, 10)) nonspace(read.stream) else read
}

fn body_params(tokens, count, index = 0, output = []) any^ {
    if (index >= len(tokens)) {tokens: output, issue: null}
    else {
        let t = tokens[index];
        if (not charcat(t, 6)) body_params(tokens, count, index + 1, [*output, t])
        else if (index + 1 >= len(tokens)) {tokens: output, issue: "tex-illegal-param"}
        else {
            let n = tokens[index + 1];
            let digit = if (n.kind == "char" and n.cat == 12) index_of("123456789", n.value) else null;
            if (charcat(n, 6)) body_params(tokens, count, index + 2, [*output, t])
            else if (digit == null or digit >= count) {tokens: output, issue: "tex-illegal-param"}
            else body_params(tokens, count, index + 2,
                [*output, {kind: "parameter", value: digit, cat: null, start: t.start, end: n.end}])
        }
    }
}

fn append_delimiter(delimiters, token) any^ {
    let tail = len(delimiters) - 1;
    [*slice(delimiters, 0, tail), [*delimiters[tail], token]]
}

fn set_definition(definitions, name, value) => {*:definitions, *:map([name, value])}

fn assign_meaning(st, name, value, global) any^ =>
    {*:st, definitions: set_definition(st.definitions, name, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, definitions: set_definition(scope.definitions, name, value)}] else st.groups}

fn signature(input, count, prefix = [], delimiters = []) any^ {
    let read = stream.next(input);
    let t = read.token;
    if (t == null) {input: read.stream, issue: "tex-runaway-definition"}
    else if (charcat(t, 1)) {input: stream.push(read.stream, [t]), issue: null,
        count: count, prefix: prefix, delimiters: delimiters, brace_delim: false}
    else if (charcat(t, 6)) {
        let digit = stream.next(read.stream);
        let expected = if (count < 9) slice("123456789", count, count + 1) else "";
        if (charcat(digit.token, 1) and count > 0)
            {input: stream.push(digit.stream, [digit.token]), issue: null, count: count,
                prefix: prefix, delimiters: delimiters, brace_delim: true, brace: digit.token}
        else if (charcat(digit.token, 1)) {input: digit.stream, issue: "tex-unsupported-brace-delimiter"}
        else if (digit.token == null or digit.token.kind != "char" or digit.token.cat != 12 or digit.token.value != expected)
            {input: digit.stream, issue: "tex-param-order"}
        else signature(digit.stream, count + 1, prefix, [*delimiters, []])
    }
    else if (charcat(t, 2)) {input: read.stream, issue: "tex-extra-brace"}
    else if (count == 0) signature(read.stream, count, [*prefix, t], delimiters)
    else signature(read.stream, count, prefix, append_delimiter(delimiters, t))
}

fn definition_tokens_inner(st, budget, depth = 1, output = []) any^ {
    let read = expanded_next(st, budget, true);
    let t = read.token;
    if (read.state.issue != null) {state: read.state, tokens: output}
    else if (t == null) {state: fail(read.state, "tex-runaway-definition"), tokens: output}
    else if (t.direct_toks == true)
        definition_tokens_inner(read.state, budget, depth, [*output, {*:t, noexpand: false, direct_toks: false}])
    else if (charcat(t, 2) and depth == 1) {state: read.state, tokens: output}
    else definition_tokens_inner(read.state, budget,
        depth + if (charcat(t, 1)) 1 else if (charcat(t, 2)) -1 else 0,
        [*output, {*:t, noexpand: false}])
}

fn definition_tokens(st, budget) any^ {
    let scanned = definition_tokens_inner({*:st, token_text_scan: true}, budget)^;
    {*:scanned, state: {*:scanned.state, token_text_scan: st.token_text_scan}}
}

fn define(st, global, long_ok, protected_flag, xpand, budget) any^ {
    let name = nonspace(st.input);
    if (not is_control(name.token)) fail({*:st, input: name.stream}, "tex-missing-cs")
    else {
        let sig = signature(name.stream, 0);
        if (sig.issue != null) fail({*:st, input: sig.input}, sig.issue)
        else {
            let scan = if (xpand) {
                let opening = stream.next(sig.input);
                definition_tokens({*:st, input: opening.stream}, budget)^
            } else {
                let body = stream.argument(sig.input, true);
                {state: if (body.error != null) fail({*:st, input: body.stream}, body.error)
                    else {*:st, input: body.stream}, tokens: body.tokens}
            };
            if (scan.state.issue != null) scan.state
            else {
                let parsed = body_params(scan.tokens, sig.count);
                if (parsed.issue != null) fail(scan.state, parsed.issue)
                else {
                    let k = key(name.token);
                    let value = {kind: "macro", count: sig.count, prefix: sig.prefix, delimiters: sig.delimiters,
                        brace_delim: sig.brace_delim, long_ok: long_ok, protected_flag: protected_flag,
                        body: if (sig.brace_delim) [*parsed.tokens, sig.brace] else parsed.tokens};
                    assign_meaning(scan.state, k, value, global)
                }
            }
        }
    }
}

fn match_prefix(input, prefix, index = 0) any^ {
    if (index == len(prefix)) {input: input, issue: null}
    else {
        let read = stream.next(input);
        if (read.token == null) {input: read.stream, issue: "tex-macro-mismatch"}
        else if (not stream.same_token(read.token, prefix[index]))
            {input: stream.push(read.stream, [read.token]), issue: "tex-macro-mismatch"}
        else match_prefix(read.stream, prefix, index + 1)
    }
}

fn collect_args(input, delimiters, brace_delim, long_ok, args = []) any^ {
    if (len(args) == len(delimiters)) {input: input, args: args, issue: null}
    else {
        let arg = stream.delimited(input, delimiters[len(args)], long_ok,
            brace_delim and len(args) + 1 == len(delimiters));
        if (arg.error != null) {input: arg.stream, args: args, issue: arg.error}
        else collect_args(arg.stream, delimiters, brace_delim, long_ok, [*args, arg.tokens])
    }
}

fn substitute(body, args) => [for (t in body)
    if (t.kind == "parameter") *args[t.value] else t]

fn expand_macro(st, macro, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let matched = match_prefix(st.input, macro.prefix);
        if (matched.issue != null) fail({*:st, input: matched.input}, matched.issue)
        else {
            let args = collect_args(matched.input, macro.delimiters, macro.brace_delim, macro.long_ok);
            if (args.issue != null) fail({*:st, input: args.input}, args.issue)
            else {*:st, input: stream.push(args.input, substitute(macro.body, args.args)),
                expansions: st.expansions + 1}
        }
    }
}

fn assign_let(st, global) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let first = nonspace(target.stream);
        let after_equal = if (other(first.token, "=")) stream.next(first.stream) else first;
        let value = if (other(first.token, "=") and charcat(after_equal.token, 10))
            stream.next(after_equal.stream) else after_equal;
        let advanced = {*:st, input: value.stream};
        if (value.token == null) fail(advanced, "tex-runaway-assignment")
        else assign_meaning(advanced, key(target.token), meaning(st, value.token), global)
    }
}

fn assign_futurelet(st, global) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let first = stream.next(target.stream);
        let second = stream.next(first.stream);
        if (first.token == null or second.token == null) fail({*:st, input: second.stream}, "tex-runaway-assignment")
        else assign_meaning({*:st, input: stream.push(second.stream, [first.token, second.token])},
            key(target.token), meaning(st, second.token), global)
    }
}

fn prefixed(st, global, long_ok, protected_flag, budget) any^ {
    let read = nonspace(st.input);
    let t = read.token;
    let advanced = {*:st, input: read.stream};
    let cmd = primitive(st, t);
    if (cmd == null and (not is_control(t) or
        (meaning(st, t).kind != "register" and meaning(st, t).kind != "token-register")))
        fail(advanced, "tex-bad-prefix")
    else if (cmd == "global") prefixed(advanced, true, long_ok, protected_flag, budget)
    else if (cmd == "long") prefixed(advanced, global, true, protected_flag, budget)
    else if (cmd == "protected") prefixed(advanced, global, long_ok, true, budget)
    else if (contains(["def", "gdef", "edef", "xdef"], cmd))
        define(advanced, global or cmd == "gdef" or cmd == "xdef", long_ok, protected_flag,
            cmd == "edef" or cmd == "xdef", budget)
    else if (cmd == "let" and not long_ok and not protected_flag) assign_let(advanced, global)
    else if (cmd == "futurelet" and not long_ok and not protected_flag) assign_futurelet(advanced, global)
    else if (cmd == "count" and not long_ok and not protected_flag) assign_count(advanced, global, budget, null)
    else if (cmd == "countdef" and not long_ok and not protected_flag) assign_countdef(advanced, global, budget)
    else if (contains(["advance", "multiply", "divide"], cmd) and not long_ok and not protected_flag)
        arithmetic(advanced, global, budget, cmd)
    else if ((cmd == "chardef" or cmd == "mathchardef") and not long_ok and not protected_flag)
        assign_chardef(advanced, global, budget, cmd == "mathchardef")
    else if (cmd == "toks" and not long_ok and not protected_flag) assign_toks(advanced, global, budget, null)
    else if (cmd == "everyeof" and not long_ok and not protected_flag) assign_toks(advanced, global, budget, -1)
    else if (cmd == "toksdef" and not long_ok and not protected_flag) assign_toksdef(advanced, global, budget)
    else if (meaning(st, t).kind == "register" and not long_ok and not protected_flag)
        assign_count(advanced, global, budget, meaning(st, t).index)
    else if (meaning(st, t).kind == "token-register" and not long_ok and not protected_flag)
        assign_toks(advanced, global, budget, meaning(st, t).index)
    else if (cmd == "catcode" and not long_ok and not protected_flag) assign_catcode(advanced, global, budget)
    else if ((cmd == "lccode" or cmd == "uccode") and not long_ok and not protected_flag)
        assign_case_code(advanced, global, budget, cmd == "uccode")
    else if ((cmd == "endlinechar" or cmd == "escapechar") and not long_ok and not protected_flag)
        assign_parameter(advanced, global, budget, cmd)
    else fail(advanced, "tex-bad-prefix")
}

fn noexpand(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input);
        let existing = if (is_control(read.token)) meaning(st, read.token) else null;
        let cmd = primitive(st, read.token);
        let unexpandable = is_control(read.token) and (existing.kind == "relax" or existing.kind == "char" or
            (cmd != null and not primitives.expandable(cmd)));
        let marked = if (is_control(read.token) and not unexpandable)
            {*:read.token, noexpand: true} else read.token;
        {*:st, input: if (marked == null) read.stream else stream.push(read.stream, [marked]),
            expansions: st.expansions + 1}
    }
}

fn expandable(st, t) any^ => is_control(t) and t.noexpand != true and
    (meaning(st, t).kind == "macro" or (primitive(st, t) != null and primitives.expandable(primitive(st, t))))

fn expand_one(st, t, budget) any^ {
    if (not expandable(st, t)) {*:st, input: stream.push(st.input, [t])}
    else if (meaning(st, t).kind == "macro") expand_macro(st, meaning(st, t), budget)
    else if (primitive(st, t) == "noexpand") noexpand(st, budget)
    else if (primitive(st, t) == "expandafter") expand_after(st, budget)
    else if (primitive(st, t) == "csname") csname(st, budget)
    else if (primitive(st, t) == "the") expand_the(st, budget)
    else if (contains(["string", "number", "romannumeral", "meaning"], primitive(st, t)))
        string_primitive(st, primitive(st, t), budget)
    else if (primitive(st, t) == "detokenize" or primitive(st, t) == "unexpanded" or primitive(st, t) == "scantokens")
        token_primitive(st, primitive(st, t), budget)
    else if (primitive(st, t) == "expanded") expanded_primitive(st, budget)
    else if (primitive(st, t) == "pdfstrcmp") strcmp_primitive(st, budget)
    else if (primitive(st, t) == "eTeXrevision") constant_primitive(st, budget, ".6")
    else if (primitive(st, t) == "unless") unless_conditional(st, budget)
    else if (conditional_name(primitive(st, t)) or contains(["fi", "else", "or"], primitive(st, t)))
        condition(st, primitive(st, t), budget)
    else unsupported(st, primitive(st, t))
}

fn expand_after(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let first = stream.next(st.input);
        let second = if (first.token == null) null else stream.next(first.stream);
        let advanced = {*:st, input: if (second == null) first.stream else second.stream,
            expansions: st.expansions + 1};
        if (first.token == null) advanced
        else if (second.token == null) {*:advanced, input: stream.push(advanced.input, [first.token])}
        else {
            let expanded = expand_one(advanced, second.token, budget);
            {*:expanded, input: stream.push(expanded.input, [first.token])}
        }
    }
}

fn expanded_next(st, budget, protect = false) any^ {
    let read = stream.next(st.input);
    let current = {*:st, input: read.stream};
    if (read.token == null or (protect and is_control(read.token) and
        meaning(current, read.token).kind == "macro" and meaning(current, read.token).protected_flag) or
        not expandable(current, read.token)) {state: current, token: read.token}
    else {
        let expanded = expand_one(current, read.token, budget);
        if (expanded.issue != null) {state: expanded, token: null} else expanded_next(expanded, budget, protect)
    }
}

fn expanded_nonspace(st, budget) any^ {
    let read = expanded_next(st, budget);
    if (charcat(read.token, 10)) expanded_nonspace(read.state, budget) else read
}

fn csname_tokens(st, budget, chars = []) any^ {
    let read = expanded_next(st, budget);
    let t = read.token;
    if (read.state.issue != null) {state: read.state, name: ""}
    else if (t == null) {state: fail(read.state, "tex-missing-endcsname"), name: ""}
    else if (primitive(read.state, t) == "endcsname") {state: read.state, name: join(chars, "")}
    else if (t.kind == "char") csname_tokens(read.state, budget, [*chars, t.value])
    else {state: fail(put_back(read.state, t), "tex-missing-endcsname"), name: ""}
}

fn csname(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let scanned = csname_tokens({*:st, expansions: st.expansions + 1}, budget);
        if (scanned.state.issue != null) scanned.state
        else {
            let token = {kind: "control", value: scanned.name, cat: null, start: -1, end: -1};
            let k = key(token);
            {*:scanned.state, input: stream.push(scanned.state.input, [token]),
                definitions: if ((scanned.state.definitions[k] == null and not builtin(scanned.name)) or
                    scanned.state.definitions[k].kind == "undefined")
                    set_definition(scanned.state.definitions, k, {kind: "primitive", name: "relax"})
                    else scanned.state.definitions}
        }
    }
}

fn put_back(st, t) => if (t == null) st else {*:st, input: stream.push(st.input, [t])}
fn other(t, value) => charcat(t, 12) and t.value == value

fn signs(st, budget, negative = false) any^ {
    let read = expanded_nonspace(st, budget);
    if (other(read.token, "-")) signs(read.state, budget, not negative)
    else if (other(read.token, "+")) signs(read.state, budget, negative)
    else {state: read.state, token: read.token, negative: negative}
}

fn digit(t, base) any^ {
    if (t == null or t.kind != "char") null
    else if (t.cat == 12 and index_of("0123456789", t.value) != null) {
        let n = index_of("0123456789", t.value);
        if (n < base) n else null
    }
    else if (base == 16 and (t.cat == 11 or t.cat == 12) and index_of("ABCDEF", t.value) != null)
        index_of("ABCDEF", t.value) + 10
    else null
}

fn digits(st, t, base, negative, budget, count = 0, value = 0) any^ {
    let n = digit(t, base);
    if (st.issue != null) {state: st, value: 0}
    else if (n == null) {
        let after = if (count > 0 and charcat(t, 10)) st else put_back(st, t);
        if (count == 0) {state: fail(after, "tex-missing-number"), value: 0}
        else {state: after, value: if (negative) -value else value}
    } else if (value * base + n > 2147483647)
        {state: fail(st, "tex-number-too-big"), value: 0}
    else {
        let read = expanded_next(st, budget);
        if (read.state.issue != null) {state: read.state, value: 0}
        else digits(read.state, read.token, base, negative, budget, count + 1, value * base + n)
    }
}

fn character_number(st, negative, budget) any^ {
    let read = stream.next(st.input);
    let t = read.token;
    let current = {*:st, input: read.stream};
    if (t == null or not (t.kind == "char" or is_control(t)) or len(t.value) != 1)
        {state: fail(current, "tex-improper-alphabetic"), value: 0}
    else {
        let following = expanded_next(current, budget);
        let after = if (charcat(following.token, 10)) following.state else put_back(following.state, following.token);
        {state: after, value: if (negative) -ord(t.value) else ord(t.value)}
    }
}

fn scan_number(st, budget) any^ {
    let start = signs(st, budget);
    let t = start.token;
    if (start.state.issue != null) {state: start.state, value: 0}
    else if (other(t, "`")) character_number(start.state, start.negative, budget)
    else if (primitive(start.state, t) == "count" or (is_control(t) and meaning(start.state, t).kind == "register")) {
        let index = if (primitive(start.state, t) == "count") scan_register(start.state, budget)^
            else {state: start.state, value: meaning(start.state, t).index};
        let value = count_value(index.state, index.value);
        {state: index.state, value: if (start.negative) -value else value}
    }
    else if (is_control(t) and (meaning(start.state, t).kind == "chardef" or
        meaning(start.state, t).kind == "mathchardef")) {
        let value = meaning(start.state, t).value;
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (contains(["endlinechar", "escapechar"], primitive(start.state, t))) {
        let value = start.state.parameters[primitive(start.state, t)];
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (primitive(start.state, t) == "eTeXversion")
        {state: start.state, value: if (start.negative) -2 else 2}
    else if (contains(["currentgrouplevel", "currentiflevel"], primitive(start.state, t))) {
        let value = if (primitive(start.state, t) == "currentgrouplevel") len(start.state.groups)
            else len(start.state.conditions);
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (primitive(start.state, t) == "numexpr") {
        let expression = scan_expression(start.state, budget)^;
        {state: expression.state, value: if (start.negative) -expression.value else expression.value}
    }
    else if (contains(["catcode", "lccode", "uccode"], primitive(start.state, t))) {
        let code = code_quantity(start.state, primitive(start.state, t), budget)^;
        {state: code.state, value: if (start.negative) -code.value else code.value}
    }
    else if (other(t, "'") or other(t, "\"")) {
        let read = expanded_next(start.state, budget);
        digits(read.state, read.token, if (t.value == "'") 8 else 16, start.negative, budget)
    }
    else digits(start.state, t, 10, start.negative, budget)
}

fn scan_register(st, budget) any^ {
    let value = scan_number(st, budget)^;
    if (value.state.issue != null) value
    else if (value.value < 0 or value.value >= 32768) {state: fail(value.state, "tex-bad-register"), value: 0}
    else value
}

fn expression_factor(st, budget) any^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) {state: first.state, value: 0}
    else if (other(first.token, "(")) {
        let nested = expression_sum(first.state, budget)^;
        let close = expanded_nonspace(nested.state, budget)^;
        if (nested.state.issue != null) nested
        else if (not other(close.token, ")")) {state: fail(put_back(close.state, close.token), "tex-missing-paren"), value: 0}
        else {state: close.state, value: nested.value}
    }
    else scan_number(put_back(first.state, first.token), budget)
}

fn checked_integer(st, value) any^ =>
    if (value > 2147483647 or value < -2147483647) {state: fail(st, "tex-arith-overflow"), value: 0}
    else {state: st, value: value}

fn rounded_fraction(st, numerator, denominator) any^ {
    if (denominator == 0) {state: fail(st, "tex-arith-overflow"), value: 0}
    else {
        let a = abs(numerator);
        let b = abs(denominator);
        let whole = a div b;
        let rounded = whole + if (2 * (a % b) >= b) 1 else 0;
        checked_integer(st, if ((numerator < 0) != (denominator < 0)) -rounded else rounded)
    }
}

fn expression_term_tail(st, value, budget) any^ {
    let op = expanded_nonspace(st, budget)^;
    if (op.state.issue != null) {state: op.state, value: 0}
    else if (other(op.token, "*") or other(op.token, "/")) {
        let right = expression_factor(op.state, budget)^;
        if (right.state.issue != null) right
        else if (other(op.token, "/")) {
            let result = rounded_fraction(right.state, value, right.value)^;
            if (result.state.issue != null) result else expression_term_tail(result.state, result.value, budget)
        } else {
            let next = expanded_nonspace(right.state, budget)^;
            if (other(next.token, "/")) {
                let denominator = expression_factor(next.state, budget)^;
                if (denominator.state.issue != null) denominator
                else {
                    let scaled = rounded_fraction(denominator.state, value * right.value, denominator.value)^;
                    if (scaled.state.issue != null) scaled else expression_term_tail(scaled.state, scaled.value, budget)
                }
            } else {
                let product = checked_integer(put_back(next.state, next.token), value * right.value)^;
                if (product.state.issue != null) product else expression_term_tail(product.state, product.value, budget)
            }
        }
    }
    else {state: put_back(op.state, op.token), value: value}
}

fn expression_term(st, budget) any^ {
    let first = expression_factor(st, budget)^;
    if (first.state.issue != null) first else expression_term_tail(first.state, first.value, budget)
}

fn expression_sum_tail(st, value, budget) any^ {
    let op = expanded_nonspace(st, budget)^;
    if (op.state.issue != null) {state: op.state, value: 0}
    else if (other(op.token, "+") or other(op.token, "-")) {
        let right = expression_term(op.state, budget)^;
        if (right.state.issue != null) right
        else {
            let sum = checked_integer(right.state, if (other(op.token, "+")) value + right.value else value - right.value)^;
            if (sum.state.issue != null) sum else expression_sum_tail(sum.state, sum.value, budget)
        }
    }
    else {state: put_back(op.state, op.token), value: value}
}

fn expression_sum(st, budget) any^ {
    let first = expression_term(st, budget)^;
    if (first.state.issue != null) first else expression_sum_tail(first.state, first.value, budget)
}

fn scan_expression(st, budget) any^ {
    let result = expression_sum(st, budget)^;
    if (result.state.issue != null) result
    else {
        let end = expanded_nonspace(result.state, budget)^;
        {state: if (primitive(end.state, end.token) == "relax") end.state else put_back(end.state, end.token),
            value: result.value}
    }
}

fn count_value(st, index) => if (st.counts[string(index)] == null) 0 else st.counts[string(index)]
fn set_count(counts, index, value) => {*:counts, *:map([string(index), value])}
fn toks_value(st, index) => if (st.toks[string(index)] == null) [] else st.toks[string(index)]
fn set_toks(values, index, value) => {*:values, *:map([string(index), value])}

fn optional_equals(st, budget) any^ {
    let next = expanded_nonspace(st, budget)^;
    if (other(next.token, "=")) next.state else put_back(next.state, next.token)
}

fn assign_count(st, global, budget, fixed_index) any^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let before = optional_equals(index.state, budget)^;
        let value = scan_number(before, budget)^;
        if (value.state.issue != null) value.state
        else {*:value.state, counts: set_count(value.state.counts, index.value, value.value),
            groups: if (global) [for (scope in value.state.groups)
                {*:scope, counts: set_count(scope.counts, index.value, value.value)}] else value.state.groups}
    }
}

fn keyword_tail(st, word, position, consumed, budget) any^ {
    if (position == len(word)) st
    else {
        let read = expanded_next(st, budget)^;
        if (read.state.issue != null) read.state
        else if (read.token != null and read.token.kind == "char" and contains([11, 12], read.token.cat) and
            lower(read.token.value) == slice(word, position, position + 1))
            keyword_tail(read.state, word, position + 1, [*consumed, read.token], budget)
        else {*:read.state, input: stream.push(read.state.input,
            if (read.token == null) consumed else [*consumed, read.token])}
    }
}

fn optional_by(st, budget) any^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) first.state
    else if (first.token != null and first.token.kind == "char" and contains([11, 12], first.token.cat) and lower(first.token.value) == "b")
        keyword_tail(first.state, "by", 1, [first.token], budget)
    else put_back(first.state, first.token)
}

fn arithmetic(st, global, budget, operation) any^ {
    let target = expanded_nonspace(st, budget)^;
    let cmd = primitive(target.state, target.token);
    let resolved = if (is_control(target.token)) meaning(target.state, target.token) else null;
    if (target.state.issue != null) target.state
    else if (cmd != "count" and resolved.kind != "register") unsupported(target.state, operation)
    else {
        let index = if (cmd == "count") scan_register(target.state, budget)^
            else {state: target.state, value: resolved.index};
        if (index.state.issue != null) index.state
        else {
            let before = optional_by(index.state, budget)^;
            let operand = scan_number(before, budget)^;
            let previous = count_value(operand.state, index.value);
            if (operand.state.issue != null) operand.state
            else if (operation == "divide" and operand.value == 0) fail(operand.state, "tex-arith-overflow")
            else {
                let value = if (operation == "advance") previous + operand.value
                    else if (operation == "multiply") previous * operand.value
                    else if ((previous < 0) != (operand.value < 0)) -(abs(previous) div abs(operand.value))
                    else abs(previous) div abs(operand.value);
                let checked = checked_integer(operand.state, value)^;
                if (checked.state.issue != null) checked.state
                else {*:checked.state, counts: set_count(checked.state.counts, index.value, value),
                    groups: if (global) [for (scope in checked.state.groups)
                        {*:scope, counts: set_count(scope.counts, index.value, value)}] else checked.state.groups}
            }
        }
    }
}

fn assign_countdef(st, global, budget) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let before = optional_equals(cleared, budget)^;
        let index = scan_register(before, budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "register", index: index.value}, global)
    }
}

fn assign_chardef(st, global, budget, math) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let before = optional_equals(cleared, budget)^;
        let value = scan_number(before, budget)^;
        if (value.state.issue != null) value.state
        else if (value.value < 0 or value.value > if (math) 32768 else 1114111)
            fail(value.state, if (math) "tex-bad-mathchar" else "tex-bad-number")
        else assign_meaning(value.state, key(target.token),
            {kind: if (math) "mathchardef" else "chardef", value: value.value}, global)
    }
}

fn assign_toks(st, global, budget, fixed_index) any^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let before = optional_equals(index.state, budget)^;
        let first = expanded_nonspace(before, budget)^;
        let m = if (is_control(first.token)) meaning(first.state, first.token)^ else null;
        let current = first.state;
        if (current.issue != null) current
        else if (charcat(first.token, 1)) {
            let value = stream.argument(stream.push(current.input, [first.token]), true)^;
            let read = {*:current, input: value.stream};
            if (value.error != null) fail(read, value.error)
            else save_toks(read, index.value, [for (t in value.tokens) {*:t, noexpand: false}], global)
        }
        else if (primitive(current, first.token) == "toks" or primitive(current, first.token) == "everyeof" or m.kind == "token-register") {
            let from = if (primitive(current, first.token) == "everyeof") {state: current, value: -1}
                else if (m.kind == "token-register") {state: current, value: m.index}
                else scan_register(current, budget)^;
            if (from.state.issue != null) from.state
            else save_toks(from.state, index.value, toks_value(from.state, from.value), global)
        }
        else fail(put_back(current, first.token), "tex-missing-brace")
    }
}

fn save_toks(st, index, tokens, global) any^ =>
    {*:st, input: if (index == -1) {*:st.input, everyeof: tokens} else st.input,
        toks: set_toks(st.toks, index, tokens),
        groups: if (global) [for (scope in st.groups)
            {*:scope, toks: set_toks(scope.toks, index, tokens)}] else st.groups}

fn assign_toksdef(st, global, budget) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let before = optional_equals(cleared, budget)^;
        let index = scan_register(before, budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "token-register", index: index.value}, global)
    }
}

fn expand_the(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = expanded_next({*:st, expansions: st.expansions + 1}, budget)^;
        let m = if (is_control(read.token)) meaning(read.state, read.token) else null;
        if (primitive(read.state, read.token) == "count" or m.kind == "register") {
            let index = if (m.kind == "register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: stream.push(index.state.input, text_tokens(string(count_value(index.state, index.value))))}
        }
        else if (contains(["endlinechar", "escapechar"], primitive(read.state, read.token)))
            {*:read.state, input: stream.push(read.state.input,
                text_tokens(string(read.state.parameters[primitive(read.state, read.token)])))}
        else if (primitive(read.state, read.token) == "eTeXversion")
            {*:read.state, input: stream.push(read.state.input, text_tokens("2"))}
        else if (primitive(read.state, read.token) == "numexpr") {
            let value = scan_expression(read.state, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: stream.push(value.state.input, text_tokens(string(value.value)))}
        }
        else if (contains(["currentgrouplevel", "currentiflevel"], primitive(read.state, read.token))) {
            let value = if (primitive(read.state, read.token) == "currentgrouplevel") len(read.state.groups)
                else len(read.state.conditions);
            {*:read.state, input: stream.push(read.state.input, text_tokens(string(value)))}
        }
        else if (contains(["catcode", "lccode", "uccode"], primitive(read.state, read.token))) {
            let code = code_quantity(read.state, primitive(read.state, read.token), budget)^;
            if (code.state.issue != null) code.state
            else {*:code.state, input: stream.push(code.state.input, text_tokens(string(code.value)))}
        }
        else if (primitive(read.state, read.token) == "toks" or primitive(read.state, read.token) == "everyeof" or m.kind == "token-register") {
            let index = if (primitive(read.state, read.token) == "everyeof") {state: read.state, value: -1}
                else if (m.kind == "token-register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {
                let tokens = toks_value(index.state, index.value);
                let stored = if (index.state.token_text_scan) [for (t in tokens)
                    {*:t, noexpand: is_control(t), direct_toks: true}] else tokens;
                {*:index.state, input: stream.push(index.state.input, stored)}
            }
        }
        else unsupported(read.state, if (primitive(read.state, read.token) == null) "the" else primitive(read.state, read.token))
    }
}

fn text_tokens(text) any^ => [for (c in ((split(text, "")) ^ { [] }))
    {kind: "char", value: c, cat: if (c == " ") 10 else 12, start: -1, end: -1}]
fn escape(st) any^ => if (st.parameters.escapechar >= 0 and st.parameters.escapechar <= 1114111)
    chr(st.parameters.escapechar) else ""

fn repeated(text, n) any^ => if (n <= 0) "" else if (n == 1) text
    else { let half = repeated(text, n div 2)^; half ++ half ++ if (n % 2 == 1) text else "" }
fn roman(n, index = 0) any^ {
    let values = [1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1];
    let symbols = ["m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"];
    if (n <= 0 or index >= len(values)) ""
    else repeated(symbols[index], n div values[index]) ++ roman(n % values[index], index + 1)
}

fn string_primitive(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        if (name == "number" or name == "romannumeral") {
            let n = scan_number(current, budget)^;
            if (n.state.issue != null) n.state
            else {*:n.state, input: stream.push(n.state.input,
                text_tokens(if (name == "number") string(n.value) else roman(n.value)))}
        } else {
            let read = stream.next(current.input);
            let t = read.token;
            if (t == null) {*:current, input: read.stream}
            else {
                let text = if (name == "meaning") print_meaning(current, t)
                    else if (t.kind == "control")
                    (if (t.value == "") escape(current) ++ "csname" ++ escape(current) ++ "endcsname"
                        else escape(current) ++ t.value) else t.value;
                {*:current, input: stream.push(read.stream, text_tokens(text))}
            }
        }
    }
}

fn token_primitive(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let opening = expanded_nonspace(current, budget)^;
        if (opening.state.issue != null) opening.state
        else if (not charcat(opening.token, 1)) fail(put_back(opening.state, opening.token), "tex-missing-brace")
        else {
            let text = stream.argument(stream.push(opening.state.input, [opening.token]), true)^;
            if (text.error != null) fail({*:opening.state, input: text.stream}, text.error)
            else if (name == "scantokens") {
                let scanned = token_text(opening.state, text.tokens)^;
                if (len(text.stream.levels) >= 6000) fail({*:opening.state, input: text.stream}, "tex-capacity")
                else {
                    let child = {*:reader.reader(scanned, -1), catcodes: text.stream.reader.catcodes,
                        endline: text.stream.reader.endline};
                    {*:opening.state, input: stream.push_reader(text.stream, child)}
                }
            } else {
                let tokens = if (name == "detokenize") text_tokens(token_text(opening.state, text.tokens))
                    else if (opening.state.token_text_scan) [for (t in text.tokens)
                        {*:t, noexpand: is_control(t), direct_toks: true}] else text.tokens;
                {*:opening.state, input: stream.push(text.stream, tokens)}
            }
        }
    }
}

fn expanded_primitive(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let text = expanded_text({*:st, expansions: st.expansions + 1}, budget)^;
        if (text.state.issue != null) text.state
        else {*:text.state, input: stream.push(text.state.input, text.tokens)}
    }
}

fn expanded_text(st, budget) any^ {
    let opening = expanded_nonspace(st, budget)^;
    if (opening.state.issue != null) {state: opening.state, tokens: []}
    else if (not charcat(opening.token, 1))
        {state: fail(put_back(opening.state, opening.token), "tex-missing-brace"), tokens: []}
    else definition_tokens(opening.state, budget)
}

fn constant_primitive(st, budget, value) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {*:st, input: stream.push(st.input, text_tokens(value)), expansions: st.expansions + 1}
}

fn strcmp_primitive(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let a = expanded_text({*:st, expansions: st.expansions + 1}, budget)^;
        if (a.state.issue != null) a.state
        else {
            let b = expanded_text(a.state, budget)^;
            if (b.state.issue != null) b.state
            else {
                let x = token_text(b.state, a.tokens)^;
                let y = token_text(b.state, b.tokens)^;
                {*:b.state, input: stream.push(b.state.input,
                    text_tokens(if (x < y) "-1" else if (x > y) "1" else "0"))}
            }
        }
    }
}

fn write_token(st, t) any^ {
    if (t.kind == "control") {
        let suffix = if (len(t.value) == 0) escape(st) ++ "csname" ++ escape(st) ++ "endcsname" else escape(st) ++ t.value;
        let cats = if (st.input.reader == null) {} else st.input.reader.catcodes;
        let result = suffix ++ if (len(t.value) == 0 or len(t.value) > 1 or
            reader.catcode(t.value, cats) == 11) " " else "";
        result
    }
    else if (t.kind == "char" and t.cat == 6) t.value ++ t.value
    else if (t.kind == "parameter") "#" ++ string(t.value + 1)
    else t.value
}

fn token_text(st, tokens) any^ => join([for (t in tokens) write_token(st, t)], "")

fn hexadecimal(n) any^ => if (n < 16) slice("0123456789ABCDEF", n, n + 1)
    else hexadecimal(n div 16) ++ slice("0123456789ABCDEF", n % 16, n % 16 + 1)

fn print_meaning(st, t) any^ {
    let m = meaning(st, t)^;
    if (m.kind == "undefined") "undefined"
    else if (m.kind == "primitive") escape(st) ++ m.name
    else if (m.kind == "register") escape(st) ++ "count" ++ string(m.index)
    else if (m.kind == "token-register") escape(st) ++ "toks" ++ string(m.index)
    else if (m.kind == "chardef" or m.kind == "mathchardef")
        escape(st) ++ (if (m.kind == "chardef") "char\"" else "mathchar\"") ++ hexadecimal(m.value)
    else if (m.kind == "char") {
        let prefixes = ["", "begin-group character ", "end-group character ", "math shift character ",
            "alignment tab character ", "", "macro parameter character ", "superscript character ",
            "subscript character ", "", "blank space ", "the letter ", "the character ", "", "", ""];
        prefixes[m.cat] ++ m.value
    }
    else if (m.kind == "macro") {
        let params = join([for (i, d in m.delimiters) "#" ++ string(i + 1) ++ token_text(st, d)], "");
        (if (m.protected_flag) escape(st) ++ "protected" else "") ++
            (if (m.long_ok) escape(st) ++ "long" else "") ++
            (if (m.protected_flag or m.long_ok) " " else "") ++ "macro:" ++ token_text(st, m.prefix) ++ params ++
            (if (m.brace_delim) "{" else "") ++ "->" ++ token_text(st, m.body)
    }
    else "undefined"
}

fn write_message(st, budget) any^ {
    let destination = scan_number(st, budget)^;
    if (destination.state.issue != null) destination.state
    else {
        let text = expanded_text(destination.state, budget)^;
        if (text.state.issue != null) text.state
        else {*:text.state, messages: [*text.state.messages, token_text(text.state, text.tokens)]}
    }
}

fn show_tokens(st, budget) any^ {
    let opening = expanded_nonspace(st, budget)^;
    if (opening.state.issue != null) opening.state
    else if (not charcat(opening.token, 1)) fail(put_back(opening.state, opening.token), "tex-missing-brace")
    else {
        let body = stream.argument(stream.push(opening.state.input, [opening.token]), true)^;
        let current = {*:opening.state, input: body.stream};
        if (body.error != null) fail(current, body.error)
        else {*:current, messages: [*current.messages, "> " ++ token_text(current, body.tokens) ++ "."]}
    }
}

fn immediate(st, budget) any^ {
    let read = expanded_next(st, budget);
    if (read.state.issue != null) read.state
    else if (primitive(read.state, read.token) == "write") write_message(read.state, budget)
    else put_back(read.state, read.token)
}

fn set_codes(input, codes) any^ => stream.configure(input, codes, input.reader.endline)
fn set_code(codes, code, value) => {*:codes, *:map([chr(code), value])}

fn assign_parameter(st, global, budget, name) any^ {
    let before = optional_equals(st, budget)^;
    let value = scan_number(before, budget)^;
    if (value.state.issue != null) value.state
    else {*:value.state,
        input: if (name == "endlinechar") stream.configure(value.state.input,
            value.state.input.reader.catcodes, value.value) else value.state.input,
        parameters: {*:value.state.parameters, *:map([name, value.value])},
        groups: if (global) [for (scope in value.state.groups)
            {*:scope, parameters: {*:scope.parameters, *:map([name, value.value])}}] else value.state.groups}
}

fn assign_catcode(st, global, budget) any^ {
    let character = scan_number(st, budget)^;
    if (character.state.issue != null) character.state
    else {
        let equal = expanded_nonspace(character.state, budget)^;
        let value = scan_number(if (other(equal.token, "=")) equal.state else put_back(equal.state, equal.token), budget)^;
        if (value.state.issue != null) value.state
        else if (character.value < 0 or character.value > 1114111 or value.value < 0 or value.value > 15)
            fail(value.state, "tex-bad-character-code")
        else {
            let current_codes = value.state.input.reader.catcodes;
            {*:value.state,
                input: set_codes(value.state.input, set_code(current_codes, character.value, value.value)),
                groups: if (global) [for (scope in value.state.groups)
                    {*:scope, catcodes: set_code(scope.catcodes, character.value, value.value)}]
                    else value.state.groups}
        }
    }
}

fn assign_case_code(st, global, budget, upper_case) any^ {
    let character = scan_number(st, budget)^;
    if (character.state.issue != null) character.state
    else {
        let before = optional_equals(character.state, budget)^;
        let value = scan_number(before, budget)^;
        if (value.state.issue != null) value.state
        else if (character.value < 0 or character.value > 1114111 or value.value < 0 or value.value > 1114111)
            fail(value.state, "tex-bad-character-code")
        else if (upper_case) {*:value.state,
            uppercase: set_code(value.state.uppercase, character.value, value.value),
            groups: if (global) [for (scope in value.state.groups)
                {*:scope, uppercase: set_code(scope.uppercase, character.value, value.value)}] else value.state.groups}
        else {*:value.state,
            lowercase: set_code(value.state.lowercase, character.value, value.value),
            groups: if (global) [for (scope in value.state.groups)
                {*:scope, lowercase: set_code(scope.lowercase, character.value, value.value)}] else value.state.groups}
    }
}

fn case_code(st, t, upper_case) any^ {
    let codes = if (upper_case) st.uppercase else st.lowercase;
    let assigned = codes[t.value];
    if (assigned != null) assigned
    else if ((t.value >= "a" and t.value <= "z") or (t.value >= "A" and t.value <= "Z"))
        ord(if (upper_case) upper(t.value) else lower(t.value))
    else 0
}

fn code_quantity(st, name, budget) any^ {
    let code = scan_number(st, budget)^;
    if (code.state.issue != null) code
    else if (code.value < 0 or code.value > 1114111) {state: fail(code.state, "tex-bad-character-code"), value: 0}
    else {
        let c = chr(code.value);
        {state: code.state, value: if (name == "catcode") reader.catcode(c, code.state.input.reader.catcodes)
            else case_code(code.state, {value: c}, name == "uccode")}
    }
}

fn shift_case(st, budget, upper_case) any^ {
    let opening = expanded_nonspace(st, budget)^;
    if (not charcat(opening.token, 1)) fail(put_back(opening.state, opening.token), "tex-missing-brace")
    else {
        let text = stream.argument(stream.push(opening.state.input, [opening.token]), true)^;
        if (text.error != null) fail({*:opening.state, input: text.stream}, text.error)
        else {
            let shifted = [for (t in text.tokens)
                if (t.kind == "char" and case_code(opening.state, t, upper_case) != 0)
                    {*:t, value: chr(case_code(opening.state, t, upper_case))} else t];
            {*:opening.state, input: stream.push(text.stream, shifted)}
        }
    }
}

fn skip_conditional(st, depth, allow_else) any^ {
    let read = stream.next(st.input);
    let t = read.token;
    let current = {*:st, input: read.stream};
    if (t == null) {state: fail(current, "tex-incomplete-if"), delimiter: null}
    else if (primitive(st, t) != null and conditional_name(primitive(st, t)))
        skip_conditional(current, depth + 1, allow_else)
    else if (primitive(st, t) == "fi") {
        if (depth == 0) {state: current, delimiter: "fi"}
        else skip_conditional(current, depth - 1, allow_else)
    }
    else if (depth == 0 and allow_else and primitive(st, t) == "else") {state: current, delimiter: "else"}
    else skip_conditional(current, depth, allow_else)
}

fn skip_case(st, remaining, depth = 0) any^ {
    let read = stream.next(st.input);
    let t = read.token;
    let current = {*:st, input: read.stream};
    let name = primitive(st, t);
    if (t == null) fail(current, "tex-incomplete-if")
    else if (name != null and conditional_name(name)) skip_case(current, remaining, depth + 1)
    else if (name == "fi") {
        if (depth == 0) current else skip_case(current, remaining, depth - 1)
    }
    else if (depth == 0 and name == "else") {*:current, conditions: [*current.conditions, false]}
    else if (depth == 0 and name == "or" and remaining > 0) {
        if (remaining == 1) {*:current, conditions: [*current.conditions, "case"]}
        else skip_case(current, remaining - 1, depth)
    }
    else skip_case(current, remaining, depth)
}

fn case_conditional(st, budget) any^ {
    let selected = scan_number(st, budget)^;
    if (selected.state.issue != null) selected.state
    else if (selected.value == 0) {*:selected.state, conditions: [*selected.state.conditions, "case"]}
    else skip_case(selected.state, selected.value)
}

fn begin_conditional(st, truth, negate = false) any^ {
    if (truth != negate) {*:st, conditions: [*st.conditions, true]}
    else {
        let skipped = skip_conditional(st, 0, true)^;
        if (skipped.state.issue != null) skipped.state
        else if (skipped.delimiter == "else") {*:skipped.state, conditions: [*st.conditions, false]}
        else skipped.state
    }
}

fn numeric_conditional(st, name, budget, negate) any^ {
    let left = scan_number(st, budget)^;
    if (left.state.issue != null) left.state
    else if (name == "ifodd") begin_conditional(left.state, (left.value % 2) != 0, negate)
    else {
        let relation = expanded_nonspace(left.state, budget)^;
        if (not (other(relation.token, "<") or other(relation.token, "=") or other(relation.token, ">")))
            fail(put_back(relation.state, relation.token), "tex-missing-relation")
        else {
            let right = scan_number(relation.state, budget)^;
            if (right.state.issue != null) right.state
            else begin_conditional(right.state, if (relation.token.value == "<") left.value < right.value
                else if (relation.token.value == ">") left.value > right.value else left.value == right.value, negate)
        }
    }
}

fn tokens_equal(a, b, index = 0) any^ {
    if (len(a) != len(b)) false
    else if (index == len(a)) true
    else stream.same_token(a[index], b[index]) and tokens_equal(a, b, index + 1)
}

fn delimiters_equal(a, b, index = 0) any^ {
    if (len(a) != len(b)) false
    else if (index == len(a)) true
    else tokens_equal(a[index], b[index]) and delimiters_equal(a, b, index + 1)
}

fn meanings_equal(a, b) any^ {
    if (a.kind != b.kind) false
    else if (a.kind == "undefined" or a.kind == "relax") true
    else if (a.kind == "primitive") a.name == b.name
    else if (a.kind == "char") a.value == b.value and a.cat == b.cat
    else if (a.kind == "register") a.index == b.index
    else if (a.kind == "token-register") a.index == b.index
    else if (a.kind == "chardef" or a.kind == "mathchardef") a.value == b.value
    else if (a.kind == "macro") a.count == b.count and a.long_ok == b.long_ok and a.protected_flag == b.protected_flag and
        a.brace_delim == b.brace_delim and tokens_equal(a.prefix, b.prefix) and
        delimiters_equal(a.delimiters, b.delimiters) and tokens_equal(a.body, b.body)
    else false
}

fn character_key(st, t) any^ {
    if (t == null) {value: null, cat: 16}
    else if (t.noexpand == true and t.kind == "active") {value: t.value, cat: 13}
    else if (t.kind == "char") {value: t.value, cat: t.cat}
    else {
        let m = meaning(st, t);
        if (m.kind == "char") {value: m.value, cat: m.cat} else {value: null, cat: 16}
    }
}

fn unless_conditional(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input);
        let current = {*:st, input: read.stream, expansions: st.expansions + 1};
        let name = primitive(st, read.token);
        if (name == null or not conditional_name(name) or name == "ifcase")
            fail(put_back(current, read.token), "tex-bad-unless")
        else condition(current, name, budget, true)
    }
}

fn condition(st, name, budget, negate = false) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let depth = len(st.conditions);
        if (name == "iftrue" or name == "iffalse") begin_conditional(current, name == "iftrue", negate)
        else if (name == "ifnum" or name == "ifodd") numeric_conditional(current, name, budget, negate)
        else if (name == "ifcase") case_conditional(current, budget)
        else if (name == "if" or name == "ifcat") {
            let a = expanded_next(current, budget)^;
            let b = expanded_next(a.state, budget)^;
            if (b.state.issue != null) b.state
            else {
                let first = character_key(b.state, a.token);
                let second = character_key(b.state, b.token);
                begin_conditional(b.state, if (name == "if") first.value == second.value else first.cat == second.cat, negate)
            }
        }
        else if (name == "ifcsname") {
            let scanned = csname_tokens(current, budget)^;
            if (scanned.state.issue != null) scanned.state
            else {
                let t = {kind: "control", value: scanned.name};
                begin_conditional(scanned.state, meaning(scanned.state, t).kind != "undefined", negate)
            }
        }
        else if (name == "ifx") {
            let a = stream.next(current.input);
            let b = stream.next(a.stream);
            begin_conditional({*:current, input: b.stream}, a.token != null and b.token != null and
                meanings_equal(meaning(current, a.token), meaning(current, b.token)), negate)
        }
        else if (name == "ifdefined") {
            let read = stream.next(current.input);
            let t = read.token;
            let present = if (t == null) false else if (not is_control(t)) true
                else meaning(current, t).kind != "undefined";
            begin_conditional({*:current, input: read.stream}, present, negate)
        }
        else if (name == "fi") {
            if (depth == 0) fail(current, "tex-extra-fi")
            else {*:current, conditions: slice(st.conditions, 0, depth - 1)}
        }
        else if (name == "else") {
            if (depth == 0 or (st.conditions[depth - 1] != true and st.conditions[depth - 1] != "case"))
                fail(current, "tex-extra-fi")
            else {
                let skipped = skip_conditional(current, 0, false)^;
                if (skipped.state.issue != null) skipped.state
                else {*:skipped.state, conditions: slice(st.conditions, 0, depth - 1)}
            }
        }
        else if (name == "or") {
            if (depth == 0 or st.conditions[depth - 1] != "case") fail(current, "tex-extra-fi")
            else {
                let skipped = skip_conditional(current, 0, false)^;
                if (skipped.state.issue != null) skipped.state
                else {*:skipped.state, conditions: slice(st.conditions, 0, depth - 1)}
            }
        }
        else unsupported(current, name)
    }
}

fn after_assignment(before, after) any^ {
    if (after.issue != null or before.after_assignment == null) after
    else {*:after, input: stream.push(after.input, [before.after_assignment]), after_assignment: null}
}

fn save_after_assignment(st) any^ {
    let read = stream.next(st.input);
    {*:st, input: read.stream, after_assignment: read.token}
}

fn begin_group(st, token, explicit) any^ =>
    {*:st, groups: [*st.groups, {definitions: st.definitions, counts: st.counts, toks: st.toks,
        lowercase: st.lowercase, uppercase: st.uppercase, parameters: st.parameters,
        catcodes: st.input.reader.catcodes, explicit: explicit, aftergroup: []}],
        output: if (token == null) st.output else [*st.output, token]}

fn save_aftergroup(st) any^ {
    let read = stream.next(st.input);
    let depth = len(st.groups);
    let current = {*:st, input: read.stream};
    if (read.token == null) fail(current, "tex-runaway-aftergroup")
    else if (depth == 0) current
    else {*:current, groups: [for (index, scope in st.groups)
        if (index == depth - 1) {*:scope, aftergroup: [*scope.aftergroup, read.token]} else scope]}
}

fn end_group(st, token, explicit) any^ {
    let depth = len(st.groups);
    if (depth == 0 or st.groups[depth - 1].explicit != explicit)
        fail(st, if (explicit) "tex-extra-endgroup" else "tex-extra-brace")
    else {
        let scope = st.groups[depth - 1];
        let restored = {*:stream.configure(st.input, scope.catcodes, scope.parameters.endlinechar),
            everyeof: if (scope.toks["-1"] == null) [] else scope.toks["-1"]};
        {*:st, definitions: scope.definitions, counts: scope.counts, toks: scope.toks,
            lowercase: scope.lowercase, uppercase: scope.uppercase, parameters: scope.parameters,
            input: stream.push(restored, scope.aftergroup), groups: slice(st.groups, 0, depth - 1),
            output: if (token == null) st.output else [*st.output, token]}
    }
}

fn step(st, budget) any^ {
    let read = stream.next(st.input);
    let t = read.token;
    let current = {*:st, input: read.stream};
    let cmd = primitive(st, t);
    let resolved = if (is_control(t)) meaning(st, t) else null;
    if (t == null) if (len(st.conditions) > 0) fail(current, "tex-incomplete-if") else {*:current, finished: true}
    else if (t.noexpand == true) {*:current, output: [*st.output, {*:t, noexpand: false}]}
    else if (expandable(current, t)) expand_one(current, t, budget)
    else if (contains(["def", "gdef", "edef", "xdef"], cmd))
        after_assignment(current, define(current, cmd == "gdef" or cmd == "xdef", false, false, cmd == "edef" or cmd == "xdef", budget))
    else if (cmd == "let") after_assignment(current, assign_let(current, false))
    else if (cmd == "futurelet") after_assignment(current, assign_futurelet(current, false))
    else if (cmd == "catcode") after_assignment(current, assign_catcode(current, false, budget))
    else if (cmd == "lccode" or cmd == "uccode")
        after_assignment(current, assign_case_code(current, false, budget, cmd == "uccode"))
    else if (cmd == "endlinechar" or cmd == "escapechar")
        after_assignment(current, assign_parameter(current, false, budget, cmd))
    else if (cmd == "uppercase" or cmd == "lowercase") shift_case(current, budget, cmd == "uppercase")
    else if (cmd == "count") after_assignment(current, assign_count(current, false, budget, null))
    else if (cmd == "countdef") after_assignment(current, assign_countdef(current, false, budget))
    else if (contains(["advance", "multiply", "divide"], cmd))
        after_assignment(current, arithmetic(current, false, budget, cmd))
    else if (cmd == "chardef" or cmd == "mathchardef")
        after_assignment(current, assign_chardef(current, false, budget, cmd == "mathchardef"))
    else if (cmd == "toks") after_assignment(current, assign_toks(current, false, budget, null))
    else if (cmd == "everyeof") after_assignment(current, assign_toks(current, false, budget, -1))
    else if (cmd == "toksdef") after_assignment(current, assign_toksdef(current, false, budget))
    else if (resolved.kind == "register") after_assignment(current, assign_count(current, false, budget, resolved.index))
    else if (resolved.kind == "token-register") after_assignment(current, assign_toks(current, false, budget, resolved.index))
    else if (cmd == "immediate") immediate(current, budget)
    else if (cmd == "write") write_message(current, budget)
    else if (cmd == "showtokens") show_tokens(current, budget)
    else if (cmd == "end") if (len(st.conditions) > 0) fail(current, "tex-incomplete-if") else {*:current, finished: true}
    else if (cmd == "afterassignment") save_after_assignment(current)
    else if (cmd == "aftergroup") save_aftergroup(current)
    else if (cmd == "begingroup") begin_group(current, null, true)
    else if (cmd == "endgroup") end_group(current, null, true)
    else if (cmd == "global" or cmd == "long" or cmd == "protected")
        after_assignment(current, prefixed(current, cmd == "global", cmd == "long", cmd == "protected", budget))
    else if (resolved.kind == "relax" or cmd == "relax") current
    else if (resolved.kind == "char") {*:current, input: stream.push(current.input,
        [{kind: "char", value: resolved.value, cat: resolved.cat, start: t.start, end: t.end}])}
    else if (resolved.kind == "chardef") {*:current, output: [*st.output,
        {kind: "char", value: chr(resolved.value), cat: 12, start: t.start, end: t.end}]}
    else if (cmd == "par") {*:current, output: [*st.output, t]}
    else if (cmd != null) unsupported(current, cmd)
    else if (charcat(t, 1)) begin_group(current, t, false)
    else if (charcat(t, 2)) end_group(current, t, false)
    else {*:current, output: [*st.output, t]}
}

fn run(st, budget) any^ {
    if (st.issue != null or st.finished == true) st
    else run(step(st, budget), budget)
}

pub fn expand(input, budget = 20000000) any^ =>
    run(result(input, {}, [], [], 0), budget)
