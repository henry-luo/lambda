// Conversion interprets captured OOXML data; no input becomes executable source (S1.8).
import c: .common
import package: .package
import styles: .styles
import media: lambda.doc.media

fn result(nodes = [], diagnostics = []) => {nodes:nodes,diagnostics:diagnostics}
fn warning(pkg, code, message) map^ => c.issue(code,pkg.main,message)

fn paragraph_tag(resolved, options) {
    let mapped = c.get(options.style_map,resolved.style_id,options.style_map[resolved.style_name])
    let level = c.as_int(resolved.props.outline,-1)
    let style_name = lower(replace(c.get(resolved,"style_name",c.get(resolved,"style_id",""))," ",""))
    let heading = if (level >= 0 and level <= 5) "h" ++ string(level+1)
        else if (starts_with(style_name,"heading") and contains(["1","2","3","4","5","6"],slice(style_name,7)))
            "h" ++ slice(style_name,7) else "p";
    if (mapped is string) {tag:mapped,attrs:{}} else if (mapped is map)
        {tag:c.get(mapped,"tag",heading),attrs:if (mapped.class == null) {} else {class:mapped.class}}
    else {tag:heading,attrs:{}}
}

fn picture(node, pkg, options) map^ {
    let images = c.descendants(node,"blip",c.A)
    let blip = c.first(images)
    let rel = package.by_id(pkg.relationships,c.get(blip.attrs,c.R ++ "#embed",c.attr(blip,"link",c.R)))
    let drawing = c.first(c.descendants(node,"docPr",c.WP))
    let extent = c.first(c.descendants(node,"extent",c.WP))
    let mime = if (rel == null or rel.external) null else package.content_type(pkg,rel.target)
    let floating = len(c.descendants(node,"anchor",c.WP)) > 0
    let placement = (if (floating) [warning(pkg,"unsupported-floating-image","Floating image is converted in source order")^] else []) ++
        (if (len(images) > 1 or len(c.descendants(node,"txbxContent",c.W)) > 0)
            [warning(pkg,"unsupported-drawing-content","Only the first raster image in the drawing is supported")^] else []);
    if (blip == null) result([], [warning(pkg,"unsupported-drawing","Drawing has no supported raster image")^])
    else if (rel.external or c.attr(blip,"link",c.R) != null) result([], [warning(pkg,"external-image","External image was not loaded")^])
    else if (rel == null or rel.type != c.R ++ "/image") result([], [warning(pkg,"missing-image","Unresolved image relationship")^])
    else if (options.images == "omit") result([], [warning(pkg,"omitted-image","Image omitted by conversion options")^])
    else if (not contains(["image/png","image/jpeg","image/gif","image/bmp","image/tiff","image/webp"],mime))
        result([], [warning(pkg,"unsupported-image-type","Image type is outside the raster profile")^])
    else if (pkg.parts[rel.target] == null) result([], [warning(pkg,"missing-image","Image part is missing")^])
    else {
        let bytes = input(pkg.parts[rel.target],"binary")^
        let width = c.as_int(c.attr(extent,"cx",""),0)/9525.0
        let height = c.as_int(c.attr(extent,"cy",""),0)/9525.0;
        if (bytes == null) result([], [warning(pkg,"missing-image","Image part is empty")^]) else
            result([{kind:"image",src:media.data_uri(bytes,mime)^,
                alt:c.get(drawing.attrs,"#descr",c.get(drawing.attrs,"#title","")),
                width:if (width > 0) width else null,height:if (height > 0) height else null}],placement)
    }
}

fn inline_many(nodes, pkg, sheet, marks, options) map^ {
    let results = [for (node in nodes) inline_node(node,pkg,sheet,marks,options)^];
    result(c.collect(results,"nodes"),c.collect(results,"diagnostics"))
}
fn inline_node(node, pkg, sheet, marks, options) map^ {
    if (node is string) result() else if (node.uri == c.W and node.tag == "r") {
        let resolved = styles.run(sheet,marks,c.child(node,"rPr"))^
        let contents = inline_many(c.children(node,null,null),pkg,sheet,resolved.props,options)^;
        result(contents.nodes,resolved.diagnostics ++ contents.diagnostics)
    } else if (node.uri == c.W and node.tag == "rPr") result()
    else if (node.uri == c.W and contains(["t","delText"],node.tag))
        result([{kind:"text",text:c.text(node),marks:marks}],
            if (node.tag == "delText") [warning(pkg,"unsupported-revision","Deleted source text retained without revision semantics")^] else [])
    else if (node.uri == c.W and node.tag == "tab") result([{kind:"text",text:"\t",marks:marks}])
    else if (node.uri == c.W and contains(["br","cr"],node.tag))
        result([{kind:if (c.attr(node,"type") == "page") "page-break" else "break"}])
    else if (node.uri == c.W and node.tag == "noBreakHyphen") result([{kind:"text",text:"‑",marks:marks}])
    else if (node.uri == c.W and node.tag == "softHyphen") result([{kind:"text",text:"­",marks:marks}])
    else if (node.uri == c.W and node.tag == "hyperlink") {
        let contents = inline_many(c.children(node,null,null),pkg,sheet,marks,options)^
        let rel = package.by_id(pkg.relationships,c.attr(node,"id",c.R))
        let anchor = c.attr(node,"anchor")
        let href = if (anchor != null) "#" ++ options.id_prefix ++ anchor
            else if (rel != null and rel.type == c.R ++ "/hyperlink") rel.target else null;
        if (c.safe_link(href)) result([{kind:"link",href:href,children:contents.nodes}],contents.diagnostics)
        else result(contents.nodes,contents.diagnostics ++ [warning(pkg,"unsafe-or-missing-link","Link target is absent or outside the supported schemes")^])
    } else if (node.uri == c.W and node.tag == "bookmarkStart") {
        let id = c.attr(node,"name");
        if (id == null) result() else result([{kind:"bookmark",id:options.id_prefix ++ id}])
    } else if (node.uri == c.W and contains(["bookmarkEnd","proofErr","lastRenderedPageBreak","permStart","permEnd"],node.tag)) result()
    else if (node.uri == c.W and node.tag == "drawing") picture(node,pkg,options)^
    else if (node.uri == c.MC and node.tag == "AlternateContent") {
        // choose a single representation, so fallback material never duplicates the choice.
        let branch = c.child(node,"Fallback",c.MC)
        let chosen = if (branch != null) branch else c.child(node,"Choice",c.MC)
        let contents = inline_many(c.children(chosen,null,null),pkg,sheet,marks,options)^;
        result(contents.nodes,contents.diagnostics ++ [warning(pkg,"unsupported-alternate-content","Selected one compatibility representation")^])
    } else {
        let contents = inline_many(c.children(node,null,null),pkg,sheet,marks,options)^
        let leaf_text = if (len(c.children(node,null,null)) == 0) c.text(node) else "";
        result(contents.nodes ++ (if (leaf_text == "" or node.tag == "instrText") [] else [{kind:"text",text:leaf_text,marks:marks}]),
            [warning(pkg,"unsupported-element","Unsupported inline element {" ++ node.uri ++ "}" ++ node.tag)^] ++ contents.diagnostics)
    }
}

