import geometry: .mod_geometry
import radiant

pub let tools = [
    {id:"adjust",label:"Adjust",icon:"◐"}, {id:"filters",label:"Filters",icon:"▦"},
    {id:"effects",label:"Effects",icon:"✧"}, {id:"crop",label:"Crop",icon:"⌗"},
    {id:"text",label:"Text",icon:"T"}, {id:"shapes",label:"Shapes",icon:"◇"},
    {id:"stickers",label:"Stickers",icon:"☆"}
]
pub let shapes = [{id:"rectangle",label:"Rectangle"},{id:"ellipse",label:"Circle"},
    {id:"triangle",label:"Triangle"},{id:"arrow",label:"Arrow"},{id:"line",label:"Line"}]
pub let stickers = [{id:"heart",label:"Love"},{id:"star",label:"Star"},{id:"sun",label:"Sunshine"},
    {id:"bolt",label:"Energy"},{id:"pin",label:"Somewhere"},{id:"smile",label:"Good vibes"}]
pub let colors = ["#ffffff","#171d29","#80d6b3","#ffca72","#f48f9c","#9daaff"]
pub let fonts = ["Helvetica Neue","Georgia","Courier New"]
pub let placement_keys = ["x","y","width","height"]
pub let limits = {x:[0.0,1.0],y:[0.0,1.0],width:[0.02,1.0],height:[0.02,1.0],
    rotation:[-180.0,180.0],opacity:[0.0,1.0]}
pub fn object_tool(tool) => contains(["text","shapes","stickers"],tool)
pub fn color_valid(color: string) => len(color) == 7 and starts_with(color,"#") and
    all([for (i in 1 to 6) contains("0123456789abcdefABCDEF",slice(color,i,i+1)) or false])
pub fn valid(layer) => layer is map and layer.id is int and layer.id > 0 and
    contains(["text",*[for (entry in shapes) entry.id],*[for (entry in stickers) entry.id]],layer.kind) and
    all([for (key,bounds in limits) layer[key] is number and layer[key] >= bounds[0] and layer[key] <= bounds[1]]) and
    layer.color is string and color_valid(layer.color) and layer.text is string and len(layer.text) <= 160 and
    contains(fonts,layer.font) and contains(["start","middle","end"],layer.align)
pub fn selected(recipe,id) => [for (layer in recipe.layers where layer.id == id) layer][0]
pub fn create(recipe,kind,text="Your story",aspect=1.5) {
    let id = max([0,*[for (layer in recipe.layers) layer.id]])+1
    let width = if (kind == "text") 0.56 else 0.22
    let height = if (kind == "text") 0.12 else min(0.45,width*aspect);
    {id:id,kind:kind,text:slice(text,0,160),x:(1-width)/2,y:(1-height)/2,width:width,height:height,
        rotation:0.0,opacity:1.0,color:if (kind == "text") "#ffffff" else if (kind == "heart") "#f48f9c" else "#ffca72",
        font:"Helvetica Neue",align:"middle"}
}
pub fn update(recipe,id,patch) => {*:recipe,layers:[for (layer in recipe.layers)
    if (layer.id == id) {*:layer,*:patch} else layer]}
pub fn change(recipe,id,key,requested) {
    let layer = selected(recipe,id)
    let numeric = if (contains(limits,key)) float(requested) ^ { null } else null
    let changed = if (contains(limits,key) and numeric is number) geometry.clamp(numeric,limits[key][0],limits[key][1])
        else if (key == "text") slice(string(requested),0,160) else requested;
    // cleared or partially typed numeric fields cannot become an error-valued recipe draft.
    if (layer == null or (contains(limits,key) and not (numeric is number))) recipe else (
        let candidate = {*:layer,[key]:changed},
        if (valid(candidate)) update(recipe,id,{[key]:changed}) else recipe
    )
}
pub fn remove(recipe,id) => {*:recipe,layers:[for (layer in recipe.layers where layer.id != id) layer]}
pub fn reorder(recipe,id,direction) {
    let index = [for (i,layer in recipe.layers where layer.id == id) i][0];
    if (index == null) recipe else (
        let next = int(geometry.clamp(index+direction,0,len(recipe.layers)-1)),
        {*:recipe,layers:[for (i,layer in recipe.layers)
            if (i == next) recipe.layers[index] else if (i == index) recipe.layers[next] else layer]}
    )
}
pub fn duplicate(recipe,id) {
    let layer = selected(recipe,id);
    if (layer == null or len(recipe.layers) >= 24) recipe else {*:recipe,layers:[*recipe.layers,
        {*:layer,id:max([for (item in recipe.layers) item.id])+1,x:min(0.95,layer.x+0.03),y:min(0.95,layer.y+0.03)}]}
}
// test in the unrotated local box, from front to back; transparent layers remain selectable.
pub fn hit(recipe,x,y,aspect) {
    let hits = [for (layer in recipe.layers) (
        let radians = -layer.rotation*math.pi/180,
        let dx = x-layer.x-layer.width/2,
        let dy = (y-layer.y-layer.height/2)/aspect,
        let local_x = dx*math.cos(radians)-dy*math.sin(radians),
        let local_y = dx*math.sin(radians)+dy*math.cos(radians),
        if (abs(local_x) <= layer.width/2 and abs(local_y) <= layer.height/(2*aspect)) layer.id else null
    )] |: ~ != null;
    if (len(hits) == 0) null else hits[last]
}
pub fn drag(layer,handle,dx,dy,aspect=1.0) => if (handle == "resize") (
    let radians = -layer.rotation*math.pi/180,
    let local_x = dx*math.cos(radians)-dy/aspect*math.sin(radians),
    let local_y = (dx*math.sin(radians)+dy/aspect*math.cos(radians))*aspect,
    {*:layer,width:geometry.clamp(layer.width+local_x,0.02,1),height:geometry.clamp(layer.height+local_y,0.02,1)}
    )
    else {*:layer,x:geometry.clamp(layer.x+dx,0,1-layer.width),y:geometry.clamp(layer.y+dy,0,1-layer.height)}

