import img: lambda.image
fn check(label,value) => value
let empty = img.blank({width:520,height:16})^
let stroke = {points:[[250.0,8.0],[264.0,8.0]],size:4.0,hardness:1.0,spacing:0.25,color:[1.0,0.0,0.0,1.0]}
let painted = img.paint_stroke(empty,stroke)^
check("cross-tile",painted.a[8,255] == 255 and painted.a[8,256] == 255)
check("old-snapshot",empty.a[8,255] == 0)
let subdivided = img.paint_stroke(empty,{*:stroke,points:[[250.0,8.0],[257.0,8.0],[264.0,8.0]]})^
check("distance-sampling",painted == subdivided)
let erased = img.paint_stroke(painted,{*:stroke,mode:"erase"})^
check("erase",erased.a[8,256] == 0 and painted.a[8,256] == 255)
let selected = img.selection(520,16,{shape:"rectangle",start:[252,6],end:[260,10]})^
check("selection-bounds",img.coverage_bounds(selected)^ == [252,6,260,10])
let clipped = img.paint_stroke(empty,{*:stroke,selection:selected})^
check("selection-clipping",clipped.a[8,254] == 255 and clipped.a[8,250] == 0 and clipped.a[8,261] == 0)
let ellipse = img.selection(5,5,{shape:"ellipse",start:[0,0],end:[5,5]})^
check("ellipse",ellipse.pixels[2,2,0] == 255 and ellipse.pixels[0,0,0] == 0)
let polygon = img.selection(5,5,{shape:"polygon",points:[[0.0,0.0],[5.0,0.0],[0.0,5.0]]})^
check("polygon",polygon.pixels[0,0,0] == 255 and polygon.pixels[4,4,0] == 0)
let inverse = img.invert_coverage(polygon)^
check("invert",inverse.pixels[0,0,0] == 0 and inverse.pixels[4,4,0] == 255)
check("intersection",img.coverage_bounds(img.combine_coverage(polygon,inverse,"intersect")^)^ == null)
check("union",img.combine_coverage(polygon,inverse,"add")^.pixels[4,4,0] == 255)
check("subtract",img.coverage_bounds(img.combine_coverage(polygon,polygon,"subtract")^)^ == null)
let feathered = img.feather_coverage(polygon,1)^
check("feather",feathered.pixels[1,3,0]>0 and feathered.pixels[1,3,0]<255)
let gray = img.paint_stroke(img.blank({width:520,height:16,channels:1})^,{*:stroke,color:[1.0]})^
check("mask-stroke",gray.pixels[8,256,0] == 255 and gray.channels == ["gray"])
let ramp = img.gradient(img.blank({width:4,height:1})^,{color:[0.0,0.0,0.0,1.0],end_color:[1.0,1.0,1.0,1.0],start:[0.5,0.5],end:[3.5,0.5],kind:"linear"})^
check("gradient",ramp.r[0,0] == 0 and ramp.r[0,3] == 255 and ramp.r[0,1] == 85)
let red = img.blank({width:3,height:1,color:[1.0,0.0,0.0,1.0]})^
let split = img.with_region(red,[1,0,2,1],img.blank({width:1,height:1,color:[0.0,0.0,1.0,1.0]})^.pixels)^
check("connected-wand",img.wand(split,[0,0],{tolerance:0})^.pixels[0,2,0] == 0)
check("global-wand",img.wand(split,[0,0],{tolerance:0,contiguous:false})^.pixels[0,2,0] == 255)
let translated = img.reframe(split,[-1,-1,4,2])^
check("reframe",translated.width == 5 and translated.a[1,0] == 0 and translated.b[1,2] == 255)
let doc = img.document(img.stack([img.layer(split,{position:[1,1]})]),{width:6,height:4})
check("demand-region",img.render_region(doc,[1,1,4,2])^ == img.crop(img.render_frame(doc)^,[1,1,4,2])^)
check("invalid-options",img.paint_stroke(empty,{*:stroke,hardnes:0.5}) ^ { true } ~ { false })
check("invalid-gradient",img.gradient(empty,{color:stroke.color,end_color:stroke.color,start:[0,0],end:[0,0],kind:"linear"}) ^ { true } ~ { false })
check("invalid-mask",img.paint_stroke(empty,{*:stroke,selection:empty}) ^ { true } ~ { false })
