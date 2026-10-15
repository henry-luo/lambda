// The transformation uses value-owned occurrence records, never parent pointers (S10.4.3v2).
import paths: lambda.io.path
pub let DB = "http://docbook.org/ns/docbook"
pub let XLINK = "http://www.w3.org/1999/xlink"
pub let XML = "http://www.w3.org/XML/1998/namespace"
pub let XHTML = "http://www.w3.org/1999/xhtml"
pub let VERSION = "2.8.5"
pub let COMMIT = "b26a7c42445b5aae1e3e6cda866513427b2b061c"
let codes = ["invalid-source","invalid-options","invalid-namespace","duplicate-id",
    "unsupported","invalid-table","missing-resource","invalid-path"]
pub fn fail(code,message,source = null) error {
    let found = [for (i,value in codes where value == code) i];
    error({code:7700 + (if (len(found) == 0) 0 else found[0]),message:"docbook: " ++ message,file:source})
}
pub fn issue(code,message,node = null) map => {severity:"warning",code:code,message:message,
    source_uri:node.source_uri,node_path:node.key,line:null,column:null}
pub fn get(value,key,fallback = null) => if (value[key] == null) fallback else value[key]
pub fn first(values) => if (len(values) == 0) null else values[0]
pub fn attr(node,key,uri = "") => node.attrs[uri ++ "#" ++ key]
pub fn children(node,tag = null) => [for (child in node.children where child is map and
    child.uri == DB and (tag == null or child.tag == tag)) child]
pub fn child(node,tag) => first(children(node,tag))
pub fn text(value) => if (value is string) value else if (value is array or value is list)
    join([for (node in value) text(node)],"") else if (value is map) text(value.children) else ""
pub fn title(node) => get(child(child(node,"info"),"title"),"children",
    get(child(node,"title"),"children",[]))
pub fn words(value) => [for (part in split(replace(replace(replace(get({v:value},"v",""),
    "\r"," "),"\n"," "),"\t"," ")," ") where part != "") part]
pub fn normalized(value) => join(words(text(value))," ")
pub fn nodes(node) => if (not (node is map)) [] else
    [node,*[for (child in node.children, found in nodes(child)) found]]
pub fn dictionary(pairs) => map([for (pair in pairs) (pair.key,pair.value)])
pub fn is_structure(node) => node.uri == DB and (node.tag in
    ["set","book","part","article","chapter","appendix","preface","dedication","colophon",
     "acknowledgements","section","sect1","sect2","sect3","sect4","sect5","simplesect","reference",
     "refentry","refsection","refsect1","refsect2","refsect3"])
pub fn is_section(node) => node.uri == DB and (node.tag in
    ["section","sect1","sect2","sect3","sect4","sect5","simplesect","refsection","refsect1","refsect2","refsect3"])
pub fn classes(values) => join(sort(unique([for (value in values,part in words(value)) part]))," ")
pub fn resolve_uri(base,relative) => if (contains(relative,"://")) relative
    else if (contains(base,"://")) url_resolve(base,relative)
    else {
        let resolved = paths.resolve_path(if (ends_with(base,"/")) base else paths.dirname(base),relative);
        resolved ++ if (ends_with(relative,"/")) "/" else ""
    }
