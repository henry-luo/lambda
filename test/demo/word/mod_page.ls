// Page settings belong to the document, so undo, snapshots, and PDF share them (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction

pub let papers = [{id:"A4",width:210,height:297},{id:"Letter",width:215.9,height:279.4},{id:"A5",width:148,height:210}]
pub let defaults = {paper:"A4",orientation:"portrait",top:17,right:19,bottom:19,left:19}
pub fn settings(doc) {
    let raw = attrs_get(doc.attrs,'data-word-page')
    let p = if (raw==null) defaults else parse(raw,'json') ^ { defaults }
    if (valid(p)) p else defaults
}
fn paper_size(p) => [for (paper in papers where paper.id==p.paper) paper][0]
fn dimension(p,axis) {
    let paper = paper_size(p)
    if ((axis=="width") == (p.orientation=="portrait")) paper.width else paper.height
}
pub fn geometry(doc) {
    let p = settings(doc)
    {width:round(dimension(p,"width")*96/25.4),height:round(dimension(p,"height")*96/25.4),
        top:round(p.top*96/25.4),right:round(p.right*96/25.4),bottom:round(p.bottom*96/25.4),left:round(p.left*96/25.4)}
}
pub fn valid(p) => p is map and paper_size(p)!=null and contains(["portrait","landscape"],p.orientation) and
    all([for (v in [p.top,p.right,p.bottom,p.left]) v is number and v>=0 and v<=80]) and
    p.left+p.right < dimension(p,"width")-20 and p.top+p.bottom < dimension(p,"height")-20
pub fn transaction(doc,selection,p) map^ {
    if (not valid(p)) raise error("Word: choose a paper size and margins that leave at least 20 mm of writing space")
    else tx_step(tx_begin(doc,selection),step_set_attr([],'data-word-page',format(p,'json')^))
}
pub fn editor_style(doc) {
    let g = geometry(doc)
    "width:" ++ string(g.width) ++ "px; min-height:" ++ string(g.height) ++ "px; padding:" ++
        join([for (v in [g.top,g.right,g.bottom,g.left]) string(v) ++ "px"]," ") ++ ";"
}
// Keep original child identities: page wrappers must not change editing source paths.
pub fn sheets(doc) {
    let starts = [0,*[for (i,n in doc.content where i>0 and attrs_get(n.attrs,'data-word-page-break')=="true") i],len(doc.content)];
    [for (i in 0 to len(starts)-2) slice(doc.content,starts[i],starts[i+1])]
}
pub fn export_css(doc) {
    let p = settings(doc)
    "@media screen { .word-content {" ++ editor_style(doc) ++ "} } @page { size:" ++
        string(dimension(p,"width")) ++ "mm " ++ string(dimension(p,"height")) ++ "mm; margin:" ++
        join([for (v in [p.top,p.right,p.bottom,p.left]) string(v) ++ "mm"]," ") ++ "; }"
}
