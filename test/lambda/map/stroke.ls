import maps: lambda.map
let camera={width:256,height:192,zoom:0};
fn position(x,y) => maps.unproject(camera,[x,y])
let line=maps.source("road",{type:"Feature",id:"road",properties:{},geometry:{type:"LineString",coordinates:[position(64,96),position(160,96)]}});
fn model(cap) => maps.geomap([line,maps.layer("road","line","road",{'line-color':"#00f",'line-width':10},{'line-cap':cap,'line-join':"bevel"})],camera)
let butt=maps.plan(model("butt")),round=maps.plan(model("round")),square=maps.plan(model("square"));
let polygon=maps.geomap([maps.source("land",{type:"Polygon",coordinates:[
    [position(32,32),position(224,32),position(224,160),position(32,160),position(32,32)],
    [position(96,64),position(160,64),position(160,128),position(96,128),position(96,64)]]}),
    maps.layer("land","fill","land",{'fill-color':"#0a0",'fill-outline-color':"#f00"})],camera);
let land=maps.plan(polygon);
[
    len(maps.query_rendered(butt,[61,96])),len(maps.query_rendered(round,[61,96])),len(maps.query_rendered(square,[61,96])),
    len(maps.query_rendered(round,[60,100])),len(maps.query_rendered(square,[60,100])),
    len(maps.query_rendered(butt,[[60,94],[62,98]])),len(maps.query_rendered(round,[[60,94],[62,98]])),
    len(maps.query_rendered(round,[[60,100.5],[61,101.5]])),len(maps.query_rendered(square,[[60,100.5],[61,101.5]])),
    len(maps.query_rendered(land,[128,96])),len(maps.query_rendered(land,[[110,80],[140,110]])),
    len(maps.query_rendered(land,[[90,80],[110,110]])),len(maps.query_rendered(land,[[0,0],[256,192]])),
    maps.validate(model("invalid")) is error,
    maps.validate(maps.geomap([line,maps.layer("bad","fill","road",{'fill-outline-color':"nope"})],camera)) is error
]
