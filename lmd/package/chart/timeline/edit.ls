// edits retain source values and return immutable application requests (S9.1.4).
import c: .common
import schema: .schema
import dates: .date
import coordinate: .coordinate
import time_map: .time_map
import util: lambda.chart.util
import theme: .theme
import lanes: .groups

let operations=["add","update","remove","move_time","move_group","resize","group","subgroup"]
pub fn allowed(spec, item, operation, change = null) {
    let fallback=if (contains(["move_time","move_group","resize","group","subgroup"],operation)) "update" else operation;
    let permission=if (change!=null) change else operation;
    let override=if (item.editable is map) c.option(item.editable,permission,item.editable[fallback]) else item.editable;
    spec.edit.enabled==true and contains(operations,operation) and
        (if (spec.edit.override_items==true and override!=null) override==true
         else c.option(spec.edit,operation,spec.edit[fallback])!=false and
            (change==null or spec.edit[change]!=false) and override!=false)
}
fn date_value(record) => if (record.kind!="calendar") record.value else
    map([for (key in ["year","month","day","hour","minute","second","millisecond","precision","timezone"]) (key,record[key])])
fn from_value(value, spec) => date_value(dates.from_coordinate(value,spec.scale))
fn nearest(value, step) => dates.offset(value,-decimal(value)%decimal(step)+
    (if (decimal(value)%decimal(step)<0) -decimal(step) else 0)+
    (if ((decimal(value)%decimal(step)+decimal(step))%decimal(step)>=decimal(step)/2m) decimal(step) else 0))

pub fn snap(value, spec, context = {}) {
    let options=spec.edit;
    let unit=c.text(options.snap_unit);
    let step=c.option(options,"snap_step",1);
    if (options.snap is fn) {
        let result=options.snap(dates.from_coordinate(value,spec.scale),context);
        let parsed=if (result is error) result else dates.normalize(result,spec.scale);
        if (parsed is error) parsed else parsed.value
    } else if (unit=="" and options.snap_step==null) value
    else if (spec.scale.kind!="calendar" or unit=="unit") nearest(value,step)
    else {
        let record=dates.from_coordinate(value,spec.scale);
        let fixed={millisecond:1,second:1000,minute:60000,hour:3600000};
        if (fixed[unit]!=null) {
            let midnight=dates.normalize({year:record.year,month:record.month,day:record.day,timezone:record.timezone},spec.scale).value;
            dates.offset(midnight,nearest(value-midnight,fixed[unit]*step))
        } else {
            let month_index=record.year*12+record.month-1;
            let civil=if (unit=="year") {year:dates.floor_div(record.year,step)*step,month:1,day:1}
                else if (unit=="month") {let aligned=dates.floor_div(month_index,step)*step;
                    {year:dates.floor_div(aligned,12),month:aligned%12+(if (aligned%12<0) 13 else 1),day:1}}
                else {let day=dates.days(record.year,record.month,record.day);
                    let anchor=if (unit=="week") c.option(spec.scale,"week_start",1)-4 else 0;
                    let period=step*(if (unit=="week") 7 else 1);
                    dates.civil(dates.floor_div(day-anchor,period)*period+anchor)};
            let lower=dates.normalize({*:civil,timezone:record.timezone},spec.scale);
            let upper=if (lower is error) lower else dates.shift_calendar(lower,if (unit=="") "day" else unit,step,spec.scale);
            if (lower is error) lower else if (upper is error) upper else
                if (value-lower.value<upper.value-value) lower.value else upper.value
        }
    }
}

fn mutate(values, id, proposed, change) => if (change=="add") [*values,proposed]
    else if (change=="remove") values |: ~.id!=id
    else [for (item in values) if (item.id==id) {*:item,*:proposed} else item]

