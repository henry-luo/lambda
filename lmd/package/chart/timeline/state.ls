// all inputs reduce through immutable instance state; effects are returned requests.
import c: .common
import schema: .schema
import layout: .layout
import dates: .date
import util: lambda.chart.util
import story: .story
import resource: .resource
import gesture: .gesture
import search: .search
import editing: .edit
import media_playback: .media_playback
import bookmark: .bookmark

let fields=["active_id","selected_ids","window","collapsed_groups","expanded_events","query","filters","view","theme"]

fn visible_neighbor(spec, current, visible) {
    let authored=if (spec.story.order is array) spec.story.order else spec.events |> ~.id;
    let order=[if (spec.intro!=null) spec.intro.id,*authored] |: ~!=null;
    let at=index_of(order,current.active_id);
    let candidates=sort([for (index,id in visible,let position=index_of(order,id))
        {id:id,distance:if (position==null) len(order)+index else if (at==null) position else abs(position-at),
            previous:at!=null and position!=null and position<at}],
        {by:(item)=>[item.distance,item.previous]});
    candidates[0].id
}

// A nested collapse returns focus to the first header that remains exposed.
fn visible_group_header(spec, id, visible) {
    if (id==null or contains(visible,id)) id else {
        let group=(spec.groups |: ~.id==id)[0];
        visible_group_header(spec,group.parent,visible)
    }
}

pub fn initial_state(spec) {
    let normalized=schema.validate(spec);
    if (normalized is error) normalized else initial(normalized)
}
pub fn initial(spec) {
    let authored=c.attrs(spec.default_value);
    let shown=story.items(spec,authored);
    let active=if (spec.story.active_id!=null) c.text(spec.story.active_id) else
        shown[if (spec.story.start_at_end) len(shown)-1 else 0].id;
    let current={active_id:active,selected_ids:[],window:layout.window(spec,authored),
        collapsed_groups:[for (group in spec.groups where group.collapsed==true) group.id],expanded_events:[],
        query:c.option(spec.search,"query",""),filters:{},view:spec.view,theme:spec.theme,
        revision:spec.source.revision,sequence:0,draft:null,resources:{},focus_id:active,focus_pending:spec.interaction.focus_on_load==true,
        gesture:null,edit_gesture:null,engaged:spec.interaction.click_to_use!=true,suppress_click_until:null,
        expanded_clusters:[],media_session:null,media_wait:null,media_stopped:false,
        playback:{phase:if (spec.slideshow.autoplay==true) "playing" else "idle",elapsed:0,anchor:null,token:0,generation:0},
        *:authored};
    let ready=reconcile(spec,current);
    {*:ready,focus_id:ready.active_id}
}
pub fn reconcile(spec, current) {
    let source=if (current==null) initial(spec) else current;
    let controlled=map([for (field in fields where c.has(spec.value,field)) (field,spec.value[field])]);
    let merged={*:source,*:controlled};
    let ids=spec.events |> ~.id;
    let groups=spec.groups |> ~.id;
    let visible=story.items(spec,merged);
    let active=if (c.has(spec.value,"active_id") or merged.active_id==null or contains(visible |> ~.id,merged.active_id)) merged.active_id
        else visible_neighbor(spec,merged,visible |> ~.id);
    {*:merged,active_id:active,revision:spec.source.revision,
        draft:if (source.revision!=spec.source.revision) null else merged.draft,
        edit_gesture:if (source.revision!=spec.source.revision) null else merged.edit_gesture,
        selected_ids:[for (id in c.items(merged.selected_ids) where contains(ids,id)) id],
        expanded_events:[for (id in c.items(merged.expanded_events) where contains(ids,id)) id],
        collapsed_groups:[for (id in c.items(merged.collapsed_groups) where contains(groups,id)) id]}
}

fn notification(spec, action, field, old, value, revision) => {
    name:if (field=="active_id") "timeline_activate" else if (field=="selected_ids") "timeline_select"
        else if (field=="window") "timeline_window_change" else if (field=="query" or field=="filters") "timeline_search"
        else "timeline_change",
    detail:{chart_id:spec.id,ids:if (field=="active_id") c.items(value) else if (field=="selected_ids") value else [],
        source_revision:revision,origin:c.option(action,"origin","programmatic"),field:field,old:old,value:value}}

