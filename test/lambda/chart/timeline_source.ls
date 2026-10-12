import t: lambda.chart.timeline

let raw={id:"live-source",view:"story",spacing:"sequence",source:{revision:"r1"},interaction:{multiselect:true},
    slideshow:{enabled:true,duration:1000},default_value:{active_id:"b",selected_ids:["a","b"]},
    events:[{id:"a",title:"Alpha"},{id:"b",title:"Beta"},{id:"c",title:"Gamma"}]};
let initial=t.initial_state(raw);
let replacement={*:raw,source:{revision:"r2"},events:[raw.events[0],raw.events[2]]};
let changed=t.update({*:initial,focus_pending:false},{type:"source_update",spec:replacement},
    {spec:raw,now:0});
let external=t.update({*:initial,focus_pending:false},{type:"source_update",spec:replacement},{spec:raw,now:0,focus_id:null});
let empty_spec={*:raw,source:{revision:"r2"},events:[],default_value:{}};
let empty=t.update(initial,{type:"source_update",spec:empty_spec},{spec:raw,now:0});
let recovered=t.update(empty.state,{type:"source_update",spec:{*:replacement,source:{revision:"r3"}}},{spec:empty_spec,now:0});
let played=t.update(initial,{type:"play"},{spec:raw,now:0});
let sought=t.update(played.state,{type:"seek",time_ms:1500},{spec:raw,now:0});
let paused=t.update(sought.state,{type:"pause"},{spec:raw,now:0});
let reordered={*:raw,source:{revision:"r2"},slideshow:{duration:2000},events:[raw.events[2],raw.events[0],raw.events[1]]};
let retained_playhead=t.update(paused.state,{type:"source_update",spec:reordered},{spec:raw,now:5000});
let removed_playhead=t.update(paused.state,{type:"source_update",spec:{*:replacement,slideshow:{duration:2000}}},{spec:raw,now:5000});
let controlled={*:(map([for (key,value in replacement where string(key)!="default_value") (key,value)])),value:{active_id:"a",selected_ids:[]}};
let held=t.update(initial,{type:"source_update",spec:controlled},{spec:raw,now:0});
let axis_raw={id:"axis-source",view:"axis",scales:[{kind:"numeric",unit:"year"}],
    window:{from:0,to:40},events:[{id:"a",at:10},{id:"b",at:20}],default_value:{active_id:"b"}};
let shifted=t.update(t.initial_state(axis_raw),{type:"source_update",spec:{*:axis_raw,
    events:[axis_raw.events[0],{id:"b",at:300}]}},{spec:axis_raw,now:0});
