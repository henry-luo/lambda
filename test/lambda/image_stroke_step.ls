import img: lambda.image
fn check(label,value) => value
let source = img.blank({width:520,height:40,color:[0.3,0.5,0.7,0.8]})^
let options = {size:8.5,hardness:0.3,spacing:0.31,flow:0.35,opacity:0.4,color:[1.0,0.2,0.1,0.6]}
let points = [[250.3,10.2,0.2],[255.7,12.2,0.8],[257.6,12.7,0.6],[280.3,20.3,1.0],[249.4,12.1,0.5]]
let first = img.paint_stroke_step(source,[points[0]],options)^
let middle = img.paint_stroke_step(source,[points[1],points[2]],options,first)^
let final = img.paint_stroke_step(source,[points[3],points[4]],options,middle)^
check("first-dab",first.raster == img.paint_stroke(source,{*:options,points:[points[0]]})^)
check("pressure-spacing",middle.raster == img.paint_stroke(source,{*:options,points:[points[0],points[1],points[2]]})^)
check("flow-opacity-retrace",final.raster == img.paint_stroke(source,{*:options,points:points})^)
check("frozen-preview",first.raster == img.paint_stroke(source,{*:options,points:[points[0]]})^ and first.coverage != final.coverage)
let branch = img.paint_stroke_step(source,[[230.0,30.0]],options,first)^
check("branch-snapshot",branch.raster == img.paint_stroke(source,{*:options,points:[points[0],[230.0,30.0]]})^)
let duplicate = img.paint_stroke_step(source,[points[2]],options,middle)^
check("duplicate-point",duplicate.raster == middle.raster and duplicate.rect == null and duplicate.work == middle.work)
let selection = img.selection(520,40,{shape:"rectangle",start:[252,0],end:[262,40]})^
let selected_options = {*:options,selection:selection,origin:[0,0]}
let selected = img.paint_stroke_step(source,[points[0]],selected_options)^
check("selected-stroke",img.paint_stroke_step(source,[points[1],points[2]],selected_options,selected)^.raster ==
    img.paint_stroke(source,{*:selected_options,points:[points[0],points[1],points[2]]})^)
let gray = img.selection(520,40,{shape:"rectangle",start:[0,0],end:[520,40]})^
let erase = {*:options,mode:"erase"}
let masked = img.paint_stroke_step(gray,[points[0],points[1]],erase)^
check("gray-erase",img.paint_stroke_step(gray,[points[2],points[3]],erase,masked)^.raster ==
    img.paint_stroke(gray,{*:erase,points:[points[0],points[1],points[2],points[3]]})^)
let patch = img.blank({width:3,height:2,color:[0.8,0.2,0.1,0.0]})^
let patched = img.with_region(source,[255,14,258,16],patch.pixels)^
check("patch-hidden-rgb",img.read_region(patched,[255,14,258,16])^ == patch.pixels)
check("patch-outside",img.read_region(patched,[0,0,255,40])^ == img.read_region(source,[0,0,255,40])^)
check("patch-source-frozen",source == img.blank({width:520,height:40,color:[0.3,0.5,0.7,0.8]})^)
check("patch-bounds",img.with_region(source,[-1,0,2,2],patch.pixels) ^ { true } ~ { false })
check("patch-size",img.with_region(source,[0,0,2,2],patch.pixels) ^ { true } ~ { false })
check("stroke-settings-bound",img.paint_stroke_step(source,[points[1]],{*:options,opacity:0.8},first) ^ { true } ~ { false })
check("stroke-source-bound",img.paint_stroke_step(patched,[points[1]],options,first) ^ { true } ~ { false })
check("stroke-state-checked",img.paint_stroke_step(source,[points[1]],options,{*:first,distance:-1}) ^ { true } ~ { false })
check("stroke-work-budget",img.paint_stroke_step(source,[points[3]],options,{*:first,work:128000000}) ^ { true } ~ { false })
check("stroke-misspelling",img.paint_stroke_step(source,[points[0]],{*:options,hardnes:0.5}) ^ { true } ~ { false })
check("stroke-invalid-point",img.paint_stroke_step(source,[[0,0,2]],options) ^ { true } ~ { false })
check("stroke-empty-batch",img.paint_stroke_step(source,[],options,first) ^ { true } ~ { false })
let equal_source = img.blank({width:520,height:40,color:[0.3,0.5,0.7,0.8]})^
check("source-value-semantics",img.paint_stroke_step(equal_source,[points[1],points[2]],options,first)^.raster == middle.raster)
let transparent = img.paint_stroke_step(source,[points[0]],{*:options,opacity:0})^
check("zero-opacity-region",transparent.rect == null and transparent.raster == source and transparent.work>0)
let empty = img.selection(520,40,{shape:"rectangle",start:[0,0],end:[0,0]})^
let clipped = img.paint_stroke_step(source,[points[0],points[1]],{*:options,selection:empty})^
check("selection-clipped-region",clipped.rect == null and clipped.raster == source and clipped.work>0)
