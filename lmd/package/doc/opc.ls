// Shared OPC capture keeps DOCX/PPTX on one bounded ZIP/XML and URI path (D7.5.2).
import x: lambda.doc.xml
import uri: lambda.doc.uri
pub let R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
pub let P = "http://schemas.openxmlformats.org/package/2006/relationships"
pub let CT = "http://schemas.openxmlformats.org/package/2006/content-types"
fn first(values) => values[0]
fn dictionary(pairs) => map([for (pair in pairs) (pair.key,pair.value)])
fn rels_path(part) {
    let parts = split(part,"/");
    uri.dirname(part) ++ "_rels/" ++ parts[last] ++ ".rels"
}
fn files(node) array^ => [for (child in content(node)^)
    if (child.kind == 'dir') *files(child)^ else {key:child.entry_path,value:child}]

pub fn xml(parts, path, required, failure, canonical) any^ {
    if (parts[path] == null) {
        if (required) raise failure("missing-part","Missing package part " ++ path,path) else null
    } else {
        // acquire bytes before the XML error boundary, preserving ZIP quota/CRC failures.
        let source = input(parts[path],"text")^
        let parsed = parse(source,{type:"xml",flavor:"document"}) ^ {
            raise failure("invalid-xml","Invalid XML in " ++ path,path)
        }
        let roots = [for (node in content(parsed) where node is element and
            not starts_with(string(name(node)),"?") and not starts_with(string(name(node)),"!")) node];
        if (len(roots) != 1) raise failure("invalid-xml","Expected one XML root",path)
        else x.tree(roots[0],{},canonical)
    }
}

pub fn relationships(parts, owner, required, failure, canonical) array^ {
    let path = if (owner == "") "_rels/.rels" else rels_path(owner)
    let root = xml(parts,path,required,failure,canonical)^;
    if (root == null) [] else if (root.tag != "Relationships" or root.uri != P)
        raise failure("invalid-package","Invalid relationships root",path)
    else {
        let nodes = x.children(root,"Relationship",P)
        let invalid = [for (i,node in nodes, let id = x.attr(node,"Id",""),
            let mode = x.attr(node,"TargetMode",""), let kind = x.attr(node,"Type",""),
            let value = x.attr(node,"Target","")
            where id == null or id == "" or kind == null or value == null or value == "" or
                (mode != null and not contains(["Internal","External"],mode)) or
                any([for (j,prior in nodes where j < i and x.attr(prior,"Id","") == id) true])) node];
        if (len(invalid) > 0) raise failure("invalid-package","Missing or duplicate relationship attributes",path)
        else [for (node in nodes, let external = x.attr(node,"TargetMode","") == "External")
            {id:x.attr(node,"Id",""),type:canonical(x.attr(node,"Type","")),external:external,
                target:if (external) x.attr(node,"Target","") else uri.target(owner,x.attr(node,"Target",""),failure)^}]
    }
}
pub fn related(rels, kind) => first([for (rel in rels where rel.type == R ++ "/" ++ kind) rel])
pub fn by_id(rels, id) => first([for (rel in rels where rel.id == id) rel])
pub fn related_xml(pkg, kind, failure, canonical) any^ {
    let rel = related(pkg.relationships,kind);
    if (rel == null) null else if (rel.external) raise failure("invalid-package","External " ++ kind ++ " part",pkg.main)
    else xml(pkg.parts,rel.target,true,failure,canonical)^
}
pub fn content_type(pkg, path) {
    let explicit = pkg.types.overrides[path]
    let parts = split(path,".");
    if (explicit != null) explicit else pkg.types.defaults[lower(parts[len(parts)-1])]
}
fn types(root, failure) map^ {
    if (root.tag != "Types" or root.uri != CT) raise failure("invalid-package","Invalid content-types root","[Content_Types].xml")
    else {
        let defaults = [for (node in x.children(root,"Default",CT))
            {key:lower(x.attr(node,"Extension","")),value:x.attr(node,"ContentType","")}]
        let overrides = [for (node in x.children(root,"Override",CT))
            {key:uri.target("",x.attr(node,"PartName",""),failure)^,value:x.attr(node,"ContentType","")}]
        let invalid = any([for (entries in [defaults,overrides], i,entry in entries)
            entry.key == null or entry.key == "" or entry.value == null or entry.value == "" or
                any([for (j,prior in entries where j < i) prior.key == entry.key])]);
        if (invalid) raise failure("invalid-package","Missing or duplicate content-type entries","[Content_Types].xml")
        else {defaults:dictionary(defaults),overrides:dictionary(overrides)}
    }
}
fn metadata(parts, rels, failure, canonical) map^ {
    let rel = first([for (r in rels where ends_with(r.type,"/metadata/core-properties")) r]);
    if (rel == null) {} else if (rel.external) raise failure("invalid-package","External core-properties part")
    else {
        let root = xml(parts,rel.target,true,failure,canonical)^
        let ns = "http://schemas.openxmlformats.org/package/2006/metadata/core-properties";
        if (root.tag != "coreProperties" or root.uri != ns) raise failure("invalid-package","Invalid core-properties root",rel.target)
        else map([for (node in x.children(root,null,null)
            where (node.uri == "http://purl.org/dc/elements/1.1/" and
                contains(["title","subject","creator","description","language"],node.tag)) or
                (node.uri == ns and contains(["keywords","lastModifiedBy","revision"],node.tag)) or
                (node.uri == "http://purl.org/dc/terms/" and contains(["created","modified"],node.tag)))
            (node.tag,x.tree_text(node))])
    }
}
pub fn capture(source, failure, canonical, label) map^ {
    let archive = if (source is string) {
        if (contains(source,":") or starts_with(source,"//")) raise failure("invalid-source","A local " ++ label ++ " path is required")
        else input(source,"zip")^
    } else if (source is element and source.format == 'zip' and source.entry_path == "") source
    else raise failure("invalid-source","Expected a local path or ZIP file-tree root")
    let parts = dictionary(files(archive)^)
    let content_types = types(xml(parts,"[Content_Types].xml",true,failure,canonical)^,failure)^
    let root_rels = relationships(parts,"",true,failure,canonical)^
    let main_rels = [for (rel in root_rels where rel.type == R ++ "/officeDocument") rel]
    let main = first(main_rels);
    if (len(main_rels) != 1 or main.external) raise failure("invalid-package","Expected one internal main-document relationship")
    else {parts:parts,types:content_types,main:main.target,
        root:xml(parts,main.target,true,failure,canonical)^,
        relationships:relationships(parts,main.target,false,failure,canonical)^,
        metadata:metadata(parts,root_rels,failure,canonical)^}
}
