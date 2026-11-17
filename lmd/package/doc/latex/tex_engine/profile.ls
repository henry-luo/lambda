// Experimental LaTeX profile: the production package still uses the native front end.
import reader: .reader
import stream: .stream
import expansion: .expansion
import registry: lambda.doc.latex.packages.registry
let State = expansion.State;
pub type Unavailable = {issue: string}

fn kernel_reader(source: string) map => {*:reader.reader(source), origin: "bundled",
    catcodes: {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '\t': 10, '~': 13}}

pub fn expand_with_kernel(source: string, kernel: string, options: any ! error = null, budget: int = 20000000) State^ {
    // Public options retain the existing non-map-to-default input policy.
    let given = expansion.options_map(options);
    let configured = {*:given, latex: true, source_name: "<lambda-kernel>",
        adapters: if (given.adapters == null) registry.adapter_names() else given.adapters,
        chaptered: if (given.chaptered == null) registry.chaptered_classes() else given.chaptered};
    let preload = expansion.expand(stream.from_reader(kernel_reader(kernel)), budget, configured)^;
    if (preload.issue != null) preload
    else {
        let ready = if (given.script_expl3 == true) expansion.enable_script_expl3(preload)^ else preload;
        let document = expansion.resume({*:ready, echo_definitions: given.echo_definitions == true},
            source, if (given.source_name == null) "<input>" else given.source_name, budget)^;
        // the native expl3 format omits preload output; an ordinary kernel run retains it.
        if (document.expl3_format_requested == true) document
        else {*:document, messages: [*preload.messages, *document.messages]}
    }
}

pub fn expand(source: string, options: any ! error = null, budget: int = 20000000) State | Unavailable | error {
    let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text") ^ { null };
    if (kernel == null) {issue: "tex-kernel-unavailable"}
    else expand_with_kernel(source, kernel, options, budget)
}
