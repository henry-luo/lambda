import timeline: lambda.chart.timeline
import coordinate: lambda.chart.timeline.coordinate
import layout: lambda.chart.timeline.layout

let spec={id:"gestures",view:"axis",width:800,height:400,padding:0,scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},
    interaction:{pan:true,pinch:true},events:[{id:"a",at:20},{id:"b",at:80}]}
let initial=timeline.initial_state(spec)
let plan=timeline.layout(spec,{width:800,height:400},initial)
let context={spec:spec,layout:plan,viewport:{width:800,height:400},now:100}
let start=timeline.update(initial,{type:"gesture_start",point:[400,200],pointer_id:1},context).state
let moved=timeline.update(start,{type:"gesture_move",point:[480,200],pointer_id:1},context)
let ended=timeline.update(moved.state,{type:"gesture_end",point:[480,200],pointer_id:1},context).state
let cancelled=timeline.update(moved.state,{type:"gesture_cancel"},context).state
let tiny=timeline.update(start,{type:"gesture_move",point:[402,200],pointer_id:1},context).state
let second=timeline.update(start,{type:"gesture_start",point:[600,200],pointer_id:2},context).state
let pinched=timeline.update(second,{type:"gesture_move",point:[700,200],pointer_id:2},context).state
let other=timeline.update(start,{type:"gesture_move",point:[700,200],pointer_id:2},context).state
let bounded={*:spec,window:{from:0,to:100,min:0,max:100}}
let clamped=timeline.update(start,{type:"gesture_move",point:[700,200],pointer_id:1},{*:context,spec:bounded}).state
let controlled={*:spec,value:{window:{from:0,to:100}}}
let requested=timeline.update(start,{type:"gesture_move",point:[480,200],pointer_id:1},{*:context,spec:controlled})
let curved={*:spec,coordinate:{kind:"spiral",turns:2}}
let curve_plan=timeline.layout(curved,{width:800,height:400},initial)
let curve_start=timeline.update(initial,{type:"gesture_start",point:coordinate.project(curve_plan.path,0.4).point,pointer_id:1},
    {*:context,spec:curved,layout:curve_plan}).state
let curve_moved=timeline.update(curve_start,{type:"gesture_move",point:coordinate.project(curve_plan.path,0.5).point,pointer_id:1},
    {*:context,spec:curved,layout:curve_plan}).state
let large=100000000000000000000000m
let deep={*:spec,window:{from:large,to:large+10},events:[{id:"a",at:large+1},{id:"b",at:large+2}]}
let deep_state=timeline.initial_state(deep)
let deep_zoom=timeline.update(deep_state,{type:"zoom",factor:0.5},{spec:deep}).state;
[
 ["gesture starts",start.gesture!=null and start.window==initial.window],
 ["pan window",abs(moved.state.window.from+10)<0.000001 and abs(moved.state.window.to-90)<0.000001],
 ["pan emits shared notification",moved.notifications[0].name=="timeline_window_change"],
 ["end releases state",ended.gesture==null and ended.suppress_click_until==600],
 ["cancel restores committed window",cancelled.gesture==null and cancelled.window==initial.window],
 ["movement threshold",tiny.window==initial.window],
 ["pinch zooms",abs((pinched.window.to-pinched.window.from)-100*2/3)<0.000001],
 ["pointer isolation",other.window==initial.window],
 ["pan respects bounds",clamped.window==initial.window],
 ["controlled window requests",requested.state.window==initial.window and requested.requests[0].field=="window"],
 ["curved pan shares inverse",abs(curve_moved.window.from+10)<0.001],
 ["disabled pan",timeline.update(initial,{type:"gesture_start",point:[400,200]},{spec:{*:spec,interaction:{pan:false,pinch:false}}}).state.gesture==null],
 ["click to use gate",timeline.initial_state({*:spec,interaction:{click_to_use:true}}).engaged==false],
 ["engage action",timeline.update(initial,{type:"engage"},context).state.engaged==true],
 ["exact deep window",deep_state.window.from==large and deep_state.window.to==large+10],
 ["exact deep zoom",deep_zoom.window.from==large+2.5m and deep_zoom.window.to==large+7.5m],
 ["exact deep constraints",layout.constrain({from:large,to:large+2},{min_span:4})=={from:large-1,to:large+3}]
]
