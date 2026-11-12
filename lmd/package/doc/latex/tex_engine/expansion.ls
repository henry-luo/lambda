// Experimental TeX macro expansion. Unsupported primitives remain outside this API.
import stream: .stream
import reader: .reader
import primitives: .primitives
import expl3: .expl3
import fp: .fp
import dimension: .dimension
import glue: .glue
import pdfstrings: .pdfstrings
import md5: .md5
import utf8: lambda.io.utf8
import path: lambda.io.path

fn result(input, definitions, groups, output, count, issue = null, options = null) =>
    {input: input, definitions: definitions, groups: groups, output: output,
        latex: options.latex == true, script_expl3: options.script_expl3 == true, expl_syntax: null, adapters: if (options.adapters == null) [] else options.adapters,
        chaptered: if (options.chaptered == null) [] else options.chaptered, packages: [], class_options: "",
        in_document: false, base_path: if (options.base_path is string) options.base_path else null,
        begin_hooks: [], end_hooks: [], package_frames: [],
        counter_resets: [], engine_counters: [], output_streams: [], input_streams: {}, font_families: {}, random_seed: 12345,
        current_package: null, current_options: null, current_is_class: false, option_decls: [], pass_options: [],
        math_depth: 0, display_math: false, main_file: input.reader.file_id,
        echo_definitions: options.echo_definitions == true, echo_incomplete: false,
        conditions: [], counts: {}, dimens: {}, skips: {}, muskips: {}, boxes: {}, toks: {}, lowercase: {}, uppercase: {}, code_tables: {sfcode: {}, mathcode: {}, delcode: {}},
        parameters: {endlinechar: input.reader.endline, escapechar: 92, mag: 1000, tolerance: 10000,
            newlinechar: if (options.latex == true) -1 else 0}, after_assignment: null, token_text_scan: false,
        resources: options.resources, resources_seen: {}, resource_ids: {}, resource_count: 0, resource_bytes: 0,
        sources: [{id: input.reader.file_id, name: if (options.source_name == null) "<input>" else options.source_name,
            origin: if (input.reader.origin == "bundled") "bundled" else "local", data: input.reader.source}],
        fonts: [{name: "nullfont", size: 0, param_base: 0, param_count: 7, params: {}, hyphen_char: 45, skew_char: -1,
            ident: {kind: "control", value: "nullfont", cat: null, start: -1, end: -1}}],
        font_memory: 7, current_font: 0, diagnostics: [], diagnostic_records: [],
        messages: [], terminal_line: "", expansions: count, issue: issue}
fn fail(st, code) => {*:st, issue: code}
fn unsupported(st, name) => {*:st, issue: "tex-unsupported-primitive", unsupported: name}
fn charcat(t, cat) => t != null and t.kind == "char" and t.cat == cat
fn is_control(t) => t != null and (t.kind == "control" or t.kind == "active")
fn key(t) => if (t.kind == "active") "active:" ++ t.value else "control:" ++ t.value
fn conditional_name(name) => contains(["if", "ifcat", "ifnum", "ifdim", "ifodd", "ifvmode", "ifhmode", "ifmmode",
    "ifinner", "ifvoid", "ifhbox", "ifvbox", "ifx", "ifeof", "iftrue", "iffalse", "ifcase", "ifdefined",
    "ifcsname", "iffontchar", "ifpdfprimitive", "ifincsname"], name)
fn builtin(st, name) => primitives.exists(name) or (st.latex and primitives.latex_exists(name)) or
    (st.script_expl3 and expl3.exists(name))
fn meaning(st, t) any^ => if (not is_control(t)) {kind: "char", value: t.value, cat: t.cat}
    else if (st.definitions[key(t)] != null) st.definitions[key(t)]
    else if (t.kind == "control" and builtin(st, t.value)) {kind: "primitive", name: t.value}
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
        definition_tokens_inner(read.state, budget, depth, [*output, {*:t, noexpand: false, direct_toks: false, trace_internal: false}])
    else if (charcat(t, 2) and depth == 1) {state: read.state, tokens: output}
    // stored tokens must mark foreign expansion during a later definition.
    else definition_tokens_inner(read.state, budget,
        depth + if (charcat(t, 1)) 1 else if (charcat(t, 2)) -1 else 0,
        [*output, {*:t, noexpand: false, trace_internal: false}])
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
    if (len(args) >= len(delimiters)) {input: input, args: args, issue: null}
    else {
        let arg = stream.delimited(input, delimiters[len(args)], long_ok,
            brace_delim and len(args) + 1 == len(delimiters));
        if (arg.error != null) {input: arg.stream, args: args, issue: arg.error}
        else collect_args(arg.stream, delimiters, brace_delim, long_ok, [*args, arg.tokens])
    }
}

fn stored_tokens(tokens) => [for (t in tokens) {*:t, direct: false}]

// spread after selecting the token array so an empty argument contributes nothing.
fn substitute(body, args) => [for (t in body)
    *(if (t.kind == "parameter") args[t.value] else [t])]

fn expand_macro(st, macro, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let matched = match_prefix(st.input, macro.prefix);
        if (matched.issue != null) fail({*:st, input: matched.input}, matched.issue)
        else {
            let optional = if (macro.optional == true) latex_optional(matched.input)^
                else {input: matched.input, present: false, tokens: [], issue: null};
            let initial = if (macro.optional == true) [if (optional.present) optional.tokens else macro.optional_default] else [];
            let args = collect_args(optional.input, macro.delimiters, macro.brace_delim, macro.long_ok, initial);
            if (optional.issue != null) fail({*:st, input: optional.input}, optional.issue)
            else if (args.issue != null) fail({*:st, input: args.input}, args.issue)
            else {*:st, input: stream.push(args.input, stored_tokens(substitute(macro.body, args.args))),
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
    let apply_global = effective_global(st, global)^;
    if (cmd == null and (not is_control(t) or
        (meaning(st, t).kind != "register" and meaning(st, t).kind != "dimension-register" and meaning(st, t).kind != "glue-register" and meaning(st, t).kind != "font" and meaning(st, t).kind != "token-register")))
        fail(advanced, "tex-bad-prefix")
    else if (cmd == "global") prefixed(advanced, true, long_ok, protected_flag, budget)
    else if (cmd == "long") prefixed(advanced, global, true, protected_flag, budget)
    else if (cmd == "protected") prefixed(advanced, global, long_ok, true, budget)
    else if (contains(["def", "gdef", "edef", "xdef"], cmd)) {
        let assigned = define(advanced, effective_global(st, global or cmd == "gdef" or cmd == "xdef")^, long_ok, protected_flag,
            cmd == "edef" or cmd == "xdef", budget)^;
        {*:assigned, prefixed_definition: true}
    }
    else if (cmd == "let" and not long_ok and not protected_flag) assign_let(advanced, apply_global)
    else if (cmd == "futurelet" and not long_ok and not protected_flag) assign_futurelet(advanced, apply_global)
    else if (cmd == "font" and not long_ok and not protected_flag) define_font(advanced, apply_global, budget)
    else if (cmd == "letterspacefont" and not long_ok and not protected_flag) assign_letterspace_font(advanced, apply_global, budget)
    else if (contains(["read", "readline"], cmd) and not long_ok and not protected_flag) read_stream_assignment(advanced, apply_global, budget, cmd == "readline")
    else if ((cmd == "interactionmode" or page_dimension(cmd)) and not long_ok and not protected_flag)
        assign_page_state(advanced, budget, cmd)
    else if (cmd == "fontdimen" and not long_ok and not protected_flag) assign_font_dimen(advanced, budget)
    else if (contains(["hyphenchar", "skewchar"], cmd) and not long_ok and not protected_flag)
        assign_font_character(advanced, budget, cmd)
    else if (contains(["textfont", "scriptfont", "scriptscriptfont"], cmd) and not long_ok and not protected_flag)
        assign_font_family(advanced, budget, cmd)
    else if (contains(["patterns", "hyphenation", "parshape", "interlinepenalties", "clubpenalties", "widowpenalties", "displaywidowpenalties"], cmd) and not long_ok and not protected_flag)
        assign_paragraph_data(advanced, budget, cmd)
    else if (cmd == "nullfont" and not long_ok and not protected_flag) select_font(advanced, apply_global, 0)
    else if (cmd == "count" and not long_ok and not protected_flag) assign_count(advanced, apply_global, budget, null)
    else if (cmd == "countdef" and not long_ok and not protected_flag) assign_countdef(advanced, apply_global, budget)
    else if (cmd == "dimen" and not long_ok and not protected_flag) assign_dimen(advanced, apply_global, budget, null)
    else if (cmd == "dimendef" and not long_ok and not protected_flag) assign_dimendef(advanced, apply_global, budget)
    else if ((cmd == "skip" or cmd == "muskip") and not long_ok and not protected_flag)
        assign_glue(advanced, apply_global, budget, null, cmd == "muskip")
    else if ((cmd == "skipdef" or cmd == "muskipdef") and not long_ok and not protected_flag)
        assign_gluedef(advanced, apply_global, budget, cmd == "muskipdef")
    else if (contains(["advance", "multiply", "divide"], cmd) and not long_ok and not protected_flag)
        arithmetic(advanced, apply_global, budget, cmd)
    else if ((cmd == "chardef" or cmd == "mathchardef") and not long_ok and not protected_flag)
        assign_chardef(advanced, apply_global, budget, cmd == "mathchardef")
    else if (cmd == "toks" and not long_ok and not protected_flag) assign_toks(advanced, apply_global, budget, null)
    else if (cmd == "everyeof" and not long_ok and not protected_flag) assign_toks(advanced, apply_global, budget, -1)
    else if (cmd == "toksdef" and not long_ok and not protected_flag) assign_toksdef(advanced, apply_global, budget)
    else if (meaning(st, t).kind == "font" and not long_ok and not protected_flag)
        select_font(advanced, apply_global, meaning(st, t).index)
    else if (meaning(st, t).kind == "register" and not long_ok and not protected_flag)
        assign_count(advanced, apply_global, budget, meaning(st, t).index)
    else if (meaning(st, t).kind == "dimension-register" and not long_ok and not protected_flag)
        assign_dimen(advanced, apply_global, budget, meaning(st, t).index)
    else if (meaning(st, t).kind == "glue-register" and not long_ok and not protected_flag)
        assign_glue(advanced, apply_global, budget, meaning(st, t).index, meaning(st, t).mu)
    else if (meaning(st, t).kind == "token-register" and not long_ok and not protected_flag)
        assign_toks(advanced, apply_global, budget, meaning(st, t).index)
    else if (cmd == "catcode" and not long_ok and not protected_flag) assign_catcode(advanced, apply_global, budget)
    else if (contains(["sfcode", "mathcode", "delcode"], cmd) and not long_ok and not protected_flag)
        assign_extra_code(advanced, apply_global, budget, cmd)
    else if ((cmd == "lccode" or cmd == "uccode") and not long_ok and not protected_flag)
        assign_case_code(advanced, apply_global, budget, cmd == "uccode")
    else if (primitives.parameter_kind(cmd) == "int" and not long_ok and not protected_flag)
        assign_parameter(advanced, apply_global, budget, cmd)
    else if (primitives.parameter_kind(cmd) == "dimen" and not long_ok and not protected_flag)
        assign_dimen(advanced, apply_global, budget, "param:" ++ cmd)
    else if (contains(["glue", "mu"], primitives.parameter_kind(cmd)) and not long_ok and not protected_flag)
        assign_glue(advanced, apply_global, budget, "param:" ++ cmd, primitives.parameter_kind(cmd) == "mu")
    else if (primitives.parameter_kind(cmd) == "toks" and not long_ok and not protected_flag)
        assign_toks(advanced, apply_global, budget, parameter_index(cmd))
    else fail(advanced, "tex-bad-prefix")
}

fn noexpand(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input);
        let existing = if (is_control(read.token)) meaning(st, read.token) else null;
        let cmd = primitive(st, read.token);
        let unexpandable = is_control(read.token) and (existing.kind == "relax" or existing.kind == "char" or
            (cmd != null and not primitives.expandable(cmd) and not (st.latex and primitives.latex_expandable(cmd)) and
                not (st.script_expl3 and expl3.expandable(cmd))));
        let marked = if (is_control(read.token) and not unexpandable)
            {*:read.token, noexpand: true} else read.token;
        {*:st, input: if (marked == null) read.stream else stream.push(read.stream, [marked]),
            expansions: st.expansions + 1}
    }
}

fn expandable(st, t) any^ => is_control(t) and t.noexpand != true and
    (meaning(st, t).kind == "macro" or (primitive(st, t) != null and (primitives.expandable(primitive(st, t)) or (st.latex and primitives.latex_expandable(primitive(st, t))) or
        (st.script_expl3 and expl3.expandable(primitive(st, t))))))

fn expand_one(st, t, budget) any^ {
    if (not expandable(st, t)) {*:st, input: stream.push(st.input, [t])}
    else if (meaning(st, t).kind == "macro") {
        let macro = meaning(st, t)^;
        if (st.expansions >= budget) fail(st, "tex-budget")
        else if (st.latex and len(st.packages) > 0 and unresolved_package_cycle(st, macro)^)
            unsupported(st, "recursive package bootstrap")
        else expand_macro(st, macro, budget)
    }
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
    else if (contains(["pdfescapestring", "pdfescapename", "pdfescapehex", "pdfunescapehex", "pdfmdfivesum"], primitive(st, t)))
        pdf_string_primitive(st, primitive(st, t), budget)
    else if (primitive(st, t) == "eTeXrevision") constant_primitive(st, budget, ".6")
    else if (primitive(st, t) == "pdftexrevision") constant_primitive(st, budget, "27")
    else if (primitive(st, t) == "pdfuniformdeviate") random_deviate(st, budget)
    else if (primitive(st, t) == "pdfnormaldeviate")
        constant_primitive({*:st, pdf_random_seen: true}, budget, "0")
    else if (primitive(st, t) == "jobname") constant_primitive(st, budget, job_name(st.base_path)^)
    else if (primitive(st, t) == "fontname") font_name_primitive(st, budget)
    else if (primitive(st, t) == "input") input_primitive(st, budget)
    else if (primitive(st, t) == "endinput") endinput_primitive(st, budget)
    else if (contains(["topmark", "firstmark", "botmark", "splitfirstmark", "splitbotmark",
        "topmarks", "firstmarks", "botmarks", "splitfirstmarks", "splitbotmarks"], primitive(st, t)))
        empty_mark_query(st, primitive(st, t), budget)
    else if (primitive(st, t) == "pdfcreationdate") constant_primitive(st, budget, "D:20000101120000Z")
    else if (primitive(st, t) == "pdffilesize" or primitive(st, t) == "pdffilemoddate")
        pdf_file_primitive(st, primitive(st, t), budget)
    else if (st.latex and contains(["arabic", "roman", "Roman", "alph", "Alph", "fnsymbol", "value"], primitive(st, t)))
        latex_counter_format(st, t, primitive(st, t), budget)
    else if (st.script_expl3 and primitive(st, t) == "tl_use:N") expl3_tl_use(st, budget)
    else if (st.script_expl3 and expl3.token_empty(primitive(st, t))) expl3_tl_empty(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.token_equal(primitive(st, t))) expl3_tl_equal(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.token_string(primitive(st, t))) expl3_tl_string(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.token_count(primitive(st, t))) expl3_tl_count(st, primitive(st, t), budget)
    else if (st.script_expl3 and primitive(st, t) == "int_eval:n") expl3_int_eval(st, budget)
    else if (st.script_expl3 and expl3.int_math(primitive(st, t))) expl3_int_math(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.int_compare(primitive(st, t))) expl3_int_compare(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.int_compare_expr(primitive(st, t))) expl3_int_compare_expr(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.int_test(primitive(st, t))) expl3_int_test(st, primitive(st, t), budget)
    else if (st.script_expl3 and primitive(st, t) == "fp_eval:n") expl3_fp_eval(st, budget)
    else if (st.script_expl3 and contains(["seq_use:Nn", "seq_count:N"], primitive(st, t)))
        expl3_seq_expand(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.string_compare(primitive(st, t)))
        expl3_str_compare(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.text_case(primitive(st, t)))
        expl3_text_case(st, primitive(st, t), budget)
    else if (st.latex and primitive(st, t) == "lambda@xspace") latex_xspace(st, budget)
    else if (st.latex and (primitive(st, t) == "@ifpackagewith" or primitive(st, t) == "@ifclasswith"))
        if (st.expansions >= budget) fail(st, "tex-budget") else
            latex_if_package_with({*:st, expansions: st.expansions + 1}, primitive(st, t) == "@ifclasswith", budget)
    else if (primitive(st, t) == "unless") unless_conditional(st, budget)
    else if (conditional_name(primitive(st, t)) or contains(["fi", "else", "or"], primitive(st, t)))
        condition(st, primitive(st, t), budget)
    else unsupported(st, primitive(st, t))
}

// an already attempted package cannot replace a self-recursive bootstrap.
fn unresolved_package_cycle(st, macro) any^ {
    let body = macro.body;
    let size = len(body);
    if (macro.count != 0 or macro.optional == true or len(macro.prefix) != 0 or size < 4) false
    else if (primitive(st, body[0]) != "RequirePackage" or not charcat(body[1], 1) or
        not charcat(body[size - 2], 2) or not is_control(body[size - 1])) false
    else {
        let inside = slice(body, 2, size - 2)^;
        if (any([for (t in inside) t.kind != "char"])) false
        else {
            let name = trim(join([for (t in inside) t.value], ""));
            let attempted = any([for (entry in st.packages) entry.name == name and entry.is_class == false]);
            attempted and meanings_equal(macro, meaning(st, body[size - 1])^)^
        }
    }
}

fn xspace_exception(token) any^ {
    if (token == null) true
    else if (token.kind == "char") charcat(token, 1) or charcat(token, 2) or charcat(token, 10) or
        (len(token.value) == 1 and ord(token.value) < 128 and index_of(",.'/?;:!~-)", token.value) != null)
    else if (token.kind == "active") token.value == "~"
    else token.kind == "control" and contains(["/", " ", "space", "@sptoken", "@xobeysp", "footnote",
        "footnotemark", "bgroup", "egroup"], token.value)
}

