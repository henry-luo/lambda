// Word-specific validation builds on the shared OPC capture (D7.2.4v4/D7.5.2).
import c: .common
import opc: lambda.doc.opc
pub fn xml(parts,path,required=false) any^ => opc.xml(parts,path,required,c.fail,c.uri)^
pub fn relationships(parts,owner,required=false) array^ => opc.relationships(parts,owner,required,c.fail,c.uri)^
pub fn related(rels,kind) => opc.related(rels,kind)
pub fn by_id(rels,id) => opc.by_id(rels,id)
pub fn related_xml(pkg,kind) any^ => opc.related_xml(pkg,kind,c.fail,c.uri)^
pub fn content_type(pkg,path) => opc.content_type(pkg,path)
// each annotation part owns its relationships; IDs such as rId1 are local to that part.
fn stories(pkg) map^ => map([for (kind in ["footnote","endnote","comment"],
    let rel = related(pkg.relationships,kind ++ "s") where rel != null)
    (kind,{
        let root = related_xml(pkg,kind ++ "s")^;
        if (root.uri != c.W or root.tag != kind ++ "s")
            raise c.fail("invalid-package","Invalid " ++ kind ++ "s root",rel.target)
        else {
            let nodes = c.children(root,kind)
            let invalid = any([for (i,node in nodes, let id = c.attr(node,"id"))
                id == null or c.as_int(id,null) == null or
                any([for (j,prior in nodes where j < i) c.attr(prior,"id") == id])]);
            if (invalid) raise c.fail("invalid-package","Missing or duplicate annotation ID",rel.target)
            else {part:rel.target,relationships:relationships(pkg.parts,rel.target)^,
                nodes:c.dictionary([for (node in nodes where
                    not contains(["separator","continuationSeparator"],c.attr(node,"type")))
                    {key:c.attr(node,"id"),value:node}])}
        }
    })])
// saved field instructions and empty paragraph marks do not make a header/footer visible (S1.8).
fn visible_story_content(node) {
    if (node is string) trim(node) != ""
    else if (node.uri != c.W) true
    else if (contains(["t","delText"],node.tag)) c.text(node) != ""
    else if (contains(["rPr","instrText","fldChar","bookmarkStart","bookmarkEnd","proofErr",
        "lastRenderedPageBreak","permStart","permEnd","commentRangeStart","commentRangeEnd"],node.tag)) false
    else if (node.tag == "pPr") {
        let shading = c.child(node,"shd");
        any([for (border in c.children(c.child(node,"pBdr"),null,null))
            not contains(["none","nil"],c.attr(border,"val"))]) or
        (shading != null and (c.attr(shading,"themeFill") != null or
            (c.attr(shading,"fill") != null and c.attr(shading,"fill") != "auto") or
            (c.attr(shading,"val") != null and not contains(["nil","clear"],c.attr(shading,"val")))))
    } else if (contains(["hdr","ftr","p","r","fldSimple"],node.tag))
        any([for (child in node.children) visible_story_content(child)])
    else true
}
fn story_diagnostics(pkg) array^ {
    let visible = [for (rel in pkg.relationships,
        let kind = if (rel.type == c.R ++ "/header") "hdr" else if (rel.type == c.R ++ "/footer") "ftr" else null
        where kind != null)
        if (rel.external) true else {
            let root = xml(pkg.parts,rel.target,true)^;
            if (root.uri != c.W or root.tag != kind)
                raise c.fail("invalid-package","Invalid header/footer root",rel.target)
            else visible_story_content(root)
        }];
    [for (present in visible where present)
        c.issue("unsupported-story",pkg.main,"Separate document story is outside the v1 profile")]
}
pub fn open_package(source) map^ {
    let pkg = opc.capture(source,c.fail,c.uri,"DOCX")^
    let document = pkg.root;
    if (content_type(pkg,pkg.main) != "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml" or
        document.uri != c.W or document.tag != "document" or c.child(document,"body") == null)
        raise c.fail("invalid-package","Expected a DOCX WordprocessingML document",pkg.main)
    else {*:pkg,body:c.child(document,"body"),stories:stories(pkg)^,diagnostics:story_diagnostics(pkg)^}
}
