// group and subgroup rows use the same normal offsets on straight and curved axes.
import c: .common
import util: lambda.chart.util
import coordinate: .coordinate

pub fn ordered(values, parent = null, depth = 0) {
    let siblings=sort(values |: ~.parent==parent,{by:(item)=>c.option(item,"order",0)});
    [for (item in siblings) for (entry in [{*:item,depth:depth},*ordered(values,item.id,depth+1)]) entry]
}
fn height(value, fallback) => max(c.option(value,"min_height",1),min(c.option(value,"max_height",fallback),c.option(value,"height",fallback)))
fn row(group, subgroup, events, spec) {
    let id=if (subgroup!=null) subgroup.id else null;
    let items=events |: ~.group==group.id and ~.subgroup==id;
    let options=if (subgroup!=null) subgroup else group;
    let cross=c.option(spec.cards,if (spec.orientation=="vertical") "width" else "height",if (spec.orientation=="vertical") 160 else 64);
    let count=if (options.height_mode=="fit_items") max(1,len(items)) else 1;
    let fallback=max(c.option(spec.coordinate,"lane_width",60),count*(cross+spec.gap*2));
    {group:group.id,subgroup:id,title:if (subgroup!=null) subgroup.title else group.title,
        height:height(options,fallback),stack:options.stack!=false,appearance:options.appearance,ids:items |> ~.id}
}
fn group_rows(group, events, spec, collapsed) {
    let subgroups=sort(c.items(group.subgroups) |: ~.visible!=false,{by:(item)=>c.option(item,"order",0)});
    let rows=if (contains(collapsed,group.id)) [] else [
        if (len(subgroups)==0 or any([for (event in events) event.group==group.id and event.subgroup==null])) row(group,null,events,spec),
        for (subgroup in subgroups) row(group,subgroup,events,spec)] |: ~!=null;
    let required=sum(rows |> ~.height)+spec.gap;
    let total=height(group,max(spec.gap,required));
    let available=max(1,total-spec.gap);
    let scale=if (required>spec.gap) available/(required-spec.gap) else 1;
    {group:group,height:total,rows:[for (item in rows) {*:item,height:item.height*scale}]}
}
fn positions(values, from = 0.0, index = 0) {
    if (index>=len(values)) [] else {
        let item=values[index];
        [{*:item,from:from,to:from+item.height,offset:from+item.height/2.0},*positions(values,from+item.height,index+1)]
    }
}

pub fn configure(spec, shown, snapshot = null) {
    let collapsed=c.items(c.option(c.attrs(snapshot),"collapsed_groups",[for (group in spec.groups where group.collapsed==true) group.id]));
    let groups=ordered(shown.groups);
    let ungrouped=shown.events |: ~.group==null;
    let neutral=if (len(ungrouped)>0) [group_rows({id:null,title:null,subgroups:[]},shown.events,spec,[])] else [];
    let blocks=[*neutral,for (group in groups) group_rows(group,shown.events,spec,collapsed)];
    let size=sum(blocks |> ~.height);
    let direction=c.text(c.option(spec.coordinate,"lane_direction","both"));
    let origin=if (direction=="start") -size else if (direction=="end") 0.0 else -size/2.0;
    let positioned=positions(blocks,origin);
    {height:size,groups:[for (block in positioned where block.group.id!=null)
        {*:block.group,from:block.from,to:block.to,offset:block.from+spec.gap/2.0,
            collapsed:contains(collapsed,block.group.id)}],
        rows:[for (block in positioned) for (item in positions(block.rows,block.from+spec.gap)) item]}
}

pub fn lane(model, event, options = {}) {
    let found=(model.rows |: ~.group==event.group and ~.subgroup==event.subgroup)[0];
    let index=index_of(model.rows,found);
    let custom=if (options.lane_offsets is array) options.lane_offsets[index]
        else if (event.subgroup!=null) options.lane_offsets[event.group][event.subgroup] else options.lane_offsets[event.group];
    {index:index,offset:if (custom!=null) custom else if (found!=null) found.offset else 0.0,
        height:found.height,stack:found.stack!=false}
}

// Picking uses the same path normals and custom offsets as lane placement.
pub fn pick(plan, point) {
    let hit=coordinate.invert(plan.path,point);
    let frame=coordinate.project(plan.path,hit.fraction);
    let offset=(point[0]-hit.point[0])*frame.normal[0]+(point[1]-hit.point[1])*frame.normal[1];
    let model={rows:plan.lanes};
    let candidates=[for (row in plan.lanes,let position=lane(model,row,plan.spec.coordinate),
        let distance=abs(offset-position.offset) where distance<=position.height/2.0)
        {*:row,distance:distance}];
    sort(candidates,{by:(row)=>row.distance})[0]
}