fn latex_xspace(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input)^;
        let space = {kind: "char", value: " ", cat: 10, start: -1, end: -1};
        let pending = if (read.token == null) [] else [*(if (xspace_exception(read.token)^) [] else [space]), read.token];
        {*:st, input: stream.push(read.stream, pending), expansions: st.expansions + 1}
    }
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
                definitions: if ((scanned.state.definitions[k] == null and not builtin(scanned.state, scanned.name)) or
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

fn input_line_number(st) any^ {
    let active = stream.active_reader(st.input)^;
    if (active == null or active.line_no == null) 0 else active.line_no
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
    else if (primitive(start.state, t) == "dimen" or (is_control(t) and meaning(start.state, t).kind == "dimension-register")) {
        let index = if (primitive(start.state, t) == "dimen") scan_register(start.state, budget)^
            else {state: start.state, value: meaning(start.state, t).index};
        let value = dimen_value(index.state, index.value);
        {state: index.state, value: if (start.negative) -value else value}
    }
    else if (primitives.parameter_kind(primitive(start.state, t)) == "dimen") {
        let value = dimen_value(start.state, "param:" ++ primitive(start.state, t));
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (primitives.parameter_kind(primitive(start.state, t)) == "glue") {
        let value = glue_value(start.state, "param:" ++ primitive(start.state, t), false).width;
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (is_control(t) and (meaning(start.state, t).kind == "chardef" or
        meaning(start.state, t).kind == "mathchardef")) {
        let value = meaning(start.state, t).value;
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (primitives.parameter_kind(primitive(start.state, t)) == "int") {
        let value = parameter_value(start.state, primitive(start.state, t));
        {state: start.state, value: if (start.negative) -value else value}
    }
    else if (contains(["parshape", "interlinepenalties", "clubpenalties", "widowpenalties", "displaywidowpenalties"], primitive(start.state, t))) {
        let value = paragraph_quantity(start.state, primitive(start.state, t), budget)^;
        {state: value.state, value: 0}
    }
    else if (primitive(start.state, t) == "interactionmode" or page_dimension(primitive(start.state, t)))
        {state: {*:start.state, page_state_quantity_seen: true},
            value: if (primitive(start.state, t) == "interactionmode") if (start.negative) -1 else 1 else 0}
    else if (contains(["pdfrandomseed", "pdfelapsedtime"], primitive(start.state, t))) {
        let value = if (primitive(start.state, t) == "pdfrandomseed") start.state.random_seed % 2147483648 else 0;
        {state: {*:start.state, pdf_random_seen: true}, value: if (start.negative) -value else value}
    }
    else if (primitive(start.state, t) == "inputlineno") {
        let line = input_line_number(start.state)^;
        {state: start.state, value: if (start.negative) -line else line}
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
    else if (contains(["hyphenchar", "skewchar"], primitive(start.state, t))) {
        let value = font_character(start.state, budget, primitive(start.state, t))^;
        {state: value.state, value: if (start.negative) -value.value else value.value}
    }
    else if (primitive(start.state, t) == "fontdimen") {
        let value = font_dimen(start.state, budget)^;
        {state: value.state, value: if (start.negative) -value.value else value.value}
    }
    else if (primitive(start.state, t) == "dimexpr") {
        let expression = scan_expression(start.state, budget, "dimen")^;
        {state: expression.state, value: if (start.negative) -expression.value else expression.value}
    }
    else if (contains(["gluestretch", "glueshrink", "gluestretchorder", "glueshrinkorder"], primitive(start.state, t))) {
        let part = glue_quantity(start.state, primitive(start.state, t), budget)^;
        {state: part.state, value: if (start.negative) -part.value else part.value}
    }
    else if (contains(["catcode", "lccode", "uccode", "sfcode", "mathcode", "delcode"], primitive(start.state, t))) {
        let code = code_quantity(start.state, primitive(start.state, t), budget)^;
        {state: code.state, value: if (start.negative) -code.value else code.value}
    }
    else if (other(t, "'") or other(t, "\"")) {
        let read = expanded_next(start.state, budget);
        digits(read.state, read.token, if (t.value == "'") 8 else 16, start.negative, budget)
    }
    else digits(start.state, t, 10, start.negative, budget)
}

let COUNT_REGISTER_LIMIT = 32768

fn scan_register(st, budget) any^ {
    let value = scan_number(st, budget)^;
    if (value.state.issue != null) value
    else if (value.value < 0 or value.value >= COUNT_REGISTER_LIMIT) {state: fail(value.state, "tex-bad-register"), value: 0}
    else value
}

fn expression_factor(st, budget, level = "int") any^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) {state: first.state, value: 0}
    else if (other(first.token, "(")) {
        let nested = expression_sum(first.state, budget, level)^;
        let close = expanded_nonspace(nested.state, budget)^;
        if (nested.state.issue != null) nested
        else if (not other(close.token, ")")) {state: fail(put_back(close.state, close.token), "tex-missing-paren"), value: 0}
        else {state: close.state, value: nested.value}
    }
    else if (level == "dimen") scan_dimen(put_back(first.state, first.token), budget)
    else if (level == "glue" or level == "mu") scan_glue(put_back(first.state, first.token), budget, level == "mu")
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

fn expression_scaled(st, value, level, numerator, denominator) any^ {
    if (level == "glue" or level == "mu") {
        let width = rounded_fraction(st, value.width * numerator, denominator)^;
        let stretch = rounded_fraction(width.state, value.stretch * numerator, denominator)^;
        let shrink = rounded_fraction(stretch.state, value.shrink * numerator, denominator)^;
        let result = {width: width.value, stretch: stretch.value,
            stretch_order: if (stretch.value == 0) 0 else value.stretch_order,
            shrink: shrink.value, shrink_order: if (shrink.value == 0) 0 else value.shrink_order};
        if (width.state.issue != null) {state: width.state, value: result}
        else if (stretch.state.issue != null) {state: stretch.state, value: result}
        else if (shrink.state.issue != null) {state: shrink.state, value: result}
        else if (not glue.valid(result)) {state: fail(shrink.state, "tex-arith-overflow"), value: result}
        else {state: shrink.state, value: result}
    }
    else {
        let result = rounded_fraction(st, value * numerator, denominator)^;
        if (result.state.issue == null and level == "dimen" and abs(result.value) >= 1073741824)
            {state: fail(result.state, "tex-arith-overflow"), value: 0}
        else result
    }
}

fn expression_added(st, a, b, level, subtract) any^ {
    if (level == "glue" or level == "mu") {
        let result = glue.add(a, if (subtract) glue.negate(b) else b)^;
        if (glue.valid(result)) {state: st, value: result}
        else {state: fail(st, "tex-arith-overflow"), value: result}
    }
    else {
        let result = checked_integer(st, if (subtract) a - b else a + b)^;
        if (result.state.issue == null and level == "dimen" and abs(result.value) >= 1073741824)
            {state: fail(result.state, "tex-arith-overflow"), value: 0}
        else result
    }
}

fn expression_term_tail(st, value, budget, level) any^ {
    let op = expanded_nonspace(st, budget)^;
    if (op.state.issue != null) {state: op.state, value: value}
    else if (other(op.token, "*") or other(op.token, "/")) {
        let right = expression_factor(op.state, budget, "int")^;
        if (right.state.issue != null) right
        else if (other(op.token, "/")) {
            let result = expression_scaled(right.state, value, level, 1, right.value)^;
            if (result.state.issue != null) result else expression_term_tail(result.state, result.value, budget, level)
        } else {
            let next = expanded_nonspace(right.state, budget)^;
            if (other(next.token, "/")) {
                let denominator = expression_factor(next.state, budget, "int")^;
                if (denominator.state.issue != null) denominator
                else {
                    let scaled = expression_scaled(denominator.state, value, level, right.value, denominator.value)^;
                    if (scaled.state.issue != null) scaled else expression_term_tail(scaled.state, scaled.value, budget, level)
                }
            } else {
                let product = expression_scaled(put_back(next.state, next.token), value, level, right.value, 1)^;
                if (product.state.issue != null) product else expression_term_tail(product.state, product.value, budget, level)
            }
        }
    }
    else {state: put_back(op.state, op.token), value: value}
}

fn expression_term(st, budget, level) any^ {
    let first = expression_factor(st, budget, level)^;
    if (first.state.issue != null) first else expression_term_tail(first.state, first.value, budget, level)
}

fn expression_sum_tail(st, value, budget, level) any^ {
    let op = expanded_nonspace(st, budget)^;
    if (op.state.issue != null) {state: op.state, value: value}
    else if (other(op.token, "+") or other(op.token, "-")) {
        let right = expression_term(op.state, budget, level)^;
        if (right.state.issue != null) right
        else {
            let sum = expression_added(right.state, value, right.value, level, other(op.token, "-"))^;
            if (sum.state.issue != null) sum else expression_sum_tail(sum.state, sum.value, budget, level)
        }
    }
    else {state: put_back(op.state, op.token), value: value}
}

fn expression_sum(st, budget, level) any^ {
    let first = expression_term(st, budget, level)^;
    if (first.state.issue != null) first else expression_sum_tail(first.state, first.value, budget, level)
}

fn scan_expression(st, budget, level = "int") any^ {
    let result = expression_sum(st, budget, level)^;
    if (result.state.issue != null) result
    else {
        let end = expanded_nonspace(result.state, budget)^;
        {state: if (primitive(end.state, end.token) == "relax") end.state else put_back(end.state, end.token),
            value: result.value}
    }
}

fn count_value(st, index) => if (st.counts[string(index)] == null) 0 else st.counts[string(index)]
fn set_count(counts, index, value) => {*:counts, *:map([string(index), value])}
fn save_count(st, index, value, global) any^ =>
    {*:st, counts: set_count(st.counts, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, counts: set_count(scope.counts, index, value)}] else st.groups}
fn dimen_value(st, index) => if (st.dimens[string(index)] == null) 0 else st.dimens[string(index)]
fn page_dimension(name) => contains(["pagegoal", "pagetotal", "pagestretch", "pagefilstretch",
    "pagefillstretch", "pagefilllstretch", "pageshrink", "pagedepth"], name)
fn parameter_value(st, name) any^ {
    let assigned = st.parameters[name];
    if (assigned != null) assigned
    else if (name == "hangafter" or name == "day" or name == "month") 1
    else if (name == "maxdeadcycles") 25 else if (name == "time") 720
    else if (name == "year") 2000 else 0
}
fn effective_global(st, explicit) any^ => if (parameter_value(st, "globaldefs") > 0) true
    else if (parameter_value(st, "globaldefs") < 0) false else explicit
fn parameter_index(name) => if (name == "everyeof") -1 else "param:" ++ name

fn toks_value(st, index) => if (st.toks[string(index)] == null) [] else st.toks[string(index)]
fn set_toks(values, index, value) => {*:values, *:map([string(index), value])}

fn optional_equals(st, budget) any^ {
    let next = expanded_nonspace(st, budget)^;
    if (other(next.token, "=")) next.state else put_back(next.state, next.token)
}

fn word_tail(st, word, position, consumed, budget) any^ {
    if (position == len(word)) {state: st, found: true}
    else {
        let read = expanded_next(st, budget)^;
        if (read.state.issue != null) {state: read.state, found: false}
        else if (read.token != null and read.token.kind == "char" and
            lower(read.token.value) == slice(word, position, position + 1))
            word_tail(read.state, word, position + 1, [*consumed, read.token], budget)
        else {
            let backed = put_back(read.state, read.token);
            // restored keyword prefixes are copied tokens, not direct source reads
            {state: {*:backed, input: stream.push(backed.input, stored_tokens(consumed))}, found: false}
        }
    }
}

fn scan_word(st, word, budget) any^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) {state: first.state, found: false}
    else if (first.token != null and first.token.kind == "char" and
        lower(first.token.value) == slice(word, 0, 1))
        word_tail(first.state, word, 1, [first.token], budget)
    else {state: put_back(first.state, first.token), found: false}
}

fn fraction_digits(st, budget, values = []) any^ {
    let read = expanded_next(st, budget)^;
    let n = if (charcat(read.token, 12)) digit(read.token, 10) else null;
    if (read.state.issue != null) {state: read.state, value: 0}
    else if (n == null) {state: if (charcat(read.token, 10)) read.state else put_back(read.state, read.token),
        value: dimension.round_decimal(values)}
    else fraction_digits(read.state, budget, if (len(values) < 17) [*values, n] else values)
}

fn dimension_unit(st, budget, names = ["pt", "in", "pc", "cm", "mm", "bp", "dd", "cc", "nd", "nc", "sp"], index = 0) any^ {
    if (index >= len(names)) {state: fail(st, "tex-illegal-unit"), name: null}
    else {
        let word = scan_word(st, names[index], budget)^;
        if (word.state.issue != null or word.found) {state: word.state, name: if (word.found) names[index] else null}
        else dimension_unit(word.state, budget, names, index + 1)
    }
}

fn dimension_finish(st, whole, fraction, negative, budget, infinite, mu) any^ {
    let next = expanded_nonspace(st, budget)^;
    let cmd = primitive(next.state, next.token);
    let m = if (is_control(next.token)) meaning(next.state, next.token) else null;
    if (next.state.issue != null) {state: next.state, value: 0}
    else if (cmd == "dimen" or m.kind == "dimension-register" or primitives.parameter_kind(cmd) == "dimen") {
        let index = if (cmd == "dimen") scan_register(next.state, budget)^
            else {state: next.state, value: if (m.kind == "dimension-register") m.index else "param:" ++ cmd};
        let value = dimen_value(index.state, index.value);
        let magnitude = whole * value + (abs(value) * fraction div 65536) * if (value < 0) -1 else 1;
        if (abs(magnitude) >= 1073741824) {state: fail(index.state, "tex-dimension-too-large"), value: 0}
        else {state: index.state, value: if (negative) -magnitude else magnitude, order: 0}
    }
    else {
        let available = put_back(next.state, next.token);
        let fil = if (infinite) scan_word(available, "fil", budget)^ else {state: available, found: false};
        if (fil.found) {
            let first = scan_word(fil.state, "l", budget)^;
            let second = if (first.found) scan_word(first.state, "l", budget)^ else first;
            let order = if (not first.found) 1 else if (not second.found) 2 else 3;
            let amount = whole * 65536 + fraction;
            if (amount >= 1073741824) {state: fail(second.state, "tex-dimension-too-large"), value: 0, order: 0}
            else {state: second.state, value: if (negative) -amount else amount, order: order}
        }
        else {
        let em = if (mu) {state: fil.state, found: false} else scan_word(fil.state, "em", budget)^;
        let ex = if (mu or em.found) {state: em.state, found: false} else scan_word(em.state, "ex", budget)^;
        let px = if (mu or em.found or ex.found) {state: ex.state, found: false} else scan_word(ex.state, "px", budget)^;
        if (em.found or ex.found or px.found) {
            let entry = px.state.fonts[px.state.current_font];
            let metric = if (px.found) dimen_value(px.state, "param:pdfpxdimen")
                else entry.params[if (em.found) "6" else "5"];
            let value = if (metric == null) 0 else metric;
            let magnitude = whole * value + (abs(value) * fraction div 65536) * if (value < 0) -1 else 1;
            let after = expanded_next(px.state, budget)^;
            let end = if (charcat(after.token, 10)) after.state else put_back(after.state, after.token);
            if (abs(magnitude) >= 1073741824) {state: fail(end, "tex-dimension-too-large"), value: 0}
            else {state: end, value: if (negative) -magnitude else magnitude, order: 0}
        }
        else {
        let true_word = if (mu) {state: px.state, found: false} else scan_word(px.state, "true", budget)^;
        let found = if (mu) {
            let word = scan_word(true_word.state, "mu", budget)^;
            {state: if (word.found) word.state else fail(word.state, "tex-illegal-unit"), name: "pt"}
        } else dimension_unit(true_word.state, budget)^;
        if (found.state.issue != null) {state: found.state, value: 0}
        else {
            let mag = if (true_word.found and found.state.parameters.mag > 0 and found.state.parameters.mag <= 32768)
                found.state.parameters.mag else 1000;
            let adjusted_whole = whole * 1000 div mag;
            let adjusted_fraction = (1000 * fraction + 65536 * ((whole * 1000) % mag)) div mag;
            let base_whole = adjusted_whole + adjusted_fraction div 65536;
            let base_fraction = adjusted_fraction % 65536;
            let ratio = dimension.unit(found.name);
            let scaled = if (found.name == "sp") base_whole
                else (base_whole * ratio[0] div ratio[1]) * 65536 +
                    (ratio[0] * base_fraction + 65536 * ((base_whole * ratio[0]) % ratio[1])) div ratio[1];
            let after = expanded_next(found.state, budget)^;
            let end = if (charcat(after.token, 10)) after.state else put_back(after.state, after.token);
            if (scaled >= 1073741824) {state: fail(end, "tex-dimension-too-large"), value: 0}
            else {state: end, value: if (negative) -scaled else scaled, order: 0}
        }
        }
        }
    }
}

fn dimen_digits(st, token, budget, value = 0) any^ {
    let n = digit(token, 10);
    if (n == null) {state: st, value: value, token: token}
    else if (value * 10 + n > 2147483647)
        {state: fail(st, "tex-number-too-big"), value: 0, token: null}
    else {
        let read = expanded_next(st, budget)^;
        if (read.state.issue != null) {state: read.state, value: 0, token: null}
        else dimen_digits(read.state, read.token, budget, value * 10 + n)
    }
}

fn scan_dimen(st, budget, infinite = false, mu = false) any^ {
    let start = signs(st, budget)^;
    let cmd = primitive(start.state, start.token);
    let m = if (is_control(start.token)) meaning(start.state, start.token) else null;
    if (start.state.issue != null) {state: start.state, value: 0}
    else if (cmd == "dimen" or m.kind == "dimension-register") {
        let index = if (cmd == "dimen") scan_register(start.state, budget)^ else {state: start.state, value: m.index};
        let value = dimen_value(index.state, index.value);
        {state: index.state, value: if (start.negative) -value else value, order: 0}
    }
    else if (primitives.parameter_kind(cmd) == "dimen") {
        let value = dimen_value(start.state, "param:" ++ cmd);
        {state: start.state, value: if (start.negative) -value else value, order: 0}
    }
    else if (page_dimension(cmd))
        {state: {*:start.state, page_state_quantity_seen: true}, value: 0, order: 0}
    else if (cmd == "fontdimen") {
        let value = font_dimen(start.state, budget)^;
        {state: value.state, value: if (start.negative) -value.value else value.value, order: 0}
    }
    else if (cmd == "dimexpr") {
        let value = scan_expression(start.state, budget, "dimen")^;
        {state: value.state, value: if (start.negative) -value.value else value.value, order: 0}
    }
    else if (cmd == "gluestretch" or cmd == "glueshrink") {
        let value = glue_quantity(start.state, cmd, budget)^;
        {state: value.state, value: if (start.negative) -value.value else value.value, order: 0}
    }
    else {
        let decimal_start = other(start.token, ".") or other(start.token, ",");
        let first_digit = digit(start.token, 10);
        let literal = decimal_start or first_digit != null;
        let whole_scan = if (decimal_start) {state: start.state, value: 0, token: start.token}
            else if (literal) dimen_digits(start.state, start.token, budget)^
            else {
                let scanned = scan_number(put_back(start.state, start.token), budget)^;
                {state: scanned.state, value: scanned.value, token: null}
            };
        if (whole_scan.state.issue != null) {state: whole_scan.state, value: 0}
        else {
            let fraction = if (literal and (other(whole_scan.token, ".") or other(whole_scan.token, ",")))
                fraction_digits(whole_scan.state, budget)^
                else {state: if (charcat(whole_scan.token, 10)) whole_scan.state
                    else put_back(whole_scan.state, whole_scan.token), value: 0};
            if (fraction.state.issue != null) fraction
            else dimension_finish(fraction.state, abs(whole_scan.value), fraction.value,
                start.negative != (whole_scan.value < 0), budget, infinite, mu)
        }
    }
}

fn save_dimen(st, index, value, global) any^ =>
    {*:st, dimens: set_count(st.dimens, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, dimens: set_count(scope.dimens, index, value)}] else st.groups}

fn assign_dimen(st, global, budget, fixed_index) any^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let value = scan_dimen(optional_equals(index.state, budget), budget)^;
        if (value.state.issue != null) value.state else save_dimen(value.state, index.value, value.value, global)
    }
}

fn assign_dimendef(st, global, budget) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let index = scan_register(optional_equals(cleared, budget), budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "dimension-register", index: index.value}, global)
    }
}

fn glue_value(st, index, mu) any^ {
    let table = if (mu) st.muskips else st.skips;
    if (table[string(index)] == null) glue.zero() else table[string(index)]
}

fn save_glue(st, index, value, global, mu) any^ {
    if (mu) {*:st, muskips: set_count(st.muskips, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, muskips: set_count(scope.muskips, index, value)}] else st.groups}
    else {*:st, skips: set_count(st.skips, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, skips: set_count(scope.skips, index, value)}] else st.groups}
}

fn scan_glue(st, budget, mu) any^ {
    let start = signs(st, budget)^;
    let cmd = primitive(start.state, start.token);
    let m = if (is_control(start.token)) meaning(start.state, start.token) else null;
    let kind = primitives.parameter_kind(cmd);
    let is_register = cmd == "skip" or cmd == "muskip" or m.kind == "glue-register" or kind == "glue" or kind == "mu";
    let register_mu = if (m.kind == "glue-register") m.mu else cmd == "muskip" or kind == "mu";
    if (start.state.issue != null) {state: start.state, value: glue.zero()}
    else if (contains(["glueexpr", "muexpr", "gluetomu", "mutoglue"], cmd)) {
        let source_mu = cmd == "muexpr" or cmd == "gluetomu";
        let value = if (cmd == "glueexpr" or cmd == "muexpr")
            scan_expression(start.state, budget, if (cmd == "muexpr") "mu" else "glue")^
            else scan_glue(start.state, budget, cmd == "mutoglue")^;
        if (source_mu != mu) {state: fail(value.state, "tex-mu-error"), value: value.value}
        else {state: value.state, value: if (start.negative) glue.negate(value.value) else value.value}
    }
    else if (is_register) {
        let index = if (m.kind == "glue-register") {state: start.state, value: m.index}
            else if (kind == "glue" or kind == "mu") {state: start.state, value: "param:" ++ cmd}
            else scan_register(start.state, budget)^;
        let value = glue_value(index.state, index.value, register_mu);
        if (register_mu != mu) {state: fail(index.state, "tex-mu-error"), value: value}
        else {state: index.state, value: if (start.negative) glue.negate(value) else value}
    }
    else {
        let tokens = [*(if (start.negative) text_tokens("-") else []),
            *(if (start.token == null) [] else [start.token])];
        let width = scan_dimen({*:start.state, input: stream.push(start.state.input, tokens)}, budget, false, mu)^;
        if (width.state.issue != null) {state: width.state, value: glue.zero()}
        else {
            let plus = scan_word(width.state, "plus", budget)^;
            let stretch = if (plus.found) scan_dimen(plus.state, budget, true, mu)^
                else {state: plus.state, value: 0, order: 0};
            if (stretch.state.issue != null) {state: stretch.state, value: glue.zero()}
            else {
                let minus = scan_word(stretch.state, "minus", budget)^;
                let shrink = if (minus.found) scan_dimen(minus.state, budget, true, mu)^
                    else {state: minus.state, value: 0, order: 0};
                {state: shrink.state, value: {width: width.value, stretch: stretch.value,
                    stretch_order: stretch.order, shrink: shrink.value, shrink_order: shrink.order}}
            }
        }
    }
}

fn glue_quantity(st, name, budget) any^ {
    let scanned = scan_glue(st, budget, false)^;
    {state: scanned.state, value: if (name == "gluestretch") scanned.value.stretch
        else if (name == "glueshrink") scanned.value.shrink
        else if (name == "gluestretchorder") scanned.value.stretch_order else scanned.value.shrink_order}
}

fn assign_glue(st, global, budget, fixed_index, mu) any^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let value = scan_glue(optional_equals(index.state, budget), budget, mu)^;
        if (value.state.issue != null) value.state else save_glue(value.state, index.value, value.value, global, mu)
    }
}

fn assign_gluedef(st, global, budget, mu) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let index = scan_register(optional_equals(cleared, budget), budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "glue-register", index: index.value, mu: mu}, global)
    }
}

fn scan_filename_tail(st, budget, quoted, chars) any^ {
    let read = expanded_next(st, budget)^;
    let t = read.token;
    if (read.state.issue != null) {state: read.state, name: ""}
    else if (t == null) {state: read.state, name: join(chars, "")}
    else if (t.kind != "char" or (charcat(t, 10) and not quoted))
        {state: if (charcat(t, 10)) read.state else put_back(read.state, t), name: join(chars, "")}
    else if (t.value == "\"") scan_filename_tail(read.state, budget, not quoted, chars)
    else scan_filename_tail(read.state, budget, quoted, [*chars, t.value])
}

fn scan_filename(st, budget) any^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) {state: first.state, name: ""}
    else if (charcat(first.token, 1)) {
        let contents = expanded_text(put_back(first.state, first.token), budget)^;
        {state: contents.state, name: replace(token_text(contents.state, contents.tokens), "\"", "")^}
    }
    else scan_filename_tail(put_back(first.state, first.token), budget, false, [])
}

fn font_metrics(size) any^ => map([for (i, v in [0, 218453, 109226, 72818, 282168, 655360, 72818],
    part in [string(i + 1), v * size div 655360]) part])

fn define_font(st, global, budget) any^ {
    let target = nonspace(st.input);
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let name = scan_filename(optional_equals(cleared, budget), budget)^;
        let at = scan_word(name.state, "at", budget)^;
        let scaled = if (at.found) {state: at.state, found: false} else scan_word(at.state, "scaled", budget)^;
        let quantity = if (at.found) scan_dimen(at.state, budget)^
            else if (scaled.found) scan_number(scaled.state, budget)^ else {state: scaled.state, value: 1000};
        if (quantity.state.issue != null) quantity.state
        else {
            let size = if (at.found) quantity.value else 655360 * quantity.value div 1000;
            if ((at.found and (size <= 0 or size >= 134217728)) or
                (scaled.found and (quantity.value <= 0 or quantity.value > 32768)))
                fail(quantity.state, "tex-improper-at")
            else {
                let found = [for (i, entry in quantity.state.fonts where i > 0 and entry.name == name.name and entry.size == size) i];
                let index = if (len(found) > 0) found[0] else len(quantity.state.fonts);
                let current = if (len(found) > 0) quantity.state else {*:quantity.state,
                    fonts: [*quantity.state.fonts, {name: name.name, size: size, param_base: quantity.state.font_memory,
                        param_count: 7, params: font_metrics(size),
                        ident: {kind: target.token.kind, value: target.token.value, cat: null, start: -1, end: -1},
                        hyphen_char: parameter_value(quantity.state, "defaulthyphenchar"),
                        skew_char: parameter_value(quantity.state, "defaultskewchar")}], font_memory: quantity.state.font_memory + 7};
                assign_meaning(current, key(target.token), {kind: "font", index: index}, global)
            }
        }
    }
}

fn scan_font_ident(st, budget) any^ {
    let read = expanded_nonspace(st, budget)^;
    let cmd = primitive(read.state, read.token);
    let m = if (is_control(read.token)) meaning(read.state, read.token) else null;
    if (read.state.issue != null) {state: read.state, value: 0}
    else if (m.kind == "font") {state: read.state, value: m.index}
    else if (cmd == "nullfont") {state: read.state, value: 0}
    else if (cmd == "font") {state: read.state, value: read.state.current_font}
    else if (contains(["textfont", "scriptfont", "scriptscriptfont"], cmd)) {
        let family = font_family_slot(read.state, budget, cmd)^;
        let value = family.state.font_families[family.key];
        {state: family.state, value: if (value == null) 0 else value}
    }
    else {
        let diagnosed = record_diagnostic(read.state, "tex-missing-font", "Missing font identifier")^;
        {state: put_back(diagnosed, read.token), value: 0}
    }
}

// native treats letter spacing as a scanned parameter of a font alias.
fn assign_letterspace_font(st, global, budget) any^ {
    let target = nonspace(st.input);
    let current = {*:st, input: target.stream, font_alias_assignment_seen: true};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else {
        let font = scan_font_ident(current, budget)^;
        if (font.state.issue != null) font.state
        else {
            let spacing = scan_number(font.state, budget)^;
            if (spacing.state.issue != null) spacing.state
            else assign_meaning(spacing.state, key(target.token), {kind: "font", index: font.value}, global)
        }
    }
}

fn font_family_slot(st, budget, name) any^ {
    let family = scan_number(st, budget)^;
    let slot = ((family.value % 16) + 16) % 16;
    {state: family.state, key: name ++ ":" ++ string(slot)}
}

// native input stores math font families on the engine, outside group saves.
fn assign_font_family(st, budget, name) any^ {
    let family = font_family_slot({*:st, font_family_assignment_seen: true}, budget, name)^;
    if (family.state.issue != null) family.state
    else {
        let value = scan_font_ident(optional_equals(family.state, budget), budget)^;
        if (value.state.issue != null) value.state
        else {*:value.state, font_families: {*:value.state.font_families, *:map([family.key, value.value])}}
    }
}

fn font_character(st, budget, name) any^ {
    let selected = scan_font_ident({*:st, font_character_quantity_seen: true}, budget)^;
    let font = selected.state.fonts[selected.value];
    {state: selected.state, value: if (name == "hyphenchar") font.hyphen_char else font.skew_char}
}

// font properties belong to the shared font object, independently of grouping.
fn assign_font_character(st, budget, name) any^ {
    let selected = scan_font_ident({*:st, font_character_assignment_seen: true}, budget)^;
    if (selected.state.issue != null) selected.state
    else {
        let value = scan_number(optional_equals(selected.state, budget), budget)^;
        if (value.state.issue != null) value.state
        else {*:value.state, fonts: [for (i, font in value.state.fonts)
            if (i != selected.value) font else if (name == "hyphenchar") {*:font, hyphen_char: value.value}
            else {*:font, skew_char: value.value}]}
    }
}

fn font_dimen_index(st, budget) any^ {
    let n = scan_number(st, budget)^;
    let selected = if (n.state.issue == null) scan_font_ident(n.state, budget)^ else {state: n.state, value: 0};
    if (selected.state.issue != null) {state: selected.state, font_index: 0, param: n.value, valid: false}
    else {
        let entry = selected.state.fonts[selected.value];
        let grow = n.value > entry.param_count and n.value > 0 and selected.value == len(selected.state.fonts) - 1;
        if (grow and entry.param_base + n.value > 8000000)
            {state: fail(selected.state, "tex-capacity"), font_index: selected.value, param: n.value, valid: false}
        else if (n.value <= 0 or (n.value > entry.param_count and not grow))
            {state: {*:selected.state, diagnostics: [*selected.state.diagnostics, "tex-font-params"]},
                font_index: selected.value, param: n.value, valid: false}
        else {
            let current = if (grow) {*:selected.state,
                fonts: [for (i, x in selected.state.fonts) if (i == selected.value) {*:x, param_count: n.value} else x],
                font_memory: entry.param_base + n.value} else selected.state;
            {state: current, font_index: selected.value, param: n.value, valid: true}
        }
    }
}

fn font_dimen(st, budget) any^ {
    let target = font_dimen_index(st, budget)^;
    let entry = target.state.fonts[target.font_index];
    let value = if (target.valid) entry.params[string(target.param)] else null;
    {state: target.state, value: if (value == null) 0 else value}
}

fn assign_font_dimen(st, budget) any^ {
    let target = font_dimen_index(st, budget)^;
    if (target.state.issue != null) target.state
    else {
        let value = scan_dimen(optional_equals(target.state, budget), budget)^;
        if (value.state.issue != null or not target.valid) value.state
        else {*:value.state, fonts: [for (i, entry in value.state.fonts)
            if (i == target.font_index) {*:entry, params: set_count(entry.params, target.param, value.value)} else entry]}
    }
}

fn select_font(st, global, index) any^ => {*:st, current_font: index,
    groups: if (global) [for (scope in st.groups) {*:scope, current_font: index}] else st.groups}

fn assign_count(st, global, budget, fixed_index) any^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let before = optional_equals(index.state, budget)^;
        let value = scan_number(before, budget)^;
        if (value.state.issue != null) value.state
        else save_count(value.state, index.value, value.value, global)
    }
}

fn optional_by(st, budget) any^ => scan_word(st, "by", budget).state

