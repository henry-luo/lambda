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
fn expand_once(source, configured, budget, preload = null) any^ {
    let cached = if (preload is map) if (configured.script_expl3 == true) preload.expl3 else preload.ordinary else null;
    if (configured.ini == true)
        expansion.expand(stream.from_reader(reader.reader(source)), budget, configured)
    else if (cached is map and configured.kernel_source is string) {
        // bind this attempt's captures when resuming the resource-free kernel.
        let bound = {*:cached, resources:configured.resources, resource_requests:configured.resource_requests == true};
        profile.expand_with_kernel(source, configured.kernel_source, configured, budget, bound)
    }
    else profile.expand(source, configured, budget)
}

fn expand(source, options, budget, echo_definitions = true, preload = null) any^ {
    let configured = {*:expansion.options_map(options), echo_definitions:echo_definitions, auto_expl3:true};
    // Ask the host for the expl3 snapshot only when this document needs it.
    if (configured.script_expl3 == true and preload is map and preload.request_expl3 == true)
        failure("tex-expl3-preload-needed")
    else {
        let first = expand_once(source, configured, budget, preload)^;
        if (first.issue == "tex-expl3-restart")
            if (preload is map and preload.request_expl3 == true) failure("tex-expl3-preload-needed")
            else expand_once(source, {*:configured, script_expl3:true}, budget, preload)
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
fn reusable_preload(loaded) bool^ => loaded is map and loaded.issue == null and len(loaded.resources_seen) == 0

fn host_preload_options(options: map, math: bool) map =>
    // a lookup during preload must request a resource even if a caller supplied it.
    {*:options, *:if (math) {math_mode:true} else {}, echo_definitions:not math, auto_expl3:true,
        resources:{local:{},bundled:{}}, resource_requests:true}

pub fn host_kernel_preload(options: map, math: bool) any^ {
    if (options.ini == true or not (options.kernel_source is string)) null
    else {
        let configured = host_preload_options(options, math);
        let ordinary = profile.preload_kernel(options.kernel_source, configured)^;
        if (not reusable_preload(ordinary)^) null
        else {ordinary:ordinary,expl3:null,request_expl3:true}
    }
}

pub fn host_expl3_preload(options: map, math: bool, preload: map) map^ {
    let expl3 = profile.preload_expl3_kernel(options.kernel_source, host_preload_options(options, math)) ^ { null };
    {*:preload, expl3:if (reusable_preload(expl3)^) expl3 else null, request_expl3:false}
}

// Native TeX returns diagnostics and local package names alongside expanded text.
pub fn tex(source: string, options = null, budget: int = 20000000, preload = null) any^ {
    let expanded = expand(source, document_options(options), budget, true, preload)^;
    if (expanded.issue != null) failure(expanded.issue, expanded.unsupported, expanded.resource_request)
    else if (not complete_diagnostics(expanded)^)
        failure("tex-diagnostic-unsupported")
    else {
        let built = reconstruct.reconstruct(expanded)^;
        if (built.issue != null) failure(built.issue)
        else {root:{text:built.text, messages:expanded.messages,
            diagnostics:expanded.diagnostic_records,
            packages:[for (entry in expanded.packages where entry.kind == "local") entry.name]},
            issue:null, unsupported:null}
    }
}

// Definition spans are required by the source-aware LaTeX digester.
pub fn latex(source: string, options = null, budget: int = 20000000, preload = null) any^ {
    let expanded = expand(source, document_options(options), budget, true, preload)^;
    if (expanded.issue != null) failure(expanded.issue, expanded.unsupported, expanded.resource_request)
    else if (not complete_diagnostics(expanded)^) failure("tex-diagnostic-unsupported")
    else {
        let parsed = digest.digest(expanded)^;
        {root:parsed.ast, issue:parsed.issue, unsupported:parsed.unsupported}
    }
}

// Standalone math uses the same expanded token stream and atom digester as documents.
fn math_using(source: string, options, budget, preload) any^ {
    if (len(source) == 0) {root:null, issue:null, unsupported:null}
    else {
        let configured = {*:expansion.options_map(options), math_mode:true};
        let expanded = expand(source, configured, budget, false, preload)^;
        if (expanded.issue != null) failure(expanded.issue, expanded.unsupported, expanded.resource_request)
        else if (not complete_diagnostics(expanded)^) failure("tex-diagnostic-unsupported")
        else {
            let parsed = digest.math(expanded)^;
            {root:parsed.ast, issue:parsed.issue, unsupported:parsed.unsupported}
        }
    }
}

pub fn math(source: string, options = null, budget: int = 20000000) any^ =>
    math_using(source, options, budget, null)

// The host supplies a preload only after matching its complete configuration.
pub fn host_math(source: string, options: map, preload) any^ =>
    math_using(source, options, 20000000, preload)
