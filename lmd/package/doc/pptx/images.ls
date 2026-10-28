// Pictures and picture-filled shapes share relationship validation and byte capture (D7.5.2).
import c: .common
import opc: lambda.doc.opc
import media: lambda.doc.media
import theme: lambda.doc.docx.theme
import color: lambda.slide.color

fn effects(src,blip,ctx,bounds) map^ {
    // Extension metadata such as useLocalDpi does not alter pixels at saved shape bounds.
    let operations = [for (node in c.children(blip,null,c.A) where node.tag!="extLst") node]
    let duotone = c.child(blip,"duotone",c.A)
    let colors = [for (node in c.children(duotone,null,c.A)) theme.drawing_color({children:[node]},ctx.theme,null,true)];
    if (len(operations)==0) {src:src,diagnostics:[]}
    else if (len(operations)!=1 or len(colors)!=2 or colors[0]==null or colors[1]==null)
        {src:src,diagnostics:[c.warning(ctx,"unsupported-image-effect","Picture color effects are not yet reproduced")]}
    else {
        let low = color.parse(colors[0],"duotone")^
        let high = color.parse(colors[1],"duotone")^;
        if (low[3]!=1.0 or high[3]!=1.0)
            {src:src,diagnostics:[c.warning(ctx,"unsupported-image-effect","Transparent duotone endpoints are not yet reproduced")]}
        else {
            let weights = [0.2126,0.7152,0.0722]
            let matrix = [*[for (i in 0 to 2) *[*[for (weight in weights) (high[i]-low[i])*weight],0.0,low[i]]],0.0,0.0,0.0,1.0,0.0]
            // A self-contained SVG delegates the same color matrix to native image/background rendering.
            let svg = <svg xmlns:"http://www.w3.org/2000/svg",width:bounds.width,height:bounds.height,
                viewBox:"0 0 " ++ string(bounds.width) ++ " " ++ string(bounds.height),
                <defs <filter id:"duotone",x:"0",y:"0",width:"100%",height:"100%",'color-interpolation-filters':"sRGB",
                    <feColorMatrix type:"matrix",values:join([for (value in matrix) string(value)]," ")>>>
                <image href:src,width:bounds.width,height:bounds.height,preserveAspectRatio:"none",filter:"url(#duotone)">>;
            {src:media.data_uri(binary(format(svg,'xml')^),"image/svg+xml")^,diagnostics:[]}
        }
    }
}
pub fn embedded(blip,ctx,bounds) map^ {
    let rel = opc.by_id(ctx.relationships,c.attr(blip,"embed",c.R))
    let mime = if (rel==null or rel.external) null else opc.content_type(ctx,rel.target)
    let issue = if (ctx.options.images=="omit") {code:"omitted-image",message:"Image omitted by conversion options"}
        else if (c.attr(blip,"link",c.R)!=null or rel.external==true) {code:"external-image",message:"External image was not loaded"}
        else if (rel==null or rel.type!=c.R ++ "/image" or ctx.parts[rel.target]==null)
            {code:"missing-image",message:"Image relationship or part is missing"}
        else if (not (mime in ["image/png","image/jpeg","image/gif","image/bmp","image/tiff","image/webp"]))
            {code:"unsupported-image-type",message:"Unsupported embedded image type " ++ string(mime)} else null;
    if (issue!=null) {src:null,diagnostics:[c.warning(ctx,issue.code,issue.message)]}
    else effects(media.data_uri(input(ctx.parts[rel.target],'binary')^,mime)^,blip,ctx,bounds)^
}
fn axis(source_start,source_end,target_start,target_end,size) {
    let remaining = 1.0-source_start-source_end
    let scale = (1.0-target_start-target_end)/remaining
    let offset = target_start-source_start*scale;
    {scale:scale,position:if (scale==1.0) c.px(offset*size) else string(offset/(1.0-scale)*100.0) ++ "%",
        valid:remaining>0.0 and scale>0.0}
}
pub fn background(fill,src,bounds) {
    let source = c.child(fill,"srcRect",c.A)
    let target = c.child(c.child(fill,"stretch",c.A),"fillRect",c.A)
    let values = [for (node in [source,target]) [for (side in ["l","t","r","b"]) c.num(c.attr(node,side))/100000.0]]
    let x = axis(values[0][0],values[0][2],values[1][0],values[1][2],bounds.width)
    let y = axis(values[0][1],values[0][3],values[1][1],values[1][3],bounds.height);
    if (not x.valid or not y.valid or c.child(fill,"tile",c.A)!=null) null
    else c.css({["background-image"]:"url(" ++ format(src,'json')^ ++ ")",
        ["background-size"]:string(x.scale*100.0) ++ "% " ++ string(y.scale*100.0) ++ "%",
        ["background-position"]:x.position ++ " " ++ y.position,["background-repeat"]:"no-repeat"})
}
