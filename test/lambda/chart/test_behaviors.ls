// Behavior effects share template-owned parameters and one validated reduction boundary (S12.1.3).
import b: lambda.chart.behavior
import p: lambda.chart.parameter
import i: lambda.chart.interaction
import c: lambda.chart.chart
import s: lambda.chart.scale
import coord: lambda.chart.coordinate
import parse: lambda.chart.parse
fn check(label, valid) => if (valid) null else label
fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
let rows=[{id:"a",x:2,y:3,group:"one"},{id:"b",x:7,y:8,group:"two"}]
let encoding={x:{field:"x",dtype:"quantitative"},y:{field:"y",dtype:"quantitative"},key:{field:"id"},color:{field:"group",dtype:"nominal"}}
let spec=b.prepare({data:rows,mark:{kind:"point"},encoding:encoding,
    interaction:{element_highlight:{group_by:["color"]},element_select:true,crosshair:true},
    state:{default:{stroke_width:1},active:{stroke:"red",opacity:0.8},inactive:{opacity:0.2},selected:{fill:"green",opacity:1},unselected:{opacity:0.4}}})
let defs=b.definitions(spec,p.definitions(spec))
let st=p.initial(defs)
let frame={view:"chart",params:defs,behaviors:spec._behaviors,data:rows,encoding:encoding,
    scales:{x:s.linear_scale(0,10,0,100),y:s.linear_scale(0,10,100,0)},width:100.0,height:100.0}
let hover=i.update(st,{type:"focusin",row:rows[0],frame:frame})
let pick=i.update(hover,{type:"keydown",key:"Enter",row:rows[0],frame:frame})
let toggled=i.update(pick,{type:"click",row:rows[1],shiftKey:true,frame:frame})
let ctx={_behaviors:spec._behaviors,_parameter_state:pick,_state_styles:spec.state}
let clear=i.update(pick,{command:"clear",id:"element_select",type:"chart_action",frame:frame})
let reset=i.update(pick,{command:"reset",id:"element_select",type:"chart_action",frame:frame})
let custom=b.prepare({params:[{name:"count",value:0}],data:rows,mark:{kind:"point"},encoding:encoding,
    interaction:{count_clicks:{param:"count",on:"click",handler:(event,data,values,coordinate)=>
        {updates:{count:values.count+len(data)},notifications:[{name:"counted",detail:values.count+1}]}}}})
let customdefs=p.definitions(custom)
let customframe={*:frame,params:customdefs,behaviors:custom._behaviors}
let counted=i.update(p.initial(customdefs),{type:"click",row:rows[0],frame:customframe})
let linked=b.prepare({data:rows,params:[{name:"picked",select:{type:"point",fields:["group"]}}],concat:"horizontal",children:[
    {id:"overview",mark:{kind:"point"},encoding:encoding,interaction:{legend_filter:{param:"picked",targets:["detail"],rescale:false}}},
    {id:"detail",mark:{kind:"bar"},encoding:encoding}]})
let linkeddefs=p.definitions(linked)
let linkedst=p.update_value(p.initial(linkeddefs),linkeddefs,"picked",[{group:"one"}])
let detail={*:linked.children[1],_view_path:"chart-c1",_all_behaviors:linked._all_behaviors,_parameter_state:linkedst}
let native=b.prepare(<chart <data values:rows>; <mark kind:"point">; <encoding <x field:"x",dtype:"quantitative">; <y field:"y",dtype:"quantitative">>;
    <interaction <element_select>>>)
let disabled=b.prepare({interaction:{element_select:true,tooltip:true},state:{active:{opacity:0.5}},layer:[
    {mark:{kind:"point",interaction:{element_select:false,tooltip:false}}},{mark:{kind:"point"}}]})
