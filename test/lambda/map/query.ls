import maps: lambda.map
let land = {type:"Polygon",coordinates:[
    [[-40,-25],[40,-25],[40,25],[-40,25],[-40,-25]],
    [[-10,-10],[10,-10],[10,10],[-10,10],[-10,-10]]]};
let model = maps.geomap([
    maps.source("land",{type:"Feature",id:"land",properties:{name:"land"},geometry:land}),
    maps.source("dots",{type:"Feature",id:42,properties:{},geometry:{type:"Point",coordinates:[0,0]}}),
    maps.layer("land","fill","land",{'fill-color':"#0a0"}),
    maps.layer("dots","circle","dots",{'circle-color':"#f00",'circle-radius':8})
],{width:256,height:192,zoom:1});
[
    maps.query_source(model,"dots")[0].feature_id,
    maps.query_rendered(model,[128,96]) |> ~.layer,
    maps.query_rendered(model,[148,96]) |> ~.layer,
    maps.query_rendered(model,[178,96]) |> ~.layer,
    maps.query_rendered(model,[-1,96]),
    maps.query_source(model,"missing") is error,
    maps.query_rendered(model,[inf,0]) is error,
    maps.query_rendered(maps.geomap([content(model)[1],
        maps.layer("faint","circle","dots",{'circle-color':"#ff000080",'circle-opacity':0.001})],
        {width:256,height:192,zoom:1}),[128,96])
]
