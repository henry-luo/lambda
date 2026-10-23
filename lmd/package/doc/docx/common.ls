// DOCX uses ordinary immutable Items (D7.2.1/D7.2.4); XML names are expanded before interpretation.
import xml: lambda.doc.xml
import package_uri: lambda.doc.uri
pub let W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
pub let R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
pub let P = "http://schemas.openxmlformats.org/package/2006/relationships"
pub let CT = "http://schemas.openxmlformats.org/package/2006/content-types"
pub let A = "http://schemas.openxmlformats.org/drawingml/2006/main"
pub let WP = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
pub let MC = "http://schemas.openxmlformats.org/markup-compatibility/2006"
pub let CH = "http://schemas.openxmlformats.org/drawingml/2006/chart"
pub let WPS = "http://schemas.microsoft.com/office/word/2010/wordprocessingShape"
pub let DGM = "http://schemas.openxmlformats.org/drawingml/2006/diagram"
pub let V = "urn:schemas-microsoft-com:vml"
pub let O = "urn:schemas-microsoft-com:office:office"
let codes = ["invalid-source", "invalid-options", "invalid-package", "missing-part",
    "invalid-xml", "invalid-target", "unsupported", "style-cycle"]

pub fn fail(code, message, part = null) error {
    let matches = [for (i, value in codes where value == code) i];
    error({code: 7600 + (if (len(matches) == 0) 2 else matches[0]),
        message: "docx: " ++ message, file: part})
}
pub fn issue(code, part, message) map => {severity:"warning", code:code, part:part, message:message}
pub fn get(value, key, fallback = null) => if (value[key] == null) fallback else value[key]
pub fn first(values) => if (len(values) == 0) null else values[0]
pub fn dictionary(pairs) => map([for (pair in pairs) (pair.key, pair.value)])
pub fn uri(value) => if (value == "http://purl.oclc.org/ooxml/wordprocessingml/main") W
    else if (starts_with(value,"http://purl.oclc.org/ooxml/officeDocument/relationships"))
        replace(value,"http://purl.oclc.org/ooxml/officeDocument/relationships",R)
    else if (value == "http://purl.oclc.org/ooxml/drawingml/main") A
    else if (value == "http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing") WP
    else if (value == "http://purl.oclc.org/ooxml/drawingml/chart") CH
    else if (value == "http://purl.oclc.org/ooxml/drawingml/diagram") DGM
    else value

pub fn xml_node(node, inherited = {}) => xml.tree(node,inherited,uri)
pub fn children(node, tag = null, ns = W) => [for (child in node.children
    where child is map and (tag == null or child.tag == tag) and (ns == null or child.uri == ns)) child]
pub fn child(node, tag, ns = W) => first(children(node,tag,ns))
pub fn attr(node, key, ns = W) => node.attrs[ns ++ "#" ++ key]
pub fn text(node) => if (node is string) node else if (node is array) join([for (n in node) text(n)],"")
    else if (node is map) text(node.children) else ""
pub fn drawing_text(node) => if (node.uri == A and node.tag == "t") text(node)
    else if (node.uri == A and node.tag == "br") "\n"
    else if (node.uri == A and node.tag == "tab") "\t"
    else join([for (child in children(node,null,null)) drawing_text(child)],"")
pub fn descendants(node, tag, ns) => [for (child in children(node,null,null),
    found in (if (child.tag == tag and child.uri == ns) [child] else []) ++ descendants(child,tag,ns)) found]
pub fn collect(results, field) => [for (result in results, value in result[field]) value]
// shared traversal includes table cells, links and annotation blocks in reading order.
pub fn model_nodes(nodes) => [for (node in nodes, found in [node] ++ model_nodes(
    get(node,"children",[]) ++ get(node,"blocks",[]) ++ get(node,"rows",[]) ++ get(node,"cells",[]))) found]
pub fn on(node) => node != null and not contains(["0","false","off","none"],attr(node,"val"))
pub fn as_int(value, fallback = 0) => if (value == null) fallback else int(value) ^ { fallback }
pub fn pixels(value, units = 9525.0) => as_int(value,0)/units

pub fn target(owner, value) string^ => package_uri.target(owner,value,fail)^
pub fn dirname(path) {
    let parts = split(path,"/");
    if (len(parts) < 2) "" else join(slice(parts,0,len(parts)-1),"/") ++ "/"
}
pub fn rels_path(part) {
    let parts = split(part,"/");
    dirname(part) ++ "_rels/" ++ parts[len(parts)-1] ++ ".rels"
}
pub fn safe_link(value) {
    if (not (value is string) or trim(value) != value or value == "" or
        starts_with(value,"//") or contains(value,"\\") or
        any([for (ch in split(value,"")) ord(ch) < 33 or ord(ch) == 127])) false
    else {
        let colon = index_of(value,":")
        let delimiters = [for (ch in ["/","?","#"], let at = index_of(value,ch) where at != null) at];
        colon == null or any([for (at in delimiters) at < colon]) or
            contains(["https","http","mailto","tel"],lower(slice(value,0,colon)))
    }
}
