import timeline: lambda.chart.timeline
import spatial: lambda.chart.coordinate
import coordinate: lambda.chart.timeline.coordinate
import series: lambda.chart.timeline.series

let data=[{at:0,value:0,category:"A"},{at:5,value:50,category:"A"},{at:10,value:100,category:"A"}]
let overlay={id:"values",value_scale:"value",data:data,mark:"point",encoding:{x:{field:"at"},y:{field:"value",dtype:"quantitative"}}}
let spec={id:"overlays",view:"axis",width:800,height:480,scales:[{kind:"numeric",unit:"year",domain:[0,10]},
    {id:"value",kind:"linear",domain:[0,100]}],events:[{id:"a",at:0},{id:"b",at:10}],series:[overlay],axes:[{scale:"value",title:"Measure"}]}
let plan=timeline.layout(spec,{width:800,height:480})
let model=plan.series[0]
let center=coordinate.project(plan.path,0.5).point
let projected=spatial.project(model.projection,[0.5,0.5])
let marks=[for (kind in ["point","line","step","area","column","range"],
    let current=timeline.layout({*:spec,series:[{*:overlay,mark:kind,encoding:{*:overlay.encoding,
        x2:if (kind=="range") {datum:10} else null,y2:if (kind=="range") {datum:100} else null}}]},null))
    series.render(current.series[0])]
let curve=timeline.layout({*:spec,coordinate:{kind:"serpentine",levels:3}},null)
let curved_marks=[for (kind in ["point","line","step","area","column","range"],
    let current=timeline.layout({*:spec,coordinate:{kind:"serpentine",levels:3,lane_width:80},series:[{*:overlay,mark:kind}]},null))
    series.render(current.series[0])]
let shared=timeline.layout({*:spec,scales:[spec.scales[0],{id:"value",kind:"linear"}],series:[overlay,
    {*:overlay,id:"larger",data:[{at:0,value:200},{at:10,value:300}]}]},null)
let authored=<timeline.chart id:"native-series",view:"axis",width:800,height:480,
    <timeline.scale kind:"numeric",unit:"year",domain:[0,10]>
    <timeline.scale id:"value",kind:"linear",domain:[0,100]>
    <timeline.series id:"values",value_scale:"value",
        <data values:data>
        <mark type:"point">
        <encoding <x field:"at"> <y field:"value",dtype:"quantitative">>>
>;
[
 ["series model",len(plan.series)==1 and len(model.data)==3],
 ["shared chronology fraction",model.data[1]._timeline_x==0.5],
 ["center follows timeline",abs(center[0]-projected[0])<0.00001 and abs(center[1]-projected[1])<0.00001],
 ["custom projection inverse",spatial.invert(model.projection,projected)==[0.5,0.5]],
 ["native mark renderers",len(marks)==6 and all([for (image in marks) image is element and name(image)=='g'])],
 ["quantitative axis",len(plan.value_axes)==1],
 ["curved series projection",spatial.project(curve.series[0].projection,[0.5,0.5])==coordinate.project(curve.path,0.5).point],
 ["named scale domain union",shared.series[0].mapping.domain==shared.series[1].mapping.domain and shared.series[0].mapping.domain[1]>=300],
 ["native child vocabulary",len(timeline.layout(authored,null).series[0].data)==3],
 ["hidden series",len(timeline.layout({*:spec,series:[{*:overlay,visible:false}]},null).series)==0],
 ["invalid chronology value",timeline.layout({*:spec,series:[{*:overlay,data:[{at:"bad",value:1}]}]},null) is error],
 ["invalid measure value",timeline.layout({*:spec,series:[{*:overlay,data:[{at:1,value:"bad"}]}]},null) is error],
 ["unsupported mark diagnosed",timeline.layout({*:spec,series:[{*:overlay,mark:"unknown"}]},null) is error],
 ["unknown native child diagnosed",timeline.validate(<timeline.chart <timeline.series <unexpected>>>) is error],
 ["source records preserved",data[1]=={at:5,value:50,category:"A"}],
 ["SVG presenter includes overlay",timeline.render_svg(spec,null) is element],
 ["curved marks meet projection precision",all([for (image in curved_marks) image is element])]
]