let polar=coord.configure({type:"polar"},100,100)
let brushdefs=[{name:"brush",select:{type:"interval",encodings:["x"]}}]
let polarframe={*:frame,params:brushdefs,behaviors:[],coordinate:polar}
let a=coord.project(polar,[0.95,0.7])
let z=coord.project(polar,[0.05,0.7])
let down=i.update(p.initial(brushdefs),{type:"pointerdown",button:0,x:a[0],y:a[1],frame:polarframe})
let brushed=i.update(down,{type:"pointerup",x:z[0],y:z[1],frame:polarframe});
[check("three presets compile",len(spec._behaviors)==3 and len(defs)==3),
 check("empty states unset",not b.flags({_behaviors:spec._behaviors,_parameter_state:st},rows[0]).active),
 check("focus highlight",b.flags(ctx,rows[0]).active and b.flags(ctx,rows[1]).inactive),
 check("group projection",p.selected(hover.values.chart_root_element_highlight,{group:"one"},false)),
 check("keyboard selection",p.selected(pick.values.chart_root_element_select,rows[0],false)),
 check("toggle selection",p.selected(toggled.values.chart_root_element_select,rows[0],false) and p.selected(toggled.values.chart_root_element_select,rows[1],false)),
 check("state precedence",b.styles(ctx,rows[0],{}).opacity==1 and b.styles(ctx,rows[0],{}).fill=="green" and b.styles(ctx,rows[0],{}).stroke=="red"),
 check("snake case styles",b.styles(ctx,rows[0],{})["stroke-width"]==1),
 check("inactive and unselected",b.styles(ctx,rows[1],{}).opacity==0.4),
 check("clear",p.empty(clear.values.chart_root_element_select)),
 check("reset",p.empty(reset.values.chart_root_element_select)),
 check("clear notification",clear._notifications[0].phase=="clear"),
 check("unchanged has no notice",len(i.update(pick,{type:"keydown",key:"a",row:rows[0],frame:frame})._notifications)==0),
 check("unknown command",i.update(st,{command:"bogus",id:"element_select",frame:frame}) is error),
 check("bad point command",i.update(st,{command:"set",id:"element_select",value:3,frame:frame}) is error),
 check("unknown parameter",p.update_value(st,defs,"missing",3) is error),
 check("computed read only",p.update_value(p.initial([{name:"read_only",expr:"2"}]),[{name:"read_only",expr:"2"}],"read_only",3) is error),
 check("custom update",counted.values.count==2), check("custom notice",counted._custom_notifications[0].name=="counted"),
 check("filter target",b.filter_data(detail,rows)==[rows[0]]),check("retain domains",b.retain_domains(detail)),
 check("clear restores source",len(b.filter_data({*:detail,_parameter_state:p.update_value(linkedst,linkeddefs,"picked",null,"chart","clear")},rows))==2),
 check("native interaction",native._behaviors[0].type=="element_select"),
 check("explicit false",len(disabled.layer[0]._behaviors)==0 and disabled.layer[0]._tooltip_disabled),
 check("sibling inherits",len(disabled.layer[1]._behaviors)==2),
 check("inherited states",disabled.layer[1].state.active.opacity==0.5),
 check("unknown behavior",b.prepare({mark:{kind:"point"},interaction:{invalid:true}}) is error),
 check("unknown param",b.prepare({mark:{kind:"point"},interaction:{element_select:{param:"missing"}}}) is error),
 check("wrong param type",b.prepare({params:[{name:"bad",value:1}],mark:{kind:"point"},interaction:{element_select:{param:"bad"}}}) is error),
 check("invalid stream",b.prepare({mark:{kind:"point"},interaction:{element_select:{on:"timer"}}}) is error),
 check("render behavioral chart",c.render_spec(spec,null,pick) is element),
 check("focusable marks",len(elements(c.render_spec(spec,null,pick)) |: ~["data-chart-key"]!=null and ~.tabindex=="0")==2),
 check("tooltip off",len(elements(c.render_spec({data:rows,mark:{kind:"point"},encoding:{*:encoding,tooltip:{field:"id"}},interaction:{tooltip:false}})) |: name(~)=='title')==0),
 check("polar seam extent",brushed.values.brush.stores.chart.x.kind=="wrapped"),
 check("polar seam predicate",p.selected(brushed.values.brush,{x:9.8},false) and p.selected(brushed.values.brush,{x:0.2},false) and not p.selected(brushed.values.brush,{x:5},false))] |: ~!=null
