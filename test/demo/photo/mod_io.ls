import model: .mod_model
import pipeline: .mod_pipeline

// Two independent polynomial checksums fingerprint the encoded snapshot.
// This is accidental-change detection, not an authentication mechanism.
pub fn fingerprint(bytes) {
    let values = [for (byte in bytes) byte];
    {bytes:len(bytes),
    a:reduce(values,(a,b)=>(a*131+b)%2147483647),
    b:reduce(values,(a,b)=>(a*65599+b)%2147483629)}
}
pub fn open_image(path) any^ {
    let bytes = input(path,'binary')^
    if (not (bytes is binary) or len(bytes) > 32000000) raise error("Photo: empty image or encoded file exceeds 32 MB")
    else (
        let pixels = load({data:bytes,max_pixels:16000000,normalize_orientation:true})^,
        let dimensions = shape(pixels),
        {path:path,identity:fingerprint(bytes),width:dimensions[1],height:dimensions[0],
            pixels:pixels,proxy:pipeline.proxy(pixels,960)}
    )
}
pub fn document(source, recipe) => {schema_version:1,source:{path:source.path,
    identity:source.identity,width:source.width,height:source.height},recipe:recipe}
pub fn read_recipe(path, source) any^ {
    let value = input(path,'json')^
    if (not (value is map) or value.schema_version != 1 or not model.valid(value.recipe))
        raise error("Photo: unsupported or invalid recipe")
    else if (value.source.identity != source.identity or value.source.width != source.width or value.source.height != source.height)
        raise error("Photo: recipe belongs to a different image; open its source first")
    else value.recipe
}
fn destination(path, source, extension) any^ {
    if (not (path is string) or not ends_with(lower(path),extension))
        raise error("Photo: choose a " ++ extension ++ " destination")
    else if (url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",path) == source.path)
        raise error("Photo: choose a destination other than the source")
    else path
}
pub pn write_recipe(path, source, recipe) any^ {
    let target = destination(path,source,".json")^
    return output(document(source,recipe),target,{format:'json',atomic:true})^
}
pub pn export_png(path, source, recipe) any^ {
    let target = destination(path,source,".png")^
    if (not model.valid(recipe) or recipe.output_width*recipe.output_height > 16000000) {
        raise error("Photo: output must be at most 16 megapixels")
    }
    let pixels = pipeline.evaluate(source.pixels,recipe)
    // Encode beside the destination; publish only a completed PNG.
    let pending = target ++ ".photo-" ++ string(int(clock()*1000000)) ++ ".png"
    var problem = null
    save(pixels,pending) ^ { problem = ^ };
    if (problem == null) { io.rename(pending,target) ^ { problem = ^ } }
    if (problem != null) {
        io.delete(pending) ^ { null };
        raise problem
    }
    return shape(pixels)
}
