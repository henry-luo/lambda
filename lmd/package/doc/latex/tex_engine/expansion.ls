// Experimental TeX macro expansion. Unsupported primitives remain outside this API.
import stream: .stream
import reader: .reader
import primitives: .primitives
import expl3: .expl3
import regex: .regex
import fp: .fp
import dimension: .dimension
import glue: .glue
import pdfstrings: .pdfstrings
import md5: .md5
import message_catalog: .message_catalog
import utf8: lambda.io.utf8
import path: lambda.io.path

// Core state fields are common to every expansion outcome; package-specific fields remain open.
pub type State = {input: map, definitions: map, groups: array, output: map[],
    expansions: int, issue: string?, diagnostics: string[], messages: string[],
    sources: map[], main_file: int}
type NumberScan = {state: State, value: int, token?: map?, order?: int}
type GlueScan = {state: State, value: map}
type ConditionalScan = {state: State, delimiter: string?}
type WordScan = {state: State, found: bool}
type NameScan = {state: State, name: string?}
type SignedScan = {state: State, token: map?, negative: bool}
type StreamLine = {state: State, opened: bool, eof: bool, text: string}
type TokenBalance = {balance: int, tokens: map[]}
type TokenRead = {state: State, tokens: map[], balance?: int}
let StreamStep = stream.StreamStep;

// Normalize external options with a type-match arm that proves the resulting map.
pub fn options_map(value) map => match value { case map: ~ default: {} }

fn result(input: map, definitions: map, groups: array, output: map[], count: int, issue: string? = null, options: map? = null) State =>
    {input: input, definitions: definitions, groups: groups, output: output,
        latex: options.latex == true, math_mode: options.math_mode == true, script_expl3: options.script_expl3 == true,
        auto_expl3: options.auto_expl3 == true,
        constructor_argument_mode: options.constructor_argument_mode == true, expl_syntax: null, adapters: if (options.adapters == null) [] else options.adapters,
        chaptered: if (options.chaptered == null) [] else options.chaptered, packages: [], class_options: "",
        in_document: false, base_path: if (options.base_path is string) options.base_path else null,
        begin_hooks: [], end_hooks: [], package_frames: [],
        counter_resets: [], engine_counters: [], output_streams: [], input_streams: {}, font_families: {}, random_seed: 12345,
        current_package: null, current_options: null, current_is_class: false, option_decls: [], pass_options: [],
        math_depth: 0, display_math: false, main_file: input.reader.file_id,
        echo_definitions: options.echo_definitions == true, echo_incomplete: false,
        conditions: [], counts: {}, dimens: {}, skips: {}, muskips: {}, boxes: {}, box_serial: 0, box_metrics: {}, box_metrics_reported: false, toks: {}, lowercase: {}, uppercase: {}, code_tables: {sfcode: {}, mathcode: {}, delcode: {}},
        expression_arithmetic_error: false,
        parameters: {endlinechar: input.reader.endline, escapechar: 92, mag: 1000, tolerance: 10000,
            newlinechar: if (options.latex == true) -1 else 0}, after_assignment: null, token_text_scan: false,
        resources: options.resources, resource_requests: options.resource_requests == true,
        host_no_base: options.host_no_base == true,
        resources_seen: {}, resource_ids: {}, resource_count: 0, resource_bytes: 0,
        sources: [{id: input.reader.file_id, name: if (options.source_name == null) "<input>" else options.source_name,
            origin: if (input.reader.origin == "bundled") "bundled" else "local", data: input.reader.source}],
        fonts: [{name: "nullfont", size: 0, param_base: 0, param_count: 7, params: {}, hyphen_char: 45, skew_char: -1,
            ident: {kind: "control", value: "nullfont", cat: null, start: -1, end: -1}}],
        font_memory: 7, current_font: 0, diagnostics: [], diagnostic_records: [],
        messages: [], terminal_line: "", expansions: count, issue: issue}
fn fail(st: State, code: string) State => {*:st, issue: code}
fn unsupported(st: State, name: string) State => if (st.issue != null) st
    else {*:st, issue: "tex-unsupported-primitive", unsupported: name}
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
fn nonspace(input: map) StreamStep^ {
    let read = stream.next(input)^;
    if (charcat(read.token, 10)) nonspace(read.stream)^ else read
}

fn parameter_token(index, start = -1, end = -1) =>
    {kind: "parameter", value: index, cat: null, start: start, end: end}

// invalid parameters remain literal; primitive and LaTeX callers diagnose them differently.
fn body_params(tokens, count, index = 0, output = [], mode = null, errors = []) any^ {
    if (index >= len(tokens)) {tokens: output, issue: null, errors: errors}
    else {
        let t = tokens[index];
        // token lists inserted into an expanded definition bypass parameter parsing.
        if (t.definition_literal_parameter == true)
            body_params(tokens, count, index + 1, [*output, {*:t, definition_literal_parameter:false}], mode, errors)
        else if (not charcat(t, 6)) body_params(tokens, count, index + 1, [*output, t], mode, errors)
        else if (index + 1 >= len(tokens)) {
            if (mode == null) {tokens: output, issue: "tex-illegal-param"}
            else body_params(tokens, count, index + 1, [*output, t], mode,
                if (mode == "tex") [*errors, {next:null}] else errors)
        }
        else {
            let n = tokens[index + 1];
            let digit = if (n.kind == "char" and (mode == "latex" or n.cat == 12)) index_of("123456789", n.value) else null;
            if (charcat(n, 6)) body_params(tokens, count, index + 2, [*output, t], mode, errors)
            else if (digit == null or digit >= count) {
                if (mode == null) {tokens: output, issue: "tex-illegal-param"}
                else body_params(tokens, count, index + 1, [*output, t], mode, [*errors, {next:n}])
            }
            else body_params(tokens, count, index + 2,
                [*output, parameter_token(digit, t.start, n.end)], mode, errors)
        }
    }
}

fn append_delimiter(delimiters, token) any^ {
    let tail = len(delimiters) - 1;
    [*slice(delimiters, 0, tail), [*delimiters[tail], token]]
}

fn set_definition(definitions, name, value) => {*:definitions, *:map([name, value])}

fn assign_meaning(st: State, name, value, global) State^ =>
    {*:st, definitions: set_definition(st.definitions, name, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, definitions: set_definition(scope.definitions, name, value)}] else st.groups}

// native keeps the scanned signature when input ends before the body.
fn unfinished_signature(input, count, prefix, delimiters, diagnostics) =>
    {input:input,issue:null,diagnostics:diagnostics,count:count,prefix:prefix,
        delimiters:delimiters,brace_delim:false,ended:true}

fn signature(input, count, prefix = [], delimiters = [], diagnostics = []) any^ {
    let read = stream.next(input)^;
    let t = read.token;
    if (t == null) unfinished_signature(read.stream,count,prefix,delimiters,diagnostics)
    else if (charcat(t, 1)) {input: stream.push(read.stream, [t]), issue: null, diagnostics: diagnostics,
        count: count, prefix: prefix, delimiters: delimiters, brace_delim: false}
    else if (charcat(t, 6)) {
        let digit = stream.next(read.stream)^;
        let expected = if (count < 9) slice("123456789", count, count + 1) else "";
        if (charcat(digit.token, 1))
            {input: stream.push(digit.stream, [digit.token]), issue: null, diagnostics: diagnostics, count: count,
                prefix: prefix, delimiters: delimiters, brace_delim: true, brace: digit.token}
        else if (digit.token == null) unfinished_signature(digit.stream,count,prefix,delimiters,diagnostics)
        else if (count >= 9) signature(digit.stream, count, prefix, delimiters,
            [*diagnostics, {input:digit.stream, code:"tex-nine-params", message:"You already have nine parameters"}])
        else if (digit.token.kind != "char" or digit.token.cat != 12 or digit.token.value != expected)
            // native re-reads the bad number as the delimiter for the new parameter.
            signature(stream.push(digit.stream,[digit.token]), count + 1, prefix, [*delimiters, []],
                [*diagnostics, {input:digit.stream, code:"tex-param-order", message:"Parameters must be numbered consecutively"}])
        else signature(digit.stream, count + 1, prefix, [*delimiters, []], diagnostics)
    }
    else if (charcat(t, 2)) {input: read.stream, issue: "tex-extra-brace", diagnostics: diagnostics}
    else if (count == 0) signature(read.stream, count, [*prefix, t], delimiters, diagnostics)
    else signature(read.stream, count, prefix, append_delimiter(delimiters, t), diagnostics)
}

// signature scanning is raw; captured stream states preserve each error location.
fn signature_diagnostics(st: State, events, index = 0) State^ {
    if (index >= len(events)) st
    else {
        let event = events[index];
        let noted = record_diagnostic({*:st,input:event.input},event.code,event.message)^;
        signature_diagnostics({*:st,diagnostics:noted.diagnostics,diagnostic_records:noted.diagnostic_records},events,index + 1)
    }
}

// scan raw and expanded definition bodies through the same parameter logic.
fn definition_read(st, budget, xpand) any^ {
    if (xpand) expanded_next(st, budget, true)
    else {
        let read = stream.next(st.input)^;
        {state:{*:st, input:read.stream}, token:read.token}
    }
}

fn definition_tokens_inner(st, budget, depth = 1, output = [], parameter_count = null, xpand = true, recover_eof = false) any^ {
    let read = definition_read(st, budget, xpand)^;
    let t = read.token;
    if (read.state.issue != null) {state: read.state, tokens: output}
    // general expanded text needs separate source-provenance recovery at EOF.
    else if (t == null) if (parameter_count == null and not recover_eof)
        {state:fail(read.state,"tex-runaway-definition"),tokens:output}
        else {state:record_diagnostic(read.state,"tex-runaway","File ended while scanning a text")^,tokens:output}
    else if (xpand and t.direct_toks == true) {
        let stored = {*:t, noexpand: false, direct_toks: false, trace_internal: false};
        definition_tokens_inner(read.state, budget, depth, [*output, stored], parameter_count, xpand, recover_eof)
    }
    else if (charcat(t, 2) and depth == 1) {state: read.state, tokens: output}
    else if (parameter_count != null and charcat(t, 6)) {
        // expanded definitions expand the following token before classifying #.
        let next = definition_read({*:read.state, token_text_scan:false}, budget, xpand)^;
        let current = {*:next.state, token_text_scan:read.state.token_text_scan};
        let n = next.token;
        let digit = if (n != null and n.kind == "char" and n.cat == 12) index_of("123456789", n.value) else null;
        let literal = if (xpand) {*:t,noexpand:false,trace_internal:false} else t;
        if (current.issue != null) {state:current,tokens:output}
        // end of input during parameter lookahead stops before appending #.
        else if (n == null) {state:current,tokens:output}
        else if (charcat(n,6)) definition_tokens_inner(current,budget,depth,[*output,literal],parameter_count,xpand,recover_eof)
        else if (digit != null and digit < parameter_count)
            definition_tokens_inner(current,budget,depth,[*output,parameter_token(digit,t.start,n.end)],parameter_count,xpand,recover_eof)
        else {
            // buffered argument tokens use the current reader position for errors.
            let origin = if (not xpand and n.direct == true and
                stream.raw_reader(read.state.input,n)^ != null) n else null;
            let diagnosed=record_diagnostic(current,"tex-illegal-param","Illegal parameter number in definition",origin)^;
            definition_tokens_inner(put_back(diagnosed,n),budget,depth,[*output,literal],parameter_count,xpand,recover_eof)
        }
    }
    else {
        // expanded tokens must mark foreign expansion during a later definition.
        let stored = if (xpand) {*:t, noexpand:false, trace_internal:false} else t;
        definition_tokens_inner(read.state, budget,
            depth + if (charcat(t, 1)) 1 else if (charcat(t, 2)) -1 else 0,
            [*output, stored], parameter_count, xpand, recover_eof)
    }
}

fn definition_tokens(st, budget, parameter_count = null, xpand = true, recover_eof = false) any^ {
    let scanned = definition_tokens_inner({*:st, token_text_scan: xpand}, budget, 1, [], parameter_count, xpand, recover_eof)^;
    {*:scanned, state: {*:scanned.state, token_text_scan: st.token_text_scan}}
}

fn define(st: State, global, long_ok, protected_flag, xpand, budget) State^ {
    let name = nonspace(st.input)^;
    if (not is_control(name.token)) fail({*:st, input: name.stream}, "tex-missing-cs")
    else {
        let sig = signature(name.stream, 0)^;
        let checked = if (len(sig.diagnostics) == 0) st else signature_diagnostics(st,sig.diagnostics)^;
        if (sig.issue != null) fail({*:checked,input:sig.input}, sig.issue)
        else {
            let opening = if (sig.ended) null else stream.next(sig.input)^;
            let scan = if (sig.ended) {state:{*:checked,input:sig.input},tokens:[]}
                else definition_tokens({*:checked, input:opening.stream}, budget, sig.count, xpand)^;
            if (scan.state.issue != null) scan.state
            else {
                let k = key(name.token);
                let value = {kind: "macro", count: sig.count, prefix: sig.prefix, delimiters: sig.delimiters,
                    brace_delim: sig.brace_delim, long_ok: long_ok, protected_flag: protected_flag,
                    body: if (sig.brace_delim) [*scan.tokens, sig.brace] else scan.tokens};
                assign_meaning(scan.state, k, value, global)
            }
        }
    }
}

fn match_prefix(input, prefix, index = 0) any^ {
    if (index == len(prefix)) {input: input, issue: null}
    else {
        let read = stream.next(input)^;
        if (read.token == null) {input: read.stream, issue: "tex-macro-mismatch", eof: true}
        else if (not stream.same_token(read.token, prefix[index]))
            {input: stream.push(read.stream, [read.token]), issue: "tex-macro-mismatch", eof: false}
        else match_prefix(read.stream, prefix, index + 1)
    }
}

fn collect_args(input, delimiters, brace_delim, long_ok, args = []) any^ {
    if (len(args) >= len(delimiters)) {input: input, args: args, issue: null}
    else {
        let arg = stream.delimited(input, delimiters[len(args)], long_ok,
            brace_delim and len(args) + 1 == len(delimiters))^;
        if (arg.error != null) {input: arg.stream, args: args, issue: arg.error, cause:arg.cause}
        else collect_args(arg.stream, delimiters, brace_delim, long_ok, [*args, arg.tokens])
    }
}

fn stored_tokens(tokens) => [for (t in tokens) {*:t, direct: false}]

// Delimited xparse arguments retain braces and nest matching delimiter pairs.
fn document_delimited_scan(input, opening, closing, depth = 0, nested = 0, tokens = []) any^ {
    let read = stream.next(input)^;
    let token = read.token;
    if (token == null) {input:read.stream,tokens:tokens}
    else {
        let level = depth + if(charcat(token,1)) 1 else if(charcat(token,2)) -1 else 0;
        let open_match = level == 0 and token.kind == opening.kind and token.value == opening.value and
            opening.value != closing.value;
        let close_match = level == 0 and token.kind == closing.kind and token.value == closing.value;
        if (close_match and nested == 0) {input:read.stream,tokens:tokens}
        else document_delimited_scan(read.stream,opening,closing,level,
            nested + if(open_match) 1 else if(close_match) -1 else 0,[*tokens,token])
    }
}

// Body arguments stop before the matching end so the environment can close normally.
fn document_body_scan(input, name, depth = 0, tokens = []) any^ {
    let read = stream.next(input)^;
    let token = read.token;
    if (token == null) {input:read.stream,tokens:tokens}
    else if (token.kind == "control" and contains(["begin","end"],token.value)) {
        let next = stream.next(read.stream)^;
        let argument = if (charcat(next.token,1)) stream.raw_argument(stream.push(next.stream,[next.token]))^
            else {stream:next.stream,raw:if(next.token == null) [] else [next.token]};
        let text = join([for(t in (argument.raw)^ where t.kind == "char" and not charcat(t,1)^ and not charcat(t,2)^) t.value],"");
        let same = text == name;
        if (same and token.value == "end" and depth == 0)
            {input:stream.push(argument.stream,[token,*argument.raw]),tokens:tokens}
        else document_body_scan(argument.stream,name,depth + if(same) if(token.value == "begin") 1 else -1 else 0,
            [*tokens,token,*argument.raw])
    }
    else document_body_scan(read.stream,name,depth,[*tokens,token])
}

// Undeclared optional values use the native xparse sentinel.
fn document_default(spec) any^ => if (contains(["o","g","d","r"],spec.kind))
    text_tokens("-NoValue-") else stored_tokens(spec.default)

// xparse signatures scan each argument in order, including boolean tests.
fn collect_document_args(st, input, specs, index = 0, args = [], missing = []) any^ {
    if (index >= len(specs)) {input:input, args:args, issue:null, missing:missing}
    else {
        let spec = specs[index];
        if (spec.kind == "s" or spec.kind == "t") {
            let read = nonspace(input)^;
            let hit = if (read.token == null) false else if (spec.kind == "s") other(read.token,"*")
                else stream.same_token(read.token,spec.want);
            let next = if (hit or read.token == null) read.stream else stream.push(read.stream,[read.token]);
            collect_document_args(st,next,specs,index + 1,[*args,[control_token(if(hit) "BooleanTrue" else "BooleanFalse")]],missing)
        }
        else if (spec.kind == "O" or spec.kind == "o") {
            let arg = latex_optional(input)^;
            if (arg.issue != null) {input:arg.input,args:args,issue:arg.issue,
                cause:if(arg.issue == "tex-extra-brace") "brace" else "eof", optional_issue:true, missing:missing}
            else collect_document_args(st,arg.input,specs,index + 1,
                [*args,if(arg.present) arg.tokens else document_default(spec)^],missing)
        }
        else if (contains(["d","D","r","R"],spec.kind)) {
            let read = nonspace(input)^;
            let hit = stream.same_token(read.token,spec.open);
            let restored = if (hit or read.token == null) read.stream else stream.push(read.stream,[read.token]);
            let arg = if (hit) document_delimited_scan(restored,spec.open,spec.close)^
                else {input:restored,tokens:document_default(spec)^};
            let failures = if (not hit and (spec.kind == "r" or spec.kind == "R")) [*missing,read.stream] else missing;
            collect_document_args(st,arg.input,specs,index + 1,[*args,arg.tokens],failures)
        }
        else if (spec.kind == "e" or spec.kind == "E")
            collect_document_args(st,input,specs,index + 1,[*args,[]],missing)
        else if (spec.kind == "b") {
            let body = document_body_scan(input,spec.name)^;
            collect_document_args(st,body.input,specs,index + 1,[*args,body.tokens],missing)
        }
        else if (spec.kind == "g" or spec.kind == "G") {
            let read = nonspace(input)^;
            let restored = if (read.token == null) read.stream else stream.push(read.stream,[read.token]);
            if (charcat(read.token,1)) {
                let arg = stream.delimited(restored,[],true,false)^;
                if (arg.error != null) {input:arg.stream,args:args,issue:arg.error,cause:arg.cause,missing:missing}
                else collect_document_args(st,arg.stream,specs,index + 1,[*args,arg.tokens],missing)
            }
            else collect_document_args(st,restored,specs,index + 1,
                [*args,document_default(spec)^],missing)
        }
        else {
            let arg = stream.delimited(input,[],true,false)^;
            if (arg.error != null) {input:arg.stream,args:args,issue:arg.error,cause:arg.cause,missing:missing}
            else {
                // Native converts verbatim arguments to category-other tokens after scanning.
                let tokens = if(spec.kind == "v") text_tokens(token_text({*:st,input:arg.stream},arg.tokens)^)^
                    else arg.tokens;
                collect_document_args(st,arg.stream,specs,index + 1,[*args,tokens],missing)
            }
        }
    }
}

// spread after selecting the token array so an empty argument contributes nothing.
fn substitute(body, args) => [for (t in body)
    *(if (t.kind == "parameter") args[t.value] else [t])]

// native formats control names in a 128-byte C buffer, including slash and terminator.
fn macro_diagnostic_name(call) any^ {
    if (call == null) null
    else if (call.kind == "active") call.value
    else {
        let bytes = binary(call.value)^;
        let prefix = slice(bytes,0,min(len(bytes),126))^;
        let zeros = [for (i,b in prefix where b == 0) i];
        let end = if (len(zeros) == 0) len(prefix) else zeros[0];
        "\\" ++ parse(slice(prefix,0,end)^,{type:"text"})^
    }
}

// failed argument scans discard the call but return their lookahead to TeX.
fn recover_macro_argument(st: State, issue, cause, call, count_expansion = true) State^ {
    let current = {*:st, expansions:st.expansions + if (count_expansion) 1 else 0};
    let name = macro_diagnostic_name(call)^;
    if (cause == "missing") current
    else if (name == null or not contains(["eof","paragraph","brace"],cause)) fail(current,issue)
    else {
        let message = if (cause == "eof") "File ended while scanning use of " ++ name
            else if (cause == "paragraph") "Paragraph ended before " ++ name ++ " was complete"
            else "Argument of " ++ name ++ " has an extra }";
        // The last fresh token, including an emitted endline, owns the native diagnostic offset.
        record_diagnostic(current,issue,message)
    }
}

// Diagnose each missing required delimiter at the token that was peeked.
fn document_missing_diagnostics(st: State, missing, index = 0) State^ {
    if (missing == null or index >= len(missing)) st
    else {
        let noted = record_diagnostic({*:st,input:missing[index]},"latex-missing-arg",
            "missing required delimited argument")^;
        document_missing_diagnostics({*:noted,input:st.input},missing,index + 1)
    }
}

fn expand_macro(st: State, macro, budget, call = null) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let matched = match_prefix(st.input, macro.prefix);
        if (matched.issue != null) {
            // A prefix mismatch discards the call but leaves the unmatched token for TeX.
            let current = {*:st, input: matched.input, expansions: st.expansions + 1};
            let name = macro_diagnostic_name(call)^;
            if (matched.eof) current
            else if (name == null) fail(current, matched.issue)
            else record_diagnostic(current, matched.issue, "Use of " ++ name ++ " doesn't match its definition")
        }
        else {
            let optional = if (macro.document_specs == null and macro.optional == true) latex_optional(matched.input)^
                else {input: matched.input, present: false, tokens: [], issue: null};
            let initial = if (macro.optional == true and macro.document_specs == null)
                [if (optional.present) optional.tokens else stored_tokens(macro.optional_default)] else [];
            let args = if (macro.document_specs != null) collect_document_args(st,matched.input,macro.document_specs)^
                else collect_args(optional.input, macro.delimiters, macro.brace_delim, macro.long_ok, initial)^;
            let noted = if (macro.document_specs != null) document_missing_diagnostics(st,args.missing)^ else st;
            // native scans LaTeX optional arguments with a synthetic \relax call.
            if (optional.issue != null) recover_macro_argument({*:noted, input: optional.input}, optional.issue,
                if (optional.issue == "tex-extra-brace") "brace" else "eof", control_token("relax"))^
            else if (args.issue != null) recover_macro_argument({*:noted, input: args.input}, args.issue, args.cause,
                if(args.optional_issue == true) control_token("relax") else call)
            // replacement text is stored, while freshly scanned arguments retain their source identity.
            else {*:noted, input: stream.push(args.input, substitute(stored_tokens(macro.body), args.args)),
                expansions: st.expansions + 1}
        }
    }
}

fn assign_let(st: State, global) State^ {
    let target = nonspace(st.input)^;
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let first = nonspace(target.stream)^;
        let after_equal = if (other(first.token, "=")) stream.next(first.stream)^ else first;
        let value = if (other(first.token, "=") and charcat(after_equal.token, 10))
            stream.next(after_equal.stream)^ else after_equal;
        let advanced = {*:st, input: value.stream};
        if (value.token == null) fail(advanced, "tex-runaway-assignment")
        else assign_meaning(advanced, key(target.token), meaning(st, value.token), global)
    }
}

fn assign_futurelet(st: State, global) State^ {
    let target = nonspace(st.input)^;
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let first = stream.next(target.stream)^;
        let second = stream.next(first.stream)^;
        if (first.token == null or second.token == null) fail({*:st, input: second.stream}, "tex-runaway-assignment")
        else assign_meaning({*:st, input: stream.push(second.stream, [first.token, second.token])},
            key(target.token), meaning(st, second.token), global)
    }
}

// aliased expl3 lets require the assignment source-mapping guard.
fn guarded_expl_let(st: State, token) State^ => if (meaning(st, token).source_mapping_guard == "tl_eq")
    {*:st, expl3_tl_put_seen: true} else st

fn prefixed(st: State, global, long_ok, protected_flag, budget) State^ {
    // TeX expands macros while searching for the assignment after a prefix.
    let read = expanded_nonspace({*:st, prefix_scan: true}, budget)^;
    let t = read.token;
    let advanced = {*:read.state, prefix_scan: false};
    let cmd = primitive(advanced, t);
    let apply_global = effective_global(advanced, global)^;
    if (advanced.issue != null) advanced
    else if (cmd == null and (not is_control(t) or
        (meaning(advanced, t).kind != "register" and meaning(advanced, t).kind != "dimension-register" and meaning(advanced, t).kind != "glue-register" and meaning(advanced, t).kind != "font" and meaning(advanced, t).kind != "token-register")))
        fail(advanced, "tex-bad-prefix")
    else if (cmd == "global") prefixed(advanced, true, long_ok, protected_flag, budget)
    else if (cmd == "long") prefixed(advanced, global, true, protected_flag, budget)
    else if (cmd == "protected") prefixed(advanced, global, long_ok, true, budget)
    else if (contains(["def", "gdef", "edef", "xdef"], cmd)) {
        let assigned = define(advanced, effective_global(advanced, global or cmd == "gdef" or cmd == "xdef")^, long_ok, protected_flag,
            cmd == "edef" or cmd == "xdef", budget)^;
        {*:assigned, prefixed_definition: true}
    }
    else if (cmd == "let" and not long_ok and not protected_flag) assign_let(guarded_expl_let(advanced, t)^, apply_global)
    else if (cmd == "futurelet" and not long_ok and not protected_flag) assign_futurelet(advanced, apply_global)
    else if (cmd == "font" and not long_ok and not protected_flag) define_font(advanced, apply_global, budget)
    else if (cmd == "letterspacefont" and not long_ok and not protected_flag) assign_letterspace_font(advanced, apply_global, budget)
    else if (contains(["read", "readline"], cmd) and not long_ok and not protected_flag) read_stream_assignment(advanced, apply_global, budget, cmd == "readline")
    else if (primitives.engine_version(cmd) != null and not long_ok and not protected_flag)
        readonly_assignment(advanced, budget)
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
    else if (cmd == "setbox" and not long_ok and not protected_flag) assign_box(advanced, apply_global, budget)
    else if (box_dimension_name(cmd) and not long_ok and not protected_flag) assign_box_dimension(advanced, cmd, budget)
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
    else if (meaning(advanced, t).kind == "font" and not long_ok and not protected_flag)
        select_font(advanced, apply_global, meaning(advanced, t).index)
    else if (meaning(advanced, t).kind == "register" and not long_ok and not protected_flag)
        assign_count(advanced, apply_global, budget, meaning(advanced, t).index)
    else if (meaning(advanced, t).kind == "dimension-register" and not long_ok and not protected_flag)
        assign_dimen(advanced, apply_global, budget, meaning(advanced, t).index)
    else if (meaning(advanced, t).kind == "glue-register" and not long_ok and not protected_flag)
        assign_glue(advanced, apply_global, budget, meaning(advanced, t).index, meaning(advanced, t).mu)
    else if (meaning(advanced, t).kind == "token-register" and not long_ok and not protected_flag)
        assign_toks(advanced, apply_global, budget, meaning(advanced, t).index)
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

fn noexpand(st: State, budget, argument_mode = false) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input)^;
        let existing = if (is_control(read.token)) meaning(st, read.token) else null;
        let cmd = primitive(st, read.token);
        let script_macro = argument_mode and st.script_expl3 and cmd != null and
            expl3.exists(cmd) and not expl3.expandable(cmd);
        let unexpandable = is_control(read.token) and not script_macro and
            (contains(["relax", "char", "register", "dimension-register", "glue-register", "token-register",
                "font", "chardef", "mathchardef", "constructor"], existing.kind) or
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

fn expand_one(st: State, t, budget) State^ {
    let pending = len(st.conditions) > 0 and st.conditions[len(st.conditions) - 1].kind == "scanning";
    if (not expandable(st, t)) {*:st, input: stream.push(st.input, [t])}
    // TeX inserts a relax before a delimiter encountered during an if test.
    else if (pending and contains(["else", "fi", "or"], primitive(st, t)))
        {*:st, input: stream.push(st.input, [control_token("relax"), t])}
    else if (meaning(st, t).kind == "macro") {
        let macro = meaning(st, t)^;
        if (st.expansions >= budget) fail(st, "tex-budget")
        else if (macro.source_mapping_guard == "replicate_negative")
            unsupported(st, "expl3 negative replication diagnostic")
        else if (st.latex and len(st.packages) > 0 and unresolved_package_cycle(st, macro)^)
            unsupported(st, "recursive package bootstrap")
        else {
            let marked = if (macro.source_mapping_guard == "cs_eq") {*:st, expl3_cs_eq_seen: true}
                else if (macro.source_mapping_guard == "dynamic_assignment") {*:st, expl3_dynamic_assignment_seen: true}
                else if (macro.source_mapping_guard == "dynamic_read") {*:st, expl3_dynamic_read_seen: true}
                else if (macro.source_mapping_guard == "tl_head") {*:st, expl3_tl_head_seen: true}
                else if (macro.source_mapping_guard == "tl_tail") {*:st, expl3_tl_tail_seen: true}
                else if (macro.source_mapping_guard == "exp_args_once") {*:st, expl3_exp_args_once_seen: true}
                else if (macro.source_mapping_guard == "exp_args_f") {*:st, expl3_exp_args_f_seen: true}
                else if (macro.source_mapping_guard == "exp_args_value") {*:st, expl3_exp_args_value_seen: true}
                else if (macro.source_mapping_guard == "exp_args_value_once")
                    {*:st, expl3_exp_args_value_seen:true, expl3_exp_args_once_seen:true}
                else if (macro.source_mapping_guard == "bool_expr") {*:st, expl3_bool_expr_seen:true}
                else if (macro.source_mapping_guard == "str_case") {*:st, expl3_str_case_seen:true}
                else if (macro.source_mapping_guard == "seq_map") {*:st, expl3_seq_map_seen:true}
                else if (macro.source_mapping_guard == "tl_map") {*:st, expl3_tl_map_seen:true}
                else if (macro.source_mapping_guard == "replicate") {*:st, expl3_replicate_seen:true}
                else if (macro.source_mapping_guard == "int_math") {*:st, expl3_int_math_seen: true} else st;
            let traced = if (st.prefix_scan == true) {*:marked, prefixed_macro_seen: true} else marked;
            expand_macro(traced, macro, budget, t)
        }
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
    else if (primitives.engine_revision(primitive(st, t)) != null)
        constant_primitive(st, budget, primitives.engine_revision(primitive(st, t)))
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
    else if (st.script_expl3 and expl3.keyval_parse(primitive(st, t))) expl3_keyval_parse(st, budget)
    else if (st.script_expl3 and primitive(st, t) == "__keys_choice_find:n") expl3_key_choice(st, budget)
    else if (st.script_expl3 and expl3.ior_eof(primitive(st, t))) expl3_ior_eof(st, primitive(st, t))
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
    else if (st.script_expl3 and contains(["seq_use:Nn", "seq_count:N", "seq_item:Nn"], primitive(st, t)))
        expl3_seq_expand(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.string_compare(primitive(st, t)))
        expl3_str_compare(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.token_expandable(primitive(st, t)))
        expl3_token_expandable(st, primitive(st, t), budget)
    else if (st.script_expl3 and expl3.text_case(primitive(st, t)))
        expl3_text_case(st, primitive(st, t), budget)
    else if (st.latex and contains(["IfBooleanTF","IfBooleanT","IfBooleanF", "IfNoValueTF","IfNoValueT",
            "IfNoValueF","IfValueTF","IfValueT","IfValueF"],primitive(st,t)))
        latex_xparse_conditional(st,primitive(st,t),budget)
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

// xparse tests compare unexpanded argument tokens before selecting a branch.
fn latex_xparse_conditional(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st,"tex-budget")
    else {
        let value = stream.argument(st.input,true)^;
        let first = stream.argument(value.stream,true)^;
        let second = if (ends_with(name,"TF")) stream.argument(first.stream,true)^
            else {stream:first.stream,tokens:[],error:null};
        let current = {*:st,input:second.stream,expansions:st.expansions + 1};
        if (value.error != null) fail(current,value.error)
        else if (first.error != null) fail(current,first.error)
        else if (second.error != null) fail(current,second.error)
        else {
            let boolean = starts_with(name,"IfBoolean");
            let absent = all([for (t in value.tokens) t.kind == "char"]) and
                join([for(t in value.tokens) t.value],"") == "-NoValue-";
            let truth = if (boolean) len(value.tokens) == 1 and is_control(value.tokens[0]) and
                    value.tokens[0].value == "BooleanTrue"
                else if (starts_with(name,"IfNoValue")) absent else not absent;
            let take = if (ends_with(name,"F") and not ends_with(name,"TF")) not truth else truth;
            {*:current,input:stream.push(current.input,if(take) first.tokens else second.tokens)}
        }
    }
}

fn latex_xspace(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input)^;
        let space = {kind: "char", value: " ", cat: 10, start: -1, end: -1};
        let pending = if (read.token == null) [] else [*(if (xspace_exception(read.token)^) [] else [space]), read.token];
        {*:st, input: stream.push(read.stream, pending), expansions: st.expansions + 1}
    }
}

fn expand_after(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let first = stream.next(st.input)^;
        let second = if (first.token == null) null else stream.next(first.stream)^;
        let advanced = {*:st, input: if (second == null) first.stream else second.stream,
            expansions: st.expansions + 1};
        if (first.token == null) advanced
        else if (second.token == null) {*:advanced, input: stream.push(advanced.input, [first.token])}
        else {
            // Only a direct \the or \unexpanded is protected by definition scanning.
            let expanded = expand_one({*:advanced, token_text_scan:false}, second.token, budget);
            {*:expanded, token_text_scan:advanced.token_text_scan,
                input: stream.push(expanded.input, [first.token])}
        }
    }
}

// document length and counter calls stay visible after package redefinition.
fn redefined_document_command(st, token) any^ => st.latex and st.math_mode != true and
    token != null and token.kind == "control" and token.noexpand != true and
    token.direct == true and token.file == st.main_file and
    contains(["setlength", "addtolength", "setcounter", "addtocounter", "stepcounter"], token.value) and
    meaning(st, token).kind == "macro"

fn expanded_next(st, budget, protect = false, argument_mode = false) any^ {
    let read = stream.next(st.input)^;
    let current = {*:st, input: read.stream};
    if (read.token == null or (protect and is_control(read.token) and
        meaning(current, read.token).kind == "macro" and meaning(current, read.token).protected_flag) or
        (argument_mode and redefined_document_command(current, read.token)^) or
        (argument_mode and read.token.noexpand != true and is_control(read.token) and
            meaning(current, read.token).expl3_argument_unqualified == true) or
        not expandable(current, read.token)) {state: current, token: read.token}
    else {
        let expanded = if (argument_mode and primitive(current, read.token) == "noexpand")
            noexpand(current, budget, true)^ else expand_one(current, read.token, budget);
        if (expanded.issue != null) {state: expanded, token: null} else expanded_next(expanded, budget, protect, argument_mode)
    }
}

// expanded control sequences can carry the meaning of a space token.
fn expanded_space(st, token) any^ => charcat(token, 10) or
    (is_control(token) and meaning(st, token).kind == "char" and meaning(st, token).cat == 10)

