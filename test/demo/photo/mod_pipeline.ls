import geometry: .mod_geometry
import presets: .mod_presets

// Transpose is a strided view; joining four planes restores the H,W,4 contract.
fn pack(r,g,b,a) => transpose(concat(stack(r,g),stack(b,a)))
fn positive(value) => (value+abs(value))*0.5
fn clipped(value) => (abs(value)-abs(value-1.0)+1.0)*0.5
fn finish(value, exponent) => if (exponent == 1) value else gamma(positive(value),1.0/exponent)
pub fn oriented(image, recipe) {
    let turned = if (recipe.turns == 0) image else rot90(image,recipe.turns)
    let horizontal = if (recipe.flip_x) flip(turned,1) else turned;
    if (recipe.flip_y) flip(horizontal,0) else horizontal
}
fn interpolated(image, angle, height, width) {
    let planes = transpose(as_float(image))
    let premultiplied = pack(planes[0]*planes[3],planes[1]*planes[3],planes[2]*planes[3],planes[3])
    let result = if (angle != 0) rotate(premultiplied,angle) else resize(premultiplied,height,width)
    let channels = transpose(result)
    // zero alpha uses denominator one; transparent RGB therefore stays zero.
    let denominator = channels[3]+threshold(invert(channels[3]),1.0);
    pack(channels[0]/denominator,channels[1]/denominator,channels[2]/denominator,channels[3])
}
pub fn plane(image, recipe) {
    let orient = oriented(image,recipe);
    if (recipe.angle == 0) orient else interpolated(orient,recipe.angle,0,0)
}
pub fn adjustments(image, recipe) {
    if (recipe.exposure == 0 and recipe.brightness == 0 and recipe.contrast == 1 and
        recipe.saturation == 1 and recipe.temperature == 0 and recipe.gamma == 1 and
        (recipe.preset == "original" or recipe.intensity == 0)) image
    else (
        let channels = transpose(as_float(image)),
        let rgb = (channels*math.pow(2.0,recipe.exposure)+recipe.brightness-0.5)*recipe.contrast+0.5,
        let luma = rgb[0]*0.299+rgb[1]*0.587+rgb[2]*0.114,
        let red = finish(luma+(rgb[0]-luma)*recipe.saturation+recipe.temperature*0.1,recipe.gamma),
        let green = finish(luma+(rgb[1]-luma)*recipe.saturation,recipe.gamma),
        let blue = finish(luma+(rgb[2]-luma)*recipe.saturation-recipe.temperature*0.1,recipe.gamma),
        let preset = presets.channels(red,green,blue,recipe.preset),
        let amount = recipe.intensity,
        pack(clipped(red*(1-amount)+preset.r*amount),clipped(green*(1-amount)+preset.g*amount),
            clipped(blue*(1-amount)+preset.b*amount),channels[3])
    )
}
pub fn evaluate(image, recipe, compare = false, uncropped = false) {
    let prepared = plane(image,recipe)
    let dimensions = shape(prepared)
    let rectangle = geometry.bounds(recipe.crop,dimensions[1],dimensions[0])
    let selected = if (uncropped) prepared else crop(prepared,
        rectangle.top to (rectangle.bottom-1),rectangle.left to (rectangle.right-1))
    let adjusted = if (compare) selected else adjustments(selected,recipe)
    let result = if (recipe.output_width > 0 and not uncropped) interpolated(adjusted,0,
        recipe.output_height,recipe.output_width) else adjusted;
    as_ubyte(result)
}
pub fn proxy(image, edge = 1280) {
    let dimensions = shape(image)
    let scale = min(1.0,edge/max(dimensions[0],dimensions[1]));
    if (scale == 1) image else as_ubyte(interpolated(image,0,
        max(1,int(round(dimensions[0]*scale))),max(1,int(round(dimensions[1]*scale)))))
}
