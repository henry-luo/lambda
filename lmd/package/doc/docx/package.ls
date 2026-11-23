// Word-specific validation builds on the shared OPC capture (D9.3.4/D11.1.2).
import c: .common
import opc: lambda.doc.opc
import x: lambda.doc.xml
pub fn xml(parts,path,required=false) any^ => opc.xml(parts,path,required,c.fail,c.uri)^
pub fn relationships(parts,owner,required=false) array^ => opc.relationships(parts,owner,required,c.fail,c.uri)^
pub fn related(rels,kind) any^ => opc.related(rels,kind)
pub fn by_id(rels,id) any^ => opc.by_id(rels,id)
pub fn related_xml(pkg,kind) any^ => opc.related_xml(pkg,kind,c.fail,c.uri)^
pub fn content_type(pkg,path) any^ => opc.content_type(pkg,path)
// each annotation part owns its relationships; IDs such as rId1 are local to that part.
fn stories(pkg) map^ => map([for (kind in ["footnote","endnote","comment"],
    let rel = related(pkg.relationships,kind ++ "s") where ((rel != null) ^ { false }))
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
                    ((not contains(["separator","continuationSeparator"],c.attr(node,"type"))) ^ { false }))
                    {key:c.attr(node,"id"),value:node}])}
        }
    })])
// saved field instructions and empty paragraph marks do not make a header/footer visible (S1.8).
fn visible_story_content(node) any^ {
    if (node is string) trim(node) != ""
    else if (node.uri != c.W) true
    else if (contains(["t","delText"],node.tag)) c.text(node) != ""
    else if (contains(["rPr","instrText","fldChar","bookmarkStart","bookmarkEnd","proofErr",
        "lastRenderedPageBreak","permStart","permEnd","commentRangeStart","commentRangeEnd"],node.tag)) false
    else if (node.tag == "pPr") {
        let shading = c.child(node,"shd");
        any([for (border in ((c.children(c.child(node,"pBdr"),null,null)) ^ { [] }))
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
fn word_tag(node, scope) any^ {
    let tag = x.expanded(name(node),x.bindings(node,scope));
    if (c.uri(tag.uri)^ == c.W) tag.local else null
}

pub fn open_package(source, snapshot = false) map^ {
    let pkg = opc.capture(source,c.fail,c.uri,"DOCX",snapshot)^
    let document = pkg.root
    let scope = if (snapshot) x.bindings(document) else {}
    let tag = if (snapshot) word_tag(document,{})^ else if (document.uri == c.W) document.tag else null
    let body = if (snapshot)
        [for (child in x.elements(document)^ where word_tag(child,scope)^ == "body") child][0]
        else c.child(document,"body");
    if (content_type(pkg,pkg.main) != "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml" or
        tag != "document" or body == null)
        raise c.fail("invalid-package","Expected a DOCX WordprocessingML document",pkg.main)
    else if (snapshot) {
        let body_scope = x.bindings(body,scope)
        let blocks = x.elements(body)
        let section = blocks[last];
        // The terminal body section supplies page geometry without normalizing the body.
        {*:pkg,body:body,body_scope:body_scope,raw_blocks:blocks,
            section:if (section != null and word_tag(section,body_scope)^ == "sectPr")
                c.xml_node(section,body_scope)^ else null,
            stories:stories(pkg)^,diagnostics:story_diagnostics(pkg)^}
    } else {*:pkg,body:body,stories:stories(pkg)^,diagnostics:story_diagnostics(pkg)^}
}
