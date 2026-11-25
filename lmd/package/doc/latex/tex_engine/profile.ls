// LaTeX profile with a kernel and optional caller-supplied format.
import reader: .reader
import stream: .stream
import expansion: .expansion
import registry: lambda.doc.latex.packages.registry
let State = expansion.State;
pub type Unavailable = {issue: string}

fn kernel_reader(source: string) map => {*:reader.reader(source), origin: "bundled",
    catcodes: {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '\t': 10, '~': 13}}

// A caller-supplied format is authorized data and may define its own commands.
fn preload_format(st: State, given, budget) State^ {
    let resources = given.resources;
    let entry = if (resources is map and resources.bundled is map) resources.bundled['expl3.ltx'] else null;
    if (entry == null) st
    else {
        let fetched = expansion.captured_resource(st,"expl3.ltx","bundled")^;
        if (fetched.state.issue != null) fetched.state
        else if (not fetched.found) {*:fetched.state,issue:"tex-expl3-unavailable"}
        else {
            let source = expansion.decode_source(fetched.data)^;
            if (not source.valid) {*:fetched.state,issue:"tex-invalid-utf8"}
            else {
                let loaded = expansion.resume(fetched.state,source.text,"<bundled:expl3.ltx>",budget,"bundled")^;
                // A format run discards the ordinary kernel's preload messages.
                if (loaded.issue != null) loaded else {*:loaded,expl3_format_requested:true}
            }
        }
    }
}

pub fn preload_kernel(kernel: string, options: any ! error = null, budget: int = 20000000) State^ {
    // Public options retain the existing non-map-to-default input policy.
    let given = expansion.options_map(options);
    let configured = {*:given, latex: true, source_name: "<lambda-kernel>",
        adapters: if (given.adapters == null) registry.adapter_names() else given.adapters,
        chaptered: if (given.chaptered == null) registry.chaptered_classes() else given.chaptered};
    expansion.expand(stream.from_reader(kernel_reader(kernel)), budget, configured)
}

pub fn expand_with_kernel(source: string, kernel: string, options: any ! error = null,
        budget: int = 20000000, cached = null) State^ {
    let given = expansion.options_map(options);
    let preload = if (cached is map and given.script_expl3 != true) cached
        else preload_kernel(kernel, given, budget)^;
    if (preload.issue != null) preload
    else {
        let enabled = if (given.script_expl3 == true) expansion.enable_script_expl3(preload)^ else preload;
        let ready = if (given.script_expl3 == true) preload_format(enabled,given,budget)^ else enabled;
        let document = expansion.resume({*:ready, echo_definitions: given.echo_definitions == true},
            source, if (given.source_name == null) "<input>" else given.source_name, budget)^;
        // the native expl3 format omits preload output; an ordinary kernel run retains it.
        if (document.expl3_format_requested == true) document
        else {*:document, messages: [*preload.messages, *document.messages]}
    }
}

pub fn expand(source: string, options: any ! error = null, budget: int = 20000000) State | Unavailable | error {
    let configured = expansion.options_map(options);
    // Restricted hosts supply already authorized kernel bytes without exposing system information.
    let kernel = if (configured.kernel_source is string) configured.kernel_source
        else input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text") ^ { null };
    if (kernel == null) {issue: "tex-kernel-unavailable"}
    else expand_with_kernel(source, kernel, options, budget)
}