fn change(current, field, value, spec, action, context) {
    let controlled=c.has(spec.value,field);
    let notice=notification(spec,action,field,current[field],value,context.revision);
    {state:if (controlled) current else {*:current,[field]:value},
        requests:if (controlled and current[field]!=value) [{kind:"state_change",*:notice.detail}] else [],
        notifications:if (current[field]!=value) [notice] else []}
}
pub fn result(current) => {state:current,requests:[],notifications:[]}
fn playback_change(current, action, spec, context) {
    let sampled=story.command(current,action,context.now,spec);
    if (sampled is error) sampled else {
        let activated=change({*:current,playback:sampled.state.playback,story_frame:sampled.state.story_frame,
            media_wait:sampled.state.media_wait,media_stopped:sampled.state.media_stopped},"active_id",sampled.state.active_id,spec,action,context);
        {*:activated,notifications:[*sampled.notifications,*activated.notifications]}
    }
}
fn toggle(values, value) => if (contains(values,value)) values |: ~!=value else [*values,value]

fn activate(current, id, spec, action, context) {
    let changed_active=change(current,"active_id",id,spec,action,context);
    let aligned=if (changed_active.state.active_id==id and current.active_id!=id) story.align(changed_active.state,id,context.now,spec) else changed_active.state;
    let aligned_state={*:changed_active.state,
        *:(if (aligned is error) {} else aligned),
        visibility_anchor:null,
        *:(if (changed_active.state.active_id!=current.active_id) {media_stopped:false} else {}),
        *:(if (action.focus==true and changed_active.state.active_id==id) {focus_id:id,focus_group:null,focus_pending:true} else {})};
    let activated={*:changed_active,state:aligned_state};
    let anchor=if (current.selection_anchor!=null and len(current.selected_ids)>0) current.selection_anchor else current.active_id;
    let selected=if (action.extend==true) select(activated.state,{*:action,type:"select",id:id,anchor_id:anchor},spec,context) else null;
    let changed=if (selected==null) activated else {state:selected.state,requests:[*activated.requests,*selected.requests],notifications:[*activated.notifications,*selected.notifications]};
    let item=story.item(spec,id);
    let at=item.start.value;
    if (aligned is error) aligned else if (spec.spacing!="time" or spec.interaction.auto_scroll==false or at==null or at>=current.window.from and at<=current.window.to) changed
    else {
        let span=current.window.to-current.window.from;
        let from=dates.offset(at,-decimal(span)/2m);
        let shifted=set_window(changed.state,{*:action,type:"set_window",window:{from:from,to:dates.offset(from,span)}},spec,context);
        if (shifted is error) shifted else {state:shifted.state,requests:[*changed.requests,*shifted.requests],notifications:[*changed.notifications,*shifted.notifications]}
    }
}

fn visibility_change(current, field, value, spec, action, context) {
    let changed=change(current,field,value,spec,action,context);
    let shown=story.items(spec,changed.state) |> ~.id;
    let hidden=current.active_id!=null and not contains(shown,current.active_id);
    let recovery=current.active_id==null and len(shown)>0 and
        (current.visibility_anchor!=null or len(story.items(spec,current))==0);
    let focus_id=c.option(context,"focus_id",current.focus_id);
    let focus_hidden=focus_id!=null and not contains(shown,focus_id);
    let anchor=if (recovery) {*:current,active_id:current.visibility_anchor} else current;
    let activated=if (hidden or recovery) activate(changed.state,visible_neighbor(spec,anchor,shown),spec,
        {*:action,focus:action.focus==true or focus_hidden},context)
        else result(changed.state);
    let collapsed=field=="collapsed_groups" and action.type=="collapse" and action.group!=null and
        contains(changed.state.collapsed_groups,c.text(action.group));
    let header=if (collapsed) visible_group_header(spec,c.text(action.group),layout.visible(spec,changed.state).groups |> ~.id) else null;
    let focus=if (header!=null and (action.focus==true or focus_hidden))
        {focus_group:header,focus_pending:true} else {};
    if (activated is error) activated else {state:{*:activated.state,*:focus,
        visibility_anchor:if (hidden and activated.state.active_id==null) current.active_id else activated.state.visibility_anchor},
        requests:[*changed.requests,*activated.requests],notifications:[*changed.notifications,*activated.notifications]}
}