fn arithmetic_glue(st, global, budget, operation, fixed_index, mu) any^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let before = optional_by(index.state, budget)^;
        let previous = glue_value(before, index.value, mu);
        if (operation == "advance") {
            let operand = scan_glue(before, budget, mu)^;
            let sum = glue.add(previous, operand.value)^;
            if (operand.state.issue != null) operand.state
            else if (not glue.valid(sum)) fail(operand.state, "tex-arith-overflow")
            else save_glue(operand.state, index.value, sum, global, mu)
        }
        else {
            let factor = scan_number(before, budget)^;
            if (factor.state.issue != null) factor.state
            else if (operation == "divide" and factor.value == 0) fail(factor.state, "tex-arith-overflow")
            else {
                let scaled = glue.scale(previous, factor.value, operation == "divide")^;
                if (not glue.valid(scaled)) fail(factor.state, "tex-arith-overflow")
                else save_glue(factor.state, index.value, scaled, global, mu)
            }
        }
    }
}

fn arithmetic(st, global, budget, operation) any^ {
    let target = expanded_nonspace(st, budget)^;
    let cmd = primitive(target.state, target.token);
    let resolved = if (is_control(target.token)) meaning(target.state, target.token) else null;
    let kind = primitives.parameter_kind(cmd);
    let is_dimen = cmd == "dimen" or resolved.kind == "dimension-register" or kind == "dimen";
    if (target.state.issue != null) target.state
    else if (cmd == "skip" or cmd == "muskip" or resolved.kind == "glue-register" or kind == "glue" or kind == "mu")
        arithmetic_glue(target.state, global, budget, operation,
            if (resolved.kind == "glue-register") resolved.index else if (kind == "glue" or kind == "mu") "param:" ++ cmd else null,
            if (resolved.kind == "glue-register") resolved.mu else cmd == "muskip" or kind == "mu")
    else if (cmd != "count" and resolved.kind != "register" and not is_dimen and kind != "int") unsupported(target.state, operation)
    else {
        let index = if (cmd == "count" or cmd == "dimen") scan_register(target.state, budget)^
            else {state: target.state, value: if (kind == "dimen") "param:" ++ cmd else resolved.index};
        if (index.state.issue != null) index.state
        else {
            let before = optional_by(index.state, budget)^;
            let operand = if (is_dimen and operation == "advance") scan_dimen(before, budget)^ else scan_number(before, budget)^;
            let previous = if (kind == "int") parameter_value(operand.state, cmd)
                else if (is_dimen) dimen_value(operand.state, index.value) else count_value(operand.state, index.value);
            if (operand.state.issue != null) operand.state
            else if (operation == "divide" and operand.value == 0) fail(operand.state, "tex-arith-overflow")
            else {
                let value = if (operation == "advance") previous + operand.value
                    else if (operation == "multiply") previous * operand.value
                    else if ((previous < 0) != (operand.value < 0)) -(abs(previous) div abs(operand.value))
                    else abs(previous) div abs(operand.value);
                let checked = checked_integer(operand.state, value)^;
                if (checked.state.issue != null) checked.state
                else if (is_dimen and abs(value) >= 1073741824) fail(checked.state, "tex-arith-overflow")
                else if (kind == "int") save_parameter(checked.state, global, cmd, value)
                else if (is_dimen) save_dimen(checked.state, index.value, value, global)
                else save_count(checked.state, index.value, value, global)
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
            else save_toks(read, index.value, [for (t in value.tokens) {*:t, noexpand: false, direct: false}], global)
        }
        else if (primitive(current, first.token) == "toks" or primitives.parameter_kind(primitive(current, first.token)) == "toks" or m.kind == "token-register") {
            let from = if (primitives.parameter_kind(primitive(current, first.token)) == "toks")
                    {state: current, value: parameter_index(primitive(current, first.token))}
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

// numeric the-expansion is internal to the current definition trace.
fn the_text(st, text) any^ {
    let tokens = text_tokens(text)^;
    if (st.token_text_scan) [for (token in tokens) {*:token, trace_internal: true}] else tokens
}

// capture the value for inspection without expanding its token-list contents.
fn the_push(input, tokens, capture) any^ => if (capture) {*:input, captured_the: tokens}
    else stream.push(input, tokens)

fn expand_the(st, budget, count_expansion = true, capture = false) any^ {
    if (count_expansion and st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = expanded_next({*:st, expansions: st.expansions + if (count_expansion) 1 else 0}, budget)^;
        let m = if (is_control(read.token)) meaning(read.state, read.token) else null;
        if (primitive(read.state, read.token) == "count" or m.kind == "register") {
            let index = if (m.kind == "register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: the_push(index.state.input, the_text(st, string(count_value(index.state, index.value))), capture)}
        }
        else if (m.kind == "font" or contains(["font", "nullfont", "textfont", "scriptfont", "scriptscriptfont"], primitive(read.state, read.token))) {
            let selected = scan_font_ident(put_back(read.state, read.token), budget)^;
            if (selected.state.issue != null) selected.state
            else {
                let token = selected.state.fonts[selected.value].ident;
                let result = if (selected.state.token_text_scan) {*:token, noexpand: true, direct_toks: true, trace_internal: true} else token;
                {*:selected.state, font_token_seen: true, input: the_push(selected.state.input, [result], capture)}
            }
        }
        else if (contains(["hyphenchar", "skewchar"], primitive(read.state, read.token))) {
            let value = font_character(read.state, budget, primitive(read.state, read.token))^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, the_text(st, string(value.value)), capture)}
        }
        else if (primitive(read.state, read.token) == "fontdimen") {
            let value = font_dimen(read.state, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, the_text(st, dimension.format(value.value)), capture)}
        }
        else if (primitive(read.state, read.token) == "dimen" or m.kind == "dimension-register") {
            let index = if (m.kind == "dimension-register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: the_push(index.state.input,
                the_text(st, dimension.format(dimen_value(index.state, index.value))), capture)}
        }
        else if (primitive(read.state, read.token) == "skip" or primitive(read.state, read.token) == "muskip" or m.kind == "glue-register") {
            let mu = if (m.kind == "glue-register") m.mu else primitive(read.state, read.token) == "muskip";
            let index = if (m.kind == "glue-register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: the_push(index.state.input,
                the_text(st, glue.format(glue_value(index.state, index.value, mu), mu)), capture)}
        }
        else if (primitives.parameter_kind(primitive(read.state, read.token)) == "int")
            {*:read.state, input: the_push(read.state.input,
                the_text(st, string(parameter_value(read.state, primitive(read.state, read.token)))), capture)}
        else if (primitives.parameter_kind(primitive(read.state, read.token)) == "dimen")
            {*:read.state, input: the_push(read.state.input,
                the_text(st, dimension.format(dimen_value(read.state, "param:" ++ primitive(read.state, read.token)))), capture)}
        else if (contains(["glue", "mu"], primitives.parameter_kind(primitive(read.state, read.token)))) {
            let name = primitive(read.state, read.token);
            let mu = primitives.parameter_kind(name) == "mu";
            {*:read.state, input: the_push(read.state.input, the_text(st, glue.format(glue_value(read.state, "param:" ++ name, mu), mu)), capture)}
        }
        else if (contains(["parshape", "interlinepenalties", "clubpenalties", "widowpenalties", "displaywidowpenalties"], primitive(read.state, read.token))) {
            let value = paragraph_quantity(read.state, primitive(read.state, read.token), budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, the_text(st, "0"), capture)}
        }
        else if (primitive(read.state, read.token) == "interactionmode" or page_dimension(primitive(read.state, read.token))) {
            let text = if (primitive(read.state, read.token) == "interactionmode") "1" else "0.0pt";
            {*:read.state, page_state_quantity_seen: true, input: the_push(read.state.input, the_text(st, text), capture)}
        }
        else if (contains(["pdfrandomseed", "pdfelapsedtime"], primitive(read.state, read.token))) {
            let value = if (primitive(read.state, read.token) == "pdfrandomseed") read.state.random_seed % 2147483648 else 0;
            {*:read.state, pdf_random_seen: true, input: the_push(read.state.input, the_text(st, string(value)), capture)}
        }
        else if (primitive(read.state, read.token) == "inputlineno")
            {*:read.state, input: the_push(read.state.input, the_text(st, string(input_line_number(read.state)^)), capture)}
        else if (primitive(read.state, read.token) == "eTeXversion")
            {*:read.state, input: the_push(read.state.input, the_text(st, "2"), capture)}
        else if (primitive(read.state, read.token) == "numexpr") {
            let value = scan_expression(read.state, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, the_text(st, string(value.value)), capture)}
        }
        else if (contains(["dimexpr", "glueexpr", "muexpr"], primitive(read.state, read.token))) {
            let name = primitive(read.state, read.token);
            let level = if (name == "dimexpr") "dimen" else if (name == "glueexpr") "glue" else "mu";
            let value = scan_expression(read.state, budget, level)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input,
                the_text(st, if (level == "dimen") dimension.format(value.value) else glue.format(value.value, level == "mu")), capture)}
        }
        else if (contains(["gluestretch", "glueshrink", "gluestretchorder", "glueshrinkorder"], primitive(read.state, read.token))) {
            let name = primitive(read.state, read.token);
            let value = glue_quantity(read.state, name, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input,
                the_text(st, if (name == "gluestretch" or name == "glueshrink") dimension.format(value.value) else string(value.value)), capture)}
        }
        else if (contains(["currentgrouplevel", "currentiflevel"], primitive(read.state, read.token))) {
            let value = if (primitive(read.state, read.token) == "currentgrouplevel") len(read.state.groups)
                else len(read.state.conditions);
            {*:read.state, input: the_push(read.state.input, the_text(st, string(value)), capture)}
        }
        else if (contains(["catcode", "lccode", "uccode", "sfcode", "mathcode", "delcode"], primitive(read.state, read.token))) {
            let code = code_quantity(read.state, primitive(read.state, read.token), budget)^;
            if (code.state.issue != null) code.state
            else {*:code.state, input: the_push(code.state.input, the_text(st, string(code.value)), capture)}
        }
        else if (primitive(read.state, read.token) == "toks" or primitives.parameter_kind(primitive(read.state, read.token)) == "toks" or m.kind == "token-register") {
            let index = if (primitives.parameter_kind(primitive(read.state, read.token)) == "toks")
                    {state: read.state, value: parameter_index(primitive(read.state, read.token))}
                else if (m.kind == "token-register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {
                let tokens = toks_value(index.state, index.value);
                let stored = if (index.state.token_text_scan) [for (t in tokens)
                    {*:t, noexpand: is_control(t), direct_toks: true, trace_internal: true}] else tokens;
                {*:index.state, input: the_push(index.state.input, stored, capture)}
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
                let escaped = any([for (c in ["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"]) contains(text, c)]);
                {*:current, input: stream.push(read.stream, text_tokens(text)),
                    generated_text_mapping_seen: current.generated_text_mapping_seen == true or escaped}
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
                    let id = next_source_id(opening.state)^;
                    let child = {*:reader.reader(scanned, id), catcodes: text.stream.reader.catcodes,
                        endline: text.stream.reader.endline};
                    {*:opening.state, input: stream.push_reader(text.stream, child),
                        sources: [*opening.state.sources, {id: id, name: "\\scantokens", origin: "local", data: scanned}]}
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

// native expansion has no page builder, so marks read as empty token lists.
fn empty_mark_query(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        if (contains(["topmarks", "firstmarks", "botmarks", "splitfirstmarks", "splitbotmarks"], name)) {
            let scanned = scan_number(current, budget)^;
            scanned.state
        }
        else current
    }
}
fn expanded_nonrelax(st, budget) any^ {
    let read = expanded_nonspace(st, budget)^;
    if (read.state.issue != null or primitive(read.state, read.token) != "relax") read
    else expanded_nonrelax(read.state, budget)
}
// tex expands the opening brace before starting a group.
fn scan_opening_brace(st, budget) any^ {
    let opening = expanded_nonrelax(st, budget)^;
    if (opening.state.issue != null) opening
    else {
        let m = if (is_control(opening.token)) meaning(opening.state, opening.token) else null;
        if (charcat(opening.token, 1) or (m.kind == "char" and m.cat == 1)) opening
        else {state: fail(put_back(opening.state, opening.token), "tex-missing-brace"), token: null}
    }
}
fn discretionary_group(st, budget) any^ {
    let opening = scan_opening_brace(st, budget)^;
    if (opening.state.issue != null) {state: opening.state, tokens: []}
    else {
        let token = if (charcat(opening.token, 1)) opening.token
            else {*:opening.token, kind: "char", cat: 1, value: "{"};
        let group = stream.raw_argument(stream.push(opening.state.input, [token]))^;
        let current = {*:opening.state, input: group.stream};
        {state: if (group.error == null) current else fail(current, group.error), tokens: group.tokens}
    }
}

// the native engine does not retain paragraph shapes or penalty lists.
fn paragraph_quantity(st, name, budget) any^ {
    let marked = {*:st, paragraph_quantity_seen: true};
    if (name == "parshape") {state: marked, value: 0}
    else {
        let index = scan_number(marked, budget)^;
        {state: index.state, value: 0}
    }
}

// native has no page builder or interaction state, but scans assignments.
fn assign_page_state(st, budget, name) any^ {
    let marked = {*:st, page_state_assignment_seen: true};
    let start = optional_equals(marked, budget)^;
    let value = if (name == "interactionmode") scan_number(start, budget)^ else scan_dimen(start, budget)^;
    value.state
}

// native scans these assignments but has no paragraph layout state to retain.
fn scan_paragraph_values(st, budget, remaining, dimensions) any^ {
    if (remaining <= 0 or st.issue != null) st
    else {
        let value = if (dimensions) scan_dimen(st, budget)^ else scan_number(st, budget)^;
        scan_paragraph_values(value.state, budget, remaining - 1, dimensions)
    }
}
fn assign_paragraph_data(st, budget, name) any^ {
    let marked = {*:st, paragraph_data_seen: true};
    if (name == "patterns" or name == "hyphenation") {
        let text = discretionary_group(marked, budget)^;
        text.state
    }
    else {
        let count = scan_number(optional_equals(marked, budget), budget)^;
        let dimensions = name == "parshape";
        let remaining = if (count.value <= 0) 0 else if (dimensions and count.value > 2048) 4096
            else if (dimensions) count.value * 2 else if (count.value > 4096) 4096 else count.value;
        scan_paragraph_values(count.state, budget, remaining, dimensions)
    }
}

fn emit_discretionary(st, budget) any^ {
    let pre = discretionary_group(st, budget)^;
    if (pre.state.issue != null) pre.state
    else {
        let post = discretionary_group(pre.state, budget)^;
        if (post.state.issue != null) post.state
        else {
            let replacement = discretionary_group(post.state, budget)^;
            if (replacement.state.issue != null) replacement.state
            else {*:replacement.state, input: stream.push(replacement.state.input, stored_tokens(replacement.tokens))}
        }
    }
}

// native consumes expanded mark and special text without executing its contents.
fn discard_mark_text(st, budget, indexed) any^ {
    let scanned = if (indexed) scan_number(st, budget)^ else {state: st};
    if (scanned.state.issue != null) scanned.state
    else {
        let text = expanded_text(scanned.state, budget)^;
        text.state
    }
}

fn pdf_string_primitive(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let keyword = if (name == "pdfmdfivesum") scan_word(current, "file", budget)^ else {state: current, found: false};
        if (keyword.found) pdf_file_primitive(keyword.state, "pdfmdfivesum file", budget, true)
        else {
            let scanned = expanded_text(keyword.state, budget)^;
            if (scanned.state.issue != null) scanned.state
            else {
                let text = token_text(scanned.state, scanned.tokens)^;
                let result = if (name == "pdfescapestring") pdfstrings.escape_string(text)^
                    else if (name == "pdfescapename") pdfstrings.escape_name(text)^
                    else if (name == "pdfescapehex") pdfstrings.escape_hex(text)^
                    else if (name == "pdfmdfivesum") md5.digest(text)^ else pdfstrings.unescape_hex(text)^;
                {*:scanned.state, input: stream.push(scanned.state.input, text_tokens(result))}
            }
        }
    }
}

fn next_source_id(st) any^ => max(len(st.sources) + abs(st.input.reader.file_id) + 1,
    max([0, *[for (source in st.sources) source.id]]) + 1)

// The caller supplies captured, authorized bytes; this module does not open paths.
fn captured_resource(st, name, requested_scope = null) any^ {
    let cleaned = replace(name, "\"", "")^;
    let active = stream.active_reader(st.input)^;
    let scope = if (requested_scope != null) requested_scope else if (active.origin == "bundled") "bundled" else "local";
    if (st.resources == null) {state: st, available: false, found: false, data: null}
    else if (not (st.resources is map) or (st.resources[scope] != null and not (st.resources[scope] is map)))
        {state: fail(st, "tex-resource-invalid"), available: true, found: false, data: null}
    else if (st.resources[scope] == null) {state: st, available: false, found: false, data: null}
    else if (cleaned == "" or starts_with(cleaned, "/") or contains(cleaned, ".."))
        {state: st, available: true, found: false, data: null}
    else {
        let entry = st.resources[scope][cleaned];
        if (entry == null) {state: st, available: true, found: false, data: null}
        else if (not (entry.data is string or entry.data is binary))
            {state: fail(st, "tex-resource-invalid"), available: true, found: false, data: null}
        else {
            let bytes = binary(entry.data)^;
            let resource_key = scope ++ ":" ++ cleaned;
            let seen = st.resources_seen[resource_key] != null;
            let next_count = st.resource_count + if (seen or scope == "bundled") 0 else 1;
            let next_bytes = st.resource_bytes + if (seen or scope == "bundled") 0 else len(bytes);
            if (next_count > 128 or next_bytes > 16777216 or (scope == "bundled" and len(bytes) > 16777216))
                {state: fail(st, "tex-budget"), available: true, found: false, data: null}
            else {
                let existing = st.resource_ids[resource_key];
                let id = if (existing != null) existing else next_source_id(st)^;
                let sources = if (existing != null) st.sources else [*st.sources,
                    {id: id, name: cleaned, origin: scope, data: bytes}];
                {state: {*:st, resources_seen: {*:st.resources_seen, *:map([resource_key, true])},
                    resource_ids: {*:st.resource_ids, *:map([resource_key, id])}, sources: sources,
                    resource_count: next_count, resource_bytes: next_bytes},
                    available: true, found: true, data: bytes, id: id, origin: scope}
            }
        }
    }
}

fn push_resource_reader(fetched) any^ {
    let decoded = utf8.decode(fetched.data) ^ { null };
    if (decoded == null) fail(fetched.state, "tex-invalid-utf8")
    else if (len(fetched.state.input.levels) >= 6000) fail(fetched.state, "tex-capacity")
    else {
        let child = {*:reader.reader(decoded, fetched.id), origin: fetched.origin,
            catcodes: fetched.state.input.reader.catcodes, endline: fetched.state.input.reader.endline};
        {*:fetched.state, input: stream.push_reader(fetched.state.input, child)}
    }
}

// streams use the same caller-supplied resource set as input and packages.
fn input_stream_command(st, budget, opening) any^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else {
        let slot = ((scanned.value % 16) + 16) % 16;
        if (not opening) {*:scanned.state, input_stream_state_seen: true,
            input_streams: {*:scanned.state.input_streams, *:map([string(slot), {open: false}])}}
        else {
            let name = scan_filename(optional_equals(scanned.state, budget), budget)^;
            if (name.state.issue != null) name.state
            else {
                let exact = captured_resource(name.state, name.name)^;
                let chosen = if (exact.available and not exact.found and exact.state.issue == null)
                    captured_resource(exact.state, name.name ++ ".tex")^ else exact;
                if (chosen.state.issue != null) chosen.state
                else if (not chosen.available) unsupported(chosen.state, "openin resource provider")
                else {*:chosen.state, input_stream_state_seen: true, input_streams: {*:chosen.state.input_streams,
                    *:map([string(slot), if (chosen.found) {open: true, data: chosen.data, position: 0, id: chosen.id}
                        else {open: false}])}}
            }
        }
    }
}

// read and readline share stream position and physical-line handling.
fn read_stream_line(st, slot) any^ {
    let source = if (slot >= 0 and slot <= 15) st.input_streams[string(slot)] else null;
    if (source.open != true) {
        let diagnosed = record_diagnostic(st, "tex-read-terminal",
            "cannot \\read from the terminal in a document run")^;
        {state: diagnosed, opened: false, eof: true, text: ""}
    }
    else {
        let decoded = utf8.decode(source.data) ^ { null };
        if (decoded == null) {state: unsupported(st, "read invalid UTF-8"), opened: true, eof: true, text: ""}
        else {
            let cursor = if (source.reader != null) source.reader else reader.reader(decoded, source.id);
            let line = reader.load_line(cursor)^;
            let next_stream = if (line == null) {*:source, open: false} else {*:source, reader: line};
            let next = {*:st, input_streams: {*:st.input_streams, *:map([string(slot), next_stream])}};
            {state: next, opened: true, eof: line == null, text: if (line == null) "" else line.line}
        }
    }
}

// the native reader reuses a synthetic source buffer for tokenized reads.
fn read_tokens(st, line, balance) any^ {
    let endline = parameter_value(st, "endlinechar");
    if (endline >= 55296 and endline <= 57343)
        {state: unsupported(st, "read surrogate endline"), balance: balance, tokens: []}
    else read_tokens_valid(st, line, balance)
}
fn read_tokens_valid(st, line, balance) any^ {
    let id = if (st.read_source_id == null) next_source_id(st)^ else st.read_source_id;
    let text = line ++ "\n";
    let sources = if (st.read_source_id == null) [*st.sources, {id: id, name: "\\read", origin: "synthetic", data: text}]
        else [for (source in st.sources) if (source.id == id) {*:source, data: text} else source];
    let current = {*:st, read_source_id: id, sources: sources};
    let input = {*:reader.reader(text, id), catcodes: current.input.reader.catcodes,
        endline: parameter_value(current, "endlinechar")};
    let tokens = read_tokens_inner(input, balance)^;
    {state: current, balance: tokens.balance, tokens: stored_tokens(tokens.tokens)}
}
fn read_tokens_inner(input, balance, tokens = []) any^ {
    let next = reader.next(input)^;
    let token = next.token;
    if (token == null) {balance: balance, tokens: tokens}
    else if (charcat(token, 2) and balance == 0) {balance: 0, tokens: tokens}
    else read_tokens_inner(next.reader, balance + if (charcat(token, 1)) 1 else if (charcat(token, 2)) -1 else 0,
        [*tokens, token])
}
fn read_token_lines(st, slot, balance = 0, tokens = []) any^ {
    let line = read_stream_line(st, slot)^;
    if (line.state.issue != null) {state: line.state, tokens: tokens}
    else if (not line.opened) {state: line.state, tokens: tokens}
    else {
        let scanned = read_tokens(line.state, line.text, balance)^;
        let output = [*tokens, *scanned.tokens];
        if (scanned.state.issue != null) {state: scanned.state, tokens: output}
        else if (line.eof and scanned.balance > 0) {
            let diagnosed = record_diagnostic(scanned.state, "tex-read-runaway", "File ended within \\read")^;
            {state: diagnosed, tokens: output}
        }
        else if (scanned.balance > 0) read_token_lines(scanned.state, slot, scanned.balance, output)
        else {state: scanned.state, tokens: output}
    }
}

fn read_stream_assignment(st, global, budget, readline) any^ {
    let scanned = scan_number(st, budget)^;
    let keyword = if (scanned.state.issue == null) scan_word(scanned.state, "to", budget)^
        else {state: scanned.state, found: false};
    if (keyword.state.issue != null) keyword.state
    else if (not keyword.found) fail(keyword.state, "tex-missing-to")
    else {
        let target = nonspace(keyword.state.input)^;
        let current = {*:keyword.state, input: target.stream, input_stream_state_seen: true};
        if (not is_control(target.token)) fail(current, "tex-missing-cs")
        else if (not readline) {
            let read = read_token_lines(current, scanned.value)^;
            if (read.state.issue != null) read.state
            else assign_meaning(read.state, key(target.token), expl3.token_list(read.tokens), global)
        }
        else {
            let line = read_stream_line(current, scanned.value)^;
            if (line.state.issue != null) line.state
            else if (not line.opened) assign_meaning(line.state, key(target.token), expl3.token_list([]), global)
            else {
                let endline = parameter_value(line.state, "endlinechar");
                if (endline >= 55296 and endline <= 57343) unsupported(line.state, "readline surrogate endline")
                else {
                    let suffix = if (endline < 0 or endline > 1114111) "" else chr(endline);
                    assign_meaning(line.state, key(target.token), expl3.token_list(text_tokens(line.text ++ suffix)^), global)
                }
            }
        }
    }
}

fn input_primitive(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let name = scan_filename(current, budget)^;
        if (name.state.issue != null) name.state
        else {
            let exact = captured_resource(name.state, name.name)^;
            let selected = if (exact.available and not exact.found and exact.state.issue == null)
                captured_resource(exact.state, name.name ++ ".tex")^ else exact;
            if (selected.state.issue != null) selected.state
            else if (not selected.available) unsupported(selected.state, "input")
            else if (not selected.found) {*:selected.state,
                diagnostics: [*selected.state.diagnostics, if (starts_with(name.name, "/") or contains(name.name, ".."))
                    "tex-file-outside" else "tex-file-not-found"]}
            else push_resource_reader(selected)
        }
    }
}

fn endinput_primitive(st, budget) any^ =>
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {*:st, input: stream.end_input(st.input), expansions: st.expansions + 1}

