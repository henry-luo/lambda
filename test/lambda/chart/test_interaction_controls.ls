// Resolved selections, camera domains and event filters share per-chart state (S12.1.3).
import p: lambda.chart.parameter
import i: lambda.chart.interaction
import s: lambda.chart.scale
import streams: lambda.chart.event_stream
import e: lambda.chart.expression
import tr: lambda.chart.transform
import parse: lambda.chart.parse
import chart: lambda.chart.chart
import vega: lambda.chart.vega

fn frame(defs, view_key = "chart") => {view:view_key,params:defs,encoding:{x:{field:"x"},y:{field:"y"}},
    scales:{x:s.linear_scale(0,10,0,100),y:s.linear_scale(0,10,100,0)},width:100,height:100,
    data:[{group:"A",x:2,y:3},{group:"B",x:8,y:7}]}
fn click(st, fr, group) => i.update(st,{type:"click",frame:fr,row:{group:group}})
fn resolved(mode) {
    let defs = [{name:"pick",select:{type:"point",fields:["group"],resolve:mode}}];
    let a = click(p.initial(defs),frame(defs,"left"),"A");
    let b = click(a,frame(defs,"right"),"B");
    {value:b.values.pick, again:click(b,frame(defs,"right"),"A").values.pick}
}
let union = resolved("union")
let intersection = resolved("intersect")
let camera = [{name:"camera",select:{type:"interval",encodings:["x"]},bind:"scales"}]
let camera_frame = frame(camera)
let zoomed = i.update(p.initial(camera),{type:"wheel",frame:camera_frame,x:50,y:50,deltaY:-100})
let zoom_extent = p.extent(zoomed.values.camera,"x")
let pan_start = i.update(zoomed,{type:"pointerdown",frame:camera_frame,x:50,y:50,button:0})
let panned = i.update(pan_start,{type:"pointerup",frame:camera_frame,x:60,y:50})
let pan_extent = p.extent(panned.values.camera,"x")
let near = [{name:"nearest",select:{type:"point",encodings:["x"],nearest:true,on:"pointerover"}}]
let hovered = i.update(p.initial(near),{type:"mouseover",frame:frame(near),x:79,y:20})
let filtered = [{name:"brush",select:{type:"interval",encodings:["x"],
    on:"[pointerdown[event.shiftKey && event.x > 0], window:pointerup] > window:pointermove!",clear:false}}]
let filtered_frame = frame(filtered)
let refused = i.update(p.initial(filtered),{type:"pointerdown",frame:filtered_frame,x:20,y:50,button:0,shiftKey:false})
let started = i.update(p.initial(filtered),{type:"pointerdown",frame:filtered_frame,x:20,y:50,button:0,shiftKey:true})
let finished = i.update(started,{type:"pointerup",frame:filtered_frame,x:70,y:80})
let kept = i.update(finished,{type:"dblclick",frame:filtered_frame})
let translated_start = i.update(finished,{type:"pointerdown",frame:filtered_frame,x:50,y:50,shiftKey:true})
let translated = i.update(translated_start,{type:"pointerup",frame:filtered_frame,x:60,y:50})
let discrete = [{name:"brush",select:{type:"interval",encodings:["x"]}}]
let categorical_frame = {*:frame(discrete),scales:{x:s.band_scale(["A","B","C"],0,90,0)},encoding:{x:{field:"group"}}}
let categorical_start = i.update(p.initial(discrete),{type:"pointerdown",frame:categorical_frame,x:0,y:0})
let categorical = i.update(categorical_start,{type:"pointerup",frame:categorical_frame,x:70,y:90})
let initial_spec = {encoding:{x:{field:"amount"}},params:[{name:"brush",select:{type:"interval",encodings:["x"]},value:{x:[2,6]}}]}
let initial = p.initial(p.definitions(initial_spec))
let temporal = p.initial(p.definitions({encoding:{x:{field:"when",dtype:"temporal"}},
    params:[{name:"dates",select:{type:"interval",encodings:["x"]},value:{x:["2024-01-01","2024-01-03"]}}]}))
let input_defs = [{name:"level",value:1},{name:"double",expr:"level * 2"},
    {name:"bound",select:{type:"point",fields:["group","x"]},bind:{group:{input:"text"},x:{input:"range"}}}]
let input_frame = frame(input_defs)
let changed = i.update(p.initial(input_defs),{param:"level",value:4,frame:input_frame})
let bound_group = i.update(changed,{param:"bound",field:"group",value:"A",frame:input_frame})
let bound_x = i.update(bound_group,{param:"bound",field:"x",value:2,frame:input_frame})
let values = p.values(input_defs,bound_x)
let calculated = tr.apply_transforms([{x:2}],p.transforms([{type:"calculate",as:"value",expression:"datum.x * level"}],values))
let legend_defs = [{name:"pick",select:{type:"point",fields:["group"]},bind:"legend"}]
let legend_frame = frame(legend_defs)
let legend_hover = i.update(p.initial(legend_defs),{type:"mouseover",frame:legend_frame,legend:true,row:{group:"A"}})
let legend_clicked = i.update(legend_hover,{type:"click",frame:legend_frame,legend:true,row:{group:"A"}})
let legend_image = chart.render_spec({width:200,height:160,data:[{group:"A",x:2,y:3},{group:"B",x:8,y:7}],
    mark:{kind:"point"},params:legend_defs,encoding:{x:{field:"x"},y:{field:"y"},
    color:{field:"group",dtype:"nominal",condition:{param:"pick",empty:false,value:"red"}}}},null,legend_clicked)
