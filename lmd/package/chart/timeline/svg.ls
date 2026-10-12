// standalone SVG keeps rich-content conversion explicit; no foreignObject assumption.
import c: .common
import dates: .date
import layout: .layout
import coordinate: .coordinate
import theme: .theme
import util: lambda.chart.util
import svg: lambda.chart.svg
import text: lambda.chart.text
import time_map: .time_map
import series: .series

fn unsupported(value) => if (value is element or value is map or value is fn) true
    else if (value is array) any(value |> unsupported(~)) else false

fn range_path(plan, item, offset = 0.0) {
    let start=if (item.start!=null) time_map.fraction(plan.time_map,item.start.value) else 0.0;
    let end=if (item.end!=null) time_map.fraction(plan.time_map,item.end.value) else 1.0;
    svg.line_path([for (index in 0 to 64) coordinate.project(plan.path,util.lerp(max(0,start),min(1,end),index/64.0),offset).point])
}
fn label(x,y,value,font,attrs = {}) => <text x:x,y:y,*:text.attributes(font),*:attrs,c.text(value)>
fn lane_path(plan, offset) => svg.line_path([for (index in 0 to 64) coordinate.project(plan.path,index/64.0,offset).point])
fn group_offset(plan, group) {
    let row=(plan.lanes |: ~.group==group)[0];
    if (group==null or row==null) 0.0 else row.offset
}
fn reference(plan, item, skin) {
    let offset=group_offset(plan,item.group);
    let projected=coordinate.project(plan.path,time_map.fraction(plan.time_map,item.start.value),offset);
    let point=projected.point;
    let color=c.option(item,"color",skin.primary);
    let appearance=c.attrs(item.appearance);
    <g class:"lambda-timeline-annotation",'data-timeline-annotation':item.id,'data-chart-key':"annotation-" ++ item.id,
        <title layout.plain(item.title)>
        if (item.kind=="band") <path d:range_path(plan,item,offset),fill:"none",stroke:color,
            'stroke-width':c.option(appearance,"width",32),opacity:c.option(appearance,"opacity",0.2)>
        else if (item.kind=="point") svg.symbol_mark("diamond",point[0],point[1],c.option(appearance,"size",80),{fill:color})
        else if (item.kind!="text") <line x1:point[0]-projected.normal[0]*40,y1:point[1]-projected.normal[1]*40,
            x2:point[0]+projected.normal[0]*40,y2:point[1]+projected.normal[1]*40,stroke:color,
            'stroke-width':c.option(appearance,"width",2),'stroke-dasharray':appearance.dash>
        if (item.title!=null) label(point[0]+projected.normal[0]*48,point[1]+projected.normal[1]*48,
            layout.plain(item.title),plan.font,{fill:color,'text-anchor':"middle"})>
}
fn chronology_axis(plan, model, skin) {
    let cross=min(plan.box.width,plan.box.height);
    <g class:"lambda-timeline-axis",'data-timeline-axis':model.id,'data-chart-key':"axis-" ++ model.id,
        *[for (tick in [*model.ticks,*model.minor]) <g class:"lambda-timeline-tick" ++ (if (tick.major) " lambda-timeline-major-tick" else " lambda-timeline-minor-tick"),
            'data-chart-key':util.binding_key([tick.value,tick.major]),
            if (model.config.grid==true and tick.major) <line class:"lambda-timeline-grid",x1:tick.point[0]-tick.normal[0]*cross/2.0,
                y1:tick.point[1]-tick.normal[1]*cross/2.0,x2:tick.point[0]+tick.normal[0]*cross/2.0,y2:tick.point[1]+tick.normal[1]*cross/2.0,
                stroke:skin.border,opacity:0.5>;
            *[for (side in tick.sides) <line x1:tick.point[0],y1:tick.point[1],
                x2:tick.point[0]+tick.normal[0]*(if (tick.major) 5 else 3)*side,
                y2:tick.point[1]+tick.normal[1]*(if (tick.major) 5 else 3)*side,stroke:skin.muted>]>];
        *[for (entry in model.labels) label(entry.x,entry.y,entry.label,plan.font,
            {class:"lambda-timeline-tick-label",'data-chart-key':"label-" ++ c.text(entry.value),fill:skin.muted,'text-anchor':"middle",
                transform:"rotate(" ++ c.text(entry.angle) ++ " " ++ c.text(entry.x) ++ " " ++ c.text(entry.y) ++ ")"})];
        if (model.title!=null) label(model.title_point[0],model.title_point[1],
            model.title,plan.font,{class:"lambda-timeline-axis-title",fill:skin.text,'text-anchor':"middle"});
        if (model.omitted>0) <desc string(model.omitted) ++ " overlapping axis labels omitted.">
    >
}