fn paragraph(node, pkg, sheet, options) map^ {
    let properties = c.child(node,"pPr")
    let resolved = styles.paragraph(sheet,properties)^
    let numbering = styles.list_info(sheet,resolved.props)
    let tag = paragraph_tag(resolved,options)
    let contents = inline_many([for (child in c.children(node,null,null)
        where not (child.uri == c.W and child.tag == "pPr")) child],pkg,sheet,resolved.run,options)^;
    result([{kind:"paragraph",tag:tag.tag,attrs:tag.attrs,children:contents.nodes,list:numbering.value,
        break_before:resolved.props.break_before == true,section_after:c.child(properties,"sectPr") != null}],
        resolved.diagnostics ++ numbering.diagnostics ++ contents.diagnostics)
}
fn row(node, pkg, sheet, options) map^ {
    let cells = [for (cell in c.children(node,"tc"),
        let properties = c.child(cell,"tcPr"), let merge = c.child(properties,"vMerge"),
        let blocks = block_many(c.children(cell,null,null),pkg,sheet,options)^)
        {span:max(1,c.as_int(c.attr(c.child(properties,"gridSpan"),"val"),1)),
            merge:if (merge == null) null else c.get(merge.attrs,c.W ++ "#val","continue"),
            blocks:blocks.nodes,diagnostics:blocks.diagnostics ++
                (if (c.child(properties,"hMerge") == null) [] else [warning(pkg,"unsupported-horizontal-merge","Legacy hMerge requires gridSpan conversion")^])}]
    let before = c.as_int(c.attr(c.child(c.child(node,"trPr"),"gridBefore"),"val"),0);
    {header:c.on(c.child(c.child(node,"trPr"),"tblHeader")),
        cells:[for (i,cell in cells) {*:cell,column:before+sum([for (j,prior in cells where j < i) prior.span])}],
        diagnostics:c.collect(cells,"diagnostics") ++ [for (child in c.children(node,null,null)
            where child.uri != c.W or not contains(["trPr","tc"],child.tag))
            warning(pkg,"unsupported-element","Unsupported table-row child " ++ child.tag)^]}
}
fn block_many(nodes, pkg, sheet, options) map^ {
    let results = [for (node in nodes) block(node,pkg,sheet,options)^];
    result(c.collect(results,"nodes"),c.collect(results,"diagnostics"))
}
fn block(node, pkg, sheet, options) map^ {
    if (node.uri == c.W and node.tag == "p") paragraph(node,pkg,sheet,options)^
    else if (node.uri == c.W and node.tag == "tbl") {
        let rows = [for (child in c.children(node,"tr")) row(child,pkg,sheet,options)^];
        result([{kind:"table",rows:rows}],c.collect(rows,"diagnostics") ++ [for (child in c.children(node,null,null)
            where child.uri != c.W or not contains(["tblPr","tblGrid","tr"],child.tag))
            warning(pkg,"unsupported-element","Unsupported table child " ++ child.tag)^])
    } else if (node.uri == c.W and contains(["sectPr","tcPr"],node.tag)) result()
    else {
        let nested = block_many(c.children(node,null,null),pkg,sheet,options)^;
        result(nested.nodes,[warning(pkg,"unsupported-element","Unsupported block element {" ++ node.uri ++ "}" ++ node.tag)^] ++ nested.diagnostics)
    }
}
pub fn build(pkg, options) map^ {
    let sheet = styles.prepare(pkg)^
    let contents = block_many(c.children(pkg.body,null,null),pkg,sheet,options)^;
    {blocks:contents.nodes,diagnostics:pkg.diagnostics ++ contents.diagnostics}
}