pub fn glyph(kind) => if (kind == "rectangle") <rect x:5,y:5,width:90,height:90,rx:8>
    else if (kind == "ellipse") <ellipse cx:50,cy:50,rx:45,ry:45>
    else if (kind == "triangle") <path d:"M50 5 L96 95 H4 Z">
    else if (kind == "arrow") <path d:"M4 36 H58 V8 L98 50 L58 92 V64 H4 Z">
    else if (kind == "line") <rect x:2,y:44,width:96,height:12,rx:6>
    else if (kind == "heart") <path d:"M50 92 C35 78 4 56 4 30 C4 3 38 0 50 23 C62 0 96 3 96 30 C96 56 65 78 50 92 Z">
    else if (kind == "star") <path d:"M50 3 L62 35 L97 36 L70 58 L80 93 L50 73 L20 93 L30 58 L3 36 L38 35 Z">
    else if (kind == "bolt") <path d:"M56 2 L15 56 H44 L36 98 L85 40 H55 Z">
    else if (kind == "pin") <path 'fill-rule':"evenodd",d:"M50 3 C1 3 7 54 50 97 C93 54 99 3 50 3 Z M50 22 A15 15 0 1 0 50 52 A15 15 0 1 0 50 22 Z">
    else if (kind == "sun") <g *[<circle cx:50,cy:50,r:24>,*[for (i in 0 to 7)
        <rect x:46,y:2,width:8,height:16,rx:4,transform:("rotate(" ++ string(i*45) ++ " 50 50)")>]]>
    else <g <circle cx:50,cy:50,r:46> <g fill:"#171d29",<circle cx:35,cy:38,r:5> <circle cx:65,cy:38,r:5>
        <path d:"M27 57 Q50 87 73 57",fill:"none",stroke:"#171d29",'stroke-width':6,'stroke-linecap':"round">>>
pub fn icon(kind) => <svg xmlns:"http://www.w3.org/2000/svg",viewBox:"0 0 100 100",width:44,height:44,fill:"currentColor",glyph(kind)>
fn text_artwork(layer,width,height) {
    // measure through the renderer's font engine; fit without distorting glyphs.
    let metric = radiant.measure_text([{text:layer.text,font:{font_family:layer.font,font_size:100,font_weight:700}}],null)[0]
    let size = min(height*0.8,width*100/max(1,metric.width));
    <text x:(if (layer.align == "start") 0 else if (layer.align == "end") width else width/2),
        y:((height-size)/2+size*0.78),'font-family':layer.font,'font-weight':700,'font-size':size,
        'text-anchor':layer.align,layer.text>
}
fn artwork(layer,width,height) {
    let w = layer.width*width
    let h = layer.height*height;
    <g 'data-layer':string(layer.id),opacity:layer.opacity,fill:layer.color,
        transform:("translate(" ++ string(layer.x*width) ++ " " ++ string(layer.y*height) ++ ") rotate(" ++
            string(layer.rotation) ++ " " ++ string(w/2) ++ " " ++ string(h/2) ++ ")"),
        if (layer.kind == "text") text_artwork(layer,w,h)
        else <svg width:w,height:h,viewBox:"0 0 100 100",preserveAspectRatio:"none",glyph(layer.kind)>>
}
// both the retained overlay and PNG exporter use this vector scene (S1.6).
pub fn svg(recipe,width,height,background=null) => <svg xmlns:"http://www.w3.org/2000/svg",
    width:width,height:height,viewBox:("0 0 " ++ string(width) ++ " " ++ string(height)), *[
        if (background != null) <image href:background,x:0,y:0,width:width,height:height>,
        *[for (layer in recipe.layers) artwork(layer,width,height)]]>
