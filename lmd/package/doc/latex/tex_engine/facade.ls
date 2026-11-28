// Script-owned parse boundary for qualifying the native-engine migration.
import profile: .profile
import expansion: .expansion
import reader: .reader
import stream: .stream
import reconstruct: .reconstruct
import digest: .digest

fn failure(issue, unsupported = null, request = null) =>
    {root:null, issue:issue, unsupported:unsupported, request:request}

// Both public results retain source definitions in the expanded stream.
fn expand_once(source, configured, budget, preload = null, capture_checkpoint: bool = false) any^ {
    let cached = if (preload is map) if (configured.script_expl3 == true) preload.expl3 else preload.ordinary else null;
    if (configured.ini == true)
        expansion.expand(stream.from_reader(reader.reader(source)), budget, configured, capture_checkpoint)
    else if (cached is map and configured.kernel_source is string) {
        // bind this attempt's captures when resuming the resource-free kernel.
        let bound = {*:cached, resources:configured.resources, resource_requests:configured.resource_requests == true};
        profile.expand_with_kernel(source, configured.kernel_source, configured, budget, bound, capture_checkpoint)
    }
    else profile.expand(source, configured, budget, capture_checkpoint)
}

fn expand(source, options, budget, echo_definitions = true, preload = null, checkpoint = null,
        capture_checkpoint: bool = false) any^ {
    let configured = {*:expansion.options_map(options), echo_definitions:echo_definitions, auto_expl3:true};
    // Ask the host for the expl3 snapshot only when this document needs it.
    if (configured.script_expl3 == true and preload is map and preload.request_expl3 == true)
        failure("tex-expl3-preload-needed")
    else {
        let first = if (checkpoint is map)
            profile.continue_document(checkpoint.state, configured.resources, checkpoint.messages, budget)^
            else expand_once(source, configured, budget, preload, capture_checkpoint)^;
        if (first.issue == "tex-expl3-restart")
            if (preload is map and preload.request_expl3 == true) failure("tex-expl3-preload-needed")
            else expand_once(source, {*:configured, script_expl3:true}, budget, preload, capture_checkpoint)
        else first
    }
}

fn complete_diagnostics(expanded) bool^ => len(expanded.diagnostics) == len(expanded.diagnostic_records)

// Native document constructors scan their arguments without executing assignments.
fn document_options(options) map {
    let given = expansion.options_map(options);
    {*:given, constructor_argument_mode:
        if (given.constructor_argument_mode == null) true else given.constructor_argument_mode}
}

// Host captures are explicit values; the engine itself never opens a path.
pub fn host_options(options, base = null, kernel = null, captured = false, document = false) map {
    let given = if (document) document_options(options) else expansion.options_map(options);
    // An archive member is an already captured value and carries no external file grants.
    let resources = if (captured) {} else if (given.resources is map) given.resources else {};
    {*:given, kernel_source:kernel, host_no_base:base == null,
        source_name:if (given.source_name != null) given.source_name else base,
        base_path:if (given.base_path != null) given.base_path else base,
        resource_requests:not captured, resources:{*:resources,
        local:if (resources.local is map) resources.local else {},
        bundled:if (resources.bundled is map) resources.bundled else {}}}
}

pub fn host_capture(options: map, scope: string, name: string, data, no_base = false) map^ {
    let entry = if (no_base) {no_base:true} else if (data == null) {missing:true} else {data:data};
    let captured = {*:options.resources[scope], *:map([name, entry])};
    {*:options, resources:{*:options.resources, *:map([scope, captured])}}
}

// Only resource-free preloads can be reused without retaining a file grant.
fn reusable_preload(loaded) bool^ => loaded is map and loaded.issue == null and loaded.resource_lookups == 0

fn host_preload_options(options: map, math: bool) map =>
    // a lookup during preload must request a resource even if a caller supplied it.
    {*:options, *:if (math) {math_mode:true} else {}, echo_definitions:not math, auto_expl3:true,
        resources:{local:{},bundled:{}}, resource_requests:true}

pub fn host_kernel_preload(options: map, math: bool) any^ {
    if (options.ini == true or not (options.kernel_source is string)) null
    else {
        let configured = host_preload_options(options, math);
        let ordinary = profile.preload_kernel(options.kernel_source, configured, 20000000, false)^;
        if (not reusable_preload(ordinary)^) null
        else {ordinary:ordinary,expl3:null,request_expl3:true}
    }
}

pub fn host_expl3_preload(options: map, math: bool, preload: map) map^ {
    let expl3 = profile.preload_expl3_kernel(options.kernel_source, host_preload_options(options, math), 20000000, false) ^ { null };
    {*:preload, expl3:if (reusable_preload(expl3)^) expl3 else null, request_expl3:false}
}

fn parsed_result(expanded, format) any^ {
    if (expanded.issue != null) {
        let halted = failure(expanded.issue, expanded.unsupported, expanded.resource_request);
        if (expanded.resource_checkpoint is map)
            {*:halted, checkpoint:{state:expanded.resource_checkpoint, messages:expanded.kernel_messages or []}}
        else halted
    }
    else if (not complete_diagnostics(expanded)^) failure("tex-diagnostic-unsupported")
    else if (format == "tex") {
        // Native TeX returns diagnostics and local package names alongside expanded text.
        let built = reconstruct.reconstruct(expanded)^;
        if (built.issue != null) failure(built.issue)
        else {root:{text:built.text, messages:expanded.messages,
            diagnostics:expanded.diagnostic_records,
            packages:[for (entry in expanded.packages where entry.kind == "local") entry.name]},
            issue:null, unsupported:null}
    } else {
        let parsed = if (format == "math") digest.math(expanded)^ else digest.digest(expanded)^;
        {root:parsed.ast, issue:parsed.issue, unsupported:parsed.unsupported}
    }
}

fn parse_using(source: string, options, format: string, budget: int, preload, checkpoint = null,
        capture_checkpoint: bool = false) any^ {
    let math = format == "math";
    if (math and len(source) == 0) {root:null, issue:null, unsupported:null}
    else {
        let options = if (math) {*:expansion.options_map(options), math_mode:true} else document_options(options);
        parsed_result(expand(source, options, budget, not math, preload, checkpoint, capture_checkpoint)^, format)
    }
}

pub fn tex(source: string, options = null, budget: int = 20000000, preload = null) any^ =>
    parse_using(source, options, "tex", budget, preload)

pub fn latex(source: string, options = null, budget: int = 20000000, preload = null) any^ =>
    parse_using(source, options, "latex", budget, preload)

pub fn math(source: string, options = null, budget: int = 20000000) any^ =>
    parse_using(source, options, "math", budget, null)

// Host checkpoints consume a capture only with the parse that requested it.
pub fn host_parse(source: string, options: map, format: string, preload, checkpoint) any^ =>
    parse_using(source, options, format, 20000000, preload, checkpoint, true)
