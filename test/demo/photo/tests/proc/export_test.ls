import files: ~~.~~.mod_io
import model: ~~.~~.mod_model
pn main() {
    let image = as_ubyte([[[255,0,0,255],[0,255,0,128]],[[0,0,255,0],[255,255,255,255]]])
    let source = {path:"file:///photo-test-source.jpg",pixels:image,width:2,height:2,identity:files.fingerprint(b'\x010203')}
    let recipe = {*:model.recipe(),preset:"mono"}
    files.write_recipe("temp/photo-test-recipe.json",source,recipe)^
    let restored = files.read_recipe("temp/photo-test-recipe.json",source)^
    let exported = files.export_png("temp/photo-test-export.png",source,restored)^
    let result = load("temp/photo-test-export.png")^
    var invalid = false
    files.export_png("temp/photo-test-export.jpg",source,restored) ^ { invalid = true }
    let mismatch = files.read_recipe("temp/photo-test-recipe.json",{*:source,identity:files.fingerprint(b'\x010204')}) ^ { "mismatch" }
    var unwritable = false
    files.export_png("temp/photo-missing-parent/export.png",source,restored) ^ { unwritable = true }
    var too_large = false
    files.export_png("temp/photo-test-export.png",source,{*:restored,output_width:16384,output_height:16384}) ^ { too_large = true }
    var source_protected = false
    files.export_png("temp/photo-test-source.png",{*:source,path:url_resolve("file://" ++ sys.proc.self.cwd# ++ "/","temp/photo-test-source.png")},recipe) ^ { source_protected = true }
    var recipe_extension = false
    files.write_recipe("temp/photo-test-export.png",source,recipe) ^ { recipe_extension = true }
    return [restored == recipe, exported == [2,2,4],
        ravel(result) == as_ubyte([76,76,76,255,150,150,150,128,29,29,29,0,255,255,255,255]),
        invalid,mismatch == "mismatch",unwritable,too_large,source_protected,recipe_extension]
}
