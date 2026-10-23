// Page settings belong to the document, so undo, snapshots, and PDF share them (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction

pub let papers = [{id:"A4",width:210,height:297},{id:"Letter",width:215.9,height:279.4},{id:"A5",width:148,height:210}]
pub let defaults = {paper:"A4",orientation:"portrait",top:17,right:19,bottom:19,left:19}
fn margin_preset(id,label,vertical,horizontal,inside=null) => {id:id,label:label,
    values:{top:vertical,bottom:vertical,right:horizontal,left:if (inside==null) horizontal else inside,mirrored:inside!=null}}
pub let margin_presets = [margin_preset("normal","Normal",25.4,25.4),margin_preset("narrow","Narrow",12.7,12.7),
    margin_preset("moderate","Moderate",25.4,19.05),margin_preset("wide","Wide",25.4,50.8),
    margin_preset("mirrored","Mirrored",25.4,25.4,31.75)]
pub fn margin_preset_active(doc,preset) {
    let p = settings(doc);
    (p.mirrored==true)==preset.values.mirrored and all([for (side in ["top","right","bottom","left"]) p[side]==preset.values[side]])
}
fn inches(value) => string(round(value/25.4*100)/100) ++ "\""
pub fn margin_summary(p) => ["Top: " ++ inches(p.top) ++ ", Bottom: " ++ inches(p.bottom),
    (if (p.mirrored==true) "Inside: " else "Left: ") ++ inches(p.left) ++ ", " ++
    (if (p.mirrored==true) "Outside: " else "Right: ") ++ inches(p.right)]
pub fn settings(doc) {
    let raw = attrs_get(doc.attrs,'data-word-page')
    let p = if (raw==null) defaults else parse(raw,'json') ^ { defaults }
    if (valid(p)) p else defaults
}
fn paper_size(p) => [for (paper in papers where paper.id==p.paper) paper][0]
pub fn dimension(p,axis) {
    let paper = paper_size(p)
    if ((axis=="width") == (p.orientation=="portrait")) paper.width else paper.height
}
// left/right store inside/outside in mirrored mode; page parity only changes presentation.
pub fn resolved(p,page_no=1) => if (p.mirrored==true and page_no%2==0) {*:p,left:p.right,right:p.left} else p
pub fn margin_key(doc,side,page_no=1) => if (settings(doc).mirrored==true and page_no%2==0 and contains(["left","right"],side))
    (if (side=="left") "right" else "left") else side
pub fn geometry(doc,page_no=1) {
    let p = resolved(settings(doc),page_no)
    {width:round(dimension(p,"width")*96/25.4),height:round(dimension(p,"height")*96/25.4),
        top:round(p.top*96/25.4),right:round(p.right*96/25.4),bottom:round(p.bottom*96/25.4),left:round(p.left*96/25.4)}
}
pub fn valid(p) => p is map and paper_size(p)!=null and contains(["portrait","landscape"],p.orientation) and
    (p.mirrored==null or p.mirrored is bool) and
    all([for (v in [p.top,p.right,p.bottom,p.left]) v is number and v>=0 and v<=80]) and
    p.left+p.right < dimension(p,"width")-20 and p.top+p.bottom < dimension(p,"height")-20
pub fn transaction(doc,selection,p) map^ {
    if (not valid(p)) raise error("Word: choose a paper size and margins that leave at least 20 mm of writing space")
    else tx_step(tx_begin(doc,selection),step_set_attr([],'data-word-page',format(p,'json')^))
}
pub fn editor_style(doc,page_no=1) {
    let g = geometry(doc,page_no)
    "width:" ++ string(g.width) ++ "px; min-height:" ++ string(g.height) ++ "px; padding:" ++
        join([for (v in [g.top,g.right,g.bottom,g.left]) string(v) ++ "px"]," ") ++ ";"
}
// Keep original child identities: page wrappers must not change editing source paths.
pub fn starts(doc) => [0,*[for (i,n in doc.content where i>0 and attrs_get(n.attrs,'data-word-page-break')=="true") i]]
pub fn sheets(doc) {
    let boundaries = [*starts(doc),len(doc.content)];
    [for (i in 0 to len(boundaries)-2) slice(doc.content,boundaries[i],boundaries[i+1])]
}
pub fn print_style(doc) {
    let p = settings(doc)
    "size:" ++
        string(dimension(p,"width")) ++ "mm " ++ string(dimension(p,"height")) ++ "mm; margin:" ++
        join([for (v in [p.top,p.right,p.bottom,p.left]) string(v) ++ "mm"]," ") ++ ";"
}
pub fn export_css(doc) {
    let p = settings(doc)
    // the page owns the inset; a UA body margin would add an extra offset to exported text.
    "body.word-content {margin:0;} @media screen { .word-content {" ++ editor_style(doc) ++ "} } @page { " ++ print_style(doc) ++ " }" ++
        (if (p.mirrored!=true) "" else " @page:left {margin-left:" ++ string(p.right) ++ "mm;margin-right:" ++ string(p.left) ++
            "mm;} @page:right {margin-left:" ++ string(p.left) ++ "mm;margin-right:" ++ string(p.right) ++ "mm;}")
}
