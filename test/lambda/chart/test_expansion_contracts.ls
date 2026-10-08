// Coordinate, composite, and animation edge contracts use the public rendering boundary.
import chart:lambda.chart.chart
import paths:lambda.chart.path_geometry
import coordinate:lambda.chart.coordinate
import projection:lambda.chart.projection
import behavior:lambda.chart.behavior
import parameter:lambda.chart.parameter
import animation:lambda.chart.animation
import timeline:lambda.chart.timeline
import picking:lambda.chart.picking
import util:lambda.chart.util
fn check(label,valid)=>if (valid) null else label
fn elements(node)=>if (node is element) [node,for (child in content(node)) for (value in elements(child)) value] else []
fn records(image)=>[for (node in elements(image) where node["data-chart-row"]!=null)
    {row:parse(node["data-chart-row"],'json') ^ {~},view:node["data-chart-view"],focus:node["data-focus-key"]}]
let enc={x:{field:"x",dtype:"quantitative",axis_enabled:false},y:{field:"y",dtype:"quantitative",axis_enabled:false},key:{field:"id"}}
let rows=[{id:"a",g:"A",x:1,y:2},{id:"b",g:"B",x:3,y:4}]
let base={data:rows,width:240,height:160,padding:0,mark:{kind:"point"},encoding:enc}
let factory={*:base,facet:{field:"g"},mark:{kind:"composite",expand:(data,options)=>
    {layer:[{data:[{id:data[0].id,x:data[0].x,y:len(data)}],mark:{kind:"point"},interaction:{element_select:true}}]}}}
let faceted=chart.render_spec(factory,null,{values:{}})
let reordered=chart.render_spec({*:factory,data:reverse(rows)},null,{values:{}})
let a=(records(faceted) |: ~.row.id=="a")[0]
let again=(records(reordered) |: ~.row.id=="a")[0]
let tree=chart.render_spec({width:200,height:200,padding:0,data:[{id:"r"},{id:"a",parent:"r",value:1}],
    mark:{kind:"tree",labels:false},coordinate:{type:"polar",inner_radius:0.2,outer_radius:0.6}})
let circles=elements(tree) |: ~.class=="tree-node"
let vector=chart.render_spec({width:240,height:160,padding:0,data:[{id:"v",lon:0,lat:0,direction:0,magnitude:20}],
    coordinate:{type:"geo",projection:{type:"equirectangular"}},mark:{kind:"vector"},
    encoding:{longitude:{field:"lon"},latitude:{field:"lat"},key:{field:"id"}}})
let vector_path=(elements(vector) |: name(~)=='path')[0]
let vector_points=paths.sample(vector_path.d)[0].points
let pm=projection.configure(240,160,{type:"equirectangular"})
let story={*:base,timeline:{keyframes:[{at:0,spec:{params:[{name:"count",value:0}],interaction:{count_clicks:{param:"count",on:"click",
    handler:(event,data,values,coordinate)=>{updates:{count:values.count+1}}}}}},{at:1000,spec:{data:reverse(rows)}}]}}
let story_defs=parameter.definitions(behavior.prepare(story))
let disabled=chart.render_frame({*:base,animate:false,encoding:{x:enc.x,y:enc.y},timeline:{keyframes:[
    {at:0,spec:{data:rows}},{at:1000,spec:{data:reverse(rows)}}]}},500)
let fade=chart.render_frame({*:base,animate:{update:{type:"fade",duration:1000}}},
    {time_ms:500,previous:{*:base,data:[{id:"a",x:5,y:5}]}})
let hole_a=<path d:"M0 0 L40 0 L40 40 L0 40 Z M10 10 L10 20 L20 20 L20 10 Z">
let hole_b=<path d:"M0 0 L40 0 L40 40 L0 40 Z M50 50 L50 60 L60 60 L60 50 Z">
let fractional=timeline.command({phase:"paused",elapsed:0,reversed:false},{command:"seek",position:0.25},0,
    {duration:1000,length:0.5,repeat:1,direction:"normal",fill:"forwards"})
let post_close=paths.sample("M0 0 L10 0 L10 10 Z L20 20")
let brush={name:"picked",select:{type:"point",fields:["id"]},value:[{id:"a"}]}
let graph={nodes:[{id:"a"},{id:"b"}],links:[{id:"ab",source:"a",target:"b",value:2}]}
let filtered=chart.render_spec({width:240,height:160,data:graph,params:[brush],encoding:{key:{field:"id"}},
    mark:{kind:"force_graph",iterations:0,labels:false,parts:{node:{interaction:{legend_filter:{param:"picked"}}}}}},null,{values:{picked:parameter.initial([brush]).values.picked}})
