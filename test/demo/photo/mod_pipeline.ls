import presets: .mod_presets

// recipes remain pure values; the native evaluator quantizes only the final RGBA8 output.
pub fn options(recipe, compare=false, uncropped=false) => {*:recipe,
    matrix:presets.matrix(recipe.preset),compare:compare,uncropped:uncropped}
pub fn dimensions(image, recipe) {
    let dims=shape(image);
    if (recipe.turns%2==0) dims else [dims[1],dims[0],4]
}
pub fn evaluate(image, recipe, compare=false, uncropped=false) =>
    image_process(image,options(recipe,compare,uncropped))
pub fn proxy(image, edge=1280) {
    let dims=shape(image)
    let scale=min(1.0,edge/max(dims[0],dims[1]));
    if (scale==1) image else image_process(image,{output_height:max(1,int(round(dims[0]*scale))),
        output_width:max(1,int(round(dims[1]*scale)))})
}
