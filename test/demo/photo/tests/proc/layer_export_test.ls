import files: ~~.~~.mod_io
import model: ~~.~~.mod_model
import layers: ~~.~~.mod_layers
import radiant
pn main() {
    let pixels = as_ubyte([for (y in 0 to 79) [for (x in 0 to 119) [0,0,0,255]]])
    let source = {path:"file:///photo-layer-source.jpg",pixels:pixels,width:120,height:80,identity:files.fingerprint(b'\x010203')}
    let base = model.recipe()
    let rectangle = {*:layers.create(base,"rectangle"),x:0.25,y:0.25,width:0.5,height:0.5,color:"#ff0000"}
    let recipe = {*:base,layers:[rectangle]}
    files.write_recipe("temp/photo-layer-recipe.json",source,recipe)^
    let restored = files.read_recipe("temp/photo-layer-recipe.json",source)^
    let dims = files.export_png("temp/photo-layer-export.png",source,restored)^
    let exported = load("temp/photo-layer-export.png")^
    let center = exported[40][60]
    let corner = exported[0][0]
    let text = {*:layers.create(base,"text","Lambda"),x:0.1,y:0.1,width:0.8,height:0.8}
    files.export_png("temp/photo-text-export.png",source,{*:base,layers:[text]})^
    let title = load("temp/photo-text-export.png")^
    let legacy = files.document(source,base)
    output({*:legacy,schema_version:1,recipe:{turns:0,flip_x:false,flip_y:false,angle:0.0,crop:base.crop,
        exposure:0.0,brightness:0.0,contrast:1.0,saturation:1.0,temperature:0.0,gamma:1.0,
        preset:"original",intensity:1.0,output_width:0,output_height:0}},"temp/photo-legacy-recipe.json",'json')^
    let upgraded = files.read_recipe("temp/photo-legacy-recipe.json",source)^
    let reduced = files.export_png("temp/photo-layer-small.png",source,{*:recipe,output_width:60,output_height:40})^
    let small = load("temp/photo-layer-small.png")^
    let transparent = {*:source,pixels:as_ubyte([for (y in 0 to 79) [for (x in 0 to 119) [0,0,0,0]]])}
    files.export_png("temp/photo-layer-alpha.png",transparent,{*:base,layers:[{*:rectangle,opacity:0.5}]})^
    let alpha = load("temp/photo-layer-alpha.png")^
    let invalid_transparency = radiant.export_document("<html><body></body></html>","temp/photo-opaque.jpg",{format:"jpeg",transparent:true})
    return [restored == recipe,dims == [80,120,4],center[0] == 255 and center[1] == 0,
        corner[0] == 0 and corner[3] == 255,sum(ravel(as_float(transpose(title)[0]))) > 100.0,
        upgraded == base,reduced == [40,60,4],small[20][30][0] == 255,
        alpha[40][60][0] == 255 and contains([127,128],alpha[40][60][3]),alpha[0][0][3] == 0,
        invalid_transparency is string]
}
