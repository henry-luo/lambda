// S9.1.2 / S9.1.3: authored geometry remains primary content until explicit rasterization.
import img: lambda.image
import model: .model

pub fn content_for(kind,options) any^ {
    let allowed = ["width","height","fill","stroke","stroke_width","gradient"]
    let config = img.checked_options(options,[*allowed,*if (kind == "text") ["text","font","size","line_height","align"]
        else ["shape","radius","path"]])^;
    if (kind == "text") <text *:reduce([{},*[for (key,v in config where string(key) != "text") {[key]:v}]],
        (result,entry) => {*:result,*:entry}),model.value(config.text,"")>
    else <vector *:config>
}
pub fn layer_for(st,command) any^ {
    let kind = if (command.id == "text.insert") "text" else "vector"
    let source = content_for(kind,command.options)^;
    <layer kind:symbol(kind),id:model.fresh_id(st.doc),name:model.value(command.name,if (kind == "text") "Text" else "Shape"),
        position:model.value(command.position,[0,0]),visible:true,opacity:1.0,source>
}
pub fn update(st,command) any^ {
    let row = model.active(st)^
    let kind = if (command.id == "text.update") "text" else "vector"
    let old = model.primary(row.node)
    if (name(old) != symbol(kind)) raise error("Raster: select the matching editable artwork layer")
    else if (model.locked(st.doc,[row.path])) raise error("Raster: target layer is locked")
    else {
        let source = content_for(kind,{*:map(old),*:if (kind == "text") {text:old[0]} else {},*:command.options})^
        let changed = img.with_children(row.node,[for (child in content(row.node)) if (child == old) source else child])^;
        {*:st,doc:model.replace(st.doc,row.id,changed)^}
    }
}
pub fn embed_font(st,command) any^ {
    let id = command.font
    let existing = img.assets(st.doc)
    if (not (id is string) or len(id)<1 or len(id)>256 or not (command.bytes is binary) or len(command.bytes)<1 or len(command.bytes)>16*1024*1024)
        raise error("Raster: font needs an ID and at most 16 MiB of captured bytes")
    else if (id in model.identifiers(st.doc)) raise error("Raster: asset ID already exists")
    else {
        let assets = <assets *[*if (existing == null) [] else content(existing),<font id:id,data:command.bytes,family:command.family>]>
        let changed = img.with_children(st.doc,[*[for (child in content(st.doc) where name(child) != 'assets') child],assets])^;
        {*:st,doc:changed}
    }
}
