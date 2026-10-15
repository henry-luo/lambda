// DOCX uses ordinary immutable Items (D7.2.1/D7.2.4); XML names are expanded before interpretation.
import xml: lambda.doc.xml
pub let W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
pub let R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
pub let P = "http://schemas.openxmlformats.org/package/2006/relationships"
pub let CT = "http://schemas.openxmlformats.org/package/2006/content-types"
pub let A = "http://schemas.openxmlformats.org/drawingml/2006/main"
pub let WP = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
pub let MC = "http://schemas.openxmlformats.org/markup-compatibility/2006"
pub let CH = "http://schemas.openxmlformats.org/drawingml/2006/chart"
pub let WPS = "http://schemas.microsoft.com/office/word/2010/wordprocessingShape"
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
    else value

pub fn xml_node(node, inherited = {}) {
    if (node is string) node else if (not (node is element)) null else {
        let attributes = map(node)
        let bindings = xml.bindings(node,inherited)
        let tag = xml.expanded(name(node),bindings);
        {tag:tag.local, uri:uri(tag.uri),
            attrs:map([for (key,value in attributes where string(key) != "xmlns" and
                not starts_with(string(key),"xmlns:"), let n = xml.expanded(key,bindings,true))
                (uri(n.uri) ++ "#" ++ n.local,value)]),
            children:[for (child in content(node) where child is string or (child is element and
                not starts_with(string(name(child)),"?") and not starts_with(string(name(child)),"!")))
                xml_node(child,bindings)]}
    }
}
pub fn children(node, tag = null, ns = W) => [for (child in node.children
    where child is map and (tag == null or child.tag == tag) and (ns == null or child.uri == ns)) child]
pub fn child(node, tag, ns = W) => first(children(node,tag,ns))
pub fn attr(node, key, ns = W) => node.attrs[ns ++ "#" ++ key]
pub fn text(node) => if (node is string) node else if (node is array) join([for (n in node) text(n)],"")
    else if (node is map) text(node.children) else ""
pub fn descendants(node, tag, ns) => [for (child in children(node,null,null),
    found in (if (child.tag == tag and child.uri == ns) [child] else []) ++ descendants(child,tag,ns)) found]
pub fn collect(results, field) => [for (result in results, value in result[field]) value]
// shared traversal includes table cells, links and annotation blocks in reading order.
pub fn model_nodes(nodes) => [for (node in nodes, found in [node] ++ model_nodes(
    get(node,"children",[]) ++ get(node,"blocks",[]) ++ get(node,"rows",[]) ++ get(node,"cells",[]))) found]
pub fn on(node) => node != null and not contains(["0","false","off","none"],attr(node,"val"))
pub fn as_int(value, fallback = 0) => if (value == null) fallback else int(value) ^ { fallback }
pub fn pixels(value, units = 9525.0) => as_int(value,0)/units

fn hex_byte(value, index) int^ {
    let hi = index_of("0123456789abcdef",lower(slice(value,index+1,index+2)))
    let lo = index_of("0123456789abcdef",lower(slice(value,index+2,index+3)));
    if (slice(value,index,index+1) != "%" or index+2 >= len(value) or hi == null or lo == null)
        raise fail("invalid-target","Invalid percent escape")
    else hi*16+lo
}
// Decode UTF-8 URI escapes once, before checking traversal and package separators.
fn decode(value, index = 0, result = "") string^ {
    if (index >= len(value)) result else if (slice(value,index,index+1) != "%")
        decode(value,index+1,result ++ slice(value,index,index+1))
    else {
        let lead = hex_byte(value,index)^
        let byte_count = if (lead < 128) 1 else if (lead >= 194 and lead <= 223) 2
            else if (lead >= 224 and lead <= 239) 3 else if (lead >= 240 and lead <= 244) 4 else 0
        let bytes = [for (i in 1 to (byte_count-1)) hex_byte(value,index+i*3)^]
        let cp = if (byte_count == 1) lead else if (byte_count == 2) (lead-192)*64+bytes[0]-128
            else if (byte_count == 3) (lead-224)*4096+(bytes[0]-128)*64+bytes[1]-128
            else if (byte_count == 4) (lead-240)*262144+(bytes[0]-128)*4096+(bytes[1]-128)*64+bytes[2]-128 else -1;
        if (byte_count == 0 or any([for (b in bytes) b < 128 or b > 191]) or
            (byte_count == 2 and cp < 128) or (byte_count == 3 and cp < 2048) or (byte_count == 4 and cp < 65536) or
            cp > 1114111 or (cp >= 55296 and cp <= 57343) or cp < 32 or cp == 127 or cp == 47 or cp == 92)
            raise fail("invalid-target","Invalid encoded package path")
        else decode(value,index+byte_count*3,result ++ chr(cp))
    }
}
fn clean(parts, index = 0, result = []) string^ {
    if (index >= len(parts)) join(result,"/") else if (parts[index] == "" or parts[index] == ".")
        clean(parts,index+1,result)
    else if (parts[index] == "..") {
        if (len(result) == 0) raise fail("invalid-target","Relationship target escapes package root")
        else clean(parts,index+1,slice(result,0,len(result)-1))
    } else clean(parts,index+1,result ++ [parts[index]])
}
pub fn target(owner, value) string^ {
    if (not (value is string) or value == "" or starts_with(value,"//") or
        any([for (ch in ["\\",":","?","#"]) contains(value,ch)]) or
        any([for (ch in split(value,"")) ord(ch) < 32 or ord(ch) == 127]))
        raise fail("invalid-target","Invalid internal relationship target",owner)
    else {
        let decoded = decode(value)^
        let base = if (starts_with(decoded,"/")) "" else dirname(owner)
        let normalized = clean(split(base ++ decoded,"/"))^;
        if (normalized == "") raise fail("invalid-target","Empty internal relationship target",owner)
        else normalized
    }
}
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
