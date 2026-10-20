// S9.2.2: tabs retain independent sessions; activation never rewrites document history.
import editor: lambda.editor.raster
import model: .model
pub fn create() => {tabs:[],active:null,serial:0}
pub fn lookup(workspace,id) => [for (tab in workspace.tabs where tab.id == id) tab][0]
fn idle(session) any^ {
    if (session.gesture != null or session.recording != null) raise error("Raster: finish gestures and action recording before changing tabs")
    else true
}
pub fn capture(workspace,session) any^ {
    if (workspace.active == null) workspace else if (lookup(workspace,workspace.active) == null) raise error("Raster: missing active tab")
    else {*:workspace,tabs:[for (tab in workspace.tabs) if (tab.id == workspace.active) {*:tab,session:session} else tab]}
}
pub fn add(workspace,session) any^ {
    let ready = idle(session)^;
    if (len(workspace.tabs)>=16) raise error("Raster: workspace is limited to 16 open documents")
    else {
        let id = "document-" ++ string(workspace.serial+1);
        {*:workspace,tabs:[*workspace.tabs,{id:id,session:session}],active:id,serial:workspace.serial+1}
    }
}
pub fn activate(workspace,id,current) any^ {
    let ready = idle(current)^
    let saved = capture(workspace,current)^
    let target = lookup(saved,id);
    if (target == null) raise error("Raster: missing document tab")
    else {workspace:{*:saved,active:id},session:target.session}
}
pub fn close(workspace,id,discard=false) any^ {
    let tab = lookup(workspace,id);
    if (tab == null) raise error("Raster: missing document tab")
    else if (tab.session.gesture != null or tab.session.recording != null) raise error("Raster: finish gestures and action recording before closing tabs")
    else if (editor.dirty(tab.session) and not discard) raise error("Raster: save the document or explicitly discard changes")
    else {
        let remaining = [for (entry in workspace.tabs where entry.id != id) entry];
        {*:workspace,tabs:remaining,active:if (workspace.active == id) remaining[last].id else workspace.active}
    }
}
pub fn any_dirty(workspace,current) any^ {
    let saved = capture(workspace,current)^;
    any([for (tab in saved.tabs) editor.dirty(tab.session)])
}
pub fn rows(workspace,current) any^ {
    let saved = capture(workspace,current)^;
    [for (tab in saved.tabs) {id:tab.id,name:model.value(tab.session.name,"Untitled"),dirty:editor.dirty(tab.session),active:tab.id == workspace.active}]
}
