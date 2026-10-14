// Conversion interprets captured OOXML data; no input becomes executable source (S1.8).
import c: .common
import package: .package
import styles: .styles
import media: lambda.doc.media
import charts: .charts
import drawing: .drawing
import presentation: .presentation

fn result(nodes = [], diagnostics = []) => {nodes:nodes,diagnostics:diagnostics}
fn warning(pkg, code, message) map^ => c.issue(code,pkg.main,message)

// sdtContent is the saved display cache; control properties and end-marker formatting are metadata.
fn unwrap_controls(nodes, pkg) array^ => [for (node in nodes)
    if (node.uri == c.W and node.tag == "sdt") {
        let cached = c.children(node,"sdtContent")
        let invalid = any([for (child in c.children(node,null,null)) child.uri != c.W or
            not contains(["sdtPr","sdtEndPr","sdtContent"],child.tag)]);
        if (len(cached) != 1 or invalid) node
        else *unwrap_controls(c.children(cached[0],null,null),pkg)^
    } else node]

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

fn picture(node, pkg, options, width, height, properties) map^ {
    let images = c.descendants(node,"blip",c.A)
    let blip = c.first(images)
    let rel = package.by_id(pkg.relationships,c.get(blip.attrs,c.R ++ "#embed",c.attr(blip,"link",c.R)))
    let mime = if (rel == null or rel.external) null else package.content_type(pkg,rel.target)
    let extra = if (len(images) > 1 or len(c.descendants(node,"txbxContent",c.W)) > 0)
        [warning(pkg,"unsupported-drawing-content","Only the first raster image in the drawing is supported")^] else [];
    if (blip == null) result([], [warning(pkg,"unsupported-drawing","Drawing has no supported raster image")^])
    else if (rel.external or c.attr(blip,"link",c.R) != null) result([], [warning(pkg,"external-image","External image was not loaded")^])
    else if (rel == null or rel.type != c.R ++ "/image") result([], [warning(pkg,"missing-image","Unresolved image relationship")^])
    else if (options.images == "omit") result([], [warning(pkg,"omitted-image","Image omitted by conversion options")^])
    else if (not contains(["image/png","image/jpeg","image/gif","image/bmp","image/tiff","image/webp"],mime))
        result([], [warning(pkg,"unsupported-image-type","Image type is outside the raster profile")^])
    else if (pkg.parts[rel.target] == null) result([], [warning(pkg,"missing-image","Image part is missing")^])
    else {
        let bytes = input(pkg.parts[rel.target],"binary")^;
        if (bytes == null) result([], [warning(pkg,"missing-image","Image part is empty")^]) else
            result([{kind:"image",src:media.data_uri(bytes,mime)^,
                alt:c.get(properties.attrs,"#descr",c.get(properties.attrs,"#title","")),
                width:if (width > 0) width else null,height:if (height > 0) height else null}],extra)
    }
}

fn drawing_node(node, pkg, options) map^ {
    let extent = c.first(c.descendants(node,"extent",c.WP))
    let width = c.pixels(c.attr(extent,"cx",""))
    let height = c.pixels(c.attr(extent,"cy",""))
    let properties = c.first(c.descendants(node,"docPr",c.WP))
    let references = c.descendants(node,"chart",c.CH)
    let reference = c.first(references)
    let contents = if (reference != null) charts.read(pkg,reference,width,height)^
        else picture(node,pkg,options,width,height,properties)^
    let placement = drawing.placement(node,width);
    result([for (value in contents.nodes) {*:value,placement:placement.value}],
        contents.diagnostics ++ (if (reference != null and (len(references) > 1 or
            len(c.descendants(node,"blip",c.A)) > 0 or len(c.descendants(node,"txbxContent",c.W)) > 0))
            [warning(pkg,"unsupported-drawing-content","Only the first chart in the drawing is supported")^] else []) ++
        (if (placement.unsupported)
            [warning(pkg,"unsupported-floating-image","Drawing position or wrapping is outside the paragraph-relative profile")^] else []))
}

