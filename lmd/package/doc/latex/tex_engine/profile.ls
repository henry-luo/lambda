// Experimental LaTeX profile: the production package still uses the native front end.
import reader: .reader
import stream: .stream
import expansion: .expansion
import registry: lambda.doc.latex.packages.registry

fn kernel_reader(source) => {*:reader.reader(source), origin: "bundled",
    catcodes: {'{': 1, '}': 2, '$': 3, '&': 4, '#': 6, '^': 7, '_': 8, '\t': 10, '~': 13}}

pub fn expand_with_kernel(source, kernel, options = null, budget = 20000000) any^ {
    let given = if (options is map) options else {};
    let configured = {*:given, latex: true, source_name: "<lambda-kernel>",
        adapters: if (given.adapters == null) registry.adapter_names() else given.adapters,
        chaptered: if (given.chaptered == null) registry.chaptered_classes() else given.chaptered};
    let preload = expansion.expand(stream.from_reader(kernel_reader(kernel)), budget, configured)^;
    if (preload.issue != null) preload
    else {
        let ready = if (given.script_expl3 == true) expansion.enable_script_expl3(preload)^ else preload;
        expansion.resume({*:ready, echo_definitions: given.echo_definitions == true},
            source, if (given.source_name == null) "<input>" else given.source_name, budget)
    }
}

pub fn expand(source, options = null, budget = 20000000) any^ {
    let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text") ^ { null };
    if (kernel == null) {issue: "tex-kernel-unavailable"}
    else expand_with_kernel(source, kernel, options, budget)
}