fn pdf_file_primitive(st, name, budget, counted = false) any^ {
    if (not counted and st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = if (counted) st else {*:st, expansions: st.expansions + 1};
        let text = expanded_text(current, budget)^;
        if (text.state.issue != null) text.state
        else {
            let fetched = captured_resource(text.state, token_text(text.state, text.tokens))^;
            if (fetched.state.issue != null) fetched.state
            else if (not fetched.available) unsupported(fetched.state, name)
            else {
                let rendered = if (not fetched.found) ""
                    else if (name == "pdffilesize") string(len(fetched.data))
                    else if (name == "pdffilemoddate") "D:20000101120000Z"
                    else md5.digest(fetched.data)^;
                {*:fetched.state, input: stream.push(fetched.state.input, text_tokens(rendered))}
            }
        }
    }
}

// native uses an unsigned 32-bit linear congruential sequence.
fn random_deviate(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let bound = scan_number({*:st, expansions: st.expansions + 1}, budget)^;
        if (bound.state.issue != null) bound.state
        else {
            // split the product so every intermediate stays within Lambda's exact integer range.
            let prior = bound.state.random_seed;
            let low = (prior % 65536) * 1103515245;
            let high = (((prior div 65536) * 1103515245) % 65536) * 65536;
            let seed = (low + high + 12345) % 4294967296;
            let value = if (bound.value > 0) (seed div 2) % bound.value else 0;
            {*:bound.state, random_seed: seed, pdf_random_seen: true,
                input: stream.push(bound.state.input, text_tokens(string(value))^)}
        }
    }
}

fn set_random_seed(st, budget) any^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else {*:scanned.state, random_seed: ((scanned.value % 4294967296) + 4294967296) % 4294967296,
        pdf_random_seen: true}
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

// the native font name includes a size only when it differs from 10pt.
fn font_name(st, index) any^ {
    let font = st.fonts[index];
    font.name ++ if (index > 0 and font.size != 655360)
        " at " ++ dimension.format(font.size)^ else ""
}

fn font_name_primitive(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let scanned = scan_font_ident({*:st, expansions: st.expansions + 1}, budget)^;
        if (scanned.state.issue != null) scanned.state
        else {*:scanned.state, input: stream.push(scanned.state.input, text_tokens(font_name(scanned.state, scanned.value)^)^),
            font_name_seen: true}
    }
}

// job names derive from the explicit document path, not its display name.
fn job_name(base) any^ {
    if (base == null or base == "") "texput"
    else {
        let name = path.basename(base)^;
        let parts = split(name, ".")^;
        if (len(parts) <= 1) name else join(take(parts, len(parts) - 1)^, ".")^
    }
}

fn print_meaning(st, t) any^ {
    let m = meaning(st, t)^;
    if (m.kind == "undefined") "undefined"
    else if (m.kind == "primitive") escape(st) ++ m.name
    else if (m.kind == "constructor") escape(st) ++ "protected macro:->" ++
        write_token(st, {kind: "control", value: m.name})^
    else if (m.kind == "register") escape(st) ++ "count" ++ string(m.index)
    else if (m.kind == "dimension-register") escape(st) ++ "dimen" ++ string(m.index)
    else if (m.kind == "glue-register") escape(st) ++ (if (m.mu) "muskip" else "skip") ++ string(m.index)
    else if (m.kind == "token-register") escape(st) ++ "toks" ++ string(m.index)
    else if (m.kind == "font") "select font " ++ font_name(st, m.index)^
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

// terminal output is buffered until a line break or a flushing TeX command.
fn terminal_flush(st) any^ => if (st.terminal_line == "") st
    else {*:st, messages: [*st.messages, st.terminal_line], terminal_line: ""}
fn terminal_print(st, text) any^ {
    let newline = parameter_value(st, "newlinechar")^;
    if (newline < 0 or newline >= 128) {*:st, terminal_line: st.terminal_line ++ text}
    else {
        let parts = split(text, chr(newline))^;
        if (len(parts) <= 1) {*:st, terminal_line: st.terminal_line ++ text}
        else {*:st, messages: [*st.messages, st.terminal_line ++ parts[0], *slice(parts, 1, len(parts) - 1)^],
            terminal_line: parts[len(parts) - 1]}
    }
}
// preserve native diagnostic locations when the active source is known.
fn record_diagnostic(st, code, message) any^ {
    let active = stream.active_reader(st.input)^;
    let found = if (active == null) [] else [for (source in st.sources where source.id == active.file_id) source];
    let main = active != null and active.file_id == st.main_file;
    let foreign = active != null and not main and len(found) > 0;
    let bytes = binary(message)^;
    // native diagnostic formatting truncates long or NUL-containing messages.
    let exact_message = len(bytes) <= 511 and not any([for (byte in bytes) byte == 0]);
    let exhausted = active == null and st.input.root_eof == true;
    let known = ((active != null and (main or foreign)) or exhausted) and exact_message;
    let record = {code: code, message: message, offset: st.input.last_main_offset,
        *:if (foreign) {file: found[0].name} else {},
        *:if (active != null and active.line_no > 0) {line: active.line_no} else {}};
    {*:st, diagnostics: [*st.diagnostics, code],
        diagnostic_records: if (known) [*st.diagnostic_records, record] else st.diagnostic_records}
}

// native scans numeric math codes, reports the limitation and continues.
fn numeric_math(st, budget) any^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else record_diagnostic({*:scanned.state, numeric_math_seen: true},
        "tex-unsupported-mathchar", "numeric math characters are not reproduced")
}

fn terminal_message(st, budget, is_error) any^ {
    let text = expanded_text(st, budget)^;
    if (text.state.issue != null) text.state
    else {
        let message_text = token_text(text.state, text.tokens)^;
        if (is_error) {
            let diagnosed = record_diagnostic(text.state, "tex-errmessage", message_text)^;
            terminal_flush(terminal_print(terminal_flush(diagnosed)^, "! " ++ message_text ++ ".")^)
        }
        else terminal_print(text.state, (if (text.state.terminal_line == "") "" else " ") ++ message_text)
    }
}
// native echoes valid direct source spans for scanned commands.
fn emit_traced_span(closed, name) any^ {
    let raw = closed.span.tokens;
    if (raw == null or len(raw) == 0) unsupported(closed.state, name)
    else {*:closed.state, output: [*closed.state.output,
        {*:raw[0], echo_span: closed.span, echo_count: len(raw)}, *slice(raw, 1, len(raw))^]}
}
fn emit_spacing(st, original, command, budget) any^ {
    let traced = begin_main_trace(st, original, true)^;
    let mu = command == "mskip" or command == "mkern";
    let is_glue = command == "hskip" or command == "vskip" or command == "mskip";
    let scanned = if (is_glue) scan_glue(traced, budget, mu)^ else scan_dimen(traced, budget, false, mu)^;
    let closed = end_main_trace(scanned.state, original)^;
    if (closed.state.issue != null) closed.state
    else if (closed.echo) emit_traced_span(closed, "spacing echo trace")
    else {
        let name = if (command == "mskip") "mskip" else if (command == "mkern") "mkern"
            else if (command == "vskip") "vspace" else if (closed.state.math_depth > 0) command else "hspace";
        let value = if (is_glue) glue.format(scanned.value, mu)^
            else dimension.format(scanned.value, if (mu) "mu" else "pt")^;
        let tokens = [control_token(name), brace_token(true), *text_tokens(value), brace_token(false)];
        {*:closed.state, output: [*closed.state.output,
            *[for (token in tokens) {*:token, emit_origin: closed.state.input.last_main_offset}]]}
    }
}

fn scan_rule(st, budget, width, height, depth) any^ {
    let w = scan_word(st, "width", budget)^;
    let h = if (w.found) {state: w.state, found: false} else scan_word(w.state, "height", budget)^;
    let d = if (w.found or h.found) {state: h.state, found: false} else scan_word(h.state, "depth", budget)^;
    if (d.state.issue != null) {state: d.state, width: width, height: height, depth: depth}
    else if (w.found or h.found or d.found) {
        let value = scan_dimen(d.state, budget)^;
        if (value.state.issue != null) {state: value.state, width: width, height: height, depth: depth}
        else scan_rule(value.state, budget, if (w.found) value.value else width,
            if (h.found) value.value else height, if (d.found) value.value else depth)
    }
    else {state: d.state, width: width, height: height, depth: depth}
}
// all box consumers scan past the same expanded spaces and relax tokens.
fn scan_box_command(st, budget) any^ {
    let next = expanded_nonrelax(st, budget)^;
    {state: next.state, token: next.token, command: primitive(next.state, next.token)}
}

// the native parser discards leader contents and then processes the following glue.
fn discard_leader(st, budget) any^ {
    let next = scan_box_command(st, budget)^;
    let command = next.command;
    if (next.state.issue != null) next.state
    else if (command == "hrule" or command == "vrule") {
        let vertical = command == "vrule";
        scan_rule(next.state, budget, if (vertical) 26214 else -1,
            if (vertical) -1 else 26214, if (vertical) -1 else 0).state
    }
    else if (contains(["hbox", "vbox", "vtop"], command)) begin_discard_box(next.state, budget, command)
    else unsupported(put_back(next.state, next.token), "leader box")
}
fn emit_rule(st, original, command, budget) any^ {
    let traced = begin_main_trace(st, original, true)^;
    let vertical = command == "vrule";
    let rule = scan_rule(traced, budget, if (vertical) 26214 else -1,
        if (vertical) -1 else 26214, if (vertical) -1 else 0)^;
    let closed = end_main_trace(rule.state, original)^;
    if (closed.state.issue != null) closed.state
    else if (closed.echo) emit_traced_span(closed, "rule echo trace")
    else {
        let width = if (rule.width < 0) [control_token("linewidth")]
            else text_tokens(dimension.format(rule.width, "pt")^)^;
        let height = if (rule.height < 0) text_tokens("1em")^
            else text_tokens(dimension.format(rule.height + if (rule.depth > 0) rule.depth else 0, "pt")^)^;
        let tokens = [control_token("rule"), brace_token(true), *width, brace_token(false),
            brace_token(true), *height, brace_token(false)];
        {*:closed.state, output: [*closed.state.output,
            *[for (token in tokens) {*:token, emit_origin: closed.state.input.last_main_offset}]]}
    }
}

fn scan_character_number(st, budget) any^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned
    else {
        let invalid = scanned.value < 0 or scanned.value > 1114111;
        let code = if (invalid) 0 else scanned.value;
        let current = if (invalid) record_diagnostic(scanned.state, "tex-bad-char",
            "Bad character code (" ++ string(scanned.value) ++ ")")^ else scanned.state;
        {state: current, value: code}
    }
}
fn emit_character(st, budget) any^ {
    let scanned = scan_character_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    // native TeX can retain surrogate tokens that Lambda strings cannot represent.
    else if (scanned.value >= 55296 and scanned.value <= 57343) unsupported(scanned.state, "char surrogate")
    else {
        let code = scanned.value;
        let category = if ((code >= 65 and code <= 90) or (code >= 97 and code <= 122)) 11 else 12;
        let token = {kind: "char", value: chr(code), cat: category, start: -1, end: -1,
            read_offset: scanned.state.input.last_main_offset, read_file: scanned.state.main_file};
        {*:scanned.state, output: [*scanned.state.output, token]}
    }
}
fn emit_accent(st, budget) any^ {
    let accent = scan_character_number(st, budget)^;
    if (accent.state.issue != null) accent.state
    else {
        let read = expanded_next(accent.state, budget)^;
        let t = read.token;
        let m = if (is_control(t)) meaning(read.state, t) else null;
        let character = if (t != null and t.kind == "char" and contains([11, 12], t.cat))
                {state: read.state, value: ord(t.value)}
            else if (m.kind == "chardef") {state: read.state, value: m.value}
            else if (primitive(read.state, t) == "char") scan_character_number(read.state, budget)^
            else {state: put_back(read.state, t), value: null};
        if (character.state.issue != null) character.state
        else if (character.value != null and character.value >= 55296 and character.value <= 57343)
            unsupported(character.state, "accent surrogate")
        else {
            let name = map(["18", "`", "19", "'", "20", "v", "21", "u", "22", "=", "23", "r",
                "24", "c", "94", "^", "95", ".", "125", "H", "126", "~", "127", "\""])[string(accent.value)];
            let raw_character = if (character.value == null) "" else chr(character.value);
            let body = if (character.value == null) [] else text_tokens(raw_character)^;
            let tokens = if (name == null) body else [control_token(name), brace_token(true), *body, brace_token(false)];
            let raw = if (name == null) raw_character else "\\" ++ name ++ "{" ++ raw_character ++ "}";
            {*:character.state, output: [*character.state.output,
                *[for (i, token in tokens) {*:token, emit_origin: character.state.input.last_main_offset,
                    *:if (i == 0) {reconstruct_text: raw, reconstruct_count: len(tokens)} else {}}]]}
        }
    }
}

fn write_message(st, budget) any^ {
    let destination = scan_number(st, budget)^;
    if (destination.state.issue != null) destination.state
    else {
        let text = expanded_text(destination.state, budget)^;
        if (text.state.issue != null) text.state
        else if (destination.value >= 0 and destination.value <= 15 and contains(text.state.output_streams, destination.value)) text.state
        else terminal_flush(terminal_print(terminal_flush(text.state)^, token_text(text.state, text.tokens)^)^)
    }
}

// native input models open streams but does not retain written files.
fn output_stream(st, budget, opening) any^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else {
        let slot = ((scanned.value % 16) + 16) % 16;
        let filename = if (opening) scan_filename(optional_equals(scanned.state, budget), budget)^
            else {state: scanned.state};
        if (filename.state.issue != null) filename.state
        else {*:filename.state, output_streams: [*[for (item in filename.state.output_streams where item != slot) item],
            *(if (opening) [slot] else [])]}
    }
}

fn show_line(st, text) any^ =>
    terminal_flush(terminal_print(terminal_flush(st)^, "> " ++ text ++ ".")^)

fn show_meaning(st) any^ {
    let read = stream.next(st.input)^;
    let current = {*:st, input: read.stream, inspection_seen: true};
    if (read.token == null) current
    else {
        let control_name = if (read.token.value == "") escape(current) ++ "csname" ++ escape(current) ++ "endcsname"
            else escape(current) ++ read.token.value;
        let prefix = if (read.token.kind == "control") control_name ++ "="
            else if (read.token.kind == "active") read.token.value ++ "=" else "";
        show_line(current, prefix ++ print_meaning(current, read.token)^)
    }
}

fn show_the(st, budget) any^ {
    let result = expand_the({*:st, input: {*:st.input, captured_the: null}}, budget, false, true)^;
    if (result.issue != null) result
    else {
        let tokens = result.input.captured_the;
        let current = {*:result, input: {*:result.input, captured_the: null}, inspection_seen: true};
        show_line(current, token_text(current, tokens)^)
    }
}

fn show_box(st, budget) any^ {
    let index = scan_register(st, budget)^;
    if (index.state.issue != null) index.state
    else {
        let current = {*:index.state, inspection_seen: true};
        let suffix = if (current.boxes[string(index.value)] == null) "=void" else "=box";
        show_line(current, "\\box" ++ string(index.value) ++ suffix)
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
        else show_line(current, token_text(current, body.tokens)^)
    }
}

fn immediate(st, budget) any^ {
    let read = expanded_next(st, budget);
    if (read.state.issue != null) read.state
    else if (primitive(read.state, read.token) == "write") write_message(read.state, budget)
    else if (primitive(read.state, read.token) == "openout") output_stream(read.state, budget, true)
    else if (primitive(read.state, read.token) == "closeout") output_stream(read.state, budget, false)
    else put_back(read.state, read.token)
}

fn set_codes(input, codes) any^ => stream.configure(input, codes, input.reader.endline)
fn set_code(codes, code, value) => {*:codes, *:map([chr(code), value])}

fn save_parameter(st, global, name, value) any^ => {*:st,
    input: if (name == "endlinechar") stream.configure(st.input, st.input.reader.catcodes, value) else st.input,
    parameters: {*:st.parameters, *:map([name, value])},
    groups: if (global) [for (scope in st.groups)
        {*:scope, parameters: {*:scope.parameters, *:map([name, value])}}] else st.groups}

fn assign_parameter(st, global, budget, name) any^ {
    let value = scan_number(optional_equals(st, budget), budget)^;
    if (value.state.issue != null) value.state else save_parameter(value.state, global, name, value.value)
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

// Syntax settings and their saved values follow ordinary TeX group scope.
fn expl3_syntax(st, on) any^ {
    let saved = st.expl_syntax;
    if (on) {
        let previous = if (saved != null and saved.active == true) saved else {
            active: true, codes: expl3.save_codes(st.input.reader.catcodes),
            endline: st.parameters.endlinechar};
        let global = effective_global(st, false)^;
        let configured = {*:st, expl_syntax: previous,
            input: set_codes(st.input, expl3.syntax_codes(st.input.reader.catcodes)),
            groups: if (global) [for (scope in st.groups)
                {*:scope, expl_syntax: previous, catcodes: expl3.syntax_codes(scope.catcodes)}] else st.groups};
        save_parameter(configured, global, "endlinechar", 32)
    }
    else if (saved == null or saved.active != true) st
    else {
        let global = effective_global(st, false)^;
        let configured = {*:st, expl_syntax: null,
            input: set_codes(st.input, {*:st.input.reader.catcodes, *:saved.codes}),
            groups: if (global) [for (scope in st.groups)
                {*:scope, expl_syntax: null, catcodes: {*:scope.catcodes, *:saved.codes}}] else st.groups};
        save_parameter(configured, global, "endlinechar", saved.endline)
    }
}

fn expl3_cs_define(st, name, budget) any^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (expl3.fresh(name) and not latex_definable(meaning(st, target.token)))
        fail(current, "expl3-already-defined")
    else define({*:st, input: stream.push(target.stream, [target.token])},
        effective_global(st, expl3.global(name))^, true, expl3.protected(name), false, budget)
}

fn expl3_tl_assign(st, name, budget) any^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (expl3.fresh(name) and not latex_definable(meaning(st, target.token)))
        fail(current, "expl3-already-defined")
    else {
        let scanned = if (expl3.no_argument(name)) {state: current, tokens: []}
            else if (expl3.expanded_argument(name)) expanded_text(current, budget)^
            else {
                let argument = stream.argument(current.input, true)^;
                {state: if (argument.error != null) fail({*:current, input: argument.stream}, argument.error)
                    else {*:current, input: argument.stream}, tokens: argument.tokens}
            };
        if (scanned.state.issue != null) scanned.state
        else {
            let prior = meaning(scanned.state, target.token);
            if (expl3.token_put(name) and prior.kind == "undefined") fail(scanned.state, "expl3-undefined-variable")
            else if (expl3.token_put(name) and (prior.kind != "macro" or prior.count != 0))
                unsupported(scanned.state, "expl3 token-list target")
            else {
                let before = if (expl3.token_put(name)) stored_tokens(prior.body) else [];
                let added = stored_tokens(scanned.tokens);
                let body = if (not expl3.token_put(name)) added
                    else if (expl3.token_put_left(name)) [*added, *before] else [*before, *added];
                let marked = if (expl3.token_put(name)) {*:scanned.state, expl3_tl_put_seen: true} else scanned.state;
                assign_meaning(marked, key(target.token), expl3.token_list(body),
                    effective_global(marked, expl3.global(name))^)
            }
        }
    }
}

fn expl3_tl_use(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let target = nonspace(st.input)^;
        let current = {*:st, input: target.stream, expansions: st.expansions + 1};
        if (not is_control(target.token)) fail(current, "tex-missing-cs")
        else if (meaning(st, target.token).kind == "undefined") fail(current, "expl3-undefined-variable")
        else {*:current, input: stream.push(current.input, [target.token])}
    }
}

fn expl3_tl_empty(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let named = contains(["tl_if_empty:NTF", "tl_if_empty:NT", "tl_if_empty:NF"], name);
        let variable = contains(["tl_if_blank:VTF", "tl_if_blank:VT", "tl_if_blank:VF"], name);
        let target = if (named or variable) nonspace(st.input)^ else null;
        let argument = if (named or variable) {stream: target.stream, tokens: [], error: null} else stream.argument(st.input, true)^;
        let yes = if (name == "tl_if_empty:NF" or name == "tl_if_empty:nF" or name == "tl_if_blank:nF" or name == "tl_if_blank:VF")
            {stream: argument.stream, tokens: [], error: null} else stream.argument(argument.stream, true)^;
        let no = if (name == "tl_if_empty:NT" or name == "tl_if_empty:nT" or name == "tl_if_blank:nT" or name == "tl_if_blank:VT")
            {stream: yes.stream, tokens: [], error: null} else stream.argument(yes.stream, true)^;
        let current = {*:st, input: stream.note_foreign_expansion(no.stream)^,
            expansions: st.expansions + 1, expl3_tl_empty_seen: true};
        let value = if ((named or variable) and target.token != null) meaning(current, target.token) else null;
        if (argument.error != null) fail(current, argument.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else if ((named or variable) and target.token == null) fail(current, "tex-runaway")
        else if (variable and not is_control(target.token)) fail(current, "tex-missing-cs")
        else if (variable and value.kind == "undefined") fail(current, "expl3-undefined-variable")
        else if (variable and (value.kind != "macro" or value.count != 0)) unsupported(current, "expl3 token-list target")
        else {
            let empty = if (named) meanings_equal(value, expl3.token_list([]))^
                else if (variable) all([for (token in (value.body^)) charcat(token, 10)])
                else if (expl3.token_blank(name)) all([for (token in argument.tokens) charcat(token, 10)])
                else len(argument.tokens) == 0;
            {*:current, input: stream.push(current.input, if (empty) yes.tokens else no.tokens)}
        }
    }
}

// balanced groups count as one item, while top-level spaces do not count.
fn expl3_count_items(tokens, index = 0, depth = 0, count = 0) any^ {
    if (index >= len(tokens)) count
    else {
        let token = tokens[index];
        let next_depth = depth + if (charcat(token, 1)) 1 else if (charcat(token, 2)) -1 else 0;
        let next_count = count + if (depth == 0 and not charcat(token, 10)) 1 else 0;
        expl3_count_items(tokens, index + 1, next_depth, next_count)
    }
}

fn expl3_tl_count(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let variable = name == "tl_count:N";
        let target = if (variable) nonspace(st.input)^ else null;
        let arg = if (variable) {stream: target.stream, tokens: [], error: null} else stream.argument(st.input, true)^;
        let current = {*:st, input: stream.note_foreign_expansion(arg.stream)^,
            expansions: st.expansions + 1, expl3_tl_empty_seen: true};
        let value = if (variable and target.token != null) meaning(current, target.token) else null;
        if (arg.error != null) fail(current, arg.error)
        else if (variable and not is_control(target.token)) fail(current, "tex-missing-cs")
        else if (variable and value.kind == "undefined") fail(current, "expl3-undefined-variable")
        else if (variable and (value.kind != "macro" or value.count != 0)) unsupported(current, "expl3 token-list target")
        else {
            let tokens = if (variable) value.body else arg.tokens;
            let count = expl3_count_items(tokens)^;
            {*:current, input: stream.push(current.input, text_tokens(string(count))^)}
        }
    }
}

fn expl3_tl_string(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let variable = name == "tl_to_str:V";
        let target = if (variable) nonspace(st.input)^ else null;
        let arg = if (variable) {stream: target.stream, tokens: [], error: null} else stream.argument(st.input, true)^;
        // variable expansion has a macro origin even if its value is empty.
        let current = {*:st, input: if (variable) stream.note_foreign_expansion(arg.stream)^ else arg.stream,
            expansions: st.expansions + 1, expl3_tl_empty_seen: true};
        let value = if (variable and target.token != null) meaning(current, target.token) else null;
        if (arg.error != null) fail(current, arg.error)
        else if (variable and not is_control(target.token)) fail(current, "tex-missing-cs")
        else if (variable and value.kind == "undefined") fail(current, "expl3-undefined-variable")
        else if (variable and (value.kind != "macro" or value.count != 0)) unsupported(current, "expl3 token-list target")
        else {
            let tokens = if (variable) value.body else arg.tokens;
            let generated = text_tokens(token_text(current, tokens)^)^;
            // direct detokenization has already traced its source tokens.
            let output = if (not variable and current.token_text_scan) [for (token in generated) {*:token, trace_internal: true}]
                else generated;
            {*:current, input: stream.push(current.input, output)}
        }
    }
}

// named comparisons use TeX meanings; n comparisons retain token identity.
fn expl3_tl_equal(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let named = contains(["tl_if_eq:NNTF", "tl_if_eq:NNT", "tl_if_eq:NNF"], name);
        let first = if (named) nonspace(st.input)^ else stream.argument(st.input, true)^;
        let second = if (named) nonspace(first.stream)^ else stream.argument(first.stream, true)^;
        let yes = if (name == "tl_if_eq:NNF" or name == "tl_if_eq:nnF")
            {stream: second.stream, tokens: [], error: null} else stream.argument(second.stream, true)^;
        let no = if (name == "tl_if_eq:NNT" or name == "tl_if_eq:nnT")
            {stream: yes.stream, tokens: [], error: null} else stream.argument(yes.stream, true)^;
        let current = {*:st, input: if (named) stream.note_foreign_expansion(no.stream)^ else no.stream,
            expansions: st.expansions + 1, expl3_tl_empty_seen: true};
        if (not named and first.error != null) fail(current, first.error)
        else if (not named and second.error != null) fail(current, second.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else if (named and (first.token == null or second.token == null)) fail(current, "tex-runaway")
        else {
            let equal = if (named) meanings_equal(meaning(current, first.token), meaning(current, second.token))^
                else tokens_equal(first.tokens, second.tokens)^;
            {*:current, input: stream.push(current.input, if (equal) yes.tokens else no.tokens)}
        }
    }
}

