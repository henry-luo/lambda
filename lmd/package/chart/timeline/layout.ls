// pure geometry uses exact chronology until the bounded pixel projection.
import c: .common
import dates: .date
import coordinate: .coordinate
import theme: .theme
import schema: .schema
import util: lambda.chart.util
import sizing: lambda.chart.sizing
import text: lambda.chart.text
import collision: lambda.chart.collision
import time_map: .time_map
import lanes: .groups
import cluster: .cluster
import annotation: .annotation
import axis: .axis
import search: .search
import series: .series
import card: .card

pub fn plain(value) => c.plain(value)

pub fn extent(spec) {
    let points = [for (item in [*spec.events,*spec.eras]) for (value in [item.start.value,item.end.value] where value != null) value];
    if (spec.scale.domain is array) {from:dates.normalize(spec.scale.domain[0],spec.scale).value,to:dates.normalize(spec.scale.domain[1],spec.scale).value}
    else if (len(points) == 0) {from:0,to:1}
    else {from:min(points),to:if (min(points)==max(points)) max(points)+(if (spec.scale.kind=="calendar") 86400000 else 1) else max(points)}
}
fn date_option(value, scale) {
    let parsed=if (value!=null) dates.normalize(value,scale) else null;
    if (parsed is error) parsed else parsed.value
}
pub fn window(spec, snapshot = null) {
    let domain=extent(spec);
    let requested=spec.window;
    let from=if (snapshot.window.from != null) snapshot.window.from else if (requested.from!=null) date_option(requested.from,spec.scale) else domain.from;
    let to=if (snapshot.window.to!=null) snapshot.window.to else if (requested.to!=null) date_option(requested.to,spec.scale) else domain.to;
    let minimum=date_option(requested.min,spec.scale);
    let maximum=date_option(requested.max,spec.scale);
    let failure=util.first_error([from,to,minimum,maximum,
        if (not util.finite_number(from) or not util.finite_number(to) or from>=to) c.fail("window", "expected increasing finite bounds"),
        if (minimum!=null and maximum!=null and minimum>=maximum) c.fail("window", "min must precede max"),
        for (key in ["min_span","max_span"] where requested[key]!=null)
            if (not c.positive(requested[key])) c.fail("window." ++ key,"expected a positive span")]);
    if (failure is error) failure else constrain({from:from,to:to},
        {min:minimum,max:maximum,min_span:requested.min_span,max_span:requested.max_span})
}
pub fn constrain(value, bounds) {
    let requested=value.to-value.from;
    let smallest=if (bounds.min_span!=null) bounds.min_span else requested;
    let desired=max(requested,smallest);
    let span=min([desired,if (bounds.max!=null and bounds.min!=null) bounds.max-bounds.min else desired,
        if (bounds.max_span!=null) bounds.max_span else desired]);
    let centered=dates.offset(value.from,decimal(requested-span)/2m);
    let low=if (bounds.min!=null) max(centered,bounds.min) else centered;
    let from=if (bounds.max!=null) min(low,bounds.max-span) else low;
    if (not c.positive(span)) c.fail("window", "constraints leave no visible interval") else {from:from,to:dates.offset(from,span)}
}

fn ancestors_visible(group, groups, collapsed, seen = []) {
    if (group.visible == false) false else if (group.parent==null) true
    else if (contains(collapsed,group.parent)) false
    else {
        let parent=(groups |: ~.id==group.parent)[0];
        if (parent==null or contains(seen,group.parent)) false else ancestors_visible(parent,groups,collapsed,[*seen,group.id])
    }
}
pub fn visible(spec, snapshot = null) {
    let collapsed=c.option(c.attrs(snapshot),"collapsed_groups",[for (group in spec.groups where group.collapsed==true) group.id]);
    let groups=[for (group in spec.groups where ancestors_visible(group,spec.groups,collapsed)) group];
    let filters=search.filters(spec,snapshot);
    let query=c.text(c.option(c.attrs(snapshot),"query",c.option(spec.search,"query","")));
    let matches=[for (item in spec.events,
        let group=if (item.group!=null) (groups |: ~.id==item.group)[0] else null,
        let subgroup=if (item.subgroup!=null) (c.items(group.subgroups) |: ~.id==item.subgroup)[0] else null,
        let in_group=item.group==null or contains(groups |> ~.id,item.group) and not contains(collapsed,item.group) and subgroup.visible!=false,
        let accepted=search.accepts(item,filters),
        let eligible=item.visible!=false and in_group and accepted,
        let matched=if (eligible) search.matches(item,query,spec.search,{chart_id:spec.id,locale:spec.locale}) else false
        where eligible and (matched is error or c.text(spec.search.mode)!="filter" or matched))
        if (matched is error) matched else {*:item,matched:matched}];
    let failure=util.first_error([filters,*matches]);
    if (failure is error) failure else {events:matches,groups:groups,matches:[for (item in matches where item.matched) item.id],query:query}
}


