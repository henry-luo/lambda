import t: lambda.chart.timeline
let spec={id:"history",view:"axis",interaction:{multiselect:true},edit:{enabled:true},events:[
    {id:"a",at:"2010",title:"Alpha"},{id:"b",at:"2020",title:"Beta"},{id:"c",at:"2025",title:"Gamma",selectable:false}]}
let context={spec:spec,revision:"r1"}
let initial=t.initial_state(spec)
let next=t.update(initial,{type:"next"},context)
let picked=t.update(next.state,{type:"select",ids:["a","b","c"]},context)
let toggled=t.update(picked.state,{type:"toggle",id:"a"},context)
let cleared=t.update(toggled.state,{type:"clear"},context)
let moved=t.update(initial,{type:"pan",delta:86400000},context)
let zoomed=t.update(initial,{type:"zoom",factor:0.5},context)
let controlled={*:spec,value:{active_id:null,selected_ids:[]}}
let request=t.update(t.initial_state(controlled),{type:"activate",id:"b"},{spec:controlled})
let draft=t.update(initial,{type:"edit_request",id:"a",proposed:{at:"2011"}},context)
let accepted=t.update(draft.state,{type:"edit_accept",token:draft.state.draft.token},context)
let stale=t.update(draft.state,{type:"edit_accept",token:draft.state.draft.token},{*:context,revision:"r2"});
let extended_spec={*:spec,interaction:{multiselect:true,contiguous:true},events:[*spec.events,{id:"d",at:"2030",title:"Delta"}]};
let extended_context={spec:extended_spec};
let extended_once=t.update(t.initial_state(extended_spec),{type:"next",extend:true},extended_context);
let extended_twice=t.update(extended_once.state,{type:"last",extend:true},extended_context);
let grouped_spec={*:extended_spec,interaction:{multiselect:true,per_group:true},groups:[{id:"g1"},{id:"g2"}],
    events:[{id:"a",at:"2010",group:"g1"},{id:"b",at:"2020",group:"g1"},{id:"d",at:"2030",group:"g2"}]};
let grouped_context={spec:grouped_spec};
let first_group=t.update(t.initial_state(grouped_spec),{type:"select",ids:["a","b","d"]},grouped_context);
let second_group=t.update(first_group.state,{type:"toggle",id:"d"},grouped_context);
let filtered_spec={*:spec,search:{mode:"filter",fields:["title"]}};
let filtered_context={spec:filtered_spec};
let middle=t.update(t.initial_state(filtered_spec),{type:"activate",id:"b",focus:true},filtered_context).state;
let filtered=t.update(middle,{type:"search",query:"Gamma"},filtered_context);
let empty=t.update(middle,{type:"search",query:"missing"},filtered_context);
let restored=t.update(empty.state,{type:"search",query:""},filtered_context);
let controlled_filtered={*:filtered_spec,value:{active_id:"b"}};
let controlled_filter=t.update(t.initial_state(controlled_filtered),{type:"search",query:"Gamma"},{spec:controlled_filtered});
let controlled_query={*:filtered_spec,value:{query:""}};
let query_request=t.update(t.initial_state(controlled_query),{type:"search",query:"Gamma"},{spec:controlled_query});
let focused_group=t.update({*:first_group.state,focus_id:"a",focus_pending:false},{type:"collapse",group:"g1"},grouped_context);
let input_filter=t.update({*:middle,focus_pending:false},{type:"search",query:"Gamma"},{*:filtered_context,focus_id:null});
let tagged_spec={*:spec,events:[for (item in spec.events) {*:item,tags:[if (item.id=="b") "drop" else "keep"]}]};
let tagged_middle=t.update(t.initial_state(tagged_spec),{type:"activate",id:"b"},{spec:tagged_spec}).state;
let tagged_filter=t.update(tagged_middle,{type:"filter",filters:{tags:["keep"]}},{spec:tagged_spec});
let default_focus=t.initial_state({*:spec,default_value:{active_id:"b"}});
let controlled_focus=t.initial_state({*:spec,value:{active_id:"c"}});
let held_groups={*:grouped_spec,value:{collapsed_groups:[]}};
let held_collapse=t.update({*:t.initial_state(held_groups),focus_pending:false},
    {type:"collapse",group:"g1",focus:true},{spec:held_groups});