// Sequence meanings retain their TeX token representation as well as their items.
fn expl3_seq_body(items, index = 0, output = [control_token("s__seq")]) any^ {
    if (index >= len(items)) output
    else {
        let opener = brace_token(true);
        let closer = brace_token(false);
        expl3_seq_body(items, index + 1,
            [*output, control_token("__seq_item:n"), opener, *items[index], closer])
    }
}

fn expl3_seq_value(items) any^ => {*:expl3.token_list(expl3_seq_body(items)^), seq_items: items}

fn expl3_seq_assign(st, name) any^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (expl3.fresh(name) and not latex_definable(meaning(st, target.token)))
        fail(current, "expl3-already-defined")
    else {
        let arg = if (expl3.sequence_empty_assignment(name)) {stream: target.stream, tokens: [], error: null}
            else stream.argument(target.stream, true)^;
        let advanced = {*:current, input: arg.stream};
        let old = meaning(advanced, target.token);
        if (arg.error != null) fail(advanced, arg.error)
        else if (not expl3.sequence_empty_assignment(name) and not expl3.sequence_from_clist(name) and
                not (old.seq_items is array)) fail(advanced, "expl3-invalid-sequence")
        else {
            let items = if (expl3.sequence_empty_assignment(name)) []
                else if (expl3.sequence_from_clist(name)) expl3.from_clist(arg.tokens)^
                else if (expl3.sequence_put_left(name)) [arg.tokens, *old.seq_items]
                else [*old.seq_items, arg.tokens];
            assign_meaning(advanced, key(target.token), expl3_seq_value(items)^,
                effective_global(advanced, expl3.global(name))^)
        }
    }
}

fn expl3_seq_join(items, separator, index = 0, output = []) any^ {
    if (index >= len(items)) output
    else expl3_seq_join(items, separator, index + 1,
        [*output, *(if (index == 0) [] else separator), *items[index]])
}

fn expl3_seq_expand(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let target = nonspace(st.input)^;
        let current = {*:st, input: target.stream, expansions: st.expansions + 1};
        if (not is_control(target.token)) fail(current, "tex-missing-cs")
        else {
            let value = meaning(current, target.token);
            if (not (value.seq_items is array)) fail(current, "expl3-invalid-sequence")
            else if (name == "seq_count:N")
                {*:current, input: stream.push(current.input, text_tokens(string(len(value.seq_items))))}
            else {
                let separator = stream.argument(current.input, true)^;
                let advanced = {*:current, input: separator.stream};
                if (separator.error != null) fail(advanced, separator.error)
                else {
                    let tokens = expl3_seq_join(value.seq_items, separator.tokens)^;
                    let result = if (advanced.token_text_scan) [for (t in tokens)
                        {*:t, noexpand: is_control(t), direct_toks: true}] else stored_tokens(tokens);
                    {*:advanced, input: stream.push(advanced.input, result)}
                }
            }
        }
    }
}

// The n arguments compare token strings without expanding their control sequences.
fn expl3_compare_text(st, tokens) any^ => join([for (t in tokens)
    if (charcat(t, 6)) t.value else write_token(st, t)], "")

fn expl3_str_compare(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let left = stream.argument(st.input, true)^;
        let relation = nonspace(left.stream)^;
        let right = stream.argument(relation.stream, true)^;
        let yes = if (name == "str_compare:nNnF") {stream: right.stream, tokens: [], error: null}
            else stream.argument(right.stream, true)^;
        let no = if (name == "str_compare:nNnT") {stream: yes.stream, tokens: [], error: null}
            else stream.argument(yes.stream, true)^;
        let current = {*:st, input: no.stream, expansions: st.expansions + 1};
        if (left.error != null) fail(current, left.error)
        else if (right.error != null) fail(current, right.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else if (not (other(relation.token, "<") or other(relation.token, "=") or other(relation.token, ">")))
            fail(current, "tex-missing-relation")
        else {
            let a = expl3_compare_text(current, left.tokens)^;
            let b = expl3_compare_text(current, right.tokens)^;
            let matched = if (relation.token.value == "<") a < b
                else if (relation.token.value == ">") a > b else a == b;
            {*:current, input: stream.push(current.input, if (matched) yes.tokens else no.tokens)}
        }
    }
}

// Execute the supplied TeX comparator with the same grouped state and token stream.
fn expl3_sort_run(st, budget) any^ {
    if (st.issue != null or st.expl_sort_done == true) st
    else if (st.finished == true) fail(st, "expl3-sort-incomplete")
    else expl3_sort_run(step(st, budget), budget)
}

fn expl3_sort_compare(st, comparator, a, b, budget) any^ {
    if (st.expansions >= budget) {state: fail(st, "tex-budget"), swap: false}
    else {
        let boundary = {kind: "expl3-sort-end"};
        let input = stream.push(st.input, [*stored_tokens(substitute(comparator, [a, b])), boundary]);
        let running = {*:st, input: input, expl_sort_active: true, expl_sort_result: null,
            expl_sort_done: false, expansions: st.expansions + 1};
        let checked = expl3_sort_run(running, budget)^;
        let result = checked.expl_sort_result;
        let restored = {*:checked, expl_sort_active: st.expl_sort_active,
            expl_sort_result: st.expl_sort_result, expl_sort_done: st.expl_sort_done};
        if (restored.issue != null) {state: restored, swap: false}
        else if (result == null) {state: fail(restored, "expl3-sort-no-return"), swap: false}
        else {state: restored, swap: result == "swap"}
    }
}

// Merge adjacent runs from their ends, preserving TeX's comparator order.
fn expl3_sort_merge(st, comparator, left, right, budget, i, j, suffix = []) any^ {
    if (i < 0) {state: st, items: [*slice(right, 0, j + 1)^, *suffix]}
    else if (j < 0) {state: st, items: [*slice(left, 0, i + 1)^, *suffix]}
    else {
        let compared = expl3_sort_compare(st, comparator, left[i], right[j], budget)^;
        if (compared.state.issue != null) {state: compared.state, items: suffix}
        else if (compared.swap)
            expl3_sort_merge(compared.state, comparator, left, right, budget, i - 1, j, [left[i], *suffix])
        else expl3_sort_merge(compared.state, comparator, left, right, budget, i, j - 1, [right[j], *suffix])
    }
}

fn expl3_sort_pass(st, comparator, items, budget, width, index = 0, output = []) any^ {
    if (index >= len(items)) {state: st, items: output}
    else {
        let middle = min(index + width, len(items));
        let end = min(index + 2 * width, len(items));
        let left = slice(items, index, middle)^;
        let right = slice(items, middle, end)^;
        let merged = expl3_sort_merge(st, comparator, left, right, budget,
            len(left) - 1, len(right) - 1)^;
        if (merged.state.issue != null) merged
        else expl3_sort_pass(merged.state, comparator, items, budget, width, end, [*output, *merged.items])
    }
}

fn expl3_sort_items(st, comparator, items, budget, width = 1) any^ {
    if (width >= len(items)) {state: st, items: items}
    else {
        let pass = expl3_sort_pass(st, comparator, items, budget, width)^;
        if (pass.state.issue != null) pass
        else expl3_sort_items(pass.state, comparator, pass.items, budget, width * 2)
    }
}

fn expl3_seq_sort(st, name, budget) any^ {
    let target = nonspace(st.input)^;
    let code = stream.argument(target.stream, true)^;
    let current = {*:st, input: code.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (code.error != null) fail(current, code.error)
    else {
        let value = meaning(current, target.token);
        let comparator = body_params(code.tokens, 2)^;
        if (not (value.seq_items is array)) fail(current, "expl3-invalid-sequence")
        else if (comparator.issue != null) fail(current, comparator.issue)
        else {
            let grouped = begin_group(current, null, true)^;
            let sorted = expl3_sort_items(grouped, comparator.tokens, value.seq_items, budget)^;
            if (sorted.state.issue != null) sorted.state
            else {
                let ended = end_group(sorted.state, null, true)^;
                if (ended.issue != null) ended
                else assign_meaning(ended, key(target.token), expl3_seq_value(sorted.items)^,
                    effective_global(ended, expl3.global(name))^)
            }
        }
    }
}

// Case conversion expands ordinary macros but leaves math and reference keys intact.
fn expl3_case_scan(st, budget, uppercase, mode = null, output = []) any^ {
    let read = if (mode == null) expanded_next(st, budget, true)^
        else {let raw = stream.next(st.input)^; {state: {*:st, input: raw.stream}, token: raw.token}};
    let t = read.token;
    let current = read.state;
    if (current.issue != null) {state: current, tokens: output}
    else if (t == null) {state: fail(current, "expl3-case-incomplete"), tokens: output}
    else if (t.kind == "expl3-case-end")
        if (mode != null) {state: fail(current, "expl3-case-incomplete-math"), tokens: output}
        else {state: current, tokens: output}
    else if (mode == "dollar")
        expl3_case_scan(current, budget, uppercase, if (charcat(t, 3)) null else mode, [*output, t])
    else if (mode == "paren" or mode == "bracket")
        expl3_case_scan(current, budget, uppercase,
            if (is_control(t) and t.value == (if (mode == "paren") ")" else "]")) null else mode, [*output, t])
    else if (charcat(t, 3)) expl3_case_scan(current, budget, uppercase, "dollar", [*output, t])
    else if (is_control(t) and (t.value == "(" or t.value == "["))
        expl3_case_scan(current, budget, uppercase, if (t.value == "(") "paren" else "bracket", [*output, t])
    else if (is_control(t) and expl3.case_opaque(t.value)) {
        let arg = stream.raw_argument(current.input)^;
        let advanced = {*:current, input: arg.stream};
        if (arg.error != null or any([for (token in arg.raw) token.kind == "expl3-case-end"]))
            {state: fail(advanced, "expl3-case-missing-argument"), tokens: output}
        else if (len(arg.raw) > 0 and arg.raw[0].kind == "char" and
                (arg.raw[0].value == "[" or arg.raw[0].value == "*"))
            {state: unsupported(advanced, "text case reference options"), tokens: output}
        else expl3_case_scan(advanced, budget, uppercase, null, [*output, t, *arg.raw])
    }
    else if (is_control(t)) {
        let resolved = meaning(current, t);
        if (resolved.kind == "undefined") {state: unsupported(current, "text case control " ++ t.value), tokens: output}
        else if (resolved.kind == "char" or resolved.kind == "chardef" or resolved.kind == "mathchardef")
            {state: unsupported(current, "text case character meaning"), tokens: output}
        else expl3_case_scan(current, budget, uppercase, null, [*output, t])
    }
    else if (t.kind == "char" and ord(t.value) >= 128)
        {state: unsupported(current, "Unicode text case"), tokens: output}
    else if (t.kind == "char")
        expl3_case_scan(current, budget, uppercase, null,
            [*output, {*:t, value: if (uppercase) upper(t.value) else lower(t.value), direct: false}])
    else {state: unsupported(current, "text case token"), tokens: output}
}

fn expl3_text_case(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let arg = stream.argument(st.input, true)^;
        let current = {*:st, input: arg.stream, expansions: st.expansions + 1};
        if (arg.error != null) fail(current, arg.error)
        else {
            let boundary = {kind: "expl3-case-end"};
            let queued = {*:current, input: stream.push(current.input, [*arg.tokens, boundary])};
            let converted = expl3_case_scan(queued, budget, name == "text_uppercase:n")^;
            if (converted.state.issue != null) converted.state
            else {
                let tokens = if (converted.state.token_text_scan) [for (t in converted.tokens)
                    {*:t, noexpand: is_control(t), direct_toks: true}] else stored_tokens(converted.tokens);
                {*:converted.state, input: stream.push(converted.state.input, tokens)}
            }
        }
    }
}

// expand the TeX argument as tokens before evaluating the bounded decimal grammar.
fn expl3_fp_eval(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let arg = expanded_text({*:st, expansions: st.expansions + 1}, budget)^;
        if (arg.state.issue != null) arg.state
        else if (len([for (t in arg.tokens where t.kind != "char") t]) > 0)
            unsupported(arg.state, "fp expression token")
        else {
            let expression = join([for (t in arg.tokens) t.value], "");
            let result = fp.evaluate(expression)^;
            if (result.issue == "expl3-fp-unsupported") unsupported(arg.state, "fp " ++ result.unsupported)
            else if (result.issue != null) fail(arg.state, result.issue)
            else {*:arg.state, input: stream.push(arg.state.input, text_tokens(result.value))}
        }
    }
}

// expl3 integer expressions follow TeX number-expression arithmetic and rounding.
fn expl3_int_tokens(st, tokens, budget) any^ {
    let scoped = {*:st, input: stream.push(st.input, [*tokens, control_token("relax")])};
    scan_expression(scoped, budget)
}

fn expl3_int_argument(st, budget) any^ {
    let arg = stream.argument(st.input, true)^;
    let current = {*:st, input: arg.stream};
    if (arg.error != null) {state: fail(current, arg.error), value: 0}
    else expl3_int_tokens(current, arg.tokens, budget)
}

fn expl3_int_eval(st, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let result = expl3_int_argument({*:st, expansions: st.expansions + 1}, budget)^;
        if (result.state.issue != null) result.state
        else {*:result.state, input: stream.push(result.state.input, text_tokens(string(result.value)))}
    }
}

fn expl3_int_math(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let first = expl3_int_argument({*:st, expansions: st.expansions + 1, expl3_int_math_seen: true}, budget)^;
        if (first.state.issue != null) first.state
        else if (name == "int_abs:n")
            {*:first.state, input: stream.push(first.state.input, text_tokens(string(abs(first.value))))}
        else {
            let second = expl3_int_argument(first.state, budget)^;
            if (second.state.issue != null) second.state
            else {
                let value = if (name == "int_max:nn") max(first.value, second.value) else min(first.value, second.value);
                {*:second.state, input: stream.push(second.state.input, text_tokens(string(value)))}
            }
        }
    }
}

fn expl3_int_branches(input, has_yes, has_no) any^ {
    let yes = if (has_yes) stream.argument(input, true)^ else {stream: input, tokens: [], error: null};
    let no = if (has_no) stream.argument(yes.stream, true)^ else {stream: yes.stream, tokens: [], error: null};
    {yes: yes, no: no}
}

fn expl3_int_compare(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let left = stream.argument(st.input, true)^;
        let relation = nonspace(left.stream)^;
        let right = stream.argument(relation.stream, true)^;
        let branches = expl3_int_branches(right.stream, name != "int_compare:nNnF", name != "int_compare:nNnT")^;
        let yes = branches.yes;
        let no = branches.no;
        let current = {*:st, input: stream.note_foreign_expansion(no.stream)^,
            expansions: st.expansions + 1, expl3_int_math_seen: true};
        if (left.error != null) fail(current, left.error)
        else if (right.error != null) fail(current, right.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else {
            let a = expl3_int_tokens(current, left.tokens, budget)^;
            if (a.state.issue != null) a.state
            else {
                let op = expl3_int_relation(a.state, relation.token, budget)^;
                if (op.state.issue != null) op.state
                else {
                    let b = expl3_int_tokens(op.state, right.tokens, budget)^;
                    if (b.state.issue != null) b.state
                    else {
                        let selected = if (op.value == "<") a.value < b.value
                            else if (op.value == ">") a.value > b.value else a.value == b.value;
                        {*:b.state, input: stream.push(b.state.input, if (selected) yes.tokens else no.tokens)}
                    }
                }
            }
        }
    }
}

// relation macros may expand to one comparison token but cannot take arguments.
fn expl3_int_relation(st, token, budget) any^ {
    let m = if (is_control(token)) meaning(st, token)^ else null;
    if (token == null or (m != null and m.kind == "macro" and (m.count != 0 or m.optional == true or len(m.prefix) != 0)))
        {state: unsupported(st, "expl3 integer relation"), value: null}
    else {
        let scoped = {*:st, input: stream.push(st.input, [token, {kind: "expl3-int-relation-end"}])};
        let read = expanded_nonspace(scoped, budget)^;
        if (read.state.issue != null) {state: read.state, value: null}
        else if (not (other(read.token, "<") or other(read.token, "=") or other(read.token, ">")))
            {state: unsupported(read.state, "expl3 integer relation"), value: null}
        else {
            let end = expanded_nonspace(read.state, budget)^;
            if (end.state.issue != null) {state: end.state, value: null}
            else if (end.token == null or end.token.kind != "expl3-int-relation-end")
                {state: unsupported(end.state, "expl3 integer relation"), value: null}
            else {state: end.state, value: read.token.value}
        }
    }
}

// discard the rest of a false comparison chain without expanding it.
fn expl3_int_compare_skip(st) any^ {
    let read = stream.next(st.input)^;
    let current = {*:st, input: read.stream};
    if (read.token == null) {state: fail(current, "tex-runaway"), value: false}
    else if (read.token.kind == "expl3-int-compare-end") {state: current, value: false}
    else expl3_int_compare_skip(current)
}

fn expl3_int_compare_chain(st, left, budget, compared = false) any^ {
    let relation = expanded_nonspace(st, budget)^;
    if (relation.state.issue != null) {state: relation.state, value: false}
    else if (relation.token != null and relation.token.kind == "expl3-int-compare-end")
        if (compared) {state: relation.state, value: true}
        else {state: unsupported(relation.state, "expl3 integer comparison"), value: false}
    else if (not (other(relation.token, "<") or other(relation.token, ">") or
        other(relation.token, "=") or other(relation.token, "!")))
        {state: unsupported(relation.state, "expl3 integer comparison"), value: false}
    else {
        let next = expanded_nonspace(relation.state, budget)^;
        let equal = other(next.token, "=");
        let op = relation.token.value ++ if (equal) "=" else "";
        let right_start = if (equal) next.state else put_back(next.state, next.token)^;
        if (next.state.issue != null) {state: next.state, value: false}
        else if (op == "!") {state: unsupported(right_start, "expl3 integer comparison"), value: false}
        else {
            let right = scan_expression(right_start, budget)^;
            if (right.state.issue != null) {state: right.state, value: false}
            else {
                let matched = if (op == "<") left < right.value else if (op == ">") left > right.value
                    else if (op == "<=") left <= right.value else if (op == ">=") left >= right.value
                    else if (op == "!=") left != right.value else left == right.value;
                if (matched) expl3_int_compare_chain(right.state, right.value, budget, true)
                else expl3_int_compare_skip(right.state)
            }
        }
    }
}

fn expl3_int_compare_expr(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let expression = stream.argument(st.input, true)^;
        let branches = expl3_int_branches(expression.stream, name != "int_compare:nF", name != "int_compare:nT")^;
        let yes = branches.yes;
        let no = branches.no;
        let current = {*:st, input: stream.note_foreign_expansion(no.stream)^,
            expansions: st.expansions + 1, expl3_int_math_seen: true};
        if (expression.error != null) fail(current, expression.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else {
            let scoped = {*:current, input: stream.push(current.input,
                [*expression.tokens, {kind: "expl3-int-compare-end"}])};
            let first = scan_expression(scoped, budget)^;
            if (first.state.issue != null) first.state
            else {
                let selected = expl3_int_compare_chain(first.state, first.value, budget)^;
                if (selected.state.issue != null) selected.state
                else {*:selected.state, input: stream.push(selected.state.input,
                    if (selected.value) yes.tokens else no.tokens)}
            }
        }
    }
}