fn gesture_change(current, action, spec, context) {
    let plan=if (context.layout!=null) context.layout else layout.normalized(spec,context.viewport,current);
    let reduced=if (plan is error) plan else gesture.update(current,action,plan,spec);
    if (reduced is error) reduced else {
        let next={*:current,gesture:reduced.gesture,suppress_click_until:if (reduced.moved and contains(["gesture_end","gesture_cancel"],action.type))
            c.option(context,"now",0)+500 else current.suppress_click_until};
        if (reduced.window==null) result(next) else set_window(next,{*:action,type:"set_window",window:reduced.window},spec,context)
    }
}

fn navigate(current, action, spec, context) {
    let shown=layout.visible(spec,current);
    let ids=if (contains(["next_match","previous_match"],action.type)) shown.matches else story.items(spec,current) |> ~.id;
    let at=index_of(ids,current.active_id);
    let index=if (action.type=="first") 0 else if (action.type=="last") len(ids)-1
        else if (contains(["previous","previous_match"],action.type)) (if (at==null) len(ids)-1 else max(0,at-1))
        else if (at==null) 0 else min(len(ids)-1,at+1);
    activate(current,ids[index],spec,action,context)
}
fn select(current, action, spec, context) {
    let options=spec.interaction;
    let anchor=c.option(action,"anchor_id",if (current.selection_anchor!=null) current.selection_anchor else current.active_id);
    let ordered=layout.visible(spec,current).events;
    let first=index_of(ordered |> ~.id,anchor);
    let final=index_of(ordered |> ~.id,action.id);
    let requested=if (action.extend==true and options.contiguous==true and options.multiselect==true and first!=null and final!=null)
        slice(ordered,min(first,final),max(first,final)+1) |> ~.id
        else if (action.ids!=null) action.ids else c.items(action.id);
    let allowed=[for (id in requested where any([for (event in spec.events) event.id==id and event.selectable!=false])) id];
    let group=(spec.events |: ~.id==allowed[0])[0].group;
    let grouped=if (options.per_group==true) [for (id in allowed where (spec.events |: ~.id==id)[0].group==group) id] else allowed;
    let multi=options.multiselect==true;
    let previous=if (options.per_group==true and len(grouped)>0)
        [for (id in current.selected_ids where (spec.events |: ~.id==id)[0].group==group) id] else current.selected_ids;
    let ids=if (action.type=="clear") []
        else if (action.type=="toggle" and multi) (if (len(grouped)>0) toggle(previous,grouped[0]) else previous)
        else if (action.type=="toggle") (if (current.selected_ids==grouped) [] else slice(grouped,0,1))
        else if (multi) util.unique_vals(grouped) else slice(grouped,0,1);
    if (options.selectable==false) result(current) else change({*:current,selection_anchor:if (action.extend==true) anchor else grouped[0]},"selected_ids",ids,spec,action,context)
}
fn bounds(spec) {
    let w=spec.window;
    let min=if (w.min!=null) dates.normalize(w.min,spec.scale) else null;
    let max=if (w.max!=null) dates.normalize(w.max,spec.scale) else null;
    {min:min.value,max:max.value,min_span:w.min_span,max_span:w.max_span}
}
fn set_window(current, action, spec, context) {
    let visible=current.window;
    let span=visible.to-visible.from;
    let target=if (action.type=="fit") {
        let ids=if (action.ids!=null) action.ids else if (action.selection==true) current.selected_ids else null;
        let selected=if (ids!=null) spec.events |: contains(ids,~.id) else if (action.group!=null) spec.events |: ~.group==action.group else spec.events;
        layout.extent({*:spec,events:selected,eras:if (ids==null and action.group==null) spec.eras else []})
    } else if (action.type=="pan") {from:dates.offset(visible.from,action.delta),to:dates.offset(visible.to,action.delta)}
    else if (action.type=="zoom") {
        let anchor=if (action.anchor!=null) action.anchor else dates.interpolate(visible.from,visible.to,0.5);
        {from:dates.offset(anchor,decimal(visible.from-anchor)*decimal(action.factor)),
            to:dates.offset(anchor,decimal(visible.to-anchor)*decimal(action.factor))}
    } else action.window;
    let valid=util.first_error([
        if (action.type=="pan" and not util.finite_number(action.delta)) c.fail("action.delta","expected finite pan distance"),
        if (action.type=="zoom" and not c.positive(action.factor)) c.fail("action.factor","expected a positive zoom factor"),
        if (not util.finite_number(target.from) or not util.finite_number(target.to) or target.from>=target.to) c.fail("action.window","expected increasing bounds")]);
    if (valid is error) valid else {
        let constrained=layout.constrain(target,bounds(spec));
        if (constrained is error) constrained else change(current,"window",constrained,spec,action,context)
    }
}


