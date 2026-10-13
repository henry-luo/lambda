import pipeline: ~~.mod_pipeline
import model: ~~.mod_model
let pixels = as_ubyte([[[32,64,96,255],[128,128,128,128],[224,192,160,255]],
    [[64,64,64,0],[128,128,128,255],[192,192,192,255]]])
let recipe = model.recipe()
let levels = pipeline.evaluate(pixels,{*:recipe,black:0.25,white:0.75})
ravel(levels)[0] == 0 and ravel(levels)[8] == 255
transpose(levels)[3] == transpose(pixels)[3]
let vignette = pipeline.evaluate(pixels,{*:recipe,vignette:0.8});
(ravel(vignette)[0] < ravel(pixels)[0]);
transpose(vignette)[3] == transpose(pixels)[3]
let soft = pipeline.evaluate(pixels,{*:recipe,softness:3.0})
shape(soft) == shape(pixels) and transpose(soft)[3] == transpose(pixels)[3]
soft != pixels
pipeline.evaluate(pixels,{*:recipe,sharpen:1.0}) != pixels
pipeline.evaluate(pixels,{*:recipe,shadows:0.8}) != pixels
pipeline.evaluate(pixels,{*:recipe,highlights:-0.8}) != pixels
pipeline.evaluate(pixels,{*:recipe,vignette:1.0,softness:3.0,black:0.2},true) == pixels
