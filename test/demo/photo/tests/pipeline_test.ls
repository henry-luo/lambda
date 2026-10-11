import pipeline: ~~.mod_pipeline
import model: ~~.mod_model
let pixels = as_ubyte([[[255,0,0,255],[0,255,0,128]],[[0,0,255,0],[255,255,255,255]]])
let recipe = model.recipe()
let mono = pipeline.evaluate(pixels,{*:recipe,preset:"mono"})
pipeline.evaluate(pixels,recipe) == pixels
shape(mono) == [2,2,4]
ravel(mono) == as_ubyte([76,76,76,255,150,150,150,128,29,29,29,0,255,255,255,255])
ravel(pipeline.evaluate(pixels,{*:recipe,preset:"mono",intensity:0})) == ravel(pixels)
transpose(mono)[3] == transpose(pixels)[3]
pipeline.evaluate(pixels,{*:recipe,preset:"warm"},true) == pixels
shape(pipeline.evaluate(pixels,{*:recipe,crop:{left:0,top:0,right:0.5,bottom:1}})) == [2,1,4]
ravel(pipeline.evaluate(pixels,{*:recipe,flip_x:true}))[0] == 0
ravel(pipeline.evaluate(pixels,{*:recipe,turns:3}))[0] == 0
shape(pipeline.evaluate(pixels,{*:recipe,output_width:3,output_height:4})) == [4,3,4]
shape(pipeline.proxy(pixels,1)) == [1,1,4]
let transparent = as_ubyte([[[255,0,0,0],[0,0,255,255]]])
let resized = pipeline.evaluate(transparent,{*:recipe,output_width:3,output_height:1})
ravel(resized)[4] == 0 and ravel(resized)[6] == 255
