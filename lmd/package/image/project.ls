// S9.1.2 / S12.1.1v3: projects are versioned data; loading never evaluates source code.
import img: lambda.image

let LIMIT = 128*1024*1024
// preflight payloads before making binary copies or formatting hexadecimal samples.
fn estimate(node,depth=0) int^ {
    if (depth>64) raise error("image.project: nesting limit exceeded")
    else if (node is element and name(node) == 'raster') node.width*node.height*len(node.channels)*2+256
    else if (node is binary) len(node)*2+16
    else if (node is string) len(node)*6+16
    else if (node is element) 256+estimate(map(node),depth+1)^+sum([0,*[for (child in content(node)) estimate(child,depth+1)^]])
    else if (node is map) 32+sum([0,*[for (key,value in node) estimate(string(key),depth+1)^+estimate(value,depth+1)^]])
    else if (node is array) 32+sum([0,*[for (value in node) estimate(value,depth+1)^]])
    else 24
}
fn packed(node,depth=0) any^ {
    if (depth>64) raise error("image.project: nesting limit exceeded")
    else if (node is element and name(node) == 'raster') <raster_bytes width:node.width,height:node.height,
        channels:len(node.channels),color_space:node.color,bytes:img.to_bytes(node)^>
    else if (node is element) img.with_children(node,[for (child in content(node)) packed(child,depth+1)^])^
    else node
}
fn decoded_bytes(node,depth=0) int^ {
    if (depth>64) raise error("image.project: nesting limit exceeded")
    else if (node is element and name(node) == 'raster_bytes') (
        if (not (node.bytes is binary)) raise error("image.project: missing raster bytes") else len(node.bytes))
    else if (node is element) sum([0,*[for (child in content(node)) decoded_bytes(child,depth+1)^]])
    else 0
}
fn unpacked(node,depth=0) any^ {
    if (depth>64) raise error("image.project: nesting limit exceeded")
    else if (node is element and name(node) == 'raster_bytes') img.from_bytes(node.bytes,
        {width:node.width,height:node.height,channels:node.channels,color_space:node.color_space})^
    else if (node is element) img.with_children(node,[for (child in content(node)) unpacked(child,depth+1)^])^
    else node
}
pub fn encode(document) string^ {
    let admitted = if (estimate(document)^>LIMIT) raise error("image.project: encoded payload budget exceeded") else true
    let checked = img.validate(document)^
    let data = packed(document)^
    if (decoded_bytes(data)^ > LIMIT) raise error("image.project: raster payload budget exceeded")
    else {
        let text = format({format:"lambda-photos",version:1,document:data},'mark')
        if (len(text)>LIMIT) raise error("image.project: encoded project exceeds 128 MiB") else text
    }
}
pub fn decode(text) element^ {
    if (not (text is string) or len(text)>LIMIT) raise error("image.project: expected project text within 128 MiB")
    else {
        let data = parse(text,'mark')^
        if (not (data is map) or data.format != "lambda-photos" or data.version != 1 or name(data.document) != 'image')
            raise error("image.project: unsupported format or version")
        else if (decoded_bytes(data.document)^ > LIMIT) raise error("image.project: raster payload budget exceeded")
        else {
            let document = unpacked(data.document)^
            let admitted = if (estimate(document)^>LIMIT) raise error("image.project: encoded payload budget exceeded") else true
    let checked = img.validate(document)^;
            document
        }
    }
}
pub pn read(path) element^ { return decode(input(path,'text')^)^ }
pub pn write(document,path) bool^ {
    let text = encode(document)^
    output(text,path,{atomic:true})^
    return true
}
