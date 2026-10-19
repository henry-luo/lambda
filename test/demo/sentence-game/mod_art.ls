import resources: ~~.mod_resources

let BASE = url_resolve(resources.entry_uri()^,"assets/")

// Static illustrations use the document image cache across reactive updates.
pub fn picture(kind) => <img src:BASE ++ kind ++ ".svg",width:64,height:64,alt:"",'aria-hidden':"true">
pub fn scene(kind,description) => <img class:"scene-image",src:BASE ++ kind ++ ".svg",width:960,height:600,alt:description>
