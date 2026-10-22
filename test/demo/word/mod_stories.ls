// Repeated stories are document data, independent of the body caret (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import page: .mod_page
import objects: .mod_objects

pub let defaults = {header:"",footer:"",header_align:"left",footer_align:"center",size:10,distance:8,different_first:false}
pub fn settings(doc) {
    let p = objects.settings(doc,"data-word-stories",defaults)
    if (valid(p)) p else defaults
}
pub fn field_text(value,page_no,total) => replace(replace(value,"{PAGE}",string(page_no)),"{NUMPAGES}",string(total))
fn fits(value,margin,p) => value=="" or p.distance*96/25.4+len(split(value,"\n"))*p.size*96/72*1.2+4<=margin
fn valid(p) => p.header is string and p.footer is string and len(p.header)<=1000 and len(p.footer)<=1000 and
    contains(["left","center","right"],p.header_align) and contains(["left","center","right"],p.footer_align) and
    p.size is number and p.size>=8 and p.size<=24 and p.distance is number and p.distance>=3 and p.distance<=30 and p.different_first is bool
pub fn validate(doc,p) bool^ {
    let g = page.geometry(doc)
    if (not valid(p)) raise error("Word: enter header/footer text, size 8–24 pt, and distance 3–30 mm")
    else if (not fits(p.header,g.top,p) or not fits(p.footer,g.bottom,p))
        raise error("Word: the header or footer needs more room; increase the page margin or reduce its size/distance")
    else true
}
pub fn transaction(editor,p) map^ {
    let checked = validate(editor.doc,p)^
    tx_step(tx_begin(editor.doc,editor.selection),step_set_attr([],'data-word-stories',format(p,'json')^))
}
pub fn story(doc,kind,page_no,total) {
    let p = settings(doc)
    let value = p[kind]
    let g = page.geometry(doc)
    if (value=="" or (page_no==1 and p.different_first)) null else
        <div class:"word-story word-" ++ kind,contenteditable:"false",role:"note",["aria-label"]:if (kind=="header") "Page header" else "Page footer",
            style:(if (kind=="header") "top:" else "bottom:") ++ string(p.distance*96/25.4) ++ "px;left:" ++ string(g.left) ++
                "px;right:" ++ string(g.right) ++ "px;font-size:" ++ string(p.size) ++ "pt;text-align:" ++ p[kind ++ "_align"] ++ ";",
            field_text(value,page_no,total)>
}
fn css_literal(value) => "\"" ++ replace(replace(replace(replace(value,"\\","\\\\"),"\"","\\\""),"\r",""),"\n","\\A ") ++ "\""
// Tokenize first so user strings cannot inject CSS while page fields remain counters.
fn css_content(value) => join([for (part in split(value,"{PAGE}"))
    join([for (piece in split(part,"{NUMPAGES}")) css_literal(piece)]," counter(pages) ")]," counter(page) ")
pub fn export_css(doc) {
    let p = settings(doc)
    let regions = [{kind:"header",edge:"top",align:p.header_align},{kind:"footer",edge:"bottom",align:p.footer_align}]
    let rules = [for (r in regions where p[r.kind]!="") "@" ++ r.edge ++ "-" ++ r.align ++ " { content:" ++ css_content(p[r.kind]) ++
        ";font-family:Arial;font-size:" ++ string(p.size) ++ "pt;color:#718397;white-space:pre-wrap;}" ]
    "@page {" ++ join(rules," ") ++ "}" ++ if (p.different_first) "@page:first {" ++
        join([for (r in regions) "@" ++ r.edge ++ "-" ++ r.align ++ " {content:none;}"]," ") ++ "}" else ""
}
