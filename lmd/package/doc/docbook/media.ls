import c: .common
import paths: lambda.io.path

pub fn references(model) => unique([for (node in model.nodes where node.uri == c.DB and
    node.tag in ["imagedata","audiodata","videodata"] and c.attr(node,"fileref") != null)
    c.resolve_uri(node.base_uri,c.attr(node,"fileref"))])

pub fn mime(path) {
    let parts = split(lower(path),".")
    let suffix = parts[len(parts)-1];
    c.get({png:"image/png",jpg:"image/jpeg",jpeg:"image/jpeg",gif:"image/gif",svg:"image/svg+xml",
        webp:"image/webp",mp3:"audio/mpeg",ogg:"audio/ogg",mp4:"video/mp4",webm:"video/webm"},suffix,"application/octet-stream")
}
pub fn plan(model) {
    let references = references(model)
    let entries = [for (i,uri in references,let resource = model.resources[uri],
        let basename = paths.basename(split(split(uri,"?")[0],"#")[0]))
        {uri:uri,path:"media/" ++ string(i+1) ++ "-" ++ basename,resource:resource}];
    {by_uri:c.dictionary([for (entry in entries) {key:entry.uri,value:entry}]),
        assets:[for (entry in entries where entry.resource.bytes != null)
            {path:entry.path,media_type:mime(entry.uri),bytes:entry.resource.bytes,source_uri:entry.uri}],
        diagnostics:[for (entry in entries where entry.resource.bytes == null)
            c.issue("missing-resource","Media was not captured: " ++ entry.uri,
                c.first([for (node in model.nodes where c.attr(node,"fileref") != null and
                    c.resolve_uri(node.base_uri,c.attr(node,"fileref")) == entry.uri) node]))]}
}
