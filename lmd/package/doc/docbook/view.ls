// Source viewing uses the same pure converter; acquisition stays procedural (D7.2.4, S12.1.1v3).
import docbook: lambda.doc.docbook
import h: .html
import config: .options
import resources: .resources
import media: lambda.doc.media
import paths: lambda.io.path

fn asset_url(asset,home) string^ {
    if (asset.bytes != null) media.data_uri(asset.bytes,asset.media_type)^
    else {
        let originals = [for (entry in resources.ASSETS where entry.sha256 == asset.sha256) entry];
        home ++ "/package/doc/docbook/resources/xsltng/" ++ originals[0].path
    }
}
fn resolve_assets(value,urls,diagnostics) any^ {
    if (not (value is element)) value else {
        let attrs = map(value)
        // HTML links include './' while manifest paths omit it; compare lexical paths.
        let rewritten = {*:attrs,*:map([for (key,uri in attrs,
            let asset_path = if (uri is string) paths.normal_path(uri) else null where
            string(key) in ["src","href"] and urls[asset_path] != null) (key,urls[asset_path])])};
        let notices = if (string(name(value)) != "body" or len(diagnostics) == 0) [] else
            [<aside class:"docbook-import-notes",<p "Some content could not be displayed:">
                <ul *[for (issue in diagnostics) <li issue.message>]>>];
        h.make(string(name(value)),rewritten,[*notices,
            *[for (child in content(value)) resolve_assets(child,urls,diagnostics)^]])^
    }
}

pub pn render_document_file(path,options = null) element^ {
    let snapshot = docbook.read(path)^
    let opts = config.checked(options)^
    let result = docbook.convert(snapshot,{*:opts,output:"single",target:"radiant"})^
    let home = paths.resolve_path(sys.proc.self.cwd#,sys.lambda.home#)
    // Source views resolve CSS from the installation and captured images from memory.
    let urls = map([for (asset in result.assets) (paths.normal_path(asset.path),asset_url(asset,home)^)])
    let document = resolve_assets(result.document,urls,result.diagnostics)^;
    return document
}
