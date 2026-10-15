// Shipped source package (D7.2.4); pure conversion never acquires resources (S12.1.1v3).
import c: .docbook.common
import config: .docbook.options
import model: .docbook.model
import render: .docbook.render
import publication: .docbook.publication
import media: .docbook.media
import paths: lambda.io.path

let partial_attributes = {xref:["endterm","xrefstyle"],orderedlist:["continuation","inheritnum"],
    variablelist:["termlength"],itemizedlist:["mark"],
    colspec:["colwidth","align","char","charoff","colsep","rowsep"],
    entry:["char","charoff","valign","colsep","rowsep","rotate"],
    tgroup:["align","char","charoff","colsep","rowsep"],
    table:["frame","pgwide","orient"],informaltable:["frame","pgwide","orient"],
    imagedata:["width","depth","scale","scalefit","align","valign"]}
fn diagnostics(parsed,opts) => [for (node in parsed.nodes,issue in
    (if (starts_with(node.tag,"?"))
        [c.issue("unsupported-processing-instruction","Processing instruction is not executed or interpreted: " ++ node.tag,node)]
    else if (node.uri != c.DB and not starts_with(node.tag,"!"))
        [c.issue("unsupported-namespace","Unsupported namespace " ++ node.uri,node)]
    else if (node.uri == c.DB and not render.supported(node) and opts.customization[node.tag] == null)
        [c.issue("unsupported-element","Unsupported DocBook element " ++ node.tag,node)] else []) ++
    [for (key in c.get(partial_attributes,node.tag,[]) where c.attr(node,key) != null)
        c.issue("unsupported-attribute","Unsupported " ++ node.tag ++ " attribute " ++ key,node)] ++
    (if (node.tag in ["programlisting","screen","literallayout","synopsis","address",
        "funcsynopsisinfo","classsynopsisinfo"] and len(c.children(node)) > 0)
        [c.issue("unsupported-verbatim-markup","Verbatim markup is emitted as plain text in this profile",node)] else []) ++
    (if (node.tag == "footnote" and any([for (key in node.ancestors)
        parsed.by_key[key].tag in ["table","informaltable"]]))
        [c.issue("unsupported-table-footnote","Table-specific note placement and marks are not yet ported",node)] else []) ++
    (if (node.uri == c.DB and node.tag in ["xref","link","footnoteref"] and c.attr(node,"linkend") != null and
        parsed.ids[c.attr(node,"linkend")] == null)
        [c.issue("unresolved-reference","Unresolved ID " ++ c.attr(node,"linkend"),node)] else []) ++
    (if ((node.parent == null or c.attr(node,"lang",c.XML) != null) and
        node.language != "en" and not starts_with(node.language,"en-"))
        [c.issue("unsupported-language","Generated text currently uses the English locale",node)] else [])) issue]

pub fn convert(source,options = null) map^ {
    let opts = config.checked(options)^
    let parsed = model.analyze(model.prepare(source)^,opts)
    let assembled = publication.assemble(parsed,opts)^
    let issues = diagnostics(parsed,opts) ++ assembled.media_diagnostics;
    if (opts.unsupported == "error" and len(issues) > 0)
        raise c.fail("unsupported",issues[0].message,issues[0].source_uri)
    else {*:assembled,diagnostics:issues}
}

pub fn to_html(source,options = null) string^ {
    let result = convert(source,options)^;
    if (options.output == "chunked") raise c.fail("invalid-options","to_html requires single-document output")
    else "<!DOCTYPE html>\n" ++ format(result.document,"html")^
}

pub pn read(path,options = null) map^ {
    let opts = config.checked(options)^
    if (not (path is string) or path == "" or contains(path,"://"))
        raise c.fail("invalid-source","read expects a local XML filename")
    let document = input(path,{type:"xml",flavor:"document"})^
    let snapshot = {document:document,source_uri:path,base_uri:path,resources:{}}
    let parsed = model.prepare(snapshot)^
    var resources = {}
    for (uri in media.references(parsed)) {
        let bytes = if (contains(uri,"://")) null else input(uri,"binary") ^ { null }
        resources[uri] = {bytes:bytes,media_type:media.mime(uri)}
    }
    return {*:snapshot,resources:resources}
}

fn safe_path(path) => path is string and path != "" and not starts_with(path,"/") and
    not (".." in split(path,"/")) and not contains(path,"\\") and not contains(path,":")

pub pn write(result,destination) map^ {
    if (not (destination is string) or destination == "" or contains(destination,"://"))
        raise c.fail("invalid-path","write expects a local destination directory")
    if (not (result is map) or not (result.pages is array) or not (result.assets is array))
        raise c.fail("invalid-source","write expects a publication returned by convert")
    let entries = result.pages ++ result.assets
    if (any([for (entry in entries) not safe_path(entry.path)]))
        raise c.fail("invalid-path","Publication contains a non-relative output path")
    if (len(unique([for (entry in entries) entry.path])) != len(entries))
        raise c.fail("invalid-path","Publication contains duplicate output paths")
    io.mkdir(destination)^
    for (page in result.pages) {
        let target = paths.resolve_path(destination,page.path)
        io.mkdir(paths.dirname(target))^
        let html = "<!DOCTYPE html>\n" ++ format(page.document,"html")^
        output(html,target,"text")^
    }
    for (asset in result.assets) {
        let target = paths.resolve_path(destination,asset.path)
        io.mkdir(paths.dirname(target))^
        if (asset.bytes != null) output(asset.bytes,target)^
        else output(asset.text,target,"text")^
    }
    return {entry:paths.resolve_path(destination,result.entry),pages:len(result.pages),assets:len(result.assets)}
}
