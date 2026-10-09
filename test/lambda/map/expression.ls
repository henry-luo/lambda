import maps: lambda.map
import expressions: lambda.map.expression
let feature = {type:"Feature",id:7,properties:{kind:"city",radius:8,color:"#f00",present:null,truth:0},
    geometry:{type:"MultiPoint",coordinates:[[0,0]]}};
fn layer(paint, filter = null) => <layer id:"dots",type:"circle",source:"places",paint:paint,filter:filter>
fn size(expression) => expressions.evaluate(layer({'circle-radius':expression}),feature,1.5).size
fn rejected(expression) => expressions.validate(layer({'circle-radius':expression})) is error
let filtered = layer({'circle-radius':["interpolate",["linear"],["zoom"],0,4,2,12],
    'circle-color':["match",["get","kind"],["city","capital"],"#f00","#00f"]},
    ["all",["==",["geometry-type"],"Point"],["==",["id"],7],["==",["zoom"],1]]);
let evaluated = expressions.evaluate(filtered,feature,1.5);
let model = maps.geomap([maps.source("places",feature),filtered],{width:256,height:192,zoom:1.5});
let fallback = expressions.evaluate(layer({'circle-radius':["get","kind"],
    'circle-color':["get","radius"],'circle-opacity':["get","absent"]}),feature,1.5);
[
    maps.validate(model), evaluated.visible, evaluated.color, evaluated.size, evaluated.dependencies,
    maps.query_rendered(model,[137,96]) |> ~.feature_id,
    maps.query_rendered(model,[140,96]),
    expressions.evaluate(filtered,feature,2.0).visible,
    size(["get","radius"]), size(["number",["get","kind"],6]),
    size(["coalesce",["get","present"],["get","absent"],9]),
    size(["case",["has","present"],11,3]),
    size(["case",["==",["get","absent"],null],12,3]),
    size(["step",["get","radius"],1,5,2,8,3]),
    size(["case",["all",false,["boolean",["get","truth"]]],2,7]),
    size(["case",["any",true,["boolean",["get","truth"]]],7,2]),
    size(["get","radius",["literal",{radius:13}]]),
    size(["match",["get","absent"],"city",1,14]),
    fallback.color, fallback.alpha, fallback.size, fallback.warnings,
    expressions.evaluate(layer({},["get","truth"]),feature,1).visible,
    expressions.evaluate(layer({},["get","truth"]),feature,1).warnings,
    rejected(["get"]), rejected(["bogus",1]),
    rejected(["case",true,1,["bogus",1]]),
    rejected(["interpolate",["linear"],["zoom"],2,1,1,2]),
    rejected(["interpolate",["exponential",2],["zoom"],0,1,2,2]),
    rejected(["case",[">",["zoom"],1],2,3]),
    rejected(["match",["get","kind"],"city",1,["city","capital"],2,3]),
    rejected(["match",["get","kind"],"city",1,7,2,3]),
    rejected(["case",0,1,2]), rejected(["case",true,1,"oops"]),
    rejected(["step",["zoom"],1,1,2,1,3]),
    rejected(["case",["<",["get","a"],["get","b"]],1,2]),
    size(["case",[">",["number",["get","radius"]],5],10,1]),
    size(["case",["==",["string",["get","kind"]],"city"],10,1]),
    contains(format(maps.to_svg(model),'xml'),"rgb(255,0,0)"),
    rejected(["coalesce",null,["get","radius"],9]),
    rejected(["case",true,["get","radius"],"oops"]),
    expressions.evaluate_features(layer({'circle-radius':["get","radius"]}),
        [feature,{*:feature,properties:{radius:12}}],1) |> ~.size
]
