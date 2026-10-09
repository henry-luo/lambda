import maps: lambda.map
let model = maps.geomap([
    maps.source("point",{type:"Point",coordinates:[0,0]}),
    maps.layer("paper","background",null,{'background-color':"#eef3f6"}),
    maps.layer("dot","circle","point",{'circle-color':"#ff0000",'circle-radius':8})
],{width:256,height:192,zoom:1});
let image = maps.to_svg(model);
let source = if (image is error) "" else format(image,'xml');
[
    name(image), image.width, image.height,
    contains(source,"rgb(255,0,0)"), contains(source,"clipPath"),
    maps.to_svg(<geomap pitch:30>) is error,
    maps.to_svg(model,{width:-1,height:10}) is error
]