// Source replacement and resource arrival use the same activation/selection policy.
fn replace_source(current, action, context) {
    let previous=if (context.normalized_spec!=null) context.normalized_spec else resource.normalize(context.spec,current);
    let stale=action.expected_revision!=null and action.expected_revision!=previous.source.revision;
    let spec=if (stale) previous else if (context.replacement_spec!=null) context.replacement_spec else resource.normalize(action.spec,current);
    let failure=if (previous is error) previous else if (spec is error) spec else if (spec.id!=previous.id)
        c.fail("source.id","a live source update must retain chart identity") else null;
    if (failure is error) failure else if (stale) result(current)
    else {
        let old=if (current==null) initial(previous) else current;
        let retained=reconcile(spec,old);
        let window=layout.window(spec,retained);
        let visible=story.items(spec,retained) |> ~.id;
        let recovery=old.active_id==null and len(story.items(previous,old))==0 and len(visible)>0;
        let id=if (c.has(spec.value,"active_id")) retained.active_id
            else if (contains(visible,old.active_id)) old.active_id
            else if (old.active_id!=null or recovery) visible_neighbor(previous,old,visible) else null;
        let focus_id=c.option(context,"focus_id",old.focus_id);
        let focus_removed=focus_id!=null and not contains(visible,focus_id);
        let changed=previous.events!=spec.events or previous.groups!=spec.groups or previous.source!=spec.source;
        let base={*:retained,window:window,active_id:if (c.has(spec.value,"active_id")) retained.active_id else old.active_id,
            draft:if (changed) null else retained.draft,edit_gesture:if (changed) null else retained.edit_gesture,
            expanded_clusters:if (changed) [] else retained.expanded_clusters,
            selection_anchor:if (contains(spec.events |> ~.id,retained.selection_anchor)) retained.selection_anchor else id};
        let activated=if (window is error) window else activate(base,id,spec,{*:action,focus:action.focus==true or focus_removed},
            {*:context,revision:spec.source.revision});
        let rebased=if (activated is error) activated else story.rebase(old,activated.state,previous,spec,context.now);
        let retired=if (rebased is error) rebased else resource.lifecycle(spec,rebased,context.now);
        if (retired is error) retired else {
            let ready=retired.state;
            let notices=[for (field in ["active_id","selected_ids","window"] where old[field]!=ready[field] and
                not any([for (notice in activated.notifications) notice.detail.field==field]))
                    notification(spec,action,field,old[field],ready[field],spec.source.revision)];
            {state:ready,source:action.spec,normalized_spec:spec,requests:[*activated.requests,*retired.requests],
                notifications:[*activated.notifications,*notices,*retired.notifications,
                    {name:"timeline_change",detail:{chart_id:spec.id,field:if (context.resource_change==true) "data" else "source",
                        old:if (context.resource_change==true) previous.events else previous.source.revision,
                        value:if (context.resource_change==true) spec.events else spec.source.revision,source_revision:spec.source.revision,origin:c.option(action,"origin","programmatic")}}]}
        }
    }
}