let loading_raw={*:raw,source:{url:"first.json",revision:"r1"}};
let loading=t.update(t.initial_state(loading_raw),{type:"resource_request",id:"source"},{spec:loading_raw,revision:"r1",now:0});
let retired=t.update(loading.state,{type:"source_update",spec:replacement},{spec:loading_raw,now:50});
let stale=t.update(initial,{type:"source_update",spec:{bad:true},expected_revision:"old"},{spec:raw});
let reversed_source=t.update(paused.state,{type:"source_update",spec:{*:reordered,slideshow:{duration:2000,direction:"reverse"}}},{spec:raw,now:5000});
let playing_source=t.update(sought.state,{type:"source_update",spec:reordered},{spec:raw,now:250});
let repeating={*:raw,slideshow:{duration:1000,repeat:"infinite",direction:"alternate"}};
let repeat_play=t.update(t.initial_state(repeating),{type:"play"},{spec:repeating,now:0});
let repeat_seek=t.update(repeat_play.state,{type:"seek",time_ms:1500},{spec:repeating,now:0});
let repeat_pause=t.update(repeat_seek.state,{type:"pause"},{spec:repeating,now:3000});
let repeat_source=t.update(repeat_pause.state,{type:"source_update",spec:{*:reordered,slideshow:{duration:2000,repeat:"infinite",direction:"alternate"}}},{spec:repeating,now:3000});
let empty_autoplay={*:raw,default_value:{},events:[],slideshow:{autoplay:true,duration:1000}};
let loaded_autoplay=t.update(t.initial_state(empty_autoplay),{type:"source_update",spec:{*:raw,slideshow:{autoplay:true,duration:1000}}},{spec:empty_autoplay,now:50});
let editable={*:raw,edit:{enabled:true,update:true}};
let draft=t.update(initial,{type:"edit_request",id:"a",proposed:{title:"Preview"}},{spec:editable,revision:"r1"});
let authored_change={*:editable,events:[{*:raw.events[0],title:"External update"},raw.events[1],raw.events[2]]};
let retired_draft=t.update(draft.state,{type:"source_update",spec:authored_change},{spec:editable,now:0});
let preserved_draft=t.update(draft.state,{type:"source_update",spec:editable},{spec:editable,now:0});
[
 ["removed active chooses nearest old chronology",changed.state.active_id=="c"],
 ["selection prunes only removed IDs",changed.state.selected_ids==["a"]],
 ["activation notification has original ID",(changed.notifications |: ~.name=="timeline_activate")[0].detail.old=="b"],
 ["selection notification has original IDs",(changed.notifications |: ~.name=="timeline_select")[0].detail.old==["a","b"]],
 ["removed event focus follows activation",changed.state.focus_id=="c" and changed.state.focus_pending],
 ["external control focus is preserved",external.state.focus_pending==false],
 ["source revision commits",changed.state.revision=="r2" and changed.source==replacement],
 ["source notification is last",changed.notifications[len(changed.notifications)-1].detail.field=="source"],
 ["empty replacement clears activation",empty.state.active_id==null and empty.state.selected_ids==[]],
 ["empty source recovers activation",recovered.state.active_id=="a"],
 ["paused slide keeps fractional playhead",retained_playhead.state.active_id=="b" and retained_playhead.state.playback.elapsed==5000],
 ["paused phase is retained",retained_playhead.state.playback.phase=="paused" and retained_playhead.state.story_frame.progress==0.5],
 ["removed slide aligns replacement playhead",removed_playhead.state.active_id=="c" and removed_playhead.state.playback.elapsed==2000],
 ["controlled replacement is authoritative",held.state.active_id=="a" and held.state.selected_ids==[]],
 ["controlled activation change notifies",(held.notifications |: ~.name=="timeline_activate")[0].detail.value=="a"],
 ["moving retained event scrolls into view",shifted.state.window.from<=300 and shifted.state.window.to>=300],
 ["auto-scroll preserves window span",shifted.state.window.to-shifted.state.window.from==40],
 ["resource retirement requests cancellation",(retired.requests |: ~.kind=="cancel_resource")[0].token==loading.requests[0].token],
 ["stale source update is ignored before validation",stale.state==initial and len(stale.notifications)==0],
 ["chart identity change rejected",t.update(initial,{type:"source_update",spec:{*:raw,id:"other"}},{spec:raw}) is error],
 ["changed direction keeps retained slide position",reversed_source.state.active_id=="b" and reversed_source.state.story_frame.active_id=="b" and reversed_source.state.playback.elapsed==1000],
 ["playing rebase includes elapsed host time",playing_source.state.playback.elapsed==5500 and playing_source.state.story_frame.progress==0.75 and playing_source.state.playback.anchor==250],
 ["paused rebase has no advancing anchor",retained_playhead.state.playback.anchor==null],
 ["alternate repeat keeps cycle and position",repeat_source.state.story_frame.cycle==1 and repeat_source.state.story_frame.active_id=="b" and repeat_source.state.playback.elapsed==7000],
 ["empty autoplay source begins without dividing by zero",loaded_autoplay.state.active_id=="a" and loaded_autoplay.state.playback.phase=="playing" and loaded_autoplay.state.story_frame.progress==0],
 ["changed source invalidates draft without revision change",retired_draft.state.draft==null],
 ["late validation cannot commit the retired draft",t.update(retired_draft.state,{type:"edit_accept",token:draft.state.draft.token},{spec:authored_change,revision:"r1"}).requests==[]],
 ["identical source retains draft",preserved_draft.state.draft==draft.state.draft]
]
