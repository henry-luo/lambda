import img: lambda.image
import exports: lambda.editor.raster.export
let raster = img.blank({width:4,height:2,color:[1.0,0.0,0.0,0.5]})^
let doc = img.document(img.stack([img.layer(raster,{id:"red",name:"Red"})]),{width:4,height:2})
let kept = exports.prepare(doc,"png")^
img.render_frame(kept.image)^ == raster
exports.settings(doc,"png",{width:8})^.height == 4
exports.settings(doc,"png",{height:1})^.width == 2
let resized = exports.prepare(doc,"png",{width:3,height:3})^
resized.image.width == 3 and resized.image.height == 3 and img.render_frame(resized.image)^.width == 3
doc.width == 4 and img.render_frame(doc)^ == raster
let flat = exports.prepare(doc,"jpeg",{background:"#ffffff",quality:75})^
let pixel = img.sample(img.render_frame(flat.image)^,0,0)^
pixel[0] == 255 and pixel[1]>170 and pixel[1]<205 and pixel[3] == 255
flat.settings.quality == 75 and flat.settings.alpha == "flatten"
let blue = exports.prepare(img.of_raster(img.blank({width:1,height:1})^),"png",{alpha:"flatten",background:"#0000ff"})^
img.sample(img.render_frame(blue.image)^,0,0)^ == [0,0,255,255]
exports.destination("photo.PNG","jpeg")^ == "photo.jpg" and exports.destination("photo.jpg","png")^ == "photo.png"
exports.settings(doc,"jpg",{background:[1,1,1,1]})^.format == "jpeg"
exports.settings(doc,"jpeg") ^ { true } ~ { false }
exports.settings(doc,"jpeg",{alpha:"preserve"}) ^ { true } ~ { false }
exports.settings(doc,"png",{alpha:"flatten",background:"transparent"}) ^ { true } ~ { false }
exports.settings(doc,"png",{background:"#ffffff"}) ^ { true } ~ { false }
exports.settings(doc,"png",{width:0}) ^ { true } ~ { false }
exports.settings(doc,"png",{width:4001,height:4001}) ^ { true } ~ { false }
exports.settings(doc,"jpeg",{quality:101,background:"#ffffff"}) ^ { true } ~ { false }
exports.settings(doc,"png",{quality:75}) ^ { true } ~ { false }
exports.settings(doc,"png",{width:2.5}) ^ { true } ~ { false }
exports.settings(doc,"png",{unexpected:true}) ^ { true } ~ { false }
