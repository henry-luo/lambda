import xml: lambda.doc.xml
import uri: lambda.doc.uri

pub let OPF = "http://www.idpf.org/2007/opf"
pub let OCF = "urn:oasis:names:tc:opendocument:xmlns:container"
pub let XHTML = "http://www.w3.org/1999/xhtml"
pub let EPUB = "http://www.idpf.org/2007/ops"
pub let DC = "http://purl.org/dc/elements/1.1/"
pub let NCX = "http://www.daisy.org/z3986/2005/ncx/"
let codes = ["invalid-source","invalid-options","invalid-package","missing-part","invalid-xml",
    "invalid-target","unsupported","protected","limit","invalid-location"]
pub fn fail(code,message,part = null) error {
    let found = [for (i,name in codes where name == code) i];
    error({code:7800 + (if (len(found) > 0) found[0] else 2),message:"ebook: " ++ message,file:part})
}
pub fn checked_options(options,allowed) map^ {
    let opts = if (options == null) {} else options;
    if (not (opts is map)) raise fail("invalid-options","Options must be a map")
    else if (any([for (key,value in opts) not (string(key) in allowed)]))
        raise fail("invalid-options","Unknown option")
    else opts
}
pub fn get(value,key,fallback = null) => if (value[key] == null) fallback else value[key]
pub fn value(item,fallback) => if (item == null) fallback else item
pub fn first(values) => if (len(values) == 0) null else values[0]
pub fn children(node,tag = null,ns = null) => [for (child in node.children where child is map and
    (tag == null or child.tag == tag) and (ns == null or child.uri == ns)) child]
pub fn child(node,tag,ns = null) => first(children(node,tag,ns))
pub fn attr(node,key,ns = "") => node.attrs[ns ++ "#" ++ key]
pub fn text(node) => if (node is string) node else if (node is array or node is list)
    join([for (child in node) text(child)],"") else if (node is map) text(node.children) else ""
pub fn descendants(node,tag = null,ns = null) => [for (child in children(node),found in
    (if ((tag == null or child.tag == tag) and (ns == null or child.uri == ns)) [child] else []) ++
    descendants(child,tag,ns)) found]
pub fn issue(code,part,message) => {code:code,severity:"warning",part:part,message:message}
pub fn target(owner,value) map^ {
    let valid = if (not (value is string)) raise fail("invalid-target","Missing publication URL",owner) else true
    let pieces = split(value,"#")
    let path = if (pieces[0] == "") owner else uri.target(owner,pieces[0],fail)^;
    {href:path,fragment:if (len(pieces) > 1) uri.fragment(join(drop(pieces,1),"#"),fail)^ else null}
}
pub fn url(target) => uri.url(target.href,target.fragment)
pub fn tree(source,part) map^ {
    let document = parse(source,{type:"xml",flavor:"document"})^
    let root = xml.root(document);
    if (not (root is element)) raise fail("invalid-xml","Expected one document element",part)
    else xml.tree(root)
}