fn expanded_nonspace(st, budget) any^ {
    let read = expanded_next(st, budget);
    if (expanded_space(read.state, read.token)^) expanded_nonspace(read.state, budget) else read
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

fn csname(st: State, budget) State^ {
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

// An expanded scan consumes the token's one-use protection before returning it.
fn after_expanded_scan(t) => if (t != null and t.noexpand == true) {*:t, noexpand: false} else t
fn other(t, value) => charcat(t, 12) and t.value == value

fn signs(st, budget, negative = false) SignedScan^ {
    let read = expanded_nonspace(st, budget);
    if (other(read.token, "-")) signs(read.state, budget, not negative)^
    else if (other(read.token, "+")) signs(read.state, budget, negative)^
    else {state: read.state, token: read.token, negative: negative}
}

fn digit(t, base) int | null | error {
    if (t == null or t.kind != "char") null
    else if (t.cat == 12 and index_of("0123456789", t.value) != null) {
        let n = index_of("0123456789", t.value);
        if (n < base) n else null
    }
    else if (base == 16 and (t.cat == 11 or t.cat == 12) and index_of("ABCDEF", t.value) != null)
        index_of("ABCDEF", t.value) + 10
    else null
}

fn digits(st, t, base, negative, budget, count = 0, value = 0, overflow = false, return_token = false) NumberScan^ {
    let n = digit(t, base);
    if (st.issue != null) {state: st, value: 0}
    else if (n == null and return_token) {state: st, value: value, token: t}
    else if (n == null) {
        let returned = after_expanded_scan(t);
        let after = if (count > 0 and expanded_space(st, t)^) st else put_back(st, returned);
        // TeX reports the missing operand and scans the returned token again.
        if (count == 0) {state: record_diagnostic(after, "tex-missing-number", "Missing number, treated as zero")^, value: 0}
        else {state: after, value: if (negative) -value else value}
    } else {
        let too_big = overflow or value * base + n > 2147483647;
        // TeX clamps once but still consumes all following digits of the number.
        let current = if (too_big and not overflow) record_diagnostic(st, "tex-number-too-big", "Number too big")^ else st;
        let next_value = if (too_big) 2147483647 else value * base + n;
        let read = expanded_next(current, budget);
        if (read.state.issue != null) {state: read.state, value: 0}
        else digits(read.state, read.token, base, negative, budget, count + 1, next_value, too_big, return_token)^
    }
}

fn character_number(st, negative, budget) NumberScan^ {
    let read = stream.next(st.input)^;
    let t = read.token;
    let current = {*:st, input: read.stream};
    if (t == null or not (t.kind == "char" or is_control(t)) or len(t.value) != 1)
        {state: fail(current, "tex-improper-alphabetic"), value: 0}
    else {
        let following = expanded_next(current, budget);
        let after = if (expanded_space(following.state, following.token)^) following.state else put_back(following.state, after_expanded_scan(following.token));
        {state: after, value: if (negative) -ord(t.value) else ord(t.value)}
    }
}

fn input_line_number(st) int^ {
    let active = stream.active_reader(st.input)^;
    if (active == null or active.line_no == null) 0 else active.line_no
}

fn scan_number(st, budget) NumberScan^ {
    let start = signs(st, budget)^;
    let t = start.token;
    if (start.state.issue != null) {state: start.state, value: 0}
    else if (other(t, "`")) character_number(start.state, start.negative, budget)^
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
    else if (box_dimension_name(primitive(start.state, t))) {
        let value = box_dimension(start.state, primitive(start.state, t), budget)^;
        {state: value.state, value: if (start.negative) -value.value else value.value}
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
    else if (primitives.engine_version(primitive(start.state, t)) != null) {
        let value = primitives.engine_version(primitive(start.state, t));
        {state: start.state, value: if (start.negative) -value else value}
    }
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
        digits(read.state, read.token, if (t.value == "'") 8 else 16, start.negative, budget)^
    }
    else digits(start.state, t, 10, start.negative, budget)^
}

let COUNT_REGISTER_LIMIT = 32768

fn scan_register(st, budget) NumberScan^ {
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
        else if (not other(close.token, ")")) {
            // insert the missing close without consuming the token that ended the expression.
            let diagnosed=record_diagnostic(close.state,"tex-missing-paren","Missing ) inserted for expression")^;
            {state:put_back(diagnosed,close.token),value:nested.value}
        }
        else {state: close.state, value: nested.value}
    }
    else if (level == "dimen") scan_dimen(put_back(first.state, first.token), budget)^
    else if (level == "glue" or level == "mu") scan_glue(put_back(first.state, first.token), budget, level == "mu")^
    else scan_number(put_back(first.state, first.token), budget)^
}

// Expression arithmetic defers its diagnostic until the whole expression is scanned.
fn arithmetic_error(st: State, deferred) State => if (deferred)
    {*:st, expression_arithmetic_error: true} else fail(st, "tex-arith-overflow")

fn checked_integer(st, value, deferred = false) any^ =>
    if (value > 2147483647 or value < -2147483647) {state: arithmetic_error(st, deferred), value: 0}
    else {state: st, value: value}

fn rounded_fraction(st, numerator, denominator, deferred = false) any^ {
    if (denominator == 0) {state: arithmetic_error(st, deferred), value: 0}
    else {
        let a = abs(numerator);
        let b = abs(denominator);
        let whole = a div b;
        let rounded = whole + if (2 * (a % b) >= b) 1 else 0;
        checked_integer(st, if ((numerator < 0) != (denominator < 0)) -rounded else rounded, deferred)
    }
}

fn expression_scaled(st, value, level, numerator, denominator) any^ {
    if (level == "glue" or level == "mu") {
        let width = rounded_fraction(st, value.width * numerator, denominator, true)^;
        let stretch = rounded_fraction(width.state, value.stretch * numerator, denominator, true)^;
        let shrink = rounded_fraction(stretch.state, value.shrink * numerator, denominator, true)^;
        let result = {width: width.value, stretch: stretch.value,
            stretch_order: if (stretch.value == 0) 0 else value.stretch_order,
            shrink: shrink.value, shrink_order: if (shrink.value == 0) 0 else value.shrink_order};
        if (width.state.issue != null) {state: width.state, value: result}
        else if (stretch.state.issue != null) {state: stretch.state, value: result}
        else if (shrink.state.issue != null) {state: shrink.state, value: result}
        else if (not glue.valid(result)) {state: arithmetic_error(shrink.state, true), value: glue.zero()}
        else {state: shrink.state, value: result}
    }
    else {
        let result = rounded_fraction(st, value * numerator, denominator, true)^;
        if (result.state.issue == null and level == "dimen" and abs(result.value) >= 1073741824)
            {state: arithmetic_error(result.state, true), value: 0}
        else result
    }
}

fn expression_added(st, a, b, level, subtract) any^ {
    if (level == "glue" or level == "mu") {
        let result = glue.add(a, if (subtract) glue.negate(b) else b)^;
        if (glue.valid(result)) {state: st, value: result}
        else {state: arithmetic_error(st, true), value: glue.zero()}
    }
    else {
        let result = checked_integer(st, if (subtract) a - b else a + b, true)^;
        if (result.state.issue == null and level == "dimen" and abs(result.value) >= 1073741824)
            {state: arithmetic_error(result.state, true), value: 0}
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
    let outer_error = st.expression_arithmetic_error == true;
    let result = expression_sum({*:st, expression_arithmetic_error: false}, budget, level)^;
    if (result.state.issue != null) {*:result, state: {*:result.state, expression_arithmetic_error: outer_error}}
    else {
        let end = expanded_nonspace(result.state, budget)^;
        let stopped = if (primitive(end.state, end.token) == "relax") end.state else put_back(end.state, end.token);
        let overflow = stopped.expression_arithmetic_error == true;
        let restored = {*:stopped, expression_arithmetic_error: outer_error};
        {state: if (overflow) record_diagnostic(restored, "tex-arith-overflow", "Arithmetic overflow")^ else restored,
            value: if (overflow) if (level == "glue" or level == "mu") glue.zero() else 0 else result.value}
    }
}

fn count_value(st, index) => if (st.counts[string(index)] == null) 0 else st.counts[string(index)]
fn set_count(counts, index, value) => {*:counts, *:map([string(index), value])}
fn save_count(st: State, index, value, global) State^ =>
    {*:st, counts: set_count(st.counts, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, counts: set_count(scope.counts, index, value)}] else st.groups}
fn save_box(st: State, index, box, global) State^ {
    let depth = len(st.groups);
    {*:st, boxes: set_count(st.boxes, index, box),
        groups: [for (i, scope in st.groups)
            if (global) {*:scope, boxes: set_count(scope.boxes, index, box),
                box_written: set_count(scope.box_written, index, false)}
            else if (i == depth - 1) {*:scope, box_written: set_count(scope.box_written, index, true)}
            else scope]}
}

// Consumption crosses only scopes that have not saved the register locally.
fn take_box(st, index, copy) any^ {
    let box = st.boxes[string(index)];
    if (copy or box == null) {state:st, box:box}
    else {
        let saved = [for (i, scope in st.groups where scope.box_written[string(index)] == true) i];
        let barrier = if (len(saved) == 0) -1 else saved[len(saved) - 1];
        {state:{*:st, boxes:set_count(st.boxes,index,null),
            groups:[for(i, scope in st.groups) if (i > barrier)
                {*:scope, boxes:set_count(scope.boxes,index,null)} else scope]}, box:box}
    }
}

fn box_dimension_name(name) => contains(["wd", "ht", "dp"], name)

// box copies share identity; their dimensions belong to the object across register scopes.
fn box_dimension(st: State, name, budget) NumberScan^ {
    let index = scan_register(st, budget)^;
    let box = index.state.boxes[string(index.value)];
    let metrics = if (box == null) null else index.state.box_metrics[string(box.id)];
    let value = if (metrics == null or metrics[name] == null) 0 else metrics[name];
    let current = if (index.state.issue != null or box == null or index.state.box_metrics_reported == true) index.state
        else record_diagnostic({*:index.state, box_metrics_reported: true}, "approximate-box-metrics",
            "box dimensions are approximate: TeX's typesetting is not reproduced")^;
    {state: current, value: value}
}

fn assign_box_dimension(st: State, name, budget) State^ {
    let index = scan_register(st, budget)^;
    if (index.state.issue != null) index.state
    else {
        let value = scan_dimen(optional_equals(index.state, budget)^, budget)^;
        let box = value.state.boxes[string(index.value)];
        if (value.state.issue != null or box == null) value.state
        else {
            let id = string(box.id);
            let prior = value.state.box_metrics[id] or {};
            {*:value.state, box_metrics: {*:value.state.box_metrics, *:map([id, {*:prior, *:map([name, value.value])}])}}
        }
    }
}

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

// native consumes an optional equals sign before diagnosing a read-only assignment.
fn readonly_assignment(st: State, budget) State^ {
    let scanned = optional_equals(st,budget)^;
    if (scanned.issue != null) scanned
    else record_diagnostic(scanned,"tex-cant-assign","You can't assign to a read-only quantity")
}

fn word_tail(st, word, position, consumed, budget) WordScan^ {
    if (position == len(word)) {state: st, found: true}
    else {
        let read = expanded_next(st, budget)^;
        if (read.state.issue != null) {state: read.state, found: false}
        else if (read.token != null and read.token.kind == "char" and
            lower(read.token.value) == slice(word, position, position + 1))
            word_tail(read.state, word, position + 1, [*consumed, read.token], budget)^
        else {
            let backed = put_back(read.state, read.token);
            // restored keyword prefixes are copied tokens, not direct source reads
            {state: {*:backed, input: stream.push(backed.input, stored_tokens(consumed))}, found: false}
        }
    }
}

fn scan_word(st, word, budget) WordScan^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) {state: first.state, found: false}
    else if (first.token != null and first.token.kind == "char" and
        lower(first.token.value) == slice(word, 0, 1))
        word_tail(first.state, word, 1, [first.token], budget)^
    else {state: put_back(first.state, first.token), found: false}
}

fn fraction_digits(st, budget, values = []) NumberScan^ {
    let read = expanded_next(st, budget)^;
    let n = if (charcat(read.token, 12)) digit(read.token, 10) else null;
    if (read.state.issue != null) {state: read.state, value: 0}
    else if (n == null) {state: if (charcat(read.token, 10)) read.state else put_back(read.state, read.token),
        value: dimension.round_decimal(values)}
    else fraction_digits(read.state, budget, if (len(values) < 17) [*values, n] else values)^
}

fn dimension_unit(st, budget, names = ["pt", "in", "pc", "cm", "mm", "bp", "dd", "cc", "nd", "nc", "sp"], index = 0) NameScan^ {
    // Failed keyword scans restore their tokens before TeX inserts the default unit.
    if (index >= len(names)) {state: record_diagnostic(st, "tex-illegal-unit", "Illegal unit of measure (pt inserted)")^, name: "pt"}
    else {
        let word = scan_word(st, names[index], budget)^;
        if (word.state.issue != null or word.found) {state: word.state, name: if (word.found) names[index] else null}
        else dimension_unit(word.state, budget, names, index + 1)^
    }
}

fn infinite_order(st: State, budget) NumberScan^ {
    var current: State = st
    var order = 1
    var found = true
    // Consume arbitrary excess l tokens without growing the native stack.
    while (found and current.issue == null) {
        let next = scan_word(current, "l", budget)^;
        found = next.found
        current = if (found and order == 3) record_diagnostic(next.state, "tex-illegal-unit", "Illegal unit of measure (replaced by filll)")^ else next.state
        if (found and order < 3) { order = order + 1 }
    }
    return {state: current, value: order}
}

// Native TeX reports an oversized dimension after consuming its unit and clamps it.
fn dimension_result(st: State, magnitude, negative, order = 0) NumberScan^ {
    let too_large = abs(magnitude) >= 1073741824;
    let current = if (too_large) record_diagnostic(st, "tex-dimension-too-large", "Dimension too large")^ else st;
    let value = if (too_large) 1073741823 else magnitude;
    {state: current, value: if (negative) -value else value, order: order}
}

fn dimension_finish(st, whole, fraction, negative, budget, infinite, mu) NumberScan^ {
    let next = expanded_nonspace(st, budget)^;
    let cmd = primitive(next.state, next.token);
    let m = if (is_control(next.token)) meaning(next.state, next.token) else null;
    if (next.state.issue != null) {state: next.state, value: 0}
    else if (box_dimension_name(cmd) or cmd == "dimen" or m.kind == "dimension-register" or primitives.parameter_kind(cmd) == "dimen") {
        let index = if (box_dimension_name(cmd)) box_dimension(next.state, cmd, budget)^
            else if (cmd == "dimen") scan_register(next.state, budget)^
            else {state: next.state, value: if (m.kind == "dimension-register") m.index else "param:" ++ cmd};
        let value = if (box_dimension_name(cmd)) index.value else dimen_value(index.state, index.value);
        let magnitude = whole * value + (abs(value) * fraction div 65536) * if (value < 0) -1 else 1;
        dimension_result(index.state, magnitude, negative)^
    }
    else {
        let available = put_back(next.state, next.token);
        let fil = if (infinite) scan_word(available, "fil", budget)^ else {state: available, found: false};
        if (fil.found) {
            let order = infinite_order(fil.state, budget)^;
            let amount = whole * 65536 + fraction;
            if (order.state.issue != null) {state: order.state, value: 0, order: 0}
            else dimension_result(order.state, amount, negative, order.value)^
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
            dimension_result(end, magnitude, negative)^
        }
        else {
        let true_word = if (mu) {state: px.state, found: false} else scan_word(px.state, "true", budget)^;
        let found = if (mu) {
            let word = scan_word(true_word.state, "mu", budget)^;
            {state: if (word.found or word.state.issue != null) word.state
                else record_diagnostic(word.state, "tex-illegal-unit", "Illegal unit of measure (mu inserted)")^, name: "pt"}
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
            let unit_whole = if (found.name == "sp") 0 else base_whole * ratio[0] div ratio[1];
            // TeX's magnification quotient has its own limit before unit conversion.
            let mag_overflow = mag != 1000 and adjusted_whole >= 1073741824;
            let scaled = if (mag_overflow or (found.name != "sp" and unit_whole >= 16384)) 1073741824
                else if (found.name == "sp") base_whole
                else unit_whole * 65536 +
                    (ratio[0] * base_fraction + 65536 * ((base_whole * ratio[0]) % ratio[1])) div ratio[1];
            let after = expanded_next(found.state, budget)^;
            let end = if (charcat(after.token, 10)) after.state else put_back(after.state, after.token);
            dimension_result(end, scaled, negative)^
        }
        }
        }
    }
}

fn scan_dimen(st, budget, infinite = false, mu = false) NumberScan^ {
    let start = signs(st, budget)^;
    let cmd = primitive(start.state, start.token);
    let m = if (is_control(start.token)) meaning(start.state, start.token) else null;
    if (start.state.issue != null) {state: start.state, value: 0}
    else if (cmd == "dimen" or m.kind == "dimension-register") {
        let index = if (cmd == "dimen") scan_register(start.state, budget)^ else {state: start.state, value: m.index};
        let value = dimen_value(index.state, index.value);
        {state: index.state, value: if (start.negative) -value else value, order: 0}
    }
    else if (box_dimension_name(cmd)) {
        let value = box_dimension(start.state, cmd, budget)^;
        {state: value.state, value: if (start.negative) -value.value else value.value, order: 0}
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
            else if (literal) digits(start.state, start.token, 10, false, budget, 0, 0, false, true)^
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
                start.negative != (whole_scan.value < 0), budget, infinite, mu)^
        }
    }
}

fn save_dimen(st: State, index, value, global) State^ =>
    {*:st, dimens: set_count(st.dimens, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, dimens: set_count(scope.dimens, index, value)}] else st.groups}

fn assign_dimen(st: State, global, budget, fixed_index) State^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let value = scan_dimen(optional_equals(index.state, budget), budget)^;
        if (value.state.issue != null) value.state else save_dimen(value.state, index.value, value.value, global)
    }
}

fn assign_dimendef(st: State, global, budget) State^ {
    let target = nonspace(st.input)^;
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

fn save_glue(st: State, index, value, global, mu) State^ {
    if (mu) {*:st, muskips: set_count(st.muskips, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, muskips: set_count(scope.muskips, index, value)}] else st.groups}
    else {*:st, skips: set_count(st.skips, index, value),
        groups: if (global) [for (scope in st.groups)
            {*:scope, skips: set_count(scope.skips, index, value)}] else st.groups}
}

fn scan_glue(st, budget, mu) GlueScan^ {
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

fn glue_quantity(st, name, budget) NumberScan^ {
    let scanned = scan_glue(st, budget, false)^;
    {state: scanned.state, value: if (name == "gluestretch") scanned.value.stretch
        else if (name == "glueshrink") scanned.value.shrink
        else if (name == "gluestretchorder") scanned.value.stretch_order else scanned.value.shrink_order}
}

fn assign_glue(st: State, global, budget, fixed_index, mu) State^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let value = scan_glue(optional_equals(index.state, budget), budget, mu)^;
        if (value.state.issue != null) value.state else save_glue(value.state, index.value, value.value, global, mu)
    }
}

fn assign_gluedef(st: State, global, budget, mu) State^ {
    let target = nonspace(st.input)^;
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let index = scan_register(optional_equals(cleared, budget), budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "glue-register", index: index.value, mu: mu}, global)
    }
}

fn scan_filename_tail(st, budget, quoted, chars) NameScan^ {
    let read = expanded_next(st, budget)^;
    let t = read.token;
    if (read.state.issue != null) {state: read.state, name: ""}
    else if (t == null) {state: read.state, name: join(chars, "")}
    else if (t.kind != "char" or (charcat(t, 10) and not quoted))
        {state: if (charcat(t, 10)) read.state else put_back(read.state, t), name: join(chars, "")}
    else if (t.value == "\"") scan_filename_tail(read.state, budget, not quoted, chars)^
    else scan_filename_tail(read.state, budget, quoted, [*chars, t.value])^
}

fn scan_filename(st, budget) NameScan^ {
    let first = expanded_nonspace(st, budget)^;
    if (first.state.issue != null) {state: first.state, name: ""}
    else if (charcat(first.token, 1)) {
        let contents = expanded_text(put_back(first.state, first.token), budget)^;
        {state: contents.state, name: replace(token_text(contents.state, contents.tokens), "\"", "")^}
    }
    else scan_filename_tail(put_back(first.state, first.token), budget, false, [])^
}

fn font_metrics(size) any^ => map([for (i, v in [0, 218453, 109226, 72818, 282168, 655360, 72818],
    part in [string(i + 1), v * size div 655360]) part])

fn define_font(st: State, global, budget) State^ {
    let target = nonspace(st.input)^;
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
fn assign_letterspace_font(st: State, global, budget) State^ {
    let target = nonspace(st.input)^;
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
fn assign_font_family(st: State, budget, name) State^ {
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
fn assign_font_character(st: State, budget, name) State^ {
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

fn assign_font_dimen(st: State, budget) State^ {
    let target = font_dimen_index(st, budget)^;
    if (target.state.issue != null) target.state
    else {
        let value = scan_dimen(optional_equals(target.state, budget), budget)^;
        if (value.state.issue != null or not target.valid) value.state
        else {*:value.state, fonts: [for (i, entry in value.state.fonts)
            if (i == target.font_index) {*:entry, params: set_count(entry.params, target.param, value.value)} else entry]}
    }
}

fn select_font(st: State, global, index) State^ => {*:st, current_font: index,
    groups: if (global) [for (scope in st.groups) {*:scope, current_font: index}] else st.groups}

fn assign_count(st: State, global, budget, fixed_index) State^ {
    let index = if (fixed_index == null) scan_register(st, budget)^ else {state: st, value: fixed_index};
    if (index.state.issue != null) index.state
    else {
        let before = optional_equals(index.state, budget)^;
        let value = scan_number(before, budget)^;
        if (value.state.issue != null) value.state
        else save_count(value.state, index.value, value.value, global)
    }
}

fn optional_by(st: State, budget) State^ => scan_word(st, "by", budget)^.state

// A failed register operation leaves the old value in place.
fn arithmetic_overflow(st: State) State^ => record_diagnostic(st, "tex-arith-overflow", "Arithmetic overflow")^

fn arithmetic_glue(st: State, global, budget, operation, fixed_index, mu) State^ {
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
            else if (operation == "divide" and factor.value == 0) arithmetic_overflow(factor.state)^
            else {
                let scaled = glue.scale(previous, factor.value, operation == "divide")^;
                if (not glue.valid(scaled)) arithmetic_overflow(factor.state)^
                else save_glue(factor.state, index.value, scaled, global, mu)
            }
        }
    }
}

fn arithmetic(st: State, global, budget, operation) State^ {
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
            else if (operation == "divide" and operand.value == 0) arithmetic_overflow(operand.state)^
            else {
                let value = if (operation == "advance") previous + operand.value
                    else if (operation == "multiply") previous * operand.value
                    else if ((previous < 0) != (operand.value < 0)) -(abs(previous) div abs(operand.value))
                    else abs(previous) div abs(operand.value);
                let overflow = abs(value) > (if (is_dimen) 1073741823 else 2147483647);
                let checked = if (overflow and operation == "multiply")
                    {state: arithmetic_overflow(operand.state)^, value: 0} else checked_integer(operand.state, value)^;
                if (checked.state.issue != null) checked.state
                else if (overflow and operation == "multiply") checked.state
                else if (is_dimen and abs(value) >= 1073741824) fail(checked.state, "tex-arith-overflow")
                else if (kind == "int") save_parameter(checked.state, global, cmd, value)
                else if (is_dimen) save_dimen(checked.state, index.value, value, global)
                else save_count(checked.state, index.value, value, global)
            }
        }
    }
}

fn assign_countdef(st: State, global, budget) State^ {
    let target = nonspace(st.input)^;
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let before = optional_equals(cleared, budget)^;
        let index = scan_register(before, budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "register", index: index.value}, global)
    }
}

fn assign_chardef(st: State, global, budget, math) State^ {
    let target = nonspace(st.input)^;
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

fn assign_toks(st: State, global, budget, fixed_index) State^ {
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

fn save_toks(st: State, index, tokens, global) State^ =>
    {*:st, input: if (index == -1) {*:st.input, everyeof: tokens} else st.input,
        toks: set_toks(st.toks, index, tokens),
        groups: if (global) [for (scope in st.groups)
            {*:scope, toks: set_toks(scope.toks, index, tokens)}] else st.groups}

fn assign_toksdef(st: State, global, budget) State^ {
    let target = nonspace(st.input)^;
    if (not is_control(target.token)) fail({*:st, input: target.stream}, "tex-missing-cs")
    else {
        let cleared = assign_meaning({*:st, input: target.stream}, key(target.token), {kind: "primitive", name: "relax"}, global)^;
        let before = optional_equals(cleared, budget)^;
        let index = scan_register(before, budget)^;
        if (index.state.issue != null) index.state
        else assign_meaning(index.state, key(target.token), {kind: "token-register", index: index.value}, global)
    }
}

// generated text is internal to the current definition trace.
fn internal_text_tokens(st, text) map[]^ {
    let tokens = text_tokens(text)^;
    if (st.token_text_scan) [for (token in tokens) {*:token, trace_internal: true}] else tokens
}

// capture the value for inspection without expanding its token-list contents.
fn the_push(input, tokens, capture) any^ => if (capture) {*:input, captured_the: tokens}
    else stream.push(input, tokens)

fn expand_the(st: State, budget, count_expansion = true, capture = false) State^ {
    if (count_expansion and st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = expanded_next({*:st, expansions: st.expansions + if (count_expansion) 1 else 0}, budget)^;
        let m = if (is_control(read.token)) meaning(read.state, read.token) else null;
        if (primitive(read.state, read.token) == "count" or m.kind == "register") {
            let index = if (m.kind == "register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: the_push(index.state.input, internal_text_tokens(st, string(count_value(index.state, index.value))), capture)}
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
            else {*:value.state, input: the_push(value.state.input, internal_text_tokens(st, string(value.value)), capture)}
        }
        else if (primitive(read.state, read.token) == "fontdimen") {
            let value = font_dimen(read.state, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, internal_text_tokens(st, dimension.format(value.value)), capture)}
        }
        else if (primitive(read.state, read.token) == "dimen" or m.kind == "dimension-register") {
            let index = if (m.kind == "dimension-register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: the_push(index.state.input,
                internal_text_tokens(st, dimension.format(dimen_value(index.state, index.value))), capture)}
        }
        else if (box_dimension_name(primitive(read.state, read.token))) {
            let value = box_dimension(read.state, primitive(read.state, read.token), budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, internal_text_tokens(st, dimension.format(value.value)), capture)}
        }
        else if (primitive(read.state, read.token) == "skip" or primitive(read.state, read.token) == "muskip" or m.kind == "glue-register") {
            let mu = if (m.kind == "glue-register") m.mu else primitive(read.state, read.token) == "muskip";
            let index = if (m.kind == "glue-register") {state: read.state, value: m.index}
                else scan_register(read.state, budget)^;
            if (index.state.issue != null) index.state
            else {*:index.state, input: the_push(index.state.input,
                internal_text_tokens(st, glue.format(glue_value(index.state, index.value, mu), mu)), capture)}
        }
        else if (primitives.parameter_kind(primitive(read.state, read.token)) == "int")
            {*:read.state, input: the_push(read.state.input,
                internal_text_tokens(st, string(parameter_value(read.state, primitive(read.state, read.token)))), capture)}
        else if (primitives.parameter_kind(primitive(read.state, read.token)) == "dimen")
            {*:read.state, input: the_push(read.state.input,
                internal_text_tokens(st, dimension.format(dimen_value(read.state, "param:" ++ primitive(read.state, read.token)))), capture)}
        else if (contains(["glue", "mu"], primitives.parameter_kind(primitive(read.state, read.token)))) {
            let name = primitive(read.state, read.token);
            let mu = primitives.parameter_kind(name) == "mu";
            {*:read.state, input: the_push(read.state.input, internal_text_tokens(st, glue.format(glue_value(read.state, "param:" ++ name, mu), mu)), capture)}
        }
        else if (contains(["parshape", "interlinepenalties", "clubpenalties", "widowpenalties", "displaywidowpenalties"], primitive(read.state, read.token))) {
            let value = paragraph_quantity(read.state, primitive(read.state, read.token), budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, internal_text_tokens(st, "0"), capture)}
        }
        else if (primitive(read.state, read.token) == "interactionmode" or page_dimension(primitive(read.state, read.token))) {
            let text = if (primitive(read.state, read.token) == "interactionmode") "1" else "0.0pt";
            {*:read.state, page_state_quantity_seen: true, input: the_push(read.state.input, internal_text_tokens(st, text), capture)}
        }
        else if (contains(["pdfrandomseed", "pdfelapsedtime"], primitive(read.state, read.token))) {
            let value = if (primitive(read.state, read.token) == "pdfrandomseed") read.state.random_seed % 2147483648 else 0;
            {*:read.state, pdf_random_seen: true, input: the_push(read.state.input, internal_text_tokens(st, string(value)), capture)}
        }
        else if (primitive(read.state, read.token) == "inputlineno")
            {*:read.state, input: the_push(read.state.input, internal_text_tokens(st, string(input_line_number(read.state)^)), capture)}
        // character definitions and engine versions are native integer quantities.
        else if (m.kind == "chardef" or m.kind == "mathchardef" or primitives.engine_version(primitive(read.state, read.token)) != null) {
            let value = if (m.kind == "chardef" or m.kind == "mathchardef") m.value
                else primitives.engine_version(primitive(read.state, read.token));
            {*:read.state, input: the_push(read.state.input, internal_text_tokens(st, string(value)), capture)}
        }
        else if (primitive(read.state, read.token) == "numexpr") {
            let value = scan_expression(read.state, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input, internal_text_tokens(st, string(value.value)), capture)}
        }
        else if (contains(["dimexpr", "glueexpr", "muexpr"], primitive(read.state, read.token))) {
            let name = primitive(read.state, read.token);
            let level = if (name == "dimexpr") "dimen" else if (name == "glueexpr") "glue" else "mu";
            let value = scan_expression(read.state, budget, level)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input,
                internal_text_tokens(st, if (level == "dimen") dimension.format(value.value) else glue.format(value.value, level == "mu")), capture)}
        }
        else if (contains(["gluestretch", "glueshrink", "gluestretchorder", "glueshrinkorder"], primitive(read.state, read.token))) {
            let name = primitive(read.state, read.token);
            let value = glue_quantity(read.state, name, budget)^;
            if (value.state.issue != null) value.state
            else {*:value.state, input: the_push(value.state.input,
                internal_text_tokens(st, if (name == "gluestretch" or name == "glueshrink") dimension.format(value.value) else string(value.value)), capture)}
        }
        else if (contains(["currentgrouplevel", "currentiflevel"], primitive(read.state, read.token))) {
            let value = if (primitive(read.state, read.token) == "currentgrouplevel") len(read.state.groups)
                else len(read.state.conditions);
            {*:read.state, input: the_push(read.state.input, internal_text_tokens(st, string(value)), capture)}
        }
        else if (contains(["catcode", "lccode", "uccode", "sfcode", "mathcode", "delcode"], primitive(read.state, read.token))) {
            let code = code_quantity(read.state, primitive(read.state, read.token), budget)^;
            if (code.state.issue != null) code.state
            else {*:code.state, input: the_push(code.state.input, internal_text_tokens(st, string(code.value)), capture)}
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

fn string_primitive(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        if (name == "number" or name == "romannumeral") {
            let n = scan_number(current, budget)^;
            if (n.state.issue != null) n.state
            else {*:n.state, input: stream.push(n.state.input,
                text_tokens(if (name == "number") string(n.value) else roman(n.value)))}
        } else {
            let read = stream.next(current.input)^;
            let t = read.token;
            if (t == null) {*:current, input: read.stream}
            else {
                let text = if (name == "meaning") print_meaning(current, t)
                    else if (t.kind == "control")
                    (if (t.value == "") escape(current) ++ "csname" ++ escape(current) ++ "endcsname"
                        else escape(current) ++ t.value) else t.value;
                let escaped = any([for (c in ["\\", "{", "}", "$", "&", "#", "^", "_", "%", "~"]) contains(text, c)]);
                // Only emitted generated text can affect the document's source mapping.
                let generated = [for (token in text_tokens(text)^)
                    {*:token, *:if (escaped) {generated_text_mapping:true} else {}}];
                {*:current, input: stream.push(read.stream, generated)}
            }
        }
    }
}

fn token_primitive(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1};
        let text = general_text(current, budget, false)^;
        if (text.state.issue != null) text.state
        else {
            if (name == "scantokens") {
                let scanned = token_text(text.state, text.tokens)^;
                if (len(text.state.input.levels) >= 6000) fail(text.state, "tex-capacity")
                else {
                    let id = next_source_id(text.state)^;
                    let child = {*:reader.reader(scanned, id), catcodes: text.state.input.reader.catcodes,
                        endline: text.state.input.reader.endline};
                    {*:text.state, input: stream.push_reader(text.state.input, child),
                        sources: [*text.state.sources, {id: id, name: "\\scantokens", origin: "local", data: scanned}]}
                }
            } else {
                let tokens = if (name == "detokenize") internal_text_tokens(text.state, token_text(text.state, text.tokens))
                    else if (text.state.token_text_scan) [for (t in text.tokens)
                        {*:t, noexpand: is_control(t), direct_toks: true}]
                    // native replay stores the scanned list and clears its direct-source flags.
                    else stored_tokens(text.tokens);
                {*:text.state, input: stream.push(text.state.input, tokens)}
            }
        }
    }
}

fn expanded_primitive(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let text = expanded_text({*:st, expansions: st.expansions + 1}, budget, true)^;
        if (text.state.issue != null) text.state
        // Native replays a stored token list, which no longer has direct-source identity.
        else {*:text.state, input: stream.push(text.state.input, stored_tokens(text.tokens))}
    }
}

// TeX's general-text scanner inserts a missing brace and retains partial EOF text.
fn general_text(st, budget, xpand) any^ {
    let opening = expanded_nonrelax(st, budget, true)^;
    if (opening.state.issue != null) {state: opening.state, tokens: []}
    else {
        let m = if (is_control(opening.token)) meaning(opening.state, opening.token) else null;
        let valid = charcat(opening.token, 1) or (m.kind == "char" and m.cat == 1);
        // Native inserts a missing opening brace and scans the returned token as text.
        // Native expanded lookahead supplies a relax token when it reaches EOF.
        let returned = if (opening.token == null) control_token("relax") else opening.token;
        let current = if (valid) opening.state else record_diagnostic(
            put_back(opening.state, returned), "tex-missing-brace", "Missing { inserted")^;
        definition_tokens(current, budget, null, xpand, true)
    }
}

fn expanded_text(st, budget, recover_eof = false) any^ {
    if (recover_eof) general_text(st,budget,true)
    else {
        let opening = expanded_nonspace(st, budget)^;
        if (opening.state.issue != null) {state: opening.state, tokens: []}
        else if (not charcat(opening.token, 1))
            {state: fail(put_back(opening.state, opening.token), "tex-missing-brace"), tokens: []}
        else definition_tokens(opening.state, budget)
    }
}

