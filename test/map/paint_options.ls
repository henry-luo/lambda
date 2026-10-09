import maps: lambda.map
let camera={width:256,height:192,zoom:0};
fn p(x,y) => maps.unproject(camera,[x,y])
let image=maps.geomap([
    maps.source("land",{type:"Polygon",coordinates:[[p(20,20),p(120,20),p(120,100),p(20,100),p(20,20)]]}),
    maps.source("dot",{type:"Point",coordinates:p(180,50)}),
    maps.source("roads",{type:"MultiLineString",coordinates:[[p(40,140),p(100,140)],[p(160,140),p(220,140)]]}),
    maps.layer("paper","background",null,{'background-color':"#fff"}),
    maps.layer("land","fill","land",{'fill-color':"#0a0",'fill-outline-color':"#f00"}),
    maps.layer("dot","circle","dot",{'circle-color':"#f00",'circle-radius':10,'circle-stroke-color':"#00f",'circle-stroke-width':6}),
    maps.layer("roads","line","roads",{'line-color':"#00f",'line-width':12},{'line-cap':"square",'line-join':"bevel"})
],{*:camera,id:"viewport",style:"display:block"});
<html <head <style "html,body{margin:0;background:#fff}">> <body image>>
