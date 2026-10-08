// Parameter values and selections are plain snapshots, ready for template state (S9.1.4).
import p: lambda.chart.parameter
import i: lambda.chart.interaction
import e: lambda.chart.expression
import s: lambda.chart.scale
import parse: lambda.chart.parse
import tr: lambda.chart.transform

let defs = [{name:"cutoff",value:3}, {name:"double",expr:"cutoff * 2"},
    {name:"pick",select:{type:"point",fields:["group"]}},
    {name:"brush",select:{type:"interval",encodings:["x"]}}]
let st = p.initial(defs)
let values = p.values(defs, st)
values.cutoff
values.double
e.evaluate(e.compile("datum.x >= cutoff",values), {x:4})
e.compile("missing + 1") is error
p.selected(values.pick,{group:"a"})
p.selected(values.pick,{group:"a"},false)
let frame = {view:"chart",params:defs,encoding:{x:{field:"x"},y:{field:"y"}},
    scales:{x:s.linear_scale(0,10,0,100),y:s.linear_scale(0,10,100,0)},
    width:100.0,height:100.0,data:[{group:"a",x:2,y:3},{group:"b",x:7,y:8}]}
let clicked = i.update(st,{type:"click",frame:frame,row:{group:"a",x:2}})
p.selected(clicked.values.pick,{group:"a"})
p.selected(clicked.values.pick,{group:"b"})
p.empty(st.values.pick)
let toggled = i.update(clicked,{type:"click",frame:frame,row:{group:"b"},shiftKey:true})
p.selected(toggled.values.pick,{group:"a"}) and p.selected(toggled.values.pick,{group:"b"})
let untoggled = i.update(toggled,{type:"click",frame:frame,row:{group:"a"},shiftKey:true})
p.selected(untoggled.values.pick,{group:"a"},false)
p.selected(untoggled.values.pick,{group:"b"},false)
let down = i.update(clicked,{type:"pointerdown",frame:frame,x:20.0,y:30.0,button:0})
let moved = i.update(down,{type:"pointermove",frame:frame,x:70.0,y:90.0})
let up = i.update(moved,{type:"pointerup",frame:frame,x:70.0,y:90.0})
p.extent(up.values.brush,"x")
p.selected(up.values.brush,{x:5})
p.selected(up.values.brush,{x:9})
up.gesture == null
let filtered = tr.apply_transforms(frame.data,p.transforms([{type:"filter",test:{param:"brush"}}],p.values(defs,up)))
len(filtered)
let enc = p.encoding({color:{condition:{param:"pick",value:"red",empty:false},value:"grey"},
    x:{field:"x",scale:{domain:{param:"brush",encoding:"x"}}}},p.values(defs,up),defs,up,"chart",false);
[parse.channel_value(enc.color,{group:"a"}),parse.channel_value(enc.color,{group:"b"})]
enc.x.scale.domain
p.empty(i.update(up,{type:"dblclick",frame:frame}).values.brush)
let cycle = [{name:"a",expr:"b"},{name:"b",expr:"a"}]
p.values(cycle,p.initial(cycle)) is error
p.initial([{name:"bad",select:"unknown"}]) is error