// native expansion has no page builder, so marks read as empty token lists.
fn empty_mark_query(st: State, name, budget) State^ {
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
fn expanded_nonrelax(st, budget, temporary_relax = false) any^ {
    let read = expanded_nonspace(st, budget)^;
    if (read.state.issue != null) read
    else {
        // A noexpand-marked control has temporary relax meaning during this scan.
        let relax = primitive(read.state, read.token) == "relax" or
            (temporary_relax and is_control(read.token) and read.token.noexpand == true);
        if (not relax) read else expanded_nonrelax(read.state, budget, temporary_relax)
    }
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
fn assign_page_state(st: State, budget, name) State^ {
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
fn assign_paragraph_data(st: State, budget, name) State^ {
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

fn emit_discretionary(st: State, budget) State^ {
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
fn discard_mark_text(st: State, budget, indexed) State^ {
    let scanned = if (indexed) scan_number(st, budget)^ else {state: st};
    if (scanned.state.issue != null) scanned.state
    else {
        let text = expanded_text(scanned.state, budget)^;
        text.state
    }
}

fn pdf_string_primitive(st: State, name, budget) State^ {
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

// A host may capture one missing resource and restart from the original source.
fn request_resource(st, scope, name) State =>
    {*:st, issue:"tex-resource-needed", resource_request:{scope:scope, name:name}}

// The caller supplies captured, authorized bytes; this module does not open paths.
pub fn captured_resource(st, name, requested_scope = null) any^ {
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
        if (entry == null and st.resource_requests)
            {state: request_resource(st, scope, cleaned), available: false, found: false, data: null}
        else if (entry == null or entry.missing == true or entry.no_base == true)
            {state: st, available: true, found: false, data: null, no_base:entry.no_base == true}
        else if (not (entry.data is string or entry.data is binary))
            {state: fail(st, "tex-resource-invalid"), available: true, found: false, data: null}
        else {
            // S2.2.2v2 represents empty binary as null; retain an empty resource as text.
            let bytes = if (len(entry.data) == 0) "" else binary(entry.data)^;
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

type SourceText = {valid: bool, text: string}

// Decode once at the resource boundary; scanners receive text plus explicit status.
pub fn decode_source(data: string | binary) SourceText^ {
    let decoded = utf8.decode(binary(data)^) ^ { null };
    {valid: decoded != null, text: decoded or ""}
}

fn push_resource_reader(fetched) State^ {
    let decoded = decode_source(fetched.data)^;
    if (not decoded.valid) fail(fetched.state, "tex-invalid-utf8")
    else if (len(fetched.state.input.levels) >= 6000) fail(fetched.state, "tex-capacity")
    else {
        let child = {*:reader.reader(decoded.text, fetched.id), origin: fetched.origin,
            catcodes: fetched.state.input.reader.catcodes, endline: fetched.state.input.reader.endline};
        {*:fetched.state, input: stream.push_reader(fetched.state.input, child)}
    }
}

// streams use the same caller-supplied resource set as input and packages.
fn input_stream_resource(st: State, name) any^ {
    let exact = captured_resource(st, name)^;
    if (exact.available and not exact.found and exact.state.issue == null)
        captured_resource(exact.state, name ++ ".tex") else exact
}

fn input_stream_command(st: State, budget, opening) State^ {
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
                let chosen = input_stream_resource(name.state, name.name)^;
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
fn read_stream_line(st, slot) StreamLine^ {
    let source = if (slot >= 0 and slot <= 15) st.input_streams[string(slot)] else null;
    if (source.open != true) {
        let diagnosed = record_diagnostic(st, "tex-read-terminal",
            "cannot \\read from the terminal in a document run")^;
        {state: diagnosed, opened: false, eof: true, text: ""}
    }
    else {
        // The reader retains the decoded source and position across reads.
        let decoded = if (source.reader != null) {valid:true, text:""} else decode_source(source.data)^;
        if (source.reader == null and not decoded.valid) {state: unsupported(st, "read invalid UTF-8"), opened: true, eof: true, text: ""}
        else {
            let cursor = if (source.reader != null) source.reader else reader.reader(decoded.text, source.id);
            let line = reader.load_line(cursor)^;
            let next_stream = if (line == null) {*:source, open: false} else {*:source, reader: line};
            let next = {*:st, input_streams: {*:st.input_streams, *:map([string(slot), next_stream])}};
            {state: next, opened: true, eof: line == null, text: if (line == null) "" else line.line}
        }
    }
}

// the native reader reuses a synthetic source buffer for tokenized reads.
fn read_tokens(st, line, balance) TokenRead^ {
    let endline = parameter_value(st, "endlinechar");
    if (endline >= 55296 and endline <= 57343)
        {state: unsupported(st, "read surrogate endline"), balance: balance, tokens: []}
    else read_tokens_valid(st, line, balance)^
}
fn read_tokens_valid(st, line, balance) TokenRead^ {
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
fn read_tokens_inner(input, balance, tokens = []) TokenBalance^ {
    let next = reader.next(input)^;
    if (next == null) {balance: balance, tokens: tokens}
    else {
        let token = next.token;
        if (charcat(token, 2) and balance == 0) {balance: 0, tokens: tokens}
        else read_tokens_inner(next.reader, balance + if (charcat(token, 1)) 1 else if (charcat(token, 2)) -1 else 0,
            [*tokens, token])^
    }
}
fn read_token_lines(st, slot, balance = 0, tokens = []) TokenRead^ {
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
        else if (scanned.balance > 0) read_token_lines(scanned.state, slot, scanned.balance, output)^
        else {state: scanned.state, tokens: output}
    }
}

fn read_stream_assignment(st: State, global, budget, readline) State^ {
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

fn ior_slot(st, token) any^ {
    let value = if (is_control(token)) meaning(st, token) else null;
    if (value.kind == "chardef" or value.kind == "mathchardef") value.value else null
}

// The free-slot list follows TeX assignment scope, but physical streams do not.
fn save_ior_slots(st: State, value, global) State^ {
    let depth = len(st.groups);
    let prior = if (st.ior_free_slots is array) st.ior_free_slots else [];
    {*:st, ior_free_slots:value, groups:[for (i, scope in st.groups)
        if (global and scope.ior_pool_saved != null) {*:scope, ior_pool_saved:value}
        else if (not global and i == depth - 1 and scope.ior_pool_saved == null)
            {*:scope, ior_pool_saved:prior}
        else scope]}
}

// Closed ior slots are returned to the same reusable pool as expl3's stream allocator.
fn ior_close_target(st: State, target) State^ {
    let slot = ior_slot(st, target)^;
    if (slot == null) unsupported(st, "expl3 input stream variable")
    else if (slot < 0 or slot >= 16) st
    else {
        let free = if (st.ior_free_slots is array) st.ior_free_slots else [];
        let global = effective_global(st, true)^;
        let closed = {*:st, input_stream_state_seen:true,
            input_streams:{*:st.input_streams, *:map([string(slot), {open:false}])}};
        let saved = save_ior_slots(closed, if (contains(free, slot)) free else [slot, *free], global)^;
        assign_meaning(saved, key(target), {kind:"mathchardef", value:16}, global)
    }
}

fn ior_open(st: State, target, budget) State^ {
    let arg = stream.argument(st.input, true)^;
    let current = {*:st, input:arg.stream};
    if (arg.error != null) fail(current, arg.error)
    else {
        let filename = expand_argument_name(current, arg.tokens, budget)^;
        if (filename.state.issue != null) filename.state
        else {
            let resource = input_stream_resource(filename.state, trim(filename.name)^)^;
            if (resource.state.issue != null) resource.state
            else if (not resource.available) unsupported(resource.state, "openin resource provider")
            else if (not resource.found) unsupported(resource.state, "expl3 input file missing")
            else {
                let closed = ior_close_target(resource.state, target)^;
                let free = if (closed.ior_free_slots is array) closed.ior_free_slots else [];
                let slot = if (len(free) > 0) free[0] else count_value(closed, 16) + 1;
                if (closed.issue != null) closed
                else if (slot < 0 or slot >= 16) fail(closed, "tex-no-room")
                else {
                    let global = effective_global(closed, true)^;
                    let allocated = if (len(free) > 0) save_ior_slots(closed, slice(free, 1, len(free)), global)^
                        else save_count(closed, 16, slot, global)^;
                    let opened = {*:allocated, input_stream_state_seen:true,
                        input_streams:{*:allocated.input_streams,
                            *:map([string(slot), {open:true, data:resource.data, position:0, id:resource.id}])}};
                    assign_meaning(opened, key(target), {kind:"chardef", value:slot}, global)
                }
            }
        }
    }
}

fn ior_get(st: State, target, raw) State^ {
    let destination = nonspace(st.input)^;
    let current = {*:st, input:destination.stream, input_stream_state_seen:true};
    let slot = ior_slot(current, target)^;
    let global = effective_global(current, false)^;
    if (not is_control(destination.token)) fail(current, "tex-missing-cs")
    else if (slot == null) unsupported(current, "expl3 input stream variable")
    else if (slot < 0 or slot >= 16 or current.input_streams[string(slot)].open != true)
        assign_meaning(current, key(destination.token), expl3.token_list([control_token("q_no_value")]), global)
    else if (raw) {
        let line = read_stream_line(current, slot)^;
        if (line.state.issue != null) line.state
        else assign_meaning(line.state, key(destination.token), expl3.token_list(text_tokens(line.text)^), global)
    }
    else {
        let read = read_token_lines(current, slot)^;
        if (read.state.issue != null) read.state
        else assign_meaning(read.state, key(destination.token), expl3.token_list(read.tokens), global)
    }
}

fn expl3_ior_action(st: State, name, budget) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input:target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (name == "ior_open:Nn") ior_open(current, target.token, budget)
    else if (name == "ior_close:N") ior_close_target(current, target.token)
    else ior_get(current, target.token, name == "ior_str_get:NN")
}

// Match only the ordinary compiled-pattern entry until other regex forms are ported.
fn expl3_regex_match(st: State, budget) State^ {
    let pattern_arg = stream.argument(st.input, true)^;
    let subject = if (pattern_arg.error == null) stream.argument(pattern_arg.stream, true)^
        else {stream:pattern_arg.stream,tokens:[],error:null};
    let current = {*:st,input:subject.stream};
    if (pattern_arg.error != null) fail(current,pattern_arg.error)
    else if (subject.error != null) fail(current,subject.error)
    else if (parameter_value(current,"globaldefs") != 0 or current.after_assignment != null)
        unsupported(current,"expl3 regex assignment scope")
    else {
        let input = stream.push(stream.from_reader(reader.reader("")),pattern_arg.tokens)^;
        let first = nonspace(input)^;
        let pattern = stream.argument(first.stream,true)^;
        let tail = nonspace(pattern.stream)^;
        let builder = meaning(current,control_token("__regex_build:n"))^;
        if (not is_control(first.token) or first.token.value != "__regex_build:n" or
                builder.source_mapping_guard != "regex_builder" or pattern.error != null or tail.token != null)
            unsupported(current,"expl3 regex builder")
        else {
            let result = regex.evaluate(pattern.tokens,subject.tokens,budget - current.expansions)^;
            if (result.issue == "budget") fail(current,"tex-budget")
            else if (result.issue != null) unsupported(current,"expl3 regex " ++ result.issue)
            else {
                let value = meaning(current,control_token(if (result.matched) "c_true_bool" else "c_false_bool"))^;
                assign_meaning({*:current,expl3_regex_seen:true},"control:g__regex_success_bool",value,true)
            }
        }
    }
}

fn expl3_ior_eof(st: State, name) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input:target.stream, input_eof_seen:true};
    let slot = ior_slot(current, target.token)^;
    if (slot == null) unsupported(current, "expl3 input stream variable")
    else {
        let eof = slot < 0 or slot >= 16 or current.input_streams[string(slot)].open != true;
        if (name == "ior_if_eof_p:N") {*:current, input:stream.push(current.input,
            [control_token(if (eof) "c_true_bool" else "c_false_bool")])}
        else expl3_branch(current, name, eof)
    }
}

fn input_primitive(st: State, budget) State^ {
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
            else if (not selected.found) {
                let outside = starts_with(name.name, "/") or contains(name.name, "..");
                let active = stream.active_reader(selected.state.input)^;
                let no_base = selected.no_base == true or
                    (selected.state.host_no_base and (active == null or active.origin != "bundled"));
                // The public TeX result needs the diagnostic's location and message as well as its code.
                record_diagnostic(selected.state, if (no_base) "tex-no-base" else if (outside) "tex-file-outside" else "tex-file-not-found",
                    (if (no_base or outside) "file `" else "File `") ++ name.name ++
                    (if (no_base) "' needs a document location (base)" else if (outside) "' is outside the document directory" else "' not found beside the document"))^
            }
            else push_resource_reader(selected)
        }
    }
}

fn endinput_primitive(st: State, budget) State^ =>
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {*:st, input: stream.end_input(st.input), expansions: st.expansions + 1}

fn pdf_file_primitive(st: State, name, budget, counted = false) State^ {
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
fn random_deviate(st: State, budget) State^ {
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

fn set_random_seed(st: State, budget) State^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else {*:scanned.state, random_seed: ((scanned.value % 4294967296) + 4294967296) % 4294967296,
        pdf_random_seen: true}
}

fn constant_primitive(st: State, budget, value) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {*:st, input: stream.push(st.input, text_tokens(value)), expansions: st.expansions + 1}
}

fn strcmp_primitive(st: State, budget) State^ {
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

fn font_name_primitive(st: State, budget) State^ {
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
fn terminal_flush(st: State) State^ => if (st.terminal_line == "") st
    else {*:st, messages: [*st.messages, st.terminal_line], terminal_line: ""}
fn terminal_print(st: State, text) State^ {
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
fn record_diagnostic(st: State, code, message, origin = null) State^ {
    let active = stream.active_reader(st.input)^;
    let file = if (origin != null) origin.file else if (active == null) null else active.file_id;
    let found = [for (source in st.sources where source.id == file) source];
    let main = file != null and file == st.main_file;
    let foreign = file != null and not main and len(found) > 0;
    let bytes = binary(message)^;
    // native diagnostic formatting truncates long or NUL-containing messages.
    let exact_message = len(bytes) <= 511 and not any([for (byte in bytes) byte == 0]);
    let exhausted = active == null and st.input.root_eof == true;
    let known = ((file != null and (main or foreign)) or exhausted) and exact_message;
    let bytes_before = if (origin != null and len(found) > 0)
        slice(binary(found[0].data)^,0,origin.start)^ else null;
    let line = if (bytes_before != null) 1 + len([for (i,b in bytes_before where
        b == 13 or (b == 10 and (i == 0 or bytes_before[i - 1] != 13))) b])
        else if (active != null) active.line_no else 0;
    let record = {code: code, message: message,
        offset: if (origin != null and main) origin.start else st.input.last_main_offset,
        *:if (foreign) {file: found[0].name} else {},
        *:if (line > 0) {line: line} else {}};
    {*:st, diagnostics: [*st.diagnostics, code],
        diagnostic_records: if (known) [*st.diagnostic_records, record] else st.diagnostic_records}
}

fn parameter_diagnostics(st: State, errors, mode, index = 0) State^ {
    if (index >= len(errors)) st
    else {
        let origin = if (mode == "tex") errors[index].next else null;
        let diagnosed = record_diagnostic(st, if (mode == "tex") "tex-illegal-param" else "latex-illegal-param",
            "Illegal parameter number in definition", origin)^;
        parameter_diagnostics(diagnosed, errors, mode, index + 1)
    }
}

// native scans numeric math codes, reports the limitation and continues.
fn numeric_math(st: State, budget) State^ {
    let scanned = scan_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    else record_diagnostic({*:scanned.state, numeric_math_seen: true},
        "tex-unsupported-mathchar", "numeric math characters are not reproduced")
}

fn terminal_message(st: State, budget, is_error) State^ {
    let text = expanded_text(st, budget, true)^;
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
    else {*:closed.state, output: closed.state.output ++ [
        {*:raw[0], echo_span: closed.span, echo_count: len(raw)}, *slice(raw, 1, len(raw))^]}
}
// Generated tokens inherit the last source read for reconstruction and AST offsets.
fn emit_generated(st: State, tokens: map[], map_source: bool = false) State^ => {*:st, output:st.output ++
    [*[for (token in tokens) (let emitted = {*:token, emit_origin:st.input.last_main_offset},
        if (map_source) {*:emitted, emission_mapping:true} else emitted)]]}
fn emit_spacing(st: State, original, command, budget) State^ {
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
        emit_generated(closed.state, tokens)
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
fn discard_leader(st: State, budget) State^ {
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
fn emit_rule(st: State, original, command, budget) State^ {
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
        emit_generated(closed.state, tokens)
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
fn emit_character(st: State, budget) State^ {
    let scanned = scan_character_number(st, budget)^;
    if (scanned.state.issue != null) scanned.state
    // native TeX can retain surrogate tokens that Lambda strings cannot represent.
    else if (scanned.value >= 55296 and scanned.value <= 57343) unsupported(scanned.state, "char surrogate")
    else {
        let code = scanned.value;
        let category = if ((code >= 65 and code <= 90) or (code >= 97 and code <= 122)) 11 else 12;
        let token = {kind: "char", value: chr(code), cat: category, start: -1, end: -1,
            read_offset: scanned.state.input.last_main_offset, read_file: scanned.state.main_file};
        {*:scanned.state, output: scanned.state.output ++ [token]}
    }
}
fn emit_accent(st: State, budget) State^ {
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
            {*:character.state, output: character.state.output ++ [
                *[for (i, token in tokens) {*:token, emit_origin: character.state.input.last_main_offset,
                    *:if (i == 0) {reconstruct_text: raw, reconstruct_count: len(tokens)} else {}}]]}
        }
    }
}

fn write_message(st: State, budget) State^ {
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
fn output_stream(st: State, budget, opening) State^ {
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

fn show_line(st: State, text) State^ =>
    terminal_flush(terminal_print(terminal_flush(st)^, "> " ++ text ++ ".")^)

fn show_meaning(st: State) State^ {
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

fn show_the(st: State, budget) State^ {
    let result = expand_the({*:st, input: {*:st.input, captured_the: null}}, budget, false, true)^;
    if (result.issue != null) result
    else {
        let tokens = result.input.captured_the;
        let current = {*:result, input: {*:result.input, captured_the: null}, inspection_seen: true};
        show_line(current, token_text(current, tokens)^)
    }
}

fn show_box(st: State, budget) State^ {
    let index = scan_register(st, budget)^;
    if (index.state.issue != null) index.state
    else {
        let current = {*:index.state, inspection_seen: true};
        let suffix = if (current.boxes[string(index.value)] == null) "=void" else "=box";
        show_line(current, "\\box" ++ string(index.value) ++ suffix)
    }
}

fn show_tokens(st: State, budget) State^ {
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

fn immediate(st: State, budget) State^ {
    let read = expanded_next(st, budget);
    if (read.state.issue != null) read.state
    else if (primitive(read.state, read.token) == "write") write_message(read.state, budget)
    else if (primitive(read.state, read.token) == "openout") output_stream(read.state, budget, true)
    else if (primitive(read.state, read.token) == "closeout") output_stream(read.state, budget, false)
    else put_back(read.state, read.token)
}

fn set_codes(input, codes) any^ => stream.configure(input, codes, input.reader.endline)
fn set_code(codes, code, value) => {*:codes, *:map([chr(code), value])}

fn save_parameter(st: State, global, name, value) State^ => {*:st,
    input: if (name == "endlinechar") stream.configure(st.input, st.input.reader.catcodes, value) else st.input,
    parameters: {*:st.parameters, *:map([name, value])},
    groups: if (global) [for (scope in st.groups)
        {*:scope, parameters: {*:scope.parameters, *:map([name, value])}}] else st.groups}

fn assign_parameter(st: State, global, budget, name) State^ {
    let value = scan_number(optional_equals(st, budget), budget)^;
    if (value.state.issue != null) value.state else save_parameter(value.state, global, name, value.value)
}

fn assign_catcode(st: State, global, budget) State^ {
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
fn expl3_syntax(st: State, on) State^ {
    let saved = st.expl_syntax;
    if (on) {
        let previous = if (saved != null and saved.active == true) saved else {
            active: true, codes: expl3.save_codes(st.input.reader.catcodes),
            endline: st.parameters.endlinechar};
        let global = effective_global(st, false)^;
        // native loads the expl3 format on its first executed request.
        let configured = {*:st, expl3_format_requested: true, expl_syntax: previous,
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

fn expl3_cs_define(st: State, name, budget) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (expl3.fresh(name) and not latex_definable(meaning(st, target.token)))
        fail(current, "expl3-already-defined")
    else {
        // sequence membership redefines its item marker before expanding the body.
        let marked = if (target.token.value == "__seq_item:n") {*:st, expl3_seq_in_seen: true} else st;
        define({*:marked, input: stream.push(target.stream, [target.token])},
            effective_global(marked, expl3.global(name))^, true, expl3.protected(name), false, budget)
    }
}

// a new expl3 control sequence copies the source meaning globally.
fn expl3_cs_new_equal(st: State) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (not latex_definable(meaning(st, target.token))) fail(current, "expl3-already-defined")
    else {
        // an explicit equals sign preserves a source token that is itself `=`.
        let equal = {kind: "char", value: "=", cat: 12, start: -1, end: -1};
        let input = stream.push(target.stream, [target.token, equal])^;
        assign_let({*:current, input: input, expl3_tl_put_seen: true}, effective_global(st, true)^)
    }
}

// Generated variants are ordinary macros, so aliases and later base redefinitions retain TeX semantics.
fn expl3_install_variants(st: State, base, variants, protected_flag, index = 0) State^ {
    if (index >= len(variants) or st.issue != null) st
    else {
        let part = variants[index];
        let valid = all([for (token in part) token.kind == "char"]);
        let requested = if (valid) join([for (token in part) token.value], "")^ else "";
        let plan = if (valid) expl3.variant_plan(base, requested)^ else null;
        if (plan == null) unsupported(st, "expl3 variant signature")
        else if (plan.unchanged) expl3_install_variants(st, base, variants, protected_flag, index + 1)
        else {
            let ready = enable_expl_macro_sources(st, [{source:plan.source, protected_flag:false,
                source_mapping_guard:plan.guard}])^;
            let wrapper = latex_macro(0, false, protected_flag, false, [],
                [control_token(plan.helper), control_token(base)])^;
            let prior = ready.definitions["control:" ++ plan.name];
            if (ready.issue != null) ready
            else if (prior != null and not meanings_equal(prior, wrapper)^)
                unsupported(ready, "expl3 existing variant")
            else {
                let next = if (prior != null) ready else assign_meaning(ready, "control:" ++ plan.name, wrapper, true)^;
                expl3_install_variants(next, base, variants, protected_flag, index + 1)
            }
        }
    }
}

fn expl3_cs_generate_variant(st: State) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input:target.stream};
    if (not is_control(target.token) or target.token.kind != "control") fail(current, "tex-missing-cs")
    else {
        let variants = stream.argument(current.input, true)^;
        let advanced = {*:current, input:variants.stream};
        let base = meaning(advanced, target.token);
        if (variants.error != null) fail(advanced, variants.error)
        else if (base.kind == "undefined") fail(advanced, "expl3-undefined-function")
        else {
            let protected_flag = base.protected_flag == true or
                (base.kind == "primitive" and expl3.exists(base.name) and not expl3.expandable(base.name));
            expl3_install_variants(advanced, target.token.value, expl3.from_clist(variants.tokens)^, protected_flag)
        }
    }
}

fn expl3_keyval_calls(entries, absent, present, index = 0, output = []) any^ {
    if (index >= len(entries)) output
    else {
        let entry = entries[index];
        let value = if (entry.value == null) [] else [brace_token(true), *entry.value, brace_token(false)];
        let callback = if (entry.value == null) absent else present;
        expl3_keyval_calls(entries, absent, present, index + 1,
            [*output, *callback, brace_token(true), *entry.key, brace_token(false), *value])
    }
}

// Parse without expanding either callbacks or input; the resulting calls expand normally.
fn expl3_keyval_parse(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let absent = stream.argument(st.input, true)^;
        let present = stream.argument(absent.stream, true)^;
        let data = stream.argument(present.stream, true)^;
        let current = {*:st, input:data.stream, expansions:st.expansions + 1, expl3_keyval_seen:true};
        if (absent.error != null) fail(current, absent.error)
        else if (present.error != null) fail(current, present.error)
        else if (data.error != null) fail(current, data.error)
        else {
            let entries = expl3.keyval_entries(data.tokens)^;
            if (any([for (entry in entries) entry.invalid])) unsupported(current, "expl3 keyval syntax diagnostic")
            else {*:current, input:stream.push(current.input, expl3_keyval_calls(entries, absent.tokens, present.tokens)^)}
        }
    }
}

let KEYS_SCRATCH = ["l_keys_path_str", "l_keys_path_tl", "l_keys_key_str", "l_keys_key_tl",
    "l_keys_value_tl", "l_keys_choice_tl", "l__keys_module_str", "l__keys_inherit_str"]
fn expl3_keys_next_token() => {kind:"expl3-keys-next", value:"", cat:null, start:-1, end:-1}

// The l3keys path syntax discards spaces adjacent to module separators.
fn expl3_key_path(text) any^ => join([for (part in split(text, "/")^) trim(part)^], "/")

fn expl3_keys_scratch(st: State, values, index = 0) State^ {
    if (index >= len(values)) st
    else expl3_keys_scratch(assign_meaning(st, "control:" ++ values[index].name,
        values[index].value, false)^, values, index + 1)
}

fn expl3_keys_context(st: State, path, value) State^ {
    let parts = split(path, "/")^;
    let leaf = parts[len(parts) - 1];
    let parent = join(slice(parts, 0, len(parts) - 1)^, "/")^;
    let full = expl3.token_list(text_tokens(path)^);
    let key = expl3.token_list(text_tokens(leaf)^);
    expl3_keys_scratch(st, [
        {name:"l_keys_path_str", value:full}, {name:"l_keys_path_tl", value:full},
        {name:"l_keys_key_str", value:key}, {name:"l_keys_key_tl", value:key},
        {name:"l_keys_value_tl", value:expl3.token_list(value)},
        {name:"l__keys_module_str", value:expl3.token_list(text_tokens(parent)^)},
        {name:"l__keys_inherit_str", value:expl3.token_list([])}])
}

fn expl3_keys_begin(st: State, name) State^ {
    let module = stream.argument(st.input, true)^;
    let items = stream.argument(module.stream, true)^;
    let current = {*:st, input:items.stream, expl3_keys_seen:true};
    if (module.error != null) fail(current, module.error)
    else if (items.error != null) fail(current, items.error)
    else if (parameter_value(current, "globaldefs") != 0 or current.after_assignment != null)
        unsupported(current, "expl3 key assignment prefixes")
    else if (any([for (token in module.tokens) token.kind != "char" or token.cat == 1 or token.cat == 2]))
        unsupported(current, "expl3 key module tokens")
    else {
        let entries = expl3.keyval_entries(items.tokens)^;
        if (any([for (entry in entries) entry.invalid])) unsupported(current, "expl3 keyval syntax diagnostic")
        else {
            let saved = [for (variable in KEYS_SCRATCH)
                {name:variable, value:current.definitions["control:" ++ variable]}];
            let choice = meaning(current, control_token("l_keys_choice_int"))^;
            let frames = if (current.expl3_key_frames is array) current.expl3_key_frames else [];
            let frame = {module:expl3_key_path(token_text(current, module.tokens)^)^,
                entries:entries, index:0, defining:name == "keys_define:nn", saved:saved,
                choice:if (choice.kind == "register") count_value(current, choice.index) else null};
            {*:current, expl3_key_frames:[*frames, frame],
                input:stream.push(current.input, [expl3_keys_next_token()])}
        }
    }
}

fn expl3_key_code(st: State, path, tokens) State^ {
    let parsed = body_params(tokens, 1)^;
    if (parsed.issue != null) fail(st, parsed.issue)
    else assign_meaning(st, "control:key code > " ++ path,
        latex_macro(1, false, true, false, [], parsed.tokens)^, false)
}

fn expl3_choice_base(st: State, path) State^ {
    let parts = split(path, "/")^;
    let parent = join(slice(parts, 0, len(parts) - 1)^, "/")^;
    let parent_type = st.definitions["control:key type > " ++ parent];
    if (parent_type != null and parent_type.kind == "macro" and token_text(st, parent_type.body)^ == "choice")
        unsupported(st, "expl3 nested choice diagnostic")
    else {
        let body = [control_token("__keys_choice_find:n"), brace_token(true), parameter_token(0), brace_token(false)];
        let code = assign_meaning(st, "control:key code > " ++ path,
            latex_macro(1, false, true, false, [], body)^, false)^;
        assign_meaning(code, "control:key type > " ++ path, expl3.token_list(text_tokens("choice")^), false)
    }
}

// Choice values are stringified without expansion, then dispatched through normal key macros.
fn expl3_key_choice(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let arg = stream.argument(st.input, true)^;
        let current = {*:st, input:arg.stream, expansions:st.expansions + 1, expl3_keys_seen:true};
        let path = meaning(current, control_token("l_keys_path_str"))^;
        if (arg.error != null) fail(current, arg.error)
        else if (path.kind != "macro" or path.count != 0 or
            any([for (token in arg.tokens) token.kind != "char" or token.cat == 1 or token.cat == 2]))
            unsupported(current, "expl3 key choice tokens")
        else {
            let base = token_text(current, path.body)^;
            let choice = expl3_key_path(token_text(current, arg.tokens)^)^;
            let chosen = "key code > " ++ base ++ "/" ++ choice;
            let fallback = "key code > " ++ base ++ "/unknown";
            let selected = current.definitions["control:" ++ chosen];
            let target = if (selected != null and selected.kind != "undefined") chosen else fallback;
            let code = current.definitions["control:" ++ target];
            if (code == null or code.kind == "undefined") unsupported(current, "expl3 unknown key choice diagnostic")
            else {*:current, input:stream.push(current.input,
                [control_token(target), brace_token(true), *arg.tokens, brace_token(false)])}
        }
    }
}

fn expl3_choices_install(st: State, path, choices, body, index = 0) State^ {
    if (index >= len(choices) or st.issue != null) st
    else {
        let item = choices[index];
        if (any([for (token in item) token.kind != "char" or token.cat == 1 or token.cat == 2]))
            unsupported(st, "expl3 key choice tokens")
        else {
            let suffix = expl3_key_path(token_text(st, item)^)^;
            let wrapped = [control_token("tl_set:Nn"), control_token("l_keys_choice_tl"),
                brace_token(true), *item, brace_token(false), control_token("int_set:Nn"),
                control_token("l_keys_choice_int"), brace_token(true), *text_tokens(string(index + 1))^,
                brace_token(false), *body];
            let added = expl3_key_code(st, path ++ "/" ++ suffix, wrapped)^;
            expl3_choices_install(added, path, choices, body, index + 1)
        }
    }
}

fn expl3_key_choices(st: State, path, value) State^ {
    let source = stream.push(stream.from_reader(reader.reader("")), value)^;
    let choices = stream.argument(source, true)^;
    let body = stream.argument(choices.stream, true)^;
    let trailing = nonspace(body.stream)^;
    if (choices.error != null or body.error != null or trailing.token != null)
        unsupported(st, "expl3 key choices arguments")
    else expl3_choices_install(expl3_choice_base(st, path)^, path, expl3.from_clist(choices.tokens)^, body.tokens)
}

fn expl3_key_bool(st: State, path, property, value) State^ {
    if (len(value) != 1 or not is_control(value[0])) unsupported(st, "expl3 key boolean target")
    else {
        let target = value[0];
        let ready = if (latex_definable(meaning(st, target)^))
            assign_meaning(st, key(target), {kind:"chardef", value:0}, true)^ else st;
        let base = expl3_choice_base(ready, path)^;
        if (base.issue != null) base
        else {
            let global = starts_with(property, "bool_gset");
            let inverse = contains(["bool_set_inverse:N", "bool_gset_inverse:N"], property);
            let first_name = if (global) "bool_gset_" else "bool_set_";
            let truth = latex_macro(1, false, true, false, [],
                [control_token(first_name ++ (if (inverse) "false:N" else "true:N")), target])^;
            let falsehood = latex_macro(1, false, true, false, [],
                [control_token(first_name ++ (if (inverse) "true:N" else "false:N")), target])^;
            let first = assign_meaning(base, "control:key code > " ++ path ++ "/true", truth, false)^;
            let second = assign_meaning(first, "control:key code > " ++ path ++ "/false", falsehood, false)^;
            assign_meaning(second, "control:key default > " ++ path, expl3.token_list(text_tokens("true")^), false)
        }
    }
}

fn expl3_key_define(st: State, module, path, property, value) State^ {
    if (value == null) unsupported(st, "expl3 key property value diagnostic")
    else if (property == "code:n") expl3_key_code(st, path, value)
    else if (property == "default:n")
        assign_meaning(st, "control:key default > " ++ path,
            if (len(value) == 0) {kind:"undefined"} else expl3.token_list(value), false)
    else if (property == "tl_set:N" or property == "tl_gset:N") {
        if (len(value) != 1 or not is_control(value[0])) unsupported(st, "expl3 key token-list target")
        else {
            let target = value[0];
            // l3keys creates an absent target globally before defining the local key.
            let ready = if (latex_definable(meaning(st, target)^))
                assign_meaning(st, key(target), expl3.token_list([]), true)^ else st;
            let setter = control_token(if (property == "tl_set:N") "tl_set:Nn" else "tl_gset:Nn");
            let body = [setter, target, brace_token(true), parameter_token(0), brace_token(false)];
            assign_meaning(ready, "control:key code > " ++ path,
                latex_macro(1, false, true, false, [], body)^, false)
        }
    }
    else if (property == "meta:n") expl3_key_code(st, path,
        [control_token("keys_set:nn"), brace_token(true), *text_tokens(module)^, brace_token(false),
            brace_token(true), *value, brace_token(false)])
    else if (property == "choices:nn") expl3_key_choices(st, path, value)
    else if (contains(["bool_set:N", "bool_gset:N", "bool_set_inverse:N", "bool_gset_inverse:N"], property))
        expl3_key_bool(st, path, property, value)
    else if (property == "initial:n") {
        let code = st.definitions["control:key code > " ++ path];
        if (code == null or code.kind == "undefined") st
        else {
            let contextual = expl3_keys_context(st, path, value)^;
            {*:contextual, input:stream.push(contextual.input,
                [control_token("key code > " ++ path), brace_token(true), *value, brace_token(false)])}
        }
    }
    else unsupported(st, "expl3 key property " ++ property)
}

fn expl3_keys_next(st: State) State^ {
    let frames = st.expl3_key_frames;
    if (not (frames is array) or len(frames) == 0) fail(st, "expl3-key-frame")
    else if (parameter_value(st, "globaldefs") != 0 or st.after_assignment != null)
        unsupported(st, "expl3 key assignment prefixes")
    else {
        let tail = len(frames) - 1;
        let frame = frames[tail];
        if (frame.index >= len(frame.entries)) {
            let choice = meaning(st, control_token("l_keys_choice_int"))^;
            let restored = expl3_keys_scratch({*:st, expl3_key_frames:slice(frames, 0, tail)^}, frame.saved)^;
            if (frame.choice == null or choice.kind != "register")
                unsupported(restored, "expl3 key choice integer")
            else save_count(restored, choice.index, frame.choice, false)
        }
        else {
            let entry = frame.entries[frame.index];
            let advanced = {*:st, expl3_key_frames:[*slice(frames, 0, tail)^, {*:frame, index:frame.index + 1}],
                input:stream.push(st.input, [expl3_keys_next_token()])};
            if (any([for (token in entry.key) token.kind != "char" or token.cat == 1 or token.cat == 2]))
                unsupported(advanced, "expl3 key name tokens")
            else {
                let key_text = token_text(st, entry.key)^;
                let pieces = if (frame.defining) split(key_text, ".")^ else [];
                let named = if (frame.defining and len(pieces) > 1)
                    join(slice(pieces, 0, len(pieces) - 1)^, ".")^ else key_text;
                let key = expl3_key_path(named)^;
                let path = if (frame.module == "") key else frame.module ++ "/" ++ key;
                if (frame.defining and len(pieces) < 2) unsupported(advanced, "expl3 key property diagnostic")
                else if (key == "") unsupported(advanced, "expl3 key name diagnostic")
                else if (frame.defining)
                    expl3_key_define(advanced, frame.module, path, trim(pieces[len(pieces) - 1])^, entry.value)
                else {
                    let code = advanced.definitions["control:key code > " ++ path];
                    let fallback = advanced.definitions["control:key default > " ++ path];
                    let value = if (entry.value != null) entry.value
                        else if (fallback != null and fallback.kind == "macro") fallback.body else [];
                    if (code == null or code.kind == "undefined") unsupported(advanced, "expl3 unknown key diagnostic")
                    else {
                        let contextual = expl3_keys_context(advanced, path, value)^;
                        {*:contextual, input:stream.push(contextual.input,
                            [control_token("key code > " ++ path), brace_token(true), *value, brace_token(false)])}
                    }
                }
            }
        }
    }
}

fn expl3_clist_calls(items, callback, index = 0, output = []) any^ {
    if (index >= len(items)) output
    else expl3_clist_calls(items, callback, index + 1,
        [*output, callback, brace_token(true), *items[index], brace_token(false)])
}

// Inline callbacks are real protected macros so nested maps and break points share TeX scope.
fn expl3_clist_map(st: State, name) State^ {
    let literal = name == "clist_map_inline:nn";
    let first = if (literal) stream.argument(st.input, true)^
        else { let target = nonspace(st.input)^; {stream:target.stream, tokens:[target.token], error:null} };
    let body = stream.argument(first.stream, true)^;
    let current = {*:st, input:body.stream, expl3_clist_map_seen:true};
    if (first.error != null) fail(current, first.error)
    else if (body.error != null) fail(current, body.error)
    else if (current.after_assignment != null) unsupported(current, "expl3 comma-list map assignment prefix")
    else {
        let source = if (literal) expl3.token_list(first.tokens)
            else if (len(first.tokens) == 1 and is_control(first.tokens[0])) meaning(current, first.tokens[0])^
            else {kind:"undefined"};
        if (source.kind != "macro" or source.count != 0) unsupported(current, "expl3 comma-list map target")
        else {
            let items = expl3.from_clist(source.body)^;
            let saved = if (literal) assign_meaning(current, "control:l__clist_internal_clist",
                expl3.token_list(expl3.to_clist(items)^), effective_global(current, false)^)^ else current;
            if (len(items) == 0) saved
            else {
                let parsed = body_params(body.tokens, 1)^;
                let counter = meaning(saved, control_token("g__kernel_prg_map_int"))^;
                if (parsed.issue != null) fail(saved, parsed.issue)
                else if (counter.kind != "register") unsupported(saved, "expl3 map counter")
                else {
                    let depth = count_value(saved, counter.index) + 1;
                    let counted = save_count(saved, counter.index, depth, effective_global(saved, true)^)^;
                    let callback = control_token("__clist_map_" ++ string(depth) ++ ":w");
                    let installed = assign_meaning(counted, key(callback),
                        latex_macro(1, false, true, false, [], parsed.tokens)^, effective_global(counted, true)^)^;
                    let end = [control_token("prg_break_point:Nn"), control_token("clist_map_break:"),
                        brace_token(true), control_token("int_gdecr:N"), control_token("g__kernel_prg_map_int"), brace_token(false)];
                    {*:installed, input:stream.push(installed.input, [*expl3_clist_calls(items, callback)^, *end])}
                }
            }
        }
    }
}

fn expl3_tl_assign(st: State, name, budget) State^ {
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

// Replacement matches top-level token sequences without expanding either argument.
fn expl3_tl_replace(st: State, name) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input:target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else {
        let search = stream.argument(current.input, true)^;
        let replacement = if (search.error == null) stream.argument(search.stream, true)^
            else {stream:search.stream, tokens:[], error:null};
        let advanced = {*:current, input:replacement.stream};
        let prior = meaning(advanced, target.token);
        if (search.error != null) fail(advanced, search.error)
        else if (replacement.error != null) fail(advanced, replacement.error)
        else if (prior.kind == "undefined") fail(advanced, "expl3-undefined-variable")
        else if (prior.kind != "macro" or prior.count != 0 or prior.protected_flag == true)
            unsupported(advanced, "expl3 token-list target")
        else if (len(search.tokens) == 0 or any([for (token in search.tokens) charcat(token,1) or charcat(token,2)]))
            unsupported(advanced, "expl3 replacement search pattern")
        else {
            let body = expl3.replace_tokens(stored_tokens(prior.body), search.tokens,
                stored_tokens(replacement.tokens), contains(name, "_once:"))^;
            assign_meaning({*:advanced, expl3_tl_replace_seen:true}, key(target.token), expl3.token_list(body),
                effective_global(advanced, expl3.global(name))^)
        }
    }
}

fn expl3_tl_use(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let target = nonspace(st.input)^;
        let current = {*:st, input: target.stream, expansions: st.expansions + 1};
        if (not is_control(target.token)) fail(current, "tex-missing-cs")
        // expl3 treats a relax meaning as an undefined token-list variable.
        else if (latex_definable(meaning(st, target.token)^)) fail(current, "expl3-undefined-variable")
        else {*:current, input: stream.push(current.input, [target.token])}
    }
}

fn expl3_tl_empty(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let sequence = contains(["seq_if_empty:NTF", "seq_if_empty:NT", "seq_if_empty:NF"], name);
        let named = sequence or contains(["tl_if_empty:NTF", "tl_if_empty:NT", "tl_if_empty:NF"], name);
        let variable = contains(["tl_if_blank:VTF", "tl_if_blank:VT", "tl_if_blank:VF"], name);
        let target = if (named or variable) nonspace(st.input)^ else null;
        let argument = if (named or variable) {stream: target.stream, tokens: [], error: null} else stream.argument(st.input, true)^;
        let yes = if (name == "tl_if_empty:NF" or name == "tl_if_empty:nF" or name == "tl_if_blank:nF" or name == "tl_if_blank:VF" or name == "seq_if_empty:NF")
            {stream: argument.stream, tokens: [], error: null} else stream.argument(argument.stream, true)^;
        let no = if (name == "tl_if_empty:NT" or name == "tl_if_empty:nT" or name == "tl_if_blank:nT" or name == "tl_if_blank:VT" or name == "seq_if_empty:NT")
            {stream: yes.stream, tokens: [], error: null} else stream.argument(yes.stream, true)^;
        let current = {*:st, input: stream.note_foreign_expansion(no.stream)^,
            expansions: st.expansions + 1};
        let value = if ((named or variable) and target.token != null) meaning(current, target.token) else null;
        if (argument.error != null) fail(current, argument.error)
        else if (yes.error != null) fail(current, yes.error)
        else if (no.error != null) fail(current, no.error)
        else if ((named or variable) and target.token == null) fail(current, "tex-runaway")
        else if (variable and not is_control(target.token)) fail(current, "tex-missing-cs")
        else if (variable and value.kind == "undefined") fail(current, "expl3-undefined-variable")
        else if (variable and (value.kind != "macro" or value.count != 0)) unsupported(current, "expl3 token-list target")
        else {
            // sequence emptiness compares the current constant's TeX meaning.
            let empty = if (sequence) meanings_equal(value, meaning(current, control_token("c_empty_seq"))^)^
                else if (named) meanings_equal(value, expl3.token_list([]))^
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

fn expl3_tl_count(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let variable = name == "tl_count:N";
        let target = if (variable) nonspace(st.input)^ else null;
        let arg = if (variable) {stream: target.stream, tokens: [], error: null} else stream.argument(st.input, true)^;
        let current = {*:st, input: stream.note_foreign_expansion(arg.stream)^,
            expansions: st.expansions + 1};
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

fn expl3_tl_string(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let variable = name == "tl_to_str:V";
        let target = if (variable) nonspace(st.input)^ else null;
        let arg = if (variable) {stream: target.stream, tokens: [], error: null} else stream.argument(st.input, true)^;
        // variable expansion has a macro origin even if its value is empty.
        let current = {*:st, input: if (variable) stream.note_foreign_expansion(arg.stream)^ else arg.stream,
            expansions: st.expansions + 1};
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
fn expl3_tl_equal(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let named = contains(["tl_if_eq:NNTF", "tl_if_eq:NNT", "tl_if_eq:NNF"], name);
        let first = if (named) nonspace(st.input)^ else stream.argument(st.input, true)^;
        let second = if (named) nonspace(first.stream)^ else stream.argument(first.stream, true)^;
        let current = {*:st, input: second.stream, expansions: st.expansions + 1};
        if (not named and first.error != null) fail(current, first.error)
        else if (not named and second.error != null) fail(current, second.error)
        else if (named and (first.token == null or second.token == null)) fail(current, "tex-runaway")
        else {
            let equal = if (named) meanings_equal(meaning(current, first.token), meaning(current, second.token))^
                else stream.same_tokens(first.tokens, second.tokens)^;
            expl3_branch(current, name, equal, named)
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

// TeX sequences are macro token lists; decode bodies made outside the API too.
fn expl3_seq_body_items(input, items = []) any^ {
    let marker = stream.next(input)^;
    if (marker.token == null) items
    else if (marker.token.kind != "control" or marker.token.value != "__seq_item:n") null
    else {
        let item = stream.argument(marker.stream, true)^;
        if (item.error != null) null else expl3_seq_body_items(item.stream, [*items, item.tokens])
    }
}

fn expl3_seq_items(value) any^ {
    if (value.seq_items is array) value.seq_items
    else if (value.kind != "macro" or value.count != 0 or len(value.prefix) != 0 or value.optional == true) null
    else {
        let input = stream.push(stream.from_reader(reader.reader("")), value.body)^;
        let first = stream.next(input)^;
        if (first.token == null or first.token.kind != "control" or first.token.value != "s__seq") null
        else expl3_seq_body_items(first.stream)
    }
}

fn expl3_seq_assign(st: State, name) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (expl3.fresh(name) and not latex_definable(meaning(st, target.token)))
        fail(current, "expl3-already-defined")
    else {
        let splitting = expl3.sequence_split(name);
        let arg = if (expl3.sequence_empty_assignment(name) or expl3.sequence_reverse(name)) {stream: target.stream, tokens: [], error: null}
            else stream.argument(target.stream, true)^;
        let content = if (splitting and arg.error == null) stream.argument(arg.stream, true)^
            else {stream:arg.stream, tokens:[], error:null};
        let advanced = {*:current, input: content.stream};
        let old = meaning(advanced, target.token);
        let prior = expl3_seq_items(old)^;
        let uses_prior = not splitting and not expl3.sequence_empty_assignment(name) and not expl3.sequence_from_clist(name);
        let marked = if (uses_prior and prior is array and not (old.seq_items is array))
            {*:advanced, expl3_seq_body_seen: true} else advanced;
        if (arg.error != null) fail(marked, arg.error)
        else if (content.error != null) fail(marked, content.error)
        else if (splitting and any([for (token in arg.tokens) charcat(token,1) or charcat(token,2)]))
            unsupported(marked, "expl3 split delimiter group")
        else if (uses_prior and not (prior is array)) fail(marked, "expl3-invalid-sequence")
        // mutation of protected sequence macros needs native expansion semantics.
        else if (uses_prior and old.protected_flag == true and not (old.seq_items is array))
            unsupported(marked, "expl3 protected sequence assignment")
        else {
            // Sequence assignments retain the original, unexpanded TeX tokens.
            let items = if (expl3.sequence_empty_assignment(name)) []
                else if (expl3.sequence_from_clist(name)) expl3.from_clist(arg.tokens)^
                else if (splitting) expl3.from_split(content.tokens, arg.tokens, not contains(name,"keep_spaces"))^
                else if (expl3.sequence_reverse(name)) reverse(prior)^
                else if (expl3.sequence_put_left(name)) [arg.tokens, *prior]
                else [*prior, arg.tokens];
            let value = expl3_seq_value(items)^;
            // putting an item retains the original sequence body token for token.
            let assigned = if (uses_prior and not expl3.sequence_reverse(name)) {
                let added = expl3_seq_body([arg.tokens])^;
                let item_tokens = slice(added, 1, len(added))^;
                let body = if (expl3.sequence_put_left(name))
                    [old.body[0], *item_tokens, *slice(old.body, 1, len(old.body))^]
                    else [*old.body, *item_tokens];
                {*:value, body: stored_tokens(body)}
            } else value;
            assign_meaning(marked, key(target.token), assigned,
                effective_global(marked, expl3.global(name))^)
        }
    }
}

// keep the synthetic terminator and any residual tokens inside the index scope.
fn expl3_seq_index(st, tokens, budget) any^ {
    let end = {kind: "expl3-seq-index-end"};
    let relax = {*:control_token("relax"), seq_index_boundary: true};
    let scoped = {*:st, input: stream.push(st.input, [*tokens, relax, end])};
    let result = scan_expression(scoped, budget)^;
    if (result.state.issue != null) result
    else {
        let next = stream.next(result.state.input)^;
        let closed = if (next.token.seq_index_boundary == true) stream.next(next.stream)^ else next;
        if (closed.token.kind != "expl3-seq-index-end")
            {state: unsupported(result.state, "expl3 sequence index expression"), value: 0}
        else {state: {*:result.state, input: closed.stream}, value: result.value}
    }
}

fn expl3_seq_expand(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let target = nonspace(st.input)^;
        let current = {*:st, input: target.stream, expansions: st.expansions + 1};
        if (not is_control(target.token)) fail(current, "tex-missing-cs")
        else {
            let value = meaning(current, target.token);
            let items = expl3_seq_items(value)^;
            let marked = if (items is array and not (value.seq_items is array))
                {*:current, expl3_seq_body_seen: true} else current;
            if (not (items is array)) fail(marked, "expl3-invalid-sequence")
            else if (name == "seq_count:N")
                {*:marked, input: stream.push(marked.input, text_tokens(string(len(items))))}
            else if (name == "seq_item:Nn") {
                let argument = stream.argument(marked.input, true)^;
                let advanced = {*:marked, input: argument.stream};
                if (argument.error != null) fail(advanced, argument.error)
                else {
                    let index = expl3_seq_index(advanced, argument.tokens, budget)^;
                    let size = len(items);
                    let position = if (index.value < 0) size + index.value else index.value - 1;
                    let item = if (position < 0 or position >= size) [] else items[position];
                    let tokens = if (index.state.token_text_scan) [for (t in item)
                        {*:t, noexpand: is_control(t), direct_toks: true}] else stored_tokens(item);
                    if (index.state.issue != null) index.state
                    else {*:index.state, input: stream.push(index.state.input, tokens)}
                }
            }
            else {
                let separator = stream.argument(marked.input, true)^;
                let advanced = {*:marked, input: separator.stream};
                if (separator.error != null) fail(advanced, separator.error)
                else {
                    let tokens = stream.join_tokens(items, separator.tokens)^;
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

// The expl3 conditional suffix selects zero, one, or two raw branches.
fn expl3_branch(st: State, name, matched, foreign = false) State^ {
    if (contains(name, "_p:"))
        {*:st, input: stream.push(st.input, text_tokens(if (matched) "1" else "0")^)}
    else {
        let first = stream.argument(st.input, true)^;
        let second = if (ends_with(name, "TF")) stream.argument(first.stream, true)^
            else {stream:first.stream, tokens:[], error:null};
        let current = {*:st, input:if (foreign) stream.note_foreign_expansion(second.stream)^ else second.stream};
        if (first.error != null) fail(current, first.error)
        else if (second.error != null) fail(current, second.error)
        else {
            let take = if (ends_with(name, "F") and not ends_with(name, "TF")) not matched else matched;
            {*:current, input:stream.push(current.input, if (take) first.tokens else second.tokens)}
        }
    }
}

fn expl3_token_expandable(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let target = nonspace(st.input)^;
        let current = {*:st, input:target.stream, expansions:st.expansions + 1};
        if (target.token == null) fail(current, "tex-runaway")
        else expl3_branch(current, name, expandable(current, target.token)^)
    }
}

fn expl3_str_compare(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let left = stream.argument(st.input, true)^;
        let equality = starts_with(name, "str_if_eq");
        let relation = if (equality) {stream:left.stream, token:null} else nonspace(left.stream)^;
        let right = stream.argument(relation.stream, true)^;
        let current = {*:st, input: right.stream, expansions: st.expansions + 1};
        if (left.error != null) fail(current, left.error)
        else if (right.error != null) fail(current, right.error)
        else if (not equality and not (other(relation.token, "<") or other(relation.token, "=") or other(relation.token, ">")))
            fail(current, "tex-missing-relation")
        else {
            let a = expl3_compare_text(current, left.tokens)^;
            let b = expl3_compare_text(current, right.tokens)^;
            let matched = if (equality) a == b else if (relation.token.value == "<") a < b
                else if (relation.token.value == ">") a > b else a == b;
            expl3_branch(current, name, matched)
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

fn expl3_seq_sort(st: State, name, budget) State^ {
    let target = nonspace(st.input)^;
    let code = stream.argument(target.stream, true)^;
    let current = {*:st, input: code.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else if (code.error != null) fail(current, code.error)
    else {
        let value = meaning(current, target.token);
        let items = expl3_seq_items(value)^;
        let marked = if (items is array and not (value.seq_items is array))
            {*:current, expl3_seq_body_seen: true} else current;
        let comparator = body_params(code.tokens, 2)^;
        if (not (items is array)) fail(marked, "expl3-invalid-sequence")
        else if (value.protected_flag == true and not (value.seq_items is array))
            unsupported(marked, "expl3 protected sequence sort")
        else if (comparator.issue != null) fail(marked, comparator.issue)
        else {
            let grouped = begin_group(marked, null, true)^;
            let sorted = expl3_sort_items(grouped, comparator.tokens, items, budget)^;
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
        if (resolved.kind == "char" or resolved.kind == "chardef" or resolved.kind == "mathchardef")
            {state: unsupported(current, "text case character meaning"), tokens: output}
        // Undefined controls stay intact while their surrounding text is converted.
        else expl3_case_scan(current, budget, uppercase, null, [*output, t])
    }
    else if (t.kind == "char" and ord(t.value) >= 128)
        {state: unsupported(current, "Unicode text case"), tokens: output}
    else if (t.kind == "char")
        expl3_case_scan(current, budget, uppercase, null,
            [*output, {*:t, value: if (uppercase) upper(t.value) else lower(t.value), direct: false}])
    else {state: unsupported(current, "text case token"), tokens: output}
}

fn expl3_text_case(st: State, name, budget) State^ {
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
fn expl3_fp_eval(st: State, budget) State^ {
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

fn expl3_int_eval(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let result = expl3_int_argument({*:st, expansions: st.expansions + 1}, budget)^;
        if (result.state.issue != null) result.state
        else {*:result.state, input: stream.push(result.state.input, text_tokens(string(result.value)))}
    }
}

fn expl3_int_math(st: State, name, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let first = expl3_int_argument({*:st, expansions: st.expansions + 1, expl3_int_math_seen: true}, budget)^;
        if (first.state.issue != null) first.state
        else if (name == "int_abs:n" or name == "int_sign:n") {
            let value = if (name == "int_abs:n") abs(first.value)
                else if (first.value < 0) -1 else if (first.value > 0) 1 else 0;
            {*:first.state, input: stream.push(first.state.input, text_tokens(string(value)))}
        }
        else {
            let second = expl3_int_argument(first.state, budget)^;
            if (second.state.issue != null) second.state
            else {
                let division = contains(["int_div_truncate:nn", "int_div_round:nn", "int_mod:nn"], name);
                let invalid = division and second.value == 0;
                // TeX truncates toward zero, so the remainder follows the dividend's sign.
                let quotient = if (not division or invalid) 0
                    else (abs(first.value) div abs(second.value)) * if ((first.value < 0) != (second.value < 0)) -1 else 1;
                let result = if (invalid) {state: fail(second.state, "tex-arith-overflow"), value: 0}
                    else if (name == "int_div_round:nn") rounded_fraction(second.state, first.value, second.value)^
                    else {state: second.state, value: if (name == "int_div_truncate:nn") quotient
                        else if (name == "int_mod:nn") first.value - quotient * second.value
                        else if (name == "int_max:nn") max(first.value, second.value) else min(first.value, second.value)};
                if (result.state.issue != null) result.state
                else {*:result.state, input: stream.push(result.state.input, text_tokens(string(result.value)))}
            }
        }
    }
}

fn expl3_int_branches(input, has_yes, has_no) any^ {
    let yes = if (has_yes) stream.argument(input, true)^ else {stream: input, tokens: [], error: null};
    let no = if (has_no) stream.argument(yes.stream, true)^ else {stream: yes.stream, tokens: [], error: null};
    {yes: yes, no: no}
}

fn expl3_int_compare(st: State, name, budget) State^ {
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

fn expl3_int_compare_expr(st: State, name, budget) State^ {
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

fn expl3_int_test(st: State, name, budget) State^ {
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

fn assign_case_code(st: State, global, budget, upper_case) State^ {
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

fn assign_extra_code(st: State, global, budget, name) State^ {
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

fn shift_case(st: State, budget, upper_case) State^ {
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

// Tests opened during operand expansion each resume a skipped branch at EOF.
fn incomplete_skip_diagnostics(st: State, count) State^ => if (count <= 0) st else
    incomplete_skip_diagnostics(record_diagnostic(st,"tex-incomplete-if",
        "Incomplete conditional; all text was ignored after it")^,count - 1)

fn skip_conditional(st, depth, allow_else, active_depth = 0) ConditionalScan^ {
    let read = stream.next(st.input)^;
    let t = read.token;
    let current = {*:st, input: read.stream};
    if (t == null) {state: incomplete_skip_diagnostics(current,active_depth + 1)^, delimiter: "fi"}
    else if (primitive(st, t) != null and conditional_name(primitive(st, t)))
        skip_conditional(current, depth + 1, allow_else, active_depth)
    else if (primitive(st, t) == "fi") {
        if (depth == 0) {state: current, delimiter: "fi"}
        else skip_conditional(current, depth - 1, allow_else,
            if (depth <= active_depth) active_depth - 1 else active_depth)
    }
    else if (depth == 0 and allow_else and primitive(st, t) == "else") {state: current, delimiter: "else"}
    else skip_conditional(current, depth, allow_else, active_depth)
}

fn skip_case(st: State, remaining, depth = 0, origin = null, active_depth = 0) State^ {
    let read = stream.next(st.input)^;
    let t = read.token;
    let current = {*:st, input: read.stream};
    let name = primitive(st, t);
    if (t == null) incomplete_skip_diagnostics(current,active_depth + 1)
    else if (name != null and conditional_name(name)) skip_case(current, remaining, depth + 1, origin, active_depth)
    else if (name == "fi") {
        if (depth == 0) current else skip_case(current, remaining, depth - 1, origin,
            if (depth <= active_depth) active_depth - 1 else active_depth)
    }
    else if (depth == 0 and name == "else") {*:current, conditions: [*current.conditions, {kind:"false",origin:origin}]}
    else if (depth == 0 and name == "or" and remaining > 0) {
        if (remaining == 1) {*:current, conditions: [*current.conditions, {kind:"case",origin:origin}]}
        else skip_case(current, remaining - 1, depth, origin, active_depth)
    }
    else skip_case(current, remaining, depth, origin, active_depth)
}

// reserve the outer condition before expanding any operand tokens.
fn begin_scanning_conditional(st) => {*:st, conditions: [*st.conditions, {kind:"scanning",origin:st.condition_origin}]}

fn case_conditional(st: State, budget) State^ {
    let depth = len(st.conditions);
    let selected = scan_number(begin_scanning_conditional(st), budget)^;
    if (selected.state.issue != null) selected.state
    else {
        let prefix = slice(selected.state.conditions, 0, depth)^;
        let nested = slice(selected.state.conditions, depth + 1, len(selected.state.conditions))^;
        let origin = selected.state.conditions[depth].origin;
        if (selected.value == 0) {*:selected.state, conditions: [*prefix, {kind:"case",origin:origin}, *nested]}
        else skip_case({*:selected.state, conditions: prefix}, selected.value, len(nested), origin, len(nested))
    }
}

fn begin_conditional(st: State, truth, negate = false) State^ {
    if (truth != negate) {*:st, conditions: [*st.conditions, {kind:"true",origin:st.condition_origin}]}
    else {
        let skipped = skip_conditional(st, 0, true)^;
        if (skipped.state.issue != null) skipped.state
        else if (skipped.delimiter == "else") {*:skipped.state, conditions: [*st.conditions, {kind:"false",origin:st.condition_origin}]}
        else skipped.state
    }
}

// a nested test may still be open when an enclosing operand ends.
fn finish_scanned_conditional(st: State, depth, truth, negate) State^ {
    let prefix = slice(st.conditions, 0, depth)^;
    let nested = slice(st.conditions, depth + 1, len(st.conditions))^;
    let origin = st.conditions[depth].origin;
    if (truth != negate) {*:st, conditions: [*prefix, {kind:"true",origin:origin}, *nested]}
    else {
        let skipped = skip_conditional(st, len(nested), true, len(nested))^;
        if (skipped.state.issue != null) skipped.state
        else {*:skipped.state, conditions: [*prefix, *(if (skipped.delimiter == "else") [{kind:"false",origin:origin}] else [])]}
    }
}

fn numeric_conditional(st: State, name, budget, negate) State^ {
    let depth = len(st.conditions);
    let pending = begin_scanning_conditional(st);
    let left = if (name == "ifdim") scan_dimen(pending, budget)^ else scan_number(pending, budget)^;
    if (left.state.issue != null) left.state
    else if (name == "ifodd") finish_scanned_conditional(left.state, depth, (left.value % 2) != 0, negate)
    else {
        let relation = expanded_nonspace(left.state, budget)^;
        if (relation.state.issue != null) relation.state
        else if (relation.token == null) fail(relation.state, "tex-missing-relation")
        else {
            let valid = other(relation.token, "<") or other(relation.token, "=") or other(relation.token, ">");
            // TeX inserts an equals sign and replays the token as the right operand.
            let ready = if (valid) relation.state else record_diagnostic(
                put_back(relation.state, relation.token), "tex-missing-relation", "Missing = inserted for \\ifnum")^;
            let right = if (name == "ifdim") scan_dimen(ready, budget)^ else scan_number(ready, budget)^;
            if (right.state.issue != null) right.state
            else finish_scanned_conditional(right.state, depth,
                if (valid and relation.token.value == "<") left.value < right.value
                else if (valid and relation.token.value == ">") left.value > right.value else left.value == right.value, negate)
        }
    }
}

fn box_conditional(st: State, name, budget, negate) State^ {
    let depth = len(st.conditions);
    let index = scan_register(begin_scanning_conditional(st), budget)^;
    if (index.state.issue != null) index.state
    else {
        let box = index.state.boxes[string(index.value)];
        let matched = if (name == "ifvoid") box == null
            else if (name == "ifhbox") box != null and box.kind == "horizontal"
            else box != null and box.kind == "vertical";
        finish_scanned_conditional(index.state, depth, matched, negate)
    }
}

fn delimiters_equal(a, b, index = 0) any^ {
    if (len(a) != len(b)) false
    else if (index == len(a)) true
    else stream.same_tokens(a[index], b[index]) and delimiters_equal(a, b, index + 1)
}

// an unexpanded control token compares as relax for this read only.
fn ifx_meaning(st, token) any^ => if (token != null and token.noexpand == true)
    {kind: "primitive", name: "relax"} else meaning(st, token)

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
        (a.optional != true or stream.same_tokens(a.optional_default, b.optional_default)) and stream.same_tokens(a.prefix, b.prefix) and
        delimiters_equal(a.delimiters, b.delimiters) and stream.same_tokens(a.body, b.body)
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

fn unless_conditional(st: State, budget) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let read = stream.next(st.input)^;
        let current = {*:st, input: read.stream, expansions: st.expansions + 1};
        let name = primitive(st, read.token);
        if (read.token == null) current
        // TeX diagnoses the invalid prefix, then processes the untouched token.
        else if (name == null or not conditional_name(name) or name == "ifcase")
            record_diagnostic(put_back(current, read.token), "tex-bad-unless",
                "You can't use \\unless before that token")
        else condition(current, name, budget, true)
    }
}

fn input_eof_conditional(st: State, budget, negate) State^ {
    let depth = len(st.conditions);
    let scanned = scan_number(begin_scanning_conditional(st), budget)^;
    if (scanned.state.issue != null) scanned.state
    else {
        let invalid = scanned.value < 0 or scanned.value > 15;
        let diagnosed = if (invalid) record_diagnostic(scanned.state, "tex-bad-number",
            "Bad number (" ++ string(scanned.value) ++ ")")^ else scanned.state;
        let current = {*:diagnosed, input_eof_seen: true};
        let slot = if (invalid) 0 else scanned.value;
        let input = current.input_streams[string(slot)];
        finish_scanned_conditional(current, depth, input.open != true, negate)
    }
}

// Native reports where each unfinished conditional began, not where input ended.
fn conditional_origin(st) any^ {
    let active = stream.active_reader(st.input)^;
    let source = if (active == null) st.input.reader else active;
    let found = [for (entry in st.sources where entry.id == source.file_id) entry];
    let name = if (len(found) == 0 or found[0].name == "<input>") "<main>" else found[0].name;
    {name:name, line:source.line_no}
}

fn condition(st: State, name, budget, negate = false) State^ {
    if (st.expansions >= budget) fail(st, "tex-budget")
    else {
        let current = {*:st, expansions: st.expansions + 1,
            *:if (conditional_name(name)) {condition_origin:conditional_origin(st)^} else {}};
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
            let a = expanded_next(begin_scanning_conditional(current), budget)^;
            let b = expanded_next(a.state, budget)^;
            if (b.state.issue != null) b.state
            else {
                let first = character_key(b.state, a.token);
                let second = character_key(b.state, b.token);
                finish_scanned_conditional(b.state, depth,
                    if (name == "if") first.value == second.value else first.cat == second.cat, negate)
            }
        }
        else if (name == "ifcsname") {
            let scanned = csname_tokens(begin_scanning_conditional(current), budget)^;
            if (scanned.state.issue != null) scanned.state
            else {
                let t = {kind: "control", value: scanned.name};
                finish_scanned_conditional(scanned.state, depth, meaning(scanned.state, t).kind != "undefined", negate)
            }
        }
        else if (name == "ifx") {
            let a = stream.next(current.input)^;
            let b = stream.next(a.stream)^;
            begin_conditional({*:current, input: b.stream}, a.token != null and b.token != null and
                meanings_equal(ifx_meaning(current, a.token), ifx_meaning(current, b.token)), negate)
        }
        else if (name == "ifdefined") {
            let read = stream.next(current.input)^;
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
            if (depth == 0 or (st.conditions[depth - 1].kind != "true" and st.conditions[depth - 1].kind != "case"))
                fail(current, "tex-extra-fi")
            else {
                let skipped = skip_conditional(current, 0, false)^;
                if (skipped.state.issue != null) skipped.state
                else {*:skipped.state, conditions: slice(st.conditions, 0, depth - 1)}
            }
        }
        else if (name == "or") {
            if (depth == 0 or st.conditions[depth - 1].kind != "case") fail(current, "tex-extra-fi")
            else {
                let skipped = skip_conditional(current, 0, false)^;
                if (skipped.state.issue != null) skipped.state
                else {*:skipped.state, conditions: slice(st.conditions, 0, depth - 1)}
            }
        }
        else unsupported(current, name)
    }
}

fn after_assignment(before, after) State^ {
    if (after.issue != null or before.after_assignment == null) after
    else {*:after, input: stream.push(after.input, [before.after_assignment]), after_assignment: null}
}

fn save_after_assignment(st: State) State^ {
    let read = stream.next(st.input)^;
    {*:st, input: read.stream, after_assignment: read.token}
}

fn begin_group(st: State, token, explicit) State^ =>
    {*:st, groups: [*st.groups, {definitions: st.definitions, counts: st.counts, dimens: st.dimens, skips: st.skips, muskips: st.muskips, boxes: st.boxes, box_written: {}, toks: st.toks,
        lowercase: st.lowercase, uppercase: st.uppercase, code_tables: st.code_tables, current_font: st.current_font, parameters: st.parameters,
        catcodes: st.input.reader.catcodes, expl_syntax: st.expl_syntax, explicit: explicit, aftergroup: []}],
        output: if (token == null) st.output else st.output ++ [token]}

// discarded groups execute normally but remove their output on close.
fn open_discard_scope(st: State) State^ {
    let opened = begin_group(st, null, false)^;
    let depth = len(opened.groups);
    {*:opened, groups: [for (i, scope in opened.groups)
        if (i == depth - 1) {*:scope, discard_output: len(opened.output)} else scope]}
}
fn begin_discard_group(st: State, budget, indexed) State^ {
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
fn inject_box_hook(st: State, command) State^ {
    let hook = toks_value(st, parameter_index(if (command == "hbox") "everyhbox" else "everyvbox"));
    {*:st, input: stream.push(st.input, hook)}
}
fn begin_discard_box(st: State, budget, command) State^ {
    let opening = scan_box_opening(st, budget)^;
    if (opening.state.issue != null) opening.state
    else inject_box_hook(open_discard_scope(opening.state)^, command)
}
// inline boxes share scope and origin handling, even when they have no hook.
fn open_inline_body(opened, prefix, bare, hook) State^ {
    let depth = len(opened.groups);
    let origin = opened.input.last_main_offset;
    let current = {*:opened, groups: [for (i, scope in opened.groups)
            if (i == depth - 1) {*:scope, inline_box: not bare, inline_bare: bare} else scope],
        output: opened.output ++ [*[for (i, token in prefix) {*:token, emit_origin: origin,
            emit_file: opened.main_file, box_open: charcat(token, 1) and i == len(prefix) - 1}]]};
    if (hook == null) current else inject_box_hook(current, hook)
}

fn begin_inline_box(st: State, budget, command, movement = null) State^ {
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

// Reference commands share register scanning and native copy-or-consume behavior.
fn scan_box_reference(st, budget, command) any^ {
    if (command == "lastbox") {state:st,box:null}
    else {
        let index = scan_register(st,budget)^;
        if (index.state.issue != null) {state:index.state,box:null}
        else {
            let size = if (command == "vsplit") {
                let to = scan_word(index.state,"to",budget)^;
                let current = if (to.state.issue != null or to.found) to.state
                    else record_diagnostic(to.state,"tex-missing-to","Missing `to' inserted")^;
                if (current.issue != null) {state:current} else scan_dimen(current,budget)^
            } else {state:index.state};
            if (size.state.issue != null) {state:size.state,box:null}
            else take_box(size.state,index.value,contains(["copy","unhcopy","unvcopy"],command))
        }
    }
}

// Assigned boxes capture emitted material while running their ordinary group and hook.
fn assign_box(st: State, global, budget) State^ {
    let index = scan_register(st, budget)^;
    if (index.state.issue != null) index.state
    else {
        let scanned = scan_box_command(optional_equals(index.state,budget)^, budget)^;
        if (scanned.state.issue != null) scanned.state
        else if (contains(["hbox", "vbox", "vtop"], scanned.command)) {
            let opening = scan_box_opening(scanned.state,budget)^;
            if (opening.state.issue != null) opening.state
            else {
                let opened = begin_group(opening.state,null,false)^;
                let depth = len(opened.groups);
                let scope = {*:opened, box_assignment_seen:true,
                    groups:[for (i, item in opened.groups) if (i == depth - 1)
                        {*:item, box_target:index.value, box_global:global, box_start:len(opened.output),
                            box_kind:if(scanned.command == "hbox") "horizontal" else "vertical"} else item]};
                inject_box_hook(scope, scanned.command)
            }
        }
        else if (contains(["box","copy","vsplit","lastbox"],scanned.command)) {
            let taken = scan_box_reference(scanned.state,budget,scanned.command)^;
            if (taken.state.issue != null) taken.state
            else save_box({*:taken.state,box_assignment_seen:true},index.value,taken.box,global)
        }
        else unsupported(put_back(scanned.state,scanned.token), "setbox source")
    }
}

fn emit_box(st: State, budget, command) State^ {
    let taken = scan_box_reference(st,budget,command)^;
    if (taken.state.issue != null) taken.state
    else {*:taken.state,box_replay_seen:true,
        output:if(taken.box == null) taken.state.output else taken.state.output ++ [
            *[for (token in taken.box.output) {*:token,box_replayed:true}]]}
}

fn begin_vcenter(st: State, budget) State^ {
    let opening = scan_box_opening(st, budget)^;
    if (opening.state.issue != null) opening.state
    else {
        let opened = begin_group({*:opening.state, vcenter_seen: true}, null, false)^;
        open_inline_body(opened, [control_token("vcenter"), brace_token(true)], false, null)
    }
}

// movement wraps vertical shifts and otherwise emits the inline box contents.
fn move_inline_box(st: State, budget, command) State^ {
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
fn ship_inline_box(st: State, budget) State^ {
    let box = scan_box_command(st, budget)^;
    if (box.state.issue != null) box.state
    else if (not contains(["hbox", "vbox", "vtop"], box.command))
        unsupported(put_back(box.state, box.token), "shipout box source")
    else begin_discard_box({*:box.state, shipout_seen: true}, budget, box.command)
}

fn save_aftergroup(st: State) State^ {
    let read = stream.next(st.input)^;
    let depth = len(st.groups);
    let current = {*:st, input: read.stream};
    if (read.token == null) fail(current, "tex-runaway-aftergroup")
    else if (depth == 0) current
    else {*:current, groups: [for (index, scope in st.groups)
        if (index == depth - 1) {*:scope, aftergroup: [*scope.aftergroup, {*:read.token, direct: false}]} else scope]}
}

fn end_group(st: State, token, explicit, environment = false, math = false, recover = false) State^ {
    let depth = len(st.groups);
    if (depth == 0 or st.groups[depth - 1].explicit != explicit or
        ((st.groups[depth - 1].environment != null) != environment) or
        ((st.groups[depth - 1].math == true) != math))
        if (not recover) fail(st, if (explicit) "tex-extra-endgroup" else "tex-extra-brace")
        else {
            // source group errors diagnose without closing the active scope.
            let message = if (explicit) "Extra \\endgroup" else if (depth == 0) "Too many }'s"
                else "Extra }, or forgotten \\endgroup";
            let diagnosed = record_diagnostic(st, if (explicit) "tex-extra-endgroup" else "tex-extra-brace", message)^;
            if (explicit) diagnosed else {*:diagnosed, output: diagnosed.output ++ [
                {*:token, emit_origin: diagnosed.input.last_main_offset}]}
        }
    else {
        let scope = st.groups[depth - 1];
        let restored = {*:stream.configure(st.input, scope.catcodes, scope.parameters.endlinechar),
            everyeof: if (scope.toks["-1"] == null) [] else scope.toks["-1"]};
        let captured = if (scope.box_target != null) slice(st.output,scope.box_start,len(st.output))^ else null;
        let scoped = if (scope.ior_pool_saved is array) {*:st, ior_free_slots:scope.ior_pool_saved} else st;
        let returned = {*:scoped, expl_syntax: scope.expl_syntax, definitions: scope.definitions, counts: scope.counts, dimens: scope.dimens, skips: scope.skips, muskips: scope.muskips, boxes: scope.boxes, toks: scope.toks,
            lowercase: scope.lowercase, uppercase: scope.uppercase, code_tables: scope.code_tables, current_font: scope.current_font, parameters: scope.parameters,
            input: stream.push(restored, scope.aftergroup), groups: slice(st.groups, 0, depth - 1),
            output: if (scope.box_target != null) slice(st.output,0,scope.box_start)^
                else if (scope.discard_output != null) slice(st.output, 0, scope.discard_output)^
                else if (scope.inline_box == true) st.output ++ [
                    {*:brace_token(false), emit_origin: st.input.last_main_offset, emit_file: st.main_file, box_close: true}]
                else if (scope.inline_bare == true or token == null) st.output else st.output ++ [token]};
        if (scope.box_target == null) returned
        else save_box({*:returned,box_serial:returned.box_serial + 1},scope.box_target,
            {id:returned.box_serial + 1,kind:scope.box_kind,output:captured},scope.box_global)
    }
}

fn math_shift_token(st, token) any^ => charcat(token, 3) or
    (is_control(token) and meaning(st, token).kind == "char" and meaning(st, token).cat == 3)

fn begin_math(st: State, token, display, second = null, run_hook = false) State^ {
    let opened = begin_group(st, token, true)^;
    let depth = len(opened.groups);
    let math_groups = [for (i, group in opened.groups)
        if (i == depth - 1) {*:group, math: true, display: display} else group];
    let hook = if (run_hook) opened.toks[parameter_index(if (display) "everydisplay" else "everymath")] else null;
    {*:opened, groups: math_groups, math_depth: st.math_depth + 1,
        display_math: if (run_hook) display else st.display_math,
        output: if (second == null) opened.output else opened.output ++ [second],
        input: if (hook == null) opened.input else stream.push(opened.input, hook)}
}

fn finish_math(st: State, token, second = null, reset_display = true) State^ {
    let ended = end_group(st, null, true, false, true)^;
    if (ended.issue != null) ended else {*:ended, math_depth: if (st.math_depth > 0) st.math_depth - 1 else 0,
        display_math: if (reset_display) false else st.display_math,
        output: ended.output ++ [token, *(if (second == null) [] else [second])]}
}

fn math_shift(st: State, token) State^ {
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

fn latex_math_switch(st: State, token, cmd) State^ {
    if (cmd == "(" or cmd == "[") begin_math(st, token, false, null, false)
    else {
        let depth = len(st.groups);
        if (depth > 0 and st.groups[depth - 1].math == true) finish_math(st, token, null, false)
        else {*:st, output: st.output ++ [token]}
    }
}

fn control_token(name) => {kind: "control", value: name, cat: null, start: -1, end: -1}
fn brace_token(opening) => {kind: "char", value: if (opening) "{" else "}",
    cat: if (opening) 1 else 2, start: -1, end: -1}

fn latex_optional(input) any^ {
    let scanned = stream.optional_argument(input)^;
    {input: scanned.stream, present: scanned.present, tokens: scanned.tokens, raw: scanned.raw, issue: scanned.error}
}

fn decimal_prefix(text, position, value = 0, limit = 10) any^ {
    if (position >= len(text)) value
    else {
        let c = slice(text, position, position + 1)^;
        let n = index_of("0123456789", c);
        if (n == null) value else decimal_prefix(text, position + 1, min(limit, value * 10 + n), limit)
    }
}

fn latex_arg_count(tokens, limit = 10) any^ {
    let text = trim(join([for (t in tokens) t.value], ""));
    let negative = starts_with(text, "-");
    let signed = negative or starts_with(text, "+");
    let value = decimal_prefix(text, if (signed) 1 else 0, 0, limit)^;
    if (negative) -value else value
}

fn latex_macro(nargs, starred, protected_flag, optional, default_tokens, body) any^ =>
    {kind: "macro", count: nargs, prefix: [], delimiters: if (nargs == 0) [] else [for (i in 1 to nargs) []],
        brace_delim: false, long_ok: not starred, protected_flag: protected_flag,
        optional: optional, optional_default: default_tokens, body: body}

fn latex_definable(m) => m.kind == "undefined" or m.kind == "relax" or (m.kind == "primitive" and m.name == "relax")

// native command diagnostics limit names to 100 bytes.
fn latex_command_diagnostic(st: State, code, token, suffix) State^ {
    let bytes = binary(token.value)^;
    let name = utf8.decode(slice(bytes, 0, min(len(bytes), 100))^)^ { null };
    if (name == null) unsupported(st, "command diagnostic name encoding")
    else record_diagnostic(st, code, "LaTeX Error: Command \\" ++ name ++ suffix)
}

// Native definition targets end at the first closing brace, even inside a nested group.
fn latex_cs_group(input, found = null) any^ {
    let read = stream.next(input)^;
    let token = read.token;
    if (token == null or charcat(token,2)) {stream:read.stream,token:found,missing:found == null}
    else latex_cs_group(read.stream,if(found == null and is_control(token)) token else found)
}
fn latex_cs_target(input) any^ {
    let read = nonspace(input)^;
    let token = read.token;
    if (token == null) {stream:read.stream,token:control_token(chr(2) ++ "inaccessible"),missing:false}
    else if (charcat(token,1)) {
        let group = latex_cs_group(read.stream)^;
        {*:group,token:if(group.token == null) control_token(chr(2) ++ "inaccessible") else group.token}
    }
    else if (is_control(token)) {stream:read.stream,token:token,missing:false}
    else {stream:stream.push(read.stream,[token]),token:control_token(chr(2) ++ "inaccessible"),missing:true}
}
fn latex_cs_diagnostic(st: State, target) State^ => if (target.missing)
    record_diagnostic({*:st,input:target.stream},"latex-missing-cs","Missing control sequence in definition")
    else {*:st,input:target.stream}

fn latex_newcommand(st: State, name) State^ {
    let star = nonspace(st.input)^;
    let starred = other(star.token, "*");
    let remaining = if (starred) star.stream else if (star.token == null) star.stream else stream.push(star.stream, [star.token]);
    let target = latex_cs_target(remaining)^;
    let current = latex_cs_diagnostic(st,target)^;
    {
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
            let full_count = if (raw_count < 0 or raw_count > 9) latex_arg_count(count.tokens, 2147483648)^ else raw_count;
            // the native diagnostic is located at the count, before the body is scanned.
            let count_state = {*:current, input: count.input};
            let diagnosed = if (abs(full_count) >= 2147483648) unsupported(count_state, "command argument count overflow")
                else if (raw_count < 0 or raw_count > 9) record_diagnostic(count_state, "latex-bad-arg-count",
                    "Illegal number of arguments " ++ string(full_count))^ else count_state;
            let noted = {*:advanced, diagnostics: diagnosed.diagnostics, diagnostic_records: diagnosed.diagnostic_records,
                issue: diagnosed.issue, unsupported: diagnosed.unsupported};
            let old = meaning(noted, target.token);
            let definable = latex_definable(old);
            if (noted.issue != null) noted
            else if (name == "newcommand" and not definable)
                latex_command_diagnostic(noted, "latex-already-defined", target.token, " already defined")
            else if (name == "providecommand" and not definable) noted
            else {
                let ready = if (name == "renewcommand" and definable)
                    latex_command_diagnostic(noted, "latex-undefined", target.token, " undefined")^
                    else noted;
                // native converts parameters only when it will install the definition.
                let initial = body_params(body.tokens, nargs)^;
                let parsed = if (initial.issue != null) body_params(body.tokens, nargs, 0, [], "latex")^ else initial;
                let recovered = if (initial.issue != null) parameter_diagnostics(ready,parsed.errors,"latex")^ else ready;
                if (parsed.issue != null) fail(recovered, parsed.issue)
                else {
                    let macro = latex_macro(nargs, starred, name == "DeclareRobustCommand", default_arg.present,
                        default_arg.tokens, parsed.tokens)^;
                    assign_meaning(recovered, key(target.token), macro, false)
                }
            }
        }
    }
}

// Native accepts either a braced default or a single token in the signature.
fn document_spec_default(input) any^ {
    let read = nonspace(input)^;
    if (read.token == null) {input:read.stream,tokens:[],issue:null}
    else if (charcat(read.token,1)) {
        let value = stream.argument(stream.push(read.stream,[read.token]),true)^;
        {input:value.stream,tokens:value.tokens,issue:value.error}
    }
    else {input:read.stream,tokens:[read.token],issue:null}
}

// Native formats unknown argument types as one C byte, including non-UTF-8 bytes.
fn document_unknown_message(value) any^ {
    let byte = ord(value) % 256;
    if (byte == 0) "unknown argument type `"
    else "unknown argument type `" ++ parse(binary([byte])^,{type:"text"})^ ++ "'"
}

// Scan xparse specifications and retain diagnostics in their original order.
fn document_signature(input, specs = [], after_modifier = false, events = []) any^ {
    let read = if (after_modifier) stream.next(input)^ else nonspace(input)^;
    let t = read.token;
    if (t == null) {issue:null,valid:not after_modifier,count:len(specs),specs:specs,events:events}
    // Native records these modifiers while its current scanners use long arguments.
    else if (t.kind == "char" and contains(["+","!"],t.value))
        document_signature(read.stream,specs,true,events)
    else if (t.kind == "char" and t.value == ">") {
        let next = stream.next(read.stream)^;
        let processor = if (charcat(next.token,1)) stream.raw_argument(stream.push(next.stream,[next.token]))^
            else {stream:next.stream};
        let added = [*events,{code:"latex-argument-processor",message:"xparse argument processors are not applied"}];
        document_signature(processor.stream,specs,true,added)
    }
    else if (t.kind == "char" and contains(["m","s","o","g","v","b"],t.value))
        document_signature(read.stream,[*specs,{kind:t.value}],false,events)
    else if (t.kind == "char" and t.value == "t") {
        let want = stream.next(read.stream)^;
        // Missing delimiter fields are zero-initialized by the native parser.
        let token = if(want.token == null) {kind:"char",value:chr(0),cat:0} else want.token;
        document_signature(want.stream,[*specs,{kind:"t",want:token}],false,events)
    }
    else if (t.kind == "char" and (t.value == "e" or t.value == "E")) {
        let labels = document_spec_default(read.stream)^;
        let defaults = if(t.value == "E") document_spec_default(labels.input)^
            else {input:labels.input,tokens:[],issue:null};
        let added = [*events,{code:"latex-embellishment",message:"xparse embellishment arguments are not supported"}];
        if (labels.issue != null) {issue:labels.issue,specs:specs,events:events}
        else if (defaults.issue != null) {issue:defaults.issue,specs:specs,events:events}
        else document_signature(defaults.input,[*specs,{kind:t.value}],false,added)
    }
    else if (t.kind == "char" and contains(["d","D","r","R"],t.value)) {
        let opening = stream.next(read.stream)^;
        let closing = stream.next(opening.stream)^;
        let complete = opening.token != null and closing.token != null;
        let zero = {kind:"char",value:chr(0),cat:0};
        let next = if(complete) closing.stream else read.stream;
        let value = if (t.value == "D" or t.value == "R") document_spec_default(next)^
            else {input:next,tokens:[],issue:null};
        if (value.issue != null) {issue:value.issue,specs:specs,events:events}
        else document_signature(value.input,[*specs,{kind:t.value,
            open:if(complete) opening.token else zero,close:if(complete) closing.token else zero,default:value.tokens}],false,events)
    }
    else if (t.kind == "char" and contains(["O","G"],t.value)) {
        let value = document_spec_default(read.stream)^;
        if (value.issue != null) {issue:value.issue,specs:specs,events:events}
        else document_signature(value.input,[*specs,{kind:t.value,default:value.tokens}],false,events)
    }
    else if (t.kind == "char") {issue:null,valid:false,specs:specs,events:[*events,
        {code:"latex-arg-spec",message:document_unknown_message(t.value)^}]}
    else {issue:null,valid:false,specs:specs,events:events}
}

fn document_spec_diagnostics(st: State, events, index = 0) State^ {
    if (events == null or index >= len(events)) st
    else {
        let noted = record_diagnostic(st,events[index].code,events[index].message)^;
        document_spec_diagnostics(noted,events,index + 1)
    }
}

fn latex_document_command(st: State, name, budget) State^ {
    let environment = ends_with(name,"Environment");
    let target = if (environment) stream.argument(st.input,true)^ else latex_cs_target(st.input)^;
    let target_state = if(environment) {*:st,input:target.stream} else latex_cs_diagnostic(st,target)^;
    let named = if (environment) expand_argument_name(target_state,target.tokens,budget)^
        else {state:target_state,name:null};
    let signature = stream.argument(named.state.input,true)^;
    let signature_state = if(signature.error == null) {*:named.state,input:signature.stream}
        else recover_macro_argument({*:named.state,input:signature.stream},signature.error,signature.cause,control_token("relax"),false)^;
    let body = stream.argument(signature_state.input,true)^;
    let body_state = if(body.error == null) {*:signature_state,input:body.stream}
        else recover_macro_argument({*:signature_state,input:body.stream},body.error,body.cause,control_token("relax"),false)^;
    let final_body = if (environment) stream.argument(body_state.input,true)^
        else {stream:body_state.input,tokens:[],error:null};
    let current = if(final_body.error == null) {*:body_state,input:final_body.stream}
        else recover_macro_argument({*:body_state,input:final_body.stream},final_body.error,final_body.cause,control_token("relax"),false)^;
    if (environment and target.error != null) fail(current,target.error)
    else if (named.state.issue != null) named.state
    else if (current.issue != null) current
    else {
        let spec = document_signature(stream.push(stream.from_reader(reader.reader("")),signature.tokens))^;
        let diagnosed = document_spec_diagnostics(current,spec.events)^;
        let token = if (environment) control_token(named.name) else target.token;
        let old = meaning(diagnosed,token);
        let definable = latex_definable(old);
        let new = starts_with(name,"NewDocument");
        let renew = starts_with(name,"RenewDocument");
        let provide = starts_with(name,"ProvideDocument");
        let duplicate = new and not definable;
        let missing = renew and definable;
        let noted = if (duplicate) record_diagnostic(diagnosed,"latex-already-defined",
                "LaTeX Error: Command or environment already defined")^
            else if (missing) record_diagnostic(diagnosed,"latex-undefined",
                "LaTeX Error: Command or environment undefined")^ else diagnosed;
        if (duplicate or (provide and not definable)) noted
        else if (spec.issue != null) {*:noted,issue:spec.issue,unsupported:spec.unsupported}
        else if (spec.valid == false) noted
        else if (spec.count > 9) record_diagnostic(noted,"latex-bad-arg-count","too many arguments in signature")
        else {
            let parsed = body_params(body.tokens,spec.count)^;
            let ending = if (environment) body_params(final_body.tokens,0)^ else {issue:null,tokens:[]};
            if (parsed.issue != null) fail(noted,parsed.issue)
            else if (ending.issue != null) fail(noted,ending.issue)
            else {
                let base = latex_macro(spec.count,false,true,false,[],parsed.tokens)^;
                // Native stores xparse specifications separately from TeX parameter text.
                let specs = [for(item in spec.specs) if(item.kind == "b") {*:item,name:token.value} else item];
                let macro = {*:base,delimiters:[],document_specs:specs};
                let added = assign_meaning(noted,key(token),macro,false)^;
                if (not environment) added
                else {
                    let end_macro = latex_macro(0,false,false,false,[],ending.tokens)^;
                    assign_meaning(added,"control:end" ++ named.name,end_macro,false)
                }
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
fn counter_command_tokens(name, command, value = null) any^ {
    let opener = brace_token(true);
    let closer = brace_token(false);
    [control_token(command), opener, *text_tokens(name), closer,
        *(if (value == null) [] else [opener, *value, closer])]
}
// TeX allocates counts, dimensions and skips through adjacent counter slots.
fn latex_allocate_register(st: State, tracker) any^ {
    let next = count_value(st,tracker) + 1;
    let exhausted = next >= count_value(st,20);
    let index = if (exhausted) COUNT_REGISTER_LIMIT - 1 else next;
    let allocated = if (not exhausted) save_count(st,tracker,index,true)^
        else record_diagnostic(st,"tex-no-room","No room for a new register")^;
    {state:allocated,index:index}
}

// Like native cs_from_arg, a braced target ends at the first end-group token.
fn latex_length_group(st: State, tokens, target = null) any^ {
    let read = stream.next(st.input)^;
    let current = {*:st,input:read.stream};
    let token = read.token;
    let found = if (target == null and is_control(token)^) token else target;
    if (token == null or charcat(token,2)) {
        let raw = if (token == null) tokens else [*tokens,token];
        if (found != null) {state:current,target:found,raw:raw}
        else {state:record_diagnostic(current,"latex-missing-cs","Missing control sequence in definition")^,
            target:control_token(chr(2) ++ "inaccessible"),raw:raw}
    }
    else latex_length_group(current,[*tokens,token],found)
}

fn latex_length_target(st: State) any^ {
    let first = nonspace(st.input)^;
    let current = {*:st,input:first.stream};
    if (first.token == null) {state:current,target:control_token(chr(2) ++ "inaccessible"),raw:[]}
    else if (charcat(first.token,1)) latex_length_group(current,[first.token])
    else if (is_control(first.token)) {state:current,target:first.token,raw:[first.token]}
    else {state:record_diagnostic(put_back(current,first.token),"latex-missing-cs",
            "Missing control sequence in definition")^,
        target:control_token(chr(2) ++ "inaccessible"),raw:[]}
}

// LaTeX lengths share TeX skip registers and the kernel allocation counters.
fn latex_length_command(st: State, original, command, budget) State^ {
    let traced = begin_main_trace(st,original)^;
    let target = latex_length_target(traced)^;
    let current = target.state;
    let value = if (command == "newlength") {stream:current.input,tokens:[],raw:[],error:null}
        else stream.raw_argument(current.input)^;
    let scanned = {*:current,input:value.stream};
    if (current.issue != null) current
    else if (value.error != null) fail(scanned,value.error)
    else {
        let cs = target.target;
        let meaning_value = meaning(scanned,cs);
        let closed = end_main_trace(scanned,original)^;
        let original_tokens = [original,*target.raw,*value.raw];
        let emitted = if (closed.echo) {*:closed.state,output:closed.state.output ++ [
            {*:original,echo_span:closed.span,echo_count:len(original_tokens)},*slice(original_tokens,1,len(original_tokens))]}
            else closed.state;
        if (command == "newlength") {
            if (not latex_definable(meaning_value)) record_diagnostic(emitted,"latex-already-defined",
                "LaTeX Error: Command already defined (\\newlength)")
            else {
                let allocated = latex_allocate_register(emitted,12)^;
                assign_meaning(allocated.state,key(cs),{kind:"glue-register",index:allocated.index,mu:false},true)
            }
        }
        else {
            let kind = if (meaning_value.kind == "primitive") primitives.parameter_kind(meaning_value.name) else null;
            let assignable = contains(["register","dimension-register","glue-register"],meaning_value.kind) or
                contains(["int","dimen","glue","mu"],kind);
            let synthetic = [control_token(command),brace_token(true),cs,brace_token(false),
                brace_token(true),*value.tokens,brace_token(false)];
            let shown = if (closed.echo) emitted else {*:emitted,output:emitted.output ++ [
                *[for (token in synthetic) {*:token,emit_origin:emitted.input.last_main_offset}]]};
            let assigned = if (not assignable) shown else {
                let assignment = [*(if (command == "addtolength") [control_token("advance")] else []),
                    cs,*stored_tokens(value.tokens),control_token("relax")];
                step({*:shown,input:stream.push(shown.input,assignment)},budget)^
            };
            if (closed.echo) assigned
            else {
                // Native serializes the value after assignment with the current TeX print settings.
                // Native stores active-character names with a U+0001 prefix.
                let target_name = if (cs.kind == "active") chr(1) ++ cs.value else cs.value;
                let raw = "\\" ++ command ++ "{\\" ++ target_name ++ "}{" ++ token_text(assigned,value.tokens)^ ++ "}";
                let at = len(emitted.output);
                let first = {*:assigned.output[at],reconstruct_text:raw,reconstruct_count:len(synthetic)};
                {*:assigned,output:[*slice(assigned.output,0,at),first,*slice(assigned.output,at + 1,len(assigned.output))]}
            }
        }
    }
}

fn latex_newcounter(st: State, original, budget) State^ {
    let traced = begin_main_trace(st, original)^;
    let named = latex_counter_name(traced, budget)^;
    let opt = latex_optional(named.state.input)^;
    let current = {*:named.state, input: opt.input};
    if (current.issue != null) current
    else if (opt.issue != null) fail(current, opt.issue)
    else {
        let existing = counter_meaning(current, named.name)^;
        if (existing.kind == "register") {
            // Public parsing requires the native message and location, not only its code.
            let closed = end_main_trace(current, original)^;
            let emitted = if (closed.echo) {*:closed.state, output: closed.state.output ++ [original, *named.raw, *opt.raw]} else closed.state;
            record_diagnostic(emitted, "latex-counter-defined", "LaTeX Error: Counter `" ++ named.name ++ "' already defined")
        }
        else {
            let allocated = latex_allocate_register(current,10)^;
            let registered = assign_meaning(allocated.state, counter_key(named.name),
                {kind: "register", index: allocated.index}, true)^;
            let body = counter_command_tokens(named.name, "arabic")^;
            let formatted = assign_meaning(registered, "control:the" ++ named.name,
                latex_macro(0, false, false, false, [], body)^, true)^;
            let parent = if (opt.present) expand_argument_name(formatted, opt.tokens, budget)^
                else {state: formatted, name: ""};
            let closed = end_main_trace(parent.state, original)^;
            let emitted = if (closed.echo) {*:closed.state, output: closed.state.output ++ [original, *named.raw, *opt.raw]} else closed.state;
            if (emitted.issue != null) emitted
            else {*:emitted, counter_resets: if (opt.present)
                [*emitted.counter_resets, {parent: parent.name, child: named.name}] else emitted.counter_resets,
                engine_counters: if (closed.echo) emitted.engine_counters else [*emitted.engine_counters, named.name]}
        }
    }
}
fn reset_counter_children(st: State, parent, index = 0, depth = 0) State^ {
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
        else {state: record_diagnostic(put_back(after.state, after.token), "latex-counter-value", "counter value has trailing material")^, value: value.value}
    }
}
fn latex_counter_command(st: State, original, command, budget) State^ {
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
        else if (echo) {*:stepped, output: stepped.output ++ [original, *raw]}
        else if (contains(stepped.engine_counters, named.name)) stepped
        else emit_generated(stepped, counter_command_tokens(named.name, command, if (has_value and len(arg.tokens) > 0)
            if (found.kind == "register") text_tokens(string(next)) else arg.tokens else null)^, true)
    }
}
fn counter_symbol(value) => if (value == 1) "*" else if (value == 2) "\\dag"
    else if (value == 3) "\\ddag" else if (value == 4) "\\S" else if (value == 5) "\\P"
    else if (value == 6) "\\textbardbl" else if (value == 7) "**"
    else if (value == 8) "\\dag\\dag" else if (value == 9) "\\ddag\\ddag" else ""
fn latex_counter_format(st: State, original, command, budget) State^ {
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
                let checked = if (valid) named.state else record_diagnostic(named.state, "latex-counter-too-large", "LaTeX Error: Counter too large")^;
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

fn latex_newenvironment(st: State, mode, budget) State^ {
    let star = nonspace(st.input)^;
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
                record_diagnostic(advanced, "latex-already-defined", "LaTeX Error: Environment " ++ named.name ++ " already defined")
            else {
                // the AST needs the full diagnostic, including its source location.
                let ready = if (mode == "renewenvironment" and definable)
                    record_diagnostic(advanced, "latex-undefined", "LaTeX Error: Environment " ++ named.name ++ " undefined")^
                    else advanced;
                let begin_macro = latex_macro(nargs, starred, false, default_arg.present, default_arg.tokens, first.tokens)^;
                let end_macro = latex_macro(0, starred, false, false, [], final_body.tokens)^;
                let added = assign_meaning(ready, "control:" ++ named.name, begin_macro, false)^;
                assign_meaning(added, "control:end" ++ named.name, end_macro, false)
            }
        }
    }
}

fn begin_environment(st: State, name, passthrough) State^ {
    let opened = begin_group(st, null, true)^;
    let final_index = len(opened.groups) - 1;
    {*:opened, groups: [for (i, scope in opened.groups)
        if (i == final_index) {*:scope, environment: name, passthrough: passthrough} else scope]}
}

fn latex_document_hook(st: State, is_begin) State^ {
    let code = stream.argument(st.input, true)^;
    let current = {*:st, input: code.stream};
    if (code.error != null) fail(current, code.error)
    else if (is_begin and st.in_document) {*:current, input: stream.push(current.input, code.tokens)}
    else if (is_begin) {*:current, begin_hooks: [*st.begin_hooks, *stored_tokens(code.tokens)]}
    else {*:current, end_hooks: [*st.end_hooks, *stored_tokens(code.tokens)]}
}

// native echoes direct environment source, but synthesizes expanded names.
fn environment_output(st, original, raw, name, command, echo, span) map[]^ {
    let source = [original, *raw];
    if (echo) [{*:original, echo_span: span, echo_count: len(source)}, *raw]
    else [for (token in [control_token(command), brace_token(true), *text_tokens(name), brace_token(false)])
        {*:token, emit_origin: st.input.last_main_offset}]
}

// preserve raw environment bytes and resume the same reader after the terminator.
fn latex_raw_environment(st: State, original, name, source: map) State^ {
    let captured = reader.raw_environment(source.reader,name)^;
    let diagnosed = if (captured.closed) st else record_diagnostic(st,"latex-missing-end",
        "\\begin{" ++ name ++ "} has no matching \\end{" ++ name ++ "}")^;
    let input = stream.replace_reader(diagnosed.input,source.index,captured.reader)^;
    let span = {file:original.file,start:original.start,end:captured.end,
        first_seq:original.seq,last_seq:source.reader.seq};
    {*:diagnosed,input:input,output:diagnosed.output ++ [
        {*:original,echo_span:span,echo_count:1,raw_environment:name}]}
}

fn latex_begin_regular(st: State, original, argument, named, closed, budget, begin_hooks) State^ {
    let m = meaning(st, control_token(named.name));
    let custom = named.name != "document" and m.kind == "macro";
    let opened = begin_environment(st, named.name, not custom)^;
    if (custom) {
        let current_env = latex_macro(0, false, false, false, [], text_tokens(named.name))^;
        let with_name = assign_meaning(opened, "control:@currenvir", current_env, false)^;
        {*:with_name, input: stream.push(with_name.input, [control_token(named.name)])}
    }
    else {
        let emitted = environment_output(opened, original, argument.raw, named.name, "begin", closed.echo, closed.span)^;
        let passed = {*:opened, output: opened.output ++ emitted};
        if (named.name != "document")
            // native scans passthrough environment arguments in either expansion mode.
            if (passed.math_mode != true) constructor_arguments(passed, budget)^ else passed
        else {*:passed, in_document: true, begin_hooks: [], input: stream.push(passed.input, begin_hooks)}
    }
}

// Native continues with the partial environment name after an argument scan error.
fn recover_environment_argument(traced: State, argument) State^ {
    let scanned = {*:traced, input: argument.stream};
    if (argument.error == null) scanned else recover_macro_argument(scanned,argument.error,
        if (argument.error == "tex-extra-brace") "brace" else if (len(argument.raw) == 0) "missing" else "eof",
        control_token("relax"),false)
}

fn latex_begin(st: State, original, budget) State^ {
    let traced = begin_main_trace(st, original)^;
    let argument = stream.raw_argument(traced.input)^;
    let current = recover_environment_argument(traced,argument)^;
    if (current.issue != null) current
    else {
        let named = expand_argument_name(current, argument.tokens, budget)^;
        let closed = end_main_trace(named.state, original)^;
        if (closed.state.issue != null) closed.state
        else if (reader.raw_environment_name(named.name)^) {
            let source = stream.raw_reader(closed.state.input,original)^;
            if (source == null) {
                let diagnosed = record_diagnostic(closed.state,"latex-raw-environment",
                    "environment " ++ named.name ++ " needs its own source text")^;
                latex_begin_regular(diagnosed,original,argument,named,closed,budget,st.begin_hooks)
            }
            else latex_raw_environment(closed.state,original,named.name,source)
        }
        else latex_begin_regular(closed.state,original,argument,named,closed,budget,st.begin_hooks)
    }
}

fn latex_end(st: State, original, budget) State^ {
    let traced = begin_main_trace(st, original)^;
    let argument = stream.raw_argument(traced.input)^;
    let current = recover_environment_argument(traced,argument)^;
    if (current.issue != null) current
    else {
        let named = expand_argument_name(current, argument.tokens, budget)^;
        let closed = end_main_trace(named.state, original)^;
        let ready = closed.state;
        let depth = len(ready.groups);
        if (ready.issue != null) ready
        else if (named.name == "document") {
            // native finish_document appends the tail only with the root reader on top.
            let root_tail = len(ready.end_hooks) == 0 and len(ready.input.levels) == 0;
            let close = {kind: "document-close", original: original, raw: argument.raw,
                echo: closed.echo and len(ready.end_hooks) == 0, span: closed.span, root_tail: root_tail};
            {*:ready, end_hooks: [], input: stream.push(ready.input, [*ready.end_hooks, close])}
        }
        else if (depth == 0 or ready.groups[depth - 1].environment != named.name) {
            let emitted = environment_output(ready, original, argument.raw, named.name, "end", closed.echo, closed.span)^;
            // native reports the current opening environment at this source position.
            let opened = if (depth == 0 or ready.groups[depth - 1].environment == null) "?"
                else ready.groups[depth - 1].environment;
            let diagnosed = record_diagnostic(ready,"latex-env-mismatch",
                "LaTeX Error: \\begin{" ++ opened ++ "} ended by \\end{" ++ named.name ++ "}")^;
            {*:diagnosed, output: diagnosed.output ++ emitted}
        }
        else if (ready.groups[depth - 1].passthrough) {
            let emitted = environment_output(ready, original, argument.raw, named.name, "end", closed.echo, closed.span)^;
            let ended = end_group(ready, null, true, true)^;
            {*:ended, output: ended.output ++ emitted}
        }
        else {*:ready, input: stream.push(ready.input,
            [control_token("end" ++ named.name), {kind: "env-close", value: named.name}])}
    }
}

// LaTeX's text macros retain letter and space catcodes from the active table.
fn latex_text_token(st: State, token) any^ {
    let cat = reader.catcode(token.value, st.input.reader.catcodes);
    {*:token, cat:if (cat == 10 or cat == 11) cat else 12}
}

fn define_text_macro(st: State, name, value, global = true) State^ {
    let tokens = [for (token in text_tokens(value)^) latex_text_token(st, token)^];
    let macro = latex_macro(0, true, false, false, [], tokens)^;
    assign_meaning(st, "control:" ++ name, macro, global)
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
    else {
        let entry = st.resources[scope][name];
        if (entry == null and st.resource_requests)
            {state:request_resource(st, scope, name), available:false, found:false}
        else {state:st, available:true, found:entry != null and entry.missing != true and entry.no_base != true}
    }
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

fn declaration_tokens(name, opts, names, origin) any^ {
    let opener = brace_token(true);
    let closer = brace_token(false);
    let option = if (opts == "") "" else "[" ++ opts ++ "]";
    let named = join(names, ",");
    let tokens = [control_token(name), *text_tokens(option), opener, *text_tokens(named), closer];
    let raw = "\\" ++ name ++ option ++ "{" ++ named ++ "}";
    // native emits the complete declaration as raw text at one source origin.
    [for (i, token in tokens) {*:token, emit_origin: origin,
        *:if (i == 0) {reconstruct_text:raw,reconstruct_count:len(tokens)} else {}}]
}

fn latex_usepackage(st: State, original, budget) State^ {
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
        // native chooses the expl3 format before resolving the package list.
        let requested = if (contains(names,"expl3")) {*:current, expl3_format_requested: true} else current;
        // Restart before the kernel's expl3 bootstrap recursively expands itself.
        let planned = if (requested.auto_expl3 and not requested.script_expl3 and contains(names,"expl3"))
            {state:fail(requested, "tex-expl3-restart"), loads:[], passthrough:[]}
            else package_plan(requested, names, opts)^;
        if (planned.state.issue != null) planned.state
        else {
            let closed = end_main_trace(planned.state, original)^;
            let raw = [original, *opt.raw, *name_arg.raw, *version_arg.raw];
            // a script-handled name must not leak into an echoed package declaration.
            let passed = if (len(planned.loads) == 0 and len(planned.passthrough) == len(names) and closed.echo)
                [{*:original, echo_span: closed.span, echo_count: len(raw)}, *slice(raw, 1, len(raw))^]
                else if (len(planned.passthrough) > 0)
                    declaration_tokens("usepackage", opts, planned.passthrough, closed.state.input.last_main_offset)^
                else [];
            {*:closed.state, output: closed.state.output ++ passed,
                input: stream.push(closed.state.input, planned.loads)}
        }
    }
}

fn start_package(st: State, request, budget) State^ {
    if (not request.is_class and package_loaded(st, request.name, false)) st
    else {
        let suffix = if (request.is_class) ".cls" else ".sty";
        let fetched = captured_resource(st, request.name ++ suffix, request.scope)^;
        if (fetched.state.issue != null) fetched.state
        else if (not fetched.available) unsupported(fetched.state, "package resource")
        else if (not fetched.found) {*:fetched.state, diagnostics: [*fetched.state.diagnostics, "tex-file-not-found"]}
        else if (len(fetched.state.input.levels) >= 6000) fail(fetched.state, "tex-capacity")
        else {
            let decoded = decode_source(fetched.data)^;
            if (not decoded.valid) fail(fetched.state, "tex-invalid-utf8")
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
                if (not (source_input is map)) fail(recorded, "tex-resource-invalid")
                else {
                let configured = stream.configure(source_input,
                    set_code(source_input.reader.catcodes, 64, 11), source_input.reader.endline)^;
                let child = {*:reader.reader(decoded.text, fetched.id), origin: request.scope,
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
                }
            }
        }
    }
}

fn finish_package(st: State) State^ {
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

fn package_end_hook(st: State) State^ {
    let code = stream.argument(st.input, true)^;
    let current = {*:st, input: code.stream};
    let depth = len(st.package_frames);
    if (code.error != null) fail(current, code.error)
    else if (depth == 0) {*:current, input: stream.push(current.input, code.tokens)}
    else {*:current, package_frames: [for (i, frame in st.package_frames)
        if (i == depth - 1) {*:frame, end_hook: [*frame.end_hook, *stored_tokens(code.tokens)]} else frame]}
}

fn finish_package_hook(st: State) State^ {
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

fn latex_provides(st: State, cmd, budget) State^ {
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
fn latex_provides_expl(st: State, cmd, budget) State^ {
    // The format implements these declarations as a long six-argument macro;
    // its first two arguments are supplied by the public command itself.
    let args = collect_args(st.input, [[],[],[],[]], false, true)^;
    let current = {*:st, input: args.input};
    if (args.issue != null) recover_macro_argument(current, args.issue, args.cause,
        control_token("@expl@provides@file@@Nnnnnn"))
    else {
        let named = expand_argument_name(current, args.args[0], budget)^;
        let dated = expand_argument_name(named.state, args.args[1], budget)^;
        let numbered = expand_argument_name(dated.state, args.args[2], budget)^;
        let described = expand_argument_name(numbered.state, args.args[3], budget)^;
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

fn latex_declare_option(st: State, budget) State^ {
    let star = nonspace(st.input)^;
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

fn latex_execute_options(st: State, budget) State^ {
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

fn latex_process_options(st: State) State^ {
    let star = nonspace(st.input)^;
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

fn latex_pass_options(st: State, budget) State^ {
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

fn latex_if_package_with(st: State, is_class, budget) State^ {
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

fn latex_if_file_exists(st: State, include, budget) State^ {
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

fn latex_ifnextchar(st: State, star) State^ {
    let wanted = if (star) {stream: st.input, tokens: [], error: null} else stream.argument(st.input, true)^;
    let yes = stream.argument(wanted.stream, true)^;
    let no = stream.argument(yes.stream, true)^;
    let following = nonspace(no.stream)^;
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

// Commas delimit only top-level items; an enclosing group is removed per item.
fn latex_for_items(tokens, index = 0, depth = 0, item = [], items = []) any^ {
    if (index >= len(tokens)) if (len(tokens) == 0) [] else [*items, stream.strip_outer(item)^]
    else {
        let token = tokens[index];
        if (depth == 0 and other(token, ","))
            latex_for_items(tokens, index + 1, 0, [], [*items, stream.strip_outer(item)^])
        else latex_for_items(tokens, index + 1,
            depth + if (charcat(token, 1)) 1 else if (charcat(token, 2)) -1 else 0,
            [*item, token], items)
    }
}

fn latex_tfor_group_end(tokens, index, depth) any^ {
    if (index >= len(tokens)) index
    else {
        let token = tokens[index];
        let next = depth + if (charcat(token, 1)) 1 else if (charcat(token, 2)) -1 else 0;
        if (next == 0) index + 1 else latex_tfor_group_end(tokens, index + 1, next)
    }
}

fn latex_tfor_items(tokens, index = 0, items = []) any^ {
    if (index >= len(tokens)) items
    else if (charcat(tokens[index], 10)) latex_tfor_items(tokens, index + 1, items)
    else {
        let end = if (charcat(tokens[index], 1)) latex_tfor_group_end(tokens, index, 0)^ else index + 1;
        let item = stream.strip_outer(slice(tokens, index, end))^;
        latex_tfor_items(tokens, end, [*items, item])
    }
}

// Assign each loop value through the normal definition path before its body.
fn latex_for_steps(variable, items, body, index = 0, output = []) any^ {
    if (index >= len(items)) output
    else latex_for_steps(variable, items, body, index + 1, [*output, control_token("def"),
        variable, brace_token(true), *stored_tokens(items[index]), brace_token(false), *stored_tokens(body)])
}

// TeX inserts an inaccessible control sequence and leaves a bad target unread.
fn latex_required_control(st: State) any^ {
    let target = nonspace(st.input)^;
    let missing = target.token != null and not is_control(target.token);
    let noted = if (missing) record_diagnostic({*:st, input:target.stream}, "tex-missing-cs",
        "Missing control sequence inserted")^ else st;
    let input = if (missing) stream.push(target.stream, [target.token]) else target.stream;
    let variable = if (is_control(target.token)) target.token else control_token(chr(2) ++ "inaccessible");
    {state:{*:noted,input:input}, token:variable}
}

fn latex_for(st: State, tokens_loop) State^ {
    let target = latex_required_control(st)^;
    let colon = stream.next(target.state.input)^;
    let equals = stream.next(colon.stream)^;
    let current = {*:target.state, input:equals.stream};
    if (not other(colon.token, ":") or not other(equals.token, "="))
        record_diagnostic(current, "latex-for-syntax", "\\@for expects \\var:=list\\do{body}")
    else {
        let scanned = stream.delimited(current.input, [control_token("do")], true)^;
        let body = stream.argument(scanned.stream, true)^;
        let list_state = if (scanned.error != null) recover_macro_argument({*:current,input:scanned.stream},
            scanned.error, scanned.cause, control_token("relax"), false)^ else current;
        let ready = if (body.error != null) recover_macro_argument({*:list_state,input:body.stream},
            body.error, body.cause, control_token("relax"), false)^ else {*:list_state,input:body.stream};
        if (ready.issue != null) ready
        else {
            let first = if (len(scanned.tokens) > 0 and is_control(scanned.tokens[0])) meaning(ready, scanned.tokens[0]) else null;
            // Native expands one leading parameterless macro for comma loops only.
            let values = if (not tokens_loop and first != null and first.kind == "macro" and
                    first.count == 0 and first.document_specs == null)
                [*first.body, *slice(scanned.tokens, 1, len(scanned.tokens))] else scanned.tokens;
            let items = if (tokens_loop) latex_tfor_items(values)^ else latex_for_items(values)^;
            let steps = latex_for_steps(stored_tokens([target.token])[0], items, body.tokens)^;
            {*:ready, input:stream.push(ready.input, steps)}
        }
    }
}

// The kernel stores these definition prefixes in a primitive or macro alias.
fn latex_definition_prefixes(st: State) any^ {
    let found = meaning(st, control_token("l@ngrel@x"));
    if (found.kind == "primitive") [found.name]
    else if (found.kind == "macro") [for (token in (found.body)^ where is_control(token)^) primitive(st, token)^]
    else []
}

fn latex_yargdef(st: State, budget) State^ {
    let target = latex_required_control(st)^;
    let mode = scan_number(target.state, budget)^;
    if (mode.state.issue != null) mode.state
    else {
        let count = stream.argument(mode.state.input, true)^;
        let body = stream.argument(count.stream, true)^;
        let current = {*:mode.state,input:body.stream};
        if (count.error != null) fail(current,count.error)
        else if (body.error != null) fail(current,body.error)
        else {
            let value = latex_counter_value(current, count.tokens, budget)^;
            let valid = value.value >= 0 and value.value <= 9;
            let nargs = if (valid) value.value else 0;
            let noted = if (valid) value.state else record_diagnostic(value.state, "latex-bad-arg-count",
                "Illegal number of arguments " ++ string(value.value))^;
            let initial = body_params(body.tokens, nargs)^;
            let parsed = if (initial.issue != null) body_params(body.tokens, nargs, 0, [], "latex")^ else initial;
            let recovered = if (initial.issue != null) parameter_diagnostics(noted, parsed.errors, "latex")^ else noted;
            if (recovered.issue != null) recovered
            else if (parsed.issue != null) fail(recovered, parsed.issue)
            else {
                let flags = latex_definition_prefixes(recovered)^;
                let bracketed = mode.value == 2 and nargs > 0;
                let macro = {kind:"macro", count:nargs, prefix:if (bracketed) text_tokens("[")^ else [],
                    delimiters:if (nargs == 0) [] else [for (i in 1 to nargs)
                        if (i == 1 and bracketed) text_tokens("]")^ else []],
                    brace_delim:false, long_ok:contains(flags,"long"), protected_flag:contains(flags,"protected"),
                    body:parsed.tokens};
                assign_meaning(recovered, key(target.token), macro, false)
            }
        }
    }
}

fn latex_filename_parse(st: State, budget) State^ {
    let arg = stream.argument(st.input, true)^;
    let current = {*:st,input:arg.stream};
    if (arg.error != null) fail(current, arg.error)
    else {
        let named = expand_argument_name(current, arg.tokens, budget)^;
        if (named.state.issue != null) named.state
        else {
            let segments = split(named.name, "/")^;
            let filename = segments[len(segments) - 1];
            let area = if (len(segments) > 1) join(take(segments, len(segments) - 1), "/") ++ "/" else "";
            let parts = split(filename, ".")^;
            let has_extension = len(parts) > 1;
            let base = if (has_extension) join(take(parts, len(parts) - 1), ".") else filename;
            let area_state = define_text_macro(named.state, "filename@area", area, false)^;
            let base_state = define_text_macro(area_state, "filename@base", base, false)^;
            if (has_extension) define_text_macro(base_state, "filename@ext", parts[len(parts) - 1], false)
            else assign_meaning(base_state, "control:filename@ext", meaning(base_state, control_token("relax")), false)
        }
    }
}

fn latex_needs_format(st: State) State^ {
    let arg = stream.argument(st.input, true)^;
    let version_arg = latex_optional(arg.stream)^;
    let current = {*:st, input: version_arg.input};
    if (arg.error != null) fail(current, arg.error)
    else if (version_arg.issue != null) fail(current, version_arg.issue) else current
}

fn latex_documentclass(st: State, original, budget, cmd) State^ {
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
        let opts = if (cmd == "LoadClassWithOptions" and current.current_options != null) current.current_options
            else if (opt.present) token_text(current, opt.tokens)^ else "";
        let initial = if (cmd == "documentclass")
            define_text_macro({*:current, class_options: opts}, "@classoptionslist", opts)^ else current;
        let adapter = contains(initial.adapters, named.name);
        let local = if (adapter) {state: initial, available: true, found: false}
            else probe_resource(initial, named.name ++ ".cls", "local")^;
        let closed = end_main_trace(local.state, original)^;
        if (closed.state.issue != null) closed.state
        else if (not local.available) unsupported(closed.state, cmd)
        else if (local.found) {*:closed.state, input: stream.push(closed.state.input,
            [{kind: "package-load", name: named.name, options: opts, scope: "local", is_class: true}])}
        else {
            let recorded = record_package(closed.state, named.name, opts, true, if (adapter) "adapter" else "unknown")^;
            let with_chapter = if (contains(recorded.chaptered, named.name))
                assign_meaning(recorded, "control:chapter", {kind: "constructor", name: "chapter"}, true)^
                else recorded;
            let raw = [original, *opt.raw, *name_arg.raw, *version_arg.raw];
            let passed = if (closed.echo)
                [{*:original, echo_span: closed.span, echo_count: len(raw)}, *slice(raw, 1, len(raw))^]
                else declaration_tokens("documentclass", opts, [named.name], with_chapter.input.last_main_offset)^;
            {*:with_chapter, output: with_chapter.output ++ passed}
        }
    }
}

fn latex_newif(st: State) State^ {
    let target = nonspace(st.input)^;
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

fn latex_verb(st: State, token) State^ {
    let source = stream.raw_reader(st.input, token)^;
    if (source == null) unsupported(st, "verb non-direct source")
    else {
        let scanned = reader.verb_line(source.reader) ^reader.CaptureError { {issue: ^.message} }^;
        if (scanned.issue != null) unsupported(st, scanned.issue)
        else {
            let input = stream.replace_reader(st.input, source.index, scanned.reader)^;
            let span = {file: token.file, start: token.start, end: scanned.end,
                first_seq: token.seq, last_seq: source.reader.seq};
            let captured = {*:token, echo_span: span, echo_count: 1, raw_verb: true};
            {*:st, input: input, raw_inline_seen: true, output: st.output ++ [captured]}
        }
    }
}

// Standalone math text and chemistry are raw arguments; their dollar signs do
// not alter the surrounding expansion groups before math digestion.
fn latex_math_raw_group(st: State, token) State^ {
    let arg = stream.raw_argument(st.input)^;
    if (arg.error != null or len(arg.raw) == 0 or not charcat(arg.raw[0],1))
        {*:st, output:st.output ++ [token]}
    else {*:st, input:stream.push(arg.stream,[for (part in arg.raw) {*:part,noexpand:true}]),
        output:st.output ++ [token]}
}

fn raw_inline_command(name) => contains(["lstinline", "Verb", "mint", "mintinline"],name)

// raw command bodies bypass token expansion while a source reader is available.
fn latex_raw_group(st: State, token, inline_kind = null) State^ {
    let source = stream.raw_reader(st.input, token)^;
    if (source == null) {*:st, output: st.output ++ [token]}
    else {
        let scanned = if (inline_kind == null) reader.raw_group_command(source.reader)^
            else reader.raw_inline_command(source.reader,contains(["mint","mintinline"],inline_kind)) ^reader.CaptureError { {issue: ^.message} }^;
        if (scanned.issue != null) unsupported(st,scanned.issue)
        else {
            let input = stream.replace_reader(st.input, source.index, scanned.reader)^;
            let span = {file: token.file, start: token.start, end: scanned.end,
                first_seq: token.seq, last_seq: source.reader.seq};
            {*:st, input: input, raw_inline_seen: true,
                output: st.output ++ [{*:token, echo_span: span, echo_count: 1,
                    *:if(inline_kind == null) {raw_group_kind:token.value} else {raw_inline_kind:inline_kind}}]}
        }
    }
}

fn latex_constructor(st: State) State^ {
    let target = nonspace(st.input)^;
    let current = {*:st, input: target.stream};
    if (not is_control(target.token)) fail(current, "tex-missing-cs")
    else assign_meaning(current, key(target.token), {kind: "constructor", name: target.token.value}, true)
}

// The host's adapter registry identifies constructors whose arguments remain raw.
pub fn enable_raw_constructors(st: State, names, index = 0) State^ {
    if (not (names is array) or index >= len(names)) st
    else {
        let name = names[index];
        let next = if (name is string) assign_meaning(st, "control:" ++ name,
            {kind:"constructor", name:name, raw:true}, true)^ else st;
        enable_raw_constructors(next, names, index + 1)
    }
}

fn finish_definition_echo(st: State, after, tracing, emit) State^ {
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
                    else {*:finished, output: finished.output ++ [{kind: "source-span", value: raw,
                        file: traced.file, start: traced.start, end: traced.end, seq: traced.first_seq,
                        last_seq: traced.last_seq, catcodes: st.input.reader.catcodes}]}
                }
            }
        }
    }
}

fn echo_definition(st: State, token, cmd, budget) State^ {
    let tracing = st.echo_definitions and token.direct == true and token.file == st.main_file;
    let current = if (tracing) {*:st, input: stream.begin_trace(st.input, token, st.main_file)^} else st;
    let after = if (contains(["def", "gdef", "edef", "xdef"], cmd))
            after_assignment(current, define(current, effective_global(st, cmd == "gdef" or cmd == "xdef")^,
                false, false, cmd == "edef" or cmd == "xdef", budget)^)^
        else if (contains(["newcommand", "renewcommand", "providecommand", "DeclareRobustCommand"], cmd))
            latex_newcommand(current, cmd)^
        else if (contains(["NewDocumentCommand", "RenewDocumentCommand", "ProvideDocumentCommand", "DeclareDocumentCommand",
                "NewDocumentEnvironment", "RenewDocumentEnvironment", "ProvideDocumentEnvironment", "DeclareDocumentEnvironment"], cmd))
            latex_document_command(current, cmd, budget)^
        else latex_newenvironment(current, cmd, budget)^;
    finish_definition_echo(st, after, tracing, true)
}

fn echo_prefixed(st: State, token, cmd, budget) State^ {
    let tracing = st.echo_definitions and token.direct == true and token.file == st.main_file;
    let input = if (tracing) stream.begin_trace(st.input, token, st.main_file)^ else st.input;
    let current = {*:st, input: input, prefixed_definition: false};
    let after = after_assignment(current, prefixed(current, cmd == "global", cmd == "long", cmd == "protected", budget)^)^;
    finish_definition_echo(st, after, tracing, after.prefixed_definition == true)
}

fn direct_package_token(st, token) any^ => token.direct == true and
    any([for (source in st.sources) source.id == token.file and source.package == true])

// generated argument delimiters need the same origin as other emitted tokens.
fn emission_token(st, input, raw) any^ => if (raw != null and contains(["char", "control", "active"], raw.kind) and
        (raw.direct != true or raw.file != st.main_file))
    {*:raw, read_offset: input.last_main_offset, read_file: st.main_file} else raw

// passthrough environments preserve the original name tokens inside arguments.
fn constructor_environment(st: State, token, budget) State^ {
    let argument = stream.raw_argument(st.input)^;
    let current = {*:st, input: argument.stream};
    if (argument.error != null) fail(current, argument.error)
    else {
        let named = expand_argument_name(current, argument.tokens, budget)^;
        if (named.state.issue != null) named.state
        else if (meaning(named.state, control_token(named.name)).kind == "macro") {
            let current = named.state;
            if (primitive(current,token) == "begin") {
                // custom environment bodies expand in argument mode within their own scope.
                let opened = begin_environment(current,named.name,false)^;
                let current_env = latex_macro(0,false,false,false,[],text_tokens(named.name))^;
                let named_env = assign_meaning(opened,"control:@currenvir",current_env,false)^;
                {*:named_env,input:stream.push(named_env.input,[control_token(named.name)])}
            }
            else {
                let depth = len(current.groups);
                if (depth == 0 or current.groups[depth - 1].environment != named.name) current
                else {*:current,input:stream.push(current.input,
                    [control_token("end" ++ named.name),{kind:"env-close",value:named.name}])}
            }
        }
        else {
            // passthrough tokens are emitted after the complete name is read.
            let emitted = [for (raw in [token, *argument.raw])
                {*:emission_token(named.state, named.state.input, raw)^, emit_origin: named.state.input.last_main_offset}];
            {*:named.state, output: named.state.output ++ emitted}
        }
    }
}

// argument mode expands the expl3 setter macro without running its assignment.
fn constructor_expl3_tl_set(st: State, token, name, budget) State^ {
    let internal = if (name == "tl_gset:Nn") "__kernel_tl_gset:Nx" else "__kernel_tl_set:Nx";
    let macro = {kind: "macro", count: 2, prefix: [], delimiters: [[], []], brace_delim: false,
        long_ok: true, protected_flag: true, body: [control_token(internal), {kind: "parameter", value: 0},
            brace_token(true), control_token("unexpanded"), brace_token(true), {kind: "parameter", value: 1},
            brace_token(false), brace_token(false)]};
    expand_macro(st, macro, budget, token)
}

// constructor arguments expand tokens but do not execute assignments.
fn constructor_argument(st: State, budget, bracket, depth) State^ {
    let read = expanded_next(st, budget, false, true)^;
    let token = emission_token(st, read.state.input, read.token)^;
    let current = read.state;
    if (current.issue != null) current
    // native returns the accumulated argument at EOF, even if it is incomplete.
    else if (token == null) current
    else if (token.kind == "env-close") {
        let ended = end_group(current,null,true,true)^;
        if (ended.issue != null) ended else constructor_argument(ended,budget,bracket,depth)
    }
    else if (token.noexpand != true and current.latex and contains(["begin", "end"], primitive(current, token))) {
        let advanced = constructor_environment(current, token, budget)^;
        if (advanced.issue != null) advanced else constructor_argument(advanced, budget, bracket, depth)
    }
    else if (token.noexpand != true and primitive(current,token) == "verb") {
        // raw verbatim text cannot alter the surrounding argument depth.
        let after = latex_verb(current,token)^;
        if (after.issue != null) after else constructor_argument(after,budget,bracket,depth)
    }
    else if (token.noexpand != true and raw_inline_command(primitive(current,token))) {
        let after = latex_raw_group(current,token,primitive(current,token))^;
        if (after.issue != null) after else constructor_argument(after,budget,bracket,depth)
    }
    else if (token.noexpand != true and current.script_expl3 and
            contains(["tl_set:Nn", "tl_gset:Nn"], primitive(current, token))) {
        let expanded = constructor_expl3_tl_set(current, token, primitive(current, token), budget)^;
        if (expanded.issue != null) expanded else constructor_argument(expanded, budget, bracket, depth)
    }
    else if (token.noexpand != true and current.script_expl3 and is_control(token) and
            meaning(current,token).expl3_argument_handler == "expand") {
        // argument mode expands the native wrapper without executing its assignment.
        let expanded = expand_macro(current,meaning(current,token),budget,token)^;
        if (expanded.issue != null) expanded else constructor_argument(expanded,budget,bracket,depth)
    }
    else if (token.noexpand != true and current.script_expl3 and is_control(token) and
            meaning(current, token).expl3_argument_unqualified == true)
        unsupported(current, "constructor argument expl3 assignment")
    // script expl3 assignments stand in for macros that native argument mode expands.
    else if (token.noexpand != true and current.script_expl3 and is_control(token) and
            expl3.exists(primitive(current, token)) and not expl3.expandable(primitive(current, token)))
        unsupported(current, "constructor argument expl3 assignment")
    else {
        let emitted = if (token.noexpand == true) {*:token, noexpand: false} else token;
        // argument-mode tokens also need their emission-time source position.
        let next = {*:current, output: current.output ++ [
            {*:emitted, emit_origin: current.input.last_main_offset,
                *:if (is_control(token) and reader.definition_command(token.value))
                    {definition_catcodes:current.input.reader.catcodes} else {}}]};
        let level = depth + if (charcat(token, 1)) 1 else if (charcat(token, 2)) -1 else 0;
        if ((not bracket and level == 0) or (bracket and depth == 0 and other(token, "]"))) next
        // a closing brace also terminates a malformed bracket argument.
        else if (level < 0) next
        else constructor_argument(next, budget, bracket, level)
    }
}
// Raw adapter arguments are copied without expanding macros or executing assignments.
fn constructor_raw_span(st: State, mode, depth) State^ {
    let read = stream.next(st.input)^;
    let token = emission_token(st, read.stream, read.token)^;
    let current = {*:st,input:read.stream};
    if (token == null) current
    else {
        let emitted = {*:current,output:current.output ++ [token]};
        let level = depth + if (charcat(token,1)) 1 else if (charcat(token,2)) -1 else 0;
        if ((mode == "math" and charcat(token,3)) or (mode == "group" and level == 0) or
            (mode == "bracket" and depth == 0 and other(token,"]")) or (mode != "math" and level < 0)) emitted
        else constructor_raw_span(emitted,mode,level)
    }
}

fn constructor_arguments(st: State, budget, first = true, raw = false) State^ {
    let read = stream.next(st.input)^;
    let token = emission_token(st, read.stream, read.token)^;
    let current = {*:st, input: read.stream};
    if (token == null) current
    else if (first and other(token, "*"))
        constructor_arguments({*:current, output: current.output ++ [token]}, budget, false, raw)
    else if (charcat(token, 1) or other(token, "[")) {
        let bracket = other(token, "[");
        let entered = {*:current, output: current.output ++ [token]};
        let after = if (raw) constructor_raw_span(entered, if (bracket) "bracket" else "group", if (bracket) 0 else 1)^
            else constructor_argument(entered, budget, bracket, if (bracket) 0 else 1)^;
        if (after.issue != null) after else constructor_arguments(after, budget, false, raw)
    }
    else if (raw and charcat(token,3)) {
        let entered = {*:current,output:current.output ++ [token]};
        let after = constructor_raw_span(entered,"math",0)^;
        if (after.issue != null) after else constructor_arguments(after,budget,false,raw)
    }
    else put_back(current, token)
}

// echo the document call, then replay non-direct tokens to execute the macro once.
fn echo_redefined_command(st: State, token) State^ {
    let traced = begin_main_trace(st, token, true)^;
    let first = stream.argument(traced.input, true)^;
    let second = if (token.value == "stepcounter") {stream: first.stream, tokens: [], error: null}
        else stream.argument(first.stream, true)^;
    let current = {*:traced, input: second.stream};
    if (first.error != null) fail(current, first.error)
    else if (second.error != null) fail(current, second.error)
    else {
        let closed = end_main_trace(current, token)^;
        let echoed = if (closed.echo) emit_traced_span(closed, "redefined command echo trace")^ else closed.state;
        let replay = stored_tokens([control_token(token.value), brace_token(true), *first.tokens, brace_token(false),
            *(if (token.value == "stepcounter") [] else [brace_token(true), *second.tokens, brace_token(false)])]);
        if (echoed.issue != null) echoed else {*:echoed, input: stream.push(echoed.input, replay)}
    }
}


fn finish_conditionals(st: State, index) State^ {
    if (index < 0) {*:st, conditions:[], finished:true}
    else {
        let origin = st.conditions[index].origin;
        let message = "(\\end occurred when a conditional on line " ++ string(origin.line) ++ " of " ++ origin.name ++ " was incomplete)";
        let diagnosed = record_diagnostic(st,"tex-incomplete-if",message)^;
        finish_conditionals(diagnosed,index - 1)
    }
}

fn step(st: State, budget) State^ {
    let read = stream.next(st.input)^;
    let raw = read.token;
    let t = emission_token(st, read.stream, raw)^;
    let current = {*:st, input: read.stream};
    let cmd = primitive(st, t);
    let resolved = if (is_control(t)) meaning(st, t) else null;
    let assignment_global = effective_global(st, false)^;
    if (t == null) if (len(st.conditions) > 0) finish_conditionals(current,len(st.conditions) - 1) else {*:current, finished: true}
    else if ((charcat(t, 10) or (is_control(t) and t.value == "par")) and direct_package_token(st, t)) current
    else if (t.kind == "expl3-sort-end") {*:current, expl_sort_done: true}
    else if (t.kind == "expl3-keys-next") expl3_keys_next(current)
    else if (t.kind == "counter-value-end") current
    else if (t.kind == "env-close") end_group(current, null, true, true)
    else if (t.kind == "package-load") start_package(current, t, budget)
    else if (t.kind == "package-end") finish_package_hook(current)
    else if (t.kind == "package-pop") finish_package(current)
    else if (t.kind == "document-close") {
        let emitted = environment_output(current, t.original, t.raw, "document", "end", t.echo, t.span)^;
        {*:current, output: current.output ++ emitted,
            trailing_source: if (t.root_tail) stream.root_tail(current.input) else null, finished: true}
    }
    // Concatenation preserves the output array's verified element contract.
    else if (t.noexpand == true) {*:current, output: st.output ++ [{*:t, noexpand: false}]}
    else if (redefined_document_command(current, t)^) echo_redefined_command(current, t)
    else if (st.math_mode and t.kind == "control" and reader.math_raw_group_command(t.value) and resolved.kind != "macro")
        latex_math_raw_group(current, t)
    else if (expandable(current, t)) expand_one(current, t, budget)
    // these native TeX commands remain tokens for the document adapters.
    else if (contains([" ", "underline", "overline", "indent", "noindent", "hfil", "hfill", "hss", "hfilneg",
        "vfil", "vfill", "vss", "vfilneg", "noalign", "omit", "span", "cr", "crcr", "-", "/", "noboundary",
        "mathord", "mathop", "mathbin", "mathrel", "mathopen", "mathclose", "mathpunct", "mathinner",
        "displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle", "limits", "nolimits", "displaylimits",
        "nonscript", "eqno", "leqno", "mathchoice", "left", "right", "middle", "over", "atop", "above",
        "overwithdelims", "atopwithdelims", "abovewithdelims"], cmd))
        {*:current, output: st.output ++ [t]}
    else if (contains(["hskip", "vskip", "mskip", "kern", "mkern"], cmd))
        emit_spacing(current, t, cmd, budget)
    else if (contains(["hbox", "vbox", "vtop"], cmd)) begin_inline_box(current, budget, cmd)
    else if (contains(["box","copy","lastbox","vsplit","unhbox","unhcopy","unvbox","unvcopy"],cmd))
        emit_box(current, budget, cmd)
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
    // TeX expands while skipping space tokens, then returns the first other token.
    else if (cmd == "ignorespaces") {
        let read = expanded_nonspace(current, budget)^;
        put_back(read.state, after_expanded_scan(read.token))
    }
    // native unskip removes only a final emitted space token.
    else if (cmd == "unskip") {
        let count = len(current.output);
        if (count > 0 and charcat(current.output[count - 1], 10))
            {*:current, output: slice(current.output, 0, count - 1)^,
                // A removed outer space moves every open capture boundary with it.
                groups:[for(scope in current.groups) {*:scope,
                    box_start:if(scope.box_start != null and scope.box_start >= count) count - 1 else scope.box_start,
                    discard_output:if(scope.discard_output != null and scope.discard_output >= count) count - 1 else scope.discard_output}]}
        else current
    }
    else if (contains(["def", "gdef", "edef", "xdef"], cmd)) {
        // token-list assignment aliases share the existing source-mapping guard.
        let guarded = if (resolved.source_mapping_guard == "tl_put") {*:current, expl3_tl_put_seen: true} else current;
        echo_definition(guarded, t, cmd, budget)
    }
    else if (contains(["patterns", "hyphenation", "parshape", "interlinepenalties", "clubpenalties", "widowpenalties", "displaywidowpenalties"], cmd))
        after_assignment(current, assign_paragraph_data(current, budget, cmd))
    else if (cmd == "let") after_assignment(current, assign_let(guarded_expl_let(current, t)^, assignment_global))
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
        latex_provides_expl({*:current, expl3_format_requested: true}, cmd, budget)
    else if (st.script_expl3 and cmd == "cs_new_eq:NN")
        after_assignment(current, expl3_cs_new_equal(current))
    else if (st.script_expl3 and expl3.cs_definition(cmd))
        after_assignment(current, expl3_cs_define(current, cmd, budget))
    else if (st.script_expl3 and expl3.cs_variant(cmd))
        after_assignment(current, expl3_cs_generate_variant(current))
    else if (st.script_expl3 and expl3.keys_action(cmd))
        expl3_keys_begin(current, cmd)
    else if (st.script_expl3 and expl3.clist_map(cmd))
        expl3_clist_map(current, cmd)
    else if (st.script_expl3 and expl3.regex_action(cmd))
        expl3_regex_match(current, budget)
    else if (st.script_expl3 and expl3.ior_action(cmd))
        after_assignment(current, expl3_ior_action(current, cmd, budget))
    else if (st.script_expl3 and expl3.token_assignment(cmd))
        after_assignment(current, expl3_tl_assign(current, cmd, budget))
    else if (st.script_expl3 and expl3.token_replacement(cmd))
        after_assignment(current, expl3_tl_replace(current, cmd))
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
        "ProvideDocumentCommand", "DeclareDocumentCommand", "NewDocumentEnvironment", "RenewDocumentEnvironment",
        "ProvideDocumentEnvironment", "DeclareDocumentEnvironment"], cmd)) echo_definition(current, t, cmd, budget)
    else if (st.latex and (cmd == "AtBeginDocument" or cmd == "AtEndDocument"))
        latex_document_hook(current, cmd == "AtBeginDocument")
    else if (st.latex and contains(["usepackage", "RequirePackage", "RequirePackageWithOptions"], cmd))
        latex_usepackage(current, t, budget)
    else if (st.latex and contains(["newlength", "setlength", "addtolength"], cmd))
        latex_length_command(current,t,cmd,budget)
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
    else if (st.latex and contains(["@for", "@tfor"], cmd)) latex_for(current, cmd == "@tfor")
    else if (st.latex and cmd == "@yargdef") latex_yargdef(current, budget)
    else if (st.latex and cmd == "filename@parse") latex_filename_parse(current, budget)
    else if (st.latex and cmd == "NeedsTeXFormat") latex_needs_format(current)
    else if (st.latex and contains(["documentclass", "LoadClass", "LoadClassWithOptions"], cmd))
        latex_documentclass(current, t, budget, cmd)
    else if (st.latex and cmd == "verb") latex_verb(current, t)
    else if (st.latex and raw_inline_command(cmd))
        latex_raw_group(current,t,cmd)
    else if (st.latex and t.kind == "control" and resolved.kind == "undefined" and
            contains(["directlua", "luaexec"], t.value)) latex_raw_group(current, t)
    else if (st.latex and cmd == "begin") latex_begin(current, t, budget)
    else if (st.latex and cmd == "end") latex_end(current, t, budget)
    else if (cmd == "newif" and st.latex) latex_newif(current)
    else if (cmd == "lambdaconstructor" and st.latex) latex_constructor(current)
    else if (cmd == "font") after_assignment(current, define_font(current, assignment_global, budget))
    else if (cmd == "letterspacefont") after_assignment(current, assign_letterspace_font(current, assignment_global, budget))
    else if (cmd == "read" or cmd == "readline") after_assignment(current, read_stream_assignment(current, assignment_global, budget, cmd == "readline"))
    else if (primitives.engine_version(cmd) != null) after_assignment(current, readonly_assignment(current, budget)^)
    else if (cmd == "interactionmode" or page_dimension(cmd)) after_assignment(current, assign_page_state(current, budget, cmd))
    else if (contains(["batchmode", "nonstopmode", "scrollmode", "errorstopmode"], cmd))
        {*:current, page_state_assignment_seen: true}
    else if (cmd == "fontdimen") after_assignment(current, assign_font_dimen(current, budget))
    else if (cmd == "hyphenchar" or cmd == "skewchar")
        after_assignment(current, assign_font_character(current, budget, cmd))
    else if (contains(["textfont", "scriptfont", "scriptscriptfont"], cmd))
        after_assignment(current, assign_font_family(current, budget, cmd))
    else if (cmd == "nullfont") after_assignment(current, select_font(current, assignment_global, 0))
    else if (cmd == "setbox") after_assignment(current, assign_box(current, assignment_global, budget))
    else if (box_dimension_name(cmd)) after_assignment(current, assign_box_dimension(current, cmd, budget))
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
        if (len(st.conditions) > 0) finish_conditionals(current,len(st.conditions) - 1) else {*:current, finished: true}
    else if (cmd == "afterassignment") save_after_assignment(current)
    else if (cmd == "aftergroup") save_aftergroup(current)
    else if (cmd == "begingroup") begin_group(current, null, true)
    else if (cmd == "endgroup") end_group(current, null, true, false, false, true)
    else if (cmd == "global" or cmd == "long" or cmd == "protected") echo_prefixed(current, t, cmd, budget)
    else if (resolved.kind == "constructor") {
        // A constructor alias emits the target name, as TeX's copied meaning does.
        let constructor = if (t.value == resolved.name) t else {*:t,value:resolved.name};
        let emitted = {*:current, output: st.output ++ [constructor]};
        if (resolved.raw == true or (current.constructor_argument_mode and current.latex and current.math_mode != true))
            constructor_arguments(emitted, budget, true, resolved.raw == true) else emitted
    }
    else if (resolved.kind == "relax" or cmd == "relax") current
    else if (resolved.kind == "char") {*:current, input: stream.push(current.input,
        [{kind: "char", value: resolved.value, cat: resolved.cat, start: t.start, end: t.end}])}
    else if (resolved.kind == "chardef") {
        // synthesized characters need the current main-source position.
        {*:current, output: st.output ++ [
            {kind: "char", value: chr(resolved.value), cat: 12, start: t.start, end: t.end,
                chardef_emission: true, chardef_origin: t, read_offset: current.input.last_main_offset, read_file: current.main_file}]}
    }
    else if (resolved.kind == "mathchardef")
        {*:current, output: st.output ++ [t], mathchardef_emission_seen:
            current.mathchardef_emission_seen == true or mathchardef_name_needs_mapping(t)^}
    else if (cmd == "par") {*:current, output: st.output ++ [t]}
    else if (cmd != null) unsupported(current, cmd)
    else if (st.script_expl3 and is_control(t) and index_of(t.value, ":") != null)
        unsupported(current, t.value)
    else if (current.constructor_argument_mode and current.latex and current.math_mode != true and
            is_control(t) and resolved.kind == "undefined") {
        let emitted = {*:current, output: st.output ++ [t]};
        constructor_arguments(emitted, budget)
    }
    else if (charcat(t, 3)) math_shift(current, t)
    else if (charcat(t, 1)) begin_group(current, t, false)
    else if (charcat(t, 2)) end_group(current, t, false, false, false, true)
    // the output serializer needs the reader position at emission, including lookahead.
    else {*:current, output: st.output ++ [{*:t, emit_origin: current.input.last_main_offset}]}
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

fn run(st: State, budget) State^ {
    // keep document-length stepping off the native call stack.
    var current = st
    while (current.issue == null and current.finished != true) {
        current = step(current, budget)^
    }
    return terminal_flush(current)^
}

pub fn expand(input: map, budget: int = 20000000, options: any ! error = null) State^ {
    // Normalize optional external configuration before constructing engine state.
    let normalized = options_map(options);
    run(result(input, {}, [], [], 0, null, normalized), budget)
}

// The opt-in script profile replaces the kernel's full-package bootstrap.
pub fn enable_script_expl3(st: State) State^ {
    let enabled = {*:st, script_expl3: true};
    let names = ["ExplSyntaxOn", "ExplSyntaxOff", "ProvidesExplPackage", "ProvidesExplClass", "ProvidesExplFile"];
    let primitives = enable_expl_primitives(enabled, names)^;
    let constants = enable_expl_values(primitives, [
        {name: "c_zero_int", kind: "chardef", value: 0},
        {name: "c_one_int", kind: "mathchardef", value: 1}])^;
    let empty_sequence = if (constants.definitions["control:c_empty_seq"] != null) constants else
        assign_meaning(constants, "control:c_empty_seq", expl3_seq_value([])^, true)^;
    let local_put = if (empty_sequence.definitions["control:seq_put_right:Nn"] != null) empty_sequence else
        assign_meaning(empty_sequence, "control:seq_put_right:Nn", expl3_seq_put_right_macro(false)^, true)^;
    let global_put = if (local_put.definitions["control:seq_gput_right:Nn"] != null) local_put else
        assign_meaning(local_put, "control:seq_gput_right:Nn", expl3_seq_put_right_macro(true)^, true)^;
    let aliases = enable_expl_values(global_put, [
        {name: "__kernel_tl_set:Nx", kind: "primitive", value: "edef", source_mapping_guard: "tl_put"},
        {name: "__kernel_tl_gset:Nx", kind: "primitive", value: "xdef", source_mapping_guard: "tl_put"},
        {name: "__kernel_exp_not:w", kind: "primitive", value: "unexpanded"},
        {name: "exp_after:wN", kind: "primitive", value: "expandafter"}])^;
    let local_tl = if (aliases.definitions["control:tl_put_right:Nn"] != null) aliases else
        assign_meaning(aliases, "control:tl_put_right:Nn", expl3_tl_put_right_macro(false)^, true)^;
    let global_tl = if (local_tl.definitions["control:tl_gput_right:Nn"] != null) local_tl else
        assign_meaning(local_tl, "control:tl_gput_right:Nn", expl3_tl_put_right_macro(true)^, true)^;
    // expl3 predefines four empty scratch sequences alongside its token lists.
    let sequences = enable_expl_scratch(global_tl, ["l_tmpa_seq", "l_tmpb_seq", "g_tmpa_seq", "g_tmpb_seq"], expl3_seq_value([])^)^;
    let scratch = enable_expl_scratch(sequences, ["l_tmpa_tl", "l_tmpb_tl", "g_tmpa_tl", "g_tmpb_tl",
        "l__seq_internal_a_tl", "l__seq_internal_b_tl", "l__exp_internal_tl", "l__clist_internal_clist",
        *KEYS_SCRATCH], expl3.token_list([]))^;
    // Expl3's choice and mapping counters share TeX's ordinary count allocator.
    let counts = enable_expl_counts(scratch, ["l_keys_choice_int", "g__kernel_prg_map_int"])^;
    let support = enable_expl_values(counts, [
        {name: "tex_let:D", kind: "primitive", value: "let", source_mapping_guard: "tl_eq"},
        {name: "tex_global:D", kind: "primitive", value: "global"},
        {name: "tex_advance:D", kind: "primitive", value: "advance"},
        {name: "group_begin:", kind: "primitive", value: "begingroup"},
        {name: "group_end:", kind: "primitive", value: "endgroup"},
        {name: "if_cs_exist:w", kind: "primitive", value: "ifcsname"},
        {name: "if_true:", kind: "primitive", value: "iftrue"},
        {name: "if_bool:N", kind: "primitive", value: "ifodd"},
        {name: "if_predicate:w", kind: "primitive", value: "ifodd"},
        {name: "s__str_mark", kind: "primitive", value: "relax"},
        {name: "s__str_stop", kind: "primitive", value: "relax"},
        {name: "s__tl_stop", kind: "primitive", value: "relax"},
        {name: "if_int_compare:w", kind: "primitive", value: "ifnum"},
        {name: "if_int_odd:w", kind: "primitive", value: "ifodd"},
        {name: "int_value:w", kind: "primitive", value: "number"},
        {name: "__int_sep:", kind: "primitive", value: "right"},
        {name: "__int_eval:w", kind: "primitive", value: "numexpr"},
        {name: "int_eval:w", kind: "primitive", value: "numexpr"},
        {name: "__int_to_roman:w", kind: "primitive", value: "romannumeral"},
        {name: "__int_eval_end:", kind: "primitive", value: "relax"},
        {name: "reverse_if:N", kind: "primitive", value: "unless"},
        {name: "l_tmpa_bool", kind: "chardef", value: 0},
        {name: "l_tmpb_bool", kind: "chardef", value: 0},
        {name: "g_tmpa_bool", kind: "chardef", value: 0},
        {name: "g_tmpb_bool", kind: "chardef", value: 0},
        {name: "tex_expanded:D", kind: "primitive", value: "expanded"},
        {name: "tex_the:D", kind: "primitive", value: "the"},
        {name: "int_use:N", kind: "primitive", value: "the"},
        {name: "exp_not:N", kind: "primitive", value: "noexpand"},
        {name: "cs_set_nopar:Npx", kind: "primitive", value: "edef"},
        {name: "if:w", kind: "primitive", value: "if"},
        {name: "__kernel_tl_to_str:w", kind: "primitive", value: "detokenize"},
        {name: "if_false:", kind: "primitive", value: "iffalse"},
        {name: "cs_end:", kind: "primitive", value: "endcsname"},
        {name: "cs:w", kind: "primitive", value: "csname"},
        {name: "if_cs_exist:N", kind: "primitive", value: "ifdefined"},
        {name: "else:", kind: "primitive", value: "else"},
        {name: "c_true_bool", kind: "chardef", value: 1},
        {name: "c_false_bool", kind: "chardef", value: 0},
        // TeX reserves stream 16 for terminal input.
        {name: "c__ior_term_ior", kind: "mathchardef", value: 16},
        {name: "if_meaning:w", kind: "primitive", value: "ifx"},
        {name: "fi:", kind: "primitive", value: "fi"},
        {name: "exp:w", kind: "primitive", value: "romannumeral"},
        {name: "exp_end:", kind: "chardef", value: 0},
        {name: "exp_not:n", kind: "primitive", value: "unexpanded"},
        {name: "exp_stop_f:", kind: "char", value: " ", cat: 10},
        {name: "scan_stop:", kind: "primitive", value: "relax"},
        {name: "s__seq", kind: "primitive", value: "relax"},
        {name: "s__seq_stop", kind: "primitive", value: "relax"},
        {name: "s__prop", kind: "primitive", value: "relax"},
        {name: "s__prop_mark", kind: "primitive", value: "relax"},
        {name: "s__prop_stop", kind: "primitive", value: "relax"},
        {name: "s__msg_stop", kind: "primitive", value: "relax"},
        {name: "tex_romannumeral:D", kind: "primitive", value: "romannumeral"},
        {name: "token_to_str:N", kind: "primitive", value: "string"},
        {name: "__str_if_eq:nn", kind: "primitive", value: "pdfstrcmp"},
        {name: "cs_set_nopar:Npe", kind: "primitive", value: "edef"},
        {name: "cs_gset_nopar:Npe", kind: "primitive", value: "xdef"}])^;
    // native Nx variants are protected macros that call the corresponding Nn operation.
    let variants = [for (name in expl3.expanded_argument_names()) {
        source:"\\" ++ name ++ "{\\exp_args:NNx\\" ++ slice(name,0,len(name) - 1)^ ++ "n}", protected_flag:true}];
    // Native pop variants share the same conditional and argument-expansion shapes.
    let pop_shapes=[for(direction in ["left","right"],scope in ["","g"]) {
        name:"seq_" ++ scope ++ "pop_" ++ direction,
        helper:"__seq_pop_" ++ direction ++ ":NNN",
        setter:if(direction=="left") "tl_" ++ scope ++ "set:Nn" else "__kernel_tl_" ++ scope ++ "set:Nx"}];
    let pop_base=[for(shape in pop_shapes) {
        source:"\\" ++ shape.name ++ ":NN{\\__seq_pop:NNNN\\" ++ shape.helper ++ "\\" ++ shape.setter ++ "}",protected_flag:true}];
    let pop_conditionals=[for(shape in pop_shapes, suffix in ["TF","T","F"]) {
        source:"\\" ++ shape.name ++ ":NN" ++ suffix ++ "#1#2{\\__seq_pop_TF:NNNN\\" ++ shape.helper ++ "\\" ++ shape.setter ++ "#1#2\\exp_end:" ++
            (if(suffix=="T") "\\use:n\\use_none:n" else if(suffix=="F") "{}" else "") ++ "}",protected_flag:true}];
    let pop_variants=[for(shape in pop_shapes, suffix in ["","TF","T","F"]) {
        source:"\\" ++ shape.name ++ ":cN" ++ suffix ++ "{\\exp_args:Nc\\" ++ shape.name ++ ":NN" ++ suffix ++ "}",protected_flag:true}];
    // Preserve upstream macro expansion order so TeX lookahead and aliases behave normally.
    let property_in_body="\\__prop_if_flat:NTF#1{\\exp_after:wN\\prop_map_tokens:Nn\\exp_after:wN#1{\\exp_after:wN\\__prop_if_in_flat:nnn\\exp_after:wN{\\tl_to_str:n{#2}}}\\prg_return_false:}{\\exp_after:wN\\__prop_get_linked:w#1{#2}\\use_none:n\\prg_return_true:\\prg_return_false:}\\exp_end:";
    let prop_map_terminator="\\__prop_pair:wn\\fi:\\prop_map_break:\\s__prop{}";
    let prop_map_steps=join([for(i in [2,4,6,8]) "\\if_false:#" ++ string(i) ++ "\\fi:\\use:n{#1}{#" ++ string(i) ++ "}{#" ++ string(i+1) ++ "}"],"");
    let tl_map_stops = repeated("\\s__tl_stop", 8)^;
    let tl_map_steps = join([for (i in [2,3,4,5,6,7,8,9]) "#1{#" ++ string(i) ++ "}"], "")^;
    // Preserve definitions supplied by the caller's kernel during bootstrap.
    let property_support=if(support.definitions["control:c_empty_prop"]!=null) support else
        assign_meaning(support,"control:c_empty_prop",expl3.token_list([control_token("s__prop"),control_token("__prop_chk:w")])^,true)^;
    let msg_names=if(property_support.definitions["control:g_msg_module_name_prop"]!=null) property_support else
        assign_meaning(property_support,"control:g_msg_module_name_prop",msg_module_property(message_catalog.name_properties())^,true)^;
    let msg_support=if(msg_names.definitions["control:g_msg_module_type_prop"]!=null) msg_names else
        assign_meaning(msg_names,"control:g_msg_module_type_prop",msg_module_property(message_catalog.type_properties())^,true)^;
    // Native lookup variants share one splitter and conditional expansion order.
    let prop_get_conditionals=[for(suffix in ["TF","T","F"]) {
        source:"\\prop_get:NnN" ++ suffix ++ "#1#2#3{\\__prop_get:NnnTF#1{#2}{\\tl_set:Nn#3}\\prg_return_true:\\prg_return_false:\\exp_end:" ++
            (if(suffix=="T") "\\use:n\\use_none:n" else if(suffix=="F") "{}" else "") ++ "}",protected_flag:true}];
    // Build the native argument-expansion variants from their shared shapes.
    let prop_get_variant_shapes=[
        {name:"cnN",helper:"Nc"},{name:"NVN",helper:"NNV"},{name:"NvN",helper:"NNv"},
        {name:"NoN",helper:"NNo"},{name:"NeN",helper:"NNe"},{name:"NxN",helper:"NNx"},
        {name:"cVN",helper:"NcV"},{name:"cvN",helper:"Ncv"},{name:"coN",helper:"Nco"},
        {name:"ceN",helper:"Nce"},{name:"cxN",helper:"Ncx"},{name:"cnc",helper:"Ncnc"}];
    let prop_get_variants=[for(shape in prop_get_variant_shapes,suffix in ["","TF","T","F"]) {
        source:"\\prop_get:" ++ shape.name ++ suffix ++ "{\\exp_args:" ++ shape.helper ++ "\\prop_get:NnN" ++ suffix ++ "}",protected_flag:true}];
    let prop_exp_values=[{mode:"V",value:"\\exp:w\\__exp_eval_register:N#3"},
        {mode:"v",value:"\\exp:w\\__exp_eval_register:c{#3}"},{mode:"o",value:"#3"}];
    let prop_exp_helpers=[for(form in ["N","c"],value in prop_exp_values) {
        source:"\\exp_args:N" ++ form ++ value.mode ++ "#1#2#3{\\exp_after:wN#1" ++
            (if(form=="N") "\\exp_after:wN#2" else "\\cs:w#2\\exp_after:wN\\cs_end:") ++
            "\\exp_after:wN{" ++ value.value ++ "}}",protected_flag:false,
        source_mapping_guard:if(value.mode=="o") null else "exp_args_value"}];
    // Native pop and remove variants share the splitter and scope-specific setters.
    let prop_pop_shapes=[for(scope in ["","g"]) {
        pop:"prop_" ++ scope ++ "pop", remove:"prop_" ++ scope ++ "remove",
        eq:"cs_" ++ scope ++ "set_eq:NN", set:"cs_" ++ scope ++ "set_nopar:Npe"}];
    let prop_pop_bases=[for(shape in prop_pop_shapes) {
        source:"\\" ++ shape.pop ++ ":NnN#1#2#3{\\__prop_pop:NnNNnTF#1{#2}\\" ++ shape.eq ++ "\\" ++ shape.set ++ "{\\tl_set:Nn#3}{}{\\tl_set:Nn#3{\\q_no_value}}}",protected_flag:true}];
    let prop_remove_bases=[for(shape in prop_pop_shapes) {
        source:"\\" ++ shape.remove ++ ":Nn#1#2{\\__prop_pop:NnNNnTF#1{#2}\\" ++ shape.eq ++ "\\" ++ shape.set ++ "\\use_none:n{}{}}",protected_flag:true}];
    let prop_pop_conditionals=[for(shape in prop_pop_shapes,suffix in ["TF","T","F"]) {
        source:"\\" ++ shape.pop ++ ":NnN" ++ suffix ++ "#1#2#3{\\__prop_pop:NnNNnTF#1{#2}\\" ++ shape.eq ++ "\\" ++ shape.set ++ "{\\tl_set:Nn#3}\\prg_return_true:\\prg_return_false:\\exp_end:" ++
            (if(suffix=="T") "\\use:n\\use_none:n" else if(suffix=="F") "{}" else "") ++ "}",protected_flag:true}];
    let prop_pop_variants=[for(shape in prop_pop_shapes,suffix in ["","TF","T","F"]) {
        source:"\\" ++ shape.pop ++ ":cnN" ++ suffix ++ "{\\exp_args:Nc\\" ++ shape.pop ++ ":NnN" ++ suffix ++ "}",protected_flag:true}];
    let prop_remove_variants=[for(shape in prop_pop_shapes) {
        source:"\\" ++ shape.remove ++ ":cn{\\exp_args:Nc\\" ++ shape.remove ++ ":Nn}",protected_flag:true}];
    // The integer operations share TeX's register assignments and scope rules.
    let integer_ops = [for (scope in ["", "g"], op in [
        {name:"zero", params:"#1", body:"#1=\\c_zero_int"},
        {name:"set", params:"#1#2", body:"#1=\\__int_eval:w#2\\__int_eval_end:"},
        {name:"incr", params:"#1", body:"\\tex_advance:D#1\\c_one_int"},
        {name:"decr", params:"#1", body:"\\tex_advance:D#1-\\c_one_int"}]) {
        source:"\\int_" ++ scope ++ op.name ++ (if (op.name == "set") ":Nn" else ":N") ++ op.params ++
            "{" ++ (if (scope == "g") "\\tex_global:D" else "") ++ op.body ++ "}", protected_flag:true}];
    let integer_dynamic = [for (scope in ["", "g"], op in ["zero", "set", "incr", "decr"]) {
        source:"\\int_" ++ scope ++ op ++ (if (op == "set") ":cn" else ":c") ++
            "{\\exp_args:Nc\\int_" ++ scope ++ op ++ (if (op == "set") ":Nn" else ":N") ++ "}",
        protected_flag:true}];
    let token_dynamic = [for (scope in ["", "g"], form in ["n", "x"]) {
        source:"\\tl_" ++ scope ++ "set:c" ++ form ++ "{\\exp_args:Nc\\tl_" ++ scope ++ "set:N" ++ form ++ "}",
        protected_flag:true, source_mapping_guard:"dynamic_assignment", argument_unqualified:true}];
    // Native roman conversion emits letter-category tokens in both cases.
    let roman_letters = [for (form in ["roman", "Roman"], letter in ["i", "v", "x", "l", "c", "d", "m"]) {
        source:"\\__int_to_" ++ form ++ "_" ++ letter ++ ":w{" ++
            (if (form == "Roman") upper(letter)^ else letter) ++ "}", protected_flag:false}];
    // Decimal replication shares the native digit-by-digit expansion shapes.
    let replicate_digits = [for (digit in [0,1,2,3,4,5,6,7,8,9], first in [true,false]) {
        source:"\\__lambda_replicate:n#1{" ++ (if (first) "\\exp_end:" else
            "\\cs_end:{" ++ repeated("#1", 10)^ ++ "}") ++ repeated("#1", digit)^ ++ "}",
        install_as:"__prg_replicate_" ++ (if (first) "first_" else "") ++ string(digit) ++ ":n",
        protected_flag:false}];
    // The four native insertion shapes share value expansion and assignment order.
    let value_puts = [for (scope in ["", "g"], side in ["left", "right"], form in ["V", "v"]) {
        let value = "\\exp_not:" ++ form ++ (if (form == "V") "#2" else "{#2}");
        let prior = "\\__kernel_exp_not:w\\exp_after:wN{#1}";
        {source:"\\tl_" ++ scope ++ "put_" ++ side ++ ":N" ++ form ++ "#1#2{\\__kernel_tl_" ++
            scope ++ "set:Nx#1{" ++ (if (side == "left") value ++ prior else prior ++ value) ++ "}}",
            protected_flag:true, source_mapping_guard:"exp_args_value", argument_unqualified:true}
    }];
    let token_removals = [for (scope in ["", "g"], form in ["once", "all"]) {
        source:"\\tl_" ++ scope ++ "remove_" ++ form ++ ":Nn#1#2{\\tl_" ++ scope ++
            "replace_" ++ form ++ ":Nnn#1{#2}{}}", protected_flag:true, argument_unqualified:true}];
    // The boolean parser's operator control sequences are named through csname.
    let bool_returns = [for (i in [0,1,2]) {source:"\\__lambda_bool_return:{\\c_" ++
        (if (i == 0) "false" else "true") ++ "_bool}", install_as:"__bool_)_" ++ string(i) ++ ":", protected_flag:false}];
    let bool_gates = [for (gate in [
        {op:"&", state:0, select:"iv"}, {op:"&", state:1, select:"i"}, {op:"&", state:2, select:"iii"},
        {op:"|", state:0, select:"i"}, {op:"|", state:1, select:"iii"}, {op:"|", state:2, select:"iii"}]) {
        source:"\\__lambda_bool_gate:" ++ gate.op ++ "{\\__bool_get_next:NN\\use_" ++ gate.select ++ ":nnnn}",
        install_as:"__bool_" ++ gate.op ++ "_" ++ string(gate.state) ++ ":", protected_flag:false}];
    let bool_selectors = [for (selector in ["ii", "iii", "iv"]) {
        source:"\\use_" ++ selector ++ ":nnnn#1#2#3#4{#" ++
            (if (selector == "ii") "2" else if (selector == "iii") "3" else "4") ++ "}", protected_flag:false}];
    let bool_loops = [for (loop in [
        {name:"bool_while_do", test:"T", first:false}, {name:"bool_until_do", test:"F", first:false},
        {name:"bool_do_while", test:"T", first:true}, {name:"bool_do_until", test:"F", first:true}]) {
        source:"\\" ++ loop.name ++ ":nn#1#2{" ++ (if (loop.first) "#2" else "") ++
            "\\bool_if:n" ++ loop.test ++ "{#1}{" ++ (if (loop.first) "" else "#2") ++
            "\\" ++ loop.name ++ ":nn{#1}{#2}}}", protected_flag:false}];
    let macros = enable_expl_macro_sources(msg_support, [*variants,*pop_base,*pop_conditionals,*pop_variants,*prop_get_conditionals,*prop_get_variants,*prop_exp_helpers,*prop_pop_bases,*prop_remove_bases,*prop_pop_conditionals,*prop_pop_variants,*prop_remove_variants,*integer_ops,*integer_dynamic,*token_dynamic,*roman_letters,*replicate_digits,*value_puts,*token_removals,*bool_returns,*bool_gates,*bool_selectors,*bool_loops,
        {source:"\\ior_new:N#1{\\cs_new_eq:NN#1\\c__ior_term_ior}",protected_flag:true},
        {source:"\\ior_new:c{\\exp_args:Nc\\ior_new:N}",protected_flag:true},
        {source:"\\ior_open:cn{\\exp_args:Nc\\ior_open:Nn}",protected_flag:true},
        {source:"\\ior_close:c{\\exp_args:Nc\\ior_close:N}",protected_flag:true},
        {source:"\\int_new:N#1{\\__kernel_chk_if_free_cs:N#1\\cs:w newcount\\cs_end:#1}",protected_flag:true},
        {source:"\\int_new:c{\\exp_args:Nc\\int_new:N}",protected_flag:true},
        {source:"\\int_to_roman:n#1{\\exp_after:wN\\__int_to_roman:N\\__int_to_roman:w\\int_eval:n{#1}Q}",protected_flag:false,source_mapping_guard:"int_math"},
        {source:"\\__int_to_roman:N#1{\\use:c{__int_to_roman_#1:w}\\__int_to_roman:N}",protected_flag:false},
        {source:"\\__int_to_roman_Q:w#1{}",protected_flag:false},
        {source:"\\int_to_Roman:n#1{\\exp_after:wN\\__int_to_Roman_aux:N\\__int_to_roman:w\\int_eval:n{#1}Q}",protected_flag:false,source_mapping_guard:"int_math"},
        {source:"\\__int_to_Roman_aux:N#1{\\use:c{__int_to_Roman_#1:w}\\__int_to_Roman_aux:N}",protected_flag:false},
        {source:"\\__int_to_Roman_Q:w#1{}",protected_flag:false},
        {source:"\\prg_replicate:nn#1{\\exp:w\\exp_after:wN\\__prg_replicate_first:N\\int_value:w\\int_eval:n{#1}\\cs_end:}",protected_flag:false,source_mapping_guard:"replicate"},
        {source:"\\__prg_replicate:N#1{\\cs:w __prg_replicate_#1:n\\__prg_replicate:N}",protected_flag:false},
        {source:"\\__prg_replicate_first:N#1{\\cs:w __prg_replicate_first_#1:n\\__prg_replicate:N}",protected_flag:false},
        {source:"\\__prg_replicate_ :n#1{\\cs_end:}",protected_flag:false},
        {source:"\\__lambda_replicate:n#1{\\exp_end:\\msg_expandable_error:nn{prg}{negative-replication}}",install_as:"__prg_replicate_first_-:n",protected_flag:false,source_mapping_guard:"replicate_negative"},
        {source:"\\q_no_value{\\q_no_value}",protected_flag:false},
        {source:"\\exp_args:NNe#1#2#3{\\exp_after:wN#1\\exp_after:wN#2\\tex_expanded:D{{#3}}}",protected_flag:false},
        {source:"\\exp_args:Nce#1#2#3{\\tex_expanded:D{\\exp_not:N#1\\exp_after:wN\\exp_not:N\\cs:w#2\\cs_end:{\\__kernel_exp_not:w\\tex_expanded:D{{#3}}}}}",protected_flag:false},
        {source:"\\exp_args:Ncx#1#2#3{\\use:x{\\exp_not:N#1\\exp_after:wN\\exp_not:N\\cs:w#2\\cs_end:{#3}}}",protected_flag:true},
        {source:"\\exp_args:Ncnc#1#2#3#4{\\tex_expanded:D{\\exp_not:N#1\\exp_after:wN\\exp_not:N\\cs:w#2\\cs_end:{\\exp_not:n{#3}}\\exp_after:wN\\exp_not:N\\cs:w#4\\cs_end:}}",protected_flag:false},
        // Native mapping delimiters preserve nested breaks and expandable calls.
        {source:"\\prg_break:#1\\prg_break_point:{}",protected_flag:false},
        {source:"\\seq_map_break:{\\prg_map_break:Nn\\seq_map_break:{}}",protected_flag:false},
        {source:"\\seq_map_break:n{\\prg_map_break:Nn\\seq_map_break:}",protected_flag:false},
        {source:"\\__seq_push_item_def:n{\\__seq_push_item_def:\\cs_gset:Npn\\__seq_item:n##1}",protected_flag:true},
        {source:"\\__seq_push_item_def:{\\int_gincr:N\\g__kernel_prg_map_int\\cs_gset_eq:cN{__seq_map_\\int_use:N\\g__kernel_prg_map_int :w}\\__seq_item:n}",protected_flag:true},
        {source:"\\__seq_pop_item_def:{\\cs_gset_eq:Nc\\__seq_item:n{__seq_map_\\int_use:N\\g__kernel_prg_map_int :w}\\int_gdecr:N\\g__kernel_prg_map_int}",protected_flag:true},
        {source:"\\seq_map_inline:Nn#1#2{\\__seq_push_item_def:n{#2}#1\\prg_break_point:Nn\\seq_map_break:{\\__seq_pop_item_def:}}",protected_flag:true,source_mapping_guard:"seq_map",argument_unqualified:true},
        {source:"\\seq_map_indexed_inline:Nn#1#2{\\int_gincr:N\\g__kernel_prg_map_int\\cs_gset_protected:cpn{__seq_map_\\int_use:N\\g__kernel_prg_map_int :w}##1##2{#2}\\exp_args:NNc\\__seq_map_indexed:NN#1{__seq_map_\\int_use:N\\g__kernel_prg_map_int :w}\\prg_break_point:Nn\\seq_map_break:{\\int_gdecr:N\\g__kernel_prg_map_int}}",protected_flag:true,source_mapping_guard:"seq_map",argument_unqualified:true},
        {source:"\\seq_map_indexed_function:NN#1#2{\\__seq_map_indexed:NN#1#2\\prg_break_point:Nn\\seq_map_break:{}}",protected_flag:false,source_mapping_guard:"seq_map"},
        {source:"\\__seq_map_indexed:NN#1#2{\\exp_after:wN\\__seq_map_indexed:Nw\\exp_after:wN#2\\int_value:w1\\exp_after:wN\\use_i:nn\\exp_after:wN;#1\\prg_break:\\__seq_item:n{}\\prg_break_point:}",protected_flag:false},
        {source:"\\__seq_map_indexed:Nw#1#2;#3\\__seq_item:n#4{#3#1{#2}{#4}\\exp_after:wN\\__seq_map_indexed:Nw\\exp_after:wN#1\\int_value:w\\int_eval:w1+#2;}",protected_flag:false},
        {source:"\\clist_map_break:{\\prg_map_break:Nn\\clist_map_break:{}}",protected_flag:false},
        {source:"\\clist_map_break:n{\\prg_map_break:Nn\\clist_map_break:}",protected_flag:false},
        {source:"\\tl_map_break:{\\prg_map_break:Nn\\tl_map_break:{}}",protected_flag:false},
        {source:"\\tl_map_break:n{\\prg_map_break:Nn\\tl_map_break:}",protected_flag:false},
        {source:"\\tl_map_function:nN#1#2{\\__tl_map_function:Nnnnnnnnn#2#1" ++ tl_map_stops ++ "\\prg_break_point:Nn\\tl_map_break:{}}",protected_flag:false,source_mapping_guard:"tl_map"},
        {source:"\\tl_map_function:NN{\\exp_args:No\\tl_map_function:nN}",protected_flag:false,source_mapping_guard:"tl_map"},
        {source:"\\__tl_map_function:Nnnnnnnnn#1#2#3#4#5#6#7#8#9{\\__tl_use_none_delimit_by_s_stop:w#9\\__tl_map_function_end:w\\s__tl_stop" ++ tl_map_steps ++ "\\__tl_map_function:Nnnnnnnnn#1}",protected_flag:false},
        {source:"\\__tl_map_function_end:w\\s__tl_stop#1#2{\\__tl_use_none_delimit_by_s_stop:w#2\\tl_map_break:\\s__tl_stop#1{#2}\\__tl_map_function_end:w\\s__tl_stop}",protected_flag:false},
        {source:"\\__tl_use_none_delimit_by_s_stop:w#1\\s__tl_stop{}",protected_flag:false},
        {source:"\\tl_map_inline:nn#1#2{\\int_gincr:N\\g__kernel_prg_map_int\\cs_gset_protected:cpn{__tl_map_\\int_use:N\\g__kernel_prg_map_int :w}##1{#2}\\exp_args:Nc\\__tl_map_function:Nnnnnnnnn{__tl_map_\\int_use:N\\g__kernel_prg_map_int :w}#1" ++ tl_map_stops ++ "\\prg_break_point:Nn\\tl_map_break:{\\int_gdecr:N\\g__kernel_prg_map_int}}",protected_flag:true,source_mapping_guard:"tl_map",argument_unqualified:true},
        {source:"\\tl_map_inline:Nn{\\exp_args:No\\tl_map_inline:nn}",protected_flag:true,source_mapping_guard:"tl_map",argument_unqualified:true},
        {source:"\\seq_map_function:NN#1#2{\\exp_after:wN\\use_i_ii:nnn\\exp_after:wN\\__seq_map_function:Nw\\exp_after:wN#2#1\\prg_break:\\__seq_item:n{}\\__seq_item:n{}\\__seq_item:n{}\\__seq_item:n{}\\prg_break_point:\\prg_break_point:Nn\\seq_map_break:{}}",protected_flag:false},
        {source:"\\__seq_map_function:Nw#1#2\\__seq_item:n#3#4\\__seq_item:n#5#6\\__seq_item:n#7#8\\__seq_item:n#9{#2#1{#3}#4#1{#5}#6#1{#7}#8#1{#9}\\__seq_map_function:Nw#1}",protected_flag:false},
        {source:"\\seq_map_function:cN{\\exp_args:Nc\\seq_map_function:NN}",protected_flag:false},
        {source:"\\__seq_pop:NNNN#1#2#3#4{\\if_meaning:w#3\\c_empty_seq\\tl_set:Nn#4{\\q_no_value}\\else:#1#2#3#4\\fi:}",protected_flag:true},
        {source:"\\__seq_pop_TF:NNNN#1#2#3#4{\\if_meaning:w#3\\c_empty_seq\\prg_return_false:\\else:#1#2#3#4\\prg_return_true:\\fi:}",protected_flag:true},
        {source:"\\__seq_pop_left:NNN#1#2#3{\\exp_after:wN\\__seq_pop_left:wnwNNN#2\\s__seq_stop#1#2#3}",protected_flag:true},
        {source:"\\__seq_pop_left:wnwNNN#1\\__seq_item:n#2#3\\s__seq_stop#4#5#6{#4#5{#1#3}\\tl_set:Nn#6{#2}}",protected_flag:true},
        {source:"\\__seq_pop_right:NNN#1#2#3{\\cs_set_eq:NN\\__seq_tmp:w\\__seq_item:n\\cs_set_eq:NN\\__seq_item:n\\scan_stop:#1#2{\\if_false:}\\fi:\\s__seq\\exp_after:wN\\use_i:nnn\\exp_after:wN\\__seq_pop_right_loop:nn#2{\\if_false:{\\fi:}\\__kernel_tl_set:Nx#3}{}\\use_none:nn\\cs_set_eq:NN\\__seq_item:n\\__seq_tmp:w}",protected_flag:true},
        {source:"\\__seq_pop_right_loop:nn#1#2{#2{\\exp_not:n{#1}}\\__seq_pop_right_loop:nn}",protected_flag:false},
        {source:"\\use_none:nn#1#2{}",protected_flag:false},
        {source:"\\c__msg_text_prefix_tl{msg~text~>~}",protected_flag:false},
        {source:"\\c__msg_more_text_prefix_tl{msg~extra~text~>~}",protected_flag:false},
        {source:"\\cs_gset:cpn{\\exp_args:Nc\\cs_gset:Npn}",protected_flag:true,long_ok:false},
        {source:"\\cs_gset_protected:cpn{\\exp_args:Nc\\cs_gset_protected:Npn}",protected_flag:true,long_ok:false,source_mapping_guard:"dynamic_assignment",argument_unqualified:true},
        {source:"\\cs_set:cpn{\\exp_args:Nc\\cs_set:Npn}",protected_flag:true,long_ok:false},
        {source:"\\msg_if_exist:nnT#1#2{\\cs_if_exist:cTF{\\c__msg_text_prefix_tl#1/#2}{\\prg_return_true:}{\\prg_return_false:}\\exp_end:\\use:n\\use_none:n}",protected_flag:false},
        {source:"\\msg_if_exist:nnTF#1#2{\\cs_if_exist:cTF{\\c__msg_text_prefix_tl#1/#2}{\\prg_return_true:}{\\prg_return_false:}\\exp_end:}",protected_flag:false},
        {source:"\\__msg_chk_free:nn#1#2{\\msg_if_exist:nnT{#1}{#2}{\\msg_error:nnnn{msg}{already-defined}{#1}{#2}}}",protected_flag:true},
        {source:"\\msg_new:nnnn#1#2#3#4{\\__msg_chk_free:nn{#1}{#2}\\cs_gset:cpn{\\c__msg_text_prefix_tl#1/#2}##1##2##3##4{#3}\\cs_gset:cpn{\\c__msg_more_text_prefix_tl#1/#2}##1##2##3##4{#4}}",protected_flag:true},
        {source:"\\msg_new:nnn#1#2#3{\\msg_new:nnnn{#1}{#2}{#3}{}}",protected_flag:true},
        {source:"\\msg_set:nnnn#1#2#3#4{\\cs_set:cpn{\\c__msg_text_prefix_tl#1/#2}##1##2##3##4{#3}\\cs_set:cpn{\\c__msg_more_text_prefix_tl#1/#2}##1##2##3##4{#4}}",protected_flag:true},
        {source:"\\msg_set:nnn#1#2#3{\\msg_set:nnnn{#1}{#2}{#3}{}}",protected_flag:true},
        {source:"\\__msg_use_none_delimit_by_s_stop:w#1\\s__msg_stop{}",protected_flag:false},
        {source:"\\msg_module_type:n#1{\\prop_if_in:NnTF\\g_msg_module_type_prop{#1}{\\prop_item:Nn\\g_msg_module_type_prop{#1}}{Package}}",protected_flag:false},
        {source:"\\msg_module_name:n#1{\\prop_if_in:NnTF\\g_msg_module_name_prop{#1}{\\prop_item:Nn\\g_msg_module_name_prop{#1}}{#1}}",protected_flag:false},
        {source:"\\msg_error_text:n#1{\\__msg_text:nn{#1}{Error}}",protected_flag:false},
        {source:"\\__msg_text:nn#1#2{\\exp_args:Nf\\__msg_text:n{\\msg_module_type:n{#1}}\\exp_args:Nf\\__msg_text:n{\\msg_module_name:n{#1}}#2}",protected_flag:false},
        {source:"\\__msg_text:n#1{\\tl_if_blank:nF{#1}{#1~}}",protected_flag:false},
        {source:"\\exp_args:Nee#1#2#3{\\tex_expanded:D{\\exp_not:N#1{\\__kernel_exp_not:w\\tex_expanded:D{{#2}}}{\\__kernel_exp_not:w\\tex_expanded:D{{#3}}}}}",protected_flag:false},
        {source:"\\exp_args:Noooo#1#2#3#4#5{\\tex_expanded:D{\\exp_not:N#1{\\__kernel_exp_not:w\\exp_after:wN{#2}}{\\__kernel_exp_not:w\\exp_after:wN{#3}}{\\__kernel_exp_not:w\\exp_after:wN{#4}}{\\__kernel_exp_not:w\\exp_after:wN{#5}}}}",protected_flag:false},
        {source:"\\cs_to_str:N{\\tex_romannumeral:D\\if:w\\token_to_str:N\\ \\__cs_to_str:w\\fi:\\exp_after:wN\\__cs_to_str:N\\token_to_str:N}",protected_flag:false},
        {source:"\\__cs_to_str:N#1{\\c_zero_int}",protected_flag:false},
        {source:"\\__cs_to_str:w#1\\__cs_to_str:N{-\\int_value:w\\fi:\\exp_after:wN\\c_zero_int}",protected_flag:false},
        {source:"\\__msg_expandable_error:nn#1#2{\\exp_after:wN\\exp_after:wN\\exp_after:wN\\__msg_use_none_delimit_by_s_stop:w\\use:n{\\???~!~#2:~#1}\\s__msg_stop}",protected_flag:false,question_letters:true},
        {source:"\\msg_expandable_error:nnnnnn#1#2#3#4#5#6{\\exp_args:Nee\\__msg_expandable_error:nn{\\exp_args:Nc\\exp_args:Noooo{\\c__msg_text_prefix_tl#1/#2}{\\tl_to_str:n{#3}}{\\tl_to_str:n{#4}}{\\tl_to_str:n{#5}}{\\tl_to_str:n{#6}}}{\\msg_error_text:n{#1}}}",protected_flag:false},
        {source:"\\msg_expandable_error:nnnnn#1#2#3#4#5{\\msg_expandable_error:nnnnnn{#1}{#2}{#3}{#4}{#5}{}}",protected_flag:false},
        {source:"\\msg_expandable_error:nnnn#1#2#3#4{\\msg_expandable_error:nnnnnn{#1}{#2}{#3}{#4}{}{}}",protected_flag:false},
        {source:"\\msg_expandable_error:nnn#1#2#3{\\msg_expandable_error:nnnnnn{#1}{#2}{#3}{}{}{}}",protected_flag:false},
        {source:"\\msg_expandable_error:nn#1#2{\\msg_expandable_error:nnnnnn{#1}{#2}{}{}{}{}}",protected_flag:false},
        {source:"\\prop_new:N#1{\\__kernel_chk_if_free_cs:N#1\\cs_gset_eq:NN#1\\c_empty_prop}",protected_flag:true},
        {source:"\\__kernel_chk_if_free_cs:N#1{\\cs_if_free:NF#1{\\msg_error:nnee{kernel}{command-already-defined}{\\token_to_str:N#1}{\\token_to_meaning:N#1}}}",protected_flag:true},
        {source:"\\prop_put:Nnn{\\__prop_put:nNNnn{}\\cs_set_nopar:Npe}",protected_flag:true},
        {source:"\\prop_gput:Nnn{\\__prop_put:nNNnn{}\\cs_gset_nopar:Npe}",protected_flag:true},
        {source:"\\use_i:nnn#1#2#3{#1}",protected_flag:false},
        {source:"\\use_ii:nnn#1#2#3{#2}",protected_flag:false},
        {source:"\\exp_not:o#1{\\__kernel_exp_not:w\\exp_after:wN{#1}}",protected_flag:false},
        {source:"\\l__prop_internal_tl{}",protected_flag:false},
        {source:"\\__prop_chk:w{\\__prop_chk_loop:nw{}}",protected_flag:true},
        {source:"\\__prop_pair:wn#1\\s__prop#2{}",protected_flag:false},
        {source:"\\__prop_put:nNNnn#1#2#3#4#5{\\tl_set:Nn\\l__prop_internal_tl{\\exp_not:N\\__prop_pair:wn\\tl_to_str:n{#4}\\s__prop{\\exp_not:n{#5}}}\\__prop_split:NnTFn#3{#4}{#1#2#3{\\s__prop\\__prop_chk:w\\exp_not:n{##1}\\l__prop_internal_tl\\exp_not:n{##3}}}{#2#3{\\exp_not:o{#3}\\l__prop_internal_tl}}{\\exp_after:wN\\__prop_put_linked:wnnN#3{#4}{#1}#2}}",protected_flag:true},
        {source:"\\__prop_split:NnTFn#1#2{\\exp_after:wN\\__prop_split_aux:nNTFn\\exp_after:wN{\\tl_to_str:n{#2}}#1}",protected_flag:true},
        {source:"\\__prop_split_aux:nNTFn#1#2#3{\\cs_set:Npn\\__prop_split_flat:w\\__prop_split_linked:w##1\\__prop_pair:wn#1\\s__prop##2##3\\s__prop_mark##4##5\\s__prop_stop{##4{#3}}\\exp_after:wN\\__prop_split_test:wn#2\\s__prop_mark\\use_i:nnn\\__prop_pair:wn#1\\s__prop{?\\fi:\\__prop_split_wrong:Nw#2}\\s__prop_mark\\use_ii:nnn\\s__prop_stop}",protected_flag:true},
        {source:"\\__prop_split_flat:w{}",protected_flag:false},
        {source:"\\__prop_pop:NnNNnTF#1#2#3#4#5#6#7{\\__prop_split:NnTFn#1{#2}{#4#1{\\exp_not:n{\\s__prop\\__prop_chk:w##1##3}}#5{##2}#6}{#7}{\\exp_after:wN\\__prop_pop_linked:wnNNnTF#1{#2}#3#4{#5}{#6}{#7}}}",protected_flag:true},
        {source:"\\__prop_get:NnnTF#1#2#3#4#5{\\__prop_split:NnTFn#1{#2}{#3{##2}#4}{#5}{\\exp_after:wN\\__prop_get_linked:w#1{#2}{#3}{#4}{#5}}}",protected_flag:true},
        {source:"\\prop_get:NnN#1#2#3{\\__prop_get:NnnTF#1{#2}{\\tl_set:Nn#3}{}{\\tl_set:Nn#3{\\q_no_value}}}",protected_flag:true},
        {source:"\\__prop_split_test:wn#1\\s__prop#2{\\if_meaning:w\\__prop_chk:w#2\\exp_after:wN\\__prop_split_flat:w\\fi:\\__prop_split_linked:w}",protected_flag:true},
        {source:"\\__prop_split_linked:w#1\\s__prop_stop#2#3{#3}",protected_flag:true},
        {source:"\\__prop_if_flat:NTF#1{\\prop_if_exist:NT#1\\exp_after:wN\\__prop_if_flat_aux:w#1\\s__prop_mark\\use_ii:nn\\__prop_flatten:w\\s__prop_mark\\use_i:nn\\s__prop_stop}",protected_flag:false},
        {source:"\\__prop_if_flat_aux:w#1\\__prop_flatten:w#2\\s__prop_mark#3#4\\s__prop_stop{#3}",protected_flag:false},
        {source:"\\exp_args:NNo#1#2#3{\\exp_after:wN#1\\exp_after:wN#2\\exp_after:wN{#3}}",protected_flag:false},
        {source:"\\:::#1{#1}",protected_flag:false},
        {source:"\\::n#1\\:::#2#3{#1\\:::{#2{#3}}}",protected_flag:false},
        {source:"\\__exp_arg_last_unbraced:nn#1#2{#2#1}",protected_flag:false},
        {source:"\\::f_unbraced\\:::#1#2{\\exp_after:wN\\__exp_arg_last_unbraced:nn\\exp_after:wN{\\exp:w\\exp_end_continue_f:w#2}{#1}}",protected_flag:false},
        {source:"\\exp_last_unbraced:Nnf{\\::n\\::f_unbraced\\:::}",protected_flag:false},
        {source:"\\prg_break_point:Nn#1#2{#2}",protected_flag:false},
        {source:"\\use_iii:nnn#1#2#3{#3}",protected_flag:false},
        {source:"\\prg_map_break:Nn#1#2#3\\prg_break_point:Nn#4#5{#5\\if_meaning:w#1#4\\exp_after:wN\\use_iii:nnn\\fi:\\prg_map_break:Nn#1{#2}}",protected_flag:false},
        {source:"\\prop_map_break:{\\prg_map_break:Nn\\prop_map_break:{}}",protected_flag:false},
        {source:"\\prop_map_break:n{\\prg_map_break:Nn\\prop_map_break:}",protected_flag:false},
        {source:"\\prop_map_tokens:Nn#1#2{\\exp_last_unbraced:Nnf\\use_i:nnn{\\__prop_map_tokens:nw{#2}}#1" ++ repeated(prop_map_terminator,4)^ ++ "\\prg_break_point:Nn\\prop_map_break:{}}",protected_flag:false},
        {source:"\\__prop_map_tokens:nw#1\\__prop_pair:wn#2\\s__prop#3\\__prop_pair:wn#4\\s__prop#5\\__prop_pair:wn#6\\s__prop#7\\__prop_pair:wn#8\\s__prop#9{" ++ prop_map_steps ++ "\\__prop_map_tokens:nw{#1}}",protected_flag:false},
        {source:"\\str_if_eq:eeT#1#2{\\if:w0\\__str_if_eq:nn{#1}{#2}\\__prg_T_true:w\\fi:\\use_none:n}",protected_flag:false},
        {source:"\\__prop_item:nnn#1#2#3{\\str_if_eq:eeT{#1}{#2}{\\prop_map_break:n{\\exp_not:n{#3}}}}",protected_flag:false},
        {source:"\\__prop_if_in_flat:nnn#1#2#3{\\str_if_eq:eeT{#1}{#2}{\\prop_map_break:n{\\use_i:nn\\prg_return_true:}}}",protected_flag:false},
        {source:"\\prop_item:Nn#1#2{\\__prop_if_flat:NTF#1{\\exp_args:NNo\\prop_map_tokens:Nn#1{\\exp_after:wN\\__prop_item:nnn\\exp_after:wN{\\tl_to_str:n{#2}}}}{\\exp_after:wN\\__prop_get_linked:w#1{#2}\\exp_not:n{}{}}}",protected_flag:false},
        {source:"\\prop_if_in:NnTF#1#2{" ++ property_in_body ++ "}",protected_flag:false},
        {source:"\\prop_if_in:NnT#1#2{" ++ property_in_body ++ "\\use:n\\use_none:n}",protected_flag:false},
        {source:"\\prop_if_in:NnF#1#2{" ++ property_in_body ++ "{}}",protected_flag:false},
        // bundled helpers evaluate their operands in the surrounding token stream.
        {source: "\\int_abs:n#1{\\int_value:w\\exp_after:wN\\__int_abs:N\\int_value:w\\__int_eval:w#1\\__int_eval_end:\\exp_stop_f:}", protected_flag: false, source_mapping_guard: "int_math"},
        {source: "\\__int_abs:N#1{\\if_meaning:w-#1\\else:\\exp_after:wN#1\\fi:}", protected_flag: false},
        {source: "\\int_max:nn#1#2{\\int_value:w\\exp_after:wN\\__int_maxmin:wwN\\int_value:w\\__int_eval:w#1\\exp_after:wN\\__int_sep:\\int_value:w\\__int_eval:w#2\\__int_sep:>\\exp_stop_f:}", protected_flag: false, source_mapping_guard: "int_math"},
        {source: "\\int_min:nn#1#2{\\int_value:w\\exp_after:wN\\__int_maxmin:wwN\\int_value:w\\__int_eval:w#1\\exp_after:wN\\__int_sep:\\int_value:w\\__int_eval:w#2\\__int_sep:<\\exp_stop_f:}", protected_flag: false, source_mapping_guard: "int_math"},
        {source: "\\__int_maxmin:wwN#1\\__int_sep:#2\\__int_sep:#3{\\if_int_compare:w#1#3#2~#1\\else:#2\\fi:}", protected_flag: false},
        {source: "\\int_div_round:nn#1#2{\\int_value:w\\__int_eval:w(#1)/(#2)\\__int_eval_end:}", protected_flag: false, source_mapping_guard: "int_math"},
        {source: "\\use:x#1{\\cs_set_nopar:Npx\\l__exp_internal_tl{#1}\\l__exp_internal_tl}", protected_flag: true},
        {source: "\\exp_args:NNx#1#2#3{\\use:x{\\exp_not:N#1\\exp_not:N#2{#3}}}", protected_flag: true},
        {source: "\\cs_set_eq:NN#1{\\tex_let:D#1=~}", protected_flag: true},
        {source: "\\bool_new:N#1{\\cs_new_eq:NN#1\\c_false_bool}", protected_flag: true},
        {source: "\\bool_new:c{\\exp_args:Nc\\bool_new:N}", protected_flag: true},
        {source: "\\cs_gset_eq:NN{\\tex_global:D\\cs_set_eq:NN}", protected_flag: true},
        {source: "\\cs_gset_eq:cN{\\exp_args:Nc\\cs_gset_eq:NN}", protected_flag:true,source_mapping_guard:"dynamic_assignment",argument_unqualified:true},
        {source: "\\cs_gset_eq:Nc{\\exp_args:NNc\\cs_gset_eq:NN}", protected_flag:true,source_mapping_guard:"dynamic_assignment",argument_unqualified:true},
        {source: "\\bool_set_true:N#1{\\cs_set_eq:NN#1\\c_true_bool}", protected_flag: true},
        {source: "\\bool_set_false:N#1{\\cs_set_eq:NN#1\\c_false_bool}", protected_flag: true},
        {source: "\\bool_gset_true:N#1{\\cs_gset_eq:NN#1\\c_true_bool}", protected_flag: true},
        {source: "\\bool_gset_false:N#1{\\cs_gset_eq:NN#1\\c_false_bool}", protected_flag: true},
        {source: "\\bool_set_inverse:N#1{\\bool_if:NTF#1{\\bool_set_false:N}{\\bool_set_true:N}#1}", protected_flag: true},
        {source: "\\bool_gset_inverse:N#1{\\bool_if:NTF#1{\\bool_gset_false:N}{\\bool_gset_true:N}#1}", protected_flag: true},
        {source: "\\bool_set_inverse:c{\\exp_args:Nc\\bool_set_inverse:N}", protected_flag: true},
        {source: "\\bool_gset_inverse:c{\\exp_args:Nc\\bool_gset_inverse:N}", protected_flag: true},
        {source: "\\use_i_ii:nnn#1#2#3{#1#2}", protected_flag: false},
        {source: "\\__exp_eval_register:N#1{\\exp_after:wN\\if_meaning:w\\exp_not:N#1#1\\if_meaning:w\\scan_stop:#1\\__exp_eval_error_msg:w\\fi:\\else:\\exp_after:wN\\use_i_ii:nnn\\fi:\\exp_after:wN\\exp_end:\\tex_the:D#1}", protected_flag: false},
        {source: "\\__exp_eval_error_msg:w#1\\tex_the:D#2{\\fi:\\fi:\\msg_expandable_error:nnn{kernel}{bad-variable}{#2}\\exp_end:}", protected_flag: false},
        {source: "\\__exp_eval_register:c#1{\\exp_after:wN\\__exp_eval_register:N\\cs:w#1\\cs_end:}", protected_flag: false},
        {source: "\\exp_not:V#1{\\__kernel_exp_not:w\\exp_after:wN{\\exp:w\\__exp_eval_register:N#1}}", protected_flag:false, source_mapping_guard:"exp_args_value"},
        {source: "\\exp_not:v#1{\\__kernel_exp_not:w\\exp_after:wN{\\exp:w\\__exp_eval_register:c{#1}}}", protected_flag:false, source_mapping_guard:"exp_args_value"},
        {source: "\\exp_args:Nv#1#2{\\exp_after:wN#1\\exp_after:wN{\\exp:w\\__exp_eval_register:c{#2}}}", protected_flag: false, source_mapping_guard: "exp_args_value"},
        {source: "\\tl_head:v{\\exp_args:Nv\\tl_head:n}", protected_flag: false},
        {source: "\\tl_tail:v{\\exp_args:Nv\\tl_tail:n}", protected_flag: false},
        {source: "\\exp_args:NV#1#2{\\exp_after:wN#1\\exp_after:wN{\\exp:w\\__exp_eval_register:N#2}}", protected_flag: false, source_mapping_guard: "exp_args_value"},
        {source:"\\__regex_build:n{\\__regex_build_aux:Nn\\c_true_bool}",protected_flag:true,source_mapping_guard:"regex_builder"},
        {source:"\\__regex_return:{\\if_meaning:w\\c_true_bool\\g__regex_success_bool\\prg_return_true:\\else:\\prg_return_false:\\fi:}",protected_flag:true},
        {source:"\\regex_match:nnTF#1#2{\\__regex_if_match:nn{\\__regex_build:n{#1}}{#2}\\__regex_return:\\exp_end:}",protected_flag:true,argument_unqualified:true},
        {source:"\\regex_match:nnT#1#2{\\__regex_if_match:nn{\\__regex_build:n{#1}}{#2}\\__regex_return:\\exp_end:\\use:n\\use_none:n}",protected_flag:true,argument_unqualified:true},
        {source:"\\regex_match:nnF#1#2{\\__regex_if_match:nn{\\__regex_build:n{#1}}{#2}\\__regex_return:\\exp_end:{}}",protected_flag:true,argument_unqualified:true},
        // Preserve the first argument while expanding the second variable's value.
        {source: "\\exp_args:NnV#1#2#3{\\tex_expanded:D{\\exp_not:N#1{\\exp_not:n{#2}}{\\__kernel_exp_not:w\\exp_after:wN{\\exp:w\\__exp_eval_register:N#3}}}}", protected_flag:false, source_mapping_guard:"exp_args_value"},
        {source: "\\tl_head:V{\\exp_args:NV\\tl_head:n}", protected_flag: false},
        {source: "\\tl_tail:V{\\exp_args:NV\\tl_tail:n}", protected_flag: false},
        {source: "\\exp_args:No#1#2{\\exp_after:wN#1\\exp_after:wN{#2}}", protected_flag: false, source_mapping_guard: "exp_args_once"},
        // f expansion stops at the first unexpandable token, preserving the remaining tokens.
        {source: "\\exp_args:Nf#1#2{\\exp_after:wN#1\\exp_after:wN{\\exp:w\\exp_end_continue_f:w#2}}", protected_flag: false, source_mapping_guard: "exp_args_f"},
        {source: "\\tl_head:f{\\exp_args:Nf\\tl_head:n}", protected_flag: false},
        {source: "\\tl_tail:f{\\exp_args:Nf\\tl_tail:n}", protected_flag: false},
        {source: "\\tl_head:N{\\exp_args:No\\tl_head:n}", protected_flag: false},
        {source: "\\tl_tail:N{\\exp_args:No\\tl_tail:n}", protected_flag: false},
        {source: "\\tl_tail:n#1{\\__kernel_exp_not:w\\if:w\\scan_stop:\\__kernel_tl_to_str:w\\exp_after:wN{\\use_none:n#1?}\\scan_stop:\\__prg_TF_true:w\\fi:\\use_ii:nn{{}}{\\exp_after:wN{\\use_none:n#1}}}", protected_flag: false, source_mapping_guard: "tl_tail"},
        {source: "\\tl_head:n#1{\\__kernel_exp_not:w\\tex_expanded:D{{\\if_false:{\\fi:\\__tl_head_aux:n#1{}}}}}", protected_flag: false, source_mapping_guard: "tl_head"},
        {source: "\\__tl_head_aux:n#1{\\__kernel_exp_not:w{#1}\\exp_after:wN\\use_none:n\\exp_after:wN{\\if_false:}\\fi:}", protected_flag: false},
        {source: "\\int_if_zero_p:n#1{\\if_int_compare:w\\__int_eval:w#1=\\c_zero_int\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\int_if_odd_p:n#1{\\if_int_odd:w\\__int_eval:w#1\\__int_eval_end:\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\int_if_even_p:n#1{\\reverse_if:N\\if_int_odd:w\\__int_eval:w#1\\__int_eval_end:\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\bool_if:NTF#1{\\if_bool:N#1\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag: false},
        {source: "\\bool_if:NT#1{\\if_bool:N#1\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag: false},
        {source: "\\bool_if:NF#1{\\if_bool:N#1\\__prg_F_true:w\\fi:\\use:n}", protected_flag: false},
        {source: "\\bool_if_p:N#1{\\if_bool:N#1\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\bool_if:nTF#1{\\if_predicate:w\\bool_if_p:n{#1}\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag:false, source_mapping_guard:"bool_expr"},
        {source: "\\bool_if:nT#1{\\if_predicate:w\\bool_if_p:n{#1}\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag:false, source_mapping_guard:"bool_expr"},
        {source: "\\bool_if:nF#1{\\if_predicate:w\\bool_if_p:n{#1}\\__prg_F_true:w\\fi:\\use:n}", protected_flag:false, source_mapping_guard:"bool_expr"},
        {source: "\\bool_if_p:n{\\exp_args:Nf\\__bool_if_p:n}", protected_flag:false, source_mapping_guard:"bool_expr"},
        {source: "\\__bool_if_p:n#1{\\tl_if_empty:oT{\\use_none:nn#1.}{\\__bool_if_p_aux:w}\\group_align_safe_begin:\\exp_after:wN\\group_align_safe_end:\\exp:w\\exp_end_continue_f:w\\__bool_get_next:NN\\use_i:nnnn#1)}", protected_flag:false},
        {source: "\\__bool_if_p_aux:w#1\\use_i:nnnn#2#3{\\bool_if:NTF#2\\c_true_bool\\c_false_bool}", protected_flag:false},
        {source: "\\__bool_get_next:NN#1#2{\\use:c{__bool_\\if_meaning:w!#2!\\else:\\if_meaning:w(#2(\\else: p\\fi:\\fi: :Nw}#1#2}", protected_flag:false},
        {source: "\\__lambda_bool_bang:Nw#1#2{\\exp_after:wN\\__bool_get_next:NN#1\\use_ii:nnnn\\use_i:nnnn\\use_iii:nnnn\\use_iv:nnnn}", install_as:"__bool_!:Nw", protected_flag:false},
        {source: "\\__lambda_bool_open:Nw#1#2{\\exp_after:wN\\__bool_choose:NNN\\exp_after:wN#1\\int_value:w\\__bool_get_next:NN\\use_i:nnnn}", install_as:"__bool_(:Nw", protected_flag:false},
        {source: "\\__bool_p:Nw#1{\\exp_after:wN\\__bool_choose:NNN\\exp_after:wN#1\\int_value:w}", protected_flag:false},
        {source: "\\__bool_choose:NNN#1#2#3{\\use:c{__bool_\\token_to_str:N#3_#1#2{\\if_meaning:w0#21\\else:0\\fi:}20:}}", protected_flag:false},
        {source: "\\use:c#1{\\cs:w#1\\cs_end:}", protected_flag:false},
        {source: "\\tl_if_empty:oT{\\exp_args:No\\tl_if_empty:nT}", protected_flag:false},
        {source: "\\str_case:nn#1#2{\\exp:w\\__str_case:nnTF{#1}{#2}{}{}}", protected_flag:false, source_mapping_guard:"str_case"},
        {source: "\\str_case:nnT#1#2#3{\\exp:w\\__str_case:nnTF{#1}{#2}{#3}{}}", protected_flag:false, source_mapping_guard:"str_case"},
        {source: "\\str_case:nnF#1#2{\\exp:w\\__str_case:nnTF{#1}{#2}{}}", protected_flag:false, source_mapping_guard:"str_case"},
        {source: "\\str_case:nnTF#1#2{\\exp:w\\__str_case:nnTF{#1}{#2}}", protected_flag:false, source_mapping_guard:"str_case"},
        {source: "\\__str_case:nnTF#1#2#3#4{\\__str_case:nw{#1}#2{#1}{}\\s__str_mark{#3}\\s__str_mark{#4}\\s__str_stop}", protected_flag:false},
        {source: "\\__str_case:nw#1#2#3{\\str_if_eq:nnTF{#1}{#2}{\\__str_case_end:nw{#3}}{\\__str_case:nw{#1}}}", protected_flag:false},
        {source: "\\__str_case_end:nw#1#2#3\\s__str_mark#4#5\\s__str_stop{\\exp_end:#1#4}", protected_flag:false},
        {source: "\\bool_to_str:c{\\exp_args:Nc\\bool_to_str:N}", protected_flag: false},
        {source: "\\bool_if:cTF{\\exp_args:Nc\\bool_if:NTF}", protected_flag: false},
        {source: "\\bool_if:cT{\\exp_args:Nc\\bool_if:NT}", protected_flag: false},
        {source: "\\bool_if:cF{\\exp_args:Nc\\bool_if:NF}", protected_flag: false},
        {source: "\\bool_if_p:c{\\exp_args:Nc\\bool_if_p:N}", protected_flag: false},
        {source: "\\bool_set_true:c{\\exp_args:Nc\\bool_set_true:N}", protected_flag: true},
        {source: "\\bool_set_false:c{\\exp_args:Nc\\bool_set_false:N}", protected_flag: true},
        {source: "\\bool_gset_true:c{\\exp_args:Nc\\bool_gset_true:N}", protected_flag: true},
        {source: "\\bool_gset_false:c{\\exp_args:Nc\\bool_gset_false:N}", protected_flag: true},
        {source: "\\tl_if_empty:cTF{\\exp_args:Nc\\tl_if_empty:NTF}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\tl_if_empty:cT{\\exp_args:Nc\\tl_if_empty:NT}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\tl_if_empty:cF{\\exp_args:Nc\\tl_if_empty:NF}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\seq_if_empty:cTF{\\exp_args:Nc\\seq_if_empty:NTF}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\seq_if_empty:cT{\\exp_args:Nc\\seq_if_empty:NT}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\seq_if_empty:cF{\\exp_args:Nc\\seq_if_empty:NF}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\tl_use:c{\\exp_args:Nc\\tl_use:N}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\seq_use:cn{\\exp_args:Nc\\seq_use:Nn}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\seq_item:cn{\\exp_args:Nc\\seq_item:Nn}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\seq_count:c{\\exp_args:Nc\\seq_count:N}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\tl_count:c{\\exp_args:Nc\\tl_count:N}", protected_flag: false, source_mapping_guard: "dynamic_read"},
        {source: "\\tl_new:c{\\exp_args:Nc\\tl_new:N}", protected_flag: true, source_mapping_guard: "dynamic_assignment"},
        {source: "\\tl_clear:c{\\exp_args:Nc\\tl_clear:N}", protected_flag: true, source_mapping_guard: "dynamic_assignment"},
        {source: "\\tl_gclear:c{\\exp_args:Nc\\tl_gclear:N}", protected_flag: true, source_mapping_guard: "dynamic_assignment"},
        {source: "\\seq_new:c{\\exp_args:Nc\\seq_new:N}", protected_flag: true, source_mapping_guard: "dynamic_assignment"},
        {source: "\\seq_clear:c{\\exp_args:Nc\\seq_clear:N}", protected_flag: true, source_mapping_guard: "dynamic_assignment"},
        {source: "\\seq_gclear:c{\\exp_args:Nc\\seq_gclear:N}", protected_flag: true, source_mapping_guard: "dynamic_assignment"},
        {source: "\\tl_put_left:cn{\\exp_args:Nc\\tl_put_left:Nn}", protected_flag: true},
        {source: "\\tl_put_right:cn{\\exp_args:Nc\\tl_put_right:Nn}", protected_flag: true},
        {source: "\\tl_gput_left:cn{\\exp_args:Nc\\tl_gput_left:Nn}", protected_flag: true},
        {source: "\\tl_gput_right:cn{\\exp_args:Nc\\tl_gput_right:Nn}", protected_flag: true},
        {source: "\\seq_put_left:cn{\\exp_args:Nc\\seq_put_left:Nn}", protected_flag: true},
        {source: "\\seq_put_right:cn{\\exp_args:Nc\\seq_put_right:Nn}", protected_flag: true},
        {source: "\\seq_gput_left:cn{\\exp_args:Nc\\seq_gput_left:Nn}", protected_flag: true},
        {source: "\\seq_gput_right:cn{\\exp_args:Nc\\seq_gput_right:Nn}", protected_flag: true},
        {source: "\\tl_set_eq:cN{\\exp_args:Nc\\tl_set_eq:NN}", protected_flag: true},
        {source: "\\tl_set_eq:Nc{\\exp_args:NNc\\tl_set_eq:NN}", protected_flag: true},
        {source: "\\tl_set_eq:cc{\\exp_args:Ncc\\tl_set_eq:NN}", protected_flag: true},
        {source: "\\tl_gset_eq:cN{\\exp_args:Nc\\tl_gset_eq:NN}", protected_flag: true},
        {source: "\\tl_gset_eq:Nc{\\exp_args:NNc\\tl_gset_eq:NN}", protected_flag: true},
        {source: "\\tl_gset_eq:cc{\\exp_args:Ncc\\tl_gset_eq:NN}", protected_flag: true},
        {source: "\\exp_args:Nc#1#2{\\exp_after:wN#1\\cs:w#2\\cs_end:}", protected_flag: false},
        {source: "\\exp_args:NNc#1#2#3{\\exp_after:wN#1\\exp_after:wN#2\\cs:w#3\\cs_end:}", protected_flag: false},
        {source: "\\exp_args:Ncc#1#2#3{\\exp_after:wN#1\\cs:w#2\\exp_after:wN\\cs_end:\\cs:w#3\\cs_end:}", protected_flag: false},
        {source: "\\cs_if_eq:cNTF{\\exp_args:Nc\\cs_if_eq:NNTF}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:cNT{\\exp_args:Nc\\cs_if_eq:NNT}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:cNF{\\exp_args:Nc\\cs_if_eq:NNF}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq_p:cN{\\exp_args:Nc\\cs_if_eq_p:NN}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:NcTF{\\exp_args:NNc\\cs_if_eq:NNTF}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:NcT{\\exp_args:NNc\\cs_if_eq:NNT}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:NcF{\\exp_args:NNc\\cs_if_eq:NNF}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq_p:Nc{\\exp_args:NNc\\cs_if_eq_p:NN}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:ccTF{\\exp_args:Ncc\\cs_if_eq:NNTF}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:ccT{\\exp_args:Ncc\\cs_if_eq:NNT}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:ccF{\\exp_args:Ncc\\cs_if_eq:NNF}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq_p:cc{\\exp_args:Ncc\\cs_if_eq_p:NN}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:NNTF#1#2{\\if_meaning:w#1#2\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:NNT#1#2{\\if_meaning:w#1#2\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq:NNF#1#2{\\if_meaning:w#1#2\\__prg_F_true:w\\fi:\\use:n}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\cs_if_eq_p:NN#1#2{\\if_meaning:w#1#2\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false, source_mapping_guard: "cs_eq"},
        {source: "\\use_none:nnnn#1#2#3#4{}", protected_flag: false},
        {source: "\\__cs_if_free_c_aux:w\\fi:\\use_none:n#1\\if_true:{\\fi:\\exp_after:wN\\if_meaning:w\\cs:w#1\\cs_end:\\scan_stop:}", protected_flag: false},
        {source: "\\cs_if_free:NTF#1{\\if_cs_exist:N#1\\else:\\use_none:nnnn\\fi:\\if_meaning:w#1\\scan_stop:\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag: false},
        {source: "\\cs_if_free:cTF#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_free_c_aux:w\\fi:\\use_none:n{#1}\\if_true:\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag: false},
        {source: "\\cs_if_free:NT#1{\\if_cs_exist:N#1\\else:\\use_none:nnnn\\fi:\\if_meaning:w#1\\scan_stop:\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag: false},
        {source: "\\cs_if_free:cT#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_free_c_aux:w\\fi:\\use_none:n{#1}\\if_true:\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag: false},
        {source: "\\cs_if_free:NF#1{\\if_cs_exist:N#1\\else:\\use_none:nnnn\\fi:\\if_meaning:w#1\\scan_stop:\\__prg_F_true:w\\fi:\\use:n}", protected_flag: false},
        {source: "\\cs_if_free:cF#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_free_c_aux:w\\fi:\\use_none:n{#1}\\if_true:\\__prg_F_true:w\\fi:\\use:n}", protected_flag: false},
        {source: "\\cs_if_free_p:N#1{\\if_cs_exist:N#1\\else:\\use_none:nnnn\\fi:\\if_meaning:w#1\\scan_stop:\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\cs_if_free_p:c#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_free_c_aux:w\\fi:\\use_none:n{#1}\\if_true:\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\__cs_if_exist_c_aux:w\\fi:\\use_none:n#1\\if_false:{\\fi:\\exp_after:wN\\if_meaning:w\\cs:w#1\\cs_end:\\scan_stop:\\else:}", protected_flag: false},
        {source: "\\cs_if_exist:cTF#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_exist_c_aux:w\\fi:\\use_none:n{#1}\\if_false:\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag: false},
        {source: "\\cs_if_exist:cT#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_exist_c_aux:w\\fi:\\use_none:n{#1}\\if_false:\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag: false},
        {source: "\\cs_if_exist:cF#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_exist_c_aux:w\\fi:\\use_none:n{#1}\\if_false:\\__prg_F_true:w\\fi:\\use:n}", protected_flag: false},
        {source: "\\cs_if_exist_p:c#1{\\if_cs_exist:w#1\\cs_end:\\__cs_if_exist_c_aux:w\\fi:\\use_none:n{#1}\\if_false:\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\use_i:nnnn#1#2#3#4{#1}", protected_flag: false},
        {source: "\\__prg_TF_true:w\\fi:\\use_ii:nn{\\fi:\\use_i:nn}", protected_flag: false},
        {source: "\\__prg_T_true:w\\fi:\\use_none:n{\\fi:\\use:n}", protected_flag: false},
        {source: "\\__prg_F_true:w\\fi:\\use:n{\\fi:\\use_none:n}", protected_flag: false},
        {source: "\\__prg_p_true:w\\fi:\\c_false_bool{\\fi:\\c_true_bool}", protected_flag: false},
        {source: "\\cs_if_exist:NTF#1{\\if_meaning:w#1\\scan_stop:\\use_i:nnnn\\else:\\fi:\\if_cs_exist:N#1\\__prg_TF_true:w\\fi:\\use_ii:nn}", protected_flag: false},
        {source: "\\cs_if_exist:NT#1{\\if_meaning:w#1\\scan_stop:\\use_i:nnnn\\else:\\fi:\\if_cs_exist:N#1\\__prg_T_true:w\\fi:\\use_none:n}", protected_flag: false},
        {source: "\\cs_if_exist:NF#1{\\if_meaning:w#1\\scan_stop:\\use_i:nnnn\\else:\\fi:\\if_cs_exist:N#1\\__prg_F_true:w\\fi:\\use:n}", protected_flag: false},
        {source: "\\cs_if_exist_p:N#1{\\if_meaning:w#1\\scan_stop:\\use_i:nnnn\\else:\\fi:\\if_cs_exist:N#1\\__prg_p_true:w\\fi:\\c_false_bool}", protected_flag: false},
        {source: "\\exp_end_continue_f:w{`^^@}", protected_flag: true},
        {source: "\\exp_not:f#1{\\__kernel_exp_not:w\\exp_after:wN{\\exp:w\\exp_end_continue_f:w#1}}", protected_flag: false},
        {source: "\\__seq_put_left_aux:w\\s__seq{\\exp_stop_f:}", protected_flag: false},
        {source: "\\seq_put_left:Nn#1#2{\\__kernel_tl_set:Nx#1{\\exp_not:n{\\s__seq\\__seq_item:n{#2}}\\exp_not:f{\\exp_after:wN\\__seq_put_left_aux:w#1}}}", protected_flag: true},
        {source: "\\seq_gput_left:Nn#1#2{\\__kernel_tl_gset:Nx#1{\\exp_not:n{\\s__seq\\__seq_item:n{#2}}\\exp_not:f{\\exp_after:wN\\__seq_put_left_aux:w#1}}}", protected_flag: true},
        {source: "\\tl_set_eq:NN#1#2{\\tex_let:D#1=~#2}", protected_flag: true},
        {source: "\\tl_gset_eq:NN#1#2{\\tex_global:D\\tex_let:D#1=~#2}", protected_flag: true},
        {source: "\\seq_set_eq:NN#1#2{\\tex_let:D#1=~#2}", protected_flag: true},
        {source: "\\seq_gset_eq:NN#1#2{\\tex_global:D\\tex_let:D#1=~#2}", protected_flag: true},
        {source: "\\tl_put_left:Nn#1#2{\\__kernel_tl_set:Nx#1{\\__kernel_exp_not:w{#2}\\__kernel_exp_not:w\\exp_after:wN{#1}}}", protected_flag: true},
        {source: "\\tl_gput_left:Nn#1#2{\\__kernel_tl_gset:Nx#1{\\__kernel_exp_not:w{#2}\\__kernel_exp_not:w\\exp_after:wN{#1}}}", protected_flag: true},
        {source: "\\use:n#1{#1}", protected_flag: false},
        {source: "\\use_none:n#1{}", protected_flag: false},
        {source: "\\use_i:nn#1#2{#1}", protected_flag: false},
        {source: "\\use_ii:nn#1#2{#2}", protected_flag: false},
        {source: "\\prg_return_true:{\\exp_after:wN\\use_i:nn\\exp:w}", protected_flag: false},
        {source: "\\prg_return_false:{\\exp_after:wN\\use_ii:nn\\exp:w}", protected_flag: false},
        {source: "\\prg_break_point:{}", protected_flag: false},
        {source: "\\prg_break:n#1#2\\prg_break_point:{#1}", protected_flag: false},
        {source: "\\__seq_if_in:{\\prg_break:n{\\group_end:\\prg_return_true:}}", protected_flag: false},
        {source: "\\__seq_item:n{\\msg_expandable_error:nn{seq}{misused}\\use_none:n}", protected_flag: false},
        {source: "\\seq_if_in:NnTF#1#2{\\group_begin:\\tl_set:Nn\\l__seq_internal_a_tl{#2}\\cs_set_protected:Npn\\__seq_item:n##1{\\tl_set:Nn\\l__seq_internal_b_tl{##1}\\if_meaning:w\\l__seq_internal_a_tl\\l__seq_internal_b_tl\\exp_after:wN\\__seq_if_in:\\fi:}#1\\group_end:\\prg_return_false:\\prg_break_point:\\exp_end:}", protected_flag: true},
        {source: "\\seq_if_in:NnT#1#2{\\group_begin:\\tl_set:Nn\\l__seq_internal_a_tl{#2}\\cs_set_protected:Npn\\__seq_item:n##1{\\tl_set:Nn\\l__seq_internal_b_tl{##1}\\if_meaning:w\\l__seq_internal_a_tl\\l__seq_internal_b_tl\\exp_after:wN\\__seq_if_in:\\fi:}#1\\group_end:\\prg_return_false:\\prg_break_point:\\exp_end:\\use:n\\use_none:n}", protected_flag: true},
        {source: "\\seq_if_in:NnF#1#2{\\group_begin:\\tl_set:Nn\\l__seq_internal_a_tl{#2}\\cs_set_protected:Npn\\__seq_item:n##1{\\tl_set:Nn\\l__seq_internal_b_tl{##1}\\if_meaning:w\\l__seq_internal_a_tl\\l__seq_internal_b_tl\\exp_after:wN\\__seq_if_in:\\fi:}#1\\group_end:\\prg_return_false:\\prg_break_point:\\exp_end:{}}", protected_flag: true}])^;
    // Native substitutes this delimiter macro into the error body before tokenization.
    let question_macro={*:latex_macro(0,false,false,false,[],[])^,prefix:text_tokens("?")^};
    let with_question=if(msg_support.definitions["control:???"]!=null)macros else
        assign_meaning(macros,"control:???",question_macro,true)^;
    let msg_macro=with_question.definitions["control:__msg_expandable_error:nn"];
    // Substitution retains a literal space after the special control sequence.
    let msg_body=if(msg_support.definitions["control:__msg_expandable_error:nn"]!=null) [] else
        [for(t in msg_macro.body) if(t.kind=="control" and t.value=="???") [t,{kind:"char",value:" ",cat:10,start:-1,end:-1}] else [t]];
    let with_message=if(msg_support.definitions["control:__msg_expandable_error:nn"]!=null) with_question else
        assign_meaning(with_question,"control:__msg_expandable_error:nn",{*:msg_macro,body:[for(part in msg_body, t in part) t]},true)^;
    let prefix=with_message.definitions["control:c__msg_text_prefix_tl"];
    let with_prefix=if(msg_support.definitions["control:c__msg_text_prefix_tl"]!=null)with_message else
        assign_meaning(with_message,"control:c__msg_text_prefix_tl",{*:prefix,long_ok:false},true)^;
    let more=with_prefix.definitions["control:c__msg_more_text_prefix_tl"];
    let with_more=if(msg_support.definitions["control:c__msg_more_text_prefix_tl"]!=null)with_prefix else
        assign_meaning(with_prefix,"control:c__msg_more_text_prefix_tl",{*:more,long_ok:false},true)^;
    let existence_aliases = [for (family in ["tl", "seq", "int", "bool", "clist", "prop", "str", "dim"],
        suffix in [":NTF", ":NT", ":NF", "_p:N", ":cTF", ":cT", ":cF", "_p:c"])
            {name: family ++ "_if_exist" ++ suffix, kind: "alias", value: "cs_if_exist" ++ suffix}];
    let early_existence=enable_expl_values(with_more,existence_aliases)^;
    let with_iow=enable_expl_values(early_existence,[{name:"iow_char:N",kind:"alias",value:"cs_to_str:N"}])^;
    let prefix_scan=csname_tokens({*:with_iow,input:stream.push(with_iow.input,[control_token("c__msg_text_prefix_tl"),control_token("endcsname")])},20000000)^;
    let more_scan=if(prefix_scan.state.issue!=null) prefix_scan else
        csname_tokens({*:prefix_scan.state,input:stream.push(prefix_scan.state.input,[control_token("c__msg_more_text_prefix_tl"),control_token("endcsname")])},20000000)^;
    let with_catalog=if(more_scan.state.issue!=null)more_scan.state else
        enable_msg_catalog(more_scan.state,message_catalog.prepared(),message_catalog.dynamic(),prefix_scan.name,more_scan.name)^;
    let string_macro = if (with_catalog.definitions["control:bool_to_str:N"] != null) with_catalog else
        assign_meaning(with_catalog, "control:bool_to_str:N", expl3_bool_to_str_macro()^, true)^;
    let sequence_eq_aliases = [for (scope in ["set", "gset"], pattern in ["cN", "Nc", "cc"])
        {name: "seq_" ++ scope ++ "_eq:" ++ pattern, kind: "alias", value: "tl_" ++ scope ++ "_eq:" ++ pattern}];
    let with_sequence_aliases = enable_expl_values(string_macro, sequence_eq_aliases)^;
    let pop_aliases=[for(scope in ["","g"],arg in ["NN","cN"],suffix in ["","TF","T","F"])
        {name:"seq_" ++ scope ++ "pop:" ++ arg ++ suffix,kind:"alias",value:"seq_" ++ scope ++ "pop_left:" ++ arg ++ suffix}];
    let with_pop_aliases=enable_expl_values(with_sequence_aliases,pop_aliases)^;
    let boolean_eq_aliases = [for (scope in ["set", "gset"], pattern in ["NN", "cN", "Nc", "cc"])
        {name: "bool_" ++ scope ++ "_eq:" ++ pattern, kind: "alias", value: "tl_" ++ scope ++ "_eq:" ++ pattern}];
    let ready = enable_expl_values(with_pop_aliases, boolean_eq_aliases)^;
    // Expl3 uses opposite-category NUL tokens to balance alignment-safe expansion.
    let nul_end = {kind:"char", value:chr(0), cat:2, start:-1, end:-1};
    let nul_begin = {*:nul_end, cat:1};
    let backtick = {kind:"char", value:"`", cat:12, start:-1, end:-1};
    let begin = latex_macro(0, false, false, false, [], [control_token("exp:w"), control_token("if_false:"),
        brace_token(true), control_token("fi:"), backtick, nul_end, control_token("exp_stop_f:")])^;
    let ending = latex_macro(0, false, false, false, [], [control_token("exp:w"), backtick, nul_begin,
        control_token("if_false:"), brace_token(false), control_token("fi:"), control_token("exp_stop_f:")])^;
    let with_begin = if (ready.definitions["control:group_align_safe_begin:"] != null) ready else
        assign_meaning(ready, "control:group_align_safe_begin:", begin, true)^;
    if (with_begin.definitions["control:group_align_safe_end:"] != null) with_begin else
        assign_meaning(with_begin, "control:group_align_safe_end:", ending, true)
}

// tokenize trusted bundled TeX definitions under expl3 category codes.
fn expl3_definition_tokens(reader_state, output = []) any^ {
    let next = reader.next(reader_state)^;
    if (next == null) output
    else expl3_definition_tokens(next.reader, [*output, {*:next.token, direct: false}])
}

fn enable_msg_catalog(st: State, prepared, dynamic, prefix, more_prefix) State^ {
    let items=[for(entry in prepared)
        *["control:" ++ prefix ++ entry.module ++ "/" ++ entry.name,entry.text,
          "control:" ++ more_prefix ++ entry.module ++ "/" ++ entry.name,entry.extra]];
    let defaults=map(items);
    // Caller-supplied definitions retain precedence over the standard catalog.
    let merged={*:defaults,*:st.definitions};
    enable_dynamic_messages({*:st,definitions:merged},dynamic,prefix,more_prefix)
}

fn enable_dynamic_messages(st: State, records, prefix, more_prefix, index = 0) State^ {
    if (index >= len(records) or st.issue != null) st
    else {
        let entry=records[index];
        let suffix=entry.module ++ "/" ++ entry.name;
        let with_text=enable_msg_definition(st,prefix ++ suffix,entry.body,entry.preexpand)^;
        let with_more=if(with_text.issue!=null)with_text else enable_msg_definition(with_text,more_prefix ++ suffix,entry.extra,entry.preexpand)^;
        enable_dynamic_messages(with_more,records,prefix,more_prefix,index+1)
    }
}

fn enable_msg_definition(st: State, name, body, preexpand, replace = false) State^ {
    if(not replace and st.definitions["control:" ++ name]!=null) st
    else {
        let source="\\__msg_catalog_entry#1#2#3#4{" ++ body ++ "}";
        let source_reader={*:reader.reader(source,-1),endline:-1,
            catcodes:expl3.syntax_codes({'{':1,'}':2,'#':6,[chr(0)]:13})};
        let raw=expl3_definition_tokens(source_reader)^;
        // Native's enclosing use:e expands these bodies in the current format state.
        let expanded=if(preexpand) definition_tokens({*:st,input:stream.push(st.input,[*slice(raw,10,len(raw)-1)^,brace_token(false)])},20000000)^
            else {state:st,tokens:slice(raw,10,len(raw)-1)^};
        let parsed=[*slice(raw,0,10)^,*expanded.tokens,brace_token(false)];
        let tokens=[control_token(name),*slice(parsed,1,len(parsed))^];
        let ready=expanded.state;
        if(ready.issue!=null)ready else define({*:ready,input:stream.push(ready.input,tokens)},true,true,false,false,20000000)
    }
}

// The catalog generator uses the same TeX definition scanner as runtime bootstrap.
pub fn compile_message_templates(st: State, records) any^ => compile_message_templates_inner(st,records)
fn compile_message_templates_inner(st: State, records, index = 0, output = []) any^ {
    if(st.issue!=null) {issue:st.issue,templates:output}
    else if(index>=len(records)) {issue:null,templates:output}
    else {
        let entry=records[index];
        if(entry.preexpand) compile_message_templates_inner(st,records,index+1,output)
        else {
            let text_state=enable_msg_definition(st,"__catalog_compiler",entry.body,false,true)^;
            let text=text_state.definitions["control:__catalog_compiler"];
            let extra_state=if(text_state.issue!=null)text_state else
                enable_msg_definition(text_state,"__catalog_compiler",entry.extra,false,true)^;
            let extra=extra_state.definitions["control:__catalog_compiler"];
            let next=[*output,{module:entry.module,name:entry.name,text:text,extra:extra}];
            compile_message_templates_inner(extra_state,records,index+1,next)
        }
    }
}


fn msg_module_pair(entry) any^ {
    let value_reader={*:reader.reader(entry.value,-1),endline:-1,
        catcodes:expl3.syntax_codes({'{':1,'}':2,'#':6,[chr(0)]:13})};
    let value=expl3_definition_tokens(value_reader)^;
    [control_token("__prop_pair:wn"),*text_tokens(entry.name)^,control_token("s__prop"),
        brace_token(true),*value,brace_token(false)]
}
fn msg_module_property(entries) any^ => expl3.token_list([control_token("s__prop"),control_token("__prop_chk:w"),
    *[for(entry in entries, t in msg_module_pair(entry)^) t]])

fn enable_expl_macro_sources(st: State, definitions, index = 0) State^ {
    if (index >= len(definitions)) st
    else {
        let definition = definitions[index];
        let source_reader = {*:reader.reader(definition.source, -1), endline: -1,
            catcodes: expl3.syntax_codes({'{': 1, '}': 2, '&': 4, '#': 6, [chr(0)]: 13, '?': if (definition.question_letters == true) 11 else 12})};
        let scanned = expl3_definition_tokens(source_reader)^;
        // Some expl3 names contain operator characters and are normally made with csname.
        let tokens = if (definition.install_as is string)
            [{*:scanned[0], value:definition.install_as}, *slice(scanned, 1, len(scanned))^] else scanned;
        let name = key(tokens[0]);
        let created = if (st.definitions[name] != null) st else
            define({*:st, input: stream.push(st.input, tokens)}, true, definition.long_ok != false,
                definition.protected_flag, false, 20000000)^;
        let next = if (st.definitions[name] == null and created.issue == null and
            (definition.source_mapping_guard != null or definition.argument_unqualified == true))
                assign_meaning(created, name, {*:created.definitions[name],
                    source_mapping_guard: definition.source_mapping_guard,
                    expl3_argument_unqualified:definition.argument_unqualified == true}, true)^ else created;
        if (next.issue != null) next else enable_expl_macro_sources(next, definitions, index + 1)
    }
}

// expl3 string results contain other-category letters, even under expl syntax.
fn expl3_bool_to_str_macro() any^ {
    {kind: "macro", count: 1, prefix: [], delimiters: [[]], brace_delim: false,
        long_ok: true, protected_flag: false, body: [control_token("bool_if:NTF"), parameter_token(0),
            brace_token(true), *text_tokens("true")^, brace_token(false),
            brace_token(true), *text_tokens("false")^, brace_token(false)]}
}

// native tl insertion expands to an edef whose body preserves both token lists.
fn expl3_tl_put_right_macro(global) any^ {
    let parameter0 = parameter_token(0);
    let parameter1 = parameter_token(1);
    {kind: "macro", count: 2, prefix: [], delimiters: [[], []], brace_delim: false,
        long_ok: true, protected_flag: true, expl3_argument_unqualified: true, expl3_argument_handler: "expand",
        body: [control_token(if (global) "__kernel_tl_gset:Nx" else "__kernel_tl_set:Nx"), parameter0,
            brace_token(true), control_token("__kernel_exp_not:w"), control_token("exp_after:wN"),
            brace_token(true), parameter0, parameter1, brace_token(false), brace_token(false)]}
}

// the protected wrapper captures its arguments before the underlying assignment.
fn expl3_seq_put_right_macro(global) any^ {
    let parameter0 = parameter_token(0);
    let parameter1 = parameter_token(1);
    {kind: "macro", count: 2, prefix: [], delimiters: [[], []], brace_delim: false,
        long_ok: true, protected_flag: true, expl3_argument_unqualified: true,
        body: [control_token(if (global) "tl_gput_right:Nn" else "tl_put_right:Nn"), parameter0,
            brace_token(true), control_token("__seq_item:n"), brace_token(true), parameter1,
            brace_token(false), brace_token(false)]}
}

// initialize non-allocating constants and aliases with their TeX meanings.
fn enable_expl_values(st: State, values, index = 0) State^ {
    if (index >= len(values)) st
    else {
        let value = values[index];
        let name = "control:" ++ value.name;
        let meaning = if (value.kind == "alias") st.definitions["control:" ++ value.value]
            else if (value.kind == "primitive")
                {kind: "primitive", name: value.value, source_mapping_guard: value.source_mapping_guard}
            else if (value.kind == "char") {kind: "char", value: value.value, cat: value.cat}
            else {kind: value.kind, value: value.value};
        let next = if (st.definitions[name] != null) st else assign_meaning(st, name, meaning, true)^;
        enable_expl_values(next, values, index + 1)
    }
}

// expl3 declares these standard scratch values at initialization.
fn enable_expl_scratch(st: State, names, value, index = 0) State^ {
    if (index >= len(names)) st
    else {
        let name = "control:" ++ names[index];
        let next = if (st.definitions[name] != null) st else assign_meaning(st, name, value, true)^;
        enable_expl_scratch(next, names, value, index + 1)
    }
}

fn enable_expl_counts(st: State, names, index = 0) State^ {
    if (index >= len(names)) st
    else {
        let name = "control:" ++ names[index];
        let next = if (st.definitions[name] != null) st else {
            let allocated = latex_allocate_register(st, 10)^;
            assign_meaning(allocated.state, name, {kind:"register", index:allocated.index}, true)^
        };
        enable_expl_counts(next, names, index + 1)
    }
}

fn enable_expl_primitives(st: State, names, index = 0) State^ {
    if (index >= len(names)) st
    else {
        let name = names[index];
        let next = assign_meaning(st, "control:" ++ name, {kind: "primitive", name: name}, true)^;
        enable_expl_primitives(next, names, index + 1)
    }
}

// Continue from a completed preload while retaining its definitions and scoped values.
pub fn resume(st: State, source: string, name: string = "<input>", budget: int = 20000000,
        origin: string = "local") State^ {
    if (st.issue != null) st
    else if (len(st.groups) != 0 or len(st.conditions) != 0) fail(st, "tex-incomplete-preload")
    else {
        let id = next_source_id(st)^;
        // Nested inputs inherit the authority of the resumed source.
        let next_reader = {*:reader.reader(source, id), origin: origin,
            catcodes: st.input.reader.catcodes, endline: parameter_value(st, "endlinechar")};
        let next_input = {*:stream.from_reader(next_reader), everyeof: st.input.everyeof};
        let resumed = {*:st, input: next_input, finished: false, output: [], messages: [], terminal_line: "", expansions: 0,
            main_file: id, echo_incomplete: false, prefixed_macro_seen: false, prefix_scan: false,
            mathchardef_emission_seen: false,
            expl3_tl_head_seen: false, expl3_tl_tail_seen: false, expl3_keyval_seen: false, expl3_keys_seen:false,
            expl3_clist_map_seen:false, expl3_bool_expr_seen:false, expl3_tl_replace_seen:false, expl3_str_case_seen:false,
            expl3_seq_map_seen:false, expl3_tl_map_seen:false, expl3_replicate_seen:false,
            expl3_exp_args_once_seen: false, expl3_exp_args_value_seen: false, expl3_exp_args_f_seen: false,
            sources: [*st.sources, {id: id, name: name, origin: origin, data: source}]};
        run(resumed, budget)
    }
}