// the source collection is never changed in place; consumers can publish this value.
pub fn apply_normalized(spec, request) {
    let source=schema.source_record(spec);
    let original=[for (item in spec.events) schema.event_record(item)];
    let operation=request.operation;
    let proposed=request.proposed;
    let events=if (contains(["group","subgroup"],operation)) original
        else mutate(original,request.ids[0],proposed,if (operation=="add" or operation=="remove") operation else "update");
    let groups=if (operation=="group") mutate([for (item in spec.groups) schema.group_record(item)],request.ids[0],proposed,request.change)
        else if (operation=="subgroup") [for (item in spec.groups) if (item.id!=request.ids[0]) schema.group_record(item)
            else {*:schema.group_record(item),subgroups:mutate(c.items(item.subgroups),request.subgroup,proposed,request.change)}]
        else [for (item in spec.groups) schema.group_record(item)];
    let result={*:source,events:events,groups:groups,data:null,encoding:null,transform:null,source:null};
    let valid=schema.validate(result);
    if (valid is error) valid else result
}
pub fn apply_edit(raw, request) {
    let spec=schema.validate(raw);
    if (spec is error) spec else if (not (request is map) or not contains(operations,request.operation) or
        not (request.ids is array) or len(request.ids)!=1 or c.id(request.ids[0]) is error or request.chart_id!=spec.id) c.fail("edit","invalid edit request")
    else if (request.source_revision!=spec.source.revision) c.fail("edit.revision","source changed after this request")
    else {let checked=prepare(spec,{sequence:0},{id:request.ids[0],operation:request.operation,proposed:request.proposed,
        subgroup:request.subgroup,change:request.change,origin:request.origin},{revision:spec.source.revision});
        if (checked is error) checked else checked.candidate}
}
pub fn prepare(spec, current, action, context) {
    let operation=c.text(c.option(action,"operation","update"));
    let grouped=contains(["group","subgroup"],operation);
    let change=if (grouped) c.text(c.option(action,"change","update")) else operation;
    let creating=change=="add";
    let generated=if (operation!="add" or action.id!=null or action.proposed.id!=null) null else
        ([for (index in (current.sequence+1) to (current.sequence+len(spec.events)+1),
            let candidate="event-" ++ c.text(index) where not contains(spec.events |> ~.id,candidate)) candidate])[0];
    let id=c.id(if (action.id!=null) action.id else if (action.proposed.id!=null and operation!="subgroup") action.proposed.id else if (operation=="add") generated else null);
    let group=if (grouped) (spec.groups |: ~.id==id)[0] else null;
    let target=if (operation=="subgroup") (c.items(group.subgroups) |: ~.id==action.subgroup)[0]
        else if (operation=="group") group else (spec.events |: ~.id==id)[0];
    let original=if (creating) null else target;
    let supplied=c.attrs(action.proposed);
    let subgroup=if (action.subgroup!=null) c.id(action.subgroup) else if (operation=="subgroup") c.id(supplied.id) else null;
    let proposed=if (creating) {*:supplied,id:if (operation=="subgroup") subgroup else id} else supplied;
    let request={kind:"edit",token:c.text(spec.id) ++ ":" ++ c.text(current.sequence+1),chart_id:spec.id,source_revision:context.revision,
        ids:[id],operation:operation,change:if (grouped) change else null,original:original,proposed:proposed,subgroup:subgroup,
        origin:c.option(action,"origin","programmatic"),confirmation:spec.edit.confirm==true};
    let permission=allowed(spec,if (operation=="subgroup") group else original,operation,if (grouped) change else null) and (creating or original!=null);
    let failure=util.first_error([id,subgroup,
        if (grouped and not contains(["add","update","remove"],change)) c.fail("edit.change","expected add, update, or remove"),
        if (not permission) c.fail("edit","operation is disabled or the target is missing"),
        if (change!="remove" and not (action.proposed is map)) c.fail("edit.proposed","expected a patch map"),
        if (not creating and c.has(proposed,"id") and c.text(proposed.id)!=(if (operation=="subgroup") subgroup else id)) c.fail("edit.id","an edit cannot change stable identity"),
        if (operation=="subgroup" and group==null) c.fail("edit.group","unknown subgroup owner"),
        if (contains(["move_time","move_group","resize"],operation)) c.properties(proposed,
            if (operation=="move_group") ["group","subgroup"] else if (operation=="resize") ["from","to","open_start","open_end"]
            else ["at","from","to","open_start","open_end","group","subgroup"],"edit.proposed"),
        if (not grouped and not creating and change!="remove" and
            (c.has(proposed,"group") or c.has(proposed,"subgroup")) and not allowed(spec,original,"move_group")) c.fail("edit.move_group","changing lanes is disabled"),
        if (operation=="update" and any([for (key in ["at","from","to","open_start","open_end"]) c.has(proposed,key)]) and
            not allowed(spec,original,"move_time")) c.fail("edit.move_time","changing dates is disabled"),
        if (original.unstable_id==true) c.fail("edit.id","editing requires an authored stable ID")]);
    let candidate=if (failure is error) failure else apply_normalized(spec,request);
    let verdict=if (candidate is error) candidate else if (spec.edit.validate is fn) spec.edit.validate(request,{chart_id:spec.id,source_revision:context.revision}) else true;
    if (verdict is error) verdict else if (verdict!=true) c.fail("edit.validate","validation rejected the proposed change")
    else {*:request,candidate:candidate}
}
pub fn preview(spec, current) {
    let request=if (current.edit_gesture.request!=null) current.edit_gesture.request else current.draft;
    if (request.candidate==null or request.source_revision!=current.revision) spec
    else {let value=schema.validate(request.candidate); if (value is error) spec else value}
}
fn result(current) => {state:current,requests:[],notifications:[]}
pub fn reduce(current, action, spec, context) {
    if (action.type=="edit_request") {
        let request=prepare(spec,current,action,context);
        if (request is error) request else {state:{*:current,sequence:current.sequence+1,draft:request,edit_gesture:null},requests:[request],
            notifications:[{name:"timeline_edit_request",detail:request}]}
    } else if (action.type=="edit_cancel") result({*:current,draft:null,edit_gesture:null})
    else if (current.draft==null or action.token!=current.draft.token or context.revision!=current.draft.source_revision) result(current)
    else if (action.type=="edit_accept") {state:{*:current,draft:null},requests:[{*:current.draft,kind:"commit_edit"}],
        notifications:[{name:"timeline_edit_commit",detail:current.draft}]}
    else if (action.type=="edit_reject") {state:{*:current,draft:null},requests:[],
        notifications:[{name:"timeline_error",detail:{chart_id:spec.id,error:action.error,token:action.token}}]}
    else c.fail("action.type","unknown edit action")
}