fn inline_many(nodes, pkg, sheet, marks, options) map^ {
    let results = [for (node in unwrap_controls(nodes,pkg)^) inline_node(node,pkg,sheet,marks,options)^];
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
    else if (node.uri == c.W and node.tag == "drawing") drawing_node(node,pkg,options)^
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
    let paragraph_mark = styles.run(sheet,resolved.run,c.child(properties,"rPr"))^
    let contents = inline_many([for (child in c.children(node,null,null)
        where not (child.uri == c.W and child.tag == "pPr")) child],pkg,sheet,resolved.run,options)^;
    result([{kind:"paragraph",tag:tag.tag,attrs:tag.attrs,children:contents.nodes,list:numbering.value,
        props:resolved.props,run:resolved.run,empty_run:paragraph_mark.props,
        break_before:resolved.props.break_before == true,section_after:c.child(properties,"sectPr") != null}],
        resolved.diagnostics ++ numbering.diagnostics ++ contents.diagnostics)
}
fn row(node, pkg, sheet, options) map^ {
    let children = unwrap_controls(c.children(node,null,null),pkg)^
    let cells = [for (cell in children where cell.uri == c.W and cell.tag == "tc",
        let properties = c.child(cell,"tcPr"), let merge = c.child(properties,"vMerge"),
        let blocks = block_many(c.children(cell,null,null),pkg,sheet,options)^)
        {span:max(1,c.as_int(c.attr(c.child(properties,"gridSpan"),"val"),1)),
            props:presentation.cell_props(properties,sheet.theme),
            merge:if (merge == null) null else c.get(merge.attrs,c.W ++ "#val","continue"),
            blocks:blocks.nodes,diagnostics:blocks.diagnostics ++
                (if (c.child(properties,"hMerge") == null) [] else [warning(pkg,"unsupported-horizontal-merge","Legacy hMerge requires gridSpan conversion")^])}]
    let before = c.as_int(c.attr(c.child(c.child(node,"trPr"),"gridBefore"),"val"),0);
    {header:c.on(c.child(c.child(node,"trPr"),"tblHeader")),
        height:c.pixels(c.attr(c.child(c.child(node,"trPr"),"trHeight"),"val"),15.0),
        cells:[for (i,cell in cells) {*:cell,column:before+sum([for (j,prior in cells where j < i) prior.span])}],
        diagnostics:c.collect(cells,"diagnostics") ++ [for (child in children
            where child.uri != c.W or not contains(["trPr","tc"],child.tag))
            warning(pkg,"unsupported-element","Unsupported table-row child " ++ child.tag)^]}
}
fn block_many(nodes, pkg, sheet, options) map^ {
    let results = [for (node in unwrap_controls(nodes,pkg)^) block(node,pkg,sheet,options)^];
    result(c.collect(results,"nodes"),c.collect(results,"diagnostics"))
}
fn block(node, pkg, sheet, options) map^ {
    if (node.uri == c.W and node.tag == "p") paragraph(node,pkg,sheet,options)^
    else if (node.uri == c.W and node.tag == "tbl") {
        let children = unwrap_controls(c.children(node,null,null),pkg)^
        let resolved = styles.table(sheet,c.child(node,"tblPr"))^
        let rows = [for (child in children where child.uri == c.W and child.tag == "tr") row(child,pkg,resolved.sheet,options)^]
        let grid = [for (col in c.children(c.child(node,"tblGrid"),"gridCol")) c.pixels(c.attr(col,"w"),15.0)];
        result([{kind:"table",rows:rows,props:resolved.props,grid:grid}],resolved.diagnostics ++ c.collect(rows,"diagnostics") ++ [for (child in children
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
    let defaults = styles.paragraph(sheet,null)^
    let contents = block_many(c.children(pkg.body,null,null),pkg,sheet,options)^;
    {blocks:contents.nodes,page_style:presentation.page_style(c.child(pkg.body,"sectPr"),defaults.run),
        diagnostics:pkg.diagnostics ++ contents.diagnostics}
}
