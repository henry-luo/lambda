// Expanded XML names share namespace identity across document packages (S8.2.2v6).
pub let XML = "http://www.w3.org/XML/1998/namespace"
pub fn bindings(node, inherited = {}) => {*:inherited,
    *:map([for (key,value in map(node) where string(key) == "xmlns" or
        starts_with(string(key),"xmlns:")) (key,value)]), ["xmlns:xml"]:XML}

pub fn expanded(qname, scope, attribute = false) {
    let parts = split(string(qname),":")
    let key = if (len(parts) == 1) "xmlns" else "xmlns:" ++ parts[0]
    let uri = if (len(parts) == 1 and attribute) "" else scope[key];
    {local:parts[len(parts)-1], uri:if (uri == null and len(parts) == 1) "" else uri}
}

pub fn attributes(node, scope) => map([for (key,value in map(node)
    where string(key) != "xmlns" and not starts_with(string(key),"xmlns:"),
    let expanded_name = expanded(key,scope,true)) (expanded_name.uri ++ "#" ++ expanded_name.local,value)])

pub fn elements(node) => if (not (node is element)) [] else
    [for (child in content(node) where child is element and
        not starts_with(string(name(child)),"?") and not starts_with(string(name(child)),"!")) child]

pub fn root(document) => if (document is element and string(name(document)) == "document") {
    let roots = elements(document);
    if (len(roots) == 1) roots[0] else null
} else document

pub fn text(node) => if (node is string) node else if (node is element) text(content(node))
    else if (node is array or node is list) join([for (child in node) text(child)],"") else ""
