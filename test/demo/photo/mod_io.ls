import model: .mod_model
import pipeline: .mod_pipeline
import layers: .mod_layers
import radiant

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
pub fn document(source, recipe) => {schema_version:2,source:{path:source.path,
    identity:source.identity,width:source.width,height:source.height},recipe:recipe}
pub fn read_recipe(path, source) any^ {
    let value = input(path,'json')^
    let recipe = if (value.schema_version == 1 and value.recipe is map) model.upgrade(value.recipe) else value.recipe
    if (not (value is map) or not contains([1,2],value.schema_version) or not model.valid(recipe))
        raise error("Photo: unsupported or invalid recipe")
    else if (value.source.identity != source.identity or value.source.width != source.width or value.source.height != source.height)
        raise error("Photo: recipe belongs to a different image; open its source first")
    else recipe
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
    let background = pending ++ ".base.png"
    var problem = null
    if (len(recipe.layers) == 0) { save(pixels,pending) ^ { problem = ^ } }
    if (len(recipe.layers) > 0) {
        // the existing document exporter paints the same SVG as the preview at output resolution.
        save(pixels,background) ^ { problem = ^ };
        if (problem == null) {
            let dims = shape(pixels)
            let svg = layers.svg(recipe,dims[1],dims[0],url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",background))
            let html = <html <head <style "html,body{margin:0;padding:0;background:transparent}svg{display:block}">> <body svg>>
            let done = radiant.export_document(format(html,'html'),pending,
                {format:"png",width:dims[1],height:dims[0],local_resources:true,transparent:true})
            if (done != true) { problem = error("Photo: " ++ string(done)) }
        }
        io.delete(background) ^ { null }
    }
    if (problem == null) { io.rename(pending,target) ^ { problem = ^ } }
    if (problem != null) {
        io.delete(pending) ^ { null };
        raise problem
    }
    return shape(pixels)
}
