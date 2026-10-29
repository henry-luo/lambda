// Raster bytes are captured once per source; external URLs are never fetched.
import c: .export_common
import style: .export_style
import media: lambda.doc.media
import paths: lambda.io.path
fn mime(bytes) => if (slice(bytes,0,8)==b'\x89504E470D0A1A0A') {type:"image/png",ext:"png"}
    else if (slice(bytes,0,3)==b'\xFFD8FF') {type:"image/jpeg",ext:"jpg"}
    else if (slice(bytes,0,4)==binary("GIF8")) {type:"image/gif",ext:"gif"}
    else if (slice(bytes,0,2)==binary("BM")) {type:"image/bmp",ext:"bmp"}
    else if (slice(bytes,0,4) in [b'\x49492A00',b'\x4D4D002A']) {type:"image/tiff",ext:"tiff"} else null
fn captured(src,opts) binary^ {
    if (starts_with(src,"data:")) media.data_uri_bytes(src)^
    else if (opts.base==null or contains(src,":") or starts_with(src,"//"))
        raise error("Image requires a data URI or an explicitly supplied local base folder")
    else input(paths.resolve_path(opts.base,src),'binary')^
}
pub fn records(nodes,opts) => [for (i,src in unique([for (entry in nodes where c.tag(entry.node)=="img") entry.node.src or ""])) {
    // S7.4.2: the caught image failure has an explicit raised channel.
    fn read_image() map^ {
        let bytes = captured(src,opts)^
        let kind = mime(bytes)
        let dimensions = if (kind==null) null else shape(load({data:bytes,max_pixels:16000000})^);
        if (kind==null or dimensions==null) raise error("Embedded image is outside the supported PNG/JPEG/GIF/BMP/TIFF profile")
        else {bytes:bytes,mime:kind.type,filename:"image" ++ string(i+1) ++ "." ++ kind.ext,width:dimensions[1],height:dimensions[0]}
    }
    let read = read_image() ^ { {problem:^.message} };
    {*:read,src:src,id:"rIdImage" ++ string(i+1),diagnostics:if (read.problem==null) [] else
        [c.issue("image",read.problem ++ "; alternate text was retained")]}
}]
pub fn drawing(node,record,identifier) {
    let w = style.length(style.css_get(node.style,"width") or node.width)
    let h = style.length(style.css_get(node.style,"height") or node.height)
    let width = if (w!=null and w>0) w else if (h!=null and h>0) h*record.width/record.height else record.width
    let height = if (h!=null and h>0) h else width*record.height/record.width
    let cx = int(round(width*9525.0))
    let cy = int(round(height*9525.0));
    <'w:drawing' <'wp:inline' distT:"0",distB:"0",distL:"0",distR:"0",
        <'wp:extent' cx:cx,cy:cy>;
        <'wp:docPr' id:identifier,name:record.filename,descr:node.alt or "">;
        <'wp:cNvGraphicFramePr' <'a:graphicFrameLocks' noChangeAspect:"1">>;
        <'a:graphic' <'a:graphicData' uri:"http://schemas.openxmlformats.org/drawingml/2006/picture",
            <'pic:pic' <'pic:nvPicPr' <'pic:cNvPr' id:identifier,name:record.filename,descr:node.alt or ""><'pic:cNvPicPr'>>;
                <'pic:blipFill' <'a:blip' 'r:embed':record.id><'a:stretch' <'a:fillRect'>>>;
                <'pic:spPr' <'a:xfrm' <'a:off' x:"0",y:"0"><'a:ext' cx:cx,cy:cy>>;
                    <'a:prstGeom' prst:"rect",<'a:avLst'>> >>>>>>
}
