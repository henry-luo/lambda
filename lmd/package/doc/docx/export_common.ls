// DOCX writing interprets data; names never become source (S1.8, D7.2.4v4).
import c: .common
import xml: lambda.doc.xml
pub let R = c.R
pub let W = c.W
pub fn tag(n) => if (n is element) lower(string(name(n))) else ""
fn flattened(values,depth=0) array^ {
    if (depth>64) raise c.fail("invalid-source","Export arrays exceed 64 levels")
    else [for (value in values) if (value is array or value is list) *flattened(value,depth+1)^ else value]
}
pub fn kids(n) => if (n is element) flattened(content(n))^ else []
pub fn attrs(n) => if (n is element) map(n) else {}
pub fn text(n) => xml.text(n)
pub fn result(nodes=[],diagnostics=[]) => {nodes:[for (node in nodes where node!=null) node],
    diagnostics:[for (issue in diagnostics where issue!=null) issue]}
pub fn issue(code,message) => c.issue("export-" ++ code,"word/document.xml",message)
pub fn merge(values) => result([for (v in values) *v.nodes],[for (v in values) *v.diagnostics])
pub fn path_key(path) => join([for (i in path) string(i)],".")
pub fn walk(n,path=[]) array^ {
    if (len(path)>64) raise c.fail("invalid-source","Export document exceeds 64 levels")
    else [{node:n,path:path},*[for (i,child in kids(n)) *walk(child,[*path,i])^]]
}
pub fn root(value) element^ {
    if (value is string) root(xml.root(parse(value,'html')^))^
    else if (value is array or value is list) <article *value>
    else if (not (value is element)) raise c.fail("invalid-source","DOCX export expects HTML text or an element tree")
    else if (tag(value)=="html") {
        let body = [for (child in kids(value) where tag(child)=="body") child][0];
        if (body==null) raise c.fail("invalid-source","HTML document has no body") else body
    } else if (tag(value) in ["document","#document"]) {
        let html = [for (child in kids(value) where tag(child)=="html") child][0];
        if (html!=null) root(html)^ else <article *kids(value)>
    } else value
}
pub fn xml_file(filename,node) => <fs name:filename,kind:'file',format:'xml',node>
pub fn relationship(id,kind,target,external=false) => <Relationship Id:id,Type:R ++ "/" ++ kind,Target:target,
    *:if (external) {TargetMode:"External"} else {}>
pub fn ordinal(value,fallback=1) {
    let n = if (value==null) fallback else int(value) ^ { fallback };
    max(1,min(2147483647,n))
}
// Word property containers have schema order even when the source CSS does not.
pub fn ordered(node,names) => [*[for (tag in names,child in kids(node) where string(name(child))==tag) child],
    *[for (child in kids(node) where not (string(name(child)) in names)) child]]