fn legend_entries(node) => if (node is element) [*(if (node.class == "legend-entry") [node] else []),
    for (child in content(node)) for (entry in legend_entries(child)) entry] else []
let checks = [
    {name:"union",ok:p.selected(union.value,{group:"A"}) and p.selected(union.value,{group:"B"})},
    {name:"intersection",ok:not p.selected(intersection.value,{group:"A"}) and not p.selected(intersection.value,{group:"B"})},
    {name:"intersection agreement",ok:p.selected(intersection.again,{group:"A"})},
    {name:"zoom anchor",ok:zoom_extent[0] > 0 and zoom_extent[1] < 10 and abs(sum(zoom_extent)-10) < 0.0001},
    {name:"pan domain",ok:abs(pan_extent[0]-zoom_extent[0]+1) < 0.0001 and abs(pan_extent[1]-zoom_extent[1]+1) < 0.0001},
    {name:"scale binding",ok:p.encoding({x:{field:"x"}},p.values(camera,panned),camera,panned,"chart",false).x.scale.domain == pan_extent},
    {name:"nearest projection",ok:p.selected(hovered.values.nearest,{x:8}) and not p.selected(hovered.values.nearest,{x:2})},
    {name:"event start filter",ok:refused.gesture == null and started.gesture != null},
    {name:"captured stream ends",ok:finished.gesture == null and p.extent(finished.values.brush,"x") == [2,7]},
    {name:"clear disabled",ok:kept.values.brush == finished.values.brush},
    {name:"brush translates with pointer",ok:p.extent(translated.values.brush,"x") == [3,8]},
    {name:"brush keeps scale domain",ok:p.encoding({x:{field:"x",scale:{domain:[0,10]}}},p.values(filtered,finished),filtered,finished,"chart",false).x.scale.domain == [0,10]},
    {name:"clear cancels active drag",ok:i.update(started,{type:"dblclick",frame:{*:filtered_frame,params:[{*:filtered[0],select:{*:filtered[0].select,clear:"dblclick"}}]}}).gesture == null},
    {name:"discrete brushing",ok:p.selected(categorical.values.brush,{group:"A"}) and p.selected(categorical.values.brush,{group:"B"}) and not p.selected(categorical.values.brush,{group:"C"})},
    {name:"initial temporal extent",ok:p.selected(temporal.values.dates,{when:"2024-01-02"}) and not p.selected(temporal.values.dates,{when:"2024-01-05"})},
    {name:"initial encoding extent",ok:p.extent(initial.values.brush,"amount") == [2,6]},
    {name:"dependent input",ok:values.level == 4 and values.double == 8},
    {name:"bound tuple",ok:p.selected(values.bound,{group:"A",x:2}) and not p.selected(values.bound,{group:"A",x:3})},
    {name:"legend uses click",ok:p.empty(legend_hover.values.pick) and p.selected(legend_clicked.values.pick,{group:"A"},false)},
    {name:"legend retains data domain",ok:(parse(legend_entries(legend_image)[0]['data-chart-legend'], 'json')^).row.group == "A"},
    {name:"parameter calculation",ok:calculated[0].value == 8},
    {name:"projected expression values",ok:e.evaluate(e.compile("bound.group[0]",p.expression_values(values)),null) == "A"},
    {name:"timed streams diagnosed",ok:p.initial([{name:"bad",select:{type:"point",on:{type:"click",debounce:100}}}]) is error},
    {name:"unknown event diagnosed",ok:p.initial([{name:"bad",select:{type:"point",on:"timer"}}]) is error},
    {name:"external event source diagnosed",ok:streams.validate({type:"click",source:"#external"}) is error},
    {name:"reserved variable",ok:p.initial([{name:"true",value:1}]) is error},
    {name:"reserved event scope",ok:p.initial([{name:"parent",value:1}]) is error and p.initial([{name:"item",value:1}]) is error},
    {name:"variable predicates",ok:parse.test_predicate(p.predicate({param:"enabled"},{enabled:[]}),{}) == true and
        parse.test_predicate(p.predicate({param:"enabled"},{enabled:0}),{}) == false and
        parse.test_predicate(p.predicate({param:"enabled"},{enabled:null}),{}) == false},
    {name:"unknown predicate on empty data",ok:chart.render_spec(vega.convert({data:{values:[]},mark:"point",
        transform:[{filter:{param:"missing"}}]})) is error},
    {name:"element calculation tag",ok:tr.apply_transforms([{x:2}],p.transforms(<transform <calculate as:"result",expression:"datum.x * level">>,values))[0].result == 8},
    {name:"invalid between",ok:streams.validate({type:"pointermove",between:["pointerdown"]}) is error},
    {name:"unsupported input",ok:p.initial([{name:"value",value:1,bind:{input:"file"}}]) is error},
    {name:"external input",ok:p.initial([{name:"value",value:1,bind:{input:"text",element:"#input"}}]) is error},
    {name:"comma in key filter",ok:streams.matches("keydown[event.key === ',']",{type:"keydown",key:","})}
];
[for (check in checks where check.ok != true) check.name]