fn expl3_int_test(st, name, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let argument = stream.argument(st.input, true)^;
        let branches = expl3_int_branches(argument.stream,
            not contains(["int_if_zero:nF", "int_if_odd:nF", "int_if_even:nF"], name),
            not contains(["int_if_zero:nT", "int_if_odd:nT", "int_if_even:nT"], name))^;
        let yes = branches.yes;
        let no = branches.no;
        let current = {*:st, input: stream.note_foreign_expansion(no.stream)^,
            expansions: st.expansions + 1, expl3_int_math_seen: true};
        if (argument.error != null) fail(current, argument.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else {
            let value = expl3_int_tokens(current, argument.tokens, budget)^;
            if (value.state.issue != null) value.state
            else {
                let zero = contains(["int_if_zero:nTF", "int_if_zero:nT", "int_if_zero:nF"], name);
                let odd = contains(["int_if_odd:nTF", "int_if_odd:nT", "int_if_odd:nF"], name);
                let matched = if (zero) value.value == 0 else if (odd) (value.value % 2) != 0 else (value.value % 2) == 0;
                {*:value.state, input: stream.push(value.state.input, if (matched) yes.tokens else no.tokens)}
            }
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

fn set_extra_code(tables, name, code, value) any^ =>
    {*:tables, *:map([name, set_code(tables[name], code, value)])}

fn assign_extra_code(st, global, budget, name) any^ {
    let character = scan_number(st, budget)^;
    if (character.state.issue != null) character.state
    else {
        let value = scan_number(optional_equals(character.state, budget), budget)^;
        let limit = if (name == "sfcode") 32767 else if (name == "mathcode") 32768 else 16777215;
        if (value.state.issue != null) value.state
        else if (character.value < 0 or character.value > 1114111 or value.value > limit or
            (name != "delcode" and value.value < 0)) fail(value.state, "tex-bad-character-code")
        else {*:value.state, code_tables: set_extra_code(value.state.code_tables, name, character.value, value.value),
            groups: if (global) [for (scope in value.state.groups)
                {*:scope, code_tables: set_extra_code(scope.code_tables, name, character.value, value.value)}]
                else value.state.groups}
    }
}

fn extra_code(st, name, code) any^ {
    let assigned = st.code_tables[name][chr(code)];
    if (assigned != null) assigned
    else if (name == "sfcode") if (code >= 65 and code <= 90) 999 else 1000
    else if (name == "delcode") if (code == 46) 0 else -1
    else if ((code >= 65 and code <= 90) or (code >= 97 and code <= 122)) 28928 + code
    else if (code >= 48 and code <= 57) 28672 + code
    else code
}

fn code_quantity(st, name, budget) any^ {
    let code = scan_number(st, budget)^;
    if (code.state.issue != null) code
    else if (code.value < 0 or code.value > 1114111) {state: fail(code.state, "tex-bad-character-code"), value: 0}
    else {
        let c = chr(code.value);
        {state: code.state, value: if (name == "catcode") reader.catcode(c, code.state.input.reader.catcodes)
            else if (name == "lccode" or name == "uccode") case_code(code.state, {value: c}, name == "uccode")
            else extra_code(code.state, name, code.value)}
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
            {*:opening.state, input: stream.push(text.stream, stored_tokens(shifted))}
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
    let left = if (name == "ifdim") scan_dimen(st, budget)^ else scan_number(st, budget)^;
    if (left.state.issue != null) left.state
    else if (name == "ifodd") begin_conditional(left.state, (left.value % 2) != 0, negate)
    else {
        let relation = expanded_nonspace(left.state, budget)^;
        if (not (other(relation.token, "<") or other(relation.token, "=") or other(relation.token, ">")))
            fail(put_back(relation.state, relation.token), "tex-missing-relation")
        else {
            let right = if (name == "ifdim") scan_dimen(relation.state, budget)^ else scan_number(relation.state, budget)^;
            if (right.state.issue != null) right.state
            else begin_conditional(right.state, if (relation.token.value == "<") left.value < right.value
                else if (relation.token.value == ">") left.value > right.value else left.value == right.value, negate)
        }
    }
}

fn box_conditional(st, name, budget, negate) any^ {
    let index = scan_register(st, budget)^;
    if (index.state.issue != null) index.state
    else {
        let box = index.state.boxes[string(index.value)];
        let matched = if (name == "ifvoid") box == null
            else if (name == "ifhbox") box != null and box.kind == "horizontal"
            else box != null and box.kind == "vertical";
        begin_conditional(index.state, matched, negate)
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
    else if (a.kind == "primitive" or a.kind == "constructor") a.name == b.name
    else if (a.kind == "char") a.value == b.value and a.cat == b.cat
    else if (a.kind == "register" or a.kind == "dimension-register" or a.kind == "font") a.index == b.index
    else if (a.kind == "glue-register") a.index == b.index and a.mu == b.mu
    else if (a.kind == "token-register") a.index == b.index
    else if (a.kind == "chardef" or a.kind == "mathchardef") a.value == b.value
    else if (a.kind == "macro") a.count == b.count and a.long_ok == b.long_ok and a.protected_flag == b.protected_flag and
        a.brace_delim == b.brace_delim and (a.optional == true) == (b.optional == true) and
        (a.optional != true or tokens_equal(a.optional_default, b.optional_default)) and tokens_equal(a.prefix, b.prefix) and
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

fn input_eof_conditional(st, budget, negate) any^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else {
        let invalid = scanned.value < 0 or scanned.value > 15;
        let diagnosed = if (invalid) record_diagnostic(scanned.state, "tex-bad-number",
            "Bad number (" ++ string(scanned.value) ++ ")")^ else scanned.state;
        let current = {*:diagnosed, input_eof_seen: true};
        let slot = if (invalid) 0 else scanned.value;
        let input = current.input_streams[string(slot)];
        begin_conditional(current, input.open != true, negate)
    }
}

fn condition(st, name, budget, negate = false) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let depth = len(st.conditions);
        if (name == "iftrue" or name == "iffalse") begin_conditional(current, name == "iftrue", negate)
        else if (contains(["ifvmode", "ifhmode", "ifmmode", "ifinner"], name))
            begin_conditional(current, if (name == "ifvmode") false else if (name == "ifhmode") st.math_depth == 0
                else if (name == "ifmmode") st.math_depth > 0 else st.math_depth > 0 and not st.display_math, negate)
        else if (name == "ifnum" or name == "ifodd" or name == "ifdim") numeric_conditional(current, name, budget, negate)
        else if (name == "ifcase") case_conditional(current, budget)
        else if (contains(["ifvoid", "ifhbox", "ifvbox"], name)) box_conditional(current, name, budget, negate)
        else if (name == "ifeof") input_eof_conditional(current, budget, negate)
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
    {*:st, groups: [*st.groups, {definitions: st.definitions, counts: st.counts, dimens: st.dimens, skips: st.skips, muskips: st.muskips, toks: st.toks,
        lowercase: st.lowercase, uppercase: st.uppercase, code_tables: st.code_tables, current_font: st.current_font, parameters: st.parameters,
        catcodes: st.input.reader.catcodes, expl_syntax: st.expl_syntax, explicit: explicit, aftergroup: []}],
        output: if (token == null) st.output else [*st.output, token]}

// discarded groups execute normally but remove their output on close.
fn open_discard_scope(st) any^ {
    let opened = begin_group(st, null, false)^;
    let depth = len(opened.groups);
    {*:opened, groups: [for (i, scope in opened.groups)
        if (i == depth - 1) {*:scope, discard_output: len(opened.output)} else scope]}
}
fn begin_discard_group(st, budget, indexed) any^ {
    let scanned = if (indexed) scan_number(st, budget)^ else {state: st};
    if (scanned.state.issue != null) scanned.state
    else {
        let opening = scan_opening_brace(scanned.state, budget)^;
        if (opening.state.issue != null) opening.state else open_discard_scope(opening.state)
    }
}
fn scan_box_opening(st, budget) any^ {
    let to = scan_word(st, "to", budget)^;
    let spread = if (to.found) {state: to.state, found: false} else scan_word(to.state, "spread", budget)^;
    let size = if (to.found or spread.found) scan_dimen(spread.state, budget)^ else {state: spread.state};
    if (size.state.issue != null) {state: size.state, token: null}
    else scan_opening_brace(size.state, budget)
}
fn inject_box_hook(st, command) any^ {
    let hook = toks_value(st, parameter_index(if (command == "hbox") "everyhbox" else "everyvbox"));
    {*:st, input: stream.push(st.input, hook)}
}
fn begin_discard_box(st, budget, command) any^ {
    let opening = scan_box_opening(st, budget)^;
    if (opening.state.issue != null) opening.state
    else inject_box_hook(open_discard_scope(opening.state)^, command)
}
// inline boxes share scope and origin handling, even when they have no hook.
fn open_inline_body(opened, prefix, bare, hook) any^ {
    let depth = len(opened.groups);
    let origin = opened.input.last_main_offset;
    let current = {*:opened, groups: [for (i, scope in opened.groups)
            if (i == depth - 1) {*:scope, inline_box: not bare, inline_bare: bare} else scope],
        output: [*opened.output, *[for (i, token in prefix) {*:token, emit_origin: origin,
            emit_file: opened.main_file, box_open: charcat(token, 1) and i == len(prefix) - 1}]]};
    if (hook == null) current else inject_box_hook(current, hook)
}

fn begin_inline_box(st, budget, command, movement = null) any^ {
    let opening = scan_box_opening(st, budget)^;
    if (opening.state.issue != null) opening.state
    else {
        let opened = begin_group(opening.state, null, false)^;
        let prefix = if (movement == null) if (command == "hbox") [control_token("mbox"), brace_token(true)] else [brace_token(true)]
            else if (movement.wrap) [control_token("raisebox"), brace_token(true),
                *text_tokens(dimension.format(movement.value)^)^, brace_token(false), brace_token(true)] else [];
        open_inline_body(opened, prefix, movement != null and not movement.wrap, command)
    }
}

fn begin_vcenter(st, budget) any^ {
    let opening = scan_box_opening(st, budget)^;
    if (opening.state.issue != null) opening.state
    else {
        let opened = begin_group({*:opening.state, vcenter_seen: true}, null, false)^;
        open_inline_body(opened, [control_token("vcenter"), brace_token(true)], false, null)
    }
}

// movement wraps vertical shifts and otherwise emits the inline box contents.
fn move_inline_box(st, budget, command) any^ {
    let dimension = scan_dimen(st, budget)^;
    if (dimension.state.issue != null) dimension.state
    else {
        let box = scan_box_command(dimension.state, budget)^;
        let kind = box.command;
        if (box.state.issue != null) box.state
        else if (not contains(["hbox", "vbox", "vtop"], kind)) unsupported(put_back(box.state, box.token), "box movement source")
        else {
            let wrap = command == "raise" or command == "lower";
            let value = if (command == "lower") -dimension.value else dimension.value;
            begin_inline_box({*:box.state, box_movement_seen: true}, budget, kind, {wrap: wrap, value: value})
        }
    }
}

// shipout executes an inline box while suppressing its emitted material.
fn ship_inline_box(st, budget) any^ {
    let box = scan_box_command(st, budget)^;
    if (box.state.issue != null) box.state
    else if (not contains(["hbox", "vbox", "vtop"], box.command))
        unsupported(put_back(box.state, box.token), "shipout box source")
    else begin_discard_box({*:box.state, shipout_seen: true}, budget, box.command)
}

fn save_aftergroup(st) any^ {
    let read = stream.next(st.input);
    let depth = len(st.groups);
    let current = {*:st, input: read.stream};
    if (read.token == null) fail(current, "tex-runaway-aftergroup")
    else if (depth == 0) current
    else {*:current, groups: [for (index, scope in st.groups)
        if (index == depth - 1) {*:scope, aftergroup: [*scope.aftergroup, {*:read.token, direct: false}]} else scope]}
}

fn end_group(st, token, explicit, environment = false, math = false) any^ {
    let depth = len(st.groups);
    if (depth == 0 or st.groups[depth - 1].explicit != explicit or
        ((st.groups[depth - 1].environment != null) != environment) or
        ((st.groups[depth - 1].math == true) != math))
        fail(st, if (explicit) "tex-extra-endgroup" else "tex-extra-brace")
    else {
        let scope = st.groups[depth - 1];
        let restored = {*:stream.configure(st.input, scope.catcodes, scope.parameters.endlinechar),
            everyeof: if (scope.toks["-1"] == null) [] else scope.toks["-1"]};
        {*:st, expl_syntax: scope.expl_syntax, definitions: scope.definitions, counts: scope.counts, dimens: scope.dimens, skips: scope.skips, muskips: scope.muskips, toks: scope.toks,
            lowercase: scope.lowercase, uppercase: scope.uppercase, code_tables: scope.code_tables, current_font: scope.current_font, parameters: scope.parameters,
            input: stream.push(restored, scope.aftergroup), groups: slice(st.groups, 0, depth - 1),
            output: if (scope.discard_output != null) slice(st.output, 0, scope.discard_output)^
                else if (scope.inline_box == true) [*st.output,
                    {*:brace_token(false), emit_origin: st.input.last_main_offset, emit_file: st.main_file, box_close: true}]
                else if (scope.inline_bare == true or token == null) st.output else [*st.output, token]}
    }
}

fn math_shift_token(st, token) any^ => charcat(token, 3) or
    (is_control(token) and meaning(st, token).kind == "char" and meaning(st, token).cat == 3)

fn begin_math(st, token, display, second = null, run_hook = false) any^ {
    let opened = begin_group(st, token, true)^;
    let depth = len(opened.groups);
    let math_groups = [for (i, group in opened.groups)
        if (i == depth - 1) {*:group, math: true, display: display} else group];
    let hook = if (run_hook) opened.toks[parameter_index(if (display) "everydisplay" else "everymath")] else null;
    {*:opened, groups: math_groups, math_depth: st.math_depth + 1,
        display_math: if (run_hook) display else st.display_math,
        output: if (second == null) opened.output else [*opened.output, second],
        input: if (hook == null) opened.input else stream.push(opened.input, hook)}
}

fn finish_math(st, token, second = null, reset_display = true) any^ {
    let ended = end_group(st, null, true, false, true)^;
    if (ended.issue != null) ended else {*:ended, math_depth: if (st.math_depth > 0) st.math_depth - 1 else 0,
        display_math: if (reset_display) false else st.display_math,
        output: [*ended.output, token, *(if (second == null) [] else [second])]}
}

fn math_shift(st, token) any^ {
    let depth = len(st.groups);
    let closing = depth > 0 and st.groups[depth - 1].math == true;
    let scan = if ((closing and st.groups[depth - 1].display == true) or (not closing and st.math_depth == 0))
        stream.next(st.input)^ else null;
    let is_second = scan != null and scan.token != null and math_shift_token(st, scan.token)^;
    let input = if (scan == null) st.input else if (is_second or scan.token == null) scan.stream
        else stream.push(scan.stream, [scan.token]);
    let current = {*:st, input: input};
    if (closing) {
        let diagnosed = if (st.groups[depth - 1].display == true and scan != null and scan.token != null and not is_second)
            {*:current, diagnostics: [*current.diagnostics, "tex-display-math"]} else current;
        finish_math(diagnosed, token, if (is_second) scan.token else null)
    }
    else begin_math(current, token, is_second, if (is_second) scan.token else null, true)
}

fn latex_math_switch(st, token, cmd) any^ {
    if (cmd == "(" or cmd == "[") begin_math(st, token, false, null, false)
    else {
        let depth = len(st.groups);
        if (depth > 0 and st.groups[depth - 1].math == true) finish_math(st, token, null, false)
        else {*:st, output: [*st.output, token]}
    }
}

fn control_token(name) => {kind: "control", value: name, cat: null, start: -1, end: -1}
fn brace_token(opening) => {kind: "char", value: if (opening) "{" else "}",
    cat: if (opening) 1 else 2, start: -1, end: -1}

fn latex_optional(input) any^ {
    let scanned = stream.optional_argument(input)^;
    {input: scanned.stream, present: scanned.present, tokens: scanned.tokens, raw: scanned.raw, issue: scanned.error}
}

fn decimal_prefix(text, position, value = 0) any^ {
    if (position >= len(text)) value
    else {
        let c = slice(text, position, position + 1)^;
        let n = index_of("0123456789", c);
        if (n == null) value else decimal_prefix(text, position + 1, min(10, value * 10 + n))
    }
}

fn latex_arg_count(tokens) any^ {
    let text = trim(join([for (t in tokens) t.value], ""));
    let negative = starts_with(text, "-");
    let signed = negative or starts_with(text, "+");
    let value = decimal_prefix(text, if (signed) 1 else 0)^;
    if (negative) -value else value
}

fn latex_macro(nargs, starred, protected_flag, optional, default_tokens, body) any^ =>
    {kind: "macro", count: nargs, prefix: [], delimiters: if (nargs == 0) [] else [for (i in 1 to nargs) []],
        brace_delim: false, long_ok: not starred, protected_flag: protected_flag,
        optional: optional, optional_default: default_tokens, body: body}

fn latex_definable(m) => m.kind == "undefined" or m.kind == "relax" or (m.kind == "primitive" and m.name == "relax")

fn latex_newcommand(st, name) any^ {
    let star = nonspace(st.input);
    let starred = other(star.token, "*");
    let remaining = if (starred) star.stream else if (star.token == null) star.stream else stream.push(star.stream, [star.token]);
    let target = stream.argument(remaining, true)^;
    let controls = [for (t in target.tokens where (is_control(t) ^ { false })) t];
    let current = {*:st, input: target.stream};
    if (target.error != null) fail(current, target.error)
    else if (len(controls) == 0) fail(current, "latex-missing-cs")
    else {
        let count = latex_optional(target.stream)^;
        let default_arg = latex_optional(count.input)^;
        let body = stream.argument(default_arg.input, true)^;
        let advanced = {*:current, input: body.stream};
        if (count.issue != null) fail(advanced, count.issue)
        else if (default_arg.issue != null) fail(advanced, default_arg.issue)
        else if (body.error != null) fail(advanced, body.error)
        else {
            let raw_count = if (count.present) latex_arg_count(count.tokens)^ else 0;
            let nargs = if (raw_count < 0 or raw_count > 9) 0 else raw_count;
            let noted = if (raw_count < 0 or raw_count > 9) {*:advanced,
                diagnostics: [*advanced.diagnostics, "latex-bad-arg-count"]} else advanced;
            let parsed = body_params(body.tokens, nargs)^;
            let old = meaning(noted, controls[0]);
            let definable = latex_definable(old);
            if (parsed.issue != null) fail(noted, parsed.issue)
            else if (name == "newcommand" and not definable)
                {*:noted, diagnostics: [*noted.diagnostics, "latex-already-defined"]}
            else if (name == "providecommand" and not definable) noted
            else {
                let ready = if (name == "renewcommand" and definable)
                    {*:noted, diagnostics: [*noted.diagnostics, "latex-undefined"]} else noted;
                let macro = latex_macro(nargs, starred, name == "DeclareRobustCommand", default_arg.present,
                    default_arg.tokens, parsed.tokens)^;
                assign_meaning(ready, key(controls[0]), macro, false)
            }
        }
    }
}

// The first xparse subset supports mandatory arguments and one leading default.
fn document_signature(input, count = 0, optional = false, defaults = []) any^ {
    let read = nonspace(input)^;
    let t = read.token;
    if (t == null) {issue: null, count: count, optional: optional, defaults: defaults}
    else if (t.kind == "char" and t.value == "m")
        if (count >= 9) {issue: "latex-document-argument-count"}
        else document_signature(read.stream, count + 1, optional, defaults)
    else if (t.kind == "char" and t.value == "O" and count == 0 and not optional) {
        let opening = nonspace(read.stream)^;
        if (not charcat(opening.token, 1)) {issue: "latex-document-argument-default"}
        else {
            let value = stream.argument(stream.push(opening.stream, [opening.token]), true)^;
            if (value.error != null) {issue: value.error}
            else document_signature(value.stream, 1, true, value.tokens)
        }
    }
    else {issue: "latex-unsupported-document-argument", unsupported: if (t == null) null else t.value}
}

fn latex_document_command(st, name) any^ {
    let target = stream.argument(st.input, true)^;
    let signature = stream.argument(target.stream, true)^;
    let body = stream.argument(signature.stream, true)^;
    let current = {*:st, input: body.stream};
    if (target.error != null) fail(current, target.error)
    else if (signature.error != null) fail(current, signature.error)
    else if (body.error != null) fail(current, body.error)
    else if (len(target.tokens) != 1 or not is_control(target.tokens[0])) fail(current, "latex-missing-cs")
    else {
        let spec = document_signature(stream.push(stream.from_reader(reader.reader("")), signature.tokens))^;
        let old = meaning(current, target.tokens[0]);
        let definable = latex_definable(old);
        if (name == "ProvideDocumentCommand" and not definable) current
        else if (name == "NewDocumentCommand" and not definable) fail(current, "latex-already-defined")
        else if (name == "RenewDocumentCommand" and definable) fail(current, "latex-undefined")
        else if (spec.issue != null) {*:current, issue: spec.issue, unsupported: spec.unsupported}
        else {
            let parsed = body_params(body.tokens, spec.count)^;
            if (parsed.issue != null) fail(current, parsed.issue)
            else {
                let macro = latex_macro(spec.count, false, true, spec.optional, spec.defaults, parsed.tokens)^;
                assign_meaning(current, key(target.tokens[0]), macro, effective_global(current, false)^)
            }
        }
    }
}

fn expand_argument_name(st, tokens, budget) any^ {
    // plain characters need no re-reading, which also preserves source tracing.
    if (not any([for (token in tokens) token.kind != "char"]))
        {state: st, name: token_text(st, tokens)^}
    else {
        let opener = brace_token(true);
        let closer = brace_token(false);
        let wrapped = {*:st, input: stream.push(st.input, [opener, *tokens, closer])};
        let scanned = expanded_text(wrapped, budget)^;
        {state: scanned.state, name: token_text(scanned.state, scanned.tokens)}
    }
}

// counters use TeX count registers so \value and low-level assignments share state.
fn latex_counter_name(st, budget) any^ {
    let arg = stream.raw_argument(st.input)^;
    let current = {*:st, input: arg.stream};
    if (arg.error != null) {state: fail(current, arg.error), name: "", raw: arg.raw}
    else {
        let named = expand_argument_name(current, arg.tokens, budget)^;
        {state: named.state, name: named.name, raw: arg.raw}
    }
}
fn counter_key(name) => "control:c@" ++ name
fn counter_meaning(st, name) any^ => meaning(st, control_token("c@" ++ name))
fn begin_main_trace(st, original, capture = false) any^ =>
    if (original.direct == true and original.file == st.main_file)
        {*:st, input: stream.begin_trace(st.input, original, st.main_file, capture)^} else st
fn end_main_trace(st, original) any^ {
    if (original.direct != true or original.file != st.main_file) {state: st, echo: false}
    else {
        let traced = stream.end_trace(st.input)^;
        {state: {*:st, input: traced.stream}, echo: traced.valid, span: traced}
    }
}
fn counter_diagnostic(st, code) => {*:st, diagnostics: [*st.diagnostics, code]}
fn counter_command_tokens(name, command, value = null) any^ {
    let opener = brace_token(true);
    let closer = brace_token(false);
    [control_token(command), opener, *text_tokens(name), closer,
        *(if (value == null) [] else [opener, *value, closer])]
}
fn latex_newcounter(st, original, budget) any^ {
    let traced = begin_main_trace(st, original)^;
    let named = latex_counter_name(traced, budget)^;
    let opt = latex_optional(named.state.input)^;
    let current = {*:named.state, input: opt.input};
    if (current.issue != null) current
    else if (opt.issue != null) fail(current, opt.issue)
    else {
        let existing = counter_meaning(current, named.name)^;
        if (existing.kind == "register") {
            let closed = end_main_trace(current, original)^;
            let emitted = if (closed.echo) {*:closed.state, output: [*closed.state.output, original, *named.raw, *opt.raw]} else closed.state;
            counter_diagnostic(emitted, "latex-counter-defined")
        }
        else {
            let next = count_value(current, 10) + 1;
            let exhausted = next >= count_value(current, 20);
            // the native allocator reserves the final register when allocation fails.
            let index = if (exhausted) COUNT_REGISTER_LIMIT - 1 else next;
            let allocated = if (exhausted) counter_diagnostic(current, "tex-no-room")
                else save_count(current, 10, index, true)^;
            let registered = assign_meaning(allocated, counter_key(named.name), {kind: "register", index: index}, true)^;
            let body = counter_command_tokens(named.name, "arabic")^;
            let formatted = assign_meaning(registered, "control:the" ++ named.name,
                latex_macro(0, false, false, false, [], body)^, true)^;
            let parent = if (opt.present) expand_argument_name(formatted, opt.tokens, budget)^
                else {state: formatted, name: ""};
            let closed = end_main_trace(parent.state, original)^;
            let emitted = if (closed.echo) {*:closed.state, output: [*closed.state.output, original, *named.raw, *opt.raw]} else closed.state;
            if (emitted.issue != null) emitted
            else {*:emitted, counter_resets: if (opt.present)
                [*emitted.counter_resets, {parent: parent.name, child: named.name}] else emitted.counter_resets,
                engine_counters: if (closed.echo) emitted.engine_counters else [*emitted.engine_counters, named.name]}
        }
    }
}
fn reset_counter_children(st, parent, index = 0, depth = 0) any^ {
    if (st.issue != null or index >= len(st.counter_resets)) st
    else if (depth > len(st.counter_resets)) fail(st, "tex-capacity")
    else {
        let entry = st.counter_resets[index];
        let found = counter_meaning(st, entry.child)^;
        let reset = if (entry.parent == parent and found.kind == "register")
            reset_counter_children(save_count(st, found.index, 0, true)^, entry.child, 0, depth + 1)^ else st;
        reset_counter_children(reset, parent, index + 1, depth)
    }
}
// the argument boundary must survive nested numeric expressions and lookahead.
fn latex_counter_value(st, tokens, budget) any^ {
    let boundary = {kind: "counter-value-end"};
    let scoped = {*:st, input: stream.push(st.input, [*stored_tokens(tokens), boundary])};
    let value = scan_number(scoped, budget)^;
    if (value.state.issue != null) value
    else {
        let end = expanded_nonspace(value.state, budget)^;
        let after = if (primitive(end.state, end.token) == "relax") expanded_nonspace(end.state, budget)^ else end;
        if (after.state.issue != null) {state: after.state, value: value.value}
        else if (after.token != null and after.token.kind == "counter-value-end") {state: after.state, value: value.value}
        else {state: counter_diagnostic(put_back(after.state, after.token), "latex-counter-value"), value: value.value}
    }
}
fn latex_counter_command(st, original, command, budget) any^ {
    let traced = begin_main_trace(st, original)^;
    let named = latex_counter_name(traced, budget)^;
    let has_value = command == "setcounter" or command == "addtocounter";
    let arg = if (has_value) stream.raw_argument(named.state.input)^
        else {stream: named.state.input, tokens: [], raw: [], error: null};
    let current = {*:named.state, input: arg.stream};
    if (current.issue != null) current
    else if (arg.error != null) fail(current, arg.error)
    else {
        let closed = end_main_trace(current, original)^;
        let found = counter_meaning(closed.state, named.name)^;
        let value = if (found.kind == "register" and has_value) latex_counter_value(closed.state, arg.tokens, budget)^
            else {state: closed.state, value: 0};
        let previous = if (found.kind == "register") count_value(value.state, found.index) else 0;
        let next = if (command == "setcounter") value.value else if (command == "addtocounter") previous + value.value else previous + 1;
        let updated = if (value.state.issue != null or found.kind != "register") value.state
            else if (next > 2147483647 or next < -2147483647) fail(value.state, "tex-arith-overflow")
            else save_count(value.state, found.index, next, true)^;
        let stepped = if (found.kind == "register" and (command == "stepcounter" or command == "refstepcounter"))
            reset_counter_children(updated, named.name)^ else updated;
        let raw = [*named.raw, *arg.raw];
        let echo = closed.echo;
        if (stepped.issue != null) stepped
        else if (echo) {*:stepped, output: [*stepped.output, original, *raw]}
        else if (contains(stepped.engine_counters, named.name)) stepped
        else {*:stepped, output: [*stepped.output,
            *counter_command_tokens(named.name, command, if (has_value and len(arg.tokens) > 0)
                if (found.kind == "register") text_tokens(string(next)) else arg.tokens else null)]}
    }
}
fn counter_symbol(value) => if (value == 1) "*" else if (value == 2) "\\dag"
    else if (value == 3) "\\ddag" else if (value == 4) "\\S" else if (value == 5) "\\P"
    else if (value == 6) "\\textbardbl" else if (value == 7) "**"
    else if (value == 8) "\\dag\\dag" else if (value == 9) "\\ddag\\ddag" else ""
fn latex_counter_format(st, original, command, budget) any^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let named = latex_counter_name(current, budget)^;
        if (named.state.issue != null) named.state
        else {
            let found = counter_meaning(named.state, named.name)^;
            if (found.kind != "register") {*:named.state, input: stream.push(named.state.input,
                [{*:original, noexpand: true}, *named.raw])}
            else if (command == "value") {*:named.state, input: stream.push(named.state.input,
                [control_token("c@" ++ named.name)])}
            else {
                let value = count_value(named.state, found.index);
                let valid = if (command == "alph" or command == "Alph") value >= 1 and value <= 26
                    else if (command == "fnsymbol") value >= 1 and value <= 9 else true;
                let text = if (command == "arabic") string(value) else if (command == "roman") roman(value)^
                    else if (command == "Roman") upper(roman(value)^)
                    else if (command == "alph") if (valid) chr(96 + value) else ""
                    else if (command == "Alph") if (valid) chr(64 + value) else "" else "";
                let checked = if (valid) named.state else counter_diagnostic(named.state, "latex-counter-too-large");
                if (command != "fnsymbol") {*:checked, input: stream.push(checked.input, text_tokens(text))}
                else if (not valid) checked
                else if (len(checked.input.levels) >= 6000) fail(checked, "tex-capacity")
                else {
                    // symbol text is re-tokenized with the current catcodes and endline rule.
                    let child = {*:reader.reader(counter_symbol(value), -1), catcodes: checked.input.reader.catcodes,
                        endline: checked.input.reader.endline};
                    {*:checked, input: stream.push_reader(checked.input, child)}
                }
            }
        }
    }
}

fn latex_newenvironment(st, mode, budget) any^ {
    let star = nonspace(st.input);
    let starred = other(star.token, "*");
    let remaining = if (starred) star.stream else if (star.token == null) star.stream else stream.push(star.stream, [star.token]);
    let env_name = stream.argument(remaining, true)^;
    let current = {*:st, input: env_name.stream};
    if (env_name.error != null) fail(current, env_name.error)
    else {
        let named = expand_argument_name(current, env_name.tokens, budget)^;
        let count = latex_optional(named.state.input)^;
        let default_arg = latex_optional(count.input)^;
        let before = stream.argument(default_arg.input, true)^;
        let after = stream.argument(before.stream, true)^;
        let advanced = {*:named.state, input: after.stream};
        if (named.state.issue != null) named.state
        else if (count.issue != null) fail(advanced, count.issue)
        else if (default_arg.issue != null) fail(advanced, default_arg.issue)
        else if (before.error != null) fail(advanced, before.error)
        else if (after.error != null) fail(advanced, after.error)
        else {
            let raw_count = if (count.present) latex_arg_count(count.tokens)^ else 0;
            let nargs = if (raw_count < 0 or raw_count > 9) 0 else raw_count;
            let first = body_params(before.tokens, nargs)^;
            let final_body = body_params(after.tokens, 0)^;
            let old = meaning(advanced, control_token(named.name));
            let definable = latex_definable(old);
            if (first.issue != null) fail(advanced, first.issue)
            else if (final_body.issue != null) fail(advanced, final_body.issue)
            else if (mode == "newenvironment" and not definable)
                {*:advanced, diagnostics: [*advanced.diagnostics, "latex-already-defined"]}
            else {
                let ready = if (mode == "renewenvironment" and definable)
                    {*:advanced, diagnostics: [*advanced.diagnostics, "latex-undefined"]} else advanced;
                let begin_macro = latex_macro(nargs, starred, false, default_arg.present, default_arg.tokens, first.tokens)^;
                let end_macro = latex_macro(0, starred, false, false, [], final_body.tokens)^;
                let added = assign_meaning(ready, "control:" ++ named.name, begin_macro, false)^;
                assign_meaning(added, "control:end" ++ named.name, end_macro, false)
            }
        }
    }
}

fn begin_environment(st, name, passthrough) any^ {
    let opened = begin_group(st, null, true)^;
    let final_index = len(opened.groups) - 1;
    {*:opened, groups: [for (i, scope in opened.groups)
        if (i == final_index) {*:scope, environment: name, passthrough: passthrough} else scope]}
}

fn latex_document_hook(st, is_begin) any^ {
    let code = stream.argument(st.input, true)^;
    let current = {*:st, input: code.stream};
    if (code.error != null) fail(current, code.error)
    else if (is_begin and st.in_document) {*:current, input: stream.push(current.input, code.tokens)}
    else if (is_begin) {*:current, begin_hooks: [*st.begin_hooks, *stored_tokens(code.tokens)]}
    else {*:current, end_hooks: [*st.end_hooks, *stored_tokens(code.tokens)]}
}

// native echoes direct environment source, but synthesizes expanded names.
fn environment_output(st, original, raw, name, command, echo, span) any^ {
    let source = [original, *raw];
    if (echo) [{*:original, echo_span: span, echo_count: len(source)}, *raw]
    else [for (token in [control_token(command), brace_token(true), *text_tokens(name), brace_token(false)])
        {*:token, emit_origin: st.input.last_main_offset}]
}

fn latex_begin(st, original, budget) any^ {
    let traced = begin_main_trace(st, original)^;
    let argument = stream.raw_argument(traced.input)^;
    let current = {*:traced, input: argument.stream};
    if (argument.error != null) fail(current, argument.error)
    else {
        let named = expand_argument_name(current, argument.tokens, budget)^;
        let closed = end_main_trace(named.state, original)^;
        if (closed.state.issue != null) closed.state
        else {
            let m = meaning(closed.state, control_token(named.name));
            let custom = named.name != "document" and m.kind == "macro";
            let opened = begin_environment(closed.state, named.name, not custom)^;
            if (custom) {
                let current_env = latex_macro(0, false, false, false, [], text_tokens(named.name))^;
                let with_name = assign_meaning(opened, "control:@currenvir", current_env, false)^;
                {*:with_name, input: stream.push(with_name.input, [control_token(named.name)])}
            }
            else {
                let emitted = environment_output(opened, original, argument.raw, named.name, "begin", closed.echo, closed.span)^;
                let passed = {*:opened, output: [*opened.output, *emitted]};
                if (named.name != "document") passed
                else {*:passed, in_document: true, begin_hooks: [], input: stream.push(passed.input, st.begin_hooks)}
            }
        }
    }
}

fn latex_end(st, original, budget) any^ {
    let traced = begin_main_trace(st, original)^;
    let argument = stream.raw_argument(traced.input)^;
    let current = {*:traced, input: argument.stream};
    if (argument.error != null) fail(current, argument.error)
    else {
        let named = expand_argument_name(current, argument.tokens, budget)^;
        let closed = end_main_trace(named.state, original)^;
        let ready = closed.state;
        let depth = len(ready.groups);
        if (ready.issue != null) ready
        else if (named.name == "document") {
            let close = {kind: "document-close", original: original, raw: argument.raw,
                echo: closed.echo and len(ready.end_hooks) == 0, span: closed.span};
            {*:ready, end_hooks: [], input: stream.push(ready.input, [*ready.end_hooks, close])}
        }
        else if (depth == 0 or ready.groups[depth - 1].environment != named.name) {
            let emitted = environment_output(ready, original, argument.raw, named.name, "end", closed.echo, closed.span)^;
            {*:ready, output: [*ready.output, *emitted], diagnostics: [*ready.diagnostics, "latex-env-mismatch"]}
        }
        else if (ready.groups[depth - 1].passthrough) {
            let emitted = environment_output(ready, original, argument.raw, named.name, "end", closed.echo, closed.span)^;
            let ended = end_group(ready, null, true, true)^;
            {*:ended, output: [*ended.output, *emitted]}
        }
        else {*:ready, input: stream.push(ready.input,
            [control_token("end" ++ named.name), {kind: "env-close", value: named.name}])}
    }
}

fn define_text_macro(st, name, value) any^ {
    let macro = latex_macro(0, false, false, false, [], text_tokens(value))^;
    assign_meaning(st, "control:" ++ name, macro, true)
}

fn record_package(st, name, opts, is_class, kind) map^ {
    let recorded = {*:st, packages: [*st.packages, {name: name, options: opts, is_class: is_class, kind: kind}]};
    if (kind == "unknown") recorded
    else {
        let ext = if (is_class) ".cls" else ".sty";
        let version = define_text_macro(recorded, "ver@" ++ name ++ ext, "")^;
        let with_options = define_text_macro(version, "opt@" ++ name ++ ext, opts)^;
        let current = meaning(with_options, control_token("@filelist"));
        let prior = if (current.kind == "macro") token_text(with_options, current.body)^ else "";
        define_text_macro(with_options, "@filelist", prior ++ (if (prior == "") "" else ",") ++ name ++ ext)
    }
}

fn probe_resource(st, name, scope) any^ {
    if (st.resources == null) {state: st, available: false, found: false}
    else if (not (st.resources is map) or (st.resources[scope] != null and not (st.resources[scope] is map)))
        {state: fail(st, "tex-resource-invalid"), available: true, found: false}
    else if (st.resources[scope] == null) {state: st, available: false, found: false}
    else if (name == "" or starts_with(name, "/") or contains(name, "..")) {state: st, available: true, found: false}
    else {state: st, available: true, found: st.resources[scope][name] != null}
}

fn package_loaded(st, name, is_class) any^ =>
    any([for (entry in st.packages) entry.name == name and entry.is_class == is_class])

fn package_plan(st, names, opts, index = 0, loads = [], passthrough = []) any^ {
    if (st.issue != null or index >= len(names)) {state: st, loads: loads, passthrough: passthrough}
    else {
        let name = names[index];
        if (st.script_expl3 and name == "expl3") {
            if (opts != "") {state: unsupported(st, "expl3 package options"), loads: loads, passthrough: passthrough}
            else {
                let ready = if (package_loaded(st, name, false)) st else record_package(st, name, opts, false, "script")^;
                package_plan(ready, names, opts, index + 1, loads, passthrough)
            }
        }
        else if (package_loaded(st, name, false))
            package_plan(st, names, opts, index + 1, loads, [*passthrough, name])
        else {
            let bundled = probe_resource(st, name ++ ".sty", "bundled")^;
            let adapter = contains(st.adapters, name);
            if (bundled.state.issue != null) {state: bundled.state, loads: loads, passthrough: passthrough}
            else if (not bundled.available) {state: unsupported(st, "usepackage"), loads: loads, passthrough: passthrough}
            else if (bundled.found) package_plan(st, names, opts, index + 1,
                [*loads, {kind: "package-load", name: name, options: opts, scope: "bundled", is_class: false}],
                if (adapter) [*passthrough, name] else passthrough)
            else if (adapter) {
                let recorded = record_package(st, name, opts, false, "adapter")^;
                package_plan(recorded, names, opts, index + 1, loads, [*passthrough, name])
            }
            else {
                let local = probe_resource(st, name ++ ".sty", "local")^;
                if (local.state.issue != null) {state: local.state, loads: loads, passthrough: passthrough}
                else if (not local.available) {state: unsupported(st, "usepackage"), loads: loads, passthrough: passthrough}
                else if (local.found) package_plan(st, names, opts, index + 1,
                    [*loads, {kind: "package-load", name: name, options: opts, scope: "local", is_class: false}], passthrough)
                else {
                    let recorded = record_package(st, name, opts, false, "unknown")^;
                    package_plan(recorded, names, opts, index + 1, loads, [*passthrough, name])
                }
            }
        }
    }
}

fn declaration_tokens(name, opts, names) any^ {
    let opener = brace_token(true);
    let closer = brace_token(false);
    [control_token(name), *(if (opts == "") [] else text_tokens("[" ++ opts ++ "]")),
        opener, *text_tokens(join(names, ",")), closer]
}

fn latex_usepackage(st, original, budget) any^ {
    let traced = begin_main_trace(st, original)^;
    let opt = latex_optional(traced.input)^;
    let name_arg = stream.raw_argument(opt.input)^;
    let named = expand_argument_name({*:st, input: name_arg.stream}, name_arg.tokens, budget)^;
    let version_arg = latex_optional(named.state.input)^;
    let current = {*:named.state, input: version_arg.input};
    if (opt.issue != null) fail(current, opt.issue)
    else if (name_arg.error != null) fail(current, name_arg.error)
    else if (named.state.issue != null) named.state
    else if (version_arg.issue != null) fail(current, version_arg.issue)
    else {
        let opts = if (primitive(st, original) == "RequirePackageWithOptions" and st.current_options != null)
            st.current_options else if (opt.present) token_text(current, opt.tokens)^ else "";
        let names = [for (part in (split(named.name, ",") ^ { [] }) where ((trim(part) != "") ^ { false })) trim(part)];
        let planned = package_plan(current, names, opts)^;
        if (planned.state.issue != null) planned.state
        else {
            let closed = end_main_trace(planned.state, original)^;
            // a script-handled name must not leak into an echoed package declaration.
            let passed = if (len(planned.loads) == 0 and len(planned.passthrough) == len(names) and closed.echo)
                [original, *opt.raw, *name_arg.raw, *version_arg.raw]
                else if (len(planned.passthrough) > 0)
                    declaration_tokens("usepackage", opts, planned.passthrough)^
                else [];
            {*:closed.state, output: [*closed.state.output, *passed],
                input: stream.push(closed.state.input, planned.loads)}
        }
    }
}

fn start_package(st, request, budget) any^ {
    if (not request.is_class and package_loaded(st, request.name, false)) st
    else {
        let suffix = if (request.is_class) ".cls" else ".sty";
        let fetched = captured_resource(st, request.name ++ suffix, request.scope)^;
        if (fetched.state.issue != null) fetched.state
        else if (not fetched.available) unsupported(fetched.state, "package resource")
        else if (not fetched.found) {*:fetched.state, diagnostics: [*fetched.state.diagnostics, "tex-file-not-found"]}
        else if (len(fetched.state.input.levels) >= 6000) fail(fetched.state, "tex-capacity")
        else {
            let decoded = utf8.decode(fetched.data) ^ { null };
            if (decoded == null) fail(fetched.state, "tex-invalid-utf8")
            else {
                let saved_at = reader.catcode("@", fetched.state.input.reader.catcodes);
                let marked = {*:fetched.state, sources: [for (source in fetched.state.sources)
                    if (source.id == fetched.id) {*:source, package: true} else source]};
                let passed = [for (entry in marked.pass_options where entry.name == request.name) entry.options];
                let options = join([for (entry in [request.options, *passed] where entry != "") entry], ",");
                let recorded = record_package(marked, request.name, options, request.is_class,
                    if (request.scope == "bundled") "adapter" else "local")^;
                let frame = {name: request.name, is_class: request.is_class, saved_at: saved_at,
                    expl_was_active: fetched.state.expl_syntax != null and fetched.state.expl_syntax.active == true,
                    previous_name: st.current_package, previous_options: st.current_options,
                    previous_is_class: st.current_is_class, end_hook: []};
                let source_input = recorded.input;
                let configured = if (source_input is map) stream.configure(source_input,
                    set_code(source_input.reader.catcodes, 64, 11), source_input.reader.endline)^ else null;
                if (configured != null) {
                let child = {*:reader.reader(decoded, fetched.id), origin: request.scope,
                    catcodes: configured.reader.catcodes, endline: configured.reader.endline};
                let with_close = stream.push(configured, [{kind: "package-end"}]);
                let with_reader = stream.push_reader(with_close, child);
                let opener = brace_token(true);
                let closer = brace_token(false);
                let prefix = [control_token("@pushfilename"), control_token("xdef"), control_token("@currname"),
                    opener, *text_tokens(request.name), closer, control_token("gdef"), control_token("@currext"),
                    opener, *text_tokens(if (request.is_class) "cls" else "sty"), closer];
                {*:recorded, input: stream.push(with_reader, prefix), package_frames: [*st.package_frames, frame],
                    current_package: request.name, current_options: options, current_is_class: request.is_class}
                } else fail(recorded, "tex-resource-invalid")
            }
        }
    }
}

fn finish_package(st) any^ {
    let depth = len(st.package_frames);
    if (depth == 0) st
    else {
        let frame = st.package_frames[depth - 1];
        let active = st.expl_syntax != null and st.expl_syntax.active == true;
        let adjusted = if (st.script_expl3 and active != frame.expl_was_active)
            expl3_syntax(st, frame.expl_was_active)^ else st;
        let restored = {*:adjusted, input: stream.configure(adjusted.input, set_code(adjusted.input.reader.catcodes, 64, frame.saved_at),
                adjusted.input.reader.endline), package_frames: slice(adjusted.package_frames, 0, depth - 1),
            current_package: frame.previous_name, current_options: frame.previous_options, current_is_class: frame.previous_is_class};
        let with_name = define_text_macro(restored, "@currname", if (frame.previous_name == null) "" else frame.previous_name)^;
        define_text_macro(with_name, "@currext", if (frame.previous_name == null) "" else if (frame.previous_is_class) "cls" else "sty")
    }
}

fn package_end_hook(st) any^ {
    let code = stream.argument(st.input, true)^;
    let current = {*:st, input: code.stream};
    let depth = len(st.package_frames);
    if (code.error != null) fail(current, code.error)
    else if (depth == 0) {*:current, input: stream.push(current.input, code.tokens)}
    else {*:current, package_frames: [for (i, frame in st.package_frames)
        if (i == depth - 1) {*:frame, end_hook: [*frame.end_hook, *stored_tokens(code.tokens)]} else frame]}
}

fn finish_package_hook(st) any^ {
    let depth = len(st.package_frames);
    if (depth == 0) st
    else {
        let frame = st.package_frames[depth - 1];
        let cleared = {*:st, package_frames: [for (i, entry in st.package_frames)
            if (i == depth - 1) {*:entry, end_hook: []} else entry]};
        {*:cleared, input: stream.push(cleared.input,
            [*frame.end_hook, control_token("@popfilename"), {kind: "package-pop"}])}
    }
}

fn latex_provides(st, cmd, budget) any^ {
    let arg = stream.argument(st.input, true)^;
    let named = expand_argument_name({*:st, input: arg.stream}, arg.tokens, budget)^;
    let info = latex_optional(named.state.input)^;
    let current = {*:named.state, input: info.input};
    if (arg.error != null) fail(current, arg.error)
    else if (named.state.issue != null) named.state
    else if (info.issue != null) fail(current, info.issue)
    else {
        let suffix = if (cmd == "ProvidesPackage") ".sty" else if (cmd == "ProvidesClass") ".cls" else "";
        define_text_macro(current, "ver@" ++ named.name ++ suffix,
            if (info.present) token_text(current, info.tokens)^ else "")
    }
}

// Expl3 package declarations enable their syntax for the rest of the file.
fn latex_provides_expl(st, cmd, budget) any^ {
    let name = stream.argument(st.input, true)^;
    let date_arg = stream.argument(name.stream, true)^;
    let version = stream.argument(date_arg.stream, true)^;
    let description = stream.argument(version.stream, true)^;
    let current = {*:st, input: description.stream};
    if (name.error != null) fail(current, name.error)
    else if (date_arg.error != null) fail(current, date_arg.error)
    else if (version.error != null) fail(current, version.error)
    else if (description.error != null) fail(current, description.error)
    else {
        let named = expand_argument_name(current, name.tokens, budget)^;
        let dated = expand_argument_name(named.state, date_arg.tokens, budget)^;
        let numbered = expand_argument_name(dated.state, version.tokens, budget)^;
        let described = expand_argument_name(numbered.state, description.tokens, budget)^;
        if (described.state.issue != null) described.state
        else {
            let suffix = if (cmd == "ProvidesExplPackage") ".sty" else if (cmd == "ProvidesExplClass") ".cls" else "";
            let prefix = if (numbered.name == "" or starts_with(numbered.name, "v")) "" else "v";
            let value = dated.name ++ " " ++ prefix ++ numbered.name ++ " " ++ described.name;
            let defined = define_text_macro(described.state, "ver@" ++ named.name ++ suffix, value)^;
            expl3_syntax(defined, true)
        }
    }
}

fn option_list(value) any^ =>
    [for (part in (split(value, ",") ^ { [] }) where ((trim(part) != "") ^ { false })) trim(part)]

fn option_name(st) => if (st.current_package == null) "" else st.current_package

fn find_option(st, name) any^ {
    let matches = [for (entry in st.option_decls where entry.package == option_name(st) and entry.name == name) entry];
    if (len(matches) == 0) null else matches[0]
}

fn option_code(name, code) any^ {
    let opener = brace_token(true);
    let closer = brace_token(false);
    [control_token("def"), control_token("CurrentOption"), opener, *text_tokens(name), closer, *code]
}

fn execute_option_list(st, names, unknown, index = 0, tokens = []) any^ {
    if (index >= len(names)) {*:st, input: stream.push(st.input, tokens)}
    else {
        let name = names[index];
        let declared = find_option(st, name)^;
        let fallback = if (unknown) find_option(st, "*")^ else null;
        let selected = if (declared != null) declared else fallback;
        let next = if (unknown and selected == null and not st.current_is_class)
            {*:st, diagnostics: [*st.diagnostics, "latex-unknown-option"]} else st;
        let added = if (selected == null) [] else option_code(name, selected.code)^;
        execute_option_list(next, names, unknown, index + 1, [*tokens, *added])
    }
}

fn latex_declare_option(st, budget) any^ {
    let star = nonspace(st.input);
    let starred = other(star.token, "*");
    let remaining = if (starred) star.stream else if (star.token == null) star.stream else stream.push(star.stream, [star.token]);
    let name_arg = if (starred) {stream: remaining, tokens: [], error: null} else stream.argument(remaining, true)^;
    let named = if (starred) {state: {*:st, input: name_arg.stream}, name: "*"}
        else expand_argument_name({*:st, input: name_arg.stream}, name_arg.tokens, budget)^;
    let code = stream.argument(named.state.input, true)^;
    let current = {*:named.state, input: code.stream};
    if (name_arg.error != null) fail(current, name_arg.error)
    else if (named.state.issue != null) named.state
    else if (code.error != null) fail(current, code.error)
    else {*:current, option_decls: [*current.option_decls,
        {package: option_name(current), name: named.name, code: stored_tokens(code.tokens)}]}
}

fn latex_execute_options(st, budget) any^ {
    let arg = stream.argument(st.input, true)^;
    let named = expand_argument_name({*:st, input: arg.stream}, arg.tokens, budget)^;
    if (arg.error != null) fail(named.state, arg.error)
    else if (named.state.issue != null) named.state
    else execute_option_list(named.state, option_list(named.name)^, false)
}

fn option_declaration_tokens(entries, index = 0, tokens = []) any^ {
    if (index >= len(entries)) tokens
    else option_declaration_tokens(entries, index + 1,
        [*tokens, *option_code(entries[index].name, entries[index].code)^])
}

fn latex_process_options(st) any^ {
    let star = nonspace(st.input);
    let starred = other(star.token, "*");
    let remaining = if (starred) star.stream else if (star.token == null) star.stream else stream.push(star.stream, [star.token]);
    let current = {*:st, input: remaining};
    let local = option_list(if (st.current_options == null) "" else st.current_options)^;
    let global = if (st.current_is_class) [] else option_list(st.class_options)^;
    let chosen = [for (entry in st.option_decls where entry.package == option_name(current) and entry.name != "*" and
        ((contains(local, entry.name) ^ { false }) or (contains(global, entry.name) ^ { false }))) entry];
    let global_given = [for (name in global where find_option(current, name)^ != null) name];
    let local_rest = if (starred) local else [for (name in local where find_option(current, name)^ == null) name];
    if (starred) execute_option_list(current, [*global_given, *local_rest], true)
    else execute_option_list(current, local_rest, true, 0, option_declaration_tokens(chosen)^)
}

fn latex_pass_options(st, budget) any^ {
    let first = stream.argument(st.input, true)^;
    let options = expand_argument_name({*:st, input: first.stream}, first.tokens, budget)^;
    let second = stream.argument(options.state.input, true)^;
    let names = expand_argument_name({*:options.state, input: second.stream}, second.tokens, budget)^;
    if (first.error != null) fail(names.state, first.error)
    else if (options.state.issue != null) options.state
    else if (second.error != null) fail(names.state, second.error)
    else if (names.state.issue != null) names.state
    else {*:names.state, pass_options: [*names.state.pass_options,
        *[for (name in option_list(names.name)^) {name: name, options: options.name}]]}
}

fn latex_if_package_with(st, is_class, budget) any^ {
    let first = stream.argument(st.input, true)^;
    let name = expand_argument_name({*:st, input: first.stream}, first.tokens, budget)^;
    let second = stream.argument(name.state.input, true)^;
    let options = expand_argument_name({*:name.state, input: second.stream}, second.tokens, budget)^;
    let yes = stream.argument(options.state.input, true)^;
    let no = stream.argument(yes.stream, true)^;
    let current = {*:options.state, input: no.stream};
    if (first.error != null) fail(current, first.error)
    else if (name.state.issue != null) name.state
    else if (second.error != null) fail(current, second.error)
    else if (options.state.issue != null) options.state
    else if (yes.error != null) fail(current, yes.error)
    else if (no.error != null) fail(current, no.error)
    else {
        let matches = [for (entry in current.packages where entry.name == name.name and entry.is_class == is_class) entry];
        let requested = option_list(options.name)^;
        let available = if (len(matches) == 0) [] else option_list(matches[0].options)^;
        let missing = [for (item in requested where not (contains(available, item) ^ { false })) item];
        {*:current, input: stream.push(current.input, if (len(matches) > 0 and len(missing) == 0) yes.tokens else no.tokens)}
    }
}

fn latex_if_file_exists(st, include, budget) any^ {
    let arg = stream.argument(st.input, true)^;
    let name = expand_argument_name({*:st, input: arg.stream}, arg.tokens, budget)^;
    let yes = stream.argument(name.state.input, true)^;
    let no = stream.argument(yes.stream, true)^;
    let current = {*:name.state, input: no.stream};
    if (arg.error != null) fail(current, arg.error)
    else if (name.state.issue != null) name.state
    else if (yes.error != null) fail(current, yes.error)
    else if (no.error != null) fail(current, no.error)
    else {
        let exact = probe_resource(current, name.name, "local")^;
        let alternate = if (exact.available and not exact.found and exact.state.issue == null)
            probe_resource(exact.state, name.name ++ ".tex", "local")^ else exact;
        let chosen = if (exact.found) name.name else name.name ++ ".tex";
        if (alternate.state.issue != null) alternate.state
        else if (not alternate.available) unsupported(current, if (include) "InputIfFileExists" else "IfFileExists")
        else if (not alternate.found) {*:current, input: stream.push(current.input, no.tokens)}
        else if (not include) {*:current, input: stream.push(current.input, yes.tokens)}
        else {
            let fetched = captured_resource(current, chosen, "local")^;
            if (fetched.state.issue != null) fetched.state
            else {
                let opened = push_resource_reader(fetched)^;
                if (opened.issue != null) opened else {*:opened, input: stream.push(opened.input, yes.tokens)}
            }
        }
    }
}

fn halve_parameters(tokens, index = 0, out = []) any^ {
    if (index >= len(tokens)) out
    else {
        let paired = charcat(tokens[index], 6) and index + 1 < len(tokens) and charcat(tokens[index + 1], 6);
        halve_parameters(tokens, index + if (paired) 2 else 1, [*out, tokens[index]])
    }
}

fn latex_ifnextchar(st, star) any^ {
    let wanted = if (star) {stream: st.input, tokens: [], error: null} else stream.argument(st.input, true)^;
    let yes = stream.argument(wanted.stream, true)^;
    let no = stream.argument(yes.stream, true)^;
    let following = nonspace(no.stream);
    let current = {*:st, input: following.stream};
    if (wanted.error != null) fail(current, wanted.error)
    else if (yes.error != null) fail(current, yes.error)
    else if (no.error != null) fail(current, no.error)
    else {
        let matched = if (star) other(following.token, "*")
            else if (len(wanted.tokens) == 0 or following.token == null) false
            else if (wanted.tokens[0].kind == "char" and following.token.kind == "char")
                stream.same_token(wanted.tokens[0], following.token)
            else meanings_equal(meaning(current, wanted.tokens[0]), meaning(current, following.token))^;
        let rest = if (following.token == null or (star and matched)) current.input
            else stream.push(current.input, [following.token]);
        let branch = halve_parameters(if (matched) yes.tokens else no.tokens)^;
        {*:current, input: stream.push(rest, stored_tokens(branch))}
    }
}

fn latex_needs_format(st) any^ {
    let arg = stream.argument(st.input, true)^;
    let version_arg = latex_optional(arg.stream)^;
    let current = {*:st, input: version_arg.input};
    if (arg.error != null) fail(current, arg.error)
    else if (version_arg.issue != null) fail(current, version_arg.issue) else current
}

fn latex_documentclass(st, original, budget, cmd) any^ {
    let opt = latex_optional(st.input)^;
    let name_arg = stream.raw_argument(opt.input)^;
    let named = expand_argument_name({*:st, input: name_arg.stream}, name_arg.tokens, budget)^;
    let version_arg = latex_optional(named.state.input)^;
    let current = {*:named.state, input: version_arg.input};
    if (opt.issue != null) fail(current, opt.issue)
    else if (name_arg.error != null) fail(current, name_arg.error)
    else if (named.state.issue != null) named.state
    else if (version_arg.issue != null) fail(current, version_arg.issue)
    else {
        let opts = if (cmd == "LoadClassWithOptions" and current.current_options != null) current.current_options
            else if (opt.present) token_text(current, opt.tokens)^ else "";
        let initial = if (cmd == "documentclass")
            define_text_macro({*:current, class_options: opts}, "@classoptionslist", opts)^ else current;
        let adapter = contains(initial.adapters, named.name);
        let local = if (adapter) {state: initial, available: true, found: false}
            else probe_resource(initial, named.name ++ ".cls", "local")^;
        if (local.state.issue != null) local.state
        else if (not local.available) unsupported(initial, cmd)
        else if (local.found) {*:initial, input: stream.push(initial.input,
            [{kind: "package-load", name: named.name, options: opts, scope: "local", is_class: true}])}
        else {
            let recorded = record_package(initial, named.name, opts, true, if (adapter) "adapter" else "unknown")^;
            let with_chapter = if (contains(recorded.chaptered, named.name))
                assign_meaning(recorded, "control:chapter", {kind: "constructor", name: "chapter"}, true)^
                else recorded;
            let passed = if (cmd == "documentclass") [original, *opt.raw, *name_arg.raw, *version_arg.raw]
                else declaration_tokens("documentclass", opts, [named.name])^;
            {*:with_chapter, output: [*with_chapter.output, *passed]}
        }
    }
}

fn latex_newif(st) any^ {
    let target = nonspace(st.input);
    let current = {*:st, input: target.stream};
    if (not is_control(target.token) or target.token.kind != "control" or
        not starts_with(target.token.value, "if") or len(target.token.value) < 3)
        fail(current, "latex-newif")
    else {
        let base = slice(target.token.value, 2, len(target.token.value))^;
        let false_cond = assign_meaning(current, key(target.token), {kind: "primitive", name: "iffalse"}, false)^;
        let yes = {kind: "macro", count: 0, prefix: [], delimiters: [], brace_delim: false,
            long_ok: false, protected_flag: false,
            body: [control_token("let"), target.token, control_token("iftrue")]};
        let no = {*:yes, body: [control_token("let"), target.token, control_token("iffalse")]};
        let yes_state = assign_meaning(false_cond, "control:" ++ base ++ "true", yes, false)^;
        assign_meaning(yes_state, "control:" ++ base ++ "false", no, false)
    }
}

fn latex_verb(st, token) any^ {
    let source = stream.raw_reader(st.input, token)^;
    if (source == null) unsupported(st, "verb non-direct source")
    else {
        let scanned = reader.verb_line(source.reader)^;
        if (scanned.issue != null) unsupported(st, scanned.issue)
        else {
            let input = stream.replace_reader(st.input, source.index, scanned.reader)^;
            let span = {file: token.file, start: token.start, end: scanned.end,
                first_seq: token.seq, last_seq: source.reader.seq};
            let captured = {*:token, echo_span: span, echo_count: 1};
            {*:st, input: input, raw_inline_seen: true, output: [*st.output, captured]}
        }
    }
}

fn latex_constructor(st) any^ {
    let target = nonspace(st.input);
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else assign_meaning(current, key(target.token), {kind: "constructor", name: target.token.value}, true)
}

fn finish_definition_echo(st, after, tracing, emit) any^ {
    if (not tracing) after
    else {
        let traced = stream.end_trace(after.input)^;
        let finished = {*:after, input: traced.stream};
        if (after.issue != null or not emit) finished
        else if (not traced.valid) {*:finished, echo_incomplete: true}
        else {
            let found = [for (source in finished.sources where source.id == traced.file) source];
            if (len(found) == 0) {*:finished, echo_incomplete: true}
            else {
                let bytes = binary(found[0].data)^;
                if (traced.start < 0 or traced.end < traced.start or traced.end > len(bytes))
                    {*:finished, echo_incomplete: true}
                else {
                    let raw = utf8.decode(slice(bytes, traced.start, traced.end)^) ^ { null };
                    if (raw == null) {*:finished, echo_incomplete: true}
                    else {*:finished, output: [*finished.output, {kind: "source-span", value: raw,
                        file: traced.file, start: traced.start, end: traced.end, seq: traced.first_seq,
                        last_seq: traced.last_seq, catcodes: st.input.reader.catcodes}]}
                }
            }
        }
    }
}

fn echo_definition(st, token, cmd, budget) any^ {
    let tracing = st.echo_definitions and token.direct == true and token.file == st.main_file;
    let current = if (tracing) {*:st, input: stream.begin_trace(st.input, token, st.main_file)^} else st;
    let after = if (contains(["def", "gdef", "edef", "xdef"], cmd))
            after_assignment(current, define(current, effective_global(st, cmd == "gdef" or cmd == "xdef")^,
                false, false, cmd == "edef" or cmd == "xdef", budget)^)^
        else if (contains(["newcommand", "renewcommand", "providecommand", "DeclareRobustCommand"], cmd))
            latex_newcommand(current, cmd)^
        else if (contains(["NewDocumentCommand", "RenewDocumentCommand", "ProvideDocumentCommand", "DeclareDocumentCommand"], cmd))
            latex_document_command(current, cmd)^
        else latex_newenvironment(current, cmd, budget)^;
    finish_definition_echo(st, after, tracing, true)
}

fn echo_prefixed(st, token, cmd, budget) any^ {
    let tracing = st.echo_definitions and token.direct == true and token.file == st.main_file;
    let input = if (tracing) stream.begin_trace(st.input, token, st.main_file)^ else st.input;
    let current = {*:st, input: input, prefixed_definition: false};
    let after = after_assignment(current, prefixed(current, cmd == "global", cmd == "long", cmd == "protected", budget)^)^;
    finish_definition_echo(st, after, tracing, after.prefixed_definition == true)
}

fn direct_package_token(st, token) any^ => token.direct == true and
    any([for (source in st.sources) source.id == token.file and source.package == true])

fn step(st, budget) any^ {
    let read = stream.next(st.input);
    let raw = read.token;
    // keep the current main-file position when processing a non-main token.
    let t = if (raw != null and contains(["char", "control", "active"], raw.kind) and
            (raw.direct != true or raw.file != st.main_file))
        {*:raw, read_offset: read.stream.last_main_offset, read_file: st.main_file} else raw;
    let current = {*:st, input: read.stream};
    let cmd = primitive(st, t);
    let resolved = if (is_control(t)) meaning(st, t) else null;
    let assignment_global = effective_global(st, false)^;
    if (t == null) if (len(st.conditions) > 0) fail(current, "tex-incomplete-if") else {*:current, finished: true}
    else if ((charcat(t, 10) or (is_control(t) and t.value == "par")) and direct_package_token(st, t)) current
    else if (t.kind == "expl3-sort-end") {*:current, expl_sort_done: true}
    else if (t.kind == "counter-value-end") current
    else if (t.kind == "env-close") end_group(current, null, true, true)
    else if (t.kind == "package-load") start_package(current, t, budget)
    else if (t.kind == "package-end") finish_package_hook(current)
    else if (t.kind == "package-pop") finish_package(current)
    else if (t.kind == "document-close") {
        let emitted = environment_output(current, t.original, t.raw, "document", "end", t.echo, t.span)^;
        {*:current, output: [*current.output, *emitted],
            trailing_source: stream.root_tail(current.input), finished: true}
    }
    else if (t.noexpand == true) {*:current, output: [*st.output, {*:t, noexpand: false}]}
    else if (expandable(current, t)) expand_one(current, t, budget)
    // these native TeX commands remain tokens for the document adapters.
    else if (contains([" ", "underline", "overline", "indent", "noindent", "hfil", "hfill", "hss", "hfilneg",
        "vfil", "vfill", "vss", "vfilneg", "noalign", "omit", "span", "cr", "crcr", "-", "/", "noboundary",
        "mathord", "mathop", "mathbin", "mathrel", "mathopen", "mathclose", "mathpunct", "mathinner",
        "displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle", "limits", "nolimits", "displaylimits",
        "nonscript", "eqno", "leqno", "mathchoice", "left", "right", "middle", "over", "atop", "above",
        "overwithdelims", "atopwithdelims", "abovewithdelims"], cmd))
        {*:current, output: [*st.output, t]}
    else if (contains(["hskip", "vskip", "mskip", "kern", "mkern"], cmd))
        emit_spacing(current, t, cmd, budget)
    else if (contains(["hbox", "vbox", "vtop"], cmd)) begin_inline_box(current, budget, cmd)
    else if (cmd == "vcenter") begin_vcenter(current, budget)
    else if (contains(["raise", "lower", "moveleft", "moveright"], cmd)) move_inline_box(current, budget, cmd)
    else if (cmd == "shipout") ship_inline_box(current, budget)
    else if (contains(["leaders", "cleaders", "xleaders"], cmd)) discard_leader(current, budget)
    else if (cmd == "hrule" or cmd == "vrule") emit_rule(current, t, cmd, budget)
    else if (cmd == "accent") emit_accent(current, budget)
    else if (contains(["mathchar", "delimiter", "radical", "mathaccent"], cmd)) numeric_math(current, budget)
    else if (cmd == "insert" or cmd == "vadjust") begin_discard_group(current, budget, cmd == "insert")
    else if (cmd == "discretionary") emit_discretionary(current, budget)
    else if (cmd == "special" or cmd == "mark" or cmd == "marks")
        discard_mark_text(current, budget, cmd == "marks")
    else if (cmd == "penalty") {
        let scanned = scan_number(current, budget)^;
        scanned.state
    }
    else if (cmd == "unpenalty" or cmd == "unkern") current
    // native unskip removes only a final emitted space token.
    else if (cmd == "unskip") {
        let count = len(current.output);
        if (count > 0 and charcat(current.output[count - 1], 10))
            if (any([for (scope in current.groups) scope.discard_output != null and count <= scope.discard_output]))
                unsupported(current, "insert cross-boundary unskip")
            else {*:current, output: slice(current.output, 0, count - 1)^} else current
    }
    else if (contains(["def", "gdef", "edef", "xdef"], cmd)) echo_definition(current, t, cmd, budget)
    else if (contains(["patterns", "hyphenation", "parshape", "interlinepenalties", "clubpenalties", "widowpenalties", "displaywidowpenalties"], cmd))
        after_assignment(current, assign_paragraph_data(current, budget, cmd))
    else if (cmd == "let") after_assignment(current, assign_let(current, assignment_global))
    else if (cmd == "futurelet") after_assignment(current, assign_futurelet(current, assignment_global))
    else if (cmd == "catcode") after_assignment(current, assign_catcode(current, assignment_global, budget))
    else if (cmd == "lccode" or cmd == "uccode")
        after_assignment(current, assign_case_code(current, assignment_global, budget, cmd == "uccode"))
    else if (contains(["sfcode", "mathcode", "delcode"], cmd))
        after_assignment(current, assign_extra_code(current, assignment_global, budget, cmd))
    else if (primitives.parameter_kind(cmd) == "int") after_assignment(current, assign_parameter(current, assignment_global, budget, cmd))
    else if (primitives.parameter_kind(cmd) == "dimen") after_assignment(current, assign_dimen(current, assignment_global, budget, "param:" ++ cmd))
    else if (contains(["glue", "mu"], primitives.parameter_kind(cmd)))
        after_assignment(current, assign_glue(current, assignment_global, budget, "param:" ++ cmd, primitives.parameter_kind(cmd) == "mu"))
    else if (primitives.parameter_kind(cmd) == "toks")
        after_assignment(current, assign_toks(current, assignment_global, budget, parameter_index(cmd)))
    else if (cmd == "uppercase" or cmd == "lowercase") shift_case(current, budget, cmd == "uppercase")
    else if (st.script_expl3 and (cmd == "ExplSyntaxOn" or cmd == "ExplSyntaxOff"))
        after_assignment(current, expl3_syntax(current, cmd == "ExplSyntaxOn"))
    else if (st.script_expl3 and contains(["ProvidesExplPackage", "ProvidesExplClass", "ProvidesExplFile"], cmd))
        latex_provides_expl(current, cmd, budget)
    else if (st.script_expl3 and expl3.cs_definition(cmd))
        after_assignment(current, expl3_cs_define(current, cmd, budget))
    else if (st.script_expl3 and expl3.token_assignment(cmd))
        after_assignment(current, expl3_tl_assign(current, cmd, budget))
    else if (st.script_expl3 and expl3.sequence_assignment(cmd))
        after_assignment(current, expl3_seq_assign(current, cmd))
    else if (st.script_expl3 and expl3.sequence_sort(cmd))
        after_assignment(current, expl3_seq_sort(current, cmd, budget))
    else if (st.script_expl3 and expl3.sort_return(cmd))
        if (not st.expl_sort_active) fail(current, "expl3-sort-return-outside")
        else if (st.expl_sort_result != null) fail(current, "expl3-sort-return-multiple")
        else {*:current, expl_sort_result: if (cmd == "sort_return_swapped:") "swap" else "same"}
    else if (st.latex and cmd == "newcounter") latex_newcounter(current, t, budget)
    else if (st.latex and contains(["setcounter", "addtocounter", "stepcounter", "refstepcounter"], cmd))
        latex_counter_command(current, t, cmd, budget)
    else if (st.latex and contains(["newcommand", "renewcommand", "providecommand", "DeclareRobustCommand",
        "newenvironment", "renewenvironment", "NewDocumentCommand", "RenewDocumentCommand",
        "ProvideDocumentCommand", "DeclareDocumentCommand"], cmd)) echo_definition(current, t, cmd, budget)
    else if (st.latex and (cmd == "AtBeginDocument" or cmd == "AtEndDocument"))
        latex_document_hook(current, cmd == "AtBeginDocument")
    else if (st.latex and contains(["usepackage", "RequirePackage", "RequirePackageWithOptions"], cmd))
        latex_usepackage(current, t, budget)
    else if (st.latex and cmd == "DeclareOption") latex_declare_option(current, budget)
    else if (st.latex and cmd == "ExecuteOptions") latex_execute_options(current, budget)
    else if (st.latex and cmd == "ProcessOptions") latex_process_options(current)
    else if (st.latex and (cmd == "PassOptionsToPackage" or cmd == "PassOptionsToClass")) latex_pass_options(current, budget)
    else if (st.latex and (cmd == "AtEndOfPackage" or cmd == "AtEndOfClass")) package_end_hook(current)
    else if (st.latex and contains(["ProvidesPackage", "ProvidesClass", "ProvidesFile"], cmd))
        latex_provides(current, cmd, budget)
    else if (st.latex and (cmd == "IfFileExists" or cmd == "InputIfFileExists"))
        latex_if_file_exists(current, cmd == "InputIfFileExists", budget)
    else if (st.latex and contains(["(", ")", "[", "]"], cmd)) latex_math_switch(current, t, cmd)
    else if (st.latex and contains(["@ifnextchar", "kernel@ifnextchar", "@ifstar"], cmd))
        latex_ifnextchar(current, cmd == "@ifstar")
    else if (st.latex and cmd == "NeedsTeXFormat") latex_needs_format(current)
    else if (st.latex and contains(["documentclass", "LoadClass", "LoadClassWithOptions"], cmd))
        latex_documentclass(current, t, budget, cmd)
    else if (st.latex and cmd == "verb") latex_verb(current, t)
    else if (st.latex and cmd == "begin") latex_begin(current, t, budget)
    else if (st.latex and cmd == "end") latex_end(current, t, budget)
    else if (cmd == "newif" and st.latex) latex_newif(current)
    else if (cmd == "lambdaconstructor" and st.latex) latex_constructor(current)
    else if (cmd == "font") after_assignment(current, define_font(current, assignment_global, budget))
    else if (cmd == "letterspacefont") after_assignment(current, assign_letterspace_font(current, assignment_global, budget))
    else if (cmd == "read" or cmd == "readline") after_assignment(current, read_stream_assignment(current, assignment_global, budget, cmd == "readline"))
    else if (cmd == "interactionmode" or page_dimension(cmd)) after_assignment(current, assign_page_state(current, budget, cmd))
    else if (contains(["batchmode", "nonstopmode", "scrollmode", "errorstopmode"], cmd))
        {*:current, page_state_assignment_seen: true}
    else if (cmd == "fontdimen") after_assignment(current, assign_font_dimen(current, budget))
    else if (cmd == "hyphenchar" or cmd == "skewchar")
        after_assignment(current, assign_font_character(current, budget, cmd))
    else if (contains(["textfont", "scriptfont", "scriptscriptfont"], cmd))
        after_assignment(current, assign_font_family(current, budget, cmd))
    else if (cmd == "nullfont") after_assignment(current, select_font(current, assignment_global, 0))
    else if (cmd == "count") after_assignment(current, assign_count(current, assignment_global, budget, null))
    else if (cmd == "countdef") after_assignment(current, assign_countdef(current, assignment_global, budget))
    else if (cmd == "dimen") after_assignment(current, assign_dimen(current, assignment_global, budget, null))
    else if (cmd == "dimendef") after_assignment(current, assign_dimendef(current, assignment_global, budget))
    else if (cmd == "skip" or cmd == "muskip") after_assignment(current, assign_glue(current, assignment_global, budget, null, cmd == "muskip"))
    else if (cmd == "skipdef" or cmd == "muskipdef") after_assignment(current, assign_gluedef(current, assignment_global, budget, cmd == "muskipdef"))
    else if (contains(["advance", "multiply", "divide"], cmd))
        after_assignment(current, arithmetic(current, assignment_global, budget, cmd))
    else if (cmd == "chardef" or cmd == "mathchardef")
        after_assignment(current, assign_chardef(current, assignment_global, budget, cmd == "mathchardef"))
    else if (cmd == "toks") after_assignment(current, assign_toks(current, assignment_global, budget, null))
    else if (cmd == "everyeof") after_assignment(current, assign_toks(current, assignment_global, budget, -1))
    else if (cmd == "toksdef") after_assignment(current, assign_toksdef(current, assignment_global, budget))
    else if (resolved.kind == "font") after_assignment(current, select_font(current, assignment_global, resolved.index))
    else if (resolved.kind == "register") after_assignment(current, assign_count(current, assignment_global, budget, resolved.index))
    else if (resolved.kind == "dimension-register") after_assignment(current, assign_dimen(current, assignment_global, budget, resolved.index))
    else if (resolved.kind == "glue-register") after_assignment(current, assign_glue(current, assignment_global, budget, resolved.index, resolved.mu))
    else if (resolved.kind == "token-register") after_assignment(current, assign_toks(current, assignment_global, budget, resolved.index))
    else if (cmd == "pdfsetrandomseed") set_random_seed(current, budget)
    else if (cmd == "pdfresettimer") {*:current, pdf_random_seen: true}
    else if (cmd == "immediate") immediate(current, budget)
    else if (cmd == "message" or cmd == "errmessage") terminal_message(current, budget, cmd == "errmessage")
    else if (cmd == "char") emit_character(current, budget)
    else if (cmd == "write") write_message(current, budget)
    else if (cmd == "openout" or cmd == "closeout") output_stream(current, budget, cmd == "openout")
    else if (cmd == "openin" or cmd == "closein") input_stream_command(current, budget, cmd == "openin")
    else if (cmd == "showtokens") show_tokens(current, budget)
    else if (cmd == "show") show_meaning(current)
    else if (cmd == "showthe") show_the(current, budget)
    else if (cmd == "showbox") show_box(current, budget)
    else if (contains(["showlists", "showgroups", "showifs"], cmd)) {*:current, inspection_seen: true}
    else if (cmd == "end" or (cmd == "@@end" and st.latex))
        if (len(st.conditions) > 0) fail(current, "tex-incomplete-if") else {*:current, finished: true}
    else if (cmd == "afterassignment") save_after_assignment(current)
    else if (cmd == "aftergroup") save_aftergroup(current)
    else if (cmd == "begingroup") begin_group(current, null, true)
    else if (cmd == "endgroup") end_group(current, null, true)
    else if (cmd == "global" or cmd == "long" or cmd == "protected") echo_prefixed(current, t, cmd, budget)
    else if (resolved.kind == "constructor") {*:current, output: [*st.output, t]}
    else if (resolved.kind == "relax" or cmd == "relax") current
    else if (resolved.kind == "char") {*:current, input: stream.push(current.input,
        [{kind: "char", value: resolved.value, cat: resolved.cat, start: t.start, end: t.end}])}
    else if (resolved.kind == "chardef") {
        // synthesized characters need the current main-source position.
        {*:current, output: [*st.output,
            {kind: "char", value: chr(resolved.value), cat: 12, start: t.start, end: t.end,
                read_offset: current.input.last_main_offset, read_file: current.main_file}]}
    }
    else if (resolved.kind == "mathchardef")
        {*:current, output: [*st.output, t], mathchardef_emission_seen:
            current.mathchardef_emission_seen == true or mathchardef_name_needs_mapping(t)^}
    else if (cmd == "par") {*:current, output: [*st.output, t]}
    else if (cmd != null) unsupported(current, cmd)
    else if (st.script_expl3 and is_control(t) and index_of(t.value, ":") != null)
        unsupported(current, t.value)
    else if (charcat(t, 3)) math_shift(current, t)
    else if (charcat(t, 1)) begin_group(current, t, false)
    else if (charcat(t, 2)) end_group(current, t, false)
    else {*:current, output: [*st.output, t]}
}

// the native LaTeX scanner splits control words at non-letter characters.
fn mathchardef_name_needs_mapping(t) any^ {
    let size = len(t.value)^;
    if (t.kind != "control") false
    else if (size == 0) true
    else if (size == 1) ord(t.value) > 127
    else {
        let positions = 0 to size - 1;
        any([for (i in positions)
            slice(t.value, i, i + 1)^ != "@" and reader.catcode(slice(t.value, i, i + 1)^, {}) != 11])
    }
}

fn run(st, budget) any^ {
    if (st.issue != null or st.finished == true) terminal_flush(st)
    else run(step(st, budget), budget)
}

pub fn expand(input, budget = 20000000, options = null) any^ =>
    run(result(input, {}, [], [], 0, null, options), budget)

// The opt-in script profile replaces the kernel's full-package bootstrap.
pub fn enable_script_expl3(st) any^ {
    let enabled = {*:st, script_expl3: true};
    let names = ["ExplSyntaxOn", "ExplSyntaxOff", "ProvidesExplPackage", "ProvidesExplClass", "ProvidesExplFile"];
    let primitives = enable_expl_primitives(enabled, names)^;
    let constants = enable_expl_constants(primitives, [
        {name: "c_zero_int", kind: "chardef", value: 0},
        {name: "c_one_int", kind: "mathchardef", value: 1}])^;
    enable_expl_scratch(constants, ["l_tmpa_tl", "l_tmpb_tl", "g_tmpa_tl", "g_tmpb_tl"])
}

// preserve the distinct TeX meanings of expl3's non-allocating constants.
fn enable_expl_constants(st, values, index = 0) any^ {
    if (index >= len(values)) st
    else {
        let constant = values[index];
        let name = "control:" ++ constant.name;
        let next = if (st.definitions[name] != null) st else
            assign_meaning(st, name, {kind: constant.kind, value: constant.value}, true)^;
        enable_expl_constants(next, values, index + 1)
    }
}

// expl3 declares these standard scratch token lists at initialization.
fn enable_expl_scratch(st, names, index = 0) any^ {
    if (index >= len(names)) st
    else {
        let name = "control:" ++ names[index];
        let next = if (st.definitions[name] != null) st else assign_meaning(st, name, expl3.token_list([]), true)^;
        enable_expl_scratch(next, names, index + 1)
    }
}

fn enable_expl_primitives(st, names, index = 0) any^ {
    if (index >= len(names)) st
    else {
        let name = names[index];
        let next = assign_meaning(st, "control:" ++ name, {kind: "primitive", name: name}, true)^;
        enable_expl_primitives(next, names, index + 1)
    }
}

// Continue from a completed preload while retaining its definitions and scoped values.
pub fn resume(st, source, name = "<input>", budget = 20000000) any^ {
    if (st.issue != null) st
    else if (len(st.groups) != 0 or len(st.conditions) != 0) fail(st, "tex-incomplete-preload")
    else {
        let id = next_source_id(st)^;
        let next_reader = {*:reader.reader(source, id), origin: "local",
            catcodes: st.input.reader.catcodes, endline: parameter_value(st, "endlinechar")};
        let next_input = {*:stream.from_reader(next_reader), everyeof: st.input.everyeof};
        let resumed = {*:st, input: next_input, finished: false, output: [], messages: [], terminal_line: "", expansions: 0,
            main_file: id, echo_incomplete: false, generated_text_mapping_seen: false, mathchardef_emission_seen: false,
            sources: [*st.sources, {id: id, name: name, origin: "local", data: source}]};
        run(resumed, budget)
    }
}
