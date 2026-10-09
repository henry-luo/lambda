import maps: lambda.map
let source = maps.source("points",{type:"Point",coordinates:[0,0]});
let dots = maps.layer("dots","circle","points",{'circle-color':"#f00",'circle-radius':8});
let good = maps.geomap([source,dots],{width:256,height:192});
let normalized = maps.normalize(good);
[
    maps.validate(good),
    name(normalized), normalized.center, normalized.zoom,
    maps.validate(<geomap *[source,source,dots]>) is error,
    maps.validate(maps.geomap([maps.layer("bad","raster","points")])) is error,
    maps.validate(maps.geomap([source,maps.layer("bad","fill","absent")])) is error,
    maps.validate(maps.geomap([source,maps.layer("bad","circle","points",{'circle-radius':["feature-state","radius"]})])) is error,
    maps.validate(maps.geomap([source,maps.layer("bad","circle","points",{'circle-stroke-color':"#000"})])) is error,
    maps.validate(maps.geomap([maps.source("bad",{type:"Point",coordinates:[200,0]})])) is error,
    maps.validate(maps.geomap([maps.source("bad",{type:"Polygon",coordinates:[[[0,0],[1,0],[1,1],[0,1]]]})])) is error,
    maps.validate(maps.geomap([<source *:source,cluster:true>,dots])) is error,
    maps.validate(maps.geomap([source,<layer *:dots,unexpected:true>])) is error
]
