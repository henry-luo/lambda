import c: .common
import render: .render
import resources: .resources
import media: .media
import chunks: .chunks

fn document(root,rendered,options) {
    let css = options.parameters["use-docbook-css"]
    let base = options.parameters["resource-base-uri"]
    let links = if (css) [<link rel:"stylesheet",href:(base ++ "css/docbook.css")>] else []
    let head_items = [<meta charset:"utf-8">,<title c.normalized(c.title(root))>,
        <meta name:"generator",content:("Lambda DocBook (xslTNG " ++ c.VERSION ++ ")")>,*links]
    let head = <head *head_items>;
    <html xmlns:c.XHTML,lang:root.language,head rendered.body>
}
pub fn assemble(model,options) map^ {
        let captured = media.plan(model)
        let plan = chunks.plan(model,options)^
        let notes = [for (node in model.nodes where node.tag == "footnote") node]
        let note_numbers = if (options.output == "single" or not options.parameters["chunk-renumber-footnotes"])
            model.notes else c.dictionary([for (i,node in notes) {key:node.key,
                value:len([for (prior in take(notes,i) where plan.owners[prior.key] == plan.owners[node.key]) prior])+1}])
        let rendered = [for (i,page in plan.pages) render.page({*:model,page_root:page.node,notes:note_numbers},options,
            page.path,plan.owners,captured,plan.roots,chunks.navigation(plan,i))^]
        let pages = [for (i,page in plan.pages) {path:page.path,document:document(page.node,rendered[i],options)}]
        let css = options.parameters["use-docbook-css"]
        let base = options.parameters["resource-base-uri"]
        {document:pages[0].document,entry:"index.html",pages:pages,
            assets:(if (css) [for (asset in resources.ASSETS)
                {*:asset,path:(if (base == "./") "" else base) ++ asset.path}] else []) ++ captured.assets,
            external_stylesheets:if (css) resources.EXTERNAL_STYLESHEETS else [],media_diagnostics:captured.diagnostics,
            toc:rendered[0].toc,metadata:{title:c.normalized(c.title(model.root)),language:model.root.language},
            compatibility:{upstream_release:c.VERSION,upstream_commit:c.COMMIT,profile:"core-html",target:options.target,deviations:
                ["static browser head without optional scripts","syntax highlighter defaults to none",
                 "chunk db-id uses stable source occurrence IDs","external upstream font stylesheets remain external"]}}
}