pub fn in_window(item, domain) => (item.end==null or item.end.value>=domain.from) and (item.start==null or item.start.value<=domain.to)

fn box_at(point, width, height) => {left:point[0]-width/2.0,right:point[0]+width/2.0,top:point[1]-height/2.0,bottom:point[1]+height/2.0}
fn fit_box(bounds, plot) map | error => collision.translate(bounds,
    max(plot.x-bounds.left,min(0,plot.x+plot.width-bounds.right)),
    max(plot.y-bounds.top,min(0,plot.y+plot.height-bounds.bottom)))
fn clear_box(box, obstacles, direction, gap, depth = 0) {
    let overlaps=obstacles |: collision.overlaps(box,~,gap);
    if (len(overlaps)==0) box else if (depth>len(obstacles)) c.fail("layout","collision placement did not converge")
    else {
        let shift=collision.outward_shift(box,overlaps,direction,gap)+0.01;
        clear_box(collision.translate(box,direction[0]*shift,direction[1]*shift),obstacles,direction,gap,depth+1)
    }
}
fn stack(items, gap, index = 0, placed = []) {
    if (index>=len(items)) placed else {
        let item=items[index];
        let box=if (item.stack==false) item.bounds else clear_box(item.bounds,(placed |: ~.lane==item.lane) |> ~.bounds,item.normal,gap);
        if (box is error) box else stack(items,gap,index+1,[*placed,{*:item,bounds:box,
            card:[(box.left+box.right)/2.0,(box.top+box.bottom)/2.0]}])
    }
}

pub fn dimensions(spec, viewport = null) {
    let vp=sizing.viewport(viewport);
    let requested={width:if (spec.width is number) spec.width else c.option(c.attrs(vp),"width",800),
        height:if (spec.height is number) spec.height else c.option(c.attrs(vp),"height",max(400,len(spec.events)*80))};
    if (vp is error) vp else map([for (key in ["width","height"])
        (key,max(c.option(spec,"min_" ++ key,0),min(requested[key],c.option(spec,"max_" ++ key,requested[key]))))])
}

