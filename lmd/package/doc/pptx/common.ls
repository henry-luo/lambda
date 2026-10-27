// PresentationML adapts shared Office/XML helpers to its own namespace and errors (D7.2.4v4).
import office: lambda.doc.docx.common
import xml: lambda.doc.xml
pub let P = "http://schemas.openxmlformats.org/presentationml/2006/main"
pub let A = office.A
pub let R = office.R
pub let MC = office.MC
pub fn uri(value) => if (value=="http://purl.oclc.org/ooxml/presentationml/main") P else office.uri(value)
let codes = ["invalid-source","invalid-options","invalid-package","missing-part","invalid-xml","invalid-target","unsupported"]
pub fn fail(code,message,part=null) error {
    let matches = [for (i,value in codes where value==code) i];
    error({code:7700+(if (len(matches)==0) 2 else matches[0]),message:"pptx: " ++ message,file:part})
}
pub fn issue(code,part,message) => office.issue(code,part,message)
pub fn warning(ctx,code,message) => issue(code,string(ctx.main),message)
pub fn value(item,fallback) => if (item==null) fallback else item
pub fn get(value,key,fallback=null) => office.get(value,key,fallback)
pub fn children(node,tag=null,ns=P) => xml.children(node,tag,ns)
pub fn child(node,tag,ns=P) => xml.child(node,tag,ns)
pub fn attr(node,key,ns="") => xml.attr(node,key,ns)
pub fn text(node) => xml.tree_text(node)
pub fn descendants(node,tag,ns) => office.descendants(node,tag,ns)
pub fn num(value,fallback=0.0) => if (value==null) fallback else float(value) ^ { fallback }
pub fn pixels(value,fallback=0.0) => num(value,fallback*9525.0)/9525.0
pub fn truth(value) => value in ["1","true","on"]
pub fn safe_link(value) => office.safe_link(value)
pub fn css(values) => join([for (key,value in values where value!=null) string(key) ++ ":" ++ string(value) ++ ";"],"")
pub fn px(value) => string(value) ++ "px"