let gradient={gradient:"linear",stops:[{offset:0,color:"#ff0000"},{offset:1,color:"#0000ff"}]}
let painted=chart.render_spec({*:base,mark:{kind:"point"},params:[brush],interaction:{element_select:{param:"picked"}},state:{selected:{fill:gradient}}});
let parallel=chart.render_spec({*:base,mark:{kind:"line"},coordinate:{type:"parallel"},
    encoding:{position:["x","y"],key:{field:"id"}},interaction:{brush_axis_highlight:true}},null,{values:{}})
let density=chart.render_spec({*:base,mark:{kind:"density",resolution:3},interaction:{element_select:true}},null,{values:{}})
let cells=records(density)
let reduced={*:base,timeline:{keyframes:[{at:0,spec:{data:rows}},{at:500,spec:{data:[rows[0]]}},
    {at:1000,spec:{data:[rows[1]]}}]}};
[check("compact arc flags",paths.parse("M0 0 A10 10 0 0110 20")[1].end==[10.0,20.0]),
 check("path after close",len(post_close)==2 and post_close[0].closed and not post_close[1].closed and post_close[1].points[0]==[0.0,0.0]),
 check("invalid plot path",chart.render_spec({*:base,mark:{kind:"path",d:"M0 0 A1 2"}}) is error),
 check("invalid polygon hole",chart.render_spec({*:base,data:[{x:0,y:0,holes:[[[0,0],[1,1],[nan,2]]]},{x:1,y:0},{x:0,y:1}],mark:{kind:"polygon"}}) is error),
 check("factory partitions",len(records(faceted))==2 and all(records(faceted) |> ~.row.y==1)),
 check("factory partition identity",a.view==again.view and a.focus==again.focus),
 check("factory parameter identity",sort(parameter.definitions(behavior.prepare(factory)) |> ~.name)==sort(parameter.definitions(behavior.prepare({*:factory,data:reverse(rows)})) |> ~.name)),
 check("tree polar root",abs(paths.distance([circles[0].cx,circles[0].cy],[100,100])-20)<0.00001),
 check("tree polar leaf",abs(paths.distance([circles[1].cx,circles[1].cy],[100,100])-60)<0.00001),
 check("projected vector origin",paths.distance(vector_points[0],projection.project([0,0],pm))<0.00001),
 check("projected vector endpoint",paths.distance(vector_points[len(vector_points)-1],projection.project([20,0],pm))<0.001),
 check("keyframe parameter catalogue",len(story_defs)==1 and story_defs[0].name=="count"),
 check("disabled timeline key",disabled is element),
 check("update fade",any(elements(fade) |> ~.class=="chart-crossfade")),
 check("hole topology fallback",animation.morph(hole_a,hole_b,0.5)==null),
 check("hole topology diagnostic",animation.morph(hole_a,hole_b,0.5,"error") is error),
 check("fractional seek",fractional.elapsed==500),
 check("duplicate position field",coordinate.vector_axes(rows,["x","x","y"],coordinate.configure({type:"radar"},200,200)) is error),
 check("singular position axis",coordinate.vector_axes(rows,["x","y"],coordinate.configure({type:"parallel",transform:[{type:"scale",factors:[0,0]}]},200,200)) is error),
 check("part filtering",len(elements(filtered) |: ~["data-chart-part"]=="node")==1),
 check("state gradient resolution",any(elements(painted) |> starts_with(~.fill,"url(#"))),
 check("interactive position shorthand",parallel is element and len(elements(parallel) |: ~.class=="parallel-series")==2),
 check("density record identity",len(cells)==9 and len(util.unique_vals(cells |> ~.row.id))==9 and all(cells |> ~.row.source_rows==rows and util.finite_number(~.row.density))),
 check("reduced timeline target",chart.render_frame(reduced,{time_ms:0,reduced_motion:true})==chart.render_spec(reduced)),
 check("duplicate behavior id",behavior.prepare({*:base,interaction:{element_select:{id:"same"},element_highlight:{id:"same"}}}) is error)] |: ~!=null
