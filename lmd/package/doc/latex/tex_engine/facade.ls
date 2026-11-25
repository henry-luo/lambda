// Script-owned parse boundary for qualifying the native-engine migration.
import profile: .profile
import expansion: .expansion
import reader: .reader
import stream: .stream
import reconstruct: .reconstruct
import digest: .digest

fn failure(issue, unsupported = null) => {root:null, issue:issue, unsupported:unsupported}

// Both public results retain source definitions in the expanded stream.
fn expand(source, options, budget) any^ {
    let configured = {*:expansion.options_map(options), echo_definitions:true};
    if (configured.ini == true)
        expansion.expand(stream.from_reader(reader.reader(source)), budget, configured)
    else profile.expand(source, configured, budget)
}

fn complete_diagnostics(expanded) bool^ => len(expanded.diagnostics) == len(expanded.diagnostic_records)

// Native TeX returns diagnostics and local package names alongside expanded text.
pub fn tex(source: string, options = null, budget: int = 20000000) any^ {
    let expanded = expand(source, options, budget)^;
    if (expanded.issue != null) failure(expanded.issue, expanded.unsupported)
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
    if (expanded.issue != null) failure(expanded.issue, expanded.unsupported)
    else if (not complete_diagnostics(expanded)^) failure("tex-diagnostic-unsupported")
    else {
        let parsed = digest.digest(expanded)^;
        {root:parsed.ast, issue:parsed.issue, unsupported:parsed.unsupported}
    }
}