fn instant(plan, point) => time_map.invert(plan.time_map,coordinate.invert(plan.path,point).fraction)
pub fn pointer(current, action, spec, context) {
    let dragging=current.edit_gesture;
    let point=action.point;
    let valid=point is array and len(point)==2 and all(point |> util.finite_number(~));
    if (action.type=="edit_start") {
        let item=(spec.events |: ~.id==action.id)[0];
        let mode=c.text(c.option(action,"operation","move_time"));
        if (not valid or context.layout==null) c.fail("edit.point","expected finite coordinates and a layout")
        else if (item==null or not contains(["move_time","move_group","resize"],mode) or not allowed(spec,item,mode)) c.fail("edit","drag operation is disabled")
        else if (mode=="resize" and (item.kind=="point" or not contains(["start","end"],action.handle))) c.fail("edit.resize","expected a range endpoint handle")
        else if (mode=="move_time" and item.start==null and item.end==null) c.fail("edit.move_time","cannot move an undated event along time")
        else result({*:current,draft:null,edit_gesture:{id:item.id,operation:mode,handle:action.handle,pointer_id:action.pointer_id,
            point:point,plan:context.layout,original:item,revision:context.revision,moved:false,request:null}})
    } else if (dragging==null or dragging.pointer_id!=action.pointer_id or dragging.revision!=context.revision) result(current)
    else if (not valid) c.fail("edit.point","expected finite coordinates")
    else {
        let value=snap(instant(dragging.plan,point),spec,{event:dragging.original,operation:dragging.operation});
        let anchor=snap(instant(dragging.plan,dragging.point),spec,{event:dragging.original,operation:dragging.operation});
        let failure=util.first_error([value,anchor]);
        let delta=if (failure is error) 0 else value-anchor;
        let item=dragging.original;
        let target=if (dragging.operation=="move_group" or dragging.operation=="move_time" and spec.edit.drag_drop==true and allowed(spec,item,"move_group")) lanes.pick(dragging.plan,point) else null;
        let group=if (target==null) {} else {group:target.group,subgroup:target.subgroup};
        let patch=if (failure is error) {} else if (dragging.operation=="move_group") group
            else if (dragging.operation=="resize") map([(if (dragging.handle=="start") "from" else "to",from_value(value,spec)),
                (if (dragging.handle=="start") "open_start" else "open_end",false)])
            else if (item.kind=="point") {at:from_value(dates.offset(item.start.value,delta),spec)}
            else map([for (key,record in {from:item.start,to:item.end} where record!=null) (key,from_value(dates.offset(record.value,delta),spec))]);
        let moved=dragging.moved or math.sqrt((point[0]-dragging.point[0])**2+(point[1]-dragging.point[1])**2)>3;
        let request=if (failure is error) failure else if (dragging.operation=="move_group" and target==null) c.fail("edit.move_group","drop inside a visible lane")
            else prepare(spec,current,{type:"edit_request",id:item.id,operation:dragging.operation,proposed:{*:patch,*:group},origin:c.option(action,"origin","pointer")},context);
        if (action.type=="edit_end") {
            if (not moved) result({*:current,edit_gesture:null}) else if (request is error) request
            else {state:{*:current,edit_gesture:null,draft:request,sequence:current.sequence+1,suppress_click_until:c.option(context,"now",0)+500},
                requests:[request],notifications:[{name:"timeline_edit_request",detail:request}]}
        } else if (action.type!="edit_move") c.fail("action.type","unknown edit pointer action")
        else result({*:current,edit_gesture:{*:dragging,moved:moved,request:if (request is error) null else request,error:if (request is error) request else null}})
    }
}

