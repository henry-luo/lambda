import img: lambda.image
fn check(label,value) => value
let sparse = img.blank({width:520,height:2})^
let dense = img.from_bytes(img.to_bytes(sparse)^,{width:520,height:2})^
check("native-dense-sparse",sparse == dense)
check("materialized-fallback",img.materialize(sparse) == dense and sparse == img.materialize(dense))
let hidden = img.paint_fill(sparse,{color:[1,0,0,1]})^
let hidden_alpha = img.with_channel(hidden,"a",sparse.a)^
check("hidden-rgb",hidden_alpha != sparse and hidden_alpha == img.adopt(img.materialize(hidden_alpha))^)
check("metadata",img.with_properties(img.of_raster(sparse),{width:521})^ != img.of_raster(sparse))
let wrong_shape = img.blank({width:260,height:4})^
check("shape",wrong_shape != sparse)
let gray = img.blank({width:520,height:2,channels:1})^
check("channels",gray != sparse)
let red = img.with_region(sparse,[256,1,257,2],img.blank({width:1,height:1,color:[1,0,0,1]})^.pixels)^
check("changed-tile",red != sparse and red == img.from_bytes(img.to_bytes(red)^,{width:520,height:2})^)
check("not-identity-equality",[nan] != [nan])
