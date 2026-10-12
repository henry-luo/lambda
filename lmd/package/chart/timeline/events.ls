// DOM access stays procedural; input policy and changes use the pure reducer.
import dom
import c: .common
import reducer: .state
import layout: .layout
import util: lambda.chart.util
import events: lambda.chart.events
import resource: .resource
import present: .present
import coordinate: .coordinate
import time_map: .time_map
import navigator: .navigator
import editing: .edit
import media_playback: .media_playback
import native_media: .native_media
import filter_controls: .filter_controls
import prepared: .prepared
import motion: .motion
import vector: .svg
import event_index: .index

fn wheel_allowed(options, evt) {
    let modifier=c.text(c.option(options,"wheel_modifier","none"));
    options.wheel!=false and (modifier=="none" or modifier=="ctrl" and evt.ctrlKey==true or modifier=="shift" and evt.shiftKey==true
        or modifier=="alt" and evt.altKey==true or modifier=="meta" and evt.metaKey==true)
}
pn restore_control(spec, current, evt) {
    if (evt.type=="change" and events.attribute(evt.target,"data-timeline-filter")!=null) {
        filter_controls.restore(evt.target,spec,current)
    }
    if (evt.type=="input" and c.has(spec.value,"query") and events.attribute(evt.target,"data-timeline-search")!=null and
        dom.get_state(evt.target,"value")!=current.query) { dom.set_state(evt.target,"value",current.query) }
}
pn canvas_point(host, model, spec, current, evt) {
    let overview=events.closest(evt.target,"[data-timeline-navigator]");
    let canvas=if (overview!=null) overview else dom.query_selector(host,".lambda-timeline-canvas");
    let bounds=if (canvas!=null) dom.bounding_box(canvas) else null;
    let x=if (evt.clientX!=null) evt.clientX else evt.x;
    let y=if (evt.clientY!=null) evt.clientY else evt.y;
    let plan=if (bounds==null) null else if (overview!=null) navigator.plan(spec,model.viewport,current)
        else if (model.displayed!=null) model.displayed.plan else layout.normalized(spec,model.viewport,current);
    if (plan is error) plan else if (bounds==null or bounds.width<=0 or bounds.height<=0 or not util.finite_number(x) or not util.finite_number(y)) null
    else {plan:plan,surface:if (overview!=null) "navigator" else "main",point:[(x-bounds.left)*plan.width/bounds.width,(y-bounds.top)*plan.height/bounds.height]}
}

pub fn source(model, clock = null) => if (clock.source!=null) clock.source else if (model.spec!=null) model.spec else model
pub fn presentation(model, clock) {
    let cache=prepared.source(source(model,clock),clock.interaction,c.option(clock,"prepared",model.prepared));
    let loaded=cache.normalized;
    let normalized=cache.fallback;
    let current=if (normalized is error) normalized else reducer.reconcile(normalized,
        if (clock.interaction==null) reducer.initial(normalized) else clock.interaction);
    let visual={*:editing.preview(normalized,current),view:current.view};
    let sequence=visual.view=="cards" and visual.spacing=="sequence";
    let displayed=if (sequence and clock.geometry_cache.key!=prepared.geometry_key(visual,model.viewport,current)) null else clock.motion.displayed;
    if (current is error) current else if (model.legacy_list==true)
        present.cards({*:visual,live_host:true},current,displayed)
    else present.html(visual,model.viewport,
        {*:current,presentation_error:if (loaded is error) loaded else null},displayed)
}

// Sequence cards use their native flow boxes for FLIP; geometry views already have measured boxes.
pn sequence_geometry(geometry, host, displayed = null) {
    let root=dom.query_selector(host,"ol");
    let origin=if (root!=null) dom.bounding_box(root) else null;
    let previous=sort([for (item in displayed.plan.events) {id:item.id,item:item}],{by:(row)=>row.id});
    let boxes=sort([for (node in dom.query_selector_all(root,"li[data-timeline-id]"),
        let id=events.attribute(node,"data-timeline-id"),let box=dom.bounding_box(node),
        let old=event_index.lookup(previous,id).item where events.closest(node,"[data-timeline-host]")==host)
        {id:id,bounds:{left:box.left-(if (old.target_bounds!=null) old.bounds.left-old.target_bounds.left else 0),
            top:box.top-(if (old.target_bounds!=null) old.bounds.top-old.target_bounds.top else 0),width:box.width,height:box.height}}],{by:(row)=>row.id});
    let plan=geometry.plan;
    {*:geometry,plan:{*:plan,sequence_origin:origin,events:[for (item in plan.events,
        let box=event_index.lookup(boxes,item.id).bounds,
        let bounds=if (box==null) item.bounds else {left:box.left,top:box.top,right:box.left+box.width,bottom:box.top+box.height})
        {*:item,bounds:bounds,target_bounds:bounds}]}}
}

