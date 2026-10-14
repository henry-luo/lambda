// Source capture uses the existing bounded ZIP reader (S12.4.1v2/S14.3.1v2).
import c: .common

fn files(node) array^ => [for (child in content(node)^)
    if (child.kind == 'dir') *files(child)^ else {key:child.entry_path,value:child}]

pub fn xml(parts, path, required = false) any^ {
    if (parts[path] == null) {
        if (required) raise c.fail("missing-part","Missing package part " ++ path,path) else null
    } else {
        // acquire bytes before the XML error boundary, preserving ZIP quota/CRC failures.
        let source = input(parts[path],"text")^
        let parsed = parse(source,{type:"xml",flavor:"document"}) ^ {
            raise c.fail("invalid-xml","Invalid XML in " ++ path,path)
        }
        let roots = [for (node in content(parsed) where node is element and
            not starts_with(string(name(node)),"?") and not starts_with(string(name(node)),"!")) node];
        if (len(roots) != 1) raise c.fail("invalid-xml","Expected one XML root",path)
        else c.xml_node(roots[0])
    }
}

fn relationships(parts, owner, required = false) array^ {
    let path = if (owner == "") "_rels/.rels" else c.rels_path(owner)
    let root = xml(parts,path,required)^;
    if (root == null) [] else if (root.tag != "Relationships" or root.uri != c.P)
        raise c.fail("invalid-package","Invalid relationships root",path)
    else {
        let nodes = c.children(root,"Relationship",c.P)
        let invalid = [for (i,node in nodes, let id = c.attr(node,"Id",""),
            let mode = c.attr(node,"TargetMode",""), let kind = c.attr(node,"Type",""),
            let value = c.attr(node,"Target","")
            where id == null or id == "" or kind == null or value == null or value == "" or
                (mode != null and not contains(["Internal","External"],mode)) or
                any([for (j,prior in nodes where j < i and c.attr(prior,"Id","") == id) true])) node];
        if (len(invalid) > 0) raise c.fail("invalid-package","Missing or duplicate relationship attributes",path)
        else [for (node in nodes, let external = c.attr(node,"TargetMode","") == "External")
            {id:c.attr(node,"Id",""),type:c.uri(c.attr(node,"Type","")),external:external,
                target:if (external) c.attr(node,"Target","") else c.target(owner,c.attr(node,"Target",""))^}]
    }
}
pub fn related(rels, kind) => c.first([for (rel in rels where rel.type == c.R ++ "/" ++ kind) rel])
pub fn by_id(rels, id) => c.first([for (rel in rels where rel.id == id) rel])
pub fn related_xml(pkg, kind) any^ {
    let rel = related(pkg.relationships,kind);
    if (rel == null) null else if (rel.external) raise c.fail("invalid-package","External " ++ kind ++ " part",pkg.main)
    else xml(pkg.parts,rel.target,true)^
}
pub fn content_type(pkg, path) {
    let explicit = pkg.types.overrides[path]
    let parts = split(path,".");
    if (explicit != null) explicit else pkg.types.defaults[lower(parts[len(parts)-1])]
}
fn types(root) map^ {
    if (root.tag != "Types" or root.uri != c.CT) raise c.fail("invalid-package","Invalid content-types root","[Content_Types].xml")
    else {
        let defaults = [for (node in c.children(root,"Default",c.CT))
            {key:lower(c.attr(node,"Extension","")),value:c.attr(node,"ContentType","")}]
        let overrides = [for (node in c.children(root,"Override",c.CT))
            {key:c.target("",c.attr(node,"PartName",""))^,value:c.attr(node,"ContentType","")}]
        let invalid = any([for (entries in [defaults,overrides], i,entry in entries)
            entry.key == null or entry.key == "" or entry.value == null or entry.value == "" or
                any([for (j,prior in entries where j < i) prior.key == entry.key])]);
        if (invalid) raise c.fail("invalid-package","Missing or duplicate content-type entries","[Content_Types].xml")
        else {defaults:c.dictionary(defaults),overrides:c.dictionary(overrides)}
    }
}
fn metadata(parts, rels) map^ {
    let rel = c.first([for (r in rels where ends_with(r.type,"/metadata/core-properties")) r]);
    if (rel == null) {} else if (rel.external) raise c.fail("invalid-package","External core-properties part")
    else {
        let root = xml(parts,rel.target,true)^
        let ns = "http://schemas.openxmlformats.org/package/2006/metadata/core-properties";
        if (root.tag != "coreProperties" or root.uri != ns) raise c.fail("invalid-package","Invalid core-properties root",rel.target)
        else map([for (node in c.children(root,null,null)
            where (node.uri == "http://purl.org/dc/elements/1.1/" and
                contains(["title","subject","creator","description","language"],node.tag)) or
                (node.uri == ns and contains(["keywords","lastModifiedBy","revision"],node.tag)) or
                (node.uri == "http://purl.org/dc/terms/" and contains(["created","modified"],node.tag)))
            (node.tag,c.text(node))])
    }
}
fn story_diagnostics(pkg) array^ => [for (rel in pkg.relationships,
    let kind = c.first(reverse(split(rel.type,"/")))
    where contains(["header","footer","footnotes","endnotes","comments"],kind))
    *{
        // Word writes note separator definitions even when the document has no actual notes.
        let notes = contains(["footnotes","endnotes"],kind) and not rel.external
        let root = if (notes) xml(pkg.parts,rel.target,true)^ else null
        let note_tag = if (kind == "footnotes") "footnote" else "endnote";
        if (notes and (root.uri != c.W or root.tag != kind))
            raise c.fail("invalid-package","Invalid " ++ kind ++ " root",rel.target)
        else if (notes and all([for (node in c.children(root,null,null))
            node.uri == c.W and node.tag == note_tag and
            contains(["separator","continuationSeparator"],c.attr(node,"type"))])) []
        else [c.issue("unsupported-story",pkg.main,"Separate document story is outside the v1 profile")]
    }]
pub fn open_package(source) map^ {
    let archive = if (source is string) {
        if (contains(source,":") or starts_with(source,"//")) raise c.fail("invalid-source","A local DOCX path is required")
        else input(source,"zip")^
    } else if (source is element and source.format == 'zip' and source.entry_path == "") source
    else raise c.fail("invalid-source","Expected a local path or ZIP file-tree root")
    let parts = c.dictionary(files(archive)^)
    let content_types = types(xml(parts,"[Content_Types].xml",true)^)^
    let root_rels = relationships(parts,"",true)^
    let main_rels = [for (rel in root_rels where rel.type == c.R ++ "/officeDocument") rel]
    let main = c.first(main_rels);
    if (len(main_rels) != 1 or main.external) raise c.fail("invalid-package","Expected one internal main-document relationship")
    else {
        let pkg = {parts:parts,types:content_types,main:main.target,
            relationships:relationships(parts,main.target)^}
        let document = xml(parts,main.target,true)^;
        if (content_type(pkg,main.target) != "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml" or
            document.uri != c.W or document.tag != "document" or c.child(document,"body") == null)
            raise c.fail("invalid-package","Expected a DOCX WordprocessingML document",main.target)
        else {*:pkg,body:c.child(document,"body"),metadata:metadata(parts,root_rels)^,
            diagnostics:story_diagnostics(pkg)^}
    }
}