pub fn normalized(spec, viewport, snapshot = null, resources = {}) {
    let vp=dimensions(spec,viewport);
    let width=vp.width;
    let height=vp.height;
    let padding=spec.padding;
    let skin=theme.tokens(spec,snapshot);
    let font=text.style({font_family:skin.font_family},"label",skin.font_size);
    let domain=window(spec,snapshot);
    let gutter=if (domain is error) domain else axis.gutter(spec,domain,font);
    let box={x:padding.left+gutter,y:padding.top+gutter,width:width-padding.left-padding.right-gutter*2,height:height-padding.top-padding.bottom-gutter*2};
    let path=coordinate.configure({*:spec.coordinate,reverse:(spec.direction=="rtl") != (spec.coordinate.reverse==true)},box,spec.orientation);
    let temporal=if (domain is error) domain else time_map.configure(spec.scale,domain);
    let shown=visible(spec,snapshot);
    let candidates=if (spec.spacing=="time" and not (domain is error)) shown.events |: in_window(~,domain) else shown.events;
    let grouped=lanes.configure(spec,{*:shown,events:candidates},snapshot);
    let labels=[for (item in candidates) plain(item.title)];
    let metrics=text.measure(labels,font);
    let widths=[for (index,item in candidates) max(c.option(spec.cards,"min_width",1),
        min(c.option(spec.cards,"max_width",max(1,box.width)),c.option(spec.cards,"width",max(100,min(240,metrics[index].width+32)))))];
    let card_metrics=if (metrics is error) metrics else card.measure(candidates,spec,widths,vp,snapshot);
    let axes=if (spec.spacing=="time" and not (temporal is error) and not (path is error)) axis.prepare(spec,domain,temporal,path,font,box) else [];
    let annotations=annotation.resolve(spec.annotations,snapshot,spec.scale) |: ~!=null;
    let failure=util.first_error([vp,path,domain,gutter,temporal,shown,metrics,card_metrics,axes,*annotations]);
    if (failure is error) failure else {
        let geometry=[for (index,item in candidates,
            let start=if (item.start!=null) item.start.value else domain.from,
            let end=if (item.end!=null) item.end.value else domain.to,
            let mapped=time_map.project(temporal,start),
            let fraction=if (spec.spacing=="sequence") (if (len(candidates)<2) 0.5 else float(index)/(len(candidates)-1)) else mapped.fraction,
            let lane=lanes.lane(grouped,item,spec.coordinate),
            let offset=if (len(shown.groups)==0) 0.0 else lane.offset,
            let projected=coordinate.project(path,fraction,offset),
            let target=coordinate.project(path,if (spec.spacing=="sequence") fraction else time_map.fraction(temporal,end),offset),
            let sign=if (item.placement=="start" or item.placement=="auto" and index%2==0) -1.0 else 1.0,
            let normal=[for (component in projected.normal) component*sign],
            let card_width=card_metrics[index].width,
            let card_height=card_metrics[index].height,
            let center=[projected.point[0]+normal[0]*(card_width/2.0+24),projected.point[1]+normal[1]*(card_height/2.0+24)],
            let bounds=if (resources.metrics[item.id]!=null) resources.metrics[item.id] else fit_box(box_at(center,card_width,card_height),box))
            {id:item.id,event:item,layout_index:index,point:projected.point,end:target.point,fraction:fraction,normal:normal,tangent:projected.tangent,
                angle:projected.angle,bounds:bounds,card:[(bounds.left+bounds.right)/2.0,(bounds.top+bounds.bottom)/2.0],
                lane:lane.index,offset:offset,stack:lane.stack,metric:metrics[index],hidden:spec.spacing=="time" and mapped.hidden}];
        let clustered=cluster.layout(geometry,path,{*:spec.cluster,locale:spec.locale,messages:spec.messages},snapshot);
        let stacked=if (spec.cards.stack==false) clustered.events else stack(sort(clustered.events,
            {by:(item)=>c.option(item.event,"priority",0),dir:"desc"}),spec.gap);
        let placed=if (stacked is error) stacked else sort(stacked,{by:(item)=>item.layout_index});
        let base={spec:spec,width:width,height:height,box:box,path:path,time_map:temporal,window:domain,domain:extent(spec),
            events:placed,visible_events:candidates,total_events:len(spec.events),offscreen_count:len(shown.events)-len(candidates),
            clusters:clustered.clusters,annotations:annotations,groups:grouped.groups,lanes:grouped.rows,matches:shown.matches,font:font,
            axes:axes,ticks:[for (model in axes) for (tick in model.ticks) tick]};
        let overlays=series.prepare_all(spec,base);
        let value_axes=if (overlays is error) overlays else series.axes(spec,overlays,base);
        if (placed is error) placed else if (overlays is error) overlays else if (value_axes is error) value_axes
        else {*:base,series:overlays,value_axes:value_axes}
    }
}
pub fn layout(spec, viewport, snapshot = null, resources = {}) {
    let validated=schema.validate(spec);
    if (validated is error) validated else normalized(validated,viewport,snapshot,resources)
}

pub fn pick(plan, point, radius = 12) {
    let hits=[for (item in plan.events,
        let inside=point[0]>=item.bounds.left and point[0]<=item.bounds.right and point[1]>=item.bounds.top and point[1]<=item.bounds.bottom,
        let distance=math.sqrt((item.point[0]-point[0])**2+(item.point[1]-point[1])**2)
        where inside or distance<=radius) {id:item.id,distance:if (inside) 0 else distance,event:item.event}];
    sort(hits,{by:(hit)=>hit.distance})[0]
}