pn focus_pending(current, host) {
    if (current.focus_pending!=true) { return current }
    let candidates=dom.query_selector_all(host,if (current.focus_group!=null) "[data-timeline-group]" else "[data-timeline-id]");
    let target=if (current.focus_group!=null)
        [for (node in candidates where events.attribute(node,"data-timeline-group")==current.focus_group) node][0]
        else if (current.focus_id==null) host else [for (node in candidates where events.attribute(node,"data-timeline-id")==current.focus_id and
            contains(["li","article"],lower(dom.node_name(node)))) node][0];
    if (target!=null) { dom.focus_set(target,true); dom.scroll_into_view(target) }
    {*:current,focus_pending:false}
}

// the host owns frame tokens; removed instances invalidate queued delivery (D7.5.3).
pub pn handle(model, clock, evt) {
    let tick=evt.type=="timeline_frame";
    if (evt.type=="timeline_action" and evt.detail.type=="source_update") {
        let old=prepared.source(source(model,clock),clock.interaction,c.option(clock,"prepared",model.prepared)).normalized;
        let host=events.closest(evt.target,"[data-timeline-host]");
        let focused=if (host==null) null else dom.query_selector(host,":focus");
        let changed=reducer.update(clock.interaction,evt.detail,{normalized_spec:old,now:evt.time_stamp,
            focus_id:events.attribute(events.closest(focused,"[data-timeline-id]"),"data-timeline-id")});
        if (changed is error) { return {*:clock,effects:[{name:"timeline_error",detail:{error:changed}}]} }
        if (not c.has(changed,"source")) { return {*:clock,effects:[]} }
        if (clock.token>0) { dom.cancel_frame(evt.target,clock.token) }
        return {*:clock,source:changed.source,interaction:changed.state,token:0,disposition:'pass',
            prepared:prepared.record(changed.source,changed.state,changed.normalized_spec,clock.prepared),
            native_media_requests:[*c.items(clock.native_media_requests),*native_media.queued(changed.requests)],
            effects:[*changed.notifications,*[for (request in changed.requests) {name:"timeline_request",detail:request}]]}
    }
    if (tick and (clock.token==0 or evt.detail!=clock.token)) { return {*:clock,effects:[]} }
    if (evt.type=="render" and (evt.event_phase!=2 or clock.token>0 and clock.interaction.focus_pending!=true and
        len(c.items(clock.native_media_requests))==0)) { return {*:clock,effects:[]} }
    let cache=prepared.source(source(model,clock),clock.interaction,c.option(clock,"prepared",model.prepared));
    let spec=cache.normalized;
    if (spec is error) { return {*:clock,effects:[{name:"timeline_error",detail:{error:spec}}]} }
    let host=events.closest(evt.target,"[data-timeline-host]");
    if (host!=null) {
        dom.watch_window(host,"hashchange",spec.story.bookmarks==true);
        dom.watch_window(host,"popstate",spec.story.bookmarks==true)
    }
    let initial=reducer.reconcile(spec,if (clock.interaction==null) reducer.initial(spec) else clock.interaction);
    let fragment=if (spec.story.bookmarks==true) dom.owner_document(evt.target).hash else null;
    let addressed=if (fragment!=null and fragment!=clock.bookmark_fragment)
        reducer.update(initial,{type:"bookmark",fragment:fragment,origin:"bookmark",focus:spec.interaction.focus_on_load==true},
            {normalized_spec:spec,revision:spec.source.revision,now:evt.time_stamp}) else reducer.result(initial);
    let restored=if (addressed is error) initial else addressed.state;
    let owned=if (host==null) {bindings:{},actions:[]}
        else native_media.step(host,clock,restored,evt.type=="render",model.options.media!=false);
    let completed=native_media.reduce(restored,owned.actions,spec,evt.time_stamp);
    let ready=if (completed is error) restored else completed.state;
    let current=if (evt.type=="render") focus_pending(ready,host) else ready;
    let advancing=(tick or evt.type=="render") and current.playback.phase=="playing";
    let retained={*:c.attrs(model),spec:source(model,clock),displayed:clock.motion.displayed};
    let reduced=if (advancing) dispatch(retained,current,{*:evt,type:"timeline_action",detail:{type:"tick"}},spec)
        else if (evt.type=="render" or tick) reducer.result(current) else dispatch(retained,current,evt,spec);
    let accepted_spec=c.option(reduced,"normalized_spec",spec);
    let players=media_playback.synchronize(reduced.state,accepted_spec,evt.time_stamp);
    let settled=resource.lifecycle(accepted_spec,players.state,evt.time_stamp);
    var next=settled.state;
    var effects=[*(if (addressed is error) [{name:"timeline_error",detail:{chart_id:spec.id,error:addressed}}] else addressed.notifications),
        *(if (completed is error) [{name:"timeline_error",detail:{chart_id:spec.id,error:completed}}] else completed.notifications),
        *reduced.notifications,*settled.notifications,*[for (request in [*(if (completed is error) [] else completed.requests),
            *(if (addressed is error) [] else addressed.requests),
            *reduced.requests,*players.requests,*settled.requests]) {name:"timeline_request",detail:request}]];
    let requests=resource.pending(accepted_spec,next,evt.time_stamp);
    if (requests is error) { effects=[*effects,{name:"timeline_error",detail:{chart_id:spec.id,error:requests}}] }
    for (descriptor in (if (requests is error) [] else requests)) {
        let started=resource.begin(next,descriptor,accepted_spec.source.revision,evt.time_stamp);
        next=started.state;
        effects=[*effects,{name:"timeline_request",detail:started.request}]
    }
    let next_cache=prepared.record(source(model,clock),next,accepted_spec,cache);
    let visual={*:editing.preview(next_cache.normalized,next),view:next.view};
    let sequence=visual.view=="cards" and visual.spacing=="sequence";
    let measured=if (spec.animate.enabled==true) prepared.geometry(visual,model.viewport,next,clock.geometry_cache) else null;
    let changed=measured.key!=clock.geometry_cache.key;
    let deferred=sequence and changed and evt.type!="render";
    let geometry=if (deferred) clock.geometry_cache else if (sequence and changed and host!=null)
        sequence_geometry(measured,host,if (clock.geometry_cache.key==measured.key) clock.motion.displayed else null) else measured;
    let target=if (geometry!=null and not (geometry.plan is error)) {plan:geometry.plan,
        svg:if (sequence) null else vector.render(geometry.plan,next,{unsupported:"omit",cards:next.view=="story"})} else null;
    let animated=if (deferred) clock.motion else if (target!=null) motion.advance(clock.motion,target,evt.time_stamp,visual) else null;
    let pending=if (tick) 0 else clock.token;
    let scheduled=next.playback.phase=="playing" or resource.needs_clock(accepted_spec,next) or native_media.needs_clock(owned.bindings) or animated.running==true;
    if (not scheduled and pending>0) { dom.cancel_frame(evt.target,pending) }
    let token=if (not scheduled) 0 else if (pending>0) pending else dom.request_frame(evt.target,"timeline_frame");
    {*:clock,interaction:next,token:token,effects:effects,bookmark_fragment:fragment,disposition:reduced.disposition,
        prepared:next_cache,geometry_cache:geometry,motion:animated,
        native_players:owned.bindings,native_media_requests:if (model.options.media==false) [] else
            [*(if (evt.type=="render") [] else c.items(clock.native_media_requests)),
                *native_media.queued([for (effect in effects where effect.name=="timeline_request") effect.detail])]}
}

