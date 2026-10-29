// Expanded XML names share namespace identity across document packages (S8.2.2v6).
pub let XML = "http://www.w3.org/XML/1998/namespace"
pub fn bindings(node, inherited = {}) => {*:inherited,
    *:map([for (key,value in map(node) where string(key) == "xmlns" or
        starts_with(string(key),"xmlns:")) (key,value)]), ["xmlns:xml"]:XML}

pub fn expanded(qname, scope, attribute = false) {
    let parts = split(string(qname),":")
    let key = if (len(parts) == 1) "xmlns" else "xmlns:" ++ parts[0]
    let local = parts[len(parts)-1]
    // parsed names already carry resolved identity; lexical scope supplies candidate URIs.
    let resolved = [for (binding,uri in scope where uri != "" and
        qname == symbol(local,uri)) uri]
    let uri = if (len(resolved)>0) resolved[0]
        else if (len(parts) == 1 and attribute) "" else scope[key];
    {local:parts[len(parts)-1], uri:if (uri == null and len(parts) == 1) "" else uri}
}

pub fn attributes(node, scope) => map([for (key,value in map(node)
    where string(key) != "xmlns" and not starts_with(string(key),"xmlns:"),
    let expanded_name = expanded(key,scope,true)) (expanded_name.uri ++ "#" ++ expanded_name.local,value)])

pub fn elements(node) => if (not (node is element)) [] else
    [for (child in content(node) where child is element and
        not starts_with(string(name(child)),"?") and not starts_with(string(name(child)),"!") and
        not starts_with(string(name(child)),"#")) child]

pub fn root(document) => if (document is element and string(name(document)) in ["document","#document"]) {
    let roots = elements(document);
    if (len(roots) == 1) roots[0] else null
} else document

pub fn text(node) => if (node is string) node else if (node is element) text(content(node))
    else if (node is array or node is list) join([for (child in node) text(child)],"") else ""

fn identity(uri) => uri
// Ordered normalized XML is shared by document adapters; namespace aliases remain caller policy.
pub fn tree(node, inherited = {}, canonical = identity) {
    if (node is string) node else if (not (node is element)) null else {
        let scope = bindings(node,inherited)
        let tag = expanded(name(node),scope);
        {tag:tag.local,uri:canonical(tag.uri),
            attrs:map([for (key,value in map(node) where string(key) != "xmlns" and
                not starts_with(string(key),"xmlns:"),let n = expanded(key,scope,true))
                (canonical(n.uri) ++ "#" ++ n.local,value)]),
            children:[for (child in content(node) where child is string or (child is element and
                not starts_with(string(name(child)),"?") and not starts_with(string(name(child)),"!") and
                not starts_with(string(name(child)),"#")))
                tree(child,scope,canonical)]}
    }
}

// Normalized tree access is shared by Office packages; a null namespace matches any URI.
pub fn children(node, tag=null, ns=null) => [for (child in node.children
    where child is map and (tag==null or child.tag==tag) and (ns==null or child.uri==ns)) child]
pub fn child(node, tag, ns=null) {
    let found = children(node,tag,ns);
    if (len(found)==0) null else found[0]
}
pub fn attr(node, key, ns="") => node.attrs[ns ++ "#" ++ key]
pub fn tree_text(node) => if (node is string) node else if (node is array)
    join([for (child in node) tree_text(child)],"") else if (node is map) tree_text(node.children) else ""

// Lexical model adapters may choose any declared alias for the same resolved URI.
pub fn lexical_name(qname, scope, attribute = false) {
    let local = string(qname)
    let matches = [for (key,uri in scope where uri != "" and
        (not attribute or string(key) != "xmlns") and qname == symbol(local,uri)) string(key)];
    if (len(matches)==0) qname else symbol(
        if (matches[0]=="xmlns") local else slice(matches[0],6,len(matches[0])) ++ ":" ++ local)
}
