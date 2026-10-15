import c: .common

fn files(node) => [for (child in content(node)^)
    if (child.kind == 'dir') *files(child) else (child.entry_path,child)]
pub fn source_text(book,path) string^ {
    let member = book.resources[path];
    if (member == null) raise c.fail("missing-part","Missing publication resource " ++ path,path)
    else if (member is map) member.text else parse(member,"text")^
}
fn xml(book,path) map^ => c.tree(source_text(book,path)^,path)^
fn required(node,tag,ns,path) map^ {
    let values = c.children(node,tag,ns);
    if (len(values) != 1) raise c.fail("invalid-package","Expected one " ++ tag,path) else values[0]
}
fn unique_ids(nodes,part) bool^ {
    let ids = [for (node in nodes) c.attr(node,"id")];
    if (any([for (id in ids) id == null or id == ""]) or len(unique(ids)) != len(ids))
        raise c.fail("invalid-package","Missing or duplicate manifest ID",part)
    else true
}
fn fallback(entry,by_id,part,seen = []) map^ {
    if (entry.id in seen) raise c.fail("invalid-package","Manifest fallback cycle",part)
    else if (entry.media_type in ["application/xhtml+xml","image/svg+xml"]) entry
    else if (entry.fallback == null) raise c.fail("unsupported","Unsupported spine media type " ++ entry.media_type,entry.href)
    else if (by_id[entry.fallback] == null) raise c.fail("invalid-package","Missing manifest fallback ID",part)
    else fallback(by_id[entry.fallback],by_id,part,[*seen,entry.id])^
}
fn manifest_entry(item,part) map^ {
    let target = c.target(part,c.attr(item,"href"))^
    let media = c.attr(item,"media-type");
    if (media == null or media == "" or target.fragment != null)
        raise c.fail("invalid-package","Manifest requires a media type and fragment-free href",part)
    else {id:c.attr(item,"id"),href:target.href,media_type:media,
        properties:split(c.value(c.attr(item,"properties"),"")," "),fallback:c.attr(item,"fallback")}
}
fn nav_items(node,owner) array^ => [for (li in c.children(node,"li",c.XHTML),
    let link = c.child(li,"a",c.XHTML),let label = if (link != null) link else c.child(li,"span",c.XHTML))
    {label:trim(c.text(label)),target:if (link == null) null else c.target(owner,c.attr(link,"href"))^,
        children:nav_items(c.child(li,"ol",c.XHTML),owner)^}]
fn nav_tree(book,entry) map^ {
    let root = xml(book,entry.href)^
    let navs = c.descendants(root,"nav",c.XHTML);
    map([for (kind in ["toc","landmarks","page-list"],let node = c.first([for (nav in navs
        where kind in split(c.value(c.attr(nav,"type",c.EPUB),"")," ")) nav]))
        (if (kind == "page-list") "page_list" else kind,nav_items(c.child(node,"ol",c.XHTML),entry.href)^)])
}
fn ncx_items(node,owner) array^ => [for (point in c.children(node,"navPoint",c.NCX))
    {label:trim(c.text(c.child(point,"navLabel",c.NCX))),
        target:c.target(owner,c.attr(c.child(point,"content",c.NCX),"src"))^,
        children:ncx_items(point,owner)^}]