pub fn update(current, action, context) {
    let spec=if (context.normalized_spec!=null) context.normalized_spec else resource.normalize(context.spec,current);
    let reduced=if (spec is error) spec else reduce(current,action,{*:context,normalized_spec:spec});
    let authored=if (context.source_spec!=null) context.source_spec else if (context.spec!=null) context.spec else schema.source_record(spec);
    let next_spec=if (reduced is error) reduced else if (reduced.normalized_spec!=null) reduced.normalized_spec
        else if (action.type=="resource_result") resource.normalize(authored,reduced.state) else spec;
    let settled=if (next_spec is error) next_spec else if (action.type=="resource_result" and spec!=next_spec) {
        let changed=replace_source(reduced.state,{type:"source_update",spec:authored,origin:c.option(action,"origin","resource")},
            {*:context,normalized_spec:spec,replacement_spec:next_spec,resource_change:true});
        if (changed is error) changed else {*:changed,requests:[*reduced.requests,*changed.requests],
            notifications:[*reduced.notifications,*changed.notifications]}
    } else reduced;
    if (settled is error) settled else {
        let synchronized=media_playback.synchronize(settled.state,next_spec,context.now);
        {*:settled,normalized_spec:next_spec,state:synchronized.state,requests:[*settled.requests,*synchronized.requests],
            notifications:[*settled.notifications,*(if (next_spec.story.bookmarks==true and synchronized.state.active_id!=null and
                current!=null and synchronized.state.active_id!=current.active_id)
                [{name:"timeline_bookmark",detail:{chart_id:next_spec.id,event_id:synchronized.state.active_id,
                    fragment:bookmark.address(next_spec,synchronized.state.active_id),source_revision:next_spec.source.revision,
                    origin:c.option(action,"origin","programmatic")}}] else [])]}
    }
}

