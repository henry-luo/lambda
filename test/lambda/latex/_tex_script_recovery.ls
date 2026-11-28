import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
import snapshot: ._tex_native_snapshot

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}};

fn inspect_expansion(source, settings = null, cached = null, reference = null) any^ {
    let configured = if (settings == null) options else {*:options, *:settings};
    let result = profile.expand_with_kernel(source, kernel, configured, 100000, cached)^;
    let native = if (reference == null) parse(source, {type: "tex"})^ else reference.tex;
    let rebuilt = reconstruct.reconstruct(result)^;
    {issue:result.issue,unsupported:result.unsupported,reconstruct_issue:rebuilt.issue,
        text:(reference == null or source == reference.source) and rebuilt.text==native.text,messages:result.messages==native.messages,
        diagnostics:result.diagnostic_records==native.diagnostics}
}

pub fn check(source, settings = null, cached = null) any^ {
    let r=inspect_expansion(source,settings,cached)^;
    {issue:r.issue,text:r.text,messages:r.messages,diagnostics:r.diagnostics}
}

pub fn check_detail(source, settings = null) any^ => inspect_expansion(source,settings)

pub fn check_expansion(source, settings = null, reference = null, cached = null) any^ {
    let r=inspect_expansion(source,settings,cached,reference)^;
    {issue:r.issue,reconstruct_issue:r.reconstruct_issue,text_match:r.text,
        messages_match:r.messages,diagnostics_match:r.diagnostics}
}

// native captures make tree checks independent of the migrated public parser.
pub fn inspect_tree(source, settings = null, cached = null, reference = null) any^ {
    let configured = if (settings == null) options else {*:options, *:settings};
    let result = profile.expand_with_kernel(source, kernel, configured, 100000, cached)^;
    let native = if (reference == null) parse(source, {type: "tex"})^ else reference.tex;
    let tree = digest.digest(result)^;
    {expansion_issue:result.issue,issue:tree.issue,unsupported:tree.unsupported,
        text:(reference == null or source == reference.source) and reconstruct.reconstruct(result)^.text == native.text,
        messages:result.messages == native.messages,diagnostics:result.diagnostic_records == native.diagnostics,
        ast:if(tree.ast == null) null else if(reference == null) tree.ast == parse(source, {type: "latex"})^
            else snapshot.tree_matches(tree.ast,reference,source)^}
}

pub fn check_tree(source, settings = null, cached = null, reference = null) bool^ {
    let result = inspect_tree(source, settings, cached, reference)^;
    result.expansion_issue == null and result.issue == null and result.ast == true and
        result.text and result.messages and result.diagnostics
}

pub fn check_tree_or_guard(source, guard, settings = null) bool^ {
    let r=inspect_tree(source,settings)^;
    r.expansion_issue == null and r.text and r.messages and r.diagnostics and
        (if(r.issue == null) r.ast == true else
            r.issue == "latex-digest-unsupported" and r.unsupported == guard)
}

pub fn preload_expl3(settings = null) any^ {
    let configured = if (settings == null) {*:options,script_expl3:true}
        else {*:options,*:settings,script_expl3:true};
    profile.preload_expl3_kernel(kernel,configured,100000)
}

pub fn check_expl_bodies(bodies) any^ {
    // each mode retains a fresh first case and resumes its own immutable preload.
    let legacy = preload_expl3()^;
    let constructor = preload_expl3({constructor_argument_mode:true})^;
    let checked = [for (i,body in bodies, mode in [false, true]) {body: body, mode: mode,
        result: check("\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff",
            {script_expl3: true, constructor_argument_mode: mode},
            if (i == 0) null else if (mode) constructor else legacy)^}];
    {cases: len(checked), failures: [for (item in checked where item.result.issue != null or
        not item.result.text or not item.result.messages or not item.result.diagnostics) item]}
}

// Check each source against its native snapshot, while retaining one fresh load.
pub fn check_native_expl_expansions(sources, references) any^ {
    let cached = preload_expl3()^;
    [for (index,source in sources) check_expansion(source,{script_expl3:true},references[index],
        if (index == 0) null else cached)^]
}