fn keyboard_date(record, direction, spec, fallback, context) {
    let base=if (record==null) dates.from_coordinate(fallback,spec.scale) else record;
    let unit=c.text(c.option(spec.edit,"snap_unit",if (spec.scale.kind=="calendar") "day" else "unit"));
    let step=c.option(spec.edit,"snap_step",1)*direction;
    let value=if (spec.scale.kind=="calendar" and unit!="unit") dates.shift_calendar(base,unit,step,spec.scale)
        else dates.from_coordinate(dates.offset(base.value,step),spec.scale);
    let snapped=if (value is error) value else snap(value.value,spec,context);
    if (snapped is error) snapped else from_value(snapped,spec)
}

pub fn keyboard(current, action, spec, context) {
    let shown=preview(spec,current);
    let item=(shown.events |: ~.id==action.id)[0];
    let mode=c.text(c.option(action,"operation","move_time"));
    let direction=action.direction;
    let rows=context.layout.lanes;
    let found=(c.items(rows) |: ~.group==item.group and ~.subgroup==item.subgroup)[0];
    let position=if (found==null) -1 else index_of(rows,found)+direction;
    let target=if (position<0 or position>=len(c.items(rows))) null else rows[position];
    let input={event:item,operation:mode,origin:"keyboard"};
    let patch=if (mode=="move_group") if (target==null) null else {group:target.group,subgroup:target.subgroup}
        else if (mode=="resize") {let endpoint=if (action.handle=="start") "from" else "to";
            map([(endpoint,keyboard_date(if (endpoint=="from") item.start else item.end,direction,spec,current.window[endpoint],input)),
                (if (endpoint=="from") "open_start" else "open_end",false)])}
        else if (item.kind=="point") {at:keyboard_date(item.start,direction,spec,current.window.from,input)}
        else map([for (key,record in {from:item.start,to:item.end} where record!=null) (key,keyboard_date(record,direction,spec,current.window[string(key)],input))]);
    let failure=util.first_error([for (key,value in c.attrs(patch)) value]);
    if (item==null or not contains(["move_time","move_group","resize"],mode) or not allowed(spec,item,mode)) c.fail("edit.keyboard","operation is disabled or target is missing")
    else if (direction!=1 and direction!=-1) c.fail("edit.keyboard","expected a forward or backward step")
    else if (failure is error) failure else if (patch==null) result(current)
    else reduce(current,{type:"edit_request",id:item.id,operation:mode,proposed:patch,origin:"keyboard"},spec,context)
}

fn handle(spec, item, kind, label) => <button *:theme.part(spec,"edit_handle",{type:"button",
    id:"timeline-edit-" ++ util.binding_key([spec.id,item.id,kind]),class:"lambda-timeline-edit-handle",
    'data-timeline-edit-handle':kind,'aria-label':label,
    'aria-description':c.message(spec,"edit_instructions","Use arrow keys to adjust, Enter to apply, and Escape to cancel.")}),label>

pub fn controls(spec, item) {
    if (spec.edit.enabled!=true) [] else [
        if (allowed(spec,item,"move_time") and item.start!=null and item.end!=null)
            handle(spec,item,"move",c.message(spec,"move_event","Move event")),
        if (len(spec.groups)>0 and allowed(spec,item,"move_group"))
            handle(spec,item,"group",c.message(spec,"move_group","Move event to another lane")),
        if (item.kind!="point" and allowed(spec,item,"resize"))
            for (side in ["start","end"]) handle(spec,item,side,c.message(spec,"resize_" ++ side,"Resize " ++ side)),
        if (allowed(spec,item,"remove")) <button type:"button",'data-timeline-command':"remove",
            'data-timeline-action':format({type:"edit_request",operation:"remove",id:item.id},'json'),
            c.message(spec,"remove_event","Remove event")>] |: ~!=null
}
pub fn pending(spec, current) {
    let request=current.draft;
    if (request==null) null else <aside *:theme.part(spec,"edit_preview",{class:"lambda-timeline-edit-preview",role:"region",
        'aria-label':c.message(spec,"pending_edit","Pending timeline edit"),'data-timeline-edit-token':request.token}),
        <p role:"status",c.message(spec,"edit_preview","Review the proposed change before applying it.")>;
        *[for (command in ["edit_accept","edit_cancel"]) <button type:"button",'data-timeline-command':command,
            'data-timeline-action':format({type:command,token:request.token},'json'),
            c.message(spec,command,if (command=="edit_accept") "Apply change" else "Cancel change")>]>
}
