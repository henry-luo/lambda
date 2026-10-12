import t: lambda.chart.timeline
import editing: lambda.chart.timeline.edit
import dates: lambda.chart.timeline.date
import coords: lambda.chart.timeline.coordinate
let raw={id:"editor",view:"axis",width:600,height:350,scales:[{kind:"numeric",unit:"year"}],
    window:{from:0,to:100},source:{revision:"r1"},edit:{enabled:true,snap_step:5,confirm:true},
    groups:[{id:"g",subgroups:[{id:"s"}]}],events:[{id:"a",from:20,to:40,title:"Alpha",group:"g",subgroup:"s"},{id:"b",at:80,editable:false}]}
let spec=t.validate(raw)
let context={normalized_spec:spec,revision:"r1",now:10}
let current=t.initial_state(raw)
let proposed=t.update(current,{type:"edit_request",id:"a",proposed:{title:"Updated"}},context)
let request=proposed.state.draft
let accepted=t.update(proposed.state,{type:"edit_accept",token:request.token},context)
let applied=t.apply_edit(raw,accepted.requests[0])
let removed=t.update(current,{type:"edit_request",id:"a",operation:"remove"},context)
let added=t.update(current,{type:"edit_request",operation:"add",proposed:{id:"c",at:60,title:"Created"}},context)
let grouped=t.update(current,{type:"edit_request",id:"g",operation:"group",proposed:{title:"Renamed group",order:3}},context)
let sub=t.update(current,{type:"edit_request",id:"g",operation:"subgroup",subgroup:"s",proposed:{title:"Renamed subgroup"}},context)
let plan=t.layout(raw,{width:600,height:350},current)
let drag_context={*:context,layout:plan}
let start=t.update(current,{type:"edit_start",id:"a",point:coords.project(plan.path,0.2).point,pointer_id:1},drag_context)
let moved=t.update(start.state,{type:"edit_move",point:coords.project(plan.path,0.4).point,pointer_id:1},drag_context)
let ended=t.update(moved.state,{type:"edit_end",point:coords.project(plan.path,0.4).point,pointer_id:1},drag_context)
let resizing=t.update(current,{type:"edit_start",id:"a",operation:"resize",handle:"end",point:coords.project(plan.path,0.4).point,pointer_id:2},drag_context)
let resized=t.update(resizing.state,{type:"edit_end",point:coords.project(plan.path,0.7).point,pointer_id:2},drag_context)
let calendar=t.validate({edit:{enabled:true,snap_unit:"day"},events:[{id:"d",at:"-0043-03-15"}]})
let stamp=dates.normalize("-0043-03-15T16:00:00Z").value;
fn edit_request(source, action) => t.update(t.initial_state(source),action,{spec:source,revision:source.source.revision})
let created_group=edit_request(raw,{type:"edit_request",operation:"group",change:"add",proposed:{id:"h",title:"New lane"}})
let with_group=t.apply_edit(raw,created_group.requests[0])
let deleted_group=edit_request(with_group,{type:"edit_request",operation:"group",change:"remove",id:"h"})
let created_sub=edit_request(raw,{type:"edit_request",operation:"subgroup",change:"add",id:"g",proposed:{id:"t",title:"New subgroup"}})
let keyboard=t.update(current,{type:"edit_key",id:"a",direction:1},drag_context)
let keyboard_twice=t.update(keyboard.state,{type:"edit_key",id:"a",direction:1},drag_context)
let lane_raw={*:raw,groups:[raw.groups[0],{id:"h",subgroups:[{id:"t"}]}]}
let lane_plan=t.layout(lane_raw,{width:600,height:350})
let lane_context={spec:lane_raw,revision:"r1",layout:lane_plan}
let origin_row=(lane_plan.lanes |: ~.group=="g" and ~.subgroup=="s")[0]
let target_row=(lane_plan.lanes |: ~.group=="h" and ~.subgroup=="t")[0]
let lane_start=t.update(t.initial_state(lane_raw),{type:"edit_start",id:"a",operation:"move_group",point:coords.project(lane_plan.path,0.2,origin_row.offset).point,pointer_id:3},lane_context)
let lane_end=t.update(lane_start.state,{type:"edit_end",point:coords.project(lane_plan.path,0.2,target_row.offset).point,pointer_id:3},lane_context)
fn failed_snap(value, context) error => error("snap rejected")
let bad_snap={*:spec,edit:{*:spec.edit,snap:failed_snap}}
let snap_start=t.update(current,{type:"edit_start",id:"a",point:coords.project(plan.path,0.2).point,pointer_id:4},{*:drag_context,normalized_spec:bad_snap});
[
 ["request preserves original and revision",request.original.title=="Alpha" and request.source_revision=="r1" and request.confirmation==true],
 ["source unchanged before commit",raw.events[0].title=="Alpha" and proposed.state.active_id=="a"],
 ["commit carries candidate",accepted.requests[0].kind=="commit_edit" and accepted.requests[0].candidate.events[0].title=="Updated"],
 ["immutable application",applied.events[0].title=="Updated" and raw.events[0].title=="Alpha" and applied.events[0].from==20],
 ["remove request",len(removed.state.draft.candidate.events)==1 and removed.state.draft.original.id=="a"],
 ["add request",added.state.draft.candidate.events[2].id=="c"],
 ["duplicate add rejected",t.update(current,{type:"edit_request",operation:"add",proposed:{id:"a",at:20}},context) is error],
 ["identity cannot change",t.update(current,{type:"edit_request",id:"a",proposed:{id:"z"}},context) is error],
 ["invalid range rejected",t.update(current,{type:"edit_request",id:"a",proposed:{to:10}},context) is error],
 ["per-item permission",t.update(current,{type:"edit_request",id:"b",proposed:{title:"Blocked"}},context) is error],
 ["unknown target",t.update(current,{type:"edit_request",id:"missing",proposed:{}},context) is error],
 ["group edits",grouped.state.draft.candidate.groups[0].title=="Renamed group"],
 ["subgroup edits",sub.state.draft.candidate.groups[0].subgroups[0].title=="Renamed subgroup"],
 ["group cycles rejected",t.update(current,{type:"edit_request",id:"g",operation:"group",proposed:{parent:"g"}},context) is error],
 ["cancel restores preview",t.update(proposed.state,{type:"edit_cancel"},context).state.draft==null],
 ["stale completion ignored",t.update(proposed.state,{type:"edit_accept",token:request.token},{*:context,revision:"r2"}).requests==[]],
 ["source revision clears pending edits",t.update(proposed.state,{type:"reconcile"},{spec:{*:raw,source:{revision:"r2"}}}).state.draft==null],
 ["forged target rejected",t.apply_edit(raw,{operation:"remove",ids:["missing"],source_revision:"r1"}) is error],
 ["positive and negative snapping",editing.snap(13,spec)==15 and editing.snap(-13,spec)==-15],
 ["BCE calendar snapping",editing.snap(stamp,calendar)==dates.normalize("-0043-03-16").value],
 ["drag preview",moved.state.edit_gesture.request.proposed.from==40 and moved.state.edit_gesture.request.proposed.to==60],
 ["drag emits one final request",len(moved.requests)==0 and len(ended.requests)==1 and ended.state.edit_gesture==null],
 ["resize endpoint",resized.state.draft.proposed.to==70 and resized.state.draft.candidate.events[0].from==20],
 ["drag cancellation",t.update(moved.state,{type:"edit_cancel"},drag_context).state.edit_gesture==null],
 ["pure validation callback",t.update(current,{type:"edit_request",id:"a",proposed:{title:"Rejected"}},{*:context,normalized_spec:{*:spec,edit:{*:spec.edit,validate:(request,context)=>request.proposed.title!="Rejected"}}}) is error],
 ["create group",with_group.groups[1].id=="h" and created_group.state.draft.original==null],
 ["remove empty group",len(deleted_group.state.draft.candidate.groups)==1],
 ["remove referenced group rejected",edit_request(raw,{type:"edit_request",operation:"group",change:"remove",id:"g"}) is error],
 ["create subgroup",created_sub.state.draft.candidate.groups[0].subgroups[1].id=="t"],
 ["subgroup identity preserved",edit_request(raw,{type:"edit_request",operation:"subgroup",id:"g",subgroup:"s",proposed:{id:"s",title:"Same identity"}}).state.draft.original.id=="s"],
 ["subgroup identity cannot change",edit_request(raw,{type:"edit_request",operation:"subgroup",id:"g",subgroup:"s",proposed:{id:"t"}}) is error],
 ["remove referenced subgroup rejected",edit_request(raw,{type:"edit_request",operation:"subgroup",change:"remove",id:"g",subgroup:"s"}) is error],
 ["subgroup edits stay in owner",lane_end.state.draft.candidate.groups[0].subgroups[0].id=="s" and lane_end.state.draft.candidate.groups[1].subgroups[0].id=="t"],
 ["cross-lane drop",lane_end.state.draft.proposed=={group:"h",subgroup:"t"} and lane_end.state.draft.candidate.events[0].from==20],
 ["keyboard previews accumulate",keyboard.state.draft.proposed.from==25 and keyboard_twice.state.draft.proposed.from==30 and keyboard_twice.state.draft.proposed.to==50],
 ["keyboard requests invalidate earlier token",t.update(keyboard_twice.state,{type:"edit_accept",token:keyboard.state.draft.token},drag_context).requests==[]],
 ["keyboard resize",t.update(current,{type:"edit_key",id:"a",operation:"resize",handle:"end",direction:-1},drag_context).state.draft.proposed.to==35],
 ["date permission cannot be bypassed by update",edit_request({*:raw,edit:{enabled:true,move_time:false}},{type:"edit_request",id:"a",proposed:{from:25}}) is error],
 ["lane permission cannot be bypassed by update",edit_request({*:lane_raw,edit:{enabled:true,move_group:false}},{type:"edit_request",id:"a",proposed:{group:"h",subgroup:"t"}}) is error],
 ["item update permission covers movement",edit_request({*:raw,events:[{*:raw.events[0],editable:{update:false}}]},{type:"edit_request",id:"a",operation:"move_time",proposed:{from:25,to:45}}) is error],
 ["item overrides require opt in",edit_request({*:raw,edit:{enabled:true,move_time:false,override_items:true},events:[{*:raw.events[0],editable:{move_time:true}}]},
    {type:"edit_request",id:"a",operation:"move_time",proposed:{from:25,to:45}}).state.draft.proposed.from==25],
 ["application checks permissions again",t.apply_edit({*:raw,edit:{enabled:false}},accepted.requests[0]) is error],
 ["application ignores forged candidate",t.apply_edit(raw,{*:accepted.requests[0],candidate:{events:[]}}).events[0].title=="Updated"],
 ["application requires one stable identity",t.apply_edit(raw,{*:accepted.requests[0],ids:["a","b"]}) is error],
 ["snap failure is an edit error",t.update(snap_start.state,{type:"edit_end",point:coords.project(plan.path,0.4).point,pointer_id:4},{*:drag_context,normalized_spec:bad_snap}) is error],
 ["invalid permission map",t.validate({*:raw,events:[{*:raw.events[0],editable:{resize:"yes"}}]}) is error],
 ["numeric snapping rejects ambiguous units",t.validate({*:raw,edit:{enabled:true,snap_unit:"month"}}) is error],
 ["calendar snapping rejects fractional days",t.validate({edit:{enabled:true,snap_step:0.5},events:[{id:"c",at:"2000"}]}) is error]
]
