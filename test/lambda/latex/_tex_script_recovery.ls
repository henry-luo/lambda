import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest

let kernel = input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let options = {echo_definitions: true, adapters: [], resources: {local: {}, bundled: {}}};

fn inspect_expansion(source, settings = null) any^ {
    let configured = if (settings == null) options else {*:options, *:settings};
    let result = profile.expand_with_kernel(source, kernel, configured, 100000)^;
    let native = parse(source, {type: "tex"})^;
    let rebuilt = reconstruct.reconstruct(result)^;
    {issue:result.issue,unsupported:result.unsupported,reconstruct_issue:rebuilt.issue,
        text:rebuilt.text==native.text,messages:result.messages==native.messages,
        diagnostics:result.diagnostic_records==native.diagnostics}
}

pub fn check(source, settings = null) any^ {
    let r=inspect_expansion(source,settings)^;
    {issue:r.issue,text:r.text,messages:r.messages,diagnostics:r.diagnostics}
}

pub fn check_detail(source, settings = null) any^ => inspect_expansion(source,settings)

pub fn check_expansion(source, settings = null) any^ {
    let r=inspect_expansion(source,settings)^;
    {issue:r.issue,reconstruct_issue:r.reconstruct_issue,text_match:r.text,
        messages_match:r.messages,diagnostics_match:r.diagnostics}
}

pub fn inspect_tree(source, settings = null) any^ {
    let configured = if (settings == null) options else {*:options, *:settings};
    let result = profile.expand_with_kernel(source, kernel, configured, 100000)^;
    let native = parse(source, {type: "tex"})^;
    let tree = digest.digest(result)^;
    {expansion_issue:result.issue,issue:tree.issue,unsupported:tree.unsupported,
        text:reconstruct.reconstruct(result)^.text == native.text,
        messages:result.messages == native.messages,diagnostics:result.diagnostic_records == native.diagnostics,
        ast:if(tree.ast == null) null else tree.ast == parse(source, {type: "latex"})^}
}

pub fn check_tree(source, settings = null) bool^ {
    let result = inspect_tree(source, settings)^;
    result.expansion_issue == null and result.issue == null and result.ast == true and
        result.text and result.messages and result.diagnostics
}

pub fn check_tree_or_guard(source, guard, settings = null) bool^ {
    let r=inspect_tree(source,settings)^;
    r.expansion_issue == null and r.text and r.messages and r.diagnostics and
        (if(r.issue == null) r.ast == true else
            r.issue == "latex-digest-unsupported" and r.unsupported == guard)
}

pub fn check_expl_bodies(bodies) any^ {
    let checked = [for (body in bodies, mode in [false, true]) {body: body, mode: mode,
        result: check("\\ExplSyntaxOn" ++ body ++ "\\ExplSyntaxOff",
            {script_expl3: true, constructor_argument_mode: mode})^}];
    {cases: len(checked), failures: [for (item in checked where item.result.issue != null or
        not item.result.text or not item.result.messages or not item.result.diagnostics) item]}
}
