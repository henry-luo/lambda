import timeline: lambda.chart.timeline
import navigator: lambda.chart.timeline.navigator
import coordinate: lambda.chart.timeline.coordinate
import gesture: lambda.chart.timeline.gesture

let spec={id:"overview",view:"axis",width:640,height:400,scales:[{kind:"numeric",unit:"year"}],
    window:{from:20,to:40,min:0,max:100,min_span:5},navigator:{visible:true,labels:true},
    events:[{id:"a",at:0,title:"First"},{id:"b",at:50,title:"Middle"},{id:"c",at:100,title:"Last"}]}
let normalized=timeline.validate(spec)
let initial=timeline.initial_state(spec)
let model=navigator.plan(normalized,{width:640},initial)
let from=navigator.window_action(normalized,initial,"from",0.1)
let to=navigator.window_action(normalized,initial,"to",0.9)
let center=navigator.window_action(normalized,initial,"center",0.7)
let p=coordinate.project(model.path,0.3).point
let q=coordinate.project(model.path,0.5).point
let start=gesture.update(initial,{type:"gesture_start",point:p,pointer_id:1,surface:"navigator"},model,normalized)
let moved=gesture.update({*:initial,gesture:start.gesture},{type:"gesture_move",point:q,pointer_id:1},model,normalized)
let rtl=navigator.plan(timeline.validate({*:spec,direction:"rtl"}),null,initial)
let rendered=navigator.render(normalized,null,initial);
[
 ["full overview domain",model.domain=={from:0,to:100}],
 ["visible window distinct",model.window=={from:20,to:40}],
 ["full source markers",len(model.events)==3],
 ["window fractions",abs(model.from-0.2)<0.00001 and abs(model.to-0.4)<0.00001],
 ["start handle",from.window=={from:10m,to:40}],
 ["end handle",to.window=={from:20,to:90m}],
 ["center preserves span",center.window=={from:60m,to:80m}],
 ["crossing start clamped",navigator.window_action(normalized,initial,"from",0.8).window.from==35],
 ["crossing end clamped",navigator.window_action(normalized,initial,"to",0.1).window.to==25],
 ["invalid handle diagnosed",navigator.window_action(normalized,initial,"bad",0.1) is error],
 ["overview drag enabled",start.gesture!=null],
 ["overview drag follows pointer",abs(moved.window.from-40)<0.00001 and abs(moved.window.to-60)<0.00001],
 ["cancel restores window",gesture.update({*:initial,gesture:moved.gesture},{type:"gesture_cancel"},model,normalized).window==initial.window],
 ["RTL projection reversed",rtl.events[0].point[0]>rtl.events[2].point[0]],
 ["overview semantics",name(rendered)=='nav' and rendered["aria-label"]=="Timeline overview"],
 ["sequence overview semantics",navigator.render(timeline.validate({*:spec,view:"story",spacing:"sequence"}),null,initial)["aria-label"]=="Event overview"]
]