// the template assigns this result before callbacks may synchronously reenter it.
pub pn deliver(clock, evt = null) {
    for (effect in c.items(clock.effects)) {
        if (effect.name=="timeline_bookmark" and evt!=null) {
            let host=events.closest(evt.target,"[data-timeline-host]");
            if (host!=null and dom.owner_document(host).hash!=effect.detail.fragment) {
                dom.request_navigation({source:host,url:effect.detail.fragment,target:dom.document_element(host),target_kind:"existing"})
            }
        }
        emit(effect.name,effect.detail)
    }
    if (clock.disposition!=null) clock.disposition else 'pass'
}

pub pn dispatch(model, previous, evt, normalized_spec = null) {
    let owner=events.closest(evt.currentTarget,"[data-timeline-host]");
    let host=events.closest(evt.target,"[data-timeline-host]");
    if (host==null or owner!=null and owner!=host) { return reducer.result(previous) }
    let spec=if (normalized_spec!=null) normalized_spec else resource.normalize(source(model),previous);
    if (spec is error) { return {state:previous,requests:[],notifications:[{name:"timeline_error",detail:{error:spec}}]} }
    let reconciled=reducer.reconcile(spec,if (previous==null) reducer.initial(spec) else previous);
    // a fresh press starts an intentional click; only the drag's compatibility click is suppressed.
    let current=if (evt.type=="pointerdown") {*:reconciled,suppress_click_until:null} else reconciled;
    let control=events.closest(evt.target,"[data-timeline-action]");
    let double_control=events.closest(evt.target,"[data-timeline-double-action]");
    let edit_handle=events.closest(evt.target,"[data-timeline-edit-handle]");
    let item=events.closest(evt.target,"[data-timeline-id]");
    let key=events.attribute(item,"data-timeline-id");
    let pointer=contains(["pointerdown","pointermove","pointerup","pointercancel","lostpointercapture"],evt.type);
    let input=if (evt.type=="keydown" and edit_handle!=null) {plan:layout.normalized(spec,model.viewport,current)}
        else if (pointer or evt.type=="wheel" or evt.type=="click") canvas_point(host,model,spec,current,evt) else null;
    let editable=events.closest(evt.target,"input,textarea,select,button,a,[contenteditable]")!=null;
    if (evt.type=="click" and current.suppress_click_until!=null and evt.time_stamp<=current.suppress_click_until) { return reducer.result(current) }
    if (spec.interaction.click_to_use==true and current.engaged!=true and evt.type!="timeline_action") {
        if (evt.type=="click") { return reducer.result({*:current,engaged:true}) }
        return reducer.result(current)
    }
    let action=if (evt.type=="timeline_action") evt.detail
        else if (evt.type=="keydown" and evt.key=="Escape" and (current.edit_gesture!=null or current.draft!=null)) {type:"edit_cancel"}
        else if (evt.type=="keydown" and evt.key=="Enter" and edit_handle!=null and current.draft!=null) {type:"edit_accept",token:current.draft.token}
        else if (evt.type=="keydown" and edit_handle!=null and spec.interaction.keyboard!=false and contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"],evt.key))
            {type:"edit_key",id:key,operation:if (events.attribute(edit_handle,"data-timeline-edit-handle")=="move") "move_time"
                else if (events.attribute(edit_handle,"data-timeline-edit-handle")=="group") "move_group" else "resize",
                handle:events.attribute(edit_handle,"data-timeline-edit-handle"),direction:(if (contains(["ArrowRight","ArrowDown"],evt.key)) 1 else -1)*
                    (if (spec.direction=="rtl" and contains(["ArrowLeft","ArrowRight"],evt.key)) -1 else 1)}
        else if (pointer and input!=null and (edit_handle!=null or current.edit_gesture!=null)) {
            type:if (evt.type=="pointerdown") "edit_start" else if (evt.type=="pointermove") "edit_move" else if (evt.type=="pointerup") "edit_end" else "edit_cancel",
            id:key,operation:if (events.attribute(edit_handle,"data-timeline-edit-handle")=="move") "move_time"
                else if (events.attribute(edit_handle,"data-timeline-edit-handle")=="group") "move_group" else "resize",
            handle:events.attribute(edit_handle,"data-timeline-edit-handle"),point:input.point,pointer_id:evt.pointerId}
        else if (pointer and input!=null and (not editable or current.gesture!=null) and (evt.button==null or evt.button==0 or evt.type!="pointerdown"))
            {type:if (evt.type=="pointerdown") "gesture_start" else if (evt.type=="pointermove") "gesture_move"
                else if (evt.type=="pointerup") "gesture_end" else "gesture_cancel",point:input.point,pointer_id:evt.pointerId,surface:input.surface}
        else if (evt.type=="click" and control!=null) parse(events.attribute(control,"data-timeline-action"),'json') ^ { ^ }
        else if (evt.type=="dblclick" and double_control!=null) parse(events.attribute(double_control,"data-timeline-double-action"),'json') ^ { ^ }
        else if (evt.type=="click" and item!=null) {type:if (evt.shiftKey==true) "select" else if (evt.ctrlKey==true or evt.metaKey==true) "toggle" else "activate",id:key,extend:evt.shiftKey==true}
        else if (evt.type=="click" and input.surface=="navigator") navigator.window_action(spec,current,"center",coordinate.invert(input.plan.path,input.point).fraction)
        else if (contains(["input","change"],evt.type) and events.attribute(evt.target,"data-timeline-setting")!=null) {
            let setting=events.attribute(evt.target,"data-timeline-setting");
            let value=dom.get_state(evt.target,"value");
            if (setting=="quick_jump") {type:"activate",id:value,focus:true}
            else if (setting=="layout") {type:"set_view",view:value}
            else if (setting=="theme") {type:"set_theme",theme:value} else null
        }
        else if (evt.type=="input" and events.attribute(evt.target,"data-timeline-window-handle")!=null)
            navigator.window_action(spec,current,events.attribute(evt.target,"data-timeline-window-handle"),float(dom.get_state(evt.target,"value"))/1000.0)
        else if (evt.type=="change" and events.attribute(evt.target,"data-timeline-filter")!=null) filter_controls.action(evt.target,spec,current)
        else if (evt.type=="input" and events.attribute(evt.target,"data-timeline-search")!=null) {type:"search",query:dom.get_state(evt.target,"value")}
        else if (evt.type=="keydown" and spec.interaction.keyboard!=false and not editable) {
            if (evt.key=="Home") {type:"first",focus:true,extend:evt.shiftKey==true} else if (evt.key=="End") {type:"last",focus:true,extend:evt.shiftKey==true}
            else if (contains(["ArrowRight","ArrowDown"],evt.key)) {type:if (evt.key=="ArrowRight" and spec.direction=="rtl") "previous" else "next",extend:evt.shiftKey==true,focus:true}
            else if (contains(["ArrowLeft","ArrowUp"],evt.key)) {type:if (evt.key=="ArrowLeft" and spec.direction=="rtl") "next" else "previous",extend:evt.shiftKey==true,focus:true}
            else if (evt.key==" " and key!=null) {type:"toggle",id:key}
            else if (evt.key=="Enter" and key!=null) {type:"activate",id:key} else null
        } else if (evt.type=="wheel" and spec.interaction.zoom==true and wheel_allowed(spec.interaction,evt)) {
            let delta=if (evt.deltaY!=null) evt.deltaY else 0;
            {type:"zoom",factor:math.exp(util.clamp_val(delta,-500,500)*0.002),
                anchor:if (input!=null) time_map.invert(input.plan.time_map,coordinate.invert(input.plan.path,input.point).fraction) else null}
        } else null;
    if (action==null or spec.interaction.enabled==false and evt.type!="timeline_action") { return reducer.result(current) }
    let focused=dom.query_selector(host,":focus");
    let focused_item=if (events.closest(focused,"[data-timeline-host]")==host) events.closest(focused,"[data-timeline-id]") else null;
    let next=if (action is error) action else reducer.update(current,{*:action,origin:"native"},
        {normalized_spec:spec,source_spec:source(model),revision:spec.source.revision,now:evt.time_stamp,layout:input.plan,viewport:model.viewport,
            focus_id:events.attribute(focused_item,"data-timeline-id")});
    if (next is error) {
        restore_control(spec,current,evt)
        return {state:current,requests:[],notifications:[{name:"timeline_error",detail:{chart_id:spec.id,error:next}}]}
    }
    restore_control(spec,next.state,evt)
    if (evt.type=="pointerdown" and (next.state.gesture!=null or next.state.edit_gesture!=null) and evt.pointerId!=null) { host.set_pointer_capture(evt.pointerId) }
    {*:next,disposition:if (evt.type=="keydown" and contains(["edit_key","edit_cancel","edit_accept"],action.type)) 'prevent-default' else 'pass'}
}