fn navigation(book,manifest,spine) map^ {
    let nav = c.first([for (entry in manifest where "nav" in entry.properties) entry])
    let ncx = c.first([for (entry in manifest where entry.id == c.attr(spine,"toc")) entry]);
    if (nav != null) nav_tree(book,nav)^
    else if (ncx != null) {toc:ncx_items(c.child(xml(book,ncx.href)^,"navMap",c.NCX),ncx.href)^,landmarks:[],page_list:[]}
    else {toc:[],landmarks:[],page_list:[]}
}
fn metadata(node) {
    let values = c.children(node,null,c.DC)
    let entries = c.children(node,"meta",c.OPF)
    let layout = c.first([for (entry in entries where c.attr(entry,"property") == "rendition:layout") c.text(entry)]);
    {titles:[for (value in values where value.tag == "title") c.text(value)],
        creators:[for (value in values where value.tag == "creator") c.text(value)],
        identifiers:[for (value in values where value.tag == "identifier") c.text(value)],
        languages:[for (value in values where value.tag == "language") c.text(value)],
        layout:if (layout == "pre-paginated") "fixed" else "reflowable"}
}
pub fn parse_archive(archive,options) map^ {
    let validation_1 = if (not (archive is element) or archive.format != 'zip' or archive.entry_path != "")
        raise c.fail("invalid-source","Expected a captured ZIP root") else true
    let book = {resources:map(files(archive))}
    let mimetype_valid = if (source_text(book,"mimetype")^ != "application/epub+zip")
        raise c.fail("invalid-package","Invalid EPUB mimetype","mimetype") else true
    let container = xml(book,"META-INF/container.xml")^;
    let validation_2 = if (container.tag != "container" or container.uri != c.OCF)
        raise c.fail("invalid-package","Invalid EPUB container","META-INF/container.xml") else true
    let roots = c.children(c.child(container,"rootfiles",c.OCF),"rootfile",c.OCF)
    let candidates = [for (root in roots where c.attr(root,"media-type") == "application/oebps-package+xml") root]
    let selected = c.get(options,"rendition",0);
    let validation_3 = if (selected < 0 or selected >= len(candidates)) raise c.fail("invalid-package","Missing selected EPUB package") else true
    let part = c.target("",c.attr(candidates[selected],"full-path"))^.href
    let root = xml(book,part)^;
    let validation_4 = if (root.tag != "package" or root.uri != c.OPF) raise c.fail("invalid-package","Invalid OPF package",part) else true
    let version = c.attr(root,"version");
    let validation_5 = if (not (starts_with(version,"2.") or starts_with(version,"3.")))
        raise c.fail("unsupported","Unsupported EPUB package version",part) else true
    let meta = metadata(required(root,"metadata",c.OPF,part)^)
    let items = c.children(required(root,"manifest",c.OPF,part)^,"item",c.OPF)
    let checked = unique_ids(items,part)^
    let manifest = [for (item in items) manifest_entry(item,part)^]
    let by_id = map([for (entry in manifest) (entry.id,entry)])
    let spine = required(root,"spine",c.OPF,part)^
    let order = [for (i,item in c.children(spine,"itemref",c.OPF),let original = by_id[c.attr(item,"idref")],
        let entry = if (original == null) null else fallback(original,by_id,part)^)
        if (entry == null) raise c.fail("invalid-package","Spine references a missing manifest ID",part)
        else if (book.resources[entry.href] == null) raise c.fail("missing-part","Missing spine resource",entry.href)
        else {*:entry,source_href:original.href,occurrence:i,linear:c.attr(item,"linear") != "no",
            layout:if (entry.media_type == "image/svg+xml") "fixed" else if ("rendition:layout-pre-paginated" in split(c.value(c.attr(item,"properties"),"")," ")) "fixed"
                else if ("rendition:layout-reflowable" in split(c.value(c.attr(item,"properties"),"")," ")) "reflowable" else meta.layout}];
    let validation_6 = if (len(order) == 0) raise c.fail("invalid-package","Empty reading order",part) else true
    let encryption = if (book.resources["META-INF/encryption.xml"] == null) null else xml(book,"META-INF/encryption.xml")^
    let encrypted = c.descendants(encryption,"EncryptedData","http://www.w3.org/2001/04/xmlenc#")
    let restrictions = [for (entry in encrypted,let method = c.child(entry,"EncryptionMethod"),
        let ref = c.child(c.child(entry,"CipherData"),"CipherReference"))
        {href:c.target("",c.attr(ref,"URI"))^.href,algorithm:c.attr(method,"Algorithm")}]
    let protected_text = [for (entry in restrictions where
        any([for (item in order) item.href == entry.href])) entry];
    let validation_7 = if (len(protected_text) > 0) raise c.fail("protected","Required book content is encrypted",protected_text[0].href) else true
    let cover_id = c.first([for (entry in c.children(c.child(root,"metadata",c.OPF),"meta",c.OPF)
        where c.attr(entry,"name") == "cover") c.attr(entry,"content")])
    let cover = c.first([for (entry in manifest where "cover-image" in entry.properties or entry.id == cover_id) entry]);
    {*:book,version:1,format:"epub",source_identity:archive.sha256,package:part,rendition:selected,metadata:{*:meta,cover:cover},
        reading_order:order,manifest:manifest,restrictions:restrictions,
        navigation:navigation(book,manifest,spine)^,progression:c.value(c.attr(spine,"page-progression-direction"),"ltr"),
        diagnostics:[for (entry in restrictions) c.issue("obfuscated-or-encrypted-resource",entry.href,
            "Resource protection requires a supported decoder; font fallback is used")],
        capabilities:{scrolling:true,pagination:true,fixed_layout:true,profile:"horizontal-ltr",spreads:false,cfi:false,font_obfuscation:false}}
}
