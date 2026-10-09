import maps: lambda.map
let source=maps.source("dots",{type:"Feature",id:42,properties:{name:"center",tag:parse("\"a\\u0000b\"",'json')^},geometry:{type:"Point",coordinates:[0,0]}});
let dot=maps.layer("dots","circle","dots",{'circle-color':"#f00",'circle-radius':8,'circle-stroke-color':"#00f",'circle-stroke-width':4});
let model=maps.geomap([source,dot],{width:256,height:192,zoom:1});
let frame=maps.plan(model);
let moved=maps.plan(maps.geomap([source,dot],{width:256,height:192,zoom:1,center:[40,0]}));
[
    frame.type,frame.width,frame.height,
    maps.query_rendered(frame,[128,96]) |> ~.feature_id,
    maps.query_rendered(frame,[139,96]) |> ~.feature_id,
    maps.query_rendered(frame,[[137,94],[139,98]]) |> ~.layer,
    maps.query_rendered(frame,[[141,94],[143,98]]),
    maps.query_rendered(frame,[128,96],{layers:["missing"]}),
    maps.query_rendered(frame,[128,96])[0].feature.properties.name,
    len(maps.query_rendered(frame,[128,96])[0].feature.properties.tag),
    maps.query_rendered(moved,[128,96]),
    maps.query_rendered(frame,[128,96]) |> ~.feature_id,
    maps.query_rendered(frame,[[0,0],[256,192]]) |> ~.feature_id,
    maps.query_rendered(frame,[[256,192],[0,0]]) is error,
    maps.query_rendered(frame,[[0,0],2]) is error,
    maps.query_rendered(frame,[128,96],{radius:-1}) is error,
    maps.query_rendered(frame,[128,96],{layers:"dots"}) is error,
    maps.query_rendered({*:frame,index:[frame.index[0],frame.index[1],[0,0,256,192,2,2,-1]]},[128,96]) is error,
    maps.query_rendered({*:frame,paths:[for (path in frame.paths) {*:path,cap:8}]},[128,96]) is error,
    name(maps.render_frame(frame))
]
