// Column geometry is document data; projection never splits source paragraphs (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import page: .mod_page
import objects: .mod_objects

pub let defaults = {count:1,gap:12.7,line:false}
pub let presets = [{count:1,label:"One"},{count:2,label:"Two"},{count:3,label:"Three"}]
fn valid(p) => p is map and p.count is number and p.count>=1 and p.count<=6 and p.count==floor(p.count) and
    p.gap is number and p.gap>=0 and p.gap<=80 and p.line is bool
pub fn settings(doc) {
    let p = objects.settings(doc,"data-word-columns",defaults)
    if (valid(p)) p else defaults
}
pub fn width(doc,p) {
    let paper = page.settings(doc);
    (page.dimension(paper,"width")-paper.left-paper.right-(p.count-1)*p.gap)/p.count
}
pub fn minimum_width(doc) {
    let p = settings(doc)
    p.count*20+(p.count-1)*p.gap
}
pub fn validate(doc,p) bool^ {
    if (not valid(p)) raise error("Word: choose 1–6 columns and spacing from 0–80 mm")
    else if (width(doc,p)<20) raise error("Word: columns need at least 20 mm of text width; reduce spacing or column count, or widen the page")
    else true
}
pub fn transaction(editor,params) map^ {
    let p = {*:settings(editor.doc),*:params}
    let checked = validate(editor.doc,p)^
    tx_step(tx_begin(editor.doc,editor.selection),step_set_attr([],'data-word-columns',format(p,'json')^))
}
pub fn style(doc) {
    let p = settings(doc)
    "column-count:" ++ string(p.count) ++ ";column-gap:" ++ string(p.gap) ++ "mm;column-fill:balance;column-rule:" ++
        (if (p.line and p.count>1) "0.5pt solid #718397" else "none") ++ ";"
}
pub fn export_css(doc) => if (settings(doc).count==1) "" else "@media screen { body.word-content {" ++ style(doc) ++ "} }"
// PDF body regions advance to the next column before the next sheet, including inside paragraphs.
pub fn pdf_flow(doc,body) {
    if (settings(doc).count==1) body else
        // paper geometry comes from the common @page cascade, including facing-page overrides.
        "<r:master-set xmlns:r=\"urn:lambda:radiant:page\"><r:page-master name=\"word-columns\"><r:region role=\"body\" name=\"body\" style=\"" ++
        style(doc) ++ "\"></r:region></r:page-master></r:master-set>" ++
        "<r:page-sequence xmlns:r=\"urn:lambda:radiant:page\" master-reference=\"word-columns\" force-page-count=\"no-force\">" ++
        "<r:flow region-name=\"body\">" ++ body ++ "</r:flow></r:page-sequence>"
}