let nested_groups={*:grouped_spec,groups:[{id:"parent"},{id:"g1",parent:"parent"},{id:"g2"}]};
let closed_parent=t.update(t.initial_state(nested_groups),{type:"collapse",group:"parent"},{spec:nested_groups});
let closed_child=t.update(closed_parent.state,{type:"collapse",group:"g1",focus:true},{spec:nested_groups});
[
 ["initial active",initial.active_id=="a"],
 ["navigation",next.state.active_id=="b" and next.notifications[0].name=="timeline_activate"],
 ["selection permission",picked.state.selected_ids==["a","b"]],
 ["toggle",toggled.state.selected_ids==["b"]],
 ["clear",cleared.state.selected_ids==[]],
 ["pan preserves span",moved.state.window.from-initial.window.from==86400000 and moved.state.window.to-initial.window.to==86400000],
 ["zoom",zoomed.state.window.to-zoomed.state.window.from==(initial.window.to-initial.window.from)/2],
 ["controlled null remains controlled",request.state.active_id==null and request.requests[0].kind=="state_change" and request.requests[0].value=="b"],
 ["controlled empty selection",t.update(t.initial_state(controlled),{type:"select",id:"a"},{spec:controlled}).state.selected_ids==[]],
 ["search",t.update(initial,{type:"search",query:"Beta"},context).state.query=="Beta"],
 ["match navigation",t.update(t.update(initial,{type:"search",query:"Beta"},context).state,{type:"next_match"},context).state.active_id=="b"],
 ["edit request",draft.requests[0].source_revision=="r1" and draft.state.draft.original.id=="a"],
 ["edit commit",accepted.state.draft==null and accepted.requests[0].kind=="commit_edit"],
 ["stale edit ignored",stale.state.draft==draft.state.draft and len(stale.requests)==0],
 ["unknown ID",t.update(initial,{type:"activate",id:"unknown"},context) is error],
 ["invalid zoom",t.update(initial,{type:"zoom",factor:0},context) is error],
 ["unknown action",t.update(initial,{type:"unknown"},context) is error],
 ["deleted event reconciled",t.update(picked.state,{type:"reconcile"},{spec:{*:spec,events:[spec.events[0]]}}).state.selected_ids==["a"]],
 ["instance isolation",initial.active_id=="a" and initial.selected_ids==[]],
 ["contiguous range",extended_once.state.selected_ids==["a","b"]],
 ["repeated extension keeps anchor and permissions",extended_twice.state.selected_ids==["a","b","d"] and extended_twice.state.selection_anchor=="a"],
 ["per-group selection",first_group.state.selected_ids==["a","b"]],
 ["switching selection group",second_group.state.selected_ids==["d"]],
 ["filter activates visible neighbor immediately",filtered.state.active_id=="c"],
 ["filter activation notifies",(filtered.notifications |> ~.name)==["timeline_search","timeline_activate"]],
 ["filter rehomes event focus",filtered.state.focus_id=="c" and filtered.state.focus_pending],
 ["empty filter has explicit empty active",empty.state.active_id==null and empty.state.visibility_anchor=="b"],
 ["clearing empty filter restores neighbor",restored.state.active_id=="b"],
 ["controlled active remains authoritative",controlled_filter.state.active_id=="b"],
 ["controlled active requests visible neighbor",controlled_filter.requests[0].field=="active_id" and controlled_filter.requests[0].value=="c"],
 ["controlled query remains authoritative",query_request.state.query=="" and query_request.state.active_id=="a" and len(query_request.requests)==1],
 ["collapse focuses header",focused_group.state.focus_group=="g1" and focused_group.state.focus_pending],
 ["collapse preserves child selection",focused_group.state.selected_ids==["a","b"]],
 ["filter keeps search input focus",input_filter.state.active_id=="c" and input_filter.state.focus_pending==false],
 ["nearest visible tie prefers following event",tagged_filter.state.active_id=="c"],
 ["default active owns initial focus",default_focus.focus_id=="b"],
 ["controlled active owns initial focus",controlled_focus.focus_id=="c"],
 ["unknown collapse group rejected",t.update(initial,{type:"collapse",group:"missing"},context) is error],
 ["unknown expansion event rejected",t.update(initial,{type:"expand",id:"missing"},context) is error],
 ["missing collapse target rejected",t.update(initial,{type:"collapse"},context) is error],
 ["controlled collapse leaves focus alone",held_collapse.state.focus_pending==false and held_collapse.requests[0].field=="collapsed_groups"],
 ["hidden child focuses visible ancestor",closed_child.state.focus_group=="parent"]
]