pub fn render(plan, snapshot = null, options = {}) {
    let spec=plan.spec;
    let skin=theme.tokens(spec,snapshot);
    let policy=c.text(c.option(options,"unsupported","error"));
    let rich=policy!="omit" and any([for (item in plan.events) unsupported(item.event.content) or len(item.event.media)>0 or item.event.marker!=null or item.event.children!=null]);
    let overlays=[for (model in plan.series) series.render(model)];
    let legends=series.render_legends(plan.series,plan);
    let failure=util.first_error([*overlays,legends,
        if (not contains(["error","text","omit"],policy)) c.fail("export.unsupported","expected error, text, or omit"),
        if (rich and policy=="error") c.fail("export.content","rich content requires an explicit text or omit snapshot policy")]);
    let clip="timeline-plot-" ++ util.binding_key([spec.id,plan.width,plan.height]);
    if (failure is error) failure else svg.svg_root(plan.width,plan.height,[
        <title layout.plain(c.option(spec,"label",spec.title))>,
        <desc layout.plain(spec.description)>,
        <defs <clipPath id:clip,<rect x:plan.box.x,y:plan.box.y,width:plan.box.width,height:plan.box.height>>>,
        <rect x:0,y:0,width:plan.width,height:plan.height,fill:skin.background>,
        <g class:"lambda-timeline-plot",
            *[for (row in plan.lanes where len(plan.groups)>0) <path class:"lambda-timeline-lane",d:lane_path(plan,row.offset),
                'data-timeline-group':row.group,'data-timeline-subgroup':row.subgroup,fill:"none",stroke:skin.border,'stroke-width':1,opacity:0.6>];
            *[for (group in plan.groups,
                let point=coordinate.project(plan.path,0,group.offset).point)
                <g class:"lambda-timeline-group",'data-timeline-group':group.id,
                    'aria-expanded':c.text(not group.collapsed),
                    label(point[0]+4,point[1]-4,layout.plain(group.title),plan.font,{fill:skin.text})>];
            *[for (era in spec.eras) <g class:"lambda-timeline-era",'data-era-id':era.id,
                <path d:range_path(plan,era,group_offset(plan,era.group)),fill:"none",stroke:c.option(era,"color",skin.selected),'stroke-width':c.option(spec.coordinate,"lane_width",40),opacity:0.45>
                <title layout.plain(era.title)>
                label(coordinate.project(plan.path,time_map.fraction(plan.time_map,if (era.start!=null) era.start.value else plan.window.from),group_offset(plan,era.group)).point[0]+4,
                    coordinate.project(plan.path,time_map.fraction(plan.time_map,if (era.start!=null) era.start.value else plan.window.from),group_offset(plan,era.group)).point[1]-12,
                    layout.plain(era.title),plan.font,{fill:skin.muted})>];
            *[for (item in plan.annotations where item.end.value>=plan.window.from and item.start.value<=plan.window.to) reference(plan,item,skin)];
            <path class:"lambda-timeline-track",d:plan.path.path,
                transform:"translate(" ++ plan.path.origin[0] ++ " " ++ plan.path.origin[1] ++ ")",fill:"none",stroke:skin.border,'stroke-width':2>;
            *[for (span in plan.time_map.hidden,
                let point=coordinate.project(plan.path,time_map.fraction(plan.time_map,span.from)).point)
                <g class:"lambda-timeline-hidden-interval",'data-timeline-hidden-from':c.text(span.from),'data-timeline-hidden-to':c.text(span.to),
                    <title c.message(spec,"compressed_interval","Compressed time interval")>
                    <path d:"M " ++ c.text(point[0]-4) ++ " " ++ c.text(point[1]-6) ++ " l 3 12 l 3 -12 l 3 12",fill:"none",stroke:skin.muted>>];
            *[for (model in plan.axes) chronology_axis(plan,model,skin)];
            *overlays;
            *plan.value_axes;
            *[for (item in plan.events,
                let event=item.event,let color=c.option(event,"color",skin.primary),
                let active=snapshot.active_id==item.id,let selected=contains(c.items(snapshot.selected_ids),item.id),
                let card=item.bounds,let title=layout.plain(event.title),
                let fitted=text.fit([title],plan.font,max(1,card.right-card.left-16)))
                <g class:"lambda-timeline-event" ++ (if (active) " lambda-timeline-active" else "") ++ (if (selected) " lambda-timeline-selected" else ""),
                    'data-chart-key':item.id,opacity:c.option(item,"opacity",1.0),
                    *:(if (item.cluster!=true) {'data-timeline-id':item.id} else {'data-timeline-cluster':item.id,
                        *:map([(if (spec.cluster.expand_on=="dblclick") "data-timeline-double-action" else "data-timeline-action",
                            format({type:"cluster_expand",id:item.id,ids:item.ids},'json'))])}),
                    role:"button",tabindex:if (active or item.cluster==true) "0" else "-1",
                    'data-timeline-hidden':if (item.hidden) "true" else null,
                    'aria-label':title ++ " " ++ c.text(event.display_date),'aria-pressed':c.text(selected),
                    <title title ++ " " ++ c.text(event.display_date) ++ (if (item.hidden) " " ++ c.message(spec,"inside_compressed","(inside compressed time interval)") else "")>
                    if (event.kind!="point") <path d:range_path(plan,event,item.offset),fill:"none",stroke:color,'stroke-width':if (event.kind=="background") 32 else 8,opacity:0.5>;
                    *[for (bounds in [event.uncertainty.point,event.uncertainty.start,event.uncertainty.end] where bounds!=null)
                        <g class:"lambda-timeline-uncertainty",
                            <title c.text(bounds.label)>
                            <path d:range_path(plan,bounds,item.offset),fill:"none",stroke:color,'stroke-width':18,opacity:0.2>>];
                    <line x1:item.point[0],y1:item.point[1],x2:item.card[0],y2:item.card[1],stroke:skin.border>
                    if (spec.markers.visible!=false) svg.symbol_mark(c.text(c.option(c.attrs(event.appearance),"shape",c.option(spec.markers,"shape","circle"))),item.point[0],item.point[1],
                        c.option(event,"size",c.option(spec.markers,"size",80)),{fill:c.option(c.attrs(event.appearance),"fill",color),
                            stroke:c.option(c.attrs(event.appearance),"stroke",skin.background),'stroke-width':c.option(c.attrs(event.appearance),"stroke_width",2),
                            opacity:c.option(event,"opacity",1)})
                    if (item.cluster==true and spec.cluster.badge!=false) label(item.point[0],item.point[1]-12,item.count,plan.font,{fill:skin.text,'text-anchor':"middle"})
                    if (options.cards!=false and (spec.cards.visibility!="active" or active)) <g class:"lambda-timeline-card",
                        <rect x:card.left,y:card.top,width:card.right-card.left,height:card.bottom-card.top,rx:skin.radius,
                            fill:if (selected) skin.selected else skin.card,stroke:if (active) skin.primary else skin.border>
                        label(card.left+8,card.top+20,fitted[0].text,plan.font,{fill:skin.text})
                        label(card.left+8,card.top+38,event.display_date,plan.font,{fill:skin.muted})
                        if (policy=="text" and event.content!=null) label(card.left+8,card.top+56,
                            text.fit([layout.plain(event.content)],plan.font,max(1,card.right-card.left-16))[0].text,plan.font,{fill:skin.text})
                    >
                >]
        >,
        legends
    ],{role:"img",'aria-label':layout.plain(c.option(spec,"label",spec.title))})
}
