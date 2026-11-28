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

fn kernel_options(given: map) map => {*:given, latex:true, source_name:"<lambda-kernel>",
    adapters:if (given.adapters == null) registry.adapter_names() else given.adapters,
    chaptered:if (given.chaptered == null) registry.chaptered_classes() else given.chaptered}

pub fn preload_kernel(kernel: string, options: any ! error = null, budget: int = 20000000,
        echo_kernel: bool = true) State^ {
    // Public options retain the existing non-map-to-default input policy.
    let configured = kernel_options(expansion.options_map(options));
    // the document resume drops kernel output, so its preload needs no definition spans.
    let loaded = expansion.expand(stream.from_reader(kernel_reader(kernel)), budget,
        if (echo_kernel) configured else {*:configured, echo_definitions:false})^;
    // keep the original settings even when the host binds document resources later.
    {*:loaded,kernel_preload_source:kernel,kernel_preload_options:configured}
}

// compare the normalized settings used to initialize or resume a kernel.
fn same_kernel_settings(left: map, right: map) bool =>
    (left.math_mode == true) == (right.math_mode == true) and
    (left.script_expl3 == true) == (right.script_expl3 == true) and
    (left.auto_expl3 == true) == (right.auto_expl3 == true) and
    (left.constructor_argument_mode == true) == (right.constructor_argument_mode == true) and
    left.adapters == right.adapters and left.chaptered == right.chaptered and
    (if (left.base_path is string) left.base_path else null) ==
        (if (right.base_path is string) right.base_path else null) and
    (left.host_no_base == true) == (right.host_no_base == true) and
    (left.resource_requests == true) == (right.resource_requests == true)

// kernel state is reusable only for the source and settings that produced it.
fn compatible_preload(cached: map, kernel: string, given: map) bool^ {
    let configured = kernel_options(given);
    let original = cached.kernel_preload_options;
    let resource_free_host = original is map and original.resource_requests == true and
        cached.resource_lookups == 0;
    original is map and cached.kernel_preload_source == kernel and len(cached.sources) > 0 and
        cached.sources[0].data == kernel and cached.latex == true and
        same_kernel_settings(original,configured) and same_kernel_settings(cached,configured) and
        (resource_free_host or original.resources == configured.resources) and
        cached.resources == configured.resources
}

// Definitions added by the expl3 bootstrap are immutable until the document resumes.
pub fn preload_expl3_kernel(kernel: string, options: any ! error = null, budget: int = 20000000,
        echo_kernel: bool = true) State^ {
    let loaded = preload_kernel(kernel, {*:expansion.options_map(options),script_expl3:true}, budget, echo_kernel)^;
    if (loaded.issue != null) loaded
    else {*:expansion.enable_script_expl3(loaded)^,expl3_bootstrap_ready:true}
}

fn finish_document(document: State, messages: string[]) State^ {
    // the native expl3 format omits preload output; an ordinary kernel run retains it.
    let finished = if (document.expl3_format_requested == true) document
        else {*:document, messages: [*messages, *document.messages]};
    if (document.resource_checkpoint is map) {*:finished, kernel_messages:messages} else finished
}

pub fn continue_document(saved: State, resources: map, messages: string[], budget: int) State^ =>
    finish_document(expansion.resume_resource(saved, resources, budget)^, messages)

pub fn expand_with_kernel(source: string, kernel: string, options: any ! error = null,
        budget: int = 20000000, cached = null, capture_checkpoint: bool = false) State^ {
    let given = expansion.options_map(options);
    let reuse = cached is map and cached.issue == null and compatible_preload(cached,kernel,given)^;
    let preload = if (reuse) cached else preload_kernel(kernel, given, budget, false)^;
    if (preload.issue != null) preload
    else {
        let enabled = if (given.script_expl3 == true and preload.expl3_bootstrap_ready != true)
            expansion.enable_script_expl3(preload)^ else preload;
        let ready = if (given.script_expl3 == true) preload_format(enabled,given,budget)^ else enabled;
        let constructors = expansion.enable_raw_constructors(ready, given.raw)^;
        let document = expansion.resume({*:constructors, echo_definitions: given.echo_definitions == true},
            source, if (given.source_name == null) "<input>" else given.source_name, budget, "local", capture_checkpoint)^;
        finish_document(document, preload.messages)
    }
}

pub fn expand(source: string, options: any ! error = null, budget: int = 20000000,
        capture_checkpoint: bool = false) State | Unavailable | error {
    let configured = expansion.options_map(options);
    // Restricted hosts supply already authorized kernel bytes without exposing system information.
    let kernel = if (configured.kernel_source is string) configured.kernel_source
        else input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text") ^ { null };
    if (kernel == null) {issue: "tex-kernel-unavailable"}
    else expand_with_kernel(source, kernel, options, budget, null, capture_checkpoint)
}
