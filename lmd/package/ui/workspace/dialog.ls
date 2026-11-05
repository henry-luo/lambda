// The caller owns visibility, commands and focus restoration (S12.1.3v2, D10.2.1).
import c: lambda.ui.core.component
import scope: lambda.ui.workspace.scope

pub let css = scope.isolate("
:where(.workspace-dialog-backdrop)__workspace_document__ { position:fixed; inset:0; z-index:50; display:flex; align-items:center; justify-content:center; background:#17274340; }
:where(.workspace-dialog-backdrop)>.workspace-dialog__workspace_document__ { max-width:90vw; max-height:90vh; overflow:auto; padding:20px; border:1px solid var(--lambda-app-border,#cbd5e1); border-radius:var(--lambda-app-radius,4px); background:var(--lambda-app-surface,#fff); color:var(--lambda-app-text,#24334b); box-shadow:0 16px 48px #17274330; }
")

// Exactly one accessible name is required; the caller chooses literal text or a label element.
pub fn modal(options,children) element^ {
    let valid = c.properties(options,["id","label","labelled_by","backdrop_class","class"],'dialog',false,"workspace")^
    let label = options.label
    let labelled = options.labelled_by;
    if (not (options.id is string) or trim(options.id)=="" or
        not ((label is string and trim(label)!="" and labelled==null) or (labelled is string and trim(labelled)!="" and label==null)))
        raise error("workspace dialog: provide an id and one accessible name")
    else <div class:"workspace-dialog-backdrop " ++ c.text(options.backdrop_class),
        <section id:options.id,class:"workspace-dialog " ++ c.text(options.class),role:"dialog",["aria-modal"]:"true",
            *:if (label!=null) {['aria-label']:label} else {['aria-labelledby']:labelled},*c.children(children)>>
}