fn reduce(current, action, context) {
    let spec=if (context.normalized_spec!=null) context.normalized_spec else schema.validate(context.spec);
    let st=if (spec is error) spec else reconcile(spec,if (current==null) initial(spec) else current);
    let kind=c.text(action.type);
    if (spec is error) spec else if (st is error) st else if (not (action is map)) c.fail("action","expected an action map")
    else if (kind=="bookmark") {
        let id=bookmark.resolve(spec,action.fragment);
        if (id is error) id else if (id==null) result(st) else activate(st,id,spec,action,context)
    }
    else if (contains(["first","previous","next","last","next_match","previous_match"],kind)) navigate(st,action,spec,context)
    else if (contains(["activate","focus"],kind)) {
        if (action.id!=null and story.item(spec,c.text(action.id))==null) c.fail("action.id","unknown event")
        else activate(st,if (action.id!=null) c.text(action.id) else null,spec,action,context)
    } else if (contains(["select","toggle","clear"],kind)) select(st,action,spec,context)
    else if (contains(["set_window","pan","zoom","fit"],kind)) set_window(st,action,spec,context)
    else if (contains(["zoom_in","zoom_out"],kind)) set_window(st,{*:action,type:"zoom",factor:if (kind=="zoom_in") 0.8 else 1.25},spec,context)
    else if (kind=="reset") set_window(st,{*:action,type:"set_window",window:layout.window(spec)},spec,context)
    else if (starts_with(kind,"gesture_")) gesture_change(st,action,spec,context)
    else if (kind=="engage") result({*:st,engaged:true})
    else if (kind=="set_current_time") {
        let at=if (action.at!=null) dates.normalize(action.at,spec.scale) else null;
        if (at is error) at else {state:{*:st,current_time:action.at},requests:[],notifications:[
            {name:"timeline_cursor",detail:{chart_id:spec.id,old:st.current_time,value:action.at,
                source_revision:context.revision,origin:c.option(action,"origin","programmatic")}}]}
    }
    else if (kind=="cluster_expand") {
        let ids=c.items(action.ids);
        if (len(ids)>c.option(spec.cluster,"max_expansion",1000)) c.fail("cluster.max_expansion","cluster exceeds the explicit expansion limit; narrow the window or filter the source")
        else if (not all([for (id in ids) contains(spec.events |> ~.id,id)])) c.fail("cluster.ids","cluster refers to removed events")
        else result({*:st,expanded_clusters:util.unique_vals([*st.expanded_clusters,c.text(action.id)])})
    }
    else if (kind=="search") visibility_change(st,"query",c.text(action.query),spec,action,context)
    else if (kind=="filter") {
        let valid=search.filters(spec,{filters:action.filters});
        if (valid is error) valid else visibility_change(st,"filters",c.attrs(action.filters),spec,action,context)
    }
    else if (contains(["collapse","expand"],kind)) {
        let field=if (action.group!=null) "collapsed_groups" else "expanded_events";
        let id=c.text(if (action.group!=null) action.group else action.id);
        let adding=(action.group!=null) == (kind=="collapse");
        let ids=if (adding) util.unique_vals([*st[field],id]) else st[field] |: ~!=id;
        if (id=="" or not contains(if (field=="collapsed_groups") spec.groups |> ~.id else spec.events |> ~.id,id))
            c.fail(if (field=="collapsed_groups") "action.group" else "action.id","unknown " ++ (if (field=="collapsed_groups") "group" else "event"))
        else if (field=="collapsed_groups") visibility_change(st,field,ids,spec,action,context)
        else change(st,field,ids,spec,action,context)
    } else if (kind=="set_view") {
        if (not c.enum(action.view,["cards","axis","story"])) c.fail("action.view","invalid view")
        else if (action.view=="axis" and spec.spacing!="time") c.fail("action.view","axis presentation requires time spacing")
        else change(st,"view",c.text(action.view),spec,action,context)
    } else if (kind=="set_theme") change(st,"theme",action.theme,spec,action,context)
    else if (kind=="edit_key") editing.keyboard(st,action,spec,context)
    else if (contains(["edit_start","edit_move","edit_end"],kind)) editing.pointer(st,action,spec,context)
    else if (starts_with(kind,"edit_")) editing.reduce(st,action,spec,context)
    else if (kind=="resource_tick") resource.lifecycle(spec,st,context.now)
    else if (kind=="resource_request") {
        let descriptor=(resource.descriptors(spec,st) |: ~.id==action.id)[0];
        if (descriptor==null) c.fail("resource.id","unknown resource") else {
            let started=resource.begin(st,descriptor,context.revision,context.now);
            {state:started.state,requests:[started.request],notifications:[]}
        }
    } else if (kind=="resource_result") {
        let next=resource.complete(st,action,context.revision,context.now,
            if (context.source_spec!=null) context.source_spec else if (context.spec!=null) context.spec else schema.source_record(spec));
        {state:next,requests:[],notifications:if (next==st) [] else
            [{name:"timeline_resource",detail:{chart_id:spec.id,id:action.id,result:next.resources[action.id].result,error:next.resources[action.id].error,
                source_revision:context.revision,origin:c.option(action,"origin","programmatic")}}]}
    } else if (kind=="media_event") {
        let accepted=media_playback.accept(st,action,spec);
        if (accepted is error) accepted else if (accepted==st) result(accepted)
        else {
            let next=if (accepted.playback.phase=="playing") playback_change(accepted,{*:action,type:"tick"},spec,context) else result(accepted);
            if (next is error) next else {*:next,notifications:[{name:"timeline_media",detail:{chart_id:spec.id,
                event_id:action.event_id,id:action.id,token:action.token,source_revision:action.revision,
                status:action.status,origin:c.option(action,"origin","programmatic")}},*next.notifications]}
        }
    } else if (contains(["play","pause","resume","seek","stop","tick"],kind)) playback_change(st,action,spec,context)
    else if (kind=="source_update") replace_source(st,action,context)
    else if (kind=="reconcile") result(reconcile(spec,st))
    else c.fail("action.type","unknown action " ++ kind)
}
