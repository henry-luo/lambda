// embedded PNGs keep projects and SVGs independent of the source file (S1.4).
import image: lambda.image
import media: lambda.doc.media
import html: lambda.doc.readability.node

pub let MAX_SOURCE = 32000000
pub let MAX_EMBEDDED = 64000000
fn still(bytes) any^ {
    let checked_bytes = if (not (bytes is binary) or len(bytes)==0 or len(bytes)>MAX_SOURCE)
        raise error("Sketch: choose an image smaller than 32 MB.")
    let decoded = image.decode(bytes,{normalize_orientation:true})^
    let checked_frames = if (len(image.frames(decoded))>0) raise error("Sketch: animated images are not supported; import a still frame.")
    decoded
}
pub fn from_bytes(bytes,label="Image") any^ {
    let decoded = still(bytes)^
    // PNG is already an embeddable asset; keep its profile instead of compositing unknown-color samples.
    let png = if (image.assets(decoded)[0].format=="png") bytes else image.encode(decoded,{format:'png'})^
    let checked_png = if (len(png)>MAX_SOURCE) raise error("Sketch: the decoded image is too large to embed.")
    {src:media.data_uri(png,"image/png")^,image_width:decoded.width,image_height:decoded.height,image_name:label}
}
pub pn read(path) any^ {
    let parts = split(replace(path,"\\","/"),"/")
    return from_bytes(input(path,'binary')^,parts[last])^
}
pub fn from_uri(uri,label="Pasted image") any^ {
    let checked_uri = if (not (uri is string) or len(uri)>MAX_EMBEDDED or not starts_with(lower(uri),"data:image/"))
        raise error("Sketch: paste an embedded raster image.")
    from_bytes(media.data_uri_bytes(uri)^,label)^
}
pub fn clipboard_source(snapshot) {
    if (snapshot.image is string and starts_with(snapshot.image,"data:image/")) snapshot.image
    else {
        let markup = snapshot.html
        let tree = if (markup is string and len(markup)<=MAX_EMBEDDED) parse(markup,'html') ^ { null } else null
        let sources = if (tree==null) [] else [for (node in html.all_elements(html.of(tree)),
            let src = html.attr(node,"src") where node.tag=="img" and src is string and starts_with(lower(src),"data:image/")) src]
        if (len(sources)>0) sources[0]
        else if (snapshot.text is string and starts_with(lower(trim(snapshot.text)),"data:image/")) trim(snapshot.text)
        else null
    }
}
pub fn valid_descriptor(o) => o.src is string and len(o.src)<=MAX_EMBEDDED and starts_with(o.src,"data:image/png;base64,") and
    o.image_width is int and o.image_height is int and o.image_width>0 and o.image_height>0 and
    o.image_width<=16000000 and o.image_height<=16000000 and
    o.image_width*o.image_height<=16000000 and o.image_name is string and len(o.image_name)<=500
pub fn validate(o) bool^ {
    let decoded = still(media.data_uri_bytes(o.src)^)^
    let checked_size = if (decoded.width!=o.image_width or decoded.height!=o.image_height)
        raise error("Sketch: embedded image dimensions do not match the project.")
    true
}
pub fn storage(objects) => sum([for (o in objects where o.kind=="image") len(o.src)])
