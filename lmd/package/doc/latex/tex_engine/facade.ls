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
fn expand_once(source, configured, budget) any^ {
    if (configured.ini == true)
        expansion.expand(stream.from_reader(reader.reader(source)), budget, configured)
    else profile.expand(source, configured, budget)
}

fn expand(source, options, budget) any^ {
    let configured = {*:expansion.options_map(options), echo_definitions:true, auto_expl3:true};
    let first = expand_once(source, configured, budget)^;
    if (first.issue == "tex-expl3-restart")
        expand_once(source, {*:configured, script_expl3:true}, budget)
    else first
}

fn complete_diagnostics(expanded) bool^ => len(expanded.diagnostics) == len(expanded.diagnostic_records)

// Host captures are explicit values; the engine itself never opens a path.
pub fn host_options(options, base = null, kernel = null) map {
    let given = expansion.options_map(options);
    let resources = if (given.resources is map) given.resources else {};
    {*:given, kernel_source:kernel, host_no_base:base == null,
        source_name:if (given.source_name != null) given.source_name else base,
        base_path:if (given.base_path != null) given.base_path else base,
        resource_requests:true, resources:{*:resources,
        local:if (resources.local is map) resources.local else {},
        bundled:if (resources.bundled is map) resources.bundled else {}}}
}

pub fn host_capture(options: map, scope: string, name: string, data, no_base = false) map^ {
    let entry = if (no_base) {no_base:true} else if (data == null) {missing:true} else {data:data};
    let captured = {*:options.resources[scope], *:map([name, entry])};
    {*:options, resources:{*:options.resources, *:map([scope, captured])}}
}

// Native TeX returns diagnostics and local package names alongside expanded text.
pub fn tex(source: string, options = null, budget: int = 20000000) any^ {
    let expanded = expand(source, options, budget)^;
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
pub fn latex(source: string, options = null, budget: int = 20000000) any^ {
    let expanded = expand(source, options, budget)^;
    if (expanded.issue != null) failure(expanded.issue, expanded.unsupported, expanded.resource_request)
    else if (not complete_diagnostics(expanded)^) failure("tex-diagnostic-unsupported")
    else {
        let parsed = digest.digest(expanded)^;
        {root:parsed.ast, issue:parsed.issue, unsupported:parsed.unsupported}
    }
}
