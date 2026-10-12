import t: lambda.chart.timeline
import common: lambda.chart.timeline.common
let raw={id:"controls",view:"axis",scales:[{kind:"numeric",unit:"year"}],window:{from:0,to:100},events:[{id:"a",at:20}]}
let current=t.initial_state(raw)
let zoomed=t.update(current,{type:"zoom_in"},{spec:raw})
let restored=t.update(zoomed.state,{type:"reset"},{spec:raw})
let changed=t.update(current,{type:"set_view",view:"story"},{spec:raw})
let controlled={*:raw,value:{window:{from:0,to:100}}};
[
 ["zoom controls",zoomed.state.window.from==10 and zoomed.state.window.to==90],
 ["zoom out inverse",t.update(zoomed.state,{type:"zoom_out"},{spec:raw}).state.window==current.window],
 ["reset initial window",restored.state.window==current.window],
 ["controlled zoom requests",t.update(t.initial_state(controlled),{type:"zoom_in"},{spec:controlled}).requests[0].field=="window"],
 ["view retains activation",changed.state.view=="story" and changed.state.active_id=="a"],
 ["invalid view spacing diagnosed",t.update(t.initial_state({events:[]}),{type:"set_view",view:"axis"},{spec:{events:[]}}) is error],
 ["message interpolation",common.message({messages:{matches:"{count} résultats"}},"matches","",{count:3})=="3 résultats"],
 ["message callback",common.message({messages:{matches:(parameters)=>string(parameters.count) ++ " matches"}},"matches","",{count:2})=="2 matches"],
 ["custom toolbar parts",contains(format(t.render({*:raw,toolbar:{buttons:["first"],labels:{first:"Start"}},parts:{toolbar:{class:"custom-toolbar"}}}),'html'),"custom-toolbar")]
]
